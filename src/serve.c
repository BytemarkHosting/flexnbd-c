#include "serve.h"
#include "client.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "sockutil.h"
#include "util.h"
#include "bitset.h"
#include "control.h"
#include "self_pipe.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

struct server * server_create (
	struct flexnbd * flexnbd,
	char* s_ip_address,
	char* s_port,
	char* s_file,
	int default_deny,
	int acl_entries,
	char** s_acl_entries,
	int max_nbd_clients,
	int success)
{
	NULLCHECK( flexnbd );
	struct server * out;
	out = xmalloc( sizeof( struct server ) );
	out->flexnbd = flexnbd;
	out->success = success;
	out->max_nbd_clients = max_nbd_clients;
	out->nbd_client = xmalloc( max_nbd_clients * sizeof( struct client_tbl_entry ) );

	out->tcp_backlog = 10; /* does this need to be settable? */

	FATAL_IF_NULL(s_ip_address, "No IP address supplied");
	FATAL_IF_NULL(s_port, "No port number supplied");
	FATAL_IF_NULL(s_file, "No filename supplied");
	NULLCHECK( s_ip_address );
	FATAL_IF_ZERO(
		parse_ip_to_sockaddr(&out->bind_to.generic, s_ip_address),
		"Couldn't parse server address '%s' (use 0 if "
		"you want to bind to all IPs)",
		s_ip_address
	);


	out->acl = acl_create( acl_entries, s_acl_entries, default_deny );
	if (out->acl && out->acl->len != acl_entries) {
		fatal("Bad ACL entry '%s'", s_acl_entries[out->acl->len]);
	}

	parse_port( s_port, &out->bind_to.v4 );

	out->filename = s_file;
	out->filename_incomplete = xmalloc(strlen(s_file)+11+1);
	strcpy(out->filename_incomplete, s_file);
	strcpy(out->filename_incomplete + strlen(s_file), ".INCOMPLETE");

	out->l_io = flexthread_mutex_create();
	out->l_acl= flexthread_mutex_create();
	out->l_start_mirror = flexthread_mutex_create();

	out->mirror_can_start = 1;

	out->close_signal = self_pipe_create();
	out->acl_updated_signal = self_pipe_create();

	NULLCHECK( out->close_signal );
	NULLCHECK( out->acl_updated_signal );

	return out;
}

void server_destroy( struct server * serve )
{
	self_pipe_destroy( serve->acl_updated_signal );
	serve->acl_updated_signal = NULL;
	self_pipe_destroy( serve->close_signal );
	serve->close_signal = NULL;

	flexthread_mutex_destroy( serve->l_start_mirror );
	flexthread_mutex_destroy( serve->l_acl );
	flexthread_mutex_destroy( serve->l_io );

	if ( serve->acl ) {
		acl_destroy( serve->acl );
		serve->acl = NULL;
	}

	free( serve->filename_incomplete );

	free( serve->nbd_client );
	free( serve );
}


void server_unlink( struct server * serve )
{
	NULLCHECK( serve );
	NULLCHECK( serve->filename );

	FATAL_IF_NEGATIVE( unlink( serve->filename ),
			"Failed to unlink %s: %s",
			serve->filename,
			strerror( errno ) );

}


void server_dirty(struct server *serve, off64_t from, int len)
{
	NULLCHECK( serve );

	if (serve->mirror) {
		bitset_set_range(serve->mirror->dirty_map, from, len);
	}
}

#define SERVER_LOCK( s, f, msg ) \
	do { NULLCHECK( s ); \
	 FATAL_IF( 0 != flexthread_mutex_lock( s->f ), msg ); } while (0)
#define SERVER_UNLOCK( s, f, msg ) \
	do { NULLCHECK( s ); \
	 FATAL_IF( 0 != flexthread_mutex_unlock( s->f ), msg ); } while (0)

void server_lock_io( struct server * serve)
{
	debug("IO locking");

	SERVER_LOCK( serve, l_io, "Problem with I/O lock" );
}

void server_unlock_io( struct server* serve )
{
	debug("IO unlocking");

	SERVER_UNLOCK( serve, l_io, "Problem with I/O unlock" );
}


/* This is only to be called from error handlers. */
int server_io_locked( struct server * serve )
{
	NULLCHECK( serve );
	return flexthread_mutex_held( serve->l_io );
}



void server_lock_acl( struct server *serve )
{
	debug("ACL locking");

	SERVER_LOCK( serve, l_acl, "Problem with ACL lock" );
}

void server_unlock_acl( struct server *serve )
{
	debug( "ACL unlocking" );

	SERVER_UNLOCK( serve, l_acl, "Problem with ACL unlock" );
}


int server_acl_locked( struct server * serve )
{
	NULLCHECK( serve );
	return flexthread_mutex_held( serve->l_acl );
}


void server_lock_start_mirror( struct server *serve )
{
	debug("Mirror start locking");

	SERVER_LOCK( serve, l_start_mirror, "Problem with start mirror lock" );
}

void server_unlock_start_mirror( struct server *serve )
{
	debug("Mirror start unlocking");

	SERVER_UNLOCK( serve, l_start_mirror, "Problem with start mirror unlock" );
}

int server_start_mirror_locked( struct server * serve )
{
	NULLCHECK( serve );
	return flexthread_mutex_held( serve->l_start_mirror );
}

/** Return the actual port the server bound to.  This is used because we
 * are allowed to pass "0" on the command-line.
 */
int server_port( struct server * server )
{
	NULLCHECK( server );
	union mysockaddr addr;
	socklen_t len = sizeof( addr.v4 );

	if ( getsockname( server->server_fd, &addr.v4, &len ) < 0 ) {
		fatal( "Failed to get the port number." );
	}

	return be16toh( addr.v4.sin_port );
}


/** Prepares a listening socket for the NBD server, binding etc. */
void serve_open_server_socket(struct server* params)
{
	NULLCHECK( params );

	params->server_fd = socket(params->bind_to.generic.sa_family == AF_INET ?
	  PF_INET : PF_INET6, SOCK_STREAM, 0);

	FATAL_IF_NEGATIVE( params->server_fd, "Couldn't create server socket" );

	/* We need SO_REUSEADDR so that when we switch from listening to
	 * serving we don't have to change address if we don't want to.
	 *
	 * If this fails, it's not necessarily bad in principle, but at
	 * this point in the code we can't tell if it's going to be a
	 * problem.  It's also indicative of something odd going on, so
	 * we barf.
	 */
	FATAL_IF_NEGATIVE(
		sock_set_reuseaddr( params->server_fd, 1 ), "Couldn't set SO_REUSEADDR"
	);

	/* TCP_NODELAY makes everything not be slow.  If we can't set
	 * this, again, there's something odd going on which we don't
	 * understand.
	 */
	FATAL_IF_NEGATIVE(
		sock_set_tcp_nodelay( params->server_fd, 1 ), "Couldn't set TCP_NODELAY"
	);

	/* If we can't bind, presumably that's because someone else is
	 * squatting on our ip/port combo, or the ip isn't yet
	 * configured. Ideally we want to retry this. */
	FATAL_UNLESS_ZERO(
		sock_try_bind( params->server_fd, &params->bind_to.generic ),
		SHOW_ERRNO( "Failed to bind() socket" )
	);

	FATAL_IF_NEGATIVE(
		listen(params->server_fd, params->tcp_backlog),
		"Couldn't listen on server socket"
	);
}



int tryjoin_client_thread( struct client_tbl_entry *entry, int (*joinfunc)(pthread_t, void **) )
{

	NULLCHECK( entry );
	NULLCHECK( joinfunc );

	int was_closed = 0;
	void * status=NULL;
	int join_errno;

	if (entry->thread != 0) {
		char s_client_address[128];

		sockaddr_address_string( &entry->address.generic, &s_client_address[0], 128 );

		debug( "%s(%p,...)", joinfunc == pthread_join ? "joining" : "tryjoining", entry->thread );
		join_errno = joinfunc(entry->thread, &status);

		/* join_errno can legitimately be ESRCH if the thread is
		 * already dead, but the client still needs tidying up. */
		if (join_errno != 0 && !entry->client->stopped ) {
			debug( "join_errno was %s, stopped was %d", strerror( join_errno ), entry->client->stopped );
			FATAL_UNLESS( join_errno == EBUSY,
					"Problem with joining thread %p: %s",
					entry->thread,
					strerror(join_errno) );
		}
		else if ( join_errno == 0 ) {
			debug("nbd thread %016x exited (%s) with status %ld",
					entry->thread,
					s_client_address,
					(uint64_t)status);
			client_destroy( entry->client );
			entry->client = NULL;
			entry->thread = 0;
			was_closed  = 1;
		}
	}

	return was_closed;
}


/**
 * Check to see if a client thread has finished, and if so, tidy up
 * after it.
 * Returns 1 if the thread was cleaned up and the slot freed, 0
 * otherwise.
 *
 * It's important that client_destroy gets called in the same thread
 * which signals the client threads to stop.  This avoids the
 * possibility of sending a stop signal via a signal which has already
 * been destroyed.  However, it means that stopped client threads,
 * including their signal pipes, won't be cleaned up until the next new
 * client connection attempt.
 */
int cleanup_client_thread( struct client_tbl_entry * entry )
{
	return tryjoin_client_thread( entry, pthread_tryjoin_np );
}

void cleanup_client_threads( struct client_tbl_entry * entries, size_t entries_len )
{
	size_t i;
	for( i = 0; i < entries_len; i++ ) {
		cleanup_client_thread( &entries[i] );
	}
}


/**
 * Join a client thread after having sent a stop signal to it.
 * This function will not return until pthread_join has returned, so
 * ensures that the client thread is dead.
 */
int join_client_thread( struct client_tbl_entry *entry )
{
	return tryjoin_client_thread( entry, pthread_join );
}

/** We can only accommodate MAX_NBD_CLIENTS connections at once.  This function
 *  goes through the current list, waits for any threads that have finished
 *  and returns the next slot free (or -1 if there are none).
 */
int cleanup_and_find_client_slot(struct server* params)
{
	NULLCHECK( params );

	int slot=-1, i;

	cleanup_client_threads( params->nbd_client, params->max_nbd_clients );

	for ( i = 0; i < params->max_nbd_clients; i++ ) {
		if( params->nbd_client[i].thread == 0 && slot == -1 ){
			slot = i;
			break;
		}
	}

	return slot;
}


/** Check whether the address client_address is allowed or not according
 * to the current acl.  If params->acl is NULL, the result will be 1,
 * otherwise it will be the result of acl_includes().
 */
int server_acl_accepts( struct server *params, union mysockaddr * client_address )
{
	NULLCHECK( params );
	NULLCHECK( client_address );

	struct acl * acl;
	int accepted;

	server_lock_acl( params );
	{
		acl = params->acl;
		accepted = acl ? acl_includes( acl, client_address ) : 1;
	}
	server_unlock_acl( params );

	return accepted;
}


int server_should_accept_client(
		struct server * params,
		union mysockaddr * client_address,
		char *s_client_address,
		size_t s_client_address_len )
{
	NULLCHECK( params );
	NULLCHECK( client_address );
	NULLCHECK( s_client_address );

	const char* result = sockaddr_address_string(
		&client_address->generic, s_client_address, s_client_address_len
	);

	if ( NULL == result ) {
		warn( "Rejecting client %s: Bad client_address", s_client_address );
		return 0;
	}

	if ( !server_acl_accepts( params, client_address ) ) {
		warn( "Rejecting client %s: Access control error", s_client_address );
		debug( "We %s have an acl, and default_deny is %s",
				(params->acl ? "do" : "do not"),
				(params->acl->default_deny ? "true" : "false") );
 		return 0;
	}

	return 1;
}



int spawn_client_thread(
	struct client * client_params,
	pthread_t *out_thread)
{
	int result = pthread_create(out_thread, NULL, client_serve, client_params);

	return result;
}


/** Dispatch function for accepting an NBD connection and starting a thread
  * to handle it.  Rejects the connection if there is an ACL, and the far end's
  * address doesn't match, or if there are too many clients already connected.
  */
void accept_nbd_client(
		struct server* params,
		int client_fd,
		union mysockaddr* client_address)
{
	NULLCHECK(params);
	NULLCHECK(client_address);

	struct client* client_params;
	int slot;
	char s_client_address[64] = {0};


	if ( !server_should_accept_client( params, client_address, s_client_address, 64 ) ) {
		FATAL_IF_NEGATIVE( close( client_fd ),
			"Error closing client socket fd %d", client_fd );
		debug("Closed client socket fd %d", client_fd);
		return;
	}

	slot = cleanup_and_find_client_slot(params);
	if (slot < 0) {
		warn("too many clients to accept connection");
		FATAL_IF_NEGATIVE( close( client_fd ),
			"Error closing client socket fd %d", client_fd );
		debug("Closed client socket fd %d", client_fd);
		return;
	}

	info( "Client %s accepted on fd %d.", s_client_address, client_fd );
	client_params = client_create( params, client_fd );

	params->nbd_client[slot].client = client_params;
	memcpy(&params->nbd_client[slot].address, client_address,
	  sizeof(union mysockaddr));

	pthread_t * thread = &params->nbd_client[slot].thread;

	if ( 0 != spawn_client_thread( client_params, thread ) ) {
		debug( "Thread creation problem." );
		client_destroy( client_params );
		FATAL_IF_NEGATIVE( close(client_fd),
			"Error closing client socket fd %d", client_fd );
		debug("Closed client socket fd %d", client_fd);
		return;
	}

	debug("nbd thread %p started (%s)", params->nbd_client[slot].thread, s_client_address);
}


void server_audit_clients( struct server * serve)
{
	NULLCHECK( serve );

	int i;
	struct client_tbl_entry * entry;

	/* There's an apparent race here.  If the acl updates while
	 * we're traversing the nbd_clients array, the earlier entries
	 * won't have been audited against the later acl.  This isn't a
	 * problem though, because in order to update the acl
	 * server_replace_acl must have been called, so the
	 * server_accept ioop will see a second acl_updated signal as
	 * soon as it hits select, and a second audit will be run.
	 */
	for( i = 0; i < serve->max_nbd_clients; i++ ) {
		entry = &serve->nbd_client[i];
		if ( 0 == entry->thread ) { continue; }
		if ( server_acl_accepts( serve, &entry->address ) ) { continue; }
		client_signal_stop( entry->client );
	}
}


int server_is_closed(struct server* serve)
{
	NULLCHECK( serve );
	return fd_is_closed( serve->server_fd );
}


void server_close_clients( struct server *params )
{
	NULLCHECK(params);

	info("closing all clients");

	int i; /* , j; */
	struct client_tbl_entry *entry;

	for( i = 0; i < params->max_nbd_clients; i++ ) {
		entry = &params->nbd_client[i];

		if ( entry->thread != 0 ) {
			debug( "Stop signaling client %p", entry->client );
			client_signal_stop( entry->client );
		}
	}
	/* We don't join the clients here.  When we enter the final
	 * mirror pass, we get the IO lock, then wait for the server_fd
	 * to close before sending the data, to be sure that no new
	 * clients can be accepted which might think they've written
	 * to the disc.  However, an existing client thread can be
	 * waiting for the IO lock already, so if we try to join it
	 * here, we deadlock.
	 *
	 * The client threads will be joined in serve_cleanup.
	 *
	*/
}


/** Replace the current acl with a new one.  The old one will be thrown
 * away.
 */
void server_replace_acl( struct server *serve, struct acl * new_acl )
{
	NULLCHECK(serve);
	NULLCHECK(new_acl);

	/* We need to lock around updates to the acl in case we try to
	 * destroy the old acl while checking against it.
	 */
	server_lock_acl( serve );
	{
		struct acl * old_acl = serve->acl;
		serve->acl = new_acl;
		/* We should always have an old_acl, but just in case... */
		if ( old_acl ) { acl_destroy( old_acl ); }
	}
	server_unlock_acl( serve );

	self_pipe_signal( serve->acl_updated_signal );
}


void server_prevent_mirror_start( struct server *serve )
{
	NULLCHECK( serve );

	serve->mirror_can_start = 0;
}

void server_allow_mirror_start( struct server *serve )
{
	NULLCHECK( serve );

	serve->mirror_can_start = 1;
}


/* Only call this with the mirror start lock held */
int server_mirror_can_start( struct server *serve )
{
	NULLCHECK( serve );

	return serve->mirror_can_start;
}


/* Queries to see if we are currently mirroring.  If we are, we need
 * to communicate that via the process exit status. because otherwise
 * the supervisor will assume the migration completed.
 */
int serve_shutdown_is_graceful( struct server *params )
{
	int is_mirroring = 0;
	server_lock_start_mirror( params );
	{
		if ( server_is_mirroring( params ) ) {
			is_mirroring = 1;
			warn( "Stop signal received while mirroring." );
			server_prevent_mirror_start( params );
		}
	}
	server_unlock_start_mirror( params );

	return !is_mirroring;
}


/** Accept either an NBD or control socket connection, dispatch appropriately */
int server_accept( struct server * params )
{
	NULLCHECK( params );
	debug("accept loop starting");
	int              client_fd;
	union mysockaddr client_address;
	fd_set           fds;
	socklen_t        socklen=sizeof(client_address);
	/* We select on this fd to receive OS signals (only a few of
	 * which we're interested in, see flexnbd.c */
	int              signal_fd = flexnbd_signal_fd( params->flexnbd );
	int              should_continue = 1;

	FD_ZERO(&fds);
	FD_SET(params->server_fd, &fds);
	if( 0 <  signal_fd ) { FD_SET(signal_fd, &fds); }
	self_pipe_fd_set( params->close_signal, &fds );
	self_pipe_fd_set( params->acl_updated_signal, &fds );

	FATAL_IF_NEGATIVE(select(FD_SETSIZE, &fds,
				NULL, NULL, NULL), "select() failed");

	if ( self_pipe_fd_isset( params->close_signal, &fds ) ){
		server_close_clients( params );
		should_continue = 0;
	}


	if ( 0 < signal_fd && FD_ISSET( signal_fd, &fds ) ){
		debug( "Stop signal received." );
		server_close_clients( params );
		params->success = params->success && serve_shutdown_is_graceful( params );
		should_continue = 0;
	}


	if ( self_pipe_fd_isset( params->acl_updated_signal, &fds ) ) {
		self_pipe_signal_clear( params->acl_updated_signal );
		server_audit_clients( params );
	}

	if ( FD_ISSET( params->server_fd, &fds ) ){
		client_fd = accept( params->server_fd, &client_address.generic, &socklen );
		debug("Accepted nbd client socket fd %d", client_fd);
		accept_nbd_client(params, client_fd, &client_address);
	}

	return should_continue;
}


void serve_accept_loop(struct server* params)
{
	NULLCHECK( params );
	while( server_accept( params ) );
}

void* build_allocation_map_thread(void* serve_uncast)
{
	NULLCHECK( serve_uncast );

	struct server* serve = (struct server*) serve_uncast;

	NULLCHECK( serve->filename );
	NULLCHECK( serve->allocation_map );

	int fd = open( serve->filename, O_RDONLY );
	FATAL_IF_NEGATIVE( fd, "Couldn't open %s", serve->filename );

	if ( build_allocation_map( serve->allocation_map, fd ) ) {
		serve->allocation_map_built = 1;
	}
	else {
		warn( "Didn't build allocation map for %s", serve->filename );
	}

	close( fd );
	return NULL;
}

/** Initialisation function that sets up the initial allocation map, i.e. so
  * we know which blocks of the file are allocated.
  */
void serve_init_allocation_map(struct server* params)
{
	NULLCHECK( params );
	NULLCHECK( params->filename );

	int fd = open( params->filename, O_RDONLY );
	off64_t size;

	FATAL_IF_NEGATIVE(fd, "Couldn't open %s", params->filename );
	size = lseek64( fd, 0, SEEK_END );
	params->size = size;
	FATAL_IF_NEGATIVE( size, "Couldn't find size of %s",
			   params->filename );

	params->allocation_map =
		bitset_alloc( params->size, block_allocation_resolution );

	int ok = pthread_create( &params->allocation_map_builder_thread,
				 NULL,
				 build_allocation_map_thread,
				 params );

	FATAL_IF_NEGATIVE( ok, "Couldn't create thread" );
}


/* Tell the server to close all the things. */
void serve_signal_close( struct server * serve )
{
	NULLCHECK( serve );
	info("signalling close");
	self_pipe_signal( serve->close_signal );
}


/* Block until the server closes the server_fd.
 */
void serve_wait_for_close( struct server * serve )
{
	while( !fd_is_closed( serve->server_fd ) ){
		usleep(10000);
	}
}

/* We've just had an DISCONNECT pair, so we need to shut down
 * and signal our listener that we can safely take over.
 */
void server_control_arrived( struct server *serve )
{
	debug( "server_control_arrived" );
	NULLCHECK( serve );

	if ( !serve->success ) {
		serve->success = 1;
		serve_signal_close( serve );
	}
}


void flexnbd_stop_control( struct flexnbd * flexnbd );

/** Closes sockets, frees memory and waits for all client threads to finish */
void serve_cleanup(struct server* params,
		int fatal __attribute__ ((unused)) )
{
	NULLCHECK( params );

	info("cleaning up");

	int i;
	void* status;

	if (params->server_fd){ close(params->server_fd); }

	/* need to stop background build if we're killed very early on */
	pthread_cancel(params->allocation_map_builder_thread);
	pthread_join(params->allocation_map_builder_thread, &status);
	if (params->allocation_map) {
		free(params->allocation_map);
	}

	int need_mirror_lock;
	need_mirror_lock = !server_start_mirror_locked( params );

	if ( need_mirror_lock ) { server_lock_start_mirror( params ); }
	{
		if ( server_is_mirroring( params ) ) {
			server_abandon_mirror( params );
		}
		server_prevent_mirror_start( params );
	}
	if ( need_mirror_lock ) { server_unlock_start_mirror( params ); }

	for (i=0; i < params->max_nbd_clients; i++) {
		pthread_t thread_id = params->nbd_client[i].thread;

		if (thread_id != 0) {
			debug("joining thread %p", thread_id);
			pthread_join(thread_id, &status);
		}
	}

	if ( server_start_mirror_locked( params ) ) {
		server_unlock_start_mirror( params );
	}

	if ( server_acl_locked( params ) ) {
		server_unlock_acl( params );
	}

	/* if( params->flexnbd ) { */
	/* 	if ( params->flexnbd->control ) { */
	/* 		flexnbd_stop_control( params->flexnbd ); */
	/* 	} */
	/* 	flexnbd_destroy( params->flexnbd ); */
	/* } */

	/* server_destroy( params ); */

	debug( "Cleanup done");
}


int server_is_in_control( struct server *serve )
{
	NULLCHECK( serve );
	return serve->success;
}

int server_is_mirroring( struct server * serve )
{
	NULLCHECK( serve );
	return !!serve->mirror_super;
}


void mirror_super_destroy( struct mirror_super * super );

/* This must only be called with the start_mirror lock held */
void server_abandon_mirror( struct server * serve )
{
	NULLCHECK( serve );
	if ( serve->mirror_super ) {
		/* FIXME: AWOOGA!  RACE!
		 * We can set signal_abandon after mirror_super has
		 * checked it, but before the reset.  This would lead to
		 * a hang.  However, mirror_reset doesn't change the
		 * signal_abandon flag, so it'll just terminate early on
		 * the next pass.
		 * */
		serve->mirror->signal_abandon = 1;
		pthread_t tid = serve->mirror_super->thread;
		pthread_join( tid, NULL );
		debug( "Mirror thread %p pthread_join returned", tid );

		server_allow_mirror_start( serve );
		mirror_super_destroy( serve->mirror_super );

		serve->mirror = NULL;
		serve->mirror_super = NULL;

		debug( "Mirror supervisor done." );
	}
}

int server_default_deny( struct server * serve )
{
	NULLCHECK( serve );
	return acl_default_deny( serve->acl );
}

/** Full lifecycle of the server */
int do_serve( struct server* params, struct self_pipe * open_signal )
{
	NULLCHECK( params );

	int success;

	error_set_handler((cleanup_handler*) serve_cleanup, params);
	serve_open_server_socket(params);

	/* Only signal that we are open for business once the server
	   socket is open */
	if ( NULL != open_signal ) { self_pipe_signal( open_signal ); }

	serve_init_allocation_map(params);
	serve_accept_loop(params);
	success = params->success;
	serve_cleanup(params, 0);

	return success;
}

