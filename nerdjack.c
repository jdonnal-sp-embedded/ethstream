/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <math.h>

#include "netutil.h"
#include "compat.h"

#include "debug.h"
#include "nerdjack.h"
#include "util.h"
#include "netutil.h"
#include "ethstream.h"

#define NERDJACK_TIMEOUT 5	/* Timeout for connect/send/recv, in seconds */

#define NERD_HEADER_SIZE 8
#define MAX_SOCKETS 32

typedef struct __attribute__ ((__packed__))
{
  unsigned char headerone;
  unsigned char headertwo;
  unsigned short packetNumber;
  unsigned short adcused;
  unsigned short packetsready;
  signed short data[NERDJACK_NUM_SAMPLES];
} dataPacket;

struct discovered_socket {
	int sock;
	uint32_t local_ip;
	uint32_t subnet_mask;
};

struct discover_t {
	struct discovered_socket socks[MAX_SOCKETS];
	unsigned int sock_count;
};

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns -1 if no valid config found */
int
nerdjack_choose_scan (double desired_rate, double *actual_rate,
		      unsigned long *period)
{
  //The ffffe is because of a silicon bug.  The last bit is unusable in all
  //devices so far.  It is worked around on the chip, but giving it exactly
  //0xfffff would cause the workaround code to roll over.
  *period = floor ((double) NERDJACK_CLOCK_RATE / desired_rate);
  if (*period > 0x0ffffe)
    {
      info ("Cannot sample that slowly\n");
      *actual_rate = (double) NERDJACK_CLOCK_RATE / (double) 0x0ffffe;
      *period = 0x0ffffe;
      return -1;
    }
  //Period holds the period register for the NerdJack, so it needs to be right
  *actual_rate = (double) NERDJACK_CLOCK_RATE / (double) *period;
  if (*actual_rate != desired_rate)
    {
      return -1;
    }
  return 0;
}

/**
* Create a discovered socket and add it to the socket list structure.
* All sockets in the structure should be created, bound, and ready for broadcasting
*/
static int discovered_sock_create(struct discover_t *ds, uint32_t local_ip, uint32_t subnet_mask)
{
	if (ds->sock_count >= MAX_SOCKETS) {
		return 0;
	}

	/* Create socket. */
	int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		return 0;
	}

	/* Allow broadcast. */
	int sock_opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&sock_opt, sizeof(sock_opt));

    /* Set nonblocking */
    if (soblock (sock, 0) < 0)
    {
      verb ("can't set nonblocking\n");
      return 0;
    }

	/* Bind socket. */
	struct sockaddr_in sock_addr;
	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_addr.s_addr = htonl(local_ip);
	sock_addr.sin_port = htons(0);
	if (bind(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) != 0) {
		close(sock);
		return 0;
	}

	/* Write sock entry. */
	struct discovered_socket *dss = &ds->socks[ds->sock_count++];
	dss->sock = sock;
	dss->local_ip = local_ip;
	dss->subnet_mask = subnet_mask;

	return 1;
}
/**
 * Enumerate all interfaces we can find and open sockets on each
 */
 #if defined(USE_IPHLPAPI)
static void enumerate_interfaces(struct discover_t *ds)
{
	PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
	ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);

	DWORD Ret = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
	if (Ret != NO_ERROR) {
		free(pAdapterInfo);
		if (Ret != ERROR_BUFFER_OVERFLOW) {
			return;
		}
		pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen); 
		Ret = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
		if (Ret != NO_ERROR) {
			free(pAdapterInfo);
			return;
		}
	}

	PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
	while (pAdapter) {
		IP_ADDR_STRING *pIPAddr = &pAdapter->IpAddressList;
		while (pIPAddr) {
			uint32_t local_ip = ntohl(inet_addr(pIPAddr->IpAddress.String));
			uint32_t mask = ntohl(inet_addr(pIPAddr->IpMask.String));

			if (local_ip == 0) {
				pIPAddr = pIPAddr->Next;
				continue;
			}

			discovered_sock_create(ds, local_ip, mask);
			pIPAddr = pIPAddr->Next;
		}

		pAdapter = pAdapter->Next;
	}

	free(pAdapterInfo);
}

#else
static void enumerate_interfaces(struct discover_t *ds) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
		return;
	}

	struct ifconf ifc;
	uint8_t buf[8192];
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = (char *)buf;

	memset(buf, 0, sizeof(buf));

	if (ioctl(fd, SIOCGIFCONF, &ifc) != 0) {
		close(fd);
		return;
	}

	uint8_t *ptr = (uint8_t *)ifc.ifc_req;
	uint8_t	*end = (uint8_t *)&ifc.ifc_buf[ifc.ifc_len];

	while (ptr <= end) {
		struct ifreq *ifr = (struct ifreq *)ptr;
		ptr += _SIZEOF_ADDR_IFREQ(*ifr);

		if (ioctl(fd, SIOCGIFADDR, ifr) != 0) {
			continue;
		}
		struct sockaddr_in *addr_in = (struct sockaddr_in *)&(ifr->ifr_addr);
		uint32_t local_ip = ntohl(addr_in->sin_addr.s_addr);
		if (local_ip == 0) {
			continue;
		}

		if (ioctl(fd, SIOCGIFNETMASK, ifr) != 0) {
			continue;
		}

		struct sockaddr_in *mask_in = (struct sockaddr_in *)&(ifr->ifr_addr);
		uint32_t mask = ntohl(mask_in->sin_addr.s_addr);

		discovered_sock_create(ds, local_ip, mask);
	}
}
#endif
/**
 * Close all sockets previously enumerated and free the struct
 */
static void destroy_socks(struct discover_t *ds)
{
	unsigned int i;
	for (i = 0; i < ds->sock_count; i++) {
		struct discovered_socket *dss = &ds->socks[i];
		close(dss->sock);
	}

	free(ds);
}


/* Perform autodetection.  Returns 0 on success, -1 on error
 * Sets ipAddress to the detected address
 */
int
nerdjack_detect (char *ipAddress)
{
  int32_t receivesock;
  struct sockaddr_in sa, receiveaddr, sFromAddr;
  int buffer_length;
  char buffer[200];
  char incomingData[10];
  unsigned int lFromLen;

  sprintf (buffer, "TEST");
  buffer_length = strlen (buffer) + 1;

  net_init ();
    
  receivesock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  /* Set nonblocking */
  if (soblock (receivesock, 0) < 0)
    {
      verb ("can't set nonblocking\n");
      return -1;
    }

  if (-1 == receivesock)	/* if socket failed to initialize, exit */
    {
      verb ("Error Creating Socket\n");
      return -1;
    }

  //Setup family for both sockets
  sa.sin_family = PF_INET;
  receiveaddr.sin_family = PF_INET;

  //Setup ports to send on DATA and receive on RECEIVE
  receiveaddr.sin_port = htons (NERDJACK_UDP_RECEIVE_PORT);
  sa.sin_port = htons (NERDJACK_DATA_PORT);

  //Receive from any IP address
  receiveaddr.sin_addr.s_addr = INADDR_ANY;

  bind (receivesock, (struct sockaddr *) &receiveaddr,
	sizeof (struct sockaddr_in));
    
    struct discover_t *ds = (struct discover_t *)calloc(1, sizeof(struct discover_t));
	if (!ds) {
		return -1;
	}

	/* Create a routable broadcast socket. */
	if (!discovered_sock_create(ds, 0, 0)) {
		free(ds);
		return -1;
	}

	/* Detect & create local sockets. */
	enumerate_interfaces(ds);

	/*
	 * Send subnet broadcast using each local ip socket.
	 * This will work with multiple separate 169.254.x.x interfaces.
	 */
	unsigned int i;
	for (i = 0; i < ds->sock_count; i++) {
		struct discovered_socket *dss = &ds->socks[i];
		uint32_t target_ip = dss->local_ip | ~dss->subnet_mask;
        sa.sin_addr.s_addr = htonl(target_ip);
        sendto (dss->sock, buffer, buffer_length, 0, (struct sockaddr *) &sa,
	      sizeof (struct sockaddr_in));
	}

    destroy_socks(ds);

  lFromLen = sizeof (sFromAddr);

  if (0 >
      recvfrom_timeout (receivesock, incomingData, sizeof (incomingData), 0,
			(struct sockaddr *) &sFromAddr, &lFromLen,
			&(struct timeval)
			{
			.tv_sec = NERDJACK_TIMEOUT}))
    {
      close(receivesock);
      return -1;
    }

  ipAddress = malloc (INET_ADDRSTRLEN);

  //It isn't ipv6 friendly, but inet_ntop isn't on Windows...
  strcpy (ipAddress, inet_ntoa (sFromAddr.sin_addr));

  close (receivesock);
  return 0;
}

/* Send the given command to address.  The command should be something
 * of the specified length.  This expects the NerdJack to reply with OK
 * or NO
 */
int
nerd_send_command (const char *address, void *command, int length)
{
  int ret, fd_command;
  char buf[3];
  fd_command = nerd_open (address, NERDJACK_COMMAND_PORT);
  if (fd_command < 0)
    {
      info ("Connect failed: %s:%d\n", address, NERDJACK_COMMAND_PORT);
      return -2;
    }

  /* Send request */
  ret = send_all_timeout (fd_command, command, length, 0, &(struct timeval)
			  {
			  .tv_sec = NERDJACK_TIMEOUT});
  if (ret < 0 || ret != length)
    {
      verb ("short send %d\n", (int) ret);
      return -1;
    }

  ret = recv_all_timeout (fd_command, buf, 3, 0, &(struct timeval)
			  {
			  .tv_sec = NERDJACK_TIMEOUT});

  nerd_close_conn (fd_command);

  if (ret < 0 || ret != 3)
    {
      verb ("Error receiving OK for command\n");
      return -1;
    }

  if (0 != strcmp ("OK", buf))
    {
      verb ("Did not receive OK.  Received %s\n", buf);
      return -4;
    }

  return 0;
}


int
nerd_data_stream (int data_fd, int numChannels, int *channel_list,
		  int precision, int convert, int lines, int showmem,
		  unsigned short *currentcount, unsigned int period,
		  int wasreset)
{
  //Variables that should persist across retries
  static dataPacket buf;
  static int linesleft = 0;
  static int linesdumped = 0;

  //Variables essential to packet processing
  signed short datapoint = 0;
  int i;

  int numChannelsSampled = channel_list[0] + 1;

  //The number sampled will be the highest channel requested plus 1
  //(i.e. channel 0 requested means 1 sampled)
  for (i = 0; i < numChannels; i++)
    {
      if (channel_list[i] + 1 > numChannelsSampled)
	numChannelsSampled = channel_list[i] + 1;
    }


  double voltline[numChannels];

  unsigned short dataline[numChannels];

  unsigned short packetsready = 0;
  unsigned short adcused = 0;
  unsigned short tempshort = 0;
  int charsread = 0;

  int numgroupsProcessed = 0;
  double volts;

  //The timeout should be the expected time plus 60 seconds
  //This permits slower speeds to work properly
  unsigned int expectedtimeout =
    (period * NERDJACK_NUM_SAMPLES / NERDJACK_CLOCK_RATE) + 60;

  //Check to see if we're trying to resume
  //Don't blow away linesleft in that case
  if (lines != 0 && linesleft == 0)
    {
      linesleft = lines;
    }

  //If there was a reset, we still need to dump a line because of faulty PDCA start
  if (wasreset)
    {
      linesdumped = 0;
    }

  //If this is the first time called, warn the user if we're too fast
  if (linesdumped == 0)
    {
      if (period < (numChannelsSampled * 200 + 600))
	{
	  info ("You are sampling close to the limit of NerdJack\n");
	  info ("Sample fewer channels or sample slower\n");
	}
    }

  //Now destination structure array is set as well as numDuplicates.

  int totalGroups = NERDJACK_NUM_SAMPLES / numChannelsSampled;


  //Loop forever to grab data
  while ((charsread =
	  recv_all_timeout (data_fd, &buf, NERDJACK_PACKET_SIZE, 0,
			    &(struct timeval)
			    {
			    .tv_sec = expectedtimeout})))
    {

      if (charsread != NERDJACK_PACKET_SIZE)
	{
	  //There was a problem getting data.  Probably a closed
	  //connection.
	  info ("Packet timed out or was too short\n");
	  return -2;
	}

      //First check the header info
      if (buf.headerone != 0xF0 || buf.headertwo != 0xAA)
	{
	  info ("No Header info\n");
	  return -1;
	}

      //Check counter info to make sure not out of order
      tempshort = ntohs (buf.packetNumber);
      if (tempshort != *currentcount)
	{
	  info ("Count wrong. Expected %hd but got %hd\n", *currentcount,
		tempshort);
	  return -1;
	}

      //Increment number of packets received
      *currentcount = *currentcount + 1;

      adcused = ntohs (buf.adcused);
      packetsready = ntohs (buf.packetsready);
      numgroupsProcessed = 0;

      if (showmem)
	{
	  printf ("%hd %hd\n", adcused, packetsready);
	  continue;
	}

      //While there is still more data in the packet, process it
      while (numgroupsProcessed < totalGroups)
	{
	  //Poison the data structure
	  switch (convert)
	    {
	    case CONVERT_VOLTS:
	      memset (voltline, 0, numChannels * sizeof (double));
	      break;
	    default:
	    case CONVERT_HEX:
	    case CONVERT_DEC:
	      memset (dataline, 0, numChannels * sizeof (unsigned char));
	    }

	  //Read in each group
	  for (i = 0; i < numChannels; i++)
	    {
	      //Get the datapoint associated with the desired channel
	      datapoint =
		ntohs (buf.
		       data[channel_list[i] +
			    numgroupsProcessed * numChannelsSampled]);

	      //Place it into the line
	      switch (convert)
		{
		case CONVERT_VOLTS:
		  if (channel_list[i] <= 5)
		    {
		      volts =
			(double) (datapoint / 32767.0) *
			((precision & 0x01) ? 5.0 : 10.0);
		    }
		  else
		    {
		      volts =
			(double) (datapoint / 32767.0) *
			((precision & 0x02) ? 5.0 : 10.0);
		    }
		  voltline[i] = volts;
		  break;
		default:
		case CONVERT_HEX:
		case CONVERT_DEC:
		  dataline[i] = (unsigned short) (datapoint - INT16_MIN);
		  break;
		}
	    }
	  //We want to dump the first line because it's usually spurious
	  if (linesdumped != 0)
	    {
	      //Now print the group
	      switch (convert)
		{
		case CONVERT_VOLTS:
		  for (i = 0; i < numChannels; i++)
		    {
		      if (printf ("%lf ", voltline[i]) < 0)
			goto bad;
		    }
		  break;
		case CONVERT_HEX:
		  for (i = 0; i < numChannels; i++)
		    {
		      if (printf ("%04hX", dataline[i]) < 0)
			goto bad;
		    }
		  break;
		default:
		case CONVERT_DEC:
		  for (i = 0; i < numChannels; i++)
		    {
		      if (printf ("%hu ", dataline[i]) < 0)
			goto bad;
		    }
		  break;
		}
	      if (printf ("\n") < 0)
		goto bad;

	      //If we're counting lines, decrement them
	      if (lines != 0)
		{
		  linesleft--;
		  if (linesleft == 0)
		    {
		      return 0;
		    }
		}

	    }
	  else
	    {
	      linesdumped = linesdumped + 1;
	    }

	  //We've processed this group, so advance the counter
	  numgroupsProcessed++;

	}
    }

  return 0;

bad:
  info ("Output error (disk full?)\n");
  return -3;

}

/* Open a connection to the NerdJack */
int
nerd_open (const char *address, int port)
{

  struct hostent *he;

  net_init ();

  int32_t i32SocketFD = socket (PF_INET, SOCK_STREAM, 0);

  if (-1 == i32SocketFD)
    {
      verb ("cannot create socket");
      return -1;
    }

  /* Set nonblocking */
  if (soblock (i32SocketFD, 0) < 0)
    {
      verb ("can't set nonblocking\n");
      return -1;
    }

  struct sockaddr_in stSockAddr;
  memset (&stSockAddr, 0, sizeof (stSockAddr));

  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons (port);

  he = gethostbyname (address);
  if (he == NULL)
    {
      verb ("gethostbyname(\"%s\") failed\n", address);
      return -1;
    }
  stSockAddr.sin_addr = *((struct in_addr *) he->h_addr);

  debug ("Resolved %s -> %s\n", address, inet_ntoa (stSockAddr.sin_addr));

  /* Connect */
  if (connect_timeout
      (i32SocketFD, (struct sockaddr *) &stSockAddr, sizeof (stSockAddr),
       &(struct timeval)
       {
       .tv_sec = 3}) < 0)
    {
      verb ("connection to %s:%d failed: %s\n",
	    inet_ntoa (stSockAddr.sin_addr), port, compat_strerror (errno));
      return -1;
    }

  return i32SocketFD;
}

//Generate an appropriate sample initiation command
int
nerd_generate_command (getPacket * command, int *channel_list,
		       int channel_count, int precision, unsigned long period)
{

  short channelbit = 0;
  int i;
  int highestchannel = 0;

  for (i = 0; i < channel_count; i++)
    {
      if (channel_list[i] > highestchannel)
	{
	  highestchannel = channel_list[i];
	}
      //channelbit = channelbit | (0x1 << channel_list[i]);
    }

  for (i = 0; i <= highestchannel; i++)
    {
      channelbit = channelbit | (0x01 << i);
    }

  command->word[0] = 'G';
  command->word[1] = 'E';
  command->word[2] = 'T';
  command->word[3] = 'D';
  command->channelbit = htons (channelbit);
  command->precision = precision;
  command->period = htonl (period);
  command->prescaler = 0;

  return 0;

}

int
nerd_close_conn (int data_fd)
{
  shutdown (data_fd, 2);
  close (data_fd);
  return 0;
}
