#include "serve.h"
#include "listen.h"
#include "util.h"
#include "flexnbd.h"

#include <check.h>
#include <string.h>

START_TEST( test_defaults_main_serve_opts )
{
	struct flexnbd flexnbd;
	struct listen * listen = listen_create( &flexnbd, "127.0.0.1", NULL, "4777", NULL,
			"foo", 0, 0, NULL, 1 );
	NULLCHECK( listen );
	struct server *init_serve = listen->init_serve;
	struct server *main_serve = listen->main_serve;
	NULLCHECK( init_serve );
	NULLCHECK( main_serve );

	fail_unless( 0 == memcmp(&init_serve->bind_to,
				&main_serve->bind_to,
				sizeof( union mysockaddr )),
			"Main serve bind_to was not set" );
}
END_TEST


Suite* listen_suite(void)
{
	Suite *s = suite_create("listen");
	TCase *tc_create = tcase_create("create");

	tcase_add_exit_test(tc_create, test_defaults_main_serve_opts, 0);

	suite_add_tcase(s, tc_create);

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
	Suite *s = listen_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

