#include "mbox.h"
#include "util.h"

#include <pthread.h>
#include <check.h>

START_TEST( test_allocs_cvar )
{
	struct mbox * mbox = mbox_create();
	fail_if( NULL == mbox, "Nothing allocated" );

	pthread_cond_t cond_zero;
	/* A freshly inited pthread_cond_t is set to {0} */
	memset( &cond_zero, 'X', sizeof( cond_zero ) );
	fail_if( memcmp( &cond_zero, &mbox->filled_cond, sizeof( cond_zero ) ) == 0 ,
			"Condition variable not allocated" );
	fail_if( memcmp( &cond_zero, &mbox->emptied_cond, sizeof( cond_zero ) ) == 0 ,
			"Condition variable not allocated" );
}
END_TEST


START_TEST( test_post_stores_value )
{
	struct mbox * mbox = mbox_create();
	
	void * deadbeef = (void *)0xDEADBEEF;
	mbox_post( mbox, deadbeef );

	fail_unless( deadbeef == mbox_contents( mbox ), 
			"Contents were not posted" );
}
END_TEST


void * mbox_receive_runner( void * mbox_uncast )
{
	struct mbox * mbox = (struct mbox *)mbox_uncast;
	void * contents = NULL;

	contents = mbox_receive( mbox );
	return contents;
}


START_TEST( test_receive_blocks_until_post )
{
	struct mbox * mbox = mbox_create();
	pthread_t receiver;
	pthread_create( &receiver, NULL, mbox_receive_runner, mbox );
	
	void * deadbeef = (void *)0xDEADBEEF;
	void * retval =NULL;
	usleep(10000);
	fail_unless( EBUSY == pthread_tryjoin_np( receiver, &retval ),
			"Receiver thread wasn't blocked");

	mbox_post( mbox, deadbeef );
	fail_unless( 0 == pthread_join( receiver, &retval ),
			"Failed to join the receiver thread" );
	fail_unless( retval == deadbeef,
			"Return value was wrong" );


}
END_TEST


Suite* acl_suite(void)
{
	Suite *s = suite_create("acl");
	TCase *tc_create = tcase_create("create");
	TCase *tc_post = tcase_create("post");

	tcase_add_test(tc_create, test_allocs_cvar);

	tcase_add_test( tc_post, test_post_stores_value );
	tcase_add_test( tc_post, test_receive_blocks_until_post);

	suite_add_tcase(s, tc_create);
	suite_add_tcase(s, tc_post);

	return s;
}



int main(void)
{
#ifdef DEBUG
	log_level = 0;
#else
	log_level = 2;
#endif
	int number_failed;
	Suite *s = acl_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	log_level = 0;
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

