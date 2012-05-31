#ifndef __IOUTIL_H
#define __IOUTIL_H


#include "params.h"

/** Returns a bit field representing which blocks are allocated in file 
  * descriptor ''fd''.  You must supply the size, and the resolution at which
  * you want the bits to represent allocated blocks.  If the OS represents
  * allocated blocks at a finer resolution than you've asked for, any block
  * or part block will count as "allocated" with the corresponding bit set.
  */
char* build_allocation_map(int fd, off64_t size, int resolution);

/** Repeat a write() operation that succeeds partially until ''size'' bytes
  * are written, or an error is returned, when it returns -1 as usual.
  */
int writeloop(int filedes, const void *buffer, size_t size);

/** Repeat a read() operation that succeeds partially until ''size'' bytes
  * are written, or an error is returned, when it returns -1 as usual.
  */
int readloop(int filedes, void *buffer, size_t size);

/** Repeat a sendfile() operation that succeeds partially until ''size'' bytes
  * are written, or an error is returned, when it returns -1 as usual.
  */
int sendfileloop(int out_fd, int in_fd, off64_t *offset, size_t count);

/** Copy ''len'' bytes from ''fd_in'' to ''fd_out'' by creating a temporary
  * pipe and using the Linux splice call repeatedly until it has transferred
  * all the data.  Returns -1 on error.
  */
int splice_via_pipe_loop(int fd_in, int fd_out, size_t len);

/** Fill up to ''bufsize'' characters starting at ''buf'' with data from ''fd''
  * until an LF character is received, which is written to the buffer at a zero
  * byte.  Returns -1 on error, or the number of bytes written to the buffer.
  */
int read_until_newline(int fd, char* buf, int bufsize);

/** Read a number of lines using read_until_newline, until an empty line is
  * received (i.e. the sequence LF LF).  The data is read from ''fd'' and 
  * lines must be a maximum of ''max_line_length''.  The set of lines is
  * returned as an array of zero-terminated strings; you must pass an address
  * ''lines'' in which you want the address of this array returned.
  */
int read_lines_until_blankline(int fd, int max_line_length, char ***lines);

/** Open the given ''filename'', determine its size, and mmap it in its 
  * entirety.  The file descriptor is stored in ''out_fd'', the size in 
  * ''out_size'' and the address of the mmap in ''out_map''.  If anything goes
  * wrong, returns -1 setting errno, otherwise 0.
  */
int open_and_mmap(char* filename, int* out_fd, off64_t *out_size, void **out_map);

#endif
