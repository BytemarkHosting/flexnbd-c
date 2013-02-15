#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "sockutil.h"

#include <check.h>

START_TEST( test_sockaddr_address_string_af_inet_converts_to_string )
{
	struct sockaddr sa;
	struct sockaddr_in* v4 = (struct sockaddr_in*) &sa;
	char testbuf[128];
	const char* result;

	v4->sin_family = AF_INET;
	v4->sin_port = htons( 4777 );
	ck_assert_int_eq( 1, inet_pton( AF_INET, "192.168.0.1", &v4->sin_addr ));

	result = sockaddr_address_string( &sa, &testbuf[0], 128 );
	ck_assert( result != NULL );

	ck_assert_str_eq( "192.168.0.1 port 4777", testbuf );
}
END_TEST


START_TEST( test_sockaddr_address_string_af_inet6_converts_to_string )
{
	struct sockaddr_in6 v6_raw;
	struct sockaddr_in6* v6 = &v6_raw;
	struct sockaddr* sa = (struct sockaddr*) &v6_raw;

	char testbuf[128];
	const char* result;

	v6->sin6_family = AF_INET6;
	v6->sin6_port = htons( 4777 );
	ck_assert_int_eq( 1, inet_pton( AF_INET6, "fe80::1", &v6->sin6_addr ));

	result = sockaddr_address_string( sa, &testbuf[0], 128 );
	ck_assert( result != NULL );

	ck_assert_str_eq( "fe80::1 port 4777", testbuf );
}
END_TEST

/* We don't know what it is, so we just call it "???" and return NULL */
START_TEST( test_sockaddr_address_string_af_unspec_is_failure )
{
	struct sockaddr sa;
	struct sockaddr_in* v4 = (struct sockaddr_in*) &sa;
	char testbuf[128];
	const char* result;

	v4->sin_family = AF_UNSPEC;
	v4->sin_port = htons( 4777 );
	ck_assert_int_eq( 1, inet_pton( AF_INET, "192.168.0.1", &v4->sin_addr ));

	result = sockaddr_address_string( &sa, &testbuf[0], 128 );
	ck_assert( result == NULL );

	ck_assert_str_eq( "???", testbuf );
}
END_TEST

/* This is a complete failure to parse, rather than a partial failure */
START_TEST( test_sockaddr_address_string_doesnt_overflow_short_buffer )
{
	struct sockaddr sa;
	struct sockaddr_in* v4 = (struct sockaddr_in*) &sa;
	char testbuf[128];
	const char* result;

	v4->sin_family = AF_INET;
	v4->sin_port = htons( 4777 );
	ck_assert_int_eq( 1, inet_pton( AF_INET, "192.168.0.1", &v4->sin_addr ));

	result = sockaddr_address_string( &sa, &testbuf[0], 4 );
	ck_assert( result == NULL );

	ck_assert_str_eq( "", testbuf );

}
END_TEST

Suite *sockutil_suite(void)
{
	Suite *s = suite_create("sockutil");

	TCase *tc_sockaddr_address_string = tcase_create("sockaddr_address_string");

	tcase_add_test(tc_sockaddr_address_string, test_sockaddr_address_string_af_inet_converts_to_string);
	tcase_add_test(tc_sockaddr_address_string, test_sockaddr_address_string_af_inet6_converts_to_string);
    tcase_add_test(tc_sockaddr_address_string, test_sockaddr_address_string_af_unspec_is_failure);
	tcase_add_test(tc_sockaddr_address_string, test_sockaddr_address_string_doesnt_overflow_short_buffer);
	suite_add_tcase(s, tc_sockaddr_address_string);

	return s;
}

int main(void)
{
	int number_failed;

	Suite *s = sockutil_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

