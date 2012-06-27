#include "flexnbd.h"

#include <check.h>


START_TEST( test_listening_assigns_sock )
{
	struct flexnbd * flexnbd = flexnbd_create_listening(
			"127.0.0.1",
			NULL,
			"4777",
			NULL,
			"fakefile",
			"fakesock",
			0,
			0,
			NULL,
			1 );
	fail_if( NULL == flexnbd->control->socket_name, "No socket was copied" );
}
END_TEST


Suite *flexnbd_suite(void)
{
	Suite *s = suite_create("flexnbd");

	TCase *tc_create = tcase_create("create");

	tcase_add_test(tc_create, test_listening_assigns_sock);
	suite_add_tcase( s, tc_create );

	return s;
}

int main(void)
{
	int number_failed;
	
	Suite *s = flexnbd_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

