/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#ifndef DEBUG_H
#define DEBUG_H

extern int verb_count;

#include <stdio.h>

int func_fprintf (const char *func, FILE * stream, const char *format,
		  ...) __attribute__ ((format (printf, 3, 4)));

#define debug(x...) ({ \
	if(verb_count >= 2) \
		func_fprintf(__func__, stderr,x); \
})

#define verb(x...) ({ \
	if(verb_count >= 1) \
		func_fprintf(__func__, stderr,x); \
})

#define info(x...) ({ \
	if(verb_count >= 0) \
		fprintf(stderr,x); \
})

#endif
