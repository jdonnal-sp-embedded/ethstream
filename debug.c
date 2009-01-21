#include "debug.h"
#include <stdio.h>
#include <stdarg.h>

int verb_count = 0;

int func_fprintf(const char *func, FILE *stream, const char *format, ...)
{
	va_list ap;
	int ret;

	fprintf(stream, "%s: ", func);
	va_start(ap, format);
	ret = vfprintf(stream, format, ap);
	va_end(ap);
	return ret;
}

