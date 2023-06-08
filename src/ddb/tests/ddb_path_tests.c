/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <ddb_tree_path.h>

#include "ddb_cmocka.h"
#include "ddb_test_driver.h"
#include "ddb_parse.h"
#include <daos/tests_lib.h>

/* These tests are to verify the parsing and printing of the vos path */
static struct ddb_ctx g_ctx = {.dc_io_ft.ddb_print_message = dvt_fake_print};

/*
 * This just verifies that the parsing succeeds. There are other tests that verify that parts
 * are parsed correctly
 */
static void
simple_path_parsing(void **state)
{
	struct dv_indexed_tree_path itp = {0};

	assert_success(itp_parse(NULL, &itp));
	assert_success(itp_parse("", &itp));
	assert_success(itp_parse("/", &itp));
	assert_success(itp_parse("/[0]", &itp));
	assert_success(itp_parse("/[0]/", &itp));
	assert_success(itp_parse("/[0]/[0]", &itp));
	assert_success(itp_parse("/[0]/[0]/", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]/", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]/[0]", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]/[0]/", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]/[0]/[0]", &itp));
	assert_success(itp_parse("/[0]/[0]/[0]/[0]/[0]/", &itp));

	/* Too many parts */
	assert_invalid(itp_parse("/[0]/[0]/[0]/[0]/[0]/[0]", &itp));
}

/* Test the safe string function which  */
static void
key_safe_str_tests(void **state)
{
	char buf[128] = {0};
	char small_buf[8] = {0};
#define assert_key_escaped(key_str, expected) do { \
	sprintf(buf, key_str); \
	assert_true(itp_key_safe_str(buf, ARRAY_SIZE(buf))); \
	assert_string_equal(expected, buf);\
} while (0)

	itp_key_safe_str(buf, (sizeof(buf) / sizeof((buf)[0])));
	/* shouldn't add anything to buf */
	assert_int_equal(0, strlen(buf));

	/*
	 * Escaping a forward slash only requires a single backslash ('\'). However, in the C string
	 * the backslash actually has to be escaped as well, hence the double backslash.
	 */
	assert_key_escaped("a", "a");
	assert_key_escaped("/", "\\/");
	assert_key_escaped("a/", "a\\/");
	assert_key_escaped("a/b/c/d/e/f", "a\\/b\\/c\\/d\\/e\\/f");
	assert_key_escaped("{", "\\{");
	assert_key_escaped("/{/}\\", "\\/\\{\\/\\}\\\\");

	/* When buf is too small for escape characters, the buf shouldn't change */
	sprintf(small_buf, "///////");
	assert_false(itp_key_safe_str(small_buf, ARRAY_SIZE(small_buf)));
	assert_string_equal(small_buf, "///////");
}

static void
key_printing_and_parsing_tests(void **state)
{
/*
 * These tests will parse the first argument, then print it. The printed
 * value will be compared to the second (expected) argument.
 */
#define assert_key_parsed_printed(parsed, printed) do {\
	union itp_part_type __v = {0}; \
	assert_true(ddb_parse_key(parsed, &__v.itp_key) > 0); \
	itp_print_part_key(&g_ctx, &__v); \
	assert_printed_exact(printed); \
	dvt_fake_print_reset(); \
	daos_iov_free(&__v.itp_key); \
} while (0)

	assert_key_parsed_printed("akey", "akey");
	assert_key_parsed_printed("akey{4}", "akey");
	assert_key_parsed_printed("akey{64}", "akey{64}");
	/* binary should take size as input, but doesn't need it. It will always print it however */
	assert_key_parsed_printed("{bin:0xabcdef1234}", "{bin(5):0xabcdef1234}");
	assert_key_parsed_printed("{bin(5):0xabcdef1234}", "{bin(5):0xabcdef1234}");

	/* Int types. Hex letters' case doesn't matter. Will always print as lower case */
	assert_key_parsed_printed("{uint64:0xABCDEF1234}", "{uint64:0xabcdef1234}");
	assert_key_parsed_printed("{uint32:0x12345678}", "{uint32:0x12345678}");
	assert_key_parsed_printed("{uint16:0x1234}", "{uint16:0x1234}");
	assert_key_parsed_printed("{uint8:0xAF}", "{uint8:0xaf}");

	/* Parsing doesn't handle too big of values yet, so will get truncated */
	assert_key_parsed_printed("{uint8:0xFFFAAA}", "{uint8:0xaa}");
	assert_key_parsed_printed("\\/", "\\/");
}

/* Test setting and printing the full path given the path parts structure */
static void
fully_set_and_print_path_parts(void **state)
{
	struct dv_indexed_tree_path	itp = {0};
	uuid_t				null_uuid = {0};

	dvt_fake_print_reset();

	/* Empty path */
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("/");
	dvt_fake_print_reset();

	/* shouldn't be able to set object before container */
	assert_false(itp_set_obj(&itp, g_oids[0], 3));
	/* Can't set a NULL container */
	assert_false(itp_set_cont(&itp, NULL, 1));
	assert_false(itp_set_cont(&itp, null_uuid, 1));

	/* Set container and print */
	assert_true(itp_set_cont(&itp, g_uuids[0], 1));
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("CONT: (/[1]) /12345678-1234-1234-1234-123456789001");
	dvt_fake_print_reset();

	/* Set object and print */
	assert_true(itp_set_obj(&itp, g_oids[0], 2));
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("OBJ: (/[1]/[2]) /12345678-1234-1234-1234-123456789001/"
			     "281479271743488.4294967296.0.0");
	dvt_fake_print_reset();

	/* Set dkey and print */
	assert_true(itp_set_dkey(&itp, &g_dkeys[0], 3));
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("DKEY: (/[1]/[2]/[3]) /12345678-1234-1234-1234-123456789001/"
			     "281479271743488.4294967296.0.0/"
			     "dkey-1");
	dvt_fake_print_reset();

	/* set akey and print */
	assert_true(itp_set_akey(&itp, &g_akeys[0], 4));
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("AKEY: (/[1]/[2]/[3]/[4]) /12345678-1234-1234-1234-123456789001/"
			     "281479271743488.4294967296.0.0/"
			     "dkey-1/akey-1");
	dvt_fake_print_reset();

	/* set recx and print */
	assert_true(itp_set_recx(&itp, &g_recxs[0], 5));
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact("RECX: (/[1]/[2]/[3]/[4]/[5]) /12345678-1234-1234-1234-123456789001/"
			     "281479271743488.4294967296.0.0/"
			     "dkey-1/akey-1/{9-18}");
	dvt_fake_print_reset();

	itp_free(&itp);
}

/* This shouldn't actually happen in production, but test just in case  */
static void
path_parts_partial_behavior(void **state)
{
	struct dv_indexed_tree_path itp = {0};

	itp_set_cont_idx(&itp, 1);
	/* missing container uuid */
	itp_print_full(&g_ctx, &itp);
	assert_printed_exact(INVALID_PATH);
	dvt_fake_print_reset();

	itp_set_cont_part_value(&itp, g_uuids[0]);
	itp_print_full(&g_ctx, &itp);
	assert_printed_not_equal(INVALID_PATH);
	dvt_fake_print_reset();
}

/*
 * These tests take a path structure and uses the ddb path printer functions to print the path
 * to a test buffer. Then it parses that buffer to a new path structure and compares to make sure
 * that the path printing and parsing is consistent.
 */
static void
parse_path_from_printed_path(void **state)
{
	struct dv_indexed_tree_path itp = {0};
	struct dv_indexed_tree_path itp_out = {0};

	/* Empty path is success */
	dvt_fake_print_reset();
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	itp_free(&itp_out);

	/* Container */
	itp_set_cont(&itp, g_uuids[0], 10);

	itp_print_indexes(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_int_equal(itp.itp_parts[PATH_PART_CONT].itp_part_idx,
			 itp_out.itp_parts[PATH_PART_CONT].itp_part_idx);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	itp_print_parts(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_uuid_equal(itp.itp_parts[PATH_PART_CONT].itp_part_value.itp_uuid,
			  itp_out.itp_parts[PATH_PART_CONT].itp_part_value.itp_uuid);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	 /* object */
	itp_set_obj(&itp, g_oids[0], 1);
	itp_print_indexes(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_int_equal(itp.itp_parts[PATH_PART_OBJ].itp_part_idx,
			 itp_out.itp_parts[PATH_PART_OBJ].itp_part_idx);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	itp_print_parts(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));

	assert_uoid_equal(itp.itp_parts[PATH_PART_OBJ].itp_part_value.itp_oid,
			  itp_out.itp_parts[PATH_PART_OBJ].itp_part_value.itp_oid);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	/* dkey */
	itp_set_dkey(&itp, &g_dkeys[0], 2);
	itp_print_indexes(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_int_equal(itp.itp_parts[PATH_PART_DKEY].itp_part_idx,
			 itp_out.itp_parts[PATH_PART_DKEY].itp_part_idx);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	itp_print_parts(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));

	assert_key_equal(itp.itp_parts[PATH_PART_DKEY].itp_part_value.itp_key,
			 itp_out.itp_parts[PATH_PART_DKEY].itp_part_value.itp_key);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	/* akey */
	itp_set_akey(&itp, &g_akeys[0], 2);
	itp_print_indexes(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_int_equal(itp.itp_parts[PATH_PART_AKEY].itp_part_idx,
			 itp_out.itp_parts[PATH_PART_AKEY].itp_part_idx);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	itp_print_parts(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_key_equal(itp.itp_parts[PATH_PART_AKEY].itp_part_value.itp_key,
			 itp_out.itp_parts[PATH_PART_AKEY].itp_part_value.itp_key);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	/* recx */
	itp_set_recx(&itp, &g_recxs[0], 2);
	itp_print_indexes(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));
	assert_int_equal(itp.itp_parts[PATH_PART_RECX].itp_part_idx,
			 itp_out.itp_parts[PATH_PART_RECX].itp_part_idx);
	dvt_fake_print_reset();
	itp_free(&itp_out);

	itp_print_parts(&g_ctx, &itp);
	assert_success(itp_parse(dvt_fake_print_buffer, &itp_out));

	assert_recx_equal(itp.itp_parts[PATH_PART_RECX].itp_part_value.itp_recx,
			  itp_out.itp_parts[PATH_PART_RECX].itp_part_value.itp_recx);
	dvt_fake_print_reset();

	itp_free(&itp);
	itp_free(&itp_out);
}

#define assert_invalid_path(path, err_code) \
do { \
	struct dv_indexed_tree_path __itp = {0}; \
	assert_rc_equal(-err_code, itp_parse(path, &__itp)); \
	itp_free(&__itp); \
} while (0)

#define assert_path_parsed_equals(path, parsed_path) \
do {                                                 \
	struct dv_indexed_tree_path __itp = {0}; \
	itp_parse(path, &__itp); \
	itp_print_parts(&g_ctx, &__itp); \
	assert_printed_exact(parsed_path); \
	dvt_fake_print_reset(); \
	itp_free(&__itp);\
} while (0)

/*
 * These tests take a string path, parse it, then print the parsed path and compare the output
 * to the original. This verifies that the printing and parsing is consistent.
 */
static void
string_to_path_to_string(void **state)
{
	dvt_fake_print_reset();
	assert_path_parsed_equals("", "/");
	assert_path_parsed_equals("/12345678-1234-1234-1234-123456789012/",
				  "/12345678-1234-1234-1234-123456789012");

	assert_path_parsed_equals("/12345678-1234-1234-1234-123456789012/1.2.3.4/",
				  "/12345678-1234-1234-1234-123456789012/1.2.3.4");

	assert_path_parsed_equals("/12345678-1234-1234-1234-123456789012/1.2.3.4/key/",
				  "/12345678-1234-1234-1234-123456789012/1.2.3.4/key");

	assert_path_parsed_equals("/12345678-1234-1234-1234-123456789012/1.2.3.4/key{64}/",
				  "/12345678-1234-1234-1234-123456789012/1.2.3.4/key{64}");

	assert_path_parsed_equals("/12345678-1234-1234-1234-123456789012/1.2.3.4/\\/",
				  "/12345678-1234-1234-1234-123456789012/1.2.3.4/\\/");
}

/* Verify that the correct path specific return code is returned */
static void
invalid_paths_return_error(void **state)
{
	assert_invalid_path("12345678", DDBER_INVALID_CONT);
	assert_invalid_path("/12345678-1234-1234-1234-12345678900",
			    DDBER_INVALID_CONT);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0",
			    DDBER_INVALID_OBJ);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.",
			    DDBER_INVALID_OBJ);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0./dkey",
			    DDBER_INVALID_OBJ);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0../",
			    DDBER_INVALID_OBJ);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0.0.0/",
			    DDBER_INVALID_OBJ);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/invalid",
			    DDBER_INVALID_RECX);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/{-1}",
			    DDBER_INVALID_RECX);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/(0-1)",
			    DDBER_INVALID_RECX);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/"
			    "{0-1-2}",
			    DDBER_INVALID_RECX);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/"
			    "{0 1}",
			    DDBER_INVALID_RECX);
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234.0.0/dkey/akey/"
			    "{0->1}",
			    DDBER_INVALID_RECX);
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
#define TEST(x) {#x, x, NULL, NULL}
int
ddb_path_tests_run()
{
	static const struct CMUnitTest tests[] = {
	    TEST(simple_path_parsing),
	    TEST(key_safe_str_tests),
	    TEST(key_printing_and_parsing_tests),
	    TEST(fully_set_and_print_path_parts),
	    TEST(path_parts_partial_behavior),
	    TEST(parse_path_from_printed_path),
	    TEST(string_to_path_to_string),
	    TEST(invalid_paths_return_error),
	};
	return cmocka_run_group_tests_name("DDB Path Parsing Tests", tests, NULL, NULL);
}
