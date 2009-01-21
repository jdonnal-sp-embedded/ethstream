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
#include <sys/types.h>
#include <math.h>

#include "netutil.h"
#include "compat.h"

#include "debug.h"
#include "nerdjack.h"
#include "util.h"
#include "netutil.h"

#define NERDJACK_TIMEOUT 5   /* Timeout for connect/send/recv, in seconds */

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns -1 if no valid config found */
int nerdjack_choose_scan(double desired_rate, double *actual_rate, int *period)
{
    
	*period = round((double) NERDJACK_CLOCK_RATE / desired_rate);
    * actual_rate = (double) NERDJACK_CLOCK_RATE / (double) *period;
	if(*actual_rate != desired_rate) {
		return -1;
	}
    return 0;
}

int nerdjack_detect(char * ipAddress) {
   int32_t sock, receivesock;
   struct sockaddr_in sa, receiveaddr, sFromAddr;
   int bytes_sent, buffer_length;
   char buffer[200];
   char incomingData[10];
   unsigned int lFromLen;

   sprintf(buffer, "TEST");
   buffer_length = strlen(buffer) + 1;
   
   net_init();

   sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
   receivesock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
   
   	/* Set nonblocking */
   if (soblock(sock, 0) < 0) {
      verb("can't set nonblocking\n");
      return -1;
   }
   
    /* Set nonblocking */
   if (soblock(receivesock, 0) < 0) {
      verb("can't set nonblocking\n");
      return -1;
   }


   int opt = 1;
   setsockopt(sock,SOL_SOCKET,SO_BROADCAST,(void *) &opt,sizeof(int));
   
   if((-1 == sock) || (-1 == receivesock)) /* if socket failed to initialize, exit */
   {
      printf("Error Creating Socket\n");
      return -1;
   }

   //Setup family for both sockets
   sa.sin_family = PF_INET;
   receiveaddr.sin_family = PF_INET;
   
   //Setup ports to send on DATA and receive on RECEIVE
   receiveaddr.sin_port = htons(NERDJACK_UDP_RECEIVE_PORT);
   sa.sin_port = htons(NERDJACK_DATA_PORT);
   
   //Receive from any IP address, Will send to broadcast
   receiveaddr.sin_addr.s_addr = INADDR_ANY;
   sa.sin_addr.s_addr = INADDR_BROADCAST;

   bind(receivesock,(struct sockaddr*) &receiveaddr, sizeof(struct sockaddr_in));

   bytes_sent = sendto(sock, buffer, buffer_length, 0,(struct sockaddr*) &sa, sizeof(struct sockaddr_in) );
   if(bytes_sent < 0) {
     printf("Error sending packet: %s\n", strerror(errno) );
	 return -1;
	}

   lFromLen = sizeof(sFromAddr);
   
   if(0 > recvfrom_timeout(receivesock, incomingData, sizeof(incomingData),0,(struct sockaddr *) &sFromAddr, &lFromLen,
                    & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT })) {
    
        return -1;
    }
   
   ipAddress = malloc(INET_ADDRSTRLEN);
   
   //It isn't ipv6 friendly, but inet_ntop isn't on Windows...
   strcpy(ipAddress, inet_ntoa(sFromAddr.sin_addr));

   close(sock); /* close the socket */
   close(receivesock);
   return 0;
}

int nerd_data_stream(int data_fd, char * command, int numChannels, int *channel_list, int precision, int convert, int lines)
{
	unsigned char buf[NERDJACK_PACKET_SIZE];

	int numGroups = NERDJACK_NUM_SAMPLES / numChannels;

    int index = 0;
    int totalread = 0;
    int ret = 0;
    int alignment = 0;
    signed short datapoint = 0;
    signed short dataline[NERDJACK_CHANNELS];
    long double voltline[NERDJACK_CHANNELS];
    int destination[NERDJACK_CHANNELS];
    unsigned short currentcount = 0;
    unsigned long memused = 0;
	unsigned short packetsready = 0;
	unsigned short adcused = 0;
	unsigned short tempshort = 0;
    int charsread = 0;
    int charsleft = 0;
    int additionalread = 0;
    int linesleft = lines;

	int numgroups = 0;
	long double volts;
    
    int channels_left = numChannels;
    int channelprocessing = 0;
    int currentalign = 0;
    int i;
    
    //Loop through channel_list until all channels recognized
    //destination holds the index where each channel should go for reordering
    do {
        for(i = 0; i < numChannels; i++) {
            if(channelprocessing == channel_list[i]) {
                destination[currentalign] = i;
                currentalign++;
                channels_left--;
                break;
            }
        }
        channelprocessing++;
    } while(channels_left > 0);

    /* Send request */
	ret = send_all_timeout(data_fd, command, strlen(command), 0, 
			       & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT });
	if (ret < 0 || ret != strlen(command)) {
		verb("short send %d\n", (int)ret);
		return -1;
	}

	//Loop forever to grab data
    while((charsread = recv_all_timeout(data_fd,buf,NERDJACK_PACKET_SIZE,0,
           & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT }))){

		//We want a complete packet, so take the chars so far and keep waiting
		if(charsread != NERDJACK_PACKET_SIZE) {
			charsleft = NERDJACK_PACKET_SIZE - charsread;
			while(charsleft != 0){
				additionalread = recv_all_timeout(data_fd,buf+charsread,charsleft,0,
                     & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT });
				charsread = charsread + additionalread;
				charsleft = NERDJACK_PACKET_SIZE - charsread;
			}
		}

		//First check the header info
		if(buf[0] != 0xF0 || buf[1] != 0xAA) {
			printf("No Header info\n");
			return -1;
		}

		//Check counter info to make sure not out of order
		tempshort = (buf[2] << 8) | buf[3];
		if(tempshort != currentcount ){
			printf("Count wrong. Expected %hd but got %hd\n", currentcount, tempshort);
			return -1;
		}
        
        //Increment number of packets received
		currentcount++;
        
        //Process the rest of the header and update the index value to be pointing after it
		index = 12;
		memused = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | (buf[7]);
		adcused = (buf[8] << 8) | (buf[9]);
		packetsready = (buf[10] << 8) | (buf[11]);
		alignment = 0;
		numgroups = 0;
        
        //While there is still more data in the packet, process it
		while(charsread > index) {
			datapoint = (buf[index] << 8 | buf[index+1]);
			if(convert) {
				if(alignment <= 5) {
					volts = (long double) ( datapoint / 32767.0 ) * ((precision & 0x01) ? 5.0 : 10.0);
				} else {
					volts = (long double) (datapoint / 32767.0 ) * ((precision & 0x02) ? 5.0 : 10.0);
				}
                voltline[destination[alignment]] = volts;
			} else {
                dataline[destination[alignment]] = datapoint;
			}
            
            //Each point is two bytes, so increment index and total bytes read
			index++;
			index++;
			totalread++;
			alignment++;
            
            //Since channel data is packed, we need to know when to insert a newline
			if(alignment == numChannels){
                if(convert) {
                    for(i = 0; i < numChannels; i++) {
                        printf("%Lf ",voltline[i]);
                    }
                } else {
                    for(i = 0; i < numChannels; i++) {
                        printf("%hd ",dataline[i]);
                    }
                }
				printf("\n");
				alignment = 0;
				numgroups++;
                if(lines != 0) {
                    linesleft--;
                    if(linesleft == 0) {
                        return 0;
                    }
                }
                //If numgroups so far is equal to the numGroups in a packet, this packet is done
				if(numgroups == numGroups) {
					break;
				}
			}
		}
		index = 0;
	}

	return 0;

}

int nerd_open(const char *address,int port) {
    
    struct hostent *he;
    
    net_init();
    
    int32_t i32SocketFD = socket(PF_INET, SOCK_STREAM, 0);

    if(-1 == i32SocketFD)
    {
      printf("cannot create socket");
      return -1;
    }
    
    /* Set nonblocking */
	if (soblock(i32SocketFD, 0) < 0) {
		verb("can't set nonblocking\n");
		return -1;
	}

    struct sockaddr_in stSockAddr;
    memset(&stSockAddr, 0, sizeof(stSockAddr));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(port);
    
    he = gethostbyname(address);
	if (he == NULL) {
		verb("gethostbyname(\"%s\") failed\n", address);
		return -1;
	}
	stSockAddr.sin_addr = *((struct in_addr *) he->h_addr);

	debug("Resolved %s -> %s\n", address, inet_ntoa(stSockAddr.sin_addr));
    
    /* Connect */
	if (connect_timeout(i32SocketFD, (struct sockaddr *) &stSockAddr, sizeof(stSockAddr),
			    & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT }) < 0) {
		verb("connection to %s:%d failed: %s\n",
		     inet_ntoa(stSockAddr.sin_addr), port, compat_strerror(errno));
		return -1;
	}
    
	return i32SocketFD;
}

int nerd_generate_command(char * command, int * channel_list, int channel_count, int precision,
    unsigned short period) {
    
    int channelbit = 0;
    int i;
    
    for( i = 0; i < channel_count; i++) {
        channelbit = channelbit | (0x1 << channel_list[i]);
    }

    sprintf(command,"GET%3.3X%d%5.5d", channelbit,precision,period);
    
    return 0;
    
}

int nerd_close_conn(int data_fd)
{
	shutdown(data_fd, 2);
	close(data_fd);
	return 0;
}
