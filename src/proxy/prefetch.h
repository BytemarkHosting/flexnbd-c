#ifndef PREFETCH_H
#define PREFETCH_H

#include <stdint.h>
#include <stddef.h>

#define PREFETCH_BUFSIZE 4096

struct prefetch {
    /* True if there is data in the buffer. */
    int is_full;
    /* The start point of the current content of buffer */
    uint64_t from;
    /* The length of the current content of buffer */
    uint32_t len;

    /* The total size of the buffer, in bytes. */
    size_t size;

    char *buffer;
};

struct prefetch *prefetch_create(size_t size_bytes);
void prefetch_destroy(struct prefetch *prefetch);
size_t prefetch_size(struct prefetch *);
void prefetch_set_is_empty(struct prefetch *prefetch);
void prefetch_set_is_full(struct prefetch *prefetch);
void prefetch_set_full(struct prefetch *prefetch, int val);
int prefetch_is_full(struct prefetch *prefetch);
int prefetch_contains(struct prefetch *prefetch, uint64_t from,
		      uint32_t len);
char *prefetch_offset(struct prefetch *prefetch, uint64_t from);

#endif
