#include <check.h>

#include "bitset.h"

START_TEST(test_bit_set)
{
	unsigned char num;
	char *bits = (char*) &num;
	
#define TEST_BIT_SET(bit, newvalue) \
	bit_set(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));
	
	TEST_BIT_SET(0, 1);
	TEST_BIT_SET(1, 3);
	TEST_BIT_SET(2, 7);
	TEST_BIT_SET(7, 0x87);
}
END_TEST

START_TEST(test_bit_clear)
{
	unsigned char num = 255;
	char *bits = (char*) &num;
	
#define TEST_BIT_CLEAR(bit, newvalue) \
	bit_clear(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));
	
	TEST_BIT_CLEAR(0, 0xfe);
	TEST_BIT_CLEAR(1, 0xfc);
	TEST_BIT_CLEAR(2, 0xf8);
	TEST_BIT_CLEAR(7, 0x78);
}
END_TEST

Suite* bitset_suite()
{
	Suite *s = suite_create("bitset");
	TCase *tc_core = tcase_create("bitset");
	tcase_add_test(tc_core, test_bit_set);
	tcase_add_test(tc_core, test_bit_clear);
	suite_add_tcase(s, tc_core);
	
	return s;
}

int main(void)
{
	int number_failed;
	Suite *s = bitset_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}
