#ifndef __CONTROL_H
#define __CONTROL_H

void accept_control_connection(struct server* params, int client_fd, union mysockaddr* client_address);
void serve_open_control_socket(struct server* params);

#endif

