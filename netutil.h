#ifndef NETUTIL_H
#define NETUTIL_H

#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>

#ifdef __WIN32__
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define socklen_t int
#  define in_addr_t uint32_t
#  define in_port_t uint16_t
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

/* Initialize networking */
void net_init(void);

/* Set socket blocking/nonblocking */
int soblock(int socket, int blocking);

/* Like send(2), recv(2), connect(2), but with timeouts.  
   Socket must be O_NONBLOCK. */
int connect_timeout(int s, const struct sockaddr *serv_addr, socklen_t addrlen,
		    struct timeval *timeout);
ssize_t send_timeout(int s, const void *buf, size_t len, int flags, 
		     struct timeval *timeout);
ssize_t recv_timeout(int s, void *buf, size_t len, int flags, 
		     struct timeval *timeout);
ssize_t recvfrom_timeout(int s, void *buf, size_t len, int flags, struct sockaddr *address, socklen_t *address_len,
		     struct timeval *timeout);

/* Like send_timeout and recv_timeout, but they retry (with the same timeout)
   in case of partial transfers, in order to try to transfer all data. */
ssize_t send_all_timeout(int s, const void *buf, size_t len, int flags, 
			 struct timeval *timeout);
ssize_t recv_all_timeout(int s, void *buf, size_t len, int flags, 
			 struct timeval *timeout);

#endif
