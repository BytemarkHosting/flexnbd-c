#include "client.h"
#include "serve.h"
#include "ioutil.h"
#include "sockutil.h"
#include "util.h"
#include "bitset.h"
#include "nbdtypes.h"
#include "self_pipe.h"

#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


// When this signal is invoked, we call shutdown() on the client fd, which
// results in the thread being wound up
void client_killswitch_hit(int signal
			   __attribute__ ((unused)), siginfo_t * info,
			   void *ptr __attribute__ ((unused)))
{
    int fd = info->si_value.sival_int;
    warn("Killswitch for fd %i activated, calling shutdown on socket", fd);

    FATAL_IF(-1 == shutdown(fd, SHUT_RDWR),
	     SHOW_ERRNO
	     ("Failed to shutdown() the socket, killing the server")
	);
}

struct client *client_create(struct server *serve, int socket)
{
    NULLCHECK(serve);

    struct client *c;
    struct sigevent evp = {
	.sigev_notify = SIGEV_SIGNAL,
	.sigev_signo = CLIENT_KILLSWITCH_SIGNAL
    };

    /*
     * Our killswitch closes this socket, forcing read() and write() calls
     * blocked on it to return with an error. The thread then close()s the
     * socket itself, avoiding races.
     */
    evp.sigev_value.sival_int = socket;

    c = xmalloc(sizeof(struct client));
    c->stopped = 0;
    c->socket = socket;
    c->serve = serve;

    c->stop_signal = self_pipe_create();

    FATAL_IF_NEGATIVE(timer_create
		      (CLOCK_MONOTONIC, &evp, &(c->killswitch)),
		      SHOW_ERRNO("Failed to create killswitch timer")
	);

    debug("Alloced client %p with socket %d", c, socket);
    return c;
}


void client_signal_stop(struct client *c)
{
    NULLCHECK(c);

    debug("client %p: signal stop (%d, %d)", c, c->stop_signal->read_fd,
	  c->stop_signal->write_fd);
    self_pipe_signal(c->stop_signal);
}

void client_destroy(struct client *client)
{
    NULLCHECK(client);

    FATAL_IF_NEGATIVE(timer_delete(client->killswitch),
		      SHOW_ERRNO("Couldn't delete killswitch")
	);

    debug("Destroying stop signal for client %p", client);
    self_pipe_destroy(client->stop_signal);
    debug("Freeing client %p", client);
    free(client);
}



/**
 * So waiting on client->socket is len bytes of data, and we must write it all
 * to client->mapped.  However while doing do we must consult the bitmap
 * client->serve->allocation_map, which is a bitmap where one bit represents
 * block_allocation_resolution bytes.  Where a bit isn't set, there are no
 * disc blocks allocated for that portion of the file, and we'd like to keep
 * it that way.
 *
 * If the bitmap shows that every block in our prospective write is already
 * allocated, we can proceed as normal and make one call to writeloop.
 *
 */
void write_not_zeroes(struct client *client, uint64_t from, uint64_t len)
{
    NULLCHECK(client);
    NULLCHECK(client->serve);
    NULLCHECK(client->serve->allocation_map);

    struct bitset *map = client->serve->allocation_map;

    while (len > 0) {
	/* so we have to calculate how much of our input to consider
	 * next based on the bitmap of allocated blocks.  This will be
	 * at a coarser resolution than the actual write, which may
	 * not fall on a block boundary at either end.  So we look up
	 * how many blocks our write covers, then cut off the start
	 * and end to get the exact number of bytes.
	 */

	uint64_t run = bitset_run_count(map, from, len);

	debug("write_not_zeroes: from=%ld, len=%d, run=%d", from, len,
	      run);

	if (run > len) {
	    run = len;
	    debug("(run adjusted to %d)", run);
	}

	/* 
	   // Useful but expensive
	   if (0) 
	   {
	   uint64_t i;
	   fprintf(stderr, "full map resolution=%d: ", map->resolution);
	   for (i=0; i<client->serve->size; i+=map->resolution) {
	   int here = (from >= i && from < i+map->resolution);

	   if (here) { fprintf(stderr, ">"); }
	   fprintf(stderr, bitset_is_set_at(map, i) ? "1" : "0");
	   if (here) { fprintf(stderr, "<"); }
	   }
	   fprintf(stderr, "\n");
	   }
	 */

#define DO_READ(dst, len) ERROR_IF_NEGATIVE( \
			readloop( \
				client->socket, \
				(dst), \
				(len) \
			), \
			"read failed %ld+%d", from, (len) \
		)

	if (bitset_is_set_at(map, from)) {
	    debug("writing the lot: from=%ld, run=%d", from, run);
	    /* already allocated, just write it all */
	    DO_READ(client->mapped + from, run);
	    /* We know from our earlier call to  bitset_run_count that the
	     * bitset is all-1s at this point, but we need to dirty it for the
	     * sake of the event stream - the actual bytes have changed, and we
	     * are interested in that fact.
	     */
	    bitset_set_range(map, from, run);
	    len -= run;
	    from += run;
	} else {
	    char zerobuffer[block_allocation_resolution];
	    /* not allocated, read in block_allocation_resoution */
	    while (run > 0) {
		uint64_t blockrun = block_allocation_resolution -
		    (from % block_allocation_resolution);
		if (blockrun > run)
		    blockrun = run;

		DO_READ(zerobuffer, blockrun);

		/* This reads the buffer twice in the worst case
		 * but we're leaning on memcmp failing early
		 * and memcpy being fast, rather than try to
		 * hand-optimized something specific.
		 */

		int all_zeros = (zerobuffer[0] == 0) &&
		    (0 ==
		     memcmp(zerobuffer, zerobuffer + 1, blockrun - 1));

		if (!all_zeros) {
		    memcpy(client->mapped + from, zerobuffer, blockrun);
		    bitset_set_range(map, from, blockrun);
		    /* at this point we could choose to
		     * short-cut the rest of the write for
		     * faster I/O but by continuing to do it
		     * the slow way we preserve as much
		     * sparseness as possible.
		     */
		}
		/* When the block is all_zeroes, no bytes have changed, so we
		 * don't need to put an event into the bitset stream. This may
		 * be surprising in the future.
		 */

		len -= blockrun;
		run -= blockrun;
		from += blockrun;
	    }
	}
    }
}


int fd_read_request(int fd, struct nbd_request_raw *out_request)
{
    return readloop(fd, out_request, sizeof(struct nbd_request_raw));
}


/* Returns 1 if *request was filled with a valid request which we should
 * try to honour. 0 otherwise. */
int client_read_request(struct client *client,
			struct nbd_request *out_request, int *disconnected)
{
    NULLCHECK(client);
    NULLCHECK(out_request);

    struct nbd_request_raw request_raw;

    if (fd_read_request(client->socket, &request_raw) == -1) {
	*disconnected = 1;
	switch (errno) {
	case 0:
	    warn("EOF while reading request");
	    return 0;
	case ECONNRESET:
	    warn("Connection reset while" " reading request");
	    return 0;
	case ETIMEDOUT:
	    warn("Connection timed out while" " reading request");
	    return 0;
	default:
	    /* FIXME: I've seen this happen, but I
	     * couldn't reproduce it so I'm leaving
	     * it here with a better debug output in
	     * the hope it'll spontaneously happen
	     * again.  It should *probably* be an
	     * error() call, but I want to be sure.
	     * */
	    fatal("Error reading request: %d, %s", errno, strerror(errno));
	}
    }

    nbd_r2h_request(&request_raw, out_request);
    return 1;
}

int fd_write_reply(int fd, uint64_t handle, int error)
{
    struct nbd_reply reply;
    struct nbd_reply_raw reply_raw;

    reply.magic = REPLY_MAGIC;
    reply.error = error;
    reply.handle.w = handle;

    nbd_h2r_reply(&reply, &reply_raw);
    debug("Replying with handle=0x%08X, error=%" PRIu32, handle, error);

    if (-1 == writeloop(fd, &reply_raw, sizeof(reply_raw))) {
	switch (errno) {
	case ECONNRESET:
	    error("Connection reset while writing reply");
	    break;
	case EBADF:
	    fatal("Tried to write to an invalid file descriptor");
	    break;
	case EPIPE:
	    error("Remote end closed");
	    break;
	default:
	    fatal("Unhandled error while writing: %d", errno);
	}
    }

    return 1;
}


/* Writes a reply to request *request, with error, to the client's
 * socket.
 * Returns 1; we don't check for errors on the write.
 * TODO: Check for errors on the write.
 */
int client_write_reply(struct client *client, struct nbd_request *request,
		       int error)
{
    return fd_write_reply(client->socket, request->handle.w, error);
}


void client_write_init(struct client *client, uint64_t size)
{
    struct nbd_init init = { {0} };
    struct nbd_init_raw init_raw = { {0} };

    memcpy(init.passwd, INIT_PASSWD, sizeof(init.passwd));
    init.magic = INIT_MAGIC;
    init.size = size;
    /* As more features are implemented, this is the place to advertise
     * them.
     */
    init.flags = FLAG_HAS_FLAGS | FLAG_SEND_FLUSH | FLAG_SEND_FUA;
    memset(init.reserved, 0, 124);

    nbd_h2r_init(&init, &init_raw);

    ERROR_IF_NEGATIVE(writeloop
		      (client->socket, &init_raw, sizeof(init_raw)),
		      "Couldn't send hello");
}


/* Remove len bytes from the client socket. This is needed when the
 * client sends a write we can't honour - we need to get rid of the
 * bytes they've already written before we can look for another request.
 */
void client_flush(struct client *client, size_t len)
{
    int devnull = open("/dev/null", O_WRONLY);
    FATAL_IF_NEGATIVE(devnull,
		      "Couldn't open /dev/null: %s", strerror(errno));
    int pipes[2];
    pipe(pipes);

    const unsigned int flags = SPLICE_F_MORE | SPLICE_F_MOVE;
    size_t spliced = 0;

    while (spliced < len) {
	ssize_t received = splice(client->socket, NULL,
				  pipes[1], NULL,
				  len - spliced, flags);
	FATAL_IF_NEGATIVE(received, "splice error: %s", strerror(errno));
	ssize_t junked = 0;
	while (junked < received) {
	    ssize_t junk;
	    junk = splice(pipes[0], NULL, devnull, NULL, received, flags);
	    FATAL_IF_NEGATIVE(junk, "splice error: %s", strerror(errno));
	    junked += junk;
	}
	spliced += received;
    }
    debug("Flushed %d bytes", len);


    close(devnull);
}


/* Check to see if the client's request needs a reply constructing.
 * Returns 1 if we do, 0 otherwise.
 * request_err is set to 0 if the client sent a bad request, in which
 * case we drop the connection.
 */
int client_request_needs_reply(struct client *client,
			       struct nbd_request request)
{
    /* The client is stupid, but don't take down the whole server as a result.
     * We send a reply before disconnecting so that at least some indication of
     * the problem is visible, and so proxies don't retry the same (bad) request
     * forever.
     */
    if (request.magic != REQUEST_MAGIC) {
	warn("Bad magic 0x%08X from client", request.magic);
	client_write_reply(client, &request, EBADMSG);
	client->disconnect = 1;	// no need to flush
	return 0;
    }

    debug("request type=%" PRIu16 ", flags=%" PRIu16 ", from=%" PRIu64
	  ", len=%" PRIu32 ", handle=0x%08X", request.type, request.flags,
	  request.from, request.len, request.handle);

    /* check it's not out of range. NBD protocol requires ENOSPC to be
     * returned in this instance 
     */
    if (request.from + request.len > client->serve->size) {
	warn("write request %" PRIu64 "+%" PRIu32 " out of range",
	     request.from, request.len);
	if (request.type == REQUEST_WRITE) {
	    client_flush(client, request.len);
	}
	client_write_reply(client, &request, ENOSPC);
	client->disconnect = 0;
	return 0;
    }


    switch (request.type) {
    case REQUEST_READ:
	break;
    case REQUEST_WRITE:
	break;
    case REQUEST_DISCONNECT:
	debug("request disconnect");
	client->disconnect = 1;
	return 0;
    case REQUEST_FLUSH:
	break;
    default:
	/* NBD prototcol says servers SHOULD return EINVAL to unknown
	 * commands */
	warn("Unknown request 0x%08X", request.type);
	client_write_reply(client, &request, EINVAL);
	client->disconnect = 0;
	return 0;
    }
    return 1;
}


void client_reply_to_read(struct client *client,
			  struct nbd_request request)
{
    off64_t offset;

    debug("request read %ld+%d", request.from, request.len);
    sock_set_tcp_cork(client->socket, 1);
    client_write_reply(client, &request, 0);

    offset = request.from;

    /* If we get cut off partway through this sendfile, we don't
     * want to kill the server.  This should be an error.
     */
    ERROR_IF_NEGATIVE(sendfileloop(client->socket,
				   client->fileno,
				   &offset,
				   request.len),
		      "sendfile failed from=%ld, len=%d",
		      offset, request.len);

    sock_set_tcp_cork(client->socket, 0);
}


void client_reply_to_write(struct client *client,
			   struct nbd_request request)
{
    debug("request write from=%" PRIu64 ", len=%" PRIu32 ", handle=0x%08X",
	  request.from, request.len, request.handle);
    if (client->serve->allocation_map_built) {
	write_not_zeroes(client, request.from, request.len);
    } else {
	debug("No allocation map, writing directly.");
	/* If we get cut off partway through reading this data:
	 * */
	ERROR_IF_NEGATIVE(readloop(client->socket,
				   client->mapped + request.from,
				   request.len),
			  "reading write data failed from=%ld, len=%d",
			  request.from, request.len);

	/* the allocation_map is shared between client threads, and may be
	 * being built. We need to reflect the write in it, as it may be in
	 * a position the builder has already gone over.
	 */
	bitset_set_range(client->serve->allocation_map, request.from,
			 request.len);
    }

    // Only flush if FUA is set
    if (request.flags & CMD_FLAG_FUA) {
	/* multiple of page size */
	uint64_t from_rounded =
	    request.from & (~(sysconf(_SC_PAGE_SIZE) - 1));
	uint64_t len_rounded = request.len + (request.from - from_rounded);
	debug("Calling msync from=%" PRIu64 ", len=%" PRIu64 "",
	      from_rounded, len_rounded);

	FATAL_IF_NEGATIVE(msync(client->mapped + from_rounded,
				len_rounded,
				MS_SYNC | MS_INVALIDATE),
			  "msync failed %ld %ld", request.from,
			  request.len);
    }
    client_write_reply(client, &request, 0);
}

void client_reply_to_flush(struct client *client,
			   struct nbd_request request)
{
    debug("request flush from=%" PRIu64 ", len=%" PRIu32 ", handle=0x%08X",
	  request.from, request.len, request.handle);

    ERROR_IF_NEGATIVE(msync
		      (client->mapped, client->mapped_size,
		       MS_SYNC | MS_INVALIDATE), "flush failed");

    client_write_reply(client, &request, 0);
}

void client_reply(struct client *client, struct nbd_request request)
{
    switch (request.type) {
    case REQUEST_READ:
	client_reply_to_read(client, request);
	break;
    case REQUEST_WRITE:
	client_reply_to_write(client, request);
	break;
    case REQUEST_FLUSH:
	client_reply_to_flush(client, request);
	break;
    }
}


/* Starts a timer that will kill the whole process if disarm is not called
 * within a timeout (see CLIENT_HANDLE_TIMEOUT).
 */
void client_arm_killswitch(struct client *client)
{
    struct itimerspec its = {
	.it_value = {.tv_nsec = 0,.tv_sec = CLIENT_HANDLER_TIMEOUT},
	.it_interval = {.tv_nsec = 0,.tv_sec = 0}
    };

    if (!client->serve->use_killswitch) {
	return;
    }

    debug("Arming killswitch");

    FATAL_IF_NEGATIVE(timer_settime(client->killswitch, 0, &its, NULL),
		      SHOW_ERRNO("Failed to arm killswitch")
	);

    return;
}

void client_disarm_killswitch(struct client *client)
{
    struct itimerspec its = {
	.it_value = {.tv_nsec = 0,.tv_sec = 0},
	.it_interval = {.tv_nsec = 0,.tv_sec = 0}
    };

    if (!client->serve->use_killswitch) {
	return;
    }

    debug("Disarming killswitch");

    FATAL_IF_NEGATIVE(timer_settime(client->killswitch, 0, &its, NULL),
		      SHOW_ERRNO("Failed to disarm killswitch")
	);

    return;
}

/* Returns 0 if we should continue trying to serve requests */
int client_serve_request(struct client *client)
{
    struct nbd_request request = { 0 };
    int stop = 1;
    int disconnected = 0;
    fd_set rfds, efds;
    int fd_count;

    /* wait until there are some bytes on the fd before committing to reads
     * FIXME: this whole scheme is broken because we're using blocking reads.
     * read() can block directly after a select anyway, and it's possible that,
     * without the killswitch, we'd hang forever. With the killswitch, we just
     * hang for "a while". The Right Thing to do is to rewrite client.c to be
     * non-blocking.
     */

    FD_ZERO(&rfds);
    FD_SET(client->socket, &rfds);
    self_pipe_fd_set(client->stop_signal, &rfds);

    FD_ZERO(&efds);
    FD_SET(client->socket, &efds);

    fd_count = sock_try_select(FD_SETSIZE, &rfds, NULL, &efds, NULL);

    if (fd_count == 0) {
	/* This "can't ever happen" */
	fatal("No FDs selected, and no timeout!");
    } else if (fd_count < 0) {
	fatal("Select failed");
    }

    if (self_pipe_fd_isset(client->stop_signal, &rfds)) {
	debug("Client received stop signal.");
	return 1;		// Don't try to serve more requests
    }

    if (FD_ISSET(client->socket, &efds)) {
	debug("Client connection closed");
	return 1;
    }


    /* We arm / disarm around the whole request cycle. The reason for this is
     * that the remote peer could uncleanly die at any point; if we're stuck on
     * a blocking read(), then that will hang for (almost) forever. This is bad
     * in general, makes the server respond only to kill -9, and breaks
     * outward mirroring in a most unpleasant way.
     *
     * Don't forget to disarm before exiting, no matter what!
     *
     * The replication is simple: open a connection to the flexnbd server, write
     * a single byte, and then wait.
     *
     */
    client_arm_killswitch(client);

    if (!client_read_request(client, &request, &disconnected)) {
	client_disarm_killswitch(client);
	return stop;
    }
    if (disconnected) {
	client_disarm_killswitch(client);
	return stop;
    }

    if (!client_request_needs_reply(client, request)) {
	client_disarm_killswitch(client);
	return client->disconnect;
    }

    {
	if (!server_is_closed(client->serve)) {
	    client_reply(client, request);
	    stop = 0;
	}
    }

    client_disarm_killswitch(client);
    return stop;
}


void client_send_hello(struct client *client)
{
    client_write_init(client, client->serve->size);
}

void client_cleanup(struct client *client,
		    int fatal __attribute__ ((unused)))
{
    info("client cleanup for client %p", client);

    /* If the thread hits an error, we need to ensure this is off */
    client_disarm_killswitch(client);

    if (client->socket) {
	FATAL_IF_NEGATIVE(close(client->socket),
			  "Error closing client socket %d",
			  client->socket);
	debug("Closed client socket fd %d", client->socket);
	client->socket = -1;
    }
    if (client->mapped) {
	munmap(client->mapped, client->serve->size);
    }
    if (client->fileno) {
	FATAL_IF_NEGATIVE(close(client->fileno),
			  "Error closing file %d", client->fileno);
	debug("Closed client file fd %d", client->fileno);
	client->fileno = -1;
    }

    if (server_acl_locked(client->serve)) {
	server_unlock_acl(client->serve);
    }

}

void *client_serve(void *client_uncast)
{
    struct client *client = (struct client *) client_uncast;

    error_set_handler((cleanup_handler *) client_cleanup, client);

    info("client: mmaping file");
    FATAL_IF_NEGATIVE(open_and_mmap(client->serve->filename,
				    &client->fileno,
				    &client->mapped_size,
				    (void **) &client->mapped),
		      "Couldn't open/mmap file %s: %s",
		      client->serve->filename, strerror(errno)
	);

    FATAL_IF_NEGATIVE(madvise
		      (client->mapped, client->serve->size, MADV_RANDOM),
		      SHOW_ERRNO("Failed to madvise() %s",
				 client->serve->filename)
	);

    debug("Opened client file fd %d", client->fileno);
    debug("client: sending hello");
    client_send_hello(client);

    debug("client: serving requests");
    while (client_serve_request(client) == 0);
    debug("client: stopped serving requests");
    client->stopped = 1;

    if (client->disconnect) {
	debug("client: control arrived");
	server_control_arrived(client->serve);
    }

    debug("Cleaning client %p up normally in thread %p", client,
	  pthread_self());
    client_cleanup(client, 0);
    debug("Client thread done");

    return NULL;
}
