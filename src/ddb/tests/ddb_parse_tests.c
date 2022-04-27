/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <gurt/debug.h>
#include <ddb_common.h>
#include <ddb_parse.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

static int
fake_print(const char *fmt, ...)
{
	return 0;
}

#define assert_parsed_words2(str, count, ...) \
	__assert_parsed_words2(str, count, (char *[])__VA_ARGS__)
static void
__assert_parsed_words2(const char *str, int count, char **expected_words)
{
	struct argv_parsed	parse_args = {0};
	int			i;

	assert_success(ddb_str2argv_create(str, &parse_args));
	assert_int_equal(count, parse_args.ap_argc);

	for (i = 0; i < parse_args.ap_argc; i++)
		assert_string_equal(parse_args.ap_argv[i], expected_words[i]);

	ddb_str2argv_free(&parse_args);
}

static void
assert_parsed_fail(const char *str)
{
	struct argv_parsed	parse_args = {0};
	int			rc;

	rc = ddb_str2argv_create(str, &parse_args);
	ddb_str2argv_free(&parse_args);
	assert_rc_equal(-DER_INVAL, rc);
}

/*
 * -----------------------------------------------
 * Test implementations
 * -----------------------------------------------
 */

static void
string_to_argv_tests(void **state)
{
	assert_parsed_words2("one", 1, { "one" });
	assert_parsed_words2("one two", 2, {"one", "two"});
	assert_parsed_words2("one two three four five", 5, {"one", "two", "three", "four", "five"});
	assert_parsed_words2("one 'two two two'", 2, {"one", "two two two"});
	assert_parsed_words2("one 'two two two' three", 3, {"one", "two two two", "three"});
	assert_parsed_words2("one \"two two two\" three", 3, {"one", "two two two", "three"});

	assert_parsed_fail("one>");
	assert_parsed_fail("one<");
	assert_parsed_fail("'one");
	assert_parsed_fail(" \"one");
	assert_parsed_fail("one \"two");
}

#define assert_invalid_program_args(argc, ...) \
	assert_rc_equal(-DER_INVAL, _assert_invalid_program_args(argc, ((char*[])__VA_ARGS__)))
static int
_assert_invalid_program_args(uint32_t argc, char **argv)
{
	struct program_args	pa;
	struct ddb_ctx		ctx = {
		.dc_io_ft.ddb_print_message = fake_print,
		.dc_io_ft.ddb_print_error = fake_print
	};

	return ddb_parse_program_args(&ctx, argc, argv, &pa);
}

#define assert_program_args(expected_program_args, argc, ...) \
	assert_success(_assert_program_args(&expected_program_args, argc, ((char*[])__VA_ARGS__)))
static int
_assert_program_args(struct program_args *expected_pa, uint32_t argc, char **argv)
{
	struct program_args	pa = {0};
	int			rc;
	struct ddb_ctx		ctx = {
		.dc_io_ft.ddb_print_message = fake_print,
		.dc_io_ft.ddb_print_error = fake_print
	};

	rc = ddb_parse_program_args(&ctx, argc, argv, &pa);
	if (rc != 0)
		return rc;


	if (expected_pa->pa_r_cmd_run != NULL && pa.pa_r_cmd_run != NULL &&
	    strcmp(expected_pa->pa_r_cmd_run, pa.pa_r_cmd_run) != 0) {
		print_error("ERROR: %s != %s\n", expected_pa->pa_r_cmd_run, pa.pa_r_cmd_run);
		return -DER_INVAL;
	}

	if (expected_pa->pa_cmd_file != NULL &&  pa.pa_cmd_file != NULL &&
	    strcmp(expected_pa->pa_cmd_file, pa.pa_cmd_file) != 0) {
		print_error("ERROR: %s != %s\n", expected_pa->pa_cmd_file, pa.pa_cmd_file);
		return -DER_INVAL;
	}

	return 0;
}

static void
parse_args_tests(void **state)
{
	struct program_args pa = {0};

	assert_invalid_program_args(2, {"", "-z"});
	assert_invalid_program_args(3, {"", "command1", "command2"});
	pa.pa_r_cmd_run = "command";
	assert_program_args(pa, 3, {"", "-R", "command"});
	pa.pa_r_cmd_run = "";

	pa.pa_cmd_file = "path";
	assert_program_args(pa, 3, {"", "-f", "path"});
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

#define assert_path(path, expected) \
do { \
	struct dv_tree_path_builder __vt = {0}; \
	daos_handle_t poh = {0}; \
	assert_success(ddb_vtp_init(poh, path, &__vt)); \
	assert_vtp_eq(expected, __vt); \
	ddb_vtp_fini(&__vt); \
} while (0)


/** easily setup an iov and allocate */
static void
iov_alloc(d_iov_t *iov, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	assert_non_null(iov->iov_buf);
	iov->iov_buf_len = iov->iov_len = len;
}

static void
iov_alloc_str(d_iov_t *iov, const char *str)
{
	iov_alloc(iov, strlen(str));
	memcpy(iov->iov_buf, str, strlen(str));
}

static void
vos_path_parse_tests(void **state)
{
	struct dv_tree_path_builder expected_vt = {0};

	ddb_vos_tree_path_setup(&expected_vt);

	/* empty paths are valid */
	assert_path("", expected_vt);

	/* first part must be a valid uuid */
	assert_invalid_path("12345678");

	uuid_parse("12345678-1234-1234-1234-123456789012", expected_vt.vtp_path.vtp_cont);

	/* handle just container */
	assert_path("12345678-1234-1234-1234-123456789012", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012", expected_vt);
	assert_path("12345678-1234-1234-1234-123456789012/", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/", expected_vt);

	/* handle container and object id */
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.");
	expected_vt.vtp_path.vtp_oid.id_pub.lo = 1234;
	expected_vt.vtp_path.vtp_oid.id_pub.hi = 4321;

	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234", expected_vt);

	/* handle dkey */
	iov_alloc_str(&expected_vt.vtp_path.vtp_dkey, "dkey");
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey");
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'/", expected_vt);

	iov_alloc_str(&expected_vt.vtp_path.vtp_akey, "akey");
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'/akey");
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'/'akey'", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'/'akey'/", expected_vt);

	expected_vt.vtp_path.vtp_recx.rx_idx = 1;
	expected_vt.vtp_path.vtp_recx.rx_nr = 5;
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/'dkey'/'akey'/{1-6}",
		    expected_vt);

	daos_iov_free(&expected_vt.vtp_path.vtp_dkey);
	daos_iov_free(&expected_vt.vtp_path.vtp_akey);
}

static void
vos_path_parse_and_print_tests(void **state)
{
	struct dv_tree_path_builder	 vt = {0};
	daos_handle_t			 poh = {0};
	struct ddb_ctx			 ctx = {0};
	char				*path;

	path = "/12435678-1234-1234-1234-124356789012/1234.4321.0/'akey'/'dkey'";

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;

	assert_success(ddb_vtp_init(poh, path, &vt));

	vtp_print(&ctx, &vt.vtp_path, false);

	assert_string_equal(path, dvt_fake_print_buffer);

	ddb_vtp_fini(&vt);
}

static void
parse_idx_tests(void **state)
{
	struct dv_tree_path_builder expected_vt = {0};

	ddb_vos_tree_path_setup(&expected_vt);

	expected_vt.vtp_cont_idx = 1;
	assert_path("[1]", expected_vt);

	expected_vt.vtp_cont_idx = 11;
	assert_path("[11]", expected_vt);

	expected_vt.vtp_cont_idx = 1234;
	assert_path("[1234]", expected_vt);

	expected_vt.vtp_cont_idx = 1;
	expected_vt.vtp_oid_idx = 2;
	expected_vt.vtp_dkey_idx = 3;
	expected_vt.vtp_akey_idx = 4;

	expected_vt.vtp_recx_idx = 5;
	assert_path("[1]/[2]/[3]/[4]/[5]", expected_vt);
}

static void
has_parts_tests(void **state)
{
	struct dv_tree_path vtp = {0};

	assert_false(dv_has_cont(&vtp));
	uuid_copy(vtp.vtp_cont, g_uuids[0]);
	assert_true(dv_has_cont(&vtp));

	assert_false(dv_has_obj(&vtp));
	vtp.vtp_oid = g_oids[0];
	assert_true(dv_has_obj(&vtp));

	assert_false(dv_has_dkey(&vtp));
	vtp.vtp_dkey = g_dkeys[0];
	assert_true(dv_has_dkey(&vtp));

	assert_false(dv_has_akey(&vtp));
	vtp.vtp_akey = g_akeys[0];
	assert_true(dv_has_akey(&vtp));
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
		TEST(string_to_argv_tests),
		TEST(parse_args_tests),
		TEST(vos_path_parse_tests),
		TEST(vos_path_parse_and_print_tests),
		TEST(parse_idx_tests),
		TEST(has_parts_tests)
	};
	return cmocka_run_group_tests_name("DDB helper parsing function tests", tests,
					   NULL, NULL);
}
