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
#include "ethstream.h"

#define NERDJACK_TIMEOUT 5   /* Timeout for connect/send/recv, in seconds */

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns -1 if no valid config found */
int nerdjack_choose_scan(double desired_rate, double *actual_rate, int *period)
{
    
	*period = floor((double) NERDJACK_CLOCK_RATE / desired_rate);
    if(*period > UINT16_MAX) {
        info("Cannot sample that slowly\n");
        return -1;
    }
    *actual_rate = (double) NERDJACK_CLOCK_RATE / (double) *period;
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
      verb("Error Creating Socket\n");
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
     info("Error sending packet: %s\n", strerror(errno) );
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

typedef struct {
int numCopies;
int * destlist;
} deststruct;

int nerd_send_command(const char * address, char * command)
{
    int ret,fd_command;
    char buf[3];
    fd_command = nerd_open(address, NERDJACK_COMMAND_PORT);
    if (fd_command < 0) {
        info("Connect failed: %s:%d\n", address, NERDJACK_COMMAND_PORT);
        return -2; 
    }

    /* Send request */
	ret = send_all_timeout(fd_command, command, strlen(command), 0, 
			       & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT });
	if (ret < 0 || ret != strlen(command)) {
		verb("short send %d\n", (int)ret);
		return -1;
	}

    ret = recv_all_timeout(fd_command,buf,3,0,
           & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT });

    nerd_close_conn(fd_command);

    if (ret < 0 || ret != 3) {
        verb("Error receiving OK for command\n");
        return -1;
     }

    if (0 != strcmp("OK",buf)){
        verb("Did not receive OK.  Received %s\n",buf);
        return -3;
    }

    return 0;
}

static int nerd_init_channels(deststruct * destination, int numChannels, int *channel_list) {

    int channels_left = numChannels;
    int channelprocessing = 0;
    int currentalign = 0; //Index into sampled channels
    int i;
    int numDuplicates = 0;

    int tempdestlist[NERDJACK_CHANNELS];
    
    //Loop through channel_list until all channels recognized
    //start with channelprocessing = 0 and increment through channels.
    //If a channel is found in the list set it up appropriately.
    //The complication arises because we want to allow a channel to
    //display more than once or even out of order
    //We need to distill a duplicate-free list of channels in order to
    //sample as well as a mapping of how to print those channels
    //to screen.
    do {
        destination[currentalign].numCopies = 0;
        for(i = 0; i < numChannels; i++) {
            if(channelprocessing == channel_list[i]) {
                tempdestlist[destination[currentalign].numCopies] = i;
                if(destination[currentalign].numCopies > 0) {
                    numDuplicates++;
                }
                destination[currentalign].numCopies++;
                channels_left--;
            }
        }
        
        if(destination[currentalign].numCopies > 0) {
            destination[currentalign].destlist = malloc( destination[currentalign].numCopies * sizeof(int) );
            memcpy(destination[currentalign].destlist, tempdestlist, destination[currentalign].numCopies * sizeof(int));
            currentalign++;
        }
        channelprocessing++;
    } while(channels_left > 0);

return numDuplicates;

}

int nerd_data_stream(int data_fd, int numChannels, int *channel_list, int precision, int convert, int lines, int showmem, unsigned short * currentcount)
{
    //Variables that should persist across retries
    static unsigned char buf[NERDJACK_PACKET_SIZE];
    //static int charsleft = NERDJACK_PACKET_SIZE;
    static int linesleft = 0;

    int index = 0;
    int alignment = 0;
    signed short datapoint = 0;
    unsigned short dataline[NERDJACK_CHANNELS];
    long double voltline[NERDJACK_CHANNELS];
    int i;
    deststruct destination[NERDJACK_CHANNELS];

    
    unsigned long memused = 0;
	unsigned short packetsready = 0;
	unsigned short adcused = 0;
	unsigned short tempshort = 0;
    int charsread = 0;

	int numgroups = 0;
	long double volts;
    
    //Check to see if we're trying to resume
    //Don't blow away linesleft in that case
    if(lines != 0 && linesleft == 0) {
        linesleft = lines;
    }
    
    
    int numDuplicates = nerd_init_channels(destination,numChannels, channel_list);
    
    //Now destination structure array is set as well as numDuplicates.

    int numChannelsSampled = numChannels - numDuplicates;
    int numGroups = NERDJACK_NUM_SAMPLES / numChannelsSampled;


	//Loop forever to grab data
    while((charsread = recv_all_timeout(data_fd,buf,NERDJACK_PACKET_SIZE,0,
           & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT }))){

		//We want a complete packet, so take the chars so far and keep waiting
		if(charsread != NERDJACK_PACKET_SIZE) {
            //charsleft = NERDJACK_PACKET_SIZE - charsread;
            //There was a problem getting data.  Probably a closed
            //connection. Stash the data we did get, save state, and hope
            //to get it later

		    info("Packet was too short\n");
	            return -2;
            /*
			charsleft = NERDJACK_PACKET_SIZE - charsread;
			while(charsleft != 0){
				additionalread = recv_all_timeout(data_fd,buf+charsread,charsleft,0,
                     & (struct timeval) { .tv_sec = NERDJACK_TIMEOUT });
				charsread = charsread + additionalread;
				charsleft = NERDJACK_PACKET_SIZE - charsread;
			}
            */
		}
        
        //charsleft = NERDJACK_PACKET_SIZE;

		//First check the header info
		if(buf[0] != 0xF0 || buf[1] != 0xAA) {
			info("No Header info\n");
			return -1;
		}

		//Check counter info to make sure not out of order
		tempshort = (buf[2] << 8) | buf[3];
		if(tempshort != *currentcount ){
			info("Count wrong. Expected %hd but got %hd\n", *currentcount, tempshort);
			return -1;
		}
        
        //Increment number of packets received
		*currentcount = *currentcount + 1;
        
        //Process the rest of the header and update the index value to be pointing after it
		index = 12;
		memused = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | (buf[7]);
		adcused = (buf[8] << 8) | (buf[9]);
		packetsready = (buf[10] << 8) | (buf[11]);
		alignment = 0;
		numgroups = 0;

        if(showmem) {
            printf("%lX %hd %hd\n",memused, adcused, packetsready);
            continue;
        }
        
        //While there is still more data in the packet, process it
		while(charsread > index) {
			datapoint = (buf[index] << 8 | buf[index+1]);
            switch(convert) {
            case CONVERT_VOLTS:
				if(alignment <= 5) {
					volts = (long double) ( datapoint / 32767.0 ) * ((precision & 0x01) ? 5.0 : 10.0);
				} else {
					volts = (long double) (datapoint / 32767.0 ) * ((precision & 0x02) ? 5.0 : 10.0);
				}
                for(i = 0; i < destination[alignment].numCopies; i++) {
                    voltline[destination[alignment].destlist[i]] = volts;
                }
                break;
            default:
            case CONVERT_HEX:
            case CONVERT_DEC:
                for(i = 0; i < destination[alignment].numCopies; i++) {
                    dataline[destination[alignment].destlist[i]] = 
                        (unsigned short) (datapoint - INT16_MIN);
                }
                break;
			}
            
            //Each point is two bytes, so increment index and total bytes read
			index++;
			index++;
            alignment++;
			//totalread++;
            
            
			
            
            //Since channel data is packed, we need to know when to insert a newline
			if(alignment == numChannelsSampled){
                switch(convert) {
                case CONVERT_VOLTS:
                    for(i = 0; i < numChannels; i++) {
                        if (printf("%Lf ",voltline[i]) < 0)
                            goto bad;
                    }
                    break;
                case CONVERT_HEX:
                    for(i = 0; i < numChannels; i++) {
                        if (printf("%04hX",dataline[i]) < 0)
                            goto bad;
                    }
                    break;
                default:
                case CONVERT_DEC:
                    for(i = 0; i < numChannels; i++) {
                        if (printf("%hu ",dataline[i]) < 0)
                            goto bad;
                    }
                    break;
                }
				if(printf("\n") < 0)
                    goto bad;
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

bad:
    info("Output error (disk full?)\n");
    return -1;

}

int nerd_open(const char *address,int port) {
    
    struct hostent *he;
    
    net_init();
    
    int32_t i32SocketFD = socket(PF_INET, SOCK_STREAM, 0);

    if(-1 == i32SocketFD)
    {
      verb("cannot create socket");
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

    sprintf(command,"GETD%3.3X%d%5.5d", channelbit,precision,period);
    
    return 0;
    
}

int nerd_close_conn(int data_fd)
{
	shutdown(data_fd, 2);
	close(data_fd);
	return 0;
}
