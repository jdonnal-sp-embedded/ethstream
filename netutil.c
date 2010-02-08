#include "netutil.h"
#include "compat.h"
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>

/* Initialize networking */
void net_init(void)
{
#ifdef __WIN32__
	WSADATA blah;
	WSAStartup(0x0101, &blah);
#endif
}

/* Set socket blocking/nonblocking */
int soblock(int socket, int blocking)
{
#ifdef __WIN32__
	unsigned long arg = blocking ? 0 : 1;
	if (ioctlsocket(socket, FIONBIO, &arg) != 0)
		return -1;
	return 0;
#else
	int sockopt;

	/* Get flags */
	sockopt = fcntl(socket, F_GETFL);
	if (sockopt == -1) {
		return -1;
	}

	/* Modify */
	if (blocking)
		sockopt &= ~O_NONBLOCK;
	else
		sockopt |= O_NONBLOCK;

	/* Set flags */
	if (fcntl(socket, F_SETFL, sockopt) != 0)
		return -1;

	return 0;
#endif
}

/* Like connect(2), but with a timeout.  Socket must be non-blocking. */
int
connect_timeout(int s, const struct sockaddr *serv_addr, socklen_t addrlen,
		struct timeval *timeout)
{
	int ret;
	fd_set writefds;
	fd_set exceptfds;
	int optval;
	socklen_t optlen;

	/* Start connect */
	ret = connect(s, serv_addr, addrlen);

	if (ret == 0) {
		/* Success */
		return 0;
	}

	/* Check for immediate failure */
#ifdef __WIN32__
	errno = WSAGetLastError();
	if (ret < 0 && errno != WSAEWOULDBLOCK && errno != WSAEINVAL)
		return -1;
#else
	if (ret < 0 && errno != EINPROGRESS && errno != EALREADY)
		return -1;
#endif

	/* In progress, wait for result. */
	FD_ZERO(&writefds);
	FD_SET(s, &writefds);
	FD_ZERO(&exceptfds);
	FD_SET(s, &exceptfds);
	ret = select(s + 1, NULL, &writefds, &exceptfds, timeout);
	if (ret < 0) {
		/* Error */
		return -1;
	}
	if (ret == 0) {
		/* Timed out */
		errno = ETIMEDOUT;
		return -1;
	}

	/* Check the socket state */
	optlen = sizeof(optval);
	if (getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&optval, &optlen) != 0)
		return -1;

	if (optval != 0) {
		/* Connection failed. */
		errno = optval;
		return -1;
	}

	/* On Windows, SO_ERROR sometimes shows no error but the connection
	   still failed.  Sigh. */
	if (FD_ISSET(s, &exceptfds) || !FD_ISSET(s, &writefds)) {
		errno = EIO;
		return -1;
	}

	/* Success */
	return 0;
}

/* Like send(2), but with a timeout.  Socket must be non-blocking.
   The timeout only applies if no data at all is sent -- this function
   may still send less than requested. */
ssize_t
send_timeout(int s, const void *buf, size_t len, int flags,
	     struct timeval * timeout)
{
	fd_set writefds;
	int ret;

	FD_ZERO(&writefds);
	FD_SET(s, &writefds);
	ret = select(s + 1, NULL, &writefds, NULL, timeout);
	if (ret == 0) {
		/* Timed out */
		errno = ETIMEDOUT;
		return -1;
	}
	if (ret != 1) {
		/* Error */
		return -1;
	}

	return send(s, buf, len, flags);
}

/* Like recv(2), but with a timeout.  Socket must be non-blocking. 
   The timeout only applies if no data at all is received -- this
   function may still return less than requested. */
ssize_t
recv_timeout(int s, void *buf, size_t len, int flags, struct timeval * timeout)
{
	fd_set readfds;
	int ret;

	FD_ZERO(&readfds);
	FD_SET(s, &readfds);
	ret = select(s + 1, &readfds, NULL, NULL, timeout);
	if (ret == 0) {
		/* Timed out */
		errno = ETIMEDOUT;
		return -1;
	}
	if (ret != 1) {
		/* Error */
		return -1;
	}

	return recv(s, buf, len, flags);
}

/* Like recvfrom(2), but with a timeout.  Socket must be non-blocking. 
   The timeout only applies if no data at all is received -- this
   function may still return less than requested. */
ssize_t
recvfrom_timeout(int s, void *buf, size_t len, int flags,
		 struct sockaddr * address, socklen_t * address_len,
		 struct timeval * timeout)
{
	fd_set readfds;
	int ret;

	FD_ZERO(&readfds);
	FD_SET(s, &readfds);
	ret = select(s + 1, &readfds, NULL, NULL, timeout);
	if (ret == 0) {
		/* Timed out */
		errno = ETIMEDOUT;
		return -1;
	}
	if (ret != 1) {
		/* Error */
		return -1;
	}

	return recvfrom(s, buf, len, flags, address, address_len);
}

/* Like send_timeout, but retries (with the same timeout) in case of
   partial transfers.  This is a stronger attempt to send all
   requested data. */
ssize_t
send_all_timeout(int s, const void *buf, size_t len, int flags,
		 struct timeval * timeout)
{
	struct timeval tv;
	size_t left = len;
	ssize_t ret;

	while (left > 0) {
		tv.tv_sec = timeout->tv_sec;
		tv.tv_usec = timeout->tv_usec;
		ret = send_timeout(s, buf, left, flags, &tv);

		if (ret < 0)
			return ret;

		if (ret == 0)
			break;

		left -= ret;
		buf += ret;
	}

	return len - left;
}

/* Like recv_timeout, but retries (with the same timeout) in case of
   partial transfers.  This is a stronger attempt to recv all
   requested data. */
ssize_t
recv_all_timeout(int s, void *buf, size_t len, int flags,
		 struct timeval * timeout)
{
	struct timeval tv;
	size_t left = len;
	ssize_t ret;

	while (left > 0) {
		tv.tv_sec = timeout->tv_sec;
		tv.tv_usec = timeout->tv_usec;
		ret = recv_timeout(s, buf, left, flags, &tv);

		if (ret < 0)
			return ret;

		if (ret == 0)
			break;

		left -= ret;
		buf += ret;
	}

	return len - left;
}
