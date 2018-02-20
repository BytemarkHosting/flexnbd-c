#include <check.h>
#include <stdio.h>

#include "self_pipe.h"
#include "nbdtypes.h"

#include "serve.h"
#include "client.h"

#include <unistd.h>

struct server fake_server = { 0 };

#define FAKE_SERVER &fake_server
#define FAKE_SOCKET (42)

START_TEST(test_assigns_socket)
{
    struct client *c;

    c = client_create(FAKE_SERVER, FAKE_SOCKET);

    fail_unless(42 == c->socket, "Socket wasn't assigned.");
}
END_TEST

START_TEST(test_assigns_server)
{
    struct client *c;
    /* can't predict the storage size so we can't allocate one on
     * the stack
     */
    c = client_create(FAKE_SERVER, FAKE_SOCKET);

    fail_unless(FAKE_SERVER == c->serve, "Serve wasn't assigned.");

}
END_TEST

START_TEST(test_opens_stop_signal)
{
    struct client *c = client_create(FAKE_SERVER, FAKE_SOCKET);

    client_signal_stop(c);

    fail_unless(1 == self_pipe_signal_clear(c->stop_signal),
		"No signal was sent.");

}
END_TEST

int fd_is_closed(int);

START_TEST(test_closes_stop_signal)
{
    struct client *c = client_create(FAKE_SERVER, FAKE_SOCKET);
    int read_fd = c->stop_signal->read_fd;
    int write_fd = c->stop_signal->write_fd;

    client_destroy(c);

    fail_unless(fd_is_closed(read_fd), "Stop signal wasn't destroyed.");
    fail_unless(fd_is_closed(write_fd), "Stop signal wasn't destroyed.");
}
END_TEST

START_TEST(test_read_request_quits_on_stop_signal)
{
    int fds[2];
    struct nbd_request nbdr;
    pipe(fds);
    struct client *c = client_create(FAKE_SERVER, fds[0]);

    client_signal_stop(c);

    int client_serve_request(struct client *);
    fail_unless(1 == client_serve_request(c), "Didn't quit on stop.");

    close(fds[0]);
    close(fds[1]);
}
END_TEST

Suite * client_suite(void)
{
    Suite *s = suite_create("client");

    TCase *tc_create = tcase_create("create");
    TCase *tc_signal = tcase_create("signal");
    TCase *tc_destroy = tcase_create("destroy");

    tcase_add_test(tc_create, test_assigns_socket);
    tcase_add_test(tc_create, test_assigns_server);

    tcase_add_test(tc_signal, test_opens_stop_signal);
    tcase_add_test(tc_signal, test_read_request_quits_on_stop_signal);

    tcase_add_test(tc_destroy, test_closes_stop_signal);

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
