#include "flexthread.h"
#include "util.h"

#include <check.h>


START_TEST(test_mutex_create)
{
    struct flexthread_mutex *ftm = flexthread_mutex_create();
    NULLCHECK(ftm);
    flexthread_mutex_destroy(ftm);
}
END_TEST

START_TEST(test_mutex_lock)
{
    struct flexthread_mutex *ftm = flexthread_mutex_create();

    fail_if(flexthread_mutex_held(ftm),
	    "Flexthread_mutex is held before lock");
    flexthread_mutex_lock(ftm);
    fail_unless(flexthread_mutex_held(ftm),
		"Flexthread_mutex is not held inside lock");
    flexthread_mutex_unlock(ftm);
    fail_if(flexthread_mutex_held(ftm),
	    "Flexthread_mutex is held after unlock");

    flexthread_mutex_destroy(ftm);
}
END_TEST

Suite * flexthread_suite(void)
{
    Suite *s = suite_create("flexthread");
    TCase *tc_create = tcase_create("create");
    TCase *tc_destroy = tcase_create("destroy");

    tcase_add_test(tc_create, test_mutex_create);
    tcase_add_test(tc_create, test_mutex_lock);

    suite_add_tcase(s, tc_create);
    suite_add_tcase(s, tc_destroy);

    return s;
}

int main(void)
{
#ifdef DEBUG
    log_level = 0;
#else
    log_level = 2;
#endif
    int number_failed;
    Suite *s = flexthread_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    log_level = 0;
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
