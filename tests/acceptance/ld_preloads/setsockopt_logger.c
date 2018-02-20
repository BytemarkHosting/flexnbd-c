#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

/*
 * Noddy LD_PRELOAD wrapper to catch setsockopt calls, and log them to a file.
 */

typedef int (*real_setsockopt_t)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

int real_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
  return ((real_setsockopt_t)dlsym(RTLD_NEXT, "setsockopt"))(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
  FILE *fd;
  char *fn;
  int retval;
  
  retval = real_setsockopt(sockfd, level, optname, optval, optlen);
  fn = getenv("OUTPUT_setsockopt_logger");

  /* 
   * Only interested in catching non-null 4-byte (integer) values 
   */
  if ( fn != NULL && optval != NULL && optlen == 4) {
    fd = fopen(fn,"a");
    fprintf(fd,"setsockopt:%i:%i:%i:%i:%i\n", sockfd, level, optname, *(int *)optval, retval);
    fclose(fd);
  }

  return retval;
}
