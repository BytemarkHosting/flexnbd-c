#include "listen.h"
#include "serve.h"
#include "util.h"

#include <stdlib.h>

struct listen * listen_create( 
		char* s_ip_address, 
		char* s_rebind_ip_address, 
		char* s_port, 
		char* s_rebind_port, 
		char* s_file,
		char *s_ctrl_sock, 
		int default_deny,
		int acl_entries, 
		char** s_acl_entries,
		int max_nbd_clients )
{
	struct listen * listen;

	listen = (struct listen *)xmalloc( sizeof( listen ) );
	listen->init_serve = server_create( s_ip_address, 
			s_port, 
			s_file,
			s_ctrl_sock, 
			default_deny, 
			acl_entries, 
			s_acl_entries, 
			1, 0);
	listen->main_serve = server_create( 
			s_rebind_ip_address ? s_rebind_ip_address : s_ip_address, 
			s_rebind_port ? s_rebind_port : s_port, 
			s_file,
			s_ctrl_sock, 
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


void listen_switch( struct listen * listen )
{
	NULLCHECK( listen );

	/* TODO: Copy acl from init_serve to main_serve */
	/* TODO: rename underlying file from foo.INCOMPLETE to foo */
}


void listen_cleanup( void * unused __attribute__((unused)) )
{
}

void do_listen( struct listen * listen )
{
	NULLCHECK( listen );

	int have_control = 0;

	have_control = do_serve( listen->init_serve );

	if( have_control ) {
		info( "Taking control.");
		listen_switch( listen );
		server_destroy( listen->init_serve );
		info( "Switched to the main server, serving." );
		do_serve( listen->main_serve );
	}
	else {
		warn("Failed to take control, giving up.");
		server_destroy( listen->init_serve );
	}
	server_destroy( listen->main_serve );

	info("Listen done.");
	listen_cleanup( listen );
}

