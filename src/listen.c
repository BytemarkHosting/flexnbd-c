#include "listen.h"
#include "serve.h"
#include "util.h"
#include "flexnbd.h"

#include <stdlib.h>

struct listen * listen_create( 
		struct flexnbd * flexnbd,
		char* s_ip_address, 
		char* s_rebind_ip_address, 
		char* s_port, 
		char* s_rebind_port, 
		char* s_file,
		int default_deny,
		int acl_entries, 
		char** s_acl_entries,
		int max_nbd_clients )
{
	NULLCHECK( flexnbd );
	struct listen * listen;

	listen = (struct listen *)xmalloc( sizeof( struct listen ) );
	listen->flexnbd = flexnbd;
	listen->init_serve = server_create( 
			flexnbd,
			s_ip_address, 
			s_port, 
			s_file,
			default_deny, 
			acl_entries, 
			s_acl_entries, 
			1, 0);
	listen->main_serve = server_create( 
			flexnbd,
			s_rebind_ip_address ? s_rebind_ip_address : s_ip_address, 
			s_rebind_port ? s_rebind_port : s_port,
			s_file,
			default_deny, 
			acl_entries, 
			s_acl_entries, 
			max_nbd_clients, 1);
	return listen;
}


void listen_destroy( struct listen * listen )
{
	NULLCHECK( listen );
	free( listen );
}


struct server *listen_switch( struct listen * listen )
{
	NULLCHECK( listen );

	/* TODO: Copy acl from init_serve to main_serve */
	/* TODO: rename underlying file from foo.INCOMPLETE to foo */
	
	server_destroy( listen->init_serve );
	listen->init_serve = NULL;
	info( "Switched to the main server, serving." );
	return listen->main_serve;
}


void listen_cleanup( void * unused __attribute__((unused)) )
{
}

int do_listen( struct listen * listen )
{
	NULLCHECK( listen );

	int have_control = 0;

	flexnbd_switch_lock( listen->flexnbd );
	{
		flexnbd_set_server( listen->flexnbd, listen->init_serve );
	}
	flexnbd_switch_unlock( listen->flexnbd );

	/* WATCH FOR RACES HERE: flexnbd->serve is set, but the server
	 * isn't running yet and the switch lock is released.
	 */
	have_control = do_serve( listen->init_serve );


	if( have_control ) {
		info( "Taking control.");
		flexnbd_switch( listen->flexnbd, listen_switch );
		/* WATCH FOR RACES HERE: the server hasn't been
		 * restarted before we release the flexnbd switch lock.
		 * do_serve doesn't return, so there's not a lot of
		 * choice about that.
		 */
		do_serve( listen->main_serve );
	}
	else {
		warn("Failed to take control, giving up.");
		server_destroy( listen->init_serve );
		listen->init_serve = NULL;
	}
	/* TODO: here we must signal the control thread to stop before
	 * it tries to  */
	server_destroy( listen->main_serve );
	listen->main_serve = NULL;

	debug("Listen done, cleaning up");
	listen_cleanup( listen );

	return have_control;
}

