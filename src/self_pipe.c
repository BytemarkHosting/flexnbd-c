/**
 * self_pipe.c
 *
 * author: Alex Young <alex@bytemark.co.uk>
 *
 * Wrapper for the self-pipe trick for select()-based thread
 * synchronisation.   Get yourself a self_pipe with self_pipe_create(), 
 * select() on the read end of the pipe with the help of
 * self_pipe_fd_set( sig, fds  ) and self_pipe_fd_isset( sig, fds ).
 * When you've received a signal, clear it with
 * self_pipe_signal_clear(sig) so that the buffer doesn't get filled.
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>


#include "util.h"
#include "self_pipe.h"

#define ERR_MSG_PIPE "Couldn't open a pipe for signaling." 
#define ERR_MSG_FCNTL "Couldn't set a signalling pipe non-blocking."
#define ERR_MSG_WRITE "Couldn't write to a signaling pipe."
#define ERR_MSG_READ "Couldn't read from a signaling pipe."

void self_pipe_server_error( int err, char *msg )
{
	char errbuf[1024] = {0};

	strerror_r( err, errbuf, 1024 );

	fatal( "%s\t%d (%s)", msg, err, errbuf );
}

/**
 * Allocate a struct self_pipe, opening the pipe and allocating a
 * pthread mutex.
 *
 * Returns NULL if the pipe couldn't be opened or if we couldn't set it
 * non-blocking.
 *
 * Remember to call self_pipe_destroy when you're done with the return
 * value.
 */
struct self_pipe * self_pipe_create(void)
{
	struct self_pipe *sig = xmalloc( sizeof( struct self_pipe ) );
	int fds[2];
	int fcntl_err;
	
	if ( NULL == sig ) { return NULL; }

	if ( pipe( fds ) ) {
		free( sig );
		self_pipe_server_error( errno, ERR_MSG_PIPE );
		return NULL;
	}

	if ( fcntl( fds[0], F_SETFL, O_NONBLOCK ) || fcntl( fds[1], F_SETFL, O_NONBLOCK ) ) {
		fcntl_err = errno;
		while( close( fds[0] ) == -1 && errno == EINTR );
		while( close( fds[1] ) == -1 && errno == EINTR );
		free( sig );
		self_pipe_server_error( fcntl_err, ERR_MSG_FCNTL );

		return NULL;
	}

	sig->read_fd = fds[0];
	sig->write_fd = fds[1];

	return sig;
}


/**
 * Send a signal to anyone select()ing on this signal.
 *
 * Returns 1 on success.  Can fail if weirdness happened to the write fd
 * of the pipe in the self_pipe struct.
 */
int self_pipe_signal( struct self_pipe * sig )
{
	NULLCHECK( sig );
	FATAL_IF( 1 == sig->write_fd, "Shouldn't be writing to stdout" );
	FATAL_IF( 2 == sig->write_fd, "Shouldn't be writing to stderr" );

	int written = write( sig->write_fd, "X", 1 );
	if ( written != 1 ) {
		self_pipe_server_error( errno, ERR_MSG_WRITE );
		return 0;
	}

	return 1;
}


/** 
 * Clear a received signal from the pipe.  Every signal sent must be
 * cleared by one (and only one) recipient when they return from select()
 * if the signal is to be used more than once.
 * Returns the number of bytes read, which will be 1 on success and 0 if
 * there was no signal.
 */
int self_pipe_signal_clear( struct self_pipe *sig )
{
	char buf[1];

	return 1 == read( sig->read_fd, buf, 1 );
}


/**
 * Close the pipe and free the self_pipe.  Do not try to use the
 * self_pipe struct after calling this, the innards are mush.
 */
int self_pipe_destroy( struct self_pipe * sig )
{
	NULLCHECK(sig);

	while( close( sig->read_fd  ) == -1 && errno == EINTR );
	while( close( sig->write_fd ) == -1 && errno == EINTR );

	/* Just in case anyone *does* try to use this after free,
	 * we should set the memory locations to an error value
	 */
	sig->read_fd = -1;
	sig->write_fd = -1;

	free( sig );
	return 1;
}

int self_pipe_fd_set( struct self_pipe * sig, fd_set * fds)
{
	FD_SET( sig->read_fd, fds );
	return 1;
}

int self_pipe_fd_isset( struct self_pipe * sig, fd_set * fds)
{
	return FD_ISSET( sig->read_fd, fds );
}
