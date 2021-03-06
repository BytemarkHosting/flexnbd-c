#include <check.h>

#include "bitset.h"

#define assert_bitset_is( map, val ) {\
	uint64_t *num = (uint64_t*) map->bits; \
	ck_assert_int_eq( val, *num ); \
}

START_TEST(test_bit_set)
{
    uint64_t num = 0;
    bitfield_p bits = (bitfield_p) & num;

#define TEST_BIT_SET(bit, newvalue) \
	bit_set(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));

    TEST_BIT_SET(0, 1);
    TEST_BIT_SET(1, 3);
    TEST_BIT_SET(2, 7);
    TEST_BIT_SET(7, 0x87);
    TEST_BIT_SET(63, 0x8000000000000087);
}
END_TEST

START_TEST(test_bit_clear)
{
    uint64_t num = 0xffffffffffffffff;
    bitfield_p bits = (bitfield_p) & num;

#define TEST_BIT_CLEAR(bit, newvalue) \
	bit_clear(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));

    TEST_BIT_CLEAR(0, 0xfffffffffffffffe);
    TEST_BIT_CLEAR(1, 0xfffffffffffffffc);
    TEST_BIT_CLEAR(2, 0xfffffffffffffff8);
    TEST_BIT_CLEAR(7, 0xffffffffffffff78);
    TEST_BIT_CLEAR(63, 0x7fffffffffffff78);
}
END_TEST

START_TEST(test_bit_tests)
{
    uint64_t num = 0x5555555555555555;
    bitfield_p bits = (bitfield_p) & num;

    fail_unless(bit_has_value(bits, 0, 1), "bit_has_value malfunction");
    fail_unless(bit_has_value(bits, 1, 0), "bit_has_value malfunction");
    fail_unless(bit_has_value(bits, 63, 0), "bit_has_value malfunction");
    fail_unless(bit_is_set(bits, 0), "bit_is_set malfunction");
    fail_unless(bit_is_clear(bits, 1), "bit_is_clear malfunction");
    fail_unless(bit_is_set(bits, 62), "bit_is_set malfunction");
    fail_unless(bit_is_clear(bits, 63), "bit_is_clear malfunction");
}
END_TEST

START_TEST(test_bit_ranges)
{
    bitfield_word_t buffer[BIT_WORDS_FOR_SIZE(4160)];
    uint64_t *longs = (uint64_t *) buffer;
    uint64_t i;

    memset(buffer, 0, 4160);

    for (i = 0; i < 64; i++) {
	bit_set_range(buffer, i * 64, i);
	fail_unless(longs[i] == (1ULL << i) - 1,
		    "longs[%ld] = %lx SHOULD BE %lx",
		    i, longs[i], (1ULL << i) - 1);

	fail_unless(longs[i + 1] == 0, "bit_set_range overshot at i=%d",
		    i);
    }

    for (i = 0; i < 64; i++) {
	bit_clear_range(buffer, i * 64, i);
	fail_unless(longs[i] == 0, "bit_clear_range didn't work at i=%d",
		    i);
    }
}
END_TEST

START_TEST(test_bit_runs)
{
    bitfield_word_t buffer[BIT_WORDS_FOR_SIZE(256)];
    int i, ptr = 0, runs[] = {
	56, 97, 22, 12, 83, 1, 45, 80, 85, 51, 64, 40, 63, 67, 75, 64, 94,
	    81, 79, 62
    };

    memset(buffer, 0, 256);

    for (i = 0; i < 20; i += 2) {
	ptr += runs[i];
	bit_set_range(buffer, ptr, runs[i + 1]);
	ptr += runs[i + 1];
    }

    ptr = 0;

    for (i = 0; i < 20; i += 1) {
	int run = bit_run_count(buffer, ptr, 2048 - ptr, NULL);
	fail_unless(run == runs[i],
		    "run %d should have been %d, was %d", i, runs[i], run);
	ptr += runs[i];
    }
}
END_TEST

START_TEST(test_bitset)
{
    struct bitset *map;
    uint64_t *num;

    map = bitset_alloc(6400, 100);
    num = (uint64_t *) map->bits;

    bitset_set_range(map, 0, 50);
    ck_assert_int_eq(1, *num);
    bitset_set_range(map, 99, 1);
    ck_assert_int_eq(1, *num);
    bitset_set_range(map, 100, 1);
    ck_assert_int_eq(3, *num);
    bitset_set_range(map, 0, 800);
    ck_assert_int_eq(255, *num);
    bitset_set_range(map, 1499, 2);
    ck_assert_int_eq(0xc0ff, *num);
    bitset_clear_range(map, 1499, 2);
    ck_assert_int_eq(255, *num);

    *num = 0;
    bitset_set_range(map, 1499, 2);
    bitset_clear_range(map, 1300, 200);
    ck_assert_int_eq(0x8000, *num);

    *num = 0;
    bitset_set_range(map, 0, 6400);
    ck_assert_int_eq(0xffffffffffffffff, *num);
    bitset_clear_range(map, 3200, 400);
    ck_assert_int_eq(0xfffffff0ffffffff, *num);
}
END_TEST

START_TEST(test_bitset_set)
{
    struct bitset *map;
    uint64_t run;

    map = bitset_alloc(64, 1);

    assert_bitset_is(map, 0x0000000000000000);
    bitset_set(map);
    assert_bitset_is(map, 0xffffffffffffffff);
    bitset_free(map);

    map = bitset_alloc(6400, 100);
    assert_bitset_is(map, 0x0000000000000000);
    bitset_set(map);
    assert_bitset_is(map, 0xffffffffffffffff);
    bitset_free(map);

    // Now do something large and representative
    map = bitset_alloc(53687091200, 4096);
    bitset_set(map);

    run = bitset_run_count(map, 0, 53687091200);
    ck_assert_int_eq(run, 53687091200);
    bitset_free(map);


}
END_TEST

START_TEST(test_bitset_clear)
{
    struct bitset *map;
    uint64_t *num;
    uint64_t run;

    map = bitset_alloc(64, 1);
    num = (uint64_t *) map->bits;

    ck_assert_int_eq(0x0000000000000000, *num);
    bitset_set(map);
    bitset_clear(map);
    ck_assert_int_eq(0x0000000000000000, *num);

    bitset_free(map);

    // Now do something large and representative
    map = bitset_alloc(53687091200, 4096);
    bitset_set(map);
    bitset_clear(map);
    run = bitset_run_count(map, 0, 53687091200);
    ck_assert_int_eq(run, 53687091200);
    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_set_range)
{
    struct bitset *map = bitset_alloc(64, 1);
    assert_bitset_is(map, 0x0000000000000000);

    bitset_set_range(map, 8, 8);
    assert_bitset_is(map, 0x000000000000ff00);

    bitset_clear(map);
    assert_bitset_is(map, 0x0000000000000000);
    bitset_set_range(map, 0, 0);
    assert_bitset_is(map, 0x0000000000000000);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_clear_range)
{
    struct bitset *map = bitset_alloc(64, 1);
    bitset_set(map);
    assert_bitset_is(map, 0xffffffffffffffff);

    bitset_clear_range(map, 8, 8);
    assert_bitset_is(map, 0xffffffffffff00ff);

    bitset_set(map);
    assert_bitset_is(map, 0xffffffffffffffff);
    bitset_clear_range(map, 0, 0);
    assert_bitset_is(map, 0xffffffffffffffff);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_run_count)
{
    struct bitset *map = bitset_alloc(64, 1);
    uint64_t run;

    assert_bitset_is(map, 0x0000000000000000);

    run = bitset_run_count(map, 0, 64);
    ck_assert_int_eq(64, run);

    bitset_set_range(map, 0, 32);
    assert_bitset_is(map, 0x00000000ffffffff);

    run = bitset_run_count(map, 0, 64);
    ck_assert_int_eq(32, run);

    run = bitset_run_count(map, 0, 16);
    ck_assert_int_eq(16, run);

    run = bitset_run_count(map, 16, 64);
    ck_assert_int_eq(16, run);

    run = bitset_run_count(map, 31, 64);
    ck_assert_int_eq(1, run);

    run = bitset_run_count(map, 32, 64);
    ck_assert_int_eq(32, run);

    run = bitset_run_count(map, 32, 32);
    ck_assert_int_eq(32, run);

    run = bitset_run_count(map, 32, 16);
    ck_assert_int_eq(16, run);

    bitset_free(map);

    map = bitset_alloc(6400, 100);
    assert_bitset_is(map, 0x0000000000000000);

    run = bitset_run_count(map, 0, 6400);
    ck_assert_int_eq(6400, run);

    bitset_set_range(map, 0, 3200);

    run = bitset_run_count(map, 0, 6400);
    ck_assert_int_eq(3200, run);

    run = bitset_run_count(map, 1, 6400);
    ck_assert_int_eq(3199, run);

    run = bitset_run_count(map, 3200, 6400);
    ck_assert_int_eq(3200, run);

    run = bitset_run_count(map, 6500, 6400);
    ck_assert_int_eq(0, run);
    bitset_free(map);

    // Now do something large and representative
    map = bitset_alloc(53687091200, 4096);
    bitset_set(map);
    run = bitset_run_count(map, 0, 53687091200);
    ck_assert_int_eq(run, 53687091200);

    bitset_free(map);

}
END_TEST

START_TEST(test_bitset_set_range_doesnt_push_to_stream)
{
    struct bitset *map = bitset_alloc(64, 1);
    bitset_set_range(map, 0, 64);
    ck_assert_int_eq(0, bitset_stream_size(map));
    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_clear_range_doesnt_push_to_stream)
{
    struct bitset *map = bitset_alloc(64, 1);
    bitset_clear_range(map, 0, 64);
    ck_assert_int_eq(0, bitset_stream_size(map));
    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_enable_stream)
{
    struct bitset *map = bitset_alloc(64, 1);
    struct bitset_stream_entry result;
    memset(&result, 0, sizeof(result));

    bitset_enable_stream(map);

    ck_assert_int_eq(1, map->stream_enabled);

    bitset_stream_dequeue(map, &result);

    ck_assert_int_eq(BITSET_STREAM_ON, result.event);
    ck_assert_int_eq(0, result.from);
    ck_assert_int_eq(64, result.len);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_disable_stream)
{
    struct bitset *map = bitset_alloc(64, 1);
    struct bitset_stream_entry result;
    memset(&result, 0, sizeof(result));

    bitset_enable_stream(map);
    bitset_disable_stream(map);

    ck_assert_int_eq(0, map->stream_enabled);
    ck_assert_int_eq(2, bitset_stream_size(map));

    bitset_stream_dequeue(map, NULL);	// ON
    bitset_stream_dequeue(map, &result);	// OFF

    ck_assert_int_eq(BITSET_STREAM_OFF, result.event);
    ck_assert_int_eq(0, result.from);
    ck_assert_int_eq(64, result.len);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_stream_with_set_range)
{
    struct bitset *map = bitset_alloc(64, 1);
    struct bitset_stream_entry result;
    memset(&result, 0, sizeof(result));

    bitset_enable_stream(map);
    bitset_set_range(map, 0, 32);

    ck_assert_int_eq(2, bitset_stream_size(map));

    bitset_stream_dequeue(map, NULL);	// ON
    bitset_stream_dequeue(map, &result);	// SET

    ck_assert_int_eq(BITSET_STREAM_SET, result.event);
    ck_assert_int_eq(0, result.from);
    ck_assert_int_eq(32, result.len);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_stream_with_clear_range)
{
    struct bitset *map = bitset_alloc(64, 1);
    struct bitset_stream_entry result;
    memset(&result, 0, sizeof(result));

    bitset_enable_stream(map);
    bitset_clear_range(map, 0, 32);
    ck_assert_int_eq(2, bitset_stream_size(map));

    bitset_stream_dequeue(map, NULL);	// ON
    bitset_stream_dequeue(map, &result);	// UNSET

    ck_assert_int_eq(BITSET_STREAM_UNSET, result.event);
    ck_assert_int_eq(0, result.from);
    ck_assert_int_eq(32, result.len);

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_stream_size)
{
    struct bitset *map = bitset_alloc(64, 1);
    bitset_enable_stream(map);
    bitset_set_range(map, 0, 32);
    bitset_set_range(map, 16, 32);
    bitset_set_range(map, 7, 16);

    bitset_clear_range(map, 0, 32);
    bitset_clear_range(map, 16, 32);
    bitset_clear_range(map, 48, 16);
    bitset_disable_stream(map);

    ck_assert_int_eq(8, bitset_stream_size(map));

    bitset_free(map);
}
END_TEST

START_TEST(test_bitset_stream_queued_bytes)
{
    struct bitset *map = bitset_alloc(64, 1);
    bitset_enable_stream(map);
    bitset_set_range(map, 0, 32);
    bitset_set_range(map, 16, 32);
    bitset_set_range(map, 7, 16);

    bitset_clear_range(map, 0, 32);
    bitset_clear_range(map, 16, 32);
    bitset_clear_range(map, 48, 16);
    bitset_clear_range(map, 0, 2);
    bitset_disable_stream(map);

    ck_assert_int_eq(64,
		     bitset_stream_queued_bytes(map, BITSET_STREAM_ON));
    ck_assert_int_eq(80,
		     bitset_stream_queued_bytes(map, BITSET_STREAM_SET));
    ck_assert_int_eq(82,
		     bitset_stream_queued_bytes(map, BITSET_STREAM_UNSET));
    ck_assert_int_eq(64,
		     bitset_stream_queued_bytes(map, BITSET_STREAM_OFF));
    bitset_free(map);
}
END_TEST

Suite * bitset_suite(void)
{
    Suite *s = suite_create("bitset");

    TCase *tc_bit = tcase_create("bit");
    tcase_add_test(tc_bit, test_bit_set);
    tcase_add_test(tc_bit, test_bit_clear);
    tcase_add_test(tc_bit, test_bit_tests);
    tcase_add_test(tc_bit, test_bit_ranges);
    tcase_add_test(tc_bit, test_bit_runs);
    suite_add_tcase(s, tc_bit);

    TCase *tc_bitset = tcase_create("bitset");
    tcase_add_test(tc_bitset, test_bitset);
    tcase_add_test(tc_bitset, test_bitset_set);
    tcase_add_test(tc_bitset, test_bitset_clear);
    tcase_add_test(tc_bitset, test_bitset_run_count);
    tcase_add_test(tc_bitset, test_bitset_set_range);
    tcase_add_test(tc_bitset, test_bitset_clear_range);
    tcase_add_test(tc_bitset, test_bitset_set_range_doesnt_push_to_stream);
    tcase_add_test(tc_bitset,
		   test_bitset_clear_range_doesnt_push_to_stream);
    suite_add_tcase(s, tc_bitset);


    TCase *tc_bitset_stream = tcase_create("bitset_stream");
    tcase_add_test(tc_bitset_stream, test_bitset_enable_stream);
    tcase_add_test(tc_bitset_stream, test_bitset_disable_stream);
    tcase_add_test(tc_bitset_stream, test_bitset_stream_with_set_range);
    tcase_add_test(tc_bitset_stream, test_bitset_stream_with_clear_range);
    tcase_add_test(tc_bitset_stream, test_bitset_stream_size);
    tcase_add_test(tc_bitset_stream, test_bitset_stream_queued_bytes);
    suite_add_tcase(s, tc_bitset_stream);

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
