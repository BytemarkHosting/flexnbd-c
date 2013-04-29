#ifndef PREFETCH_H
#define PREFETCH_H

#define PREFETCH_BUFSIZE 4096

struct prefetch {
        int is_full;
        __be64 from;
        __be32 len;

        char buffer[PREFETCH_BUFSIZE];
};

#endif
