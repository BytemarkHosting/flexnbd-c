#ifndef READWRITE_H

#define READWRITE_H

#include <sys/types.h>
#include <sys/socket.h>
#include "nbdtypes.h"

int socket_connect(struct sockaddr *to, struct sockaddr *from);
int socket_nbd_read_hello(int fd, uint64_t * size, uint32_t * flags);
int socket_nbd_write_hello(int fd, uint64_t size, uint32_t flags);
void socket_nbd_read(int fd, uint64_t from, uint32_t len, int out_fd,
		     void *out_buf, int timeout_secs);
void socket_nbd_write(int fd, uint64_t from, uint32_t len, int out_fd,
		      void *out_buf, int timeout_secs);
int socket_nbd_disconnect(int fd);

/* as you can see, we're slowly accumulating code that should really be in an
 * NBD library */

void nbd_hello_to_buf(struct nbd_init_raw *buf, uint64_t out_size,
		      uint32_t out_flags);
int nbd_check_hello(struct nbd_init_raw *init_raw, uint64_t * out_size,
		    uint32_t * out_flags);

#endif
