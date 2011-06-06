/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
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
#include "ethstream.h"

#include "example.inc"

#define DEFAULT_HOST "192.168.1.209"
#define UE9_COMMAND_PORT 52360
#define UE9_DATA_PORT 52361

#define MAX_CHANNELS 256

struct callbackInfo {
	struct ue9Calibration calib;
	int convert;
	int maxlines;
};

struct options opt[] = {
	{'a', "address", "string", "host/address of device (192.168.1.209)"},
	{'n', "numchannels", "n", "sample the first N ADC channels (2)"},
	{'C', "channels", "a,b,c", "sample channels a, b, and c"},
	{'r', "rate", "hz", "sample each channel at this rate (8000.0)"},

	{'L', "labjack", NULL, "Force LabJack device"},
	{'t', "timers", "a,b,c", "set LabJack timer modes to a, b, and c"},
	{'T', "timerdivisor", "n", "set LabJack timer divisor to n"},

	{'N', "nerdjack", NULL, "Force NerdJack device"},
	{'d', "detect", NULL, "Detect NerdJack IP address"},
	{'R', "range", "a,b",
	 "Set range on NerdJack for channels 0-5,6-11 to either 5 or 10 (10,10)"},
	{'g', "gain", "a,b,c", "Set Labjack AIN channel gains: 0,1,2,4,8 in -C channel order"},
	{'o', "oneshot", NULL, "don't retry in case of errors"},
	{'f', "forceretry", NULL, "retry no matter what happens"},
	{'c', "convert", NULL, "convert output to volts"},
	{'H', "converthex", NULL, "convert output to hex"},
	{'m', "showmem", NULL, "output memory stats with data (NJ only)"},
	{'l', "lines", "num", "if set, output this many lines and quit"},
	{'h', "help", NULL, "this help"},
	{'v', "verbose", NULL, "be verbose"},
	{'V', "version", NULL, "show version number and exit"},
	{'i', "info", NULL, "get info from device (NJ only)"},
	{'X', "examples", NULL, "show ethstream examples and exit"},
	{0, NULL, NULL, NULL}
};

int doStream(const char *address, uint8_t scanconfig, uint16_t scaninterval,
	     int *channel_list, int channel_count, 
	     int *timer_mode_list, int timer_mode_count, int timer_divisor,
	     int *gain_list, int gain_count,  
	     int convert, int maxlines);
int nerdDoStream(const char *address, int *channel_list, int channel_count,
		 int precision, unsigned long period, int convert, int lines,
		 int showmem);
int data_callback(int channels,  int *channel_list, int gain_count, int *gain_list, 
		uint16_t * data, void *context);

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
	int convert = CONVERT_DEC;
	int showmem = 0;
	int inform = 0;
	uint8_t scanconfig;
	uint16_t scaninterval;
	int timer_mode_list[UE9_TIMERS];
	int timer_mode_count = 0;
	int timer_divisor = 1;
	int gain_list[MAX_CHANNELS];
	int gain_count = 0;
	int channel_list[MAX_CHANNELS];
	int channel_count = 0;
	int nerdjack = 0;
	int labjack = 0;
	int detect = 0;
	int precision = 0;
	int addressSpecified = 0;
	int donerdjack = 0;
	unsigned long period = NERDJACK_CLOCK_RATE / desired_rate;

	/* Parse arguments */
	opt_init(&optind);
	while ((c = opt_parse(argc, argv, &optind, &optarg, opt)) != 0) {
		switch (c) {
		case 'a':
			free(address);
			address = strdup(optarg);
			addressSpecified = 1;
			break;
		case 'n':
			channel_count = 0;
			tmp = strtol(optarg, &endp, 0);
			if (*endp || tmp < 1 || tmp > MAX_CHANNELS) {
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
				if (*endp != '\0' && *endp != ',') {
					info("bad channel number: %s\n",
					     optarg);
					goto printhelp;
				}
				//We do not want to overflow channel_list, so we need the check here
				//The rest of the sanity checking can come later after we know
				//whether this is a 
				//LabJack or a NerdJack
				if (channel_count >= MAX_CHANNELS) {
					info("error: too many channels specified\n");
					goto printhelp;
				}
				channel_list[channel_count++] = tmp;
				optarg = endp + 1;
			}
			while (*endp);
			break;
		case 'g':	/* labjack only */
			gain_count = 0;
			do {
				tmp = strtol(optarg, &endp, 0);
				if (*endp != '\0' && *endp != ',') {
					info("bad gain number: %s\n",
					     optarg);
					goto printhelp;
				}
				if (gain_count >= MAX_CHANNELS) {
					info("error: too many gains specified\n");
					goto printhelp;
				}
				if (!(tmp == 0 || tmp == 1 || tmp == 2 || tmp == 4 || tmp == 8)) {
				info("error: invalid gain specified\n");
					goto printhelp;
				}
								
				gain_list[gain_count++] = tmp;
				optarg = endp + 1;
			}
			while (*endp);
			break;
		case 't':	/* labjack only */
			timer_mode_count = 0;
			do {
				tmp = strtol(optarg, &endp, 0);
				if (*endp != '\0' && *endp != ',') {
					info("bad timer mode: %s\n", optarg);
					goto printhelp;
				}
				if (timer_mode_count >= UE9_TIMERS) {
					info("error: too many timers specified\n");
					goto printhelp;
				}
				timer_mode_list[timer_mode_count++] = tmp;
				optarg = endp + 1;
			}
			while (*endp);
			break;
		case 'T':	/* labjack only */
			timer_divisor = strtod(optarg, &endp);
			if (*endp || timer_divisor < 0 || timer_divisor > 255) {
				info("bad timer divisor: %s\n", optarg);
				goto printhelp;
			}
			break;
		case 'r':
			desired_rate = strtod(optarg, &endp);
			if (*endp || desired_rate <= 0) {
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
		case 'R':
			tmp = strtol(optarg, &endp, 0);
			if (*endp != ',') {
				info("bad range number: %s\n", optarg);
				goto printhelp;
			}
			if (tmp != 5 && tmp != 10) {
				info("valid choices for range are 5 or 10\n");
				goto printhelp;
			}
			if (tmp == 5)
				precision = precision + 1;

			optarg = endp + 1;
			if (*endp == '\0') {
				info("Range needs two numbers, one for channels 0-5 and another for 6-11\n");
				goto printhelp;
			}
			tmp = strtol(optarg, &endp, 0);
			if (*endp != '\0') {
				info("Range needs only two numbers, one for channels 0-5 and another for 6-11\n");
				goto printhelp;
			}
			if (tmp != 5 && tmp != 10) {
				info("valid choices for range are 5 or 10\n");
				goto printhelp;
			}
			if (tmp == 5)
				precision = precision + 2;
			break;
		case 'N':
			nerdjack++;
			break;
		case 'L':
			labjack++;
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
			if (convert != 0) {
				info("specify only one conversion type\n");
				goto printhelp;
			}
			convert = CONVERT_VOLTS;
			break;
		case 'H':
			if (convert != 0) {
				info("specify only one conversion type\n");
				goto printhelp;
			}
			convert = CONVERT_HEX;
			break;
		case 'm':
			showmem++;
		case 'v':
			verb_count++;
			break;
		case 'X':
			printf("%s", examplestring);
			return 0;
			break;
		case 'V':
			printf("etherstream " VERSION "\n");
			printf("Written by Jim Paris <jim@jtan.com>\n");
			printf("and Zachary Clifford <zacharyc@mit.edu>.\n");
			printf("This program comes with no warranty and is "
			       "provided under the GPLv2.\n");
			return 0;
			break;
		case 'i':
			inform++;
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

	if (detect && labjack) {
		info("The LabJack does not support autodetection\n");
		goto printhelp;
	}

	if (detect && !nerdjack) {
		info("Only the NerdJack supports autodetection - assuming -N option\n");
		nerdjack = 1;
	}

	if (detect && addressSpecified) {
		info("Autodetection and specifying address are mutually exclusive\n");
		goto printhelp;
	}

	if (nerdjack && labjack) {
		info("Nerdjack and Labjack options are mutually exclusive\n");
		goto printhelp;
	}

	donerdjack = nerdjack;

	//First if no options were supplied try the Nerdjack
	//The second time through, donerdjack will be true and this will not fire
	if (!nerdjack && !labjack) {
		info("No device specified...Defaulting to Nerdjack\n");
		donerdjack = 1;
	}

 doneparse:

	if (inform) {
		//We just want information from NerdJack
		if (!detect) {
			if (nerd_get_version(address) < 0) {
				info("Could not find NerdJack at specified address\n");
			} else {
				return 0;
			}
		}
		info("Autodetecting NerdJack address\n");
		free(address);
		if (nerdjack_detect(address) < 0) {
			info("Error with autodetection\n");
			goto printhelp;
		} else {
			info("Found NerdJack at address: %s\n", address);
			if (nerd_get_version(address) < 0) {
				info("Error getting NerdJack version\n");
				goto printhelp;
			}
			return 0;
		}
	}

	if (donerdjack) {
		if (channel_count > NERDJACK_CHANNELS) {
			info("Too many channels for NerdJack\n");
			goto printhelp;
		}
		for (i = 0; i < channel_count; i++) {
			if (channel_list[i] >= NERDJACK_CHANNELS) {
				info("Channel is out of NerdJack range: %d\n",
				     channel_list[i]);
				goto printhelp;
			}
		}
	} else {
		if (channel_count > UE9_MAX_CHANNEL_COUNT) {
			info("Too many channels for LabJack\n");
			goto printhelp;
		}
		for (i = 0; i < channel_count; i++) {
			if (channel_list[i] > UE9_MAX_CHANNEL) {
				info("Channel is out of LabJack range: %d\n",
				     channel_list[i]);
				goto printhelp;
			}
		}
	}
	
	/* Timer requires Labjack */
	if (timer_mode_count && !labjack) {
		info("Can't use timers on NerdJack\n");
		goto printhelp;
	}

	/* Individual Analog Channel Gain Set requires Labjack*/
	if (gain_count && !labjack) {
		info("Can't use Individual Gain Set on NerdJack\n");
		goto printhelp;
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
			info_no_timestamp(" AIN%d", channel_list[i]);
		info_no_timestamp("\n");
	}

	/* Figure out actual rate. */
	if (donerdjack) {
		if (nerdjack_choose_scan(desired_rate, &actual_rate, &period) <
		    0) {
			info("error: can't achieve requested scan rate (%lf Hz)\n", desired_rate);
		}
	} else {
		if (ue9_choose_scan(desired_rate, &actual_rate,
				    &scanconfig, &scaninterval) < 0) {
			info("error: can't achieve requested scan rate (%lf Hz)\n", desired_rate);
		}
	}

	if ((desired_rate != actual_rate) || verb_count) {
		info("Actual scanrate is %lf Hz\n", actual_rate);
		info("Period is %ld\n", period);
	}

	if (verb_count && lines) {
		info("Stopping capture after %d lines\n", lines);
	}

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

#ifdef SIGPIPE /* not on Windows */
	/* Ignore SIGPIPE so I/O errors to the network device won't kill the process */
	signal(SIGPIPE, SIG_IGN);
#endif

	if (detect) {
		info("Autodetecting NerdJack address\n");
		free(address);
		if (nerdjack_detect(address) < 0) {
			info("Error with autodetection\n");
			goto printhelp;
		} else {
			info("Found NerdJack at address: %s\n", address);
		}
	}

	for (;;) {
		int ret;
		if (donerdjack) {
			ret =
			    nerdDoStream(address, channel_list, channel_count,
					 precision, period, convert, lines,
					 showmem);
			verb("nerdDoStream returned %d\n", ret);

		} else {
			ret = doStream(address, scanconfig, scaninterval,
				       channel_list, channel_count,
				       timer_mode_list, timer_mode_count, timer_divisor,
				       gain_list, gain_count,
				       convert, lines);
			verb("doStream returned %d\n", ret);
		}
		if (oneshot)
			break;

		if (ret == 0)
			break;

		//Neither options specified at command line and first time through.
		//Try LabJack
		if (ret == -ENOTCONN && donerdjack && !labjack && !nerdjack) {
			info("Could not connect NerdJack...Trying LabJack\n");
			donerdjack = 0;
			goto doneparse;
		}
		//Neither option supplied, no address, and second time through.
		//Try autodetection
		if (ret == -ENOTCONN && !donerdjack && !labjack && !nerdjack
		    && !addressSpecified) {
			info("Could not connect LabJack...Trying to autodetect Nerdjack\n");
			detect = 1;
			donerdjack = 1;
			goto doneparse;
		}

		if (ret == -ENOTCONN && nerdjack && !detect
		    && !addressSpecified) {
			info("Could not reach NerdJack...Trying to autodetect\n");
			detect = 1;
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

int
nerdDoStream(const char *address, int *channel_list, int channel_count,
	     int precision, unsigned long period, int convert, int lines,
	     int showmem)
{
	int retval = -EAGAIN;
	int fd_data;
	static int first_call = 1;
	static int started = 0;
	static int wasreset = 0;
	getPacket command;
	static unsigned short currentcount = 0;
 tryagain:

	//If this is the first time, set up acquisition
	//Otherwise try to resume the previous one
	if (started == 0) {
		if (nerd_generate_command
		    (&command, channel_list, channel_count, precision,
		     period) < 0) {
			info("Failed to create configuration command\n");
			goto out;
		}

		if (nerd_send_command(address, "STOP", 4) < 0) {
			if (first_call) {
				retval = -ENOTCONN;
				if (verb_count)
					info("Failed to send STOP command\n");
			} else {
				info("Failed to send STOP command\n");
			}
			goto out;
		}

		if (nerd_send_command(address, &command, sizeof(command)) < 0) {
			info("Failed to send GET command\n");
			goto out;
		}

	} else {
		//If we had a transmission in progress, send a command to resume from there
		char cmdbuf[10];
		sprintf(cmdbuf, "SETC%05hd", currentcount);
		retval = nerd_send_command(address, cmdbuf, strlen(cmdbuf));
		if (retval == -4) {
			info("NerdJack was reset\n");
			//Assume we have not started yet, reset on this side.
			//If this routine is retried, start over
			printf("# NerdJack was reset here\n");
			currentcount = 0;
			started = 0;
			wasreset = 1;
			goto tryagain;
		} else if (retval < 0) {
			info("Failed to send SETC command\n");
			goto out;
		}
	}

	//The transmission has begun
	started = 1;

	/* Open connection */
	fd_data = nerd_open(address, NERDJACK_DATA_PORT);
	if (fd_data < 0) {
		info("Connect failed: %s:%d\n", address, NERDJACK_DATA_PORT);
		goto out;
	}

	retval = nerd_data_stream
	    (fd_data, channel_count, channel_list, precision, convert, lines,
	     showmem, &currentcount, period, wasreset);
	wasreset = 0;
	if (retval == -3) {
		retval = 0;
	}
	if (retval < 0) {
		info("Failed to open data stream\n");
		goto out1;
	}

	info("Stream finished\n");
	retval = 0;

 out1:
	nerd_close_conn(fd_data);
 out:
	//We've tried communicating, so this is not the first call anymore
	first_call = 0;
	return retval;
}

int
doStream(const char *address, uint8_t scanconfig, uint16_t scaninterval,
	 int *channel_list, int channel_count, 
	 int *timer_mode_list, int timer_mode_count, int timer_divisor,
	 int *gain_list, int gain_count,
	 int convert, int lines)
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

	/* Set timer configuration */
	if (timer_mode_count &&
	    ue9_timer_config(fd_cmd, timer_mode_list, timer_mode_count,
			     timer_divisor) < 0) {
		info("Failed to set timer configuration\n");
		goto out2;
	}		

	if (gain_count) {
		/* Set stream configuration */
		if (ue9_streamconfig(fd_cmd, channel_list, channel_count,
						scanconfig, scaninterval,
						gain_list, gain_count) < 0) {
			info("Failed to set stream configuration\n");
			goto out2;
		}
	} else 	{
		/* Set stream configuration */
		if (ue9_streamconfig_simple(fd_cmd, channel_list, channel_count,
						scanconfig, scaninterval,
						UE9_BIPOLAR_GAIN1) < 0) {
			info("Failed to set stream configuration\n");
			goto out2;
		}
	}

	/* Start stream */
	if (ue9_stream_start(fd_cmd) < 0) {
		info("Failed to start stream\n");
		goto out2;
	}

	/* Stream data */
	ret =
	    ue9_stream_data(fd_data, channel_count, channel_list, gain_count, gain_list, data_callback, (void *)&ci);
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

int data_callback(int channels, int *channel_list, int gain_count, int *gain_list, uint16_t * data, void *context)
{
	int i;
	struct callbackInfo *ci = (struct callbackInfo *)context;
	static int lines = 0;

	columns_left = channels;
	for (i = 0; i < channels; i++) {
		if (ci->convert == CONVERT_VOLTS &&
		    channel_list[i] <= UE9_MAX_ANALOG_CHANNEL) {
			/* CONVERT_VOLTS */
			if (i < gain_count)
			{
			if (printf("%lf", ue9_binary_to_analog(
					   &ci->calib, gain_list[i],
					   12, data[i])) < 0)
				goto bad;
			} else {
			if (printf("%lf", ue9_binary_to_analog(
					   &ci->calib, 0,
					   12, data[i])) < 0)
				goto bad;
			}
		} else if (ci->convert == CONVERT_HEX) {
			/* CONVERT_HEX */
			if (printf("%04X", data[i]) < 0)
				goto bad;
		} else {
			/* CONVERT_DEC */
			if (printf("%d", data[i]) < 0)
				goto bad;
		}
		columns_left--;
		if (i < (channels - 1)) {
			if (ci->convert != CONVERT_HEX && putchar(' ') < 0)
				goto bad;
		} else {
			if (putchar('\n') < 0)
				goto bad;
			lines++;
			if (ci->maxlines && lines >= ci->maxlines)
				return -1;
		}
	}

	return 0;

 bad:
	info("Output error (disk full?)\n");
	return -3;
}
