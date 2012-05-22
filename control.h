#ifndef __CONTROL_H
#define __CONTROL_H

void accept_control_connection(struct mode_serve_params* params, int client_fd, struct sockaddr* client_address);
void serve_open_control_socket(struct mode_serve_params* params);

#endif

