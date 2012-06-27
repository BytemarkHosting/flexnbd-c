#ifndef CONTROL_H
#define CONTROL_H


#include "parse.h"
#include "mirror.h"
#include "control.h"
#include "flexnbd.h"

struct control {
	struct flexnbd * flexnbd;
	int control_fd;
	const char * socket_name;

	pthread_t thread;
	
	struct self_pipe * close_signal;
};

struct control_client{
	int              socket;
	struct flexnbd * flexnbd;
};

struct control * control_create(struct flexnbd *, const char * control_socket_name);
void control_signal_close( struct control * );
void control_destroy( struct control * );

void * control_runner( void * );

void accept_control_connection(struct server* params, int client_fd, union mysockaddr* client_address);
void serve_open_control_socket(struct server* params);

#endif

