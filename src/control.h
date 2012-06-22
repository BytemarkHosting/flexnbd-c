#ifndef CONTROL_H
#define CONTROL_H

/* MS_CONNECT_TIME_SECS
 * The length of time after which the sender will assume a connect() to
 * the destination has failed.
 */
#define MS_CONNECT_TIME_SECS 60

/* MS_HELLO_TIME_SECS
 * The length of time the sender will wait for the NBD hello message
 * after connect() before aborting the connection attempt.
 */
#define MS_HELLO_TIME_SECS 5


/* MS_RETRY_DELAY_SECS
 * The delay after a failed migration attempt before launching another
 * thread to try again.
 */
#define MS_RETRY_DELAY_SECS 1


#include "parse.h"
#include "serve.h"

void accept_control_connection(struct server* params, int client_fd, union mysockaddr* client_address);
void serve_open_control_socket(struct server* params);

#endif

