#ifndef __NBDTYPES_H
#define __NBDTYPES_H


/* http://linux.derkeiler.com/Mailing-Lists/Kernel/2003-09/2332.html */
#define INIT_PASSWD "NBDMAGIC" 
#define INIT_MAGIC 0x0000420281861253 
#define REQUEST_MAGIC 0x25609513 
#define REPLY_MAGIC 0x67446698 
#define REQUEST_READ 0 
#define REQUEST_WRITE 1 
#define REQUEST_DISCONNECT 2 

#ifndef _LARGEFILE64_SOURCE
# define _LARGEFILE64_SOURCE
#endif

#include <linux/types.h>
#include <inttypes.h>

/* The _raw types are the types as they appear on the wire.  Non-_raw
 * types are in host-format.
 * Conversion functions are _r2h_ for converting raw to host, and _h2r_
 * for converting host to raw.
 */
struct nbd_init_raw {
	char passwd[8];
	__be64 magic;
	__be64 size;
	char reserved[128];
};

struct nbd_request_raw {
	__be32 magic;
	__be32 type;    /* == READ || == WRITE  */
	char handle[8];
	__be64 from;
	__be32 len;
} __attribute__((packed));

struct nbd_reply_raw {
	__be32 magic;
	__be32 error;           /* 0 = ok, else error   */
	char handle[8];         /* handle you got from request  */
};



struct nbd_init {
	char passwd[8];
	uint64_t magic;
	uint64_t size;
	char reserved[128];
};

struct nbd_request {
	uint32_t magic;
	uint32_t type;    /* == READ || == WRITE  */
	char handle[8];
	uint64_t from;
	uint32_t len;
} __attribute__((packed));

struct nbd_reply {
	uint32_t magic;
	uint32_t error;           /* 0 = ok, else error   */
	char handle[8];         /* handle you got from request  */
};


void nbd_r2h_init( struct nbd_init_raw * from, struct nbd_init * to );
void nbd_r2h_request( struct nbd_request_raw *from, struct nbd_request * to );
void nbd_r2h_reply( struct nbd_reply_raw * from, struct nbd_reply * to );

void nbd_h2r_init( struct nbd_init * from, struct nbd_init_raw * to);
void nbd_h2r_request( struct nbd_request * from, struct nbd_request_raw * to );
void nbd_h2r_reply( struct nbd_reply * from, struct nbd_reply_raw * to );

#endif

