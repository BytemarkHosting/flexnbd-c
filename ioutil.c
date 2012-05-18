#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

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

char* build_allocation_map(int fd, off64_t size, int resolution)
{
	int i;
	char *allocation_map = xmalloc((size+resolution)/resolution);
	struct fiemap *fiemap;

	fiemap = (struct fiemap*) xmalloc(sizeof(struct fiemap));

	fiemap->fm_start = 0;
	fiemap->fm_length = size;
	fiemap->fm_flags = 0;
	fiemap->fm_extent_count = 0;
	fiemap->fm_mapped_extents = 0;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0)
		return NULL;

	/* Resize fiemap to allow us to read in the extents */
	fiemap = (struct fiemap*)xrealloc(
		fiemap,
		sizeof(struct fiemap) + (
			sizeof(struct fiemap_extent) * 
        		fiemap->fm_mapped_extents
        	)
	); 

	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	fiemap->fm_mapped_extents = 0;

	if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0)
		return NULL;
	
	for (i=0;i<fiemap->fm_mapped_extents;i++) {
		int first_bit = fiemap->fm_extents[i].fe_logical / resolution;
		int last_bit  = (fiemap->fm_extents[i].fe_logical + 
		  fiemap->fm_extents[i].fe_length + resolution - 1) / 
		  resolution;
		int run = last_bit - first_bit;
		  
		bit_set_range(allocation_map, first_bit, run);
	}
	
	for (i=0; i<16; i++) {
		debug("map[%d] = %d%d%d%d%d%d%d%d",
		  i,
		  (allocation_map[i] & 1) == 1,
		  (allocation_map[i] & 2) == 2,
		  (allocation_map[i] & 4) == 4,
		  (allocation_map[i] & 8) == 8,
		  (allocation_map[i] & 16) == 16,
		  (allocation_map[i] & 32) == 32,
		  (allocation_map[i] & 64) == 64,
		  (allocation_map[i] & 128) == 128
		);
	}
	
	free(fiemap);
	
	return allocation_map;
}


int writeloop(int filedes, const void *buffer, size_t size)
{
	size_t written=0;
	while (written < size) {
		size_t result = write(filedes, buffer+written, size-written);
		if (result == -1)
			return -1;
		written += result;
	}
	return 0;
}

int readloop(int filedes, void *buffer, size_t size)
{
	size_t readden=0;
	while (readden < size) {
		size_t result = read(filedes, buffer+readden, size-readden);
		if (result == 0 /* EOF */ || result == -1 /* error */)
			return -1;
		readden += result;
	}
	return 0;
}

int sendfileloop(int out_fd, int in_fd, off64_t *offset, size_t count)
{
	size_t sent=0;
	while (sent < count) {
		size_t result = sendfile64(out_fd, in_fd, offset+sent, count-sent);
		if (result == -1)
			return -1;
		sent += result;
	}
	return 0;
}

int splice_via_pipe_loop(int fd_in, int fd_out, size_t len)
{
	int pipefd[2];
	size_t spliced=0;
	
	if (pipe(pipefd) == -1)
		return -1;
	
	while (spliced < len) {
		size_t r1,r2;
		r1 = splice(fd_in, NULL, pipefd[1], NULL, len-spliced, 0);
		if (r1 <= 0)
			break;
		r2 = splice(pipefd[0], NULL, fd_out, NULL, r1, 0);
		if (r1 != r2)
			break;
		spliced += r1;
	}
	close(pipefd[0]);
	close(pipefd[1]);
	
	return spliced < len ? -1 : 0;
}


