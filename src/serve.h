#ifndef SERVE_H
#define SERVE_H

#define _GNU_SOURCE

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>

#include "parse.h"
#include "acl.h"


static const int block_allocation_resolution = 4096;//128<<10;

enum mirror_finish_action {
	ACTION_PROXY,
	ACTION_EXIT,
	ACTION_NOTHING
};

struct mirror_status {
	pthread_t            thread;
	int                  client;
	char                 *filename;
	off64_t              max_bytes_per_second;
	enum mirror_finish_action action_at_finish;
	
	char                 *mapped;
	struct bitset_mapping *dirty_map;
};

struct control_params {
	int                       socket;
	struct server* serve;
};

struct client_tbl_entry {
	pthread_t thread;
	union mysockaddr address;
	struct client * client;
};


#define MAX_NBD_CLIENTS 16
struct server {
	/** address/port to bind to */
	union mysockaddr     bind_to;
	/** access control list */
	struct acl *         acl;
	/** (static) file name to serve */
	char*                filename;
	/** file name of INCOMPLETE flag */
	char*                filename_incomplete;
	/** TCP backlog for listen() */
	int                  tcp_backlog;
	/** (static) file name of UNIX control socket (or NULL if none) */
	char*                control_socket_name;
	/** size of file */
	off64_t              size;

	/** Claims around any I/O to this file */
	pthread_mutex_t      l_io;
	
	/** set to non-zero to cause r/w requests to go via this fd */
	int                  proxy_fd;
	
	/** to interrupt accept loop and clients, write() to close_signal[1] */
	struct self_pipe *   close_signal;

	/** acl_updated_signal will be signalled after the acl struct
	 * has been replaced
	 */
	struct self_pipe *   acl_updated_signal;

	struct mirror_status* mirror;
	int                  server_fd;
	int                  control_fd;
	
	struct bitset_mapping* allocation_map;
	
	struct client_tbl_entry nbd_client[MAX_NBD_CLIENTS];
};

int server_is_closed(struct server* serve);
void server_dirty(struct server *serve, off64_t from, int len);
int server_lock_io( struct server * serve);
void server_unlock_io( struct server* serve );
void serve_signal_close( struct server *serve );


struct mode_readwrite_params {
	union mysockaddr     connect_to;
	union mysockaddr     connect_from;
	off64_t              from;
	off64_t              len;
	int                  data_fd;
	int                  client;
};


#endif

