#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "util.h"
#include "bitset.h"
#include "ioutil.h"


int build_allocation_map(struct bitset * allocation_map, int fd)
{
	/* break blocking ioctls down */
	const unsigned long max_length  = 100*1024*1024;
	const unsigned int  max_extents = 1000;

	unsigned long offset = 0;

	struct {
		struct fiemap fiemap;
		struct fiemap_extent extents[max_extents];
	} fiemap_static;
	struct fiemap* fiemap = (struct fiemap*) &fiemap_static;

	memset(&fiemap_static, 0, sizeof(fiemap_static));

	for (offset = 0; offset < allocation_map->size; ) {

		unsigned int i;

		fiemap->fm_start = offset;

		fiemap->fm_length = max_length;
		if ( offset + max_length > allocation_map->size ) {
			fiemap->fm_length = allocation_map->size-offset;
		}

		fiemap->fm_flags = FIEMAP_FLAG_SYNC;
		fiemap->fm_extent_count = max_extents;
		fiemap->fm_mapped_extents = 0;

		if ( ioctl( fd, FS_IOC_FIEMAP, fiemap ) < 0 ) {
			debug( "Couldn't get fiemap, returning no allocation_map" );
			return 0; /* it's up to the caller to free the map */
		}
		else {
			for ( i = 0; i < fiemap->fm_mapped_extents; i++ ) {
				bitset_set_range( allocation_map,
						  fiemap->fm_extents[i].fe_logical,
						  fiemap->fm_extents[i].fe_length );
			}


			/* must move the offset on, but careful not to jump max_length
			 * if we've actually hit max_offsets.
			 */
			if (fiemap->fm_mapped_extents > 0) {
				struct fiemap_extent *last = &fiemap->fm_extents[
					fiemap->fm_mapped_extents-1
				];
				offset = last->fe_logical + last->fe_length;
			}
			else {
				offset += fiemap->fm_length;
			}
		}
	}

	info("Successfully built allocation map");
	return 1;
}


int open_and_mmap(const char* filename, int* out_fd, off64_t *out_size, void **out_map)
{
	off64_t size;

	/* O_DIRECT seems to be intermittently supported.  Leaving it as
	 * a compile-time option for now. */
#ifdef DIRECT_IO
	*out_fd = open(filename, O_RDWR | O_DIRECT | O_SYNC );
#else
	*out_fd = open(filename, O_RDWR | O_SYNC );
#endif

	if (*out_fd < 1) {
		warn("open(%s) failed: does it exist?", filename);
		return *out_fd;
	}

	size = lseek64(*out_fd, 0, SEEK_END);
	if (size < 0) {
		warn("lseek64() failed");
		return size;
	}
	if (out_size) {
		*out_size = size;
	}

	if (out_map) {
		*out_map = mmap64(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
		  *out_fd, 0);
		if (((long) *out_map) == -1) {
			warn("mmap64() failed");
			return -1;
		}
	}
	debug("opened %s size %ld on fd %d @ %p", filename, size, *out_fd, *out_map);

	return 0;
}


int writeloop(int filedes, const void *buffer, size_t size)
{
	size_t written=0;
	while (written < size) {
		ssize_t result = write(filedes, buffer+written, size-written);
		if (result == -1) {
			if ( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ) {
				continue; // busy-wait
			}
			return -1; // failure
		}
		written += result;
	}
	return 0;
}

int readloop(int filedes, void *buffer, size_t size)
{
	size_t readden=0;
	while (readden < size) {
		ssize_t result = read(filedes, buffer+readden, size-readden);

		if ( result == 0 /* EOF */ ) {
			warn( "end-of-file detected while reading" );
			return -1;
		}

		if ( result == -1 ) {
			if ( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ) {
				continue; // busy-wait
			}
			return -1; // failure
		}
		readden += result;
	}
	return 0;
}

int sendfileloop(int out_fd, int in_fd, off64_t *offset, size_t count)
{
	size_t sent=0;
	while (sent < count) {
		ssize_t result = sendfile64(out_fd, in_fd, offset, count-sent);
		debug("sendfile64(out_fd=%d, in_fd=%d, offset=%p, count-sent=%ld) = %ld", out_fd, in_fd, offset, count-sent, result);

		if (result == -1) {
			debug( "%s (%i) calling sendfile64()", strerror(errno), errno );
			return -1;
		}
		sent += result;
		debug("sent=%ld, count=%ld", sent, count);
	}
	debug("exiting sendfileloop");
	return 0;
}

#include <errno.h>
ssize_t spliceloop(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len, unsigned int flags2)
{
	const unsigned int flags = SPLICE_F_MORE|SPLICE_F_MOVE|flags2;
	size_t spliced=0;

	//debug("spliceloop(%d, %ld, %d, %ld, %ld)", fd_in, off_in ? *off_in : 0, fd_out, off_out ? *off_out : 0, len);

	while (spliced < len) {
		ssize_t result = splice(fd_in, off_in, fd_out, off_out, len, flags);
		if (result < 0) {
			//debug("result=%ld (%s), spliced=%ld, len=%ld", result, strerror(errno), spliced, len);
			if (errno == EAGAIN && (flags & SPLICE_F_NONBLOCK) ) {
				return spliced;
			}
			else {
				return -1;
			}
		} else {
			spliced += result;
			//debug("result=%ld (%s), spliced=%ld, len=%ld", result, strerror(errno), spliced, len);
		}
	}

	return spliced;
}

int splice_via_pipe_loop(int fd_in, int fd_out, size_t len)
{

	int pipefd[2]; /* read end, write end */
	size_t spliced=0;

	if (pipe(pipefd) == -1) {
		return -1;
	}

	while (spliced < len) {
		ssize_t run = len-spliced;
		ssize_t s2, s1 = spliceloop(fd_in, NULL, pipefd[1], NULL, run, SPLICE_F_NONBLOCK);
		/*if (run > 65535)
			run = 65535;*/
		if (s1 < 0) { break; }

		s2 = spliceloop(pipefd[0], NULL, fd_out, NULL, s1, 0);
		if (s2 < 0) { break; }
		spliced += s2;
	}
	close(pipefd[0]);
	close(pipefd[1]);

	return spliced < len ? -1 : 0;
}

/* Reads single bytes from fd until either an EOF or a newline appears.
 * If an EOF occurs before a newline, returns -1.  The line is lost.
 * Inserts the read bytes (without the newline) into buf, followed by a
 * trailing NULL.
 * Returns the number of read bytes: the length of the line without the
 * newline, plus the trailing null.
 */
int read_until_newline(int fd, char* buf, int bufsize)
{
	int cur;

	for (cur=0; cur < bufsize; cur++) {
		int result = read(fd, buf+cur, 1);
		if (result <= 0) { return -1; }
		if (buf[cur] == 10) {
			buf[cur] = '\0';
			break;
		}
	}

	return cur+1;
}

int read_lines_until_blankline(int fd, int max_line_length, char ***lines)
{
	int lines_count = 0;
	char line[max_line_length+1];
	*lines = NULL;

	memset(line, 0, max_line_length+1);

	while (1) {
		int readden = read_until_newline(fd, line, max_line_length);
		/* readden will be:
		 * 1 for an empty line
		 * -1 for an eof
		 * -1 for a read error
		 */
		if (readden <= 1) { return lines_count; }
		*lines = xrealloc(*lines, (lines_count+1) * sizeof(char*));
		(*lines)[lines_count] = strdup(line);
		if ((*lines)[lines_count][0] == 0) {
			return lines_count;
		}
		lines_count++;
	}
}


int fd_is_closed( int fd_in )
{
	int errno_old = errno;
	int result = fcntl( fd_in, F_GETFL ) < 0;
	errno = errno_old;
	return result;
}


static inline int io_errno_permanent(void)
{
	return ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR );
}


/* Returns -1 if the operation failed, or the number of bytes read if all is
 * well. Note that 0 bytes may be returned. Unlike read(), this is not an EOF! */
ssize_t iobuf_read(int fd, struct iobuf *iobuf, size_t default_size )
{
	size_t left;
	ssize_t count;

	if ( iobuf->needle == 0 ) {
		iobuf->size = default_size;
	}

	left = iobuf->size - iobuf->needle;
	debug( "Reading %"PRIu32" of %"PRIu32" bytes from fd %i", left, iobuf->size, fd );

	count = read( fd, iobuf->buf + iobuf->needle, left );

	if ( count > 0 ) {
		iobuf->needle += count;
		debug( "read() returned %"PRIu32" bytes", count );
	} else if ( count == 0 ) {
		warn( "read() returned EOF on fd %i", fd );
		errno = 0;
		return -1;
	} else if ( count == -1 ) {
		if ( io_errno_permanent() ) {
			warn( SHOW_ERRNO( "read() failed on fd %i", fd ) );
		} else {
			debug( SHOW_ERRNO( "read() returned 0 bytes" ) );
			count = 0;
		}
	}

	return count;
}

ssize_t iobuf_write( int fd, struct iobuf *iobuf )
{
	size_t left = iobuf->size - iobuf->needle;
	ssize_t count;

	debug( "Writing %"PRIu32" of %"PRIu32" bytes to fd %i", left, iobuf->size, fd );
	count = write( fd, iobuf->buf + iobuf->needle, left );

	if ( count >= 0 ) {
		iobuf->needle += count;
		debug( "write() returned %"PRIu32" bytes", count );
	} else {
		if ( io_errno_permanent() ) {
			warn( SHOW_ERRNO( "write() failed on fd %i", fd ) );
		} else {
			debug( SHOW_ERRNO( "write() returned 0 bytes" ) );
			count = 0;
		}
	}

	return count;
}
