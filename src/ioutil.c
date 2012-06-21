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

struct bitset_mapping* build_allocation_map(int fd, uint64_t size, int resolution)
{
	unsigned int i;
	struct bitset_mapping* allocation_map = bitset_alloc(size, resolution);
	struct fiemap *fiemap_count, *fiemap;

	fiemap_count = (struct fiemap*) xmalloc(sizeof(struct fiemap));

	fiemap_count->fm_start = 0;
	fiemap_count->fm_length = size;
	fiemap_count->fm_flags = 0;
	fiemap_count->fm_extent_count = 0;
	fiemap_count->fm_mapped_extents = 0;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, fiemap_count) < 0) {
		return NULL;
	}

	/* Resize fiemap to allow us to read in the extents */
	fiemap = (struct fiemap*)xmalloc(
		sizeof(struct fiemap) + (
			sizeof(struct fiemap_extent) * 
        		fiemap_count->fm_mapped_extents
        	)
	); 
	
	/* realloc makes valgrind complain a lot */
	memcpy(fiemap, fiemap_count, sizeof(struct fiemap));

	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	fiemap->fm_mapped_extents = 0;

	if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) { return NULL; }

	for (i=0;i<fiemap->fm_mapped_extents;i++) {
		bitset_set_range(
			allocation_map, 
			fiemap->fm_extents[i].fe_logical, 
			fiemap->fm_extents[i].fe_length
		);
	}
	
	for (i=0; i<(size/resolution); i++) {
		debug("map[%d] = %d%d%d%d%d%d%d%d",
		  i,
		  (allocation_map->bits[i] & 1) == 1,
		  (allocation_map->bits[i] & 2) == 2,
		  (allocation_map->bits[i] & 4) == 4,
		  (allocation_map->bits[i] & 8) == 8,
		  (allocation_map->bits[i] & 16) == 16,
		  (allocation_map->bits[i] & 32) == 32,
		  (allocation_map->bits[i] & 64) == 64,
		  (allocation_map->bits[i] & 128) == 128
		);
	}
	
	
	free(fiemap);
	
	return allocation_map;
}

int open_and_mmap(char* filename, int* out_fd, off64_t *out_size, void **out_map)
{
	off64_t size;
	
	*out_fd = open(filename, O_RDWR|O_DIRECT|O_SYNC);
	if (*out_fd < 1) {
		return *out_fd;
	}
	
	size = lseek64(*out_fd, 0, SEEK_END);
	if (size < 0) {
		return size;
	}
	if (out_size) {
		*out_size = size;
	}
	
	if (out_map) {
		*out_map = mmap64(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, 
		  *out_fd, 0);
		if (((long) *out_map) == -1) {
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
		if (result == -1) { return -1; }
		written += result;
	}
	return 0;
}

int readloop(int filedes, void *buffer, size_t size)
{
	size_t readden=0;
	while (readden < size) {
		ssize_t result = read(filedes, buffer+readden, size-readden);
		if (result == 0 /* EOF */ || result == -1 /* error */) {
			return -1;
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

		if (result == -1) { return -1; }
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

int read_until_newline(int fd, char* buf, int bufsize)
{
	int cur;
	bufsize -=1;
	
	for (cur=0; cur < bufsize; cur++) {
		int result = read(fd, buf+cur, 1);
		if (result < 0) { return -1; }
		if (buf[cur] == 10) { break; }
	}
	buf[cur++] = 0;
	
	return cur;
}

int read_lines_until_blankline(int fd, int max_line_length, char ***lines)
{
	int lines_count = 0;
	char line[max_line_length+1];
	*lines = NULL;
	
	memset(line, 0, max_line_length+1);
	
	while (1) {
		if (read_until_newline(fd, line, max_line_length) < 0) {
			return lines_count;
		}
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
	int result = fcntl( fd_in, F_GETFD, 9 ) < 0;
	errno = errno_old;
	return result;
}
