#include "serve.h"
#include "util.h"

#include "self_pipe.h"
#include "client.h"
#include "flexnbd.h"

#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef DEBUG
#define LOG_LEVEL 0
#else
#define LOG_LEVEL 2
#endif


/* Need these because libcheck is braindead and doesn't
 * run teardown after a failing test
 */
#define myfail( msg ) do { teardown(); fail(msg); } while (0)
#define myfail_if( tst, msg ) do { if( tst ) { myfail( msg ); } } while (0)
#define myfail_unless( tst, msg ) myfail_if( !(tst), msg )

char *dummy_file;

char *make_tmpfile(void)
{
    FILE *fp;
    char *fn_buf;
    char leader[] = "/tmp/check_serve";

    fn_buf = (char *) malloc(1024);
    strncpy(fn_buf, leader, sizeof(leader) - 1);
    snprintf(&fn_buf[sizeof(leader) - 1], 10, "%d", getpid());
    fp = fopen(fn_buf, "w");
    fwrite(fn_buf, 1024, 1, fp);
    fclose(fp);

    return fn_buf;
}


void setup(void)
{
    dummy_file = make_tmpfile();
}

void teardown(void)
{
    if (dummy_file) {
	unlink(dummy_file);
    }
    free(dummy_file);
    dummy_file = NULL;
}


START_TEST(test_replaces_acl)
{
    struct flexnbd flexnbd;
    flexnbd.signal_fd = -1;
    struct server *s =
	server_create(&flexnbd, "127.0.0.1", "0", dummy_file, 0, 0, NULL,
		      1, 0, 1);
    struct acl *new_acl = acl_create(0, NULL, 0);

    server_replace_acl(s, new_acl);

    myfail_unless(s->acl == new_acl, "ACL wasn't replaced.");
    server_destroy(s);
}
END_TEST

START_TEST(test_signals_acl_updated)
{
    struct flexnbd flexnbd;
    flexnbd.signal_fd = -1;
    struct server *s =
	server_create(&flexnbd, "127.0.0.1", "0", dummy_file, 0, 0, NULL,
		      1, 0, 1);
    struct acl *new_acl = acl_create(0, NULL, 0);

    server_replace_acl(s, new_acl);

    myfail_unless(1 == self_pipe_signal_clear(s->acl_updated_signal),
		  "No signal sent.");
    server_destroy(s);
}
END_TEST

int connect_client(char *addr, int actual_port, char *source_addr)
{
    int client_fd = -1;

    struct addrinfo hint;
    struct addrinfo *ailist, *aip;




    memset(&hint, '\0', sizeof(struct addrinfo));
    hint.ai_socktype = SOCK_STREAM;

    myfail_if(getaddrinfo(addr, NULL, &hint, &ailist) != 0,
	      "getaddrinfo failed.");

    int connected = 0;
    for (aip = ailist; aip; aip = aip->ai_next) {
	((struct sockaddr_in *) aip->ai_addr)->sin_port =
	    htons(actual_port);
	client_fd =
	    socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol);

	if (source_addr) {
	    struct sockaddr src;
	    if (!parse_ip_to_sockaddr(&src, source_addr)) {
		close(client_fd);
		continue;
	    }
	    bind(client_fd, &src, sizeof(struct sockaddr_in6));
	}

	if (client_fd == -1) {
	    continue;
	}
	if (connect(client_fd, aip->ai_addr, aip->ai_addrlen) == 0) {
	    connected = 1;
	    break;
	}
	close(client_fd);
    }

    myfail_unless(connected, "Didn't connect.");
    return client_fd;
}

/* These are "internal" functions we need for the following test.  We
 * shouldn't need them but there's no other way at the moment. */
void serve_open_server_socket(struct server *);
int server_port(struct server *);
void server_accept(struct server *);
int fd_is_closed(int);
void server_close_clients(struct server *);

START_TEST(test_acl_update_closes_bad_client)
{
    /* This is the wrong way round.  Rather than pulling the thread
     * and socket out of the server structure, we should be testing
     * a client socket.
     */
    struct flexnbd flexnbd;
    flexnbd.signal_fd = -1;
    struct server *s =
	server_create(&flexnbd, "127.0.0.7", "0", dummy_file, 0, 0, NULL,
		      1, 0, 1);
    struct acl *new_acl = acl_create(0, NULL, 1);
    struct client *c;
    struct client_tbl_entry *entry;

    int actual_port;
    int client_fd;
    int server_fd;


    serve_open_server_socket(s);
    actual_port = server_port(s);

    client_fd = connect_client("127.0.0.7", actual_port, "127.0.0.1");
    server_accept(s);
    entry = &s->nbd_client[0];
    c = entry->client;
    /* At this point there should be an entry in the nbd_clients
     * table and a background thread to run the client loop
     */
    myfail_if(entry->thread == 0, "No client thread was started.");
    server_fd = c->socket;
    myfail_if(fd_is_closed(server_fd),
	      "Sanity check failed - client socket wasn't open.");

    server_replace_acl(s, new_acl);

    /* accept again, so that we can react to the acl replacement signal */
    server_accept(s);

    /* Fail if we time out here */
    while (!fd_is_closed(server_fd));

    close(client_fd);
    server_close_clients(s);
    server_destroy(s);
}
END_TEST

START_TEST(test_acl_update_leaves_good_client)
{
    struct flexnbd flexnbd;
    flexnbd.signal_fd = -1;

    struct server *s =
	server_create(&flexnbd, "127.0.0.7", "0", dummy_file, 0, 0, NULL,
		      1, 0, 1);

    char *lines[] = { "127.0.0.1" };
    struct acl *new_acl = acl_create(1, lines, 1);
    struct client *c;
    struct client_tbl_entry *entry;

    int actual_port;
    int client_fd;
    int server_fd;

    serve_open_server_socket(s);
    actual_port = server_port(s);
    client_fd = connect_client("127.0.0.7", actual_port, "127.0.0.1");
    server_accept(s);
    entry = &s->nbd_client[0];
    c = entry->client;
    /* At this point there should be an entry in the nbd_clients
     * table and a background thread to run the client loop
     */
    myfail_if(entry->thread == 0, "No client thread was started.");
    server_fd = c->socket;
    myfail_if(fd_is_closed(server_fd),
	      "Sanity check failed - client socket wasn't open.");

    server_replace_acl(s, new_acl);
    server_accept(s);

    myfail_if(self_pipe_signal_clear(c->stop_signal),
	      "Client was told to stop.");

    close(client_fd);
    server_close_clients(s);
    server_destroy(s);
}
END_TEST

Suite * serve_suite(void)
{
    Suite *s = suite_create("serve");
    TCase *tc_acl_update = tcase_create("acl_update");

    tcase_add_checked_fixture(tc_acl_update, setup, NULL);

    tcase_add_test(tc_acl_update, test_replaces_acl);
    tcase_add_test(tc_acl_update, test_signals_acl_updated);

    tcase_add_exit_test(tc_acl_update, test_acl_update_closes_bad_client,
			0);
    tcase_add_exit_test(tc_acl_update, test_acl_update_leaves_good_client,
			0);

    suite_add_tcase(s, tc_acl_update);

    return s;
}

int main(void)
{
    log_level = LOG_LEVEL;
    error_init();
    int number_failed;
    Suite *s = serve_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
