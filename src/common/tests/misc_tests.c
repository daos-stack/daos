/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(tests)

#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h> /** For cmocka.h */
#include <cmocka.h>
#include <daos/common.h>
#include <daos/tests_lib.h>

static void test_sgl_get_bytes_with_single_iov(void **state)
{
	uint8_t			*buf = NULL;
	size_t			 len;
	d_sg_list_t		 sgl;
	struct daos_sgl_idx	 idx = {0};

	dts_sgl_init_with_strings(&sgl, 1, "abcd");

	/** Get the first byte of the sgl */
	daos_sgl_get_bytes(&sgl, false, &idx, 1, &buf, &len);
	assert_int_equal(0, idx.iov_idx);
	assert_int_equal(1, idx.iov_offset);
	assert_int_equal('a', *(char *)buf);
	assert_int_equal(1, len);

	/** Get the next two bytes */
	daos_sgl_get_bytes(&sgl, false, &idx, 2, &buf, &len);
	assert_int_equal(0, idx.iov_idx);
	assert_int_equal(3, idx.iov_offset);
	assert_int_equal('b', *(char *)buf);
	assert_int_equal('c', *((char *)buf + 1));
	assert_int_equal(2, len);

	d_sgl_fini(&sgl, true);
}

static void test_sgl_get_bytes_with_multiple_iovs(void **state)
{
	uint8_t			*buf = NULL;
	size_t			 len;
	d_sg_list_t		 sgl;
	struct daos_sgl_idx	 idx = {0};
	bool			 end;

	dts_sgl_init_with_strings(&sgl, 2, "a", "b");

	end = daos_sgl_get_bytes(&sgl, false, &idx, 3, &buf, &len);
	assert_int_equal('a', *(char *)buf);
	/** even though 3 requested, only got 2 because can only process a
	 * single iov at a time.
	 */
	assert_int_equal(2, len);
	assert_int_equal(1, idx.iov_idx);
	assert_int_equal(0, idx.iov_offset);
	assert_false(end);

	end = daos_sgl_get_bytes(&sgl, false, &idx, 2, &buf, &len);
	assert_int_equal(2, len);
	assert_int_equal('b', *(char *)buf);
	/** idx points to after the sgl when done */
	assert_int_equal(2, idx.iov_idx);
	assert_int_equal(0, idx.iov_offset);
	assert_true(end);

	d_sgl_fini(&sgl, true);
}

static void test_sgl_get_bytes_trying_to_exceed_len(void **state)
{
	uint8_t			*buf = NULL;
	size_t			 len;
	d_sg_list_t		 sgl;
	struct daos_sgl_idx	 idx = {0};
	bool			 end;
	size_t			 sgl_len;

	dts_sgl_init_with_strings(&sgl, 1, "a");
	sgl_len = sgl.sg_iovs[0].iov_len;
	end = daos_sgl_get_bytes(&sgl, false, &idx, sgl_len + 1, &buf, &len);

	assert_int_equal(sgl_len, len); /** len is still only sgl_len */
	assert_true(end); /** yep, still the end */

	d_sgl_fini(&sgl, true);
}

/** Dummy functions for testing the daos_sgl_processor */
static int sgl_cb_call_count;
#define SGL_CB_BUFF_SIZE 64
static char sgl_cb_buf[SGL_CB_BUFF_SIZE];
static char *sgl_cb_buf_idx = sgl_cb_buf;
int dummy_sgl_cb(uint8_t *buf, size_t len, void *args)
{
	strncpy(sgl_cb_buf_idx, (char *)buf, len);
	sgl_cb_buf_idx += len;
	sgl_cb_call_count++;
	return 0;
}

static void test_completely_process_sgl(void **state)
{
	d_sg_list_t		sgl;
	struct daos_sgl_idx	idx = {0};

	memset(sgl_cb_buf, 0, SGL_CB_BUFF_SIZE);
	sgl_cb_buf_idx = sgl_cb_buf;
	sgl_cb_call_count = 0;

	dts_sgl_init_with_strings(&sgl, 2, "a", "bc");

	daos_sgl_processor(&sgl, false, &idx, 6, dummy_sgl_cb, NULL);

	assert_int_equal(2, sgl_cb_call_count); /** one for each iov in sgl */
	sgl_cb_buf[1] = '_'; /** Remove '\0' */
	assert_string_equal("a_bc", sgl_cb_buf);

	d_sgl_fini(&sgl, true);
}

static void test_process_sgl_span_iov_with_diff_requests(void **state)
{
	d_sg_list_t		sgl;
	struct daos_sgl_idx	idx = {0};

	sgl_cb_call_count = 0;
	memset(sgl_cb_buf, 0, SGL_CB_BUFF_SIZE);
	sgl_cb_buf_idx = sgl_cb_buf;

	dts_sgl_init_with_strings(&sgl, 2, "abc", "def");

	daos_sgl_processor(&sgl, false, &idx, 2, dummy_sgl_cb, NULL);
	assert_int_equal(1, sgl_cb_call_count);

	sgl_cb_call_count = 0; /** reset */

	daos_sgl_processor(&sgl, false, &idx, 6, dummy_sgl_cb, NULL);

	/** callback called twice. Once for first iov (wasn't
	 * 'consumed' with initial processor request), then another
	 * for last iov
	 */
	assert_int_equal(2, sgl_cb_call_count);
	/** idx should be at end */
	assert_int_equal(2, idx.iov_idx);
	assert_int_equal(0, idx.iov_offset);

	d_sgl_fini(&sgl, true);
}

static const struct CMUnitTest tests[] = {
	{"SGL01: Processing an SGL",
		test_sgl_get_bytes_with_single_iov,           NULL, NULL},
	{"SGL02: Processing a more complicated SGL",
		test_sgl_get_bytes_with_multiple_iovs,        NULL, NULL},
	{"SGL02.5: Exceed SGL length",
		test_sgl_get_bytes_trying_to_exceed_len,      NULL, NULL},
	{"SGL03: More SGL processing",
		test_completely_process_sgl,                  NULL, NULL},
	{"SGL04: SGL processing, spanning iovs",
		test_process_sgl_span_iov_with_diff_requests, NULL, NULL},
};

int
misc_tests_run()
{
	return cmocka_run_group_tests_name("Misc Tests", tests, NULL, NULL);
}
