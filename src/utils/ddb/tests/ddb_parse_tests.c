/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <gurt/debug.h>
#include <daos_srv/vos.h>
#include <ddb_common.h>
#include <ddb_parse.h>
#include "daos_errno.h"
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

static int
fake_print(const char *fmt, ...)
{
	return 0;
}

/*
 * -----------------------------------------------
 * Test implementations
 * -----------------------------------------------
 */

#define assert_invalid_f_path(path, parts) assert_invalid(vos_path_parse(path, &parts))
#define assert_f_path(path, parts) assert_success(vos_path_parse(path, &parts))

static void
vos_file_parts_tests(void **state)
{
	struct vos_file_parts parts = {0};
	uuid_t expected_uuid;

	uuid_parse("12345678-1234-1234-1234-123456789012", expected_uuid);

	assert_invalid_f_path("", parts);
	assert_invalid_f_path("/mnt/daos", parts);
	assert_invalid_f_path("/mnt/daos/12345678-1234-1234-1234-123456789012", parts);

	assert_f_path("/mnt/daos/12345678-1234-1234-1234-123456789012/vos-1", parts);

	assert_string_equal("/mnt/daos", parts.vf_db_path);
	assert_uuid_equal(expected_uuid, parts.vf_pool_uuid);
	assert_string_equal("vos-1", parts.vf_vos_file);
	assert_int_equal(1, parts.vf_target_idx);
}

#define assert_vtp_eq(a, b) \
do { \
	assert_uuid_equal(a.vtp_path.vtp_cont, b.vtp_path.vtp_cont); \
	assert_int_equal(a.vtp_cont_idx, b.vtp_cont_idx); \
	assert_int_equal(a.vtp_oid_idx, b.vtp_oid_idx); \
	assert_int_equal(a.vtp_dkey_idx, b.vtp_dkey_idx); \
	assert_int_equal(a.vtp_akey_idx, b.vtp_akey_idx); \
	assert_int_equal(a.vtp_recx_idx, b.vtp_recx_idx); \
	assert_int_equal(a.vtp_path.vtp_oid.id_pub.hi, b.vtp_path.vtp_oid.id_pub.hi); \
	assert_int_equal(a.vtp_path.vtp_oid.id_pub.lo, b.vtp_path.vtp_oid.id_pub.lo); \
	assert_int_equal(a.vtp_path.vtp_dkey.iov_len, b.vtp_path.vtp_dkey.iov_len); \
	if (a.vtp_path.vtp_dkey.iov_len > 0) \
		assert_memory_equal(a.vtp_path.vtp_dkey.iov_buf, b.vtp_path.vtp_dkey.iov_buf, \
					a.vtp_path.vtp_dkey.iov_len); \
	assert_int_equal(a.vtp_path.vtp_akey.iov_len, b.vtp_path.vtp_akey.iov_len); \
	if (a.vtp_path.vtp_akey.iov_len > 0) \
		assert_memory_equal(a.vtp_path.vtp_akey.iov_buf, b.vtp_path.vtp_akey.iov_buf, \
					a.vtp_path.vtp_akey.iov_len); \
	} while (0)

#define assert_invalid_path(path) \
do { \
	struct dv_tree_path_builder __vt = {0}; \
		daos_handle_t poh = {0}; \
		assert_rc_equal(-DER_INVAL, ddb_vtp_init(poh, path, &__vt)); \
} while (0)

#define assert_invalid_parse_dtx_id(str) \
	do { \
		struct dtx_id __dtx_id = {0}; \
		assert_invalid(ddb_parse_dtx_id(str, &__dtx_id)); \
	} while (0)

static void
parse_dtx_id_tests(void **state)
{
	struct dtx_id id;
	uuid_t uuid;

	assert_invalid_parse_dtx_id(NULL);
	assert_invalid_parse_dtx_id("");
	assert_invalid_parse_dtx_id("garbage.more_garbage");
	assert_invalid_parse_dtx_id("12345678-1234-1243-1243-124356789012.garbage");
	assert_invalid_parse_dtx_id("garbage.123456890");

	assert_success(ddb_parse_dtx_id("12345678-1234-1243-1243-124356789012.123456890", &id));
	uuid_parse("12345678-1234-1243-1243-124356789012", uuid);
	assert_uuid_equal(uuid, id.dti_uuid);
	assert_int_equal(0x123456890, id.dti_hlc);
}

#define assert_parsed_key(str, e) do {\
	daos_key_t __key = {0}; \
	assert_int_equal(strlen(str), ddb_parse_key(str, &__key)); \
	assert_key_equal(e, __key);  \
	daos_iov_free(&__key); \
} while (0)

#define set_expected_str(key, str) do { \
	sprintf(key.iov_buf, str); \
	d_iov_set(&key, key.iov_buf, strlen(str)); \
} while (0)

#define set_expected_str_len(key, str, len) do { \
	memset(key.iov_buf, 0, len); \
	sprintf(key.iov_buf, str); \
	d_iov_set(&key, key.iov_buf, len); \
} while (0)

#define set_expected(key, val) d_iov_set(&key, &val, sizeof(val))
#define set_expected_len(key, val, len) d_iov_set(&key, &val, len)

static void
keys_are_parsed_correctly(void **state)
{
	daos_key_t	key = {0};
	daos_key_t	expected_key = {0};
	char		buf[128] = {0};

	d_iov_set(&expected_key, buf, ARRAY_SIZE(buf));

	/*
	 * Invalid key path parts
	 */
	/* key should not be an empty string or NULL */
	assert_invalid(ddb_parse_key("", &key));
	assert_invalid(ddb_parse_key(NULL, &key));
	/* invalid syntax */
	assert_invalid(ddb_parse_key("{}", &key));
	assert_invalid(ddb_parse_key("{", &key));
	assert_invalid(ddb_parse_key("}", &key));
	assert_invalid(ddb_parse_key("string_key{{64}", &key));
	assert_invalid(ddb_parse_key("string_key{1{64}", &key));
	assert_invalid(ddb_parse_key("string_key{64}}", &key));
	assert_invalid(ddb_parse_key("string_key{64", &key));
	assert_invalid(ddb_parse_key("string_key}64", &key));
	/* must actually have a string value before size, or a type */
	assert_invalid(ddb_parse_key("{64}", &key));
	/* invalid size */
	assert_invalid(ddb_parse_key("string_key{a}", &key));
	/* shouldn't have anything after the size */
	assert_invalid(ddb_parse_key("string_key{5}more", &key));
	/* length is too small */
	assert_invalid(ddb_parse_key("string_key{0}", &key));
	assert_invalid(ddb_parse_key("string_key{3}", &key));
	/* invalid type */
	assert_invalid(ddb_parse_key("{uint:3}", &key));
	/* value is too big for type */

	/* String keys ... some with length specified */
	/* Note that length of key does NOT include a NULL terminator */
	set_expected_str(expected_key, "string_key");
	assert_parsed_key("string_key", expected_key);

	set_expected_str_len(expected_key, "string_key", 64);
	assert_parsed_key("string_key{64}", expected_key);

	/* able to escape curly brace */
	set_expected_str_len(expected_key, "string_{key", 64);
	assert_parsed_key("string_\\{key{64}", expected_key);

	set_expected_str(expected_key, "string_{key");
	assert_parsed_key("string_\\{key", expected_key);

	set_expected_str_len(expected_key, "{{{{", 64);
	assert_parsed_key("\\{\\{\\{\\{{64}", expected_key);

	set_expected_str(expected_key, "{{{{");
	assert_parsed_key("\\{\\{\\{\\{", expected_key);

	set_expected_str(expected_key, "}}}}");
	assert_parsed_key("\\}\\}\\}\\}", expected_key);

	set_expected_str(expected_key, "string_value{24}");
	assert_parsed_key("string_value\\{24\\}", expected_key);

	/* Number strings */
	uint8_t key_val_8 = 9;

	set_expected(expected_key, key_val_8);
	assert_parsed_key("{uint8:9}", expected_key);

	uint16_t key_val_16 = 17;

	set_expected(expected_key, key_val_16);
	assert_parsed_key("{uint16:17}", expected_key);

	uint32_t key_val_32 = 33;

	set_expected(expected_key, key_val_32);
	assert_parsed_key("{uint32:33}", expected_key);

	uint64_t key_val_64 = 99999999;

	set_expected(expected_key, key_val_64);
	assert_parsed_key("{uint64:99999999}", expected_key);

	uint64_t key_val_hex = 0x12345678;

	set_expected(expected_key, key_val_hex);
	assert_parsed_key("{uint64:0x12345678}", expected_key);

	uint8_t bin_buf[10] = {0};

	memset(bin_buf, 0xAB, ARRAY_SIZE(bin_buf));
	set_expected_len(expected_key, bin_buf, ARRAY_SIZE(bin_buf));
	assert_parsed_key("{bin:0xABABABABABABABABABAB}", expected_key);
	assert_parsed_key("{bin(5):0xABABABABABABABABABAB}", expected_key);

	/* Currently don't check for value that's too big */
	assert_true(ddb_parse_key("{uint8:3000000000}", &key) > 0);
	daos_iov_free(&key);
}

static void
pool_flags_tests(void **state)
{
	struct ddb_ctx ctx = {.dc_io_ft.ddb_print_message = fake_print,
			      .dc_io_ft.ddb_print_error   = fake_print};
	uint64_t       compat_flags, incompat_flags;
	uint64_t       expected_flags;
	int            rc;

	expected_flags = VOS_POOL_COMPAT_FLAG_IMMUTABLE | VOS_POOL_COMPAT_FLAG_SKIP_START;
	rc = ddb_feature_string2flags(&ctx, "immutable,skip_start", &compat_flags, &incompat_flags);
	assert_success(rc);
	assert(compat_flags == expected_flags);
	expected_flags = 0;
	if (incompat_flags != expected_flags) {
		print_error("ERROR: %lu != %lu\n", incompat_flags, expected_flags);
		assert_success(-DER_INVAL);
	}

	rc = ddb_feature_string2flags(&ctx, "immutablexxx", &compat_flags, &incompat_flags);
	assert_rc_equal(-DER_INVAL, rc);
}

static void
date2cmt_time_tests(void **state)
{
	uint64_t cmt_time;
	int      rc;

	cmt_time = -1;
	rc       = ddb_date2cmt_time(NULL, &cmt_time);
	assert_rc_equal(rc, -DER_INVAL);
	assert_int_equal(cmt_time, -1);

	rc = ddb_date2cmt_time("1970-01-01 00:00:00", NULL);
	assert_rc_equal(rc, -DER_INVAL);
	assert_int_equal(cmt_time, -1);

	rc = ddb_date2cmt_time(NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	assert_int_equal(cmt_time, -1);

	rc = ddb_date2cmt_time("foo", NULL);
	assert_rc_equal(rc, -DER_INVAL);
	assert_int_equal(cmt_time, -1);

	rc = ddb_date2cmt_time("0000-00-00 00:00:00", &cmt_time);
	assert_rc_equal(rc, -DER_INVAL);
	assert_int_equal(cmt_time, -1);

	rc = ddb_date2cmt_time("1970-01-01 00:00:00", &cmt_time);
	assert_success(rc);
	assert_int_equal(rc, 0);
	assert_int_equal(cmt_time, 0ull);

	rc = ddb_date2cmt_time("1970-01-01 00:01:00", &cmt_time);
	assert_success(rc);
	assert_int_equal(rc, 0);
	assert_int_equal(cmt_time, 60ull);
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
#define TEST(x) {#x, x, NULL, NULL}
int
ddb_parse_tests_run()
{
	static const struct CMUnitTest tests[] = {
	    TEST(vos_file_parts_tests), TEST(parse_dtx_id_tests),  TEST(keys_are_parsed_correctly),
	    TEST(pool_flags_tests),     TEST(date2cmt_time_tests),
	};
	return cmocka_run_group_tests_name("DDB helper parsing function tests", tests,
					   NULL, NULL);
}
