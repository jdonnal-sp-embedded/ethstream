/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

/* ljstream: Stream data from the first N (1-14) analog inputs.
   Resolution is set to 12-bit and all channels are in bipolar (-5 to
   +5V) mode.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include "debug.h"
#include "ue9.h"
#include "ue9error.h"
#include "nerdjack.h"
#include "opt.h"
#include "version.h"
#include "compat.h"

#define DEFAULT_HOST "192.168.1.209"
#define UE9_COMMAND_PORT 52360
#define UE9_DATA_PORT 52361

struct callbackInfo { 
	struct ue9Calibration calib;
	int convert;
	int maxlines;
};

struct options opt[] = {
	{ 'a', "address", "string", "host/address of UE9 (192.168.1.209)" },
	{ 'n', "numchannels", "n", "sample the first N ADC channels (2)" },
    { 'N', "nerdjack", NULL, "Use NerdJack device instead" },
    { 'd', "detect", NULL, "Detect NerdJack IP address" },
    { 'p', "precision", "0-3", "Set precision on NerdJack (0 - max range, 1 - max precision)"}, 
	{ 'C', "channels", "a,b,c", "sample channels a, b, and c" },
	{ 'r', "rate", "hz", "sample each channel at this rate (8000.0)" },
	{ 'o', "oneshot", NULL, "don't retry in case of errors" },
	{ 'f', "forceretry", NULL, "retry no matter what happens" },
	{ 'c', "convert", NULL, "display output in volts" },
	{ 'l', "lines", "num", "if set, output this many lines and quit" },
	{ 'h', "help", NULL, "this help" },
	{ 'v', "verbose", NULL, "be verbose" },
	{ 'V', "version", NULL, "show version number and exit" },
	{ 0, NULL, NULL, NULL }
};

int doStream(const char *address, uint8_t scanconfig, uint16_t scaninterval,
	     int *channel_list, int channel_count, int convert, int maxlines);
int nerdDoStream(const char *address, int *channel_list, int channel_count, int precision, 
            unsigned short period, int convert, int lines);
int data_callback(int channels, uint16_t *data, void *context);

int columns_left = 0;
void handle_sig(int sig)
{
	while (columns_left--) {
		printf(" 0");
	}
	fflush(stdout);
	exit(0);
}

int main(int argc, char *argv[])
{
	int optind;
	char *optarg, *endp;
	char c;
	int tmp, i;
	FILE *help = stderr;
	char *address = strdup(DEFAULT_HOST);
	double desired_rate = 8000.0;
	int lines = 0;
	double actual_rate;
	int oneshot = 0;
	int forceretry = 0;
	int convert = 0;
	uint8_t scanconfig;
	uint16_t scaninterval;
#if UE9_CHANNELS > NERDJACK_CHANNELS
	int channel_list[UE9_CHANNELS];
#else
    int channel_list[NERDJACK_CHANNELS];
#endif
	int channel_count = 0;
    int nerdjack = 0;
    int detect = 0;
    int precision = 0;
    int period = NERDJACK_CLOCK_RATE / desired_rate;

	/* Parse arguments */
	opt_init(&optind);
	while ((c = opt_parse(argc, argv, &optind, &optarg, opt)) != 0) {
		switch (c) {
		case 'a':
			free(address);
			address = strdup(optarg);
			break;
		case 'n':
			channel_count = 0;
			tmp = strtol(optarg, &endp, 0);
			if (*endp || tmp < 1 || tmp > UE9_CHANNELS) {
				info("bad number of channels: %s\n", optarg);
				goto printhelp;
			}
			for (i = 0; i < tmp; i++)
				channel_list[channel_count++] = i;
			break;
		case 'C':
			channel_count = 0;
			do {
				tmp = strtol(optarg, &endp, 0);
				if (*endp != '\0' && *endp != ',')  {
                //|| tmp < 0 || tmp >= UE9_CHANNELS) {
					info("bad channel number: %s\n", optarg);
					goto printhelp;
				}
                //We do not want to overflow channel_list, so we need the check here
                //The rest of the sanity checking can come later after we know whether this is a 
                //LabJack or a NerdJack
#if UE9_CHANNELS > NERDJACK_CHANNELS
				if (channel_count >= UE9_CHANNELS) {
#else
                if (channel_count >= NERDJACK_CHANNELS) {
#endif
                	info("error: too many channels specified\n");
					goto printhelp;
				}
				channel_list[channel_count++] = tmp;
				optarg = endp + 1;
			} while (*endp);
			break;			
		case 'r':
			desired_rate = strtod(optarg, &endp);
			if(*endp || desired_rate <= 0) {
				info("bad rate: %s\n", optarg);
				goto printhelp;
			}
			break;
		case 'l':
			lines = strtol(optarg, &endp, 0);
			if (*endp || lines <= 0) {
				info("bad number of lines: %s\n", optarg);
				goto printhelp;
			}
			break;
        case 'p':
            precision++;
            break;
        case 'N':
            nerdjack++;
            break;
        case 'd':
            detect++;
            break;
		case 'o':
			oneshot++;
			break;
		case 'f':
		  	forceretry++;
			break;
		case 'c':
			convert++;
			break;
		case 'v':
			verb_count++;
			break;
		case 'V':
			printf("ljstream " VERSION "\n");
			printf("Written by Jim Paris <jim@jtan.com>\n");
			printf("This program comes with no warranty and is "
			       "provided under the GPLv2.\n");
			return 0;
			break;
		case 'h':
			help = stdout;
		default:
		printhelp:
			fprintf(help, "Usage: %s [options]\n", *argv);
			opt_help(opt, help);
			fprintf(help, "Read data from the specified Labjack UE9"
				" via Ethernet.  See README for details.\n");
			return (help == stdout) ? 0 : 1;
		}
	}

    doneparse:
    
    if (nerdjack) {
        if (channel_count > NERDJACK_CHANNELS) {
            info("Too many channels for NerdJack\n");
            goto printhelp;
        }
        for (i = 0; i < channel_count; i++) {
            if (channel_list[i] >= NERDJACK_CHANNELS) {
                info("Channel is out of NerdJack range: %d\n",channel_list[i]);
                goto printhelp;
            }
        }
    } else {
        if (channel_count > UE9_CHANNELS) {
            info("Too many channels for LabJack\n");
            goto printhelp;
        }
        for (i = 0; i < channel_count; i++) {
            if (channel_list[i] >= UE9_CHANNELS) {
                info("Channel is out of LabJack range: %d\n",channel_list[i]);
                goto printhelp;
            }
        }
    }
        
        

	if (optind < argc) {
		info("error: too many arguments (%s)\n\n", argv[optind]);
		goto printhelp;
	}

	if (forceretry && oneshot) {
		info("forceretry and oneshot options are mutually exclusive\n");
		goto printhelp;
	}
	
	/* Two channels if none specified */
	if (channel_count == 0) {
		channel_list[channel_count++] = 0;
		channel_list[channel_count++] = 1;
	}

	if (verb_count) {
		info("Scanning channels:");
		for (i = 0; i < channel_count; i++)
			info(" AIN%d", channel_list[i]);
		info("\n");
	}

	/* Figure out actual rate. */
    if (nerdjack) {
        if (nerdjack_choose_scan(desired_rate, &actual_rate, &period) < 0) {
            info("error: can't achieve requested scan rate (%lf Hz)\n",
                desired_rate);
            return 1;
        }
    } else {
        if (ue9_choose_scan(desired_rate, &actual_rate, 
                    &scanconfig, &scaninterval) < 0) {
            info("error: can't achieve requested scan rate (%lf Hz)\n",
                desired_rate);
            return 1;
        }
    }
    

	if ((desired_rate != actual_rate) || verb_count)
		info("Actual scanrate is %lf Hz\n", actual_rate);

	if (verb_count && lines) {
		info("Stopping capture after %d lines\n", lines);
	}

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);
    
    if (detect) {
        info("Autodetecting NerdJack address\n");
        free(address);
        if(nerdjack_detect(address) < 0) {
            info("Error with autodetection\n");
        } else {
            info("Found NerdJack at address: %s\n",address);
        }
    }
        

	for (;;) {
		int ret;
        if(nerdjack) {
            ret = nerdDoStream(address, channel_list, channel_count, precision, period, convert, lines);
            verb("nerdDoStream returned %d\n", ret);
        
        } else {
            ret = doStream(address, scanconfig, scaninterval,
                    channel_list, channel_count, convert,
                    lines);
            verb("doStream returned %d\n", ret);
        }
		if (oneshot)
			break;

		if (ret == 0)
		        break;

        if (ret == -ENOTCONN && !nerdjack) {
            info("Could not connect LabJack...Trying NerdJack\n");
            nerdjack = 1;
            goto doneparse;
        }
        
		if (ret == -ENOTCONN && !forceretry) {
			info("Initial connection failed, giving up\n");
			break;
		}

		if (ret == -EAGAIN || ret == -ENOTCONN) {
			/* Some transient error.  Wait a tiny bit, then retry */
			info("Retrying in 5 secs.\n");
			sleep(5);
		} else {
			info("Retrying now.\n");
		}
	}

	debug("Done loop\n");

	return 0;
}

int nerdDoStream(const char *address, int *channel_list, int channel_count, int precision, 
            unsigned short period, int convert, int lines)
{
	int retval = -EAGAIN;
	int fd_data;
	static int first_call = 1;
    char command[13];

	/* Open connection.  If this fails, and this is the
  	   first attempt, return a different error code so we give up. */
	fd_data = nerd_open(address, NERDJACK_DATA_PORT);
	if (fd_data < 0) {
		info("Connect failed: %s:%d\n", address, NERDJACK_DATA_PORT);
		if (first_call)
			retval = -ENOTCONN;
		goto out;
	}
	first_call = 0;
    
    if (nerd_generate_command(command, channel_list, channel_count, precision, period) < 0) {
        info("Failed to create configuration command\n");
        goto out1;
    }
    
    if (nerd_data_stream(fd_data, command, channel_count, channel_list, precision, convert, lines) < 0) {
        info("Failed to open data stream\n");
        goto out1;
    }

	info("Stream finished\n");
	retval = 0;

 out1:
	nerd_close_conn(fd_data);
 out:
	return retval;
}

int doStream(const char *address, uint8_t scanconfig, uint16_t scaninterval,
	     int *channel_list, int channel_count, int convert, int lines)
{
	int retval = -EAGAIN;
	int fd_cmd, fd_data;
    int ret;
	static int first_call = 1;
	struct callbackInfo ci = {
		.convert = convert,
		.maxlines = lines,
	};

	/* Open command connection.  If this fails, and this is the
  	   first attempt, return a different error code so we give up. */
	fd_cmd = ue9_open(address, UE9_COMMAND_PORT);
	if (fd_cmd < 0) {
		info("Connect failed: %s:%d\n", address, UE9_COMMAND_PORT);
		if (first_call)
			retval = -ENOTCONN;
		goto out;
	}
	first_call = 0;

	/* Make sure nothing is left over from a previous stream */
	if (ue9_stream_stop(fd_cmd) == 0)
		verb("Stopped previous stream.\n");
	ue9_buffer_flush(fd_cmd);

	/* Open data connection */
	fd_data = ue9_open(address, UE9_DATA_PORT);
	if (fd_data < 0) {
		info("Connect failed: %s:%d\n", address, UE9_DATA_PORT);
		goto out1;
	}
	
	/* Get calibration */
	if (ue9_get_calibration(fd_cmd, &ci.calib) < 0) {
		info("Failed to get device calibration\n");
		goto out2;
	}

	/* Set stream configuration */
	if (ue9_streamconfig_simple(fd_cmd, channel_list, channel_count,
				    scanconfig, scaninterval,
				    UE9_BIPOLAR_GAIN1) < 0) {
		info("Failed to set stream configuration\n");
		goto out2;
	}

	/* Start stream */
	if (ue9_stream_start(fd_cmd) < 0) {
		info("Failed to start stream\n");
		goto out2;
	}

	/* Stream data */
	ret = ue9_stream_data(fd_data, channel_count, data_callback, (void *)&ci);
	if (ret < 0) {
		info("Data stream failed with error %d\n", ret);
		goto out3;
	}

	info("Stream finished\n");
	retval = 0;

 out3:
	/* Stop stream and clean up */
	ue9_stream_stop(fd_cmd);
	ue9_buffer_flush(fd_cmd);
 out2:
	ue9_close(fd_data);
 out1:
	ue9_close(fd_cmd);
 out:
	return retval;
}

int data_callback(int channels, uint16_t *data, void *context)
{
	int i;
	struct callbackInfo *ci = (struct callbackInfo *)context;
	static int lines = 0;

	columns_left = channels;
	for (i = 0; i < channels; i++) {
		if (ci->convert)
			printf("%lf", ue9_binary_to_analog(
				       &ci->calib, UE9_BIPOLAR_GAIN1, 12, 
				       data[i]));
		else
			printf("%d", data[i]);
		columns_left--;
		if (i < (channels - 1)) {
			putchar(' ');
		} else {
			putchar('\n');
			lines++;
			if (ci->maxlines && lines >= ci->maxlines)
				return -1;
		}
	}
	
	return 0;
}
