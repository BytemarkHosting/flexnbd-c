#ifndef MIRROR_H
#define MIRROR_H

#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "bitset.h"
#include "self_pipe.h"
enum mirror_state;
#include "serve.h"
#include "mbox.h"


/* MS_CONNECT_TIME_SECS
 * The length of time after which the sender will assume a connect() to
 * the destination has failed.
 */
#define MS_CONNECT_TIME_SECS 60

/* MS_MAX_DOWNTIME_SECS
 * The length of time a migration must be estimated to have remaining for us to
 * disconnect clients for convergence
 *
 * TODO: Make this configurable so refusing-to-converge clients can be manually
 *       fixed.
 * TODO: Make this adaptive - 5 seconds is fine, as long as we can guarantee
 *       that all migrations will be able to converge in time. We'd add a new
 *       state between open and closed, where gradually-increasing latency is
 *       added to client requests to allow the mirror to be faster.
 */
#define MS_CONVERGE_TIME_SECS 5

/* MS_HELLO_TIME_SECS
 * The length of time the sender will wait for the NBD hello message
 * after connect() before aborting the connection attempt.
 */
#define MS_HELLO_TIME_SECS 5


/* MS_RETRY_DELAY_SECS
 * The delay after a failed migration attempt before launching another
 * thread to try again.
 */
#define MS_RETRY_DELAY_SECS 1


/* MS_REQUEST_LIMIT_SECS
 * We must receive a reply to a request within this time.  For a read
 * request, this is the time between the end of the NBD request and the
 * start of the NBD reply.  For a write request, this is the time
 * between the end of the written data and the start of the NBD reply.
 */
#define MS_REQUEST_LIMIT_SECS 4
#define MS_REQUEST_LIMIT_SECS_F 4.0

enum mirror_finish_action {
	ACTION_EXIT,
	ACTION_UNLINK,
	ACTION_NOTHING
};

enum mirror_state {
	MS_UNKNOWN,
	MS_INIT,
	MS_GO,
	MS_ABANDONED,
	MS_DONE,
	MS_FAIL_CONNECT,
	MS_FAIL_REJECTED,
	MS_FAIL_NO_HELLO,
	MS_FAIL_SIZE_MISMATCH
};

struct mirror {
	pthread_t            thread;

	/* Signal to this then join the thread if you want to abandon mirroring */
	struct self_pipe *   abandon_signal;

	union mysockaddr *   connect_to;
	union mysockaddr *   connect_from;
	int                  client;
	const char *         filename;

	/* Limiter, used to restrict migration speed Only dirty bytes (those going
	 * over the network) are considered */
	uint64_t              max_bytes_per_second;

	enum mirror_finish_action action_at_finish;

	char                 *mapped;

	/* We need to send every byte at least once; we do so by  */
	uint64_t offset;

	enum mirror_state    commit_state;

	/* commit_signal is sent immediately after attempting to connect
	 * and checking the remote size, whether successful or not.
	 */
	struct mbox *        commit_signal;

	/* The time (from monotonic_time_ms()) the migration was started. Can be
	 * used to calculate bps, etc. */
	uint64_t migration_started;

	/* Running count of all bytes we've transferred */
	uint64_t all_dirty;
};


struct mirror_super {
	struct mirror * mirror;
	pthread_t thread;
	struct mbox * state_mbox;
};



/* We need these declaration to get around circular dependencies in the
 * .h's
 */
struct server;
struct flexnbd;

struct mirror_super * mirror_super_create(
		const char * filename,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		uint64_t max_Bps,
		enum mirror_finish_action action_at_finish,
		struct mbox * state_mbox
		);
void * mirror_super_runner( void * serve_uncast );

uint64_t mirror_current_bps( struct mirror * mirror );

#endif

