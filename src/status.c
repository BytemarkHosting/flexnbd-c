#include "status.h"
#include "serve.h"
#include "util.h"

struct status * status_create( struct server * serve )
{
	NULLCHECK( serve );
	struct status * status;

	status = xmalloc( sizeof( struct status ) );
	status->pid = getpid();
	status->has_control = serve->success;
	status->is_mirroring = NULL != serve->mirror;
	return status;

}

#define BOOL_S(var) (var ? "true" : "false" )
#define PRINT_BOOL( var ) \
	do{dprintf( fd, #var "=%s ", BOOL_S( status->var ) );}while(0)
#define PRINT_INT( var ) \
	do{dprintf( fd, #var "=%d ", status->var );}while(0)

int status_write( struct status * status, int fd )
{
	PRINT_INT( pid );
	PRINT_BOOL( is_mirroring );
	PRINT_BOOL( has_control );
	dprintf(fd, "\n");
	return 1;
}


void status_destroy( struct status * status )
{
	NULLCHECK( status );
	free( status );
}
