#ifndef READWRITE_H

#define READWRITE_H

#include <sys/types.h>
#include <sys/socket.h>
#include "nbdtypes.h"

int socket_connect(struct sockaddr* to, struct sockaddr* from);
int socket_nbd_read_hello(int fd, off64_t * size);
int socket_nbd_write_hello(int fd, off64_t size);
void socket_nbd_read(int fd, off64_t from, int len, int out_fd, void* out_buf, int timeout_secs);
void socket_nbd_write(int fd, off64_t from, int len, int out_fd, void* out_buf, int timeout_secs);
int socket_nbd_disconnect( int fd );

/* as you can see, we're slowly accumulating code that should really be in an
 * NBD library */

void nbd_hello_to_buf( struct nbd_init_raw* buf, off64_t out_size );
int nbd_check_hello( struct nbd_init_raw* init_raw, off64_t* out_size );

#endif

