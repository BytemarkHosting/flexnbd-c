#include "status.h"
#include "serve.h"
#include "ioutil.h"
#include "util.h"

#include <check.h>

struct server* mock_server(void)
{
	struct server* out = xmalloc( sizeof( struct server ) );
	out->l_start_mirror = flexthread_mutex_create();
	return out;
}

struct server* mock_mirroring_server(void)
{
	struct server *out = mock_server();
	out->mirror = xmalloc( sizeof( struct mirror ) );

	return out;
}

void destroy_mock_server( struct server* serve )
{
	if ( NULL != serve->mirror ) {
		free( serve->mirror );
	}

	flexthread_mutex_destroy( serve->l_start_mirror );
	free( serve );
}

START_TEST( test_status_create )
{
	struct server * server = mock_server();
	struct status * status = status_create( server );

	fail_if( NULL == status, "Status wasn't allocated" );
	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST

START_TEST( test_gets_has_control )
{
	struct server * server = mock_server();
	server->success = 1;

	struct status * status = status_create( server );

	fail_unless( status->has_control == 1, "has_control wasn't copied" );
	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST


START_TEST( test_gets_is_mirroring )
{
	struct server * server = mock_server();
	struct status * status = status_create( server );

	fail_if( status->is_mirroring, "is_mirroring was set" );
	status_destroy( status );
	destroy_mock_server( server );

	server = mock_mirroring_server();
	status = status_create( server );

	fail_unless( status->is_mirroring, "is_mirroring wasn't set" );
	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST

START_TEST( test_gets_migration_pass )
{
	struct server * server = mock_server();
	struct status * status = status_create( server );

	fail_if( status->migration_pass != 0, "migration_pass was set" );
	status_destroy( status );
	destroy_mock_server( server );

	server = mock_mirroring_server();
	server->mirror->pass = 1;
	status = status_create( server );

	fail_unless( status->migration_pass == 1, "migration_pass wasn't set" );
	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST


START_TEST( test_gets_pid )
{
	struct server * server = mock_server();
	struct status * status = status_create( server );

	fail_unless( getpid() == status->pid, "Pid wasn't gathered" );

	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST

START_TEST( test_gets_size )
{
	struct server * server = mock_server();
	server->size = 1024;

	struct status * status = status_create( server );

	fail_unless( 1024 == status->size, "Size wasn't gathered" );

	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST

START_TEST( test_gets_pass_statistics )
{
	struct server * server = mock_mirroring_server();
	server->mirror->this_pass_clean = 2048;
	server->mirror->this_pass_dirty = 4096;

	struct status * status = status_create( server );

	fail_unless( 2048 == status->pass_clean_bytes, "pass_clean_bytes wasn't gathered" );
	fail_unless( 4096 == status->pass_dirty_bytes, "pass_dirty_bytes wasn't gathered" );

	status_destroy( status );
	destroy_mock_server( server );
}
END_TEST


START_TEST( test_renders_has_control )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.has_control = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );

	char *found = strstr( buf, "has_control=true" );
	fail_if( NULL == found, "has_control=true not found" );

	status.has_control = 0;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "has_control=false" );
	fail_if( NULL == found, "has_control=false not found" );

}
END_TEST


START_TEST( test_renders_is_mirroring )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.is_mirroring = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );

	char *found = strstr( buf, "is_mirroring=true" );
	fail_if( NULL == found, "is_mirroring=true not found" );

	status.is_mirroring = 0;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "is_mirroring=false" );
	fail_if( NULL == found, "is_mirroring=false not found" );

}
END_TEST


START_TEST( test_renders_pid )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.pid = 42;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	char *found = strstr( buf, "pid=42" );
	fail_if( NULL == found, "pid=42 not found" );
}
END_TEST

START_TEST( test_renders_size )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.size = ( (uint64_t)1 << 33 );
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	char *found = strstr( buf, "size=8589934592" );
	fail_if( NULL == found, "size=8589934592 not found" );
}
END_TEST

START_TEST( test_renders_migration_pass )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};
	char *found;

	status.is_mirroring = 0;
	status.migration_pass = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "migration_pass" );
	fail_if( NULL != found, "migration pass output when not migrating" );

	status.is_mirroring = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "migration_pass=1" );
	fail_if( NULL == found, "migration pass not output when not migrating" );


}
END_TEST

START_TEST( test_renders_pass_statistics )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};
	char *found;

	status.is_mirroring = 0;
	status.pass_dirty_bytes = 2048;
	status.pass_clean_bytes = 4096;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "pass_dirty_bytes" );
	fail_if( NULL != found, "migration pass output when not migrating" );

	found = strstr( buf, "pass_clean_bytes" );
	fail_if( NULL != found, "migration pass output when not migrating" );

	status.is_mirroring = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "pass_dirty_bytes=2048" );
	fail_if( NULL == found, "migration pass not output when not migrating" );
	found = strstr( buf, "pass_clean_bytes=4096" );
	fail_if( NULL == found, "migration pass not output when not migrating" );

}
END_TEST


Suite *status_suite(void)
{
	Suite *s = suite_create("status");
	TCase *tc_create = tcase_create("create");
	TCase *tc_render = tcase_create("render");

	tcase_add_test(tc_create, test_status_create);
	tcase_add_test(tc_create, test_gets_has_control);
	tcase_add_test(tc_create, test_gets_is_mirroring);
	tcase_add_test(tc_create, test_gets_pid);
	tcase_add_test(tc_create, test_gets_size);
	tcase_add_test(tc_create, test_gets_migration_pass);
	tcase_add_test(tc_create, test_gets_pass_statistics);


	tcase_add_test(tc_render, test_renders_has_control);
	tcase_add_test(tc_render, test_renders_is_mirroring);
	tcase_add_test(tc_render, test_renders_pid);
	tcase_add_test(tc_render, test_renders_size);
	tcase_add_test(tc_render, test_renders_migration_pass);
	tcase_add_test(tc_render, test_renders_pass_statistics);

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

