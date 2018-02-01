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

#define NBD_FLAG_HAS_FLAGS         (1 << 0)
#define NBD_FLAG_READ_ONLY         (1 << 1)
#define NBD_FLAG_SEND_FLUSH        (1 << 2)
#define NBD_FLAG_SEND_FUA          (1 << 3)
#define NBD_FLAG_ROTATIONAL        (1 << 4)
#define NBD_FLAG_SEND_TRIM         (1 << 5)
#define NBD_FLAG_SEND_WRITE_ZEROES (1 << 6)

/* The top 2 bytes of the type field are overloaded and can contain flags */
#define REQUEST_MASK 0x0000ffff


/* 1MiB is the de-facto standard for maximum size of header + data */
#define NBD_MAX_SIZE ( 32 * 1024 * 1024 )

#define NBD_REQUEST_SIZE ( sizeof( struct nbd_request_raw ) )
#define NBD_REPLY_SIZE   ( sizeof( struct nbd_reply_raw ) )

#include <linux/types.h>
#include <inttypes.h>

typedef union nbd_handle_t {
	uint8_t b[8];
	uint64_t w;
} nbd_handle_t;

/* The _raw types are the types as they appear on the wire.  Non-_raw
 * types are in host-format.
 * Conversion functions are _r2h_ for converting raw to host, and _h2r_
 * for converting host to raw.
 */
struct nbd_init_raw {
	char passwd[8];
	__be64 magic;
	__be64 size;
	__be32 flags;
	char reserved[124];
};

struct nbd_request_raw {
	__be32 magic;
	__be32 type;    /* == READ || == WRITE  */
	nbd_handle_t handle;
	__be64 from;
	__be32 len;
} __attribute__((packed));

struct nbd_reply_raw {
	__be32 magic;
	__be32 error;           /* 0 = ok, else error   */
	nbd_handle_t handle;         /* handle you got from request  */
};

struct nbd_init {
	char passwd[8];
	uint64_t magic;
	uint64_t size;
	uint32_t flags;
	char reserved[124];
};

struct nbd_request {
	uint32_t magic;
	uint32_t type;    /* == READ || == WRITE || == DISCONNECT */
	nbd_handle_t handle;
	uint64_t from;
	uint32_t len;
} __attribute__((packed));

struct nbd_reply {
	uint32_t magic;
	uint32_t error;           /* 0 = ok, else error   */
	nbd_handle_t handle;      /* handle you got from request  */
};

void nbd_r2h_init( struct nbd_init_raw * from, struct nbd_init * to );
void nbd_r2h_request( struct nbd_request_raw *from, struct nbd_request * to );
void nbd_r2h_reply( struct nbd_reply_raw * from, struct nbd_reply * to );

void nbd_h2r_init( struct nbd_init * from, struct nbd_init_raw * to);
void nbd_h2r_request( struct nbd_request * from, struct nbd_request_raw * to );
void nbd_h2r_reply( struct nbd_reply * from, struct nbd_reply_raw * to );

#endif

