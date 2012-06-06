#ifndef CLIENT_H
#define CLIENT_H

struct client {
	int     socket;
	
	int     fileno;
	char*   mapped;
	
	struct server* serve; /* FIXME: remove above duplication */
};


void* client_serve(void* client_uncast);

#endif
