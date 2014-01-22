#include "util.h"
#include "self_pipe.h"

#include <check.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>



struct cleanup_bucket {
	struct self_pipe *called_signal;
};

struct cleanup_bucket bkt;

void bucket_init(void){
	if ( bkt.called_signal ) {
		self_pipe_destroy( bkt.called_signal );
	}
	bkt.called_signal = self_pipe_create();
}

void setup(void)
{
	bkt.called_signal = NULL;
}

int handler_called(void)
{
	return self_pipe_signal_clear( bkt.called_signal );
}

void dummy_cleanup( struct cleanup_bucket * foo, int fatal __attribute__((unused)) )
{
	if (NULL != foo){
		self_pipe_signal( foo->called_signal );
	}
}


void trigger_fatal(void)
{ 
	error_init();
	error_set_handler( (cleanup_handler*) dummy_cleanup, &bkt );

	log_level = 5;

	fatal("Expected fatal error");
}

void trigger_error( void )
{
	error_init();
	error_set_handler( (cleanup_handler *) dummy_cleanup, &bkt);
	log_level = 4;
	error("Expected error");
}


START_TEST( test_fatal_kills_process ) 
{
	pid_t pid;

	pid = fork();

	if ( pid == 0 ) {
		trigger_fatal();
		/* If we get here, just block so the test timeout fails
		 * us */
		sleep(10);
	} 
	else {
		int kidret, kidstatus, result;
		result = waitpid( pid, &kidret, 0 );
		fail_if(  result < 0, "Wait failed." );
		kidstatus = WEXITSTATUS( kidret );
		ck_assert_int_eq( kidstatus, SIGABRT );
//		fail_unless( kidstatus == 6, "Kid was not aborted." );
	}

}
END_TEST


void * error_thread( void *nothing __attribute__((unused)) )
{
	trigger_error();
	return NULL;
}


START_TEST( test_error_doesnt_kill_process )
{
	bucket_init();
	pthread_attr_t attr;
	pthread_t tid;

	pthread_attr_init( &attr );

	pthread_create( &tid, &attr, error_thread, NULL );
	pthread_join( tid, NULL );
}
END_TEST


START_TEST( test_error_calls_handler )
{
	bucket_init();

	pthread_attr_t attr;
	pthread_t tid;

	pthread_attr_init( &attr );

	pthread_create( &tid, &attr, error_thread, NULL );
	pthread_join( tid, NULL );
	fail_unless( handler_called(), "Handler wasn't called." );
}
END_TEST


START_TEST( test_fatal_doesnt_call_handler )
{
	bucket_init();

	pid_t kidpid;

	kidpid = fork();
	if ( kidpid == 0 ) {
		trigger_fatal();
	}
	else {
		int kidstatus;
		int result = waitpid( kidpid, &kidstatus, 0 );
		fail_if( result < 0, "Wait failed" );
		fail_if( handler_called(), "Handler was called.");
	}

}
END_TEST


Suite* error_suite(void)
{
	Suite *s = suite_create("error");
	TCase *tc_process = tcase_create("process");
	TCase *tc_handler = tcase_create("handler");

	tcase_add_checked_fixture( tc_process, setup, NULL );

	tcase_add_test(tc_process, test_fatal_kills_process);
	tcase_add_test(tc_process, test_error_doesnt_kill_process);
	tcase_add_test(tc_handler, test_error_calls_handler );
	tcase_add_test(tc_handler, test_fatal_doesnt_call_handler);

	suite_add_tcase(s, tc_process);
	suite_add_tcase(s, tc_handler);
	
	return s;
}

int main(void)
{
	int number_failed;
	Suite *s = error_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

