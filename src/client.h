#ifndef CLIENT_H
#define CLIENT_H

#include <signal.h>
#include <time.h>

/** CLIENT_MAX_WAIT_SECS
 * This is the length of time an inbound migration will wait for a fresh
 * write before assuming the source has Gone Away.  Note: it is *not*
 * the time from one write to the next, it is the gap between the end of
 * one write and the start of the next.
 */
#define CLIENT_MAX_WAIT_SECS 120

/** CLIENT_HANDLER_TIMEOUT
 * This is the length of time (in seconds) any request can be outstanding for.
 * If we spend longer than this in a request, the whole server is killed.
 */
#define CLIENT_HANDLER_TIMEOUT 120

/** CLIENT_KILLSWITCH_SIGNAL
 * The signal number we use to kill the server when *any* killswitch timer
 * fires. We don't actually need to install a signal handler for it, the default
 * behaviour is perfectly fine.
 */
#define CLIENT_KILLSWITCH_SIGNAL ( SIGRTMIN + 1 )


struct client {
	/* When we call pthread_join, if the thread is already dead
	 * we can get an ESRCH.  Since we have no other way to tell
	 * if that ESRCH is from a dead thread or a thread that never
	 * existed, we use a `stopped` flag to indicate a thread which
	 * did exist, but went away.  Only check this after a
	 * pthread_join call.
	 */
	int     stopped;
	int     socket;

	int     fileno;
	char*   mapped;

	struct self_pipe * stop_signal;

	struct server* serve; /* FIXME: remove above duplication */

	/* Have we seen a REQUEST_DISCONNECT message? */
	int     disconnect;

	/* kill the whole server if a request has been outstanding too long,
	 * assuming use_killswitch is set in serve
	 */
	timer_t killswitch;

};


void* client_serve(void* client_uncast);
struct client * client_create( struct server * serve, int socket );
void client_destroy( struct client * client );
void client_signal_stop( struct client * client );

#endif

