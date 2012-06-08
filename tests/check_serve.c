#include "serve.h"
#include "self_pipe.h"

#include <check.h>
#include <stdio.h>


START_TEST( test_replaces_acl )
{
	struct server s;
	s.acl_updated_signal = self_pipe_create();
	struct acl * acl = acl_create( 0, NULL, 0 );

	server_replace_acl( &s, acl );

	fail_unless( s.acl == acl, "ACL wasn't replaced." );
	self_pipe_destroy( s.acl_updated_signal );
}
END_TEST


START_TEST( test_signals_acl_updated )
{
	struct server s;
	struct acl * new_acl = acl_create( 0, NULL, 0 );

	s.acl_updated_signal = self_pipe_create();
	s.acl = acl_create( 0, NULL, 0);


	server_replace_acl( &s, new_acl );

	fail_unless( 1 == self_pipe_signal_clear( s.acl_updated_signal ), 
			"No signal sent." );
	self_pipe_destroy( s.acl_updated_signal );
}
END_TEST


Suite* serve_suite()
{
	Suite *s = suite_create("serve");
	TCase *tc_acl_update = tcase_create("acl_update");
	
	tcase_add_test(tc_acl_update, test_replaces_acl);
	tcase_add_test(tc_acl_update, test_signals_acl_updated);

	suite_add_tcase(s, tc_acl_update);

	return s;
}


int main(void)
{
	set_debug(1);
	int number_failed;
	Suite *s = serve_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

