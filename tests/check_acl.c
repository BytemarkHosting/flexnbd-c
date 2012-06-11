#include <check.h>
#include <stdio.h>

#include "acl.h"
#include "util.h"

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

START_TEST( test_parses_multiple_lines )
{
	char *lines[] = {"127.0.0.1", "::1"};
	struct acl * acl = acl_create( 2, lines, 0 );
	union mysockaddr e0, e1;

	parse_ip_to_sockaddr( &e0.generic, lines[0] );
	parse_ip_to_sockaddr( &e1.generic, lines[1] );

	fail_unless( acl->len == 2, "Multiple lines not parsed" );

	struct ip_and_mask *entry;
	entry = &(*acl->entries)[0];
	fail_unless(entry->ip.family == e0.family, "entry 0 has wrong family!");
	entry = &(*acl->entries)[1];
	fail_unless(entry->ip.family == e1.family, "entry 1 has wrong family!");
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

START_TEST( test_includes_single_address_when_netmask_specified_ipv4 )
{
	char *lines[] = {"127.0.0.1/24"};
	struct acl * acl = acl_create( 1, lines, 0 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "127.0.0.0" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );

	parse_ip_to_sockaddr( &x.generic, "127.0.0.1" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );

	parse_ip_to_sockaddr( &x.generic, "127.0.0.255" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );
}
END_TEST

START_TEST( test_includes_single_address_when_netmask_specified_ipv6 )
{
	char *lines[] = {"fe80::/10"};
	struct acl * acl = acl_create( 1, lines, 0 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "fe80::1" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );

	parse_ip_to_sockaddr( &x.generic, "fe80::2" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );

	parse_ip_to_sockaddr( &x.generic, "fe80:ffff:ffff::ffff" );
	fail_unless( acl_includes( acl, &x ), "Included address wasn't covered" );
}
END_TEST

START_TEST( test_includes_single_address_when_multiple_entries_exist )
{
	char *lines[] = {"127.0.0.1", "::1"};
	struct acl * acl = acl_create( 2, lines, 0 );
	union mysockaddr e0;
	union mysockaddr e1;

	parse_ip_to_sockaddr( &e0.generic, "127.0.0.1" );
	parse_ip_to_sockaddr( &e1.generic, "::1" );

	fail_unless( acl_includes( acl, &e0 ), "Included address 0 wasn't covered" );
	fail_unless( acl_includes( acl, &e1 ), "Included address 1 wasn't covered" );
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

START_TEST( test_doesnt_include_other_address_when_netmask_specified )
{
	char *lines[] = {"127.0.0.1/32"};
	struct acl * acl  = acl_create( 1, lines, 0 );
	union mysockaddr x;

	parse_ip_to_sockaddr( &x.generic, "127.0.0.2" );
	fail_if( acl_includes( acl, &x ), "Excluded address was covered." );
}
END_TEST

START_TEST( test_doesnt_include_other_address_when_multiple_entries_exist )
{
	char *lines[] = {"127.0.0.1", "::1"};
	struct acl * acl  = acl_create( 2, lines, 0 );
	union mysockaddr e0;
	union mysockaddr e1;

	parse_ip_to_sockaddr( &e0.generic, "127.0.0.2" );
	parse_ip_to_sockaddr( &e1.generic, "::2" );

	fail_if( acl_includes( acl, &e0 ), "Excluded address 0 was covered." );
	fail_if( acl_includes( acl, &e1 ), "Excluded address 1 was covered." );
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


Suite* acl_suite(void)
{
	Suite *s = suite_create("acl");
	TCase *tc_create = tcase_create("create");
	TCase *tc_includes = tcase_create("includes");
	TCase *tc_destroy = tcase_create("destroy");


	tcase_add_test(tc_create, test_null_acl);
	tcase_add_test(tc_create, test_parses_single_line);
	tcase_add_test(tc_includes, test_parses_multiple_lines);

	tcase_add_test(tc_includes, test_includes_single_address);
	tcase_add_test(tc_includes, test_includes_single_address_when_netmask_specified_ipv4);
	tcase_add_test(tc_includes, test_includes_single_address_when_netmask_specified_ipv6);

	tcase_add_test(tc_includes, test_includes_single_address_when_multiple_entries_exist);

	tcase_add_test(tc_includes, test_doesnt_include_other_address);
	tcase_add_test(tc_includes, test_doesnt_include_other_address_when_netmask_specified);
	tcase_add_test(tc_includes, test_doesnt_include_other_address_when_multiple_entries_exist);

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
	log_level = 0;
	int number_failed;
	Suite *s = acl_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	log_level = 0;
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

