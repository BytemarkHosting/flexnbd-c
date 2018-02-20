#include "parse.h"
#include "util.h"

#include <check.h>

START_TEST(test_can_parse_ip_address_twice)
{
    char ip_address[] = "127.0.0.1";
    struct sockaddr saddr;

    parse_ip_to_sockaddr(&saddr, ip_address);
    parse_ip_to_sockaddr(&saddr, ip_address);
}

END_TEST Suite * parse_suite(void)
{
    Suite *s = suite_create("parse");
    TCase *tc_create = tcase_create("ip_to_sockaddr");

    tcase_add_test(tc_create, test_can_parse_ip_address_twice);

    suite_add_tcase(s, tc_create);

    return s;
}

#ifdef DEBUG
#define LOG_LEVEL 0
#else
#define LOG_LEVEL 2
#endif

int main(void)
{
    log_level = LOG_LEVEL;
    int number_failed;
    Suite *s = parse_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
