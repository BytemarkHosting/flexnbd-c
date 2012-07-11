#ifndef FLEXNBD_H
#define FLEXNBD_H

#include "acl.h"
#include "mirror.h"
#include "serve.h"
#include "listen.h"
#include "self_pipe.h"
#include "mbox.h"
#include "control.h"
#include "flexthread.h"

/* Carries the "globals". */
struct flexnbd {

	/* We always have a serve pointer, but it should never be
	 * dereferenced outside a flexnbd_switch_lock/unlock pair.
	 */
	struct server * serve;
	/* We only have a listen object if the process was started in
	 * listen mode.
	 */
	struct listen * listen;
	/* We only have a control object if a control socket name was
	 * passed on the command line.
	 */
	struct control * control;

	/* switch_mutex is the lock around dereferencing the serve
	 * pointer.
	 */
	struct flexthread_mutex * switch_mutex;

	/* File descriptor for a signalfd(2) signal stream. */
	int signal_fd;
};

struct flexnbd * flexnbd_create(void);
struct flexnbd * flexnbd_create_serving(
	char* s_ip_address,
	char* s_port,
	char* s_file,
	char *s_ctrl_sock,
	int default_deny,
	int acl_entries,
	char** s_acl_entries,
	int max_nbd_clients);

struct flexnbd * flexnbd_create_listening(
		char* s_ip_address, 
		char* s_rebind_ip_address, 
		char* s_port, 
		char* s_rebind_port, 
		char* s_file,
		char *s_ctrl_sock, 
		int default_deny,
		int acl_entries, 
		char** s_acl_entries,
		int max_nbd_clients );

void flexnbd_destroy( struct flexnbd * );
enum mirror_state;
enum mirror_state flexnbd_get_mirror_state( struct flexnbd * );
void flexnbd_lock_switch( struct flexnbd * );
void flexnbd_unlock_switch( struct flexnbd * );
int flexnbd_switch_locked( struct flexnbd * );
int flexnbd_default_deny( struct flexnbd * );
void flexnbd_set_server( struct flexnbd * flexnbd, struct server * serve );
void flexnbd_switch( struct flexnbd * flexnbd, struct server *(listen_cb)(struct listen *) );
int flexnbd_signal_fd( struct flexnbd * flexnbd );


int flexnbd_serve( struct flexnbd * flexnbd );
struct server * flexnbd_server( struct flexnbd * flexnbd );
void flexnbd_replace_acl( struct flexnbd * flexnbd, struct acl * acl );
struct status * flexnbd_status_create( struct flexnbd * flexnbd );
#endif
