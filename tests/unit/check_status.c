#include "status.h"
#include "serve.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"

#include <check.h>

struct server *mock_server(void)
{
    struct server *out = xmalloc(sizeof(struct server));
    out->l_start_mirror = flexthread_mutex_create();
    out->nbd_client = xmalloc(sizeof(struct client_tbl_entry) * 4);
    out->max_nbd_clients = 4;
    out->size = 65536;

    out->allocation_map = bitset_alloc(65536, 4096);

    return out;
}

struct server *mock_mirroring_server(void)
{
    struct server *out = mock_server();
    out->mirror = xmalloc(sizeof(struct mirror));
    out->mirror_super = xmalloc(sizeof(struct mirror_super));
    return out;
}

void destroy_mock_server(struct server *serve)
{
    if (NULL != serve->mirror) {
	free(serve->mirror);
    }

    if (NULL != serve->mirror_super) {
	free(serve->mirror_super);
    }

    flexthread_mutex_destroy(serve->l_start_mirror);

    bitset_free(serve->allocation_map);
    free(serve->nbd_client);
    free(serve);
}

START_TEST(test_status_create)
{
    struct server *server = mock_server();
    struct status *status = status_create(server);

    fail_if(NULL == status, "Status wasn't allocated");
    status_destroy(status);
    destroy_mock_server(server);
}
END_TEST

START_TEST(test_gets_has_control)
{
    struct server *server = mock_server();
    server->success = 1;

    struct status *status = status_create(server);

    fail_unless(status->has_control == 1, "has_control wasn't copied");
    status_destroy(status);
    destroy_mock_server(server);
}
END_TEST

START_TEST(test_gets_is_mirroring)
{
    struct server *server = mock_server();
    struct status *status = status_create(server);

    fail_if(status->is_mirroring, "is_mirroring was set");
    status_destroy(status);
    destroy_mock_server(server);

    server = mock_mirroring_server();
    status = status_create(server);

    fail_unless(status->is_mirroring, "is_mirroring wasn't set");
    status_destroy(status);
    destroy_mock_server(server);
}
END_TEST

START_TEST(test_gets_clients_allowed)
{
    struct server *server = mock_server();
    struct status *status = status_create(server);

    fail_if(status->clients_allowed, "clients_allowed was set");
    status_destroy(status);

    server->allow_new_clients = 1;
    status = status_create(server);

    fail_unless(status->clients_allowed, "clients_allowed was not set");
    status_destroy(status);
    destroy_mock_server(server);

}
END_TEST

START_TEST(test_gets_pid)
{
    struct server *server = mock_server();
    struct status *status = status_create(server);

    fail_unless(getpid() == status->pid, "Pid wasn't gathered");

    status_destroy(status);
    destroy_mock_server(server);
}
END_TEST

START_TEST(test_gets_size)
{
    struct server *server = mock_server();
    server->size = 1024;

    struct status *status = status_create(server);

    fail_unless(1024 == status->size, "Size wasn't gathered");

    status_destroy(status);
    destroy_mock_server(server);
}
END_TEST

START_TEST(test_gets_migration_statistics)
{
    struct server *server = mock_mirroring_server();
    server->mirror->all_dirty = 16384;
    server->mirror->max_bytes_per_second = 32768;
    server->mirror->offset = 0;

    /* we have a bit of a time dependency here */
    server->mirror->migration_started = monotonic_time_ms();

    struct status *status = status_create(server);

    fail_unless(0 == status->migration_duration ||
		1 == status->migration_duration ||
		2 == status->migration_duration,
		"migration_duration is unreasonable!");

    fail_unless(16384 / (status->migration_duration + 1) ==
		status->migration_speed,
		"migration_speed not calculated correctly");

    fail_unless(32768 == status->migration_speed_limit,
		"migration_speed_limit not read");

    // ( size / current_bps ) + 1 happens to be 3 for this test
    fail_unless(3 == status->migration_seconds_left,
		"migration_seconds_left not gathered");

    status_destroy(status);
    destroy_mock_server(server);
}

END_TEST
#define RENDER_TEST_SETUP \
	struct status status; \
	int fds[2];           \
	pipe( fds );
void fail_unless_rendered(int fd, char *fragment)
{
    char buf[1024] = { 0 };
    char emsg[1024] = { 0 };
    char *found = NULL;

    sprintf(emsg, "Fragment: %s not found", fragment);

    fail_unless(read_until_newline(fd, buf, 1024) > 0, "Couldn't read");
    found = strstr(buf, fragment);
    fail_if(NULL == found, emsg);

    return;
}

void fail_if_rendered(int fd, char *fragment)
{
    char buf[1024] = { 0 };
    char emsg[1024] = { 0 };
    char *found = NULL;

    sprintf(emsg, "Fragment: %s found", fragment);

    fail_unless(read_until_newline(fd, buf, 1024) > 0, "Couldn't read");
    found = strstr(buf, fragment);
    fail_unless(NULL == found, emsg);

    return;
}

START_TEST(test_renders_has_control)
{
    RENDER_TEST_SETUP status.has_control = 1;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "has_control=true");

    status.has_control = 0;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "has_control=false");
}
END_TEST

START_TEST(test_renders_is_mirroring)
{
    RENDER_TEST_SETUP status.is_mirroring = 1;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "is_mirroring=true");

    status.is_mirroring = 0;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "is_mirroring=false");
}
END_TEST

START_TEST(test_renders_clients_allowed)
{
    RENDER_TEST_SETUP status.clients_allowed = 1;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "clients_allowed=true");

    status.clients_allowed = 0;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "clients_allowed=false");
}
END_TEST

START_TEST(test_renders_num_clients)
{
    RENDER_TEST_SETUP status.num_clients = 0;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "num_clients=0");

    status.num_clients = 4000;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "num_clients=4000");

}
END_TEST

START_TEST(test_renders_pid)
{
    RENDER_TEST_SETUP status.pid = 42;
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "pid=42");
}
END_TEST

START_TEST(test_renders_size)
{
    RENDER_TEST_SETUP status.size = ((uint64_t) 1 << 33);
    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "size=8589934592");
}
END_TEST

START_TEST(test_renders_migration_statistics)
{
    RENDER_TEST_SETUP status.is_mirroring = 0;
    status.migration_duration = 8;
    status.migration_speed = 40000000;
    status.migration_speed_limit = 40000001;
    status.migration_seconds_left = 1;
    status.migration_bytes_left = 5000;

    status_write(&status, fds[1]);
    fail_if_rendered(fds[0], "migration_duration");

    status_write(&status, fds[1]);
    fail_if_rendered(fds[0], "migration_speed");

    status_write(&status, fds[1]);
    fail_if_rendered(fds[0], "migration_speed_limit");

    status_write(&status, fds[1]);
    fail_if_rendered(fds[0], "migration_seconds_left");

    status.is_mirroring = 1;

    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "migration_duration=8");

    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "migration_speed=40000000");

    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "migration_speed_limit=40000001");

    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "migration_seconds_left=1");

    status_write(&status, fds[1]);
    fail_unless_rendered(fds[0], "migration_bytes_left=5000");

    status.migration_speed_limit = UINT64_MAX;

    status_write(&status, fds[1]);
    fail_if_rendered(fds[0], "migration_speed_limit");
}
END_TEST

Suite * status_suite(void)
{
    Suite *s = suite_create("status");
    TCase *tc_create = tcase_create("create");
    TCase *tc_render = tcase_create("render");

    tcase_add_test(tc_create, test_status_create);
    tcase_add_test(tc_create, test_gets_has_control);
    tcase_add_test(tc_create, test_gets_is_mirroring);
    tcase_add_test(tc_create, test_gets_clients_allowed);
    tcase_add_test(tc_create, test_gets_pid);
    tcase_add_test(tc_create, test_gets_size);
    tcase_add_test(tc_create, test_gets_migration_statistics);


    tcase_add_test(tc_render, test_renders_has_control);
    tcase_add_test(tc_render, test_renders_is_mirroring);
    tcase_add_test(tc_render, test_renders_clients_allowed);
    tcase_add_test(tc_render, test_renders_num_clients);
    tcase_add_test(tc_render, test_renders_pid);
    tcase_add_test(tc_render, test_renders_size);
    tcase_add_test(tc_render, test_renders_migration_statistics);

    suite_add_tcase(s, tc_create);
    suite_add_tcase(s, tc_render);

    return s;
}

int main(void)
{
    int number_failed;

    Suite *s = status_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
