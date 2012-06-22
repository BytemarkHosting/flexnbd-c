#include "status.h"
#include "serve.h"
#include "util.h"

struct status * status_create( struct server * serve )
{
	NULLCHECK( serve );
	struct status * status;
	
	status = xmalloc( sizeof( struct status ) );
	status->has_control = serve->has_control;
	status->is_mirroring = NULL != serve->mirror;
	return status;

}

#define BOOL_S(var) (var ? "true" : "false" )
#define PRINT_FIELD( var ) \
	do{dprintf( fd, #var "=%s ", BOOL_S( status->var ) );}while(0)

int status_write( struct status * status, int fd )
{
	PRINT_FIELD( is_mirroring );
	PRINT_FIELD( has_control );
	dprintf(fd, "\n");
	return 1;
}


void status_destroy( struct status * status )
{
	NULLCHECK( status );
	free( status );
}
