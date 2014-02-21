#ifndef SELF_PIPE_H
#define SELF_PIPE_H

#include <sys/select.h>

struct self_pipe {
	int read_fd;
	int write_fd;
};


struct self_pipe * self_pipe_create(void);
int self_pipe_signal( struct self_pipe * sig );
int self_pipe_signal_clear( struct self_pipe *sig );
int self_pipe_destroy( struct self_pipe * sig );
int self_pipe_fd_set( struct self_pipe * sig, fd_set * fds );
int self_pipe_fd_isset( struct self_pipe *sig, fd_set *fds );

#endif
