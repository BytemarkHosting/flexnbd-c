#ifndef READWRITE_H

#define READWRITE_H

int socket_connect(struct sockaddr* to, struct sockaddr* from);
off64_t socket_nbd_read_hello(int fd);
void socket_nbd_read(int fd, off64_t from, int len, int out_fd, void* out_buf);
void socket_nbd_write(int fd, off64_t from, int len, int out_fd, void* out_buf);

#endif

