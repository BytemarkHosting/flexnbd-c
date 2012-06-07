#include <check.h>

#include "self_pipe.h"

#include "client.h"


START_TEST( test_assigns_socket )
{
	struct client * c;

	c = client_create( NULL, 42 );

	fail_unless( 42 == c->socket, "Socket wasn't assigned." );
}
END_TEST


START_TEST( test_assigns_server )
{
	struct client * c;
	/* can't predict the storage size so we can't allocate one on
	 * the stack
	 */
	struct server * s = (struct server *)42;

	c = client_create( (struct server *)s, 0 );

	fail_unless( s == c->serve, "Serve wasn't assigned." );

}
END_TEST


START_TEST( test_opens_stop_signal )
{
	struct client *c = client_create( NULL, 0 );

	client_signal_stop( c );

	fail_unless( 1 == self_pipe_signal_clear( c->stop_signal ),
			"No signal was sent." );

}
END_TEST


START_TEST( test_closes_stop_signal )
{
	struct client *c = client_create( NULL, 0 );
	int read_fd = c->stop_signal->read_fd;
	int write_fd = c->stop_signal->write_fd;

	client_destroy( c );

	fail_unless( fd_is_closed( read_fd ), "Stop signal wasn't destroyed." );
	fail_unless( fd_is_closed( write_fd ), "Stop signal wasn't destroyed." );
}
END_TEST


Suite *client_suite()
{
	Suite *s = suite_create("client");

	TCase *tc_create = tcase_create("create");
	TCase *tc_signal = tcase_create("signal");
	TCase *tc_destroy = tcase_create("destroy");

	tcase_add_test(tc_create, test_assigns_socket);
	tcase_add_test(tc_create, test_assigns_server);

	tcase_add_test(tc_signal, test_opens_stop_signal);

	tcase_add_test( tc_destroy, test_closes_stop_signal );

	suite_add_tcase(s, tc_create);
	suite_add_tcase(s, tc_signal);
	suite_add_tcase(s, tc_destroy);

	return s;
}

int main(void)
{
	int number_failed;
	
	Suite *s = client_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}

