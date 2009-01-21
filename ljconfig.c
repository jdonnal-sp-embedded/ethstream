/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

/* ljconfig: display/change comm/control processor configuration */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "debug.h"
#include "ue9.h"
#include "ue9error.h"
#include "opt.h"
#include "version.h"

#define DEFAULT_HOST "192.168.1.209"
#define UE9_COMMAND_PORT 52360

struct options opt[] = {
	{ 'a', "address", "string", "host/address of UE9 (192.168.1.209)" },
	{ 'h', "help", NULL, "this help" },
	{ 'v', "verbose", NULL, "be verbose" },
	{ 'V', "version", NULL, "show version number and exit" },
	{ 0, NULL, NULL, NULL }
};

int main(int argc, char *argv[])
{
	int optind;
	char *optarg;
	char c;
	FILE *help = stderr;
	char *address = strdup(DEFAULT_HOST);
	int fd;
	int ret;

	/* Parse arguments */
	opt_init(&optind);
	while ((c = opt_parse(argc, argv, &optind, &optarg, opt)) != 0) {
		switch (c) {
		case 'a':
			free(address);
			address = strdup(optarg);
			break;
		case 'v':
			verb_count++;
			break;
		case 'V':
			printf("ljconfig " VERSION "\n");
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
			fprintf(help, "Displays/changes Labjack UE9 config.\n");
			return (help == stdout) ? 0 : 1;
		}
	}

	if(optind<argc) {
		info("Error: too many arguments (%s)\n\n", argv[optind]);
		goto printhelp;
	}
	
	ret = 1;

	/* Open */
	fd = ue9_open(address, UE9_COMMAND_PORT);
	if (fd < 0) {
		info("Connect failed: %s:%d\n", address, UE9_COMMAND_PORT);
		goto out0;
	}

	goto out1;
	
	
	ret = 0;
 out1:
	/* Close */
	ue9_close(fd);
 out0:
	return ret;
}

