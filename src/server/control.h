#ifndef CONTROL_H
#define CONTROL_H

/* We need this to avoid a complaint about struct server * in
 * void accept_control_connection
 */
struct server;

#include "parse.h"
#include "mirror.h"
#include "serve.h"
#include "flexnbd.h"
#include "mbox.h"

struct control {
    struct flexnbd *flexnbd;
    int control_fd;
    const char *socket_name;

    pthread_t thread;

    struct self_pipe *open_signal;
    struct self_pipe *close_signal;

    /* This is owned by the control object, and used by a
     * mirror_super to communicate the state of a mirror attempt as
     * early as feasible.  It can't be owned by the mirror_super
     * object because the mirror_super object can be freed at any
     * time (including while the control_client is waiting on it),
     * whereas the control object lasts for the lifetime of the
     * process (and we can only have a mirror thread if the control
     * thread has started it).
     */
    struct mbox *mirror_state_mbox;
};

struct control_client {
    int socket;
    struct flexnbd *flexnbd;

    /* Passed in on creation.  We know it's all right to do this
     * because we know there's only ever one control_client.
     */
    struct mbox *mirror_state_mbox;
};

struct control *control_create(struct flexnbd *,
			       const char *control_socket_name);
void control_signal_close(struct control *);
void control_destroy(struct control *);

void *control_runner(void *);

void accept_control_connection(struct server *params, int client_fd,
			       union mysockaddr *client_address);
void serve_open_control_socket(struct server *params);

#endif
