#ifndef SOCKUTIL_H

#define SOCKUTIL_H

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>

/* Returns the size of the sockaddr, or 0 on error */
size_t sockaddr_size(const struct sockaddr* sa);

/* Convert a sockaddr into an address. Like inet_ntop, it returns dest if
 * successful, NULL otherwise. In the latter case, dest will contain "???"
 */
const char* sockaddr_address_string(const struct sockaddr* sa, char* dest, size_t len);

/* Set the SOL_REUSEADDR otion */
int sock_set_reuseaddr(int fd, int optval);

/* Set the tcp_nodelay option */
int sock_set_tcp_nodelay(int fd, int optval);

/* TODO: Set the tcp_cork option */
// int sock_set_cork(int fd, int optval);

int sock_set_nonblock(int fd, int optval);

/* Attempt to bind the fd to the sockaddr, retrying common transient failures */
int sock_try_bind(int fd, const struct sockaddr* sa);

/* Try to call select(), retrying EINTR */
int sock_try_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

/* Try to call connect(), timing out after wait seconds */
int sock_try_connect( int fd, struct sockaddr* to, socklen_t addrlen, int wait );

/* Try to call close(), retrying EINTR */
int sock_try_close( int fd );

#endif

