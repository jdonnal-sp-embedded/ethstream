#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

int verb_count = 0;

int func_fprintf(const char *func, FILE * stream, const char *format, ...)
{
	va_list ap;
	int ret;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	fprintf(stream, "%ld.%06ld: ", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
	fprintf(stream, "%s: ", func);
	va_start(ap, format);
	ret = vfprintf(stream, format, ap);
	va_end(ap);
	return ret;
}

int my_fprintf(FILE * stream, const char *format, ...)
{
	va_list ap;
	int ret;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	fprintf(stream, "%ld.%06ld: ", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
	va_start(ap, format);
	ret = vfprintf(stream, format, ap);
	va_end(ap);
	return ret;
}
