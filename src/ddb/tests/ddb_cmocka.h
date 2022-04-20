/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_CMOCKA_H
#define DAOS_DDB_CMOCKA_H
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>

#define assert_uuid_equal(a, b) \
	do { \
		char str_a[DAOS_UUID_STR_SIZE]; \
		char str_b[DAOS_UUID_STR_SIZE]; \
		uuid_unparse(a, str_a); \
		uuid_unparse(b, str_b); \
		assert_string_equal(str_a, str_b); \
	} while (0)
#define assert_uuid_not_equal(a, b) \
	do { \
		char str_a[DAOS_UUID_STR_SIZE]; \
		char str_b[DAOS_UUID_STR_SIZE]; \
		uuid_unparse(a, str_a); \
		uuid_unparse(b, str_b); \
		assert_string_not_equal(str_a, str_b); \
	} while (0)
#define assert_oid_equal(a, b) \
	do { \
		assert_int_equal((a).hi, (b).hi); \
		assert_int_equal((a).lo, (b).lo); \
	} while (0)

#define assert_oid_not_equal(a, b) assert_true(a.hi != b.hi || a.lo != b.lo)
#define assert_key_equal(a, b) \
	do { \
		assert_int_equal(a.iov_len, b.iov_len); \
		assert_int_equal(a.iov_buf_len, b.iov_buf_len); \
		assert_memory_equal(a.iov_buf, b.iov_buf, a.iov_len); \
	} while (0)

#define assert_key_not_equal(a, b) \
	do { \
		if (a.iov_len == b.iov_len && a.iov_buf_len == b.iov_buf_len) \
			assert_memory_not_equal(a.iov_buf, b.iov_buf, a.iov_len); \
	} while (0)

#define assert_string_contains(str, substr) \
	do { \
		if (strstr(str, substr) == NULL) \
			fail_msg("'%s' not found in '%s'", substr, str); \
	} while (0)


#endif /* DAOS_DDB_CMOCKA_H */
