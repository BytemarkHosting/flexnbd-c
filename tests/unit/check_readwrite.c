#include "readwrite.h"

#include <check.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>

#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "util.h"
#include "nbdtypes.h"




int fd_read_request( int, struct nbd_request_raw *);
int fd_write_reply( int, char *, int );

int marker;

void error_marker(void * unused __attribute__((unused)), 
		int fatal __attribute__((unused)))
{
	marker = 1;
	return;
}

struct respond {
	int sock_fds[2]; // server end
	int do_fail;
	pthread_t thread_id;
	pthread_attr_t thread_attr;
	struct nbd_request received;
};

void * responder( void *respond_uncast )
{
	struct respond * resp = (struct respond *) respond_uncast;
	int sock_fd = resp->sock_fds[1];
	struct nbd_request_raw request_raw;
	char wrong_handle[] = "WHOOPSIE";


	if( fd_read_request( sock_fd, &request_raw ) == -1){
		fprintf(stderr, "Problem with fd_read_request\n");
	} else {
		nbd_r2h_request( &request_raw, &resp->received);
		if (resp->do_fail){
			fd_write_reply( sock_fd, wrong_handle, 0 );
		}
		else {
			fd_write_reply( sock_fd, (char*)resp->received.handle.b, 0 );
		}
		write( sock_fd, "12345678", 8 );
	}
	return NULL;
}


struct respond * respond_create( int do_fail )
{
	struct respond * respond = (struct respond *)calloc( 1, sizeof( struct respond ) );
	socketpair( PF_UNIX, SOCK_STREAM, 0, respond->sock_fds );
	respond->do_fail = do_fail;

	pthread_attr_init( &respond->thread_attr );
	pthread_create( &respond->thread_id, &respond->thread_attr, responder, respond );

	return respond;
}

void respond_destroy( struct respond * respond ){
	NULLCHECK( respond );

	pthread_join( respond->thread_id, NULL );
	pthread_attr_destroy( &respond->thread_attr );

	close( respond->sock_fds[0] );
	close( respond->sock_fds[1] );
	free( respond );
}


void * reader( void * nothing __attribute__((unused)))
{
	DECLARE_ERROR_CONTEXT( error_context );
	error_set_handler( (cleanup_handler *)error_marker, error_context );

	struct respond * respond = respond_create( 1 );
	int devnull = open("/dev/null", O_WRONLY);
	char outbuf[8] = {0};

	socket_nbd_read( respond->sock_fds[0], 0, 8, devnull, outbuf, 1 );

	return NULL;
}

START_TEST( test_rejects_mismatched_handle )
{

	error_init();
	pthread_t reader_thread;

	log_level=5;
	
	marker = 0;
	pthread_create( &reader_thread, NULL, reader, NULL );
	FATAL_UNLESS( 0 == pthread_join( reader_thread, NULL ),
			"pthread_join failed");
	
	log_level=2;

	fail_unless( marker == 1, "Error handler wasn't called" );
}
END_TEST


START_TEST( test_accepts_matched_handle )
{
	struct respond * respond = respond_create( 0 );

	int devnull = open("/dev/null", O_WRONLY);
	char outbuf[8] = {0};

	socket_nbd_read( respond->sock_fds[0], 0, 8, devnull, outbuf, 1 );

	respond_destroy( respond );
}
END_TEST


START_TEST( test_disconnect_doesnt_read_reply )
{
	struct respond * respond = respond_create( 1 );

	socket_nbd_disconnect( respond->sock_fds[0] );

	respond_destroy( respond );
}
END_TEST


Suite* readwrite_suite(void)
{
	Suite *s = suite_create("acl");
	TCase *tc_transfer = tcase_create("entrust");
	TCase *tc_disconnect = tcase_create("disconnect");


	tcase_add_test(tc_transfer, test_rejects_mismatched_handle);
	tcase_add_exit_test(tc_transfer, test_accepts_matched_handle, 0);

	/* This test is a little funny.  We respond with a dodgy handle
	 * and check that this *doesn't* cause a message rejection,
	 * because we want to know that the sender won't even try to
	 * read the response.
	 */
	tcase_add_exit_test( tc_disconnect, test_disconnect_doesnt_read_reply,0 );

	suite_add_tcase(s, tc_transfer);
	suite_add_tcase(s, tc_disconnect);

	return s;
}



#ifdef DEBUG
# define LOG_LEVEL 0
#else
# define LOG_LEVEL 2
#endif

int main(void)
{
	log_level = LOG_LEVEL;
	int number_failed;
	Suite *s = readwrite_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	log_level = 0;
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

