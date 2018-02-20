#include "ioutil.h"

#include <check.h>

START_TEST(test_read_until_newline_returns_line_length_plus_null)
{
    int fds[2];
    int nread;
    char buf[5] = { 0 };
    pipe(fds);

    write(fds[1], "1234\n", 5);

    nread = read_until_newline(fds[0], buf, 5);

    ck_assert_int_eq(5, nread);
}

END_TEST START_TEST(test_read_until_newline_inserts_null)
{
    int fds[2];
    int nread;
    char buf[5] = { 0 };
    pipe(fds);

    write(fds[1], "1234\n", 5);

    nread = read_until_newline(fds[0], buf, 5);

    ck_assert_int_eq('\0', buf[4]);

}

END_TEST START_TEST(test_read_empty_line_inserts_null)
{
    int fds[2];
    int nread;
    char buf[5] = { 0 };
    pipe(fds);

    write(fds[1], "\n", 1);
    nread = read_until_newline(fds[0], buf, 1);

    ck_assert_int_eq('\0', buf[0]);
    ck_assert_int_eq(1, nread);
}

END_TEST START_TEST(test_read_eof_returns_err)
{
    int fds[2];
    int nread;
    char buf[5] = { 0 };
    pipe(fds);

    close(fds[1]);
    nread = read_until_newline(fds[0], buf, 5);

    ck_assert_int_eq(-1, nread);
}

END_TEST START_TEST(test_read_eof_fills_line)
{
    int fds[2];
    int nread;
    char buf[5] = { 0 };
    pipe(fds);

    write(fds[1], "1234", 4);
    close(fds[1]);
    nread = read_until_newline(fds[0], buf, 5);

    ck_assert_int_eq(-1, nread);
    ck_assert_int_eq('4', buf[3]);
}

END_TEST START_TEST(test_read_lines_until_blankline)
{
    char **lines = NULL;
    int fds[2];
    int nlines;
    pipe(fds);

    write(fds[1], "a\nb\nc\n\n", 7);

    nlines = read_lines_until_blankline(fds[0], 256, &lines);

    ck_assert_int_eq(3, nlines);
}

END_TEST Suite * ioutil_suite(void)
{
    Suite *s = suite_create("ioutil");

    TCase *tc_read_until_newline = tcase_create("read_until_newline");
    TCase *tc_read_lines_until_blankline =
	tcase_create("read_lines_until_blankline");

    tcase_add_test(tc_read_until_newline,
		   test_read_until_newline_returns_line_length_plus_null);
    tcase_add_test(tc_read_until_newline,
		   test_read_until_newline_inserts_null);
    tcase_add_test(tc_read_until_newline,
		   test_read_empty_line_inserts_null);
    tcase_add_test(tc_read_until_newline, test_read_eof_returns_err);
    tcase_add_test(tc_read_until_newline, test_read_eof_fills_line);

    tcase_add_test(tc_read_lines_until_blankline,
		   test_read_lines_until_blankline);

    suite_add_tcase(s, tc_read_until_newline);
    suite_add_tcase(s, tc_read_lines_until_blankline);

    return s;
}

int main(void)
{
    int number_failed;

    Suite *s = ioutil_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
