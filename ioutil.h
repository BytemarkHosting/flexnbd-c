#ifndef __IOUTIL_H
#define __IOUTIL_H


#include "params.h"

char* build_allocation_map(int fd, off64_t size, int resolution);
int writeloop(int filedes, const void *buffer, size_t size);
int readloop(int filedes, void *buffer, size_t size);
int sendfileloop(int out_fd, int in_fd, off64_t *offset, size_t count);
int splice_via_pipe_loop(int fd_in, int fd_out, size_t len);

#endif

