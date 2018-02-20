#include "status.h"
#include "serve.h"
#include "util.h"

struct status *status_create(struct server *serve)
{
    NULLCHECK(serve);
    struct status *status;

    status = xmalloc(sizeof(struct status));
    status->pid = getpid();
    status->size = serve->size;
    status->has_control = serve->success;

    status->clients_allowed = serve->allow_new_clients;
    status->num_clients = server_count_clients(serve);

    server_lock_start_mirror(serve);

    status->is_mirroring = NULL != serve->mirror;
    if (status->is_mirroring) {
	status->migration_duration = monotonic_time_ms();

	if ((serve->mirror->migration_started) <
	    status->migration_duration) {
	    status->migration_duration -= serve->mirror->migration_started;
	} else {
	    status->migration_duration = 0;
	}
	status->migration_duration /= 1000;
	status->migration_speed = server_mirror_bps(serve);
	status->migration_speed_limit =
	    serve->mirror->max_bytes_per_second;

	status->migration_seconds_left = server_mirror_eta(serve);
	status->migration_bytes_left =
	    server_mirror_bytes_remaining(serve);
    }

    server_unlock_start_mirror(serve);

    return status;

}

#define BOOL_S(var) (var ? "true" : "false" )
#define PRINT_BOOL( var ) \
	do{dprintf( fd, #var "=%s ", BOOL_S( status->var ) );}while(0)
#define PRINT_INT( var ) \
	do{dprintf( fd, #var "=%d ", status->var );}while(0)
#define PRINT_UINT64( var ) \
	do{dprintf( fd, #var "=%"PRIu64" ", status->var );}while(0)

int status_write(struct status *status, int fd)
{
    PRINT_INT(pid);
    PRINT_UINT64(size);
    PRINT_BOOL(is_mirroring);
    PRINT_BOOL(clients_allowed);
    PRINT_INT(num_clients);
    PRINT_BOOL(has_control);

    if (status->is_mirroring) {
	PRINT_UINT64(migration_speed);
	PRINT_UINT64(migration_duration);
	PRINT_UINT64(migration_seconds_left);
	PRINT_UINT64(migration_bytes_left);
	if (status->migration_speed_limit < UINT64_MAX) {
	    PRINT_UINT64(migration_speed_limit);
	};
    }

    dprintf(fd, "\n");
    return 1;
}


void status_destroy(struct status *status)
{
    NULLCHECK(status);
    free(status);
}
