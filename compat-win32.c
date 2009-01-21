#include <unistd.h>
#include <stdio.h>
#include "compat.h"
#include <windows.h>

unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

static struct {
	int num;
	char *msg;
} win32_error[] = {
	/* Errors that we might vaguely expect to see */
	{ WSAEINTR, "Winsock: Interrupted system call" },
	{ WSAEBADF, "Winsock: Bad file number" },
	{ WSAEFAULT, "Winsock: Bad address" },
	{ WSAEINVAL, "Winsock: Invalid argument" },
	{ WSAEMFILE, "Winsock: Too many open files" },
	{ WSAEWOULDBLOCK, "Winsock: Operation would block" },
	{ WSAEINPROGRESS, "Winsock: Operation now in progress" },
	{ WSAEALREADY, "Winsock: Operation already in progress" },
	{ WSAENOTSOCK, "Winsock: Socket operation on nonsocket" },
	{ WSAEADDRINUSE, "Winsock: Address already in use" },
	{ WSAEADDRNOTAVAIL, "Winsock: Cannot assign requested address" },
	{ WSAENETDOWN, "Winsock: Network is down" },
	{ WSAENETUNREACH, "Winsock: Network is unreachable" },
	{ WSAENETRESET, "Winsock: Network dropped connection on reset" },
	{ WSAECONNABORTED, "Winsock: Software caused connection abort" },
	{ WSAECONNRESET, "Winsock: Connection reset by peer" },
	{ WSAETIMEDOUT, "Winsock: Connection timed out" },
	{ WSAECONNREFUSED, "Winsock: Connection refused" },
	{ WSAEHOSTDOWN, "Winsock: Host is down" },
	{ WSAEHOSTUNREACH, "Winsock: No route to host" },
	{ WSAVERNOTSUPPORTED, "Winsock: Unsupported Winsock version" },
	{ ETIMEDOUT, "Connection timed out" },
	{ ENOTCONN, "Not connected" },
	{ -1, NULL },
};
char *compat_strerror(int errnum)
{
	int i;
	static char buf[128];
	
	for (i = 0; win32_error[i].num != -1; i++)
		if (errnum == win32_error[i].num)
			return win32_error[i].msg;
	if (errnum >= 10000) {
		sprintf(buf, "Winsock: unknown error %d\n", errnum);
		return buf;
	}
	return strerror(errnum);
}

#ifdef __WIN32__

/*const char *compat_inet_ntop(int af, const void *src, char *dst, socklen_t cnt)
{
        if (af == AF_INET)
        {
                struct sockaddr_in in;
                memset(&in, 0, sizeof(in));
                in.sin_family = AF_INET;
                memcpy(&in.sin_addr, src, sizeof(struct in_addr));
                getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in), dst, cnt, NULL, 0, NI_NUMERICHOST);
                return dst;
        }
        else if (af == AF_INET6)
        {
                struct sockaddr_in6 in;
                memset(&in, 0, sizeof(in));
                in.sin6_family = AF_INET6;
                memcpy(&in.sin6_addr, src, sizeof(struct in_addr6));
                getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in6), dst, cnt, NULL, 0, NI_NUMERICHOST);
                return dst;
        }
        return NULL;
}
*/
#endif

