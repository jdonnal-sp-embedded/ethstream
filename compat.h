#ifndef COMPAT_H
#define COMPAT_H

#ifdef __WIN32__
unsigned int sleep(unsigned int seconds);
char *compat_strerror(int errnum);
//const char *inet_ntop(int af, void *src, const char *dst, socklen_t cnt);
#define INET_ADDRSTRLEN 16
#define ETIMEDOUT 110
#define ENOTCONN 107
#else
#define compat_strerror strerror
#endif

#endif
