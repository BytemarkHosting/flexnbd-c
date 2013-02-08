#include "nbdtypes.h"

#include <string.h>
#include <endian.h>


/**
 * We intentionally ignore the reserved 128 bytes at the end of the
 * request, since there's nothing we can do with them.
 */
void nbd_r2h_init( struct nbd_init_raw * from, struct nbd_init * to )
{
	memcpy( to->passwd, from->passwd, 8 );
	to->magic = be64toh( from->magic );
	to->size = be64toh( from->size );
}

void nbd_h2r_init( struct nbd_init * from, struct nbd_init_raw * to)
{
	memcpy( to->passwd, from->passwd, 8 );
	to->magic = htobe64( from->magic );
	to->size = htobe64( from->size );
}


void nbd_r2h_request( struct nbd_request_raw *from, struct nbd_request * to )
{
	to->magic = htobe32( from->magic );
	to->type = htobe32( from->type );
	memcpy( to->handle, from->handle, 8 );
	to->from = htobe64( from->from );
	to->len = htobe32( from->len );
}

void nbd_h2r_request( struct nbd_request * from, struct nbd_request_raw * to )
{
	to->magic = be32toh( from->magic );
	to->type = be32toh( from->type );
	memcpy( to->handle, from->handle, 8 );
	to->from = be64toh( from->from );
	to->len = be32toh( from->len );
}


void nbd_r2h_reply( struct nbd_reply_raw * from, struct nbd_reply * to )
{
	to->magic = htobe32( from->magic );
	to->error = htobe32( from->error );
	memcpy( to->handle, from->handle, 8 );
}

void nbd_h2r_reply( struct nbd_reply * from, struct nbd_reply_raw * to )
{
	to->magic = be32toh( from->magic );
	to->error = be32toh( from->error );
	memcpy( to->handle, from->handle, 8 );
}

