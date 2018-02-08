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
#define REQUEST_FLUSH 3

/* values for transmission flag field */
#define FLAG_HAS_FLAGS	(1 << 0)	/* Flags are there */
#define FLAG_SEND_FLUSH	(1 << 2)	/* Send FLUSH */
#define FLAG_SEND_FUA	(1 << 3)	/* Send FUA (Force Unit Access) */

/* values for command flag field */
#define CMD_FLAG_FUA     (1 << 0)

#if 0
/* Not yet implemented by flexnbd */
#define REQUEST_TRIM 4
#define REQUEST_WRITE_ZEROES 6

#define FLAG_READ_ONLY	(1 << 1)	/* Device is read-only */
#define FLAG_ROTATIONAL	(1 << 4)	/* Use elevator algorithm - rotational media */
#define FLAG_SEND_TRIM	(1 << 5)	/* Send TRIM (discard) */
#define FLAG_SEND_WRITE_ZEROES (1 << 6) /* Send NBD_CMD_WRITE_ZEROES */
#define FLAG_CAN_MULTI_CONN    (1 << 8) /* multiple connections are okay */

#define CMD_FLAG_NO_HOLE (1 << 1)
#endif


/* 32 MiB is the maximum qemu will send you:
 * https://github.com/qemu/qemu/blob/v2.11.0/include/block/nbd.h#L183
 */
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
	__be16 flags;
	__be16 type;    /* == READ || == WRITE || == FLUSH */
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
	uint16_t flags;
	uint16_t type;    /* == READ || == WRITE || == DISCONNECT || == FLUSH */
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

