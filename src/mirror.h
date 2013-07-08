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


enum mirror_finish_action {
	ACTION_EXIT,
	ACTION_UNLINK,
	ACTION_NOTHING
};

enum mirror_state {
	MS_UNKNOWN,
	MS_INIT,
	MS_GO,
	MS_DONE,
	MS_FAIL_CONNECT,
	MS_FAIL_REJECTED,
	MS_FAIL_NO_HELLO,
	MS_FAIL_SIZE_MISMATCH
};

struct mirror {
	pthread_t            thread;
	/* set to 1, then join thread to make mirror terminate early */
	int                  signal_abandon;
	union mysockaddr *   connect_to;
	union mysockaddr *   connect_from;
	int                  client;
	const char *         filename;
	off64_t              max_bytes_per_second;
	enum mirror_finish_action action_at_finish;

	char                 *mapped;
	struct bitset_mapping *dirty_map;

	enum mirror_state    commit_state;

	/* commit_signal is sent immediately after attempting to connect
	 * and checking the remote size, whether successful or not.
	 */
	struct mbox *        commit_signal;

	/* The current mirror pass. We put this here so status can query it */
	int pass;

	/* The number of dirty (had to send to dest) and clean (could skip) bytes
	 * for this pass. Add them together and subtract from size to get remaining
	 * bytes. */
	uint64_t this_pass_dirty;
	uint64_t this_pass_clean;
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
		int max_Bps,
		int action_at_finish,
		struct mbox * state_mbox
		);
void * mirror_super_runner( void * serve_uncast );
#endif

