#include "control.h"
#include "flexnbd.h"

#include <check.h>


START_TEST(test_assigns_sock_name)
{
    struct flexnbd flexnbd = { 0 };
    char csn[] = "foobar";

    struct control *control = control_create(&flexnbd, csn);

    fail_unless(csn == control->socket_name, "Socket name not assigned");
}
END_TEST

Suite * control_suite(void)
{
    Suite *s = suite_create("control");

    TCase *tc_create = tcase_create("create");

    tcase_add_test(tc_create, test_assigns_sock_name);
    suite_add_tcase(s, tc_create);

    return s;
}

int main(void)
{
    int number_failed;

    Suite *s = control_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
