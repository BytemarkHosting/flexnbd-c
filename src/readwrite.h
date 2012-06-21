#ifndef READWRITE_H

#define READWRITE_H

#include <sys/types.h>
#include <sys/socket.h>

int socket_connect(struct sockaddr* to, struct sockaddr* from);
off64_t socket_nbd_read_hello(int fd);
void socket_nbd_read(int fd, off64_t from, int len, int out_fd, void* out_buf);
void socket_nbd_write(int fd, off64_t from, int len, int out_fd, void* out_buf);
void socket_nbd_entrust(int fd);
int socket_nbd_disconnect( int fd );

#endif

