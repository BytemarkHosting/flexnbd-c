#ifndef SERVE_H
#define SERVE_H

#include <sys/types.h>
#include <unistd.h>
#include <signal.h> /* for sig_atomic_t */

#include "flexnbd.h"
#include "parse.h"
#include "acl.h"


static const int block_allocation_resolution = 4096;//128<<10;


struct client_tbl_entry {
	pthread_t thread;
	union mysockaddr address;
	struct client * client;
};


#define MAX_NBD_CLIENTS 16
struct server {
	/* The flexnbd wrapper this server is attached to */
	struct flexnbd * flexnbd;

	/** address/port to bind to */
	union mysockaddr     bind_to;
	/** (static) file name to serve */
	char*                filename;
	/** TCP backlog for listen() */
	int                  tcp_backlog;
	/** (static) file name of UNIX control socket (or NULL if none) */
	char*                control_socket_name;
	/** size of file */
	uint64_t 	     size;

	/** to interrupt accept loop and clients, write() to close_signal[1] */
	struct self_pipe *   close_signal;

	/** access control list */
	struct acl *         acl;
	/** acl_updated_signal will be signalled after the acl struct
	 * has been replaced
	 */
	struct self_pipe *   acl_updated_signal;

	/* Claimed around any updates to the ACL. */
	struct flexthread_mutex *   l_acl;

	/* Claimed around starting a mirror so that it doesn't race with
	 * shutting down on a SIGTERM. */
	struct flexthread_mutex *   l_start_mirror;

	struct mirror_t * mirror;
	struct mirror_super_t * mirror_super;
	/* This is used to stop the mirror from starting after we
	 * receive a SIGTERM */
	int mirror_can_start;

	int                  server_fd;
	int                  control_fd;

	/* the allocation_map keeps track of which blocks in the backing file
	 * have been allocated, or part-allocated on disc, with unallocated
	 * blocks presumed to contain zeroes (i.e. represented as sparse files
	 * by the filesystem).  We can use this information when receiving
	 * incoming writes, and avoid writing zeroes to unallocated sections
	 * of the file which would needlessly increase disc usage.  This
	 * bitmap will start at all-zeroes for an empty file, and tend towards
	 * all-ones as the file is written to (i.e. we assume that allocated
	 * blocks can never become unallocated again, as is the case with ext3
	 * at least).
	 */
	struct bitset * allocation_map;
	/* when starting up, this thread builds the allocation_map */
	pthread_t               allocation_map_builder_thread;

	/* when the thread has finished, it sets this to 1 */
	volatile sig_atomic_t  allocation_map_built;
	volatile sig_atomic_t  allocation_map_not_built;

	int                  max_nbd_clients;
	struct client_tbl_entry *nbd_client;

	/** Should clients use the killswitch? */
	int use_killswitch;

	/** If this isn't set, newly accepted clients will be closed immediately */
	int allow_new_clients;

	/* Marker for whether this server has control over the data in
	 * the file, or if we're waiting to receive it from an inbound
	 * migration which hasn't yet finished.
	 *
	 * It's the value which controls the exit status of a serve or
	 * listen process.
	 */
	int success;
};

struct server * server_create(
		struct flexnbd * flexnbd,
		char* s_ip_address,
		char* s_port,
		char* s_file,
		int default_deny,
		int acl_entries,
		char** s_acl_entries,
		int max_nbd_clients,
		int use_killswitch,
		int success );
void server_destroy( struct server * );
int server_is_closed(struct server* serve);
void serve_signal_close( struct server *serve );
void serve_wait_for_close( struct server * serve );
void server_replace_acl( struct server *serve, struct acl * acl);
void server_control_arrived( struct server *serve );
int server_is_in_control( struct server *serve );
int server_default_deny( struct server * serve );
int server_acl_locked( struct server * serve );
void server_lock_acl( struct server *serve );
void server_unlock_acl( struct server *serve );
void server_lock_start_mirror( struct server *serve );
void server_unlock_start_mirror( struct server *serve );
int server_is_mirroring( struct server * serve );

uint64_t server_mirror_bytes_remaining( struct server * serve );
uint64_t server_mirror_eta( struct server * serve );
uint64_t server_mirror_bps( struct server * serve );

void server_abandon_mirror( struct server * serve );
void server_prevent_mirror_start( struct server *serve );
void server_allow_mirror_start( struct server *serve );
int server_mirror_can_start( struct server *serve );

/* These three functions are used by mirror around the final pass, to close
 * existing clients and prevent new ones from being around
 */

void server_forbid_new_clients( struct server *serve );
void server_close_clients( struct server *serve );
void server_join_clients( struct server *serve );
void server_allow_new_clients( struct server *serve );

/* Returns a count (ish) of the number of currently-running client threads */
int server_count_clients( struct server *params );

void server_unlink( struct server * serve );

int do_serve( struct server *, struct self_pipe * );

struct mode_readwrite_params {
	union mysockaddr     connect_to;
	union mysockaddr     connect_from;

	uint64_t             from;
	uint32_t             len;

	int                  data_fd;
	int                  client;
};


#endif

