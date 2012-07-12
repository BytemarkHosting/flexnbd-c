#ifndef SERVE_H
#define SERVE_H

#include <sys/types.h>
#include <unistd.h>

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
	/** file name of INCOMPLETE flag */
	char*                filename_incomplete;
	/** TCP backlog for listen() */
	int                  tcp_backlog;
	/** (static) file name of UNIX control socket (or NULL if none) */
	char*                control_socket_name;
	/** size of file */
	uint64_t 	     size;

	/** Claims around any I/O to this file */
	struct flexthread_mutex * l_io;
	
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

	struct mirror* mirror;
	struct mirror_super * mirror_super; 
	int                  server_fd;
	int                  control_fd;
	
	struct bitset_mapping* allocation_map;
	
	int                  max_nbd_clients;
	struct client_tbl_entry *nbd_client;


	/* Marker for whether this server has control over the data in
	 * the file, or if we're waiting to receive it from an inbound
	 * migration which hasn't yet finished.
	 */
	int has_control;
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
		int has_control );
void server_destroy( struct server * );
int server_is_closed(struct server* serve);
void server_dirty(struct server *serve, off64_t from, int len);
void server_lock_io( struct server * serve);
void server_unlock_io( struct server* serve );
void serve_signal_close( struct server *serve );
void serve_wait_for_close( struct server * serve );
void server_replace_acl( struct server *serve, struct acl * acl);
void server_control_arrived( struct server *serve );
int server_is_in_control( struct server *serve );
int server_default_deny( struct server * serve );
int server_io_locked( struct server * serve );
int server_acl_locked( struct server * serve );
void server_lock_acl( struct server *serve );
void server_unlock_acl( struct server *serve );


int do_serve( struct server * );

struct mode_readwrite_params {
	union mysockaddr     connect_to;
	union mysockaddr     connect_from;
	off64_t              from;
	off64_t              len;
	int                  data_fd;
	int                  client;
};


#endif

