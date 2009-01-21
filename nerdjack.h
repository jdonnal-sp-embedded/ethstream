/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#ifndef NERDJACK_H
#define NERDJACK_H

#include <stdint.h>
#include <stdlib.h>

#include "netutil.h"

#define NERDJACK_CHANNELS 12
#define NERDJACK_CLOCK_RATE 54000000
#define NERDJACK_DATA_PORT 49155
#define NERDJACK_UDP_RECEIVE_PORT 49156

#define NERDJACK_PACKET_SIZE 1460
#define NERDJACK_NUM_SAMPLES 724

/* Open/close TCP/IP connection to the NerdJack */
int nerd_open(const char *address,int port);
int nerd_close_conn(int data_fd);

/* Generate the command word for the NerdJack */
int nerd_generate_command(char * command, int * channel_list, int channel_count, int precision,
    unsigned short period);

/* Stream data out of the NerdJack */
int nerd_data_stream(int data_fd, char * command, int numChannels, int * channel_list, int precision, int convert, int lines);

/* Detect the IP Address of the NerdJack and return in ipAddress */
int nerdjack_detect(char * ipAddress);

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns -1 if no valid config found */
int nerdjack_choose_scan(double desired_rate, double *actual_rate, int *period);

#endif
