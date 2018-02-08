#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

typedef int (*real_msync_t)(void *addr, size_t length, int flags);

int real_msync(void *addr, size_t length, int flags) {
  return ((real_msync_t)dlsym(RTLD_NEXT, "msync"))(addr, length, flags);
}

/*
 * Noddy LD_PRELOAD wrapper to catch msync calls, and log them to a file.
 */

int msync(void *addr, size_t length, int flags) {
  FILE *fd;
  char *fn;
  int retval;
  
  retval = real_msync(addr, length, flags);
  
  fn = getenv("MSYNC_CATCHER_OUTPUT");
  if ( fn != NULL ) {
    fd = fopen(fn,"a");
    fprintf(fd,"msync:%d:%i:%i:%i\n", addr, length, flags, retval);
    fclose(fd);
  }

  return retval;
}
