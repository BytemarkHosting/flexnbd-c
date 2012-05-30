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

#include <linux/types.h>
struct nbd_init {
	char passwd[8];
	__be64 magic;
	__be64 size;
	char reserved[128];
};

struct nbd_request {
        __be32 magic;
        __be32 type;    /* == READ || == WRITE  */
        char handle[8];
        __be64 from;
        __be32 len;
} __attribute__((packed));

struct nbd_reply {
        __be32 magic;
        __be32 error;           /* 0 = ok, else error   */
        char handle[8];         /* handle you got from request  */
};

#endif

