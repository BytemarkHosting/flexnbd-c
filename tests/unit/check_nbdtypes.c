#include <check.h>

#include "nbdtypes.h"

START_TEST(test_init_passwd)
{
    struct nbd_init_raw init_raw;
    struct nbd_init init;

    memcpy(init_raw.passwd, INIT_PASSWD, 8);

    nbd_r2h_init(&init_raw, &init);
    memset(init_raw.passwd, 0, 8);
    nbd_h2r_init(&init, &init_raw);

    fail_unless(memcmp(init.passwd, INIT_PASSWD, 8) == 0,
		"The password was not copied.");
    fail_unless(memcmp(init_raw.passwd, INIT_PASSWD, 8) == 0,
		"The password was not copied back.");
}
END_TEST

START_TEST(test_init_magic)
{
    struct nbd_init_raw init_raw;
    struct nbd_init init;

    init_raw.magic = 12345;
    nbd_r2h_init(&init_raw, &init);
    fail_unless(be64toh(12345) == init.magic, "Magic was not converted.");

    init.magic = 67890;
    nbd_h2r_init(&init, &init_raw);
    fail_unless(htobe64(67890) == init_raw.magic,
		"Magic was not converted back.");
}
END_TEST

START_TEST(test_init_size)
{
    struct nbd_init_raw init_raw;
    struct nbd_init init;

    init_raw.size = 12345;
    nbd_r2h_init(&init_raw, &init);
    fail_unless(be64toh(12345) == init.size, "Size was not converted.");

    init.size = 67890;
    nbd_h2r_init(&init, &init_raw);
    fail_unless(htobe64(67890) == init_raw.size,
		"Size was not converted back.");
}
END_TEST

START_TEST(test_request_magic)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    request_raw.magic = 12345;
    nbd_r2h_request(&request_raw, &request);
    fail_unless(be32toh(12345) == request.magic,
		"Magic was not converted.");

    request.magic = 67890;
    nbd_h2r_request(&request, &request_raw);
    fail_unless(htobe32(67890) == request_raw.magic,
		"Magic was not converted back.");
}
END_TEST

START_TEST(test_request_type)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    request_raw.type = 123;
    nbd_r2h_request(&request_raw, &request);
    fail_unless(be16toh(123) == request.type, "Type was not converted.");

    request.type = 234;
    nbd_h2r_request(&request, &request_raw);
    fail_unless(htobe16(234) == request_raw.type,
		"Type was not converted back.");
}
END_TEST

START_TEST(test_request_flags)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    request_raw.flags = 123;
    nbd_r2h_request(&request_raw, &request);
    fail_unless(be16toh(123) == request.flags,
		"Flags were not converted.");

    request.flags = 234;
    nbd_h2r_request(&request, &request_raw);
    fail_unless(htobe16(234) == request_raw.flags,
		"Flags were not converted back.");
}
END_TEST

START_TEST(test_request_handle)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    memcpy(request_raw.handle.b, "MYHANDLE", 8);

    nbd_r2h_request(&request_raw, &request);
    request_raw.handle.w = 0;
    nbd_h2r_request(&request, &request_raw);

    fail_unless(memcmp(request.handle.b, "MYHANDLE", 8) == 0,
		"The handle was not copied.");
    fail_unless(memcmp(request_raw.handle.b, "MYHANDLE", 8) == 0,
		"The handle was not copied back.");
}
END_TEST

START_TEST(test_request_from)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    request_raw.from = 12345;
    nbd_r2h_request(&request_raw, &request);
    fail_unless(be64toh(12345) == request.from, "From was not converted.");

    request.from = 67890;
    nbd_h2r_request(&request, &request_raw);
    fail_unless(htobe64(67890) == request_raw.from,
		"From was not converted back.");
}
END_TEST

START_TEST(test_request_len)
{
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    request_raw.len = 12345;
    nbd_r2h_request(&request_raw, &request);
    fail_unless(be32toh(12345) == request.len, "Type was not converted.");

    request.len = 67890;
    nbd_h2r_request(&request, &request_raw);
    fail_unless(htobe32(67890) == request_raw.len,
		"Type was not converted back.");
}
END_TEST

START_TEST(test_reply_magic)
{
    struct nbd_reply_raw reply_raw;
    struct nbd_reply reply;

    reply_raw.magic = 12345;
    nbd_r2h_reply(&reply_raw, &reply);
    fail_unless(be32toh(12345) == reply.magic, "Magic was not converted.");

    reply.magic = 67890;
    nbd_h2r_reply(&reply, &reply_raw);
    fail_unless(htobe32(67890) == reply_raw.magic,
		"Magic was not converted back.");
}
END_TEST

START_TEST(test_reply_error)
{
    struct nbd_reply_raw reply_raw;
    struct nbd_reply reply;

    reply_raw.error = 12345;
    nbd_r2h_reply(&reply_raw, &reply);
    fail_unless(be32toh(12345) == reply.error, "Error was not converted.");

    reply.error = 67890;
    nbd_h2r_reply(&reply, &reply_raw);
    fail_unless(htobe32(67890) == reply_raw.error,
		"Error was not converted back.");
}
END_TEST

START_TEST(test_reply_handle)
{
    struct nbd_reply_raw reply_raw;
    struct nbd_reply reply;

    memcpy(reply_raw.handle.b, "MYHANDLE", 8);

    nbd_r2h_reply(&reply_raw, &reply);
    reply_raw.handle.w = 0;
    nbd_h2r_reply(&reply, &reply_raw);

    fail_unless(memcmp(reply.handle.b, "MYHANDLE", 8) == 0,
		"The handle was not copied.");
    fail_unless(memcmp(reply_raw.handle.b, "MYHANDLE", 8) == 0,
		"The handle was not copied back.");
}
END_TEST

START_TEST(test_convert_from)
{
    /* Check that we can correctly pull numbers out of an
     * nbd_request_raw */
    struct nbd_request_raw request_raw;
    struct nbd_request request;

    uint64_t target = 0x8000000000000000;

    /* this is stored big-endian */
    request_raw.from = htobe64(target);

    /* We expect this to convert big-endian to the host format */
    nbd_r2h_request(&request_raw, &request);

    fail_unless(target == request.from, "from was wrong");
}
END_TEST

Suite * nbdtypes_suite(void)
{
    Suite *s = suite_create("nbdtypes");
    TCase *tc_init = tcase_create("nbd_init");
    TCase *tc_request = tcase_create("nbd_request");
    TCase *tc_reply = tcase_create("nbd_reply");

    tcase_add_test(tc_init, test_init_passwd);
    tcase_add_test(tc_init, test_init_magic);
    tcase_add_test(tc_init, test_init_size);
    tcase_add_test(tc_request, test_request_magic);
    tcase_add_test(tc_request, test_request_type);
    tcase_add_test(tc_request, test_request_flags);
    tcase_add_test(tc_request, test_request_handle);
    tcase_add_test(tc_request, test_request_from);
    tcase_add_test(tc_request, test_request_len);
    tcase_add_test(tc_request, test_convert_from);
    tcase_add_test(tc_reply, test_reply_magic);
    tcase_add_test(tc_reply, test_reply_error);
    tcase_add_test(tc_reply, test_reply_handle);

    suite_add_tcase(s, tc_init);
    suite_add_tcase(s, tc_request);
    suite_add_tcase(s, tc_reply);


    return s;
}


int main(void)
{
    int number_failed;
    Suite *s = nbdtypes_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
