#include <check.h>
#include <stdio.h>

#include "acl.h"

START_TEST( test_null_acl )
{
	struct acl *acl = acl_create( 0,NULL, 0 );

	fail_if( NULL == acl, "No acl alloced." );
	fail_unless( 0 == acl->len, "Incorrect length" );
}
END_TEST


START_TEST( test_parses_single_line )
{
	char *lines[] = {"127.0.0.1"};
	struct acl * acl = acl_create( 1, lines, 0 );

	fail_unless( 1 == acl->len, "Incorrect length." );
	fail_if( NULL == acl->entries, "No entries present." );
}
END_TEST


START_TEST( test_destroy_doesnt_crash )
{
	char *lines[] = {"127.0.0.1"};
	struct acl * acl = acl_create( 1, lines, 0 );

	acl_destroy( acl );
}
END_TEST


START_TEST( test_includes_single_address )
{
	char *lines[] = {"127.0.0.1"};
	struct acl * acl = acl_create( 1, lines, 0 );
	union mysockaddr x;
	
	parse_ip_to_sockaddr( &x.generic, "127.0.0.1" );

	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );
}
END_TEST


START_TEST( test_doesnt_include_other_address )
{
	char *lines[] = {"127.0.0.1"};
	struct acl * acl  = acl_create( 1, lines, 0 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "127.0.0.2" );
	fail_if( acl_includes( acl, &x ), "Excluded address was covered." );
}
END_TEST


START_TEST( test_default_deny_rejects )
{
	struct acl * acl = acl_create( 0, NULL, 1 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "127.0.0.1" );

	fail_if( acl_includes( acl, &x ), "Default deny accepted." );
}
END_TEST


START_TEST( test_default_accept_rejects )
{
	struct acl * acl = acl_create( 0, NULL, 0 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "127.0.0.1" );

	fail_unless( acl_includes( acl, &x ), "Default accept rejected." );
}
END_TEST


Suite* acl_suite()
{
	Suite *s = suite_create("acl");
	TCase *tc_create = tcase_create("create");
	TCase *tc_includes = tcase_create("includes");
	TCase *tc_destroy = tcase_create("destroy");
	
	tcase_add_test(tc_create, test_null_acl);
	tcase_add_test(tc_create, test_parses_single_line);

	tcase_add_test(tc_includes, test_includes_single_address);
	tcase_add_test(tc_includes, test_doesnt_include_other_address);
	tcase_add_test(tc_includes, test_default_deny_rejects);
	tcase_add_test(tc_includes, test_default_accept_rejects);

	tcase_add_test(tc_destroy, test_destroy_doesnt_crash);

	suite_add_tcase(s, tc_create);
	suite_add_tcase(s, tc_includes);
	suite_add_tcase(s, tc_destroy);

	
	return s;
}

int main(void)
{
	set_debug(1);
	int number_failed;
	Suite *s = acl_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

