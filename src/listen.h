#ifndef LISTEN_H
#define LISTEN_H

#include "flexnbd.h"
#include "serve.h"

struct listen {
	struct flexnbd * flexnbd;
	struct server * init_serve;
	struct server * main_serve;
};

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
		int max_nbd_clients );
void listen_destroy( struct listen* );

int do_listen( struct listen * );

#endif
