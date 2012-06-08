#ifndef CLIENT_H
#define CLIENT_H


struct client {
	int     socket;
	
	int     fileno;
	char*   mapped;

	struct self_pipe * stop_signal;
	
	struct server* serve; /* FIXME: remove above duplication */
};


void* client_serve(void* client_uncast);
struct client * client_create( struct server * serve, int socket );
void client_destroy( struct client * client );
void client_signal_stop( struct client * client );

#endif
