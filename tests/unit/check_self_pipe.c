#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <check.h>
#include <mcheck.h>

#include "self_pipe.h"

START_TEST(test_opens_pipe)
{
    struct self_pipe *sig;
    char buf[] = " ";

    sig = self_pipe_create();

    write(sig->write_fd, "1", 1);
    read(sig->read_fd, buf, 1);

    fail_unless(buf[0] == '1', "Pipe does not seem to be open;");
    self_pipe_destroy(sig);
}
END_TEST

void *signal_thread(void *thing)
{
    struct self_pipe *sig = (struct self_pipe *) thing;
    usleep(100000);
    self_pipe_signal(sig);
    return NULL;
}

pthread_t start_signal_thread(struct self_pipe * sig)
{
    pthread_attr_t attr;
    pthread_t thread_id;

    pthread_attr_init(&attr);
    pthread_create(&thread_id, &attr, signal_thread, sig);
    pthread_attr_destroy(&attr);

    return thread_id;
}


START_TEST(test_signals)
{
    struct self_pipe *sig;
    fd_set fds;
    pthread_t signal_thread_id;

    sig = self_pipe_create();

    FD_ZERO(&fds);
    self_pipe_fd_set(sig, &fds);

    signal_thread_id = start_signal_thread(sig);
    if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) == -1) {
	fail(strerror(errno));
    }
    self_pipe_signal_clear(sig);

    fail_unless(self_pipe_fd_isset(sig, &fds),
		"Signalled pipe was not FD_ISSET.");
    pthread_join(signal_thread_id, NULL);

    self_pipe_destroy(sig);
}
END_TEST

START_TEST(test_clear_returns_immediately)
{
    struct self_pipe *sig;
    sig = self_pipe_create();
    fail_unless(0 == self_pipe_signal_clear(sig), "Wrong clear result.");
}
END_TEST

START_TEST(test_destroy_closes_read_pipe)
{
    struct self_pipe *sig;
    ssize_t read_len;
    int orig_read_fd;

    sig = self_pipe_create();
    orig_read_fd = sig->read_fd;
    self_pipe_destroy(sig);

    while ((read_len = read(orig_read_fd, "", 0)) == -1 && errno == EINTR);

    switch (read_len) {
    case 0:
	fail("The read fd wasn't closed.");
	break;
    case -1:
	switch (errno) {
	case EBADF:
	    /* This is what we want */
	    break;
	case EAGAIN:
	    fail("The read fd wasn't closed.");
	    break;
	default:
	    fail(strerror(errno));
	    break;
	}
	break;
    default:
	fail("The read fd wasn't closed, and had data in it.");
	break;
    }
}
END_TEST

START_TEST(test_destroy_closes_write_pipe)
{
    struct self_pipe *sig;
    ssize_t write_len;
    int orig_write_fd;

    sig = self_pipe_create();
    orig_write_fd = sig->write_fd;
    self_pipe_destroy(sig);

    while ((write_len = write(orig_write_fd, "", 0)) == -1
	   && errno == EINTR);

    switch (write_len) {
    case 0:
	fail("The write fd wasn't closed.");
	break;
    case -1:
	switch (errno) {
	case EPIPE:
	case EBADF:
	    /* This is what we want */
	    break;
	case EAGAIN:
	    fail("The write fd wasn't closed.");
	    break;
	default:
	    fail(strerror(errno));
	    break;
	}
	break;
    default:
	/* To get here, the write(_,_,0) would have to
	 * write some bytes.
	 */
	fail("The write fd wasn't closed, and something REALLY WEIRD is going on.");
	break;
    }

}
END_TEST

Suite * self_pipe_suite(void)
{
    Suite *s = suite_create("self_pipe");

    TCase *tc_create = tcase_create("create");
    TCase *tc_signal = tcase_create("signal");
    TCase *tc_destroy = tcase_create("destroy");

    tcase_add_test(tc_create, test_opens_pipe);
    tcase_add_test(tc_signal, test_signals);
    tcase_add_test(tc_signal, test_clear_returns_immediately);
    tcase_add_test(tc_destroy, test_destroy_closes_read_pipe);
    tcase_add_test(tc_destroy, test_destroy_closes_write_pipe);
    /* We don't test that destroy free()'s the self_pipe pointer because
     * that'll be caught by valgrind.
     */

    suite_add_tcase(s, tc_create);
    suite_add_tcase(s, tc_signal);
    suite_add_tcase(s, tc_destroy);

    return s;
}

int main(void)
{
    int number_failed;

    Suite *s = self_pipe_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
