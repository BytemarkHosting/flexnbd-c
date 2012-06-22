#include "serve.h"
#include "client.h"
#include "nbdtypes.h"
#include "ioutil.h"
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

static inline void* sockaddr_address_data(struct sockaddr* sockaddr)
{
	NULLCHECK( sockaddr );

	struct sockaddr_in*  in  = (struct sockaddr_in*) sockaddr;
	struct sockaddr_in6* in6 = (struct sockaddr_in6*) sockaddr;
	
	if (sockaddr->sa_family == AF_INET) {
		return &in->sin_addr;
	}
	if (sockaddr->sa_family == AF_INET6) {
		return &in6->sin6_addr;
	}
	return NULL;
}

struct server * server_create (
	char* s_ip_address,
	char* s_port,
	char* s_file,
	char *s_ctrl_sock,
	int default_deny,
	int acl_entries,
	char** s_acl_entries,
	int max_nbd_clients,
	int has_control)
{
	struct server * out;
	out = xmalloc( sizeof( struct server ) );
	out->has_control = has_control;
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

	/* control_socket_name is optional. It just won't get created if
	 * we pass NULL. */
	out->control_socket_name = s_ctrl_sock;

	out->acl = acl_create( acl_entries, s_acl_entries, default_deny );
	if (out->acl && out->acl->len != acl_entries) {
		fatal("Bad ACL entry '%s'", s_acl_entries[out->acl->len]);
	}

	parse_port( s_port, &out->bind_to.v4 );

	out->filename = s_file;
	out->filename_incomplete = xmalloc(strlen(s_file)+11+1);
	strcpy(out->filename_incomplete, s_file);
	strcpy(out->filename_incomplete + strlen(s_file), ".INCOMPLETE");

	pthread_mutex_init(&out->l_io, NULL);
	pthread_mutex_init(&out->l_acl, NULL);

	out->close_signal = self_pipe_create();
	out->acl_updated_signal = self_pipe_create();
	out->vacuum_signal = self_pipe_create();

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
	self_pipe_destroy( serve->vacuum_signal );
	serve->vacuum_signal = NULL;

	pthread_mutex_destroy( &serve->l_acl );
	pthread_mutex_destroy( &serve->l_io );

	if ( serve->acl ) { 
		acl_destroy( serve->acl );
		serve->acl = NULL;
	}

	free( serve->filename_incomplete );

	free( serve->nbd_client );
	free( serve );
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
	 FATAL_IF( 0 != pthread_mutex_lock( &s->f ), msg ); } while (0)
#define SERVER_UNLOCK( s, f, msg ) \
	do { NULLCHECK( s ); \
	 FATAL_IF( 0 != pthread_mutex_unlock( &s->f ), msg ); } while (0)

void server_lock_io( struct server * serve)
{
	SERVER_LOCK( serve, l_io, "Problem with I/O lock" );
}

void server_unlock_io( struct server* serve )
{
	SERVER_UNLOCK( serve, l_io, "Problem with I/O unlock" );
}

void server_lock_acl( struct server *serve )
{
	SERVER_LOCK( serve, l_acl, "Problem with ACL lock" );
}

void server_unlock_acl( struct server *serve )
{
	SERVER_UNLOCK( serve, l_acl, "Problem with ACL unlock" );
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

	int optval=1;
	
	params->server_fd= socket(params->bind_to.generic.sa_family == AF_INET ? 
	  PF_INET : PF_INET6, SOCK_STREAM, 0);
	
	FATAL_IF_NEGATIVE(params->server_fd, 
	  "Couldn't create server socket");

	FATAL_IF_NEGATIVE(
		setsockopt(params->server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)),
		"Couldn't set SO_REUSEADDR"
	);

	FATAL_IF_NEGATIVE(
		setsockopt(params->server_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)),
		"Couldn't set TCP_NODELAY"
	);

	FATAL_IF_NEGATIVE(
		bind(params->server_fd, &params->bind_to.generic,
		  sizeof(params->bind_to)),
		"Couldn't bind server to IP address"
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
		char s_client_address[64];

		memset(s_client_address, 0, 64);
		strcpy(s_client_address, "???");
		inet_ntop( entry->address.generic.sa_family, 
				sockaddr_address_data(&entry->address.generic), 
				s_client_address, 
				64 );

		join_errno = joinfunc(entry->thread, &status);
		/* join_errno can legitimately be ESRCH if the thread is
		 * already dead, but the client still needs tidying up. */
		if (join_errno != 0 && !entry->client->stopped ) {
			FATAL_UNLESS( join_errno == EBUSY,  
					"Problem with joining thread %p: %s", 
					entry->thread,
					strerror(join_errno) );
		}
		else {
			debug("nbd thread %016x exited (%s) with status %ld", 
					entry->thread, 
					s_client_address, 
					(uint64_t)status);
			client_destroy( entry->client );
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

	if (inet_ntop(client_address->generic.sa_family,
				sockaddr_address_data(&client_address->generic),
				s_client_address, s_client_address_len ) == NULL) {
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


struct client_cleanup {
	pthread_t client_thread;
	struct self_pipe * vacuum_signal;
};


void *client_vacuum( void * cleanup_uncast )
{
	pthread_detach( pthread_self() );
	NULLCHECK( cleanup_uncast );
	struct client_cleanup *cleanup = (struct client_cleanup *)cleanup_uncast;

	pthread_join( cleanup->client_thread, NULL );
	self_pipe_signal( cleanup->vacuum_signal );
	free( cleanup );
	return NULL;
}


/* Why do we need this rather odd arrangement?  Because if we don't have
 * it, dead threads don't get tidied up until the next incoming
 * connection happens.
 */
int spawn_client_thread( 
	struct client * client_params, 
	struct self_pipe * vacuum_signal, 
	pthread_t *out_thread)
{
	struct client_cleanup * cleanup = xmalloc( sizeof( struct client_cleanup ) );
	cleanup->vacuum_signal = vacuum_signal;
	int result = pthread_create(&cleanup->client_thread, NULL, client_serve, client_params);

	if ( 0 == result ){
		pthread_t watcher;
		pthread_create( &watcher, NULL, client_vacuum, cleanup );

		*out_thread = cleanup->client_thread;
	}
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
		close( client_fd );
		return;
	}

	slot = cleanup_and_find_client_slot(params); 
	if (slot < 0) {
		warn("too many clients to accept connection");
		close(client_fd);
		return;
	}
	
	debug( "Client %s accepted.", s_client_address );
	client_params = client_create( params, client_fd );

	params->nbd_client[slot].client = client_params;
	memcpy(&params->nbd_client[slot].address, client_address, 
	  sizeof(union mysockaddr));
	
	pthread_t * thread = &params->nbd_client[slot].thread;

	if (spawn_client_thread( client_params, params->vacuum_signal, thread ) != 0) {
		debug( "Thread creation problem." );
		client_destroy( client_params );
		close(client_fd);
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

	int i, j;
	struct client_tbl_entry *entry;

	for( i = 0; i < params->max_nbd_clients; i++ ) {
		entry = &params->nbd_client[i];

		if ( entry->thread != 0 ) {
			client_signal_stop( entry->client );	
		}
	}
	for( j = 0; j < params->max_nbd_clients; j++ ) {
		join_client_thread( &params->nbd_client[j] );
	}
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



/** Accept either an NBD or control socket connection, dispatch appropriately */
int server_accept( struct server * params )
{
	NULLCHECK( params );
	debug("accept loop starting");
	int              client_fd;
	union mysockaddr client_address;
	fd_set           fds;
	socklen_t        socklen=sizeof(client_address);

	FD_ZERO(&fds);
	FD_SET(params->server_fd, &fds);
	self_pipe_fd_set( params->close_signal, &fds );
	self_pipe_fd_set( params->acl_updated_signal, &fds );
	self_pipe_fd_set( params->vacuum_signal, &fds );
	if (params->control_socket_name) {
		FD_SET(params->control_fd, &fds);
	}

	FATAL_IF_NEGATIVE(select(FD_SETSIZE, &fds, 
				NULL, NULL, NULL), "select() failed");

	if ( self_pipe_fd_isset( params->close_signal, &fds ) ){
		server_close_clients( params );
		return 0;
	}

	if ( self_pipe_fd_isset( params->vacuum_signal, &fds ) ) {
		cleanup_client_threads( params->nbd_client, params->max_nbd_clients );
		self_pipe_signal_clear( params->vacuum_signal );
	}

	if ( self_pipe_fd_isset( params->acl_updated_signal, &fds ) ) {
		self_pipe_signal_clear( params->acl_updated_signal );
		server_audit_clients( params );
	}

	if ( FD_ISSET( params->server_fd, &fds ) ){
		client_fd = accept( params->server_fd, &client_address.generic, &socklen );
		debug("Accepted nbd client socket");
		accept_nbd_client(params, client_fd, &client_address);
	} 
	else if( FD_ISSET( params->control_fd, &fds ) ) {
		client_fd = accept( params->control_fd, &client_address.generic, &socklen );
		debug("Accepted control client socket"); 
		accept_control_connection(params, client_fd, &client_address);
	}


	return 1;
}


void serve_accept_loop(struct server* params) 
{
	NULLCHECK( params );
	while( server_accept( params ) );
}

/** Initialisation function that sets up the initial allocation map, i.e. so
  * we know which blocks of the file are allocated.
  */
void serve_init_allocation_map(struct server* params)
{
	NULLCHECK( params );

	int fd = open(params->filename, O_RDONLY);
	off64_t size;

	FATAL_IF_NEGATIVE(fd, "Couldn't open %s", params->filename);
	size = lseek64(fd, 0, SEEK_END);
	params->size = size;
	FATAL_IF_NEGATIVE(size, "Couldn't find size of %s", 
			params->filename);
	params->allocation_map = 
		build_allocation_map(fd, size, block_allocation_resolution);
	close(fd);
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

/* We've just had an ENTRUST/DISCONNECT pair, so we need to shut down
 * and signal our listener that we can safely take over.
 */
void server_control_arrived( struct server *serve )
{
	NULLCHECK( serve );

	serve->has_control = 1;
	serve_signal_close( serve );
}


/** Closes sockets, frees memory and waits for all client threads to finish */
void serve_cleanup(struct server* params, 
		int fatal __attribute__ ((unused)) )
{
	NULLCHECK( params );
	
	info("cleaning up");

	int i;
	
	if (params->server_fd){ close(params->server_fd); }
	if (params->control_fd){ close(params->control_fd); }
	if (params->control_socket_name){ ; }

	if (params->allocation_map) {
		free(params->allocation_map);
	}
	
	if (params->mirror) {
		pthread_t mirror_t = params->mirror->thread;
		params->mirror->signal_abandon = 1;
		pthread_join(mirror_t, NULL);
	}
	
	for (i=0; i < params->max_nbd_clients; i++) {
		void* status;
		pthread_t thread_id = params->nbd_client[i].thread;
		
		if (thread_id != 0) {
			debug("joining thread %p", thread_id);
			pthread_join(thread_id, &status);
		}
	}
	debug( "Cleanup done");
}


int server_is_in_control( struct server *serve )
{
	NULLCHECK( serve );
	return serve->has_control;
}


/** Full lifecycle of the server */
int do_serve(struct server* params)
{
	NULLCHECK( params );

	int has_control;
	
	error_set_handler((cleanup_handler*) serve_cleanup, params);
	serve_open_server_socket(params);
	serve_open_control_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
	has_control = params->has_control;
	serve_cleanup(params, 0);
	debug("Server %s control.", has_control ? "has" : "does not have" );

	return has_control;
}

