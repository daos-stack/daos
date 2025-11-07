/**
 * (C) Copyright 2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_CMOCKA_H
#define DAOS_DDB_CMOCKA_H
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdarg.h>
#include <regex.h>
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

#define assert_uoid_equal(a, b) \
	do { \
		assert_oid_equal((a).id_pub, (b).id_pub); \
		assert_int_equal((a).id_shard, (b).id_shard); \
		assert_int_equal((a).id_layout_ver, (b).id_layout_ver); \
	} while (0)

#define assert_oid_not_equal(a, b) assert_true(a.hi != b.hi || a.lo != b.lo)

#define assert_key_equal(a, b) \
	do { \
		assert_int_equal(a.iov_len, b.iov_len); \
		assert_memory_equal(a.iov_buf, b.iov_buf, a.iov_len); \
	} while (0)

#define assert_key_not_equal(a, b) \
	do { \
		if (a.iov_len == b.iov_len && a.iov_buf_len == b.iov_buf_len) \
			assert_memory_not_equal(a.iov_buf, b.iov_buf, a.iov_len); \
	} while (0)

#define assert_recx_equal(a, b) \
	do { \
		assert_int_equal((a).rx_nr, (b).rx_nr); \
		assert_int_equal((a).rx_idx, (b).rx_idx); \
	} while (0)

#define assert_string_contains(str, substr) \
	do { \
		if (strstr(str, substr) == NULL) \
			fail_msg("'%s' not found in '%s'", substr, str); \
	} while (0)

#define assert_invalid(x) assert_rc_equal(-DER_INVAL, (x))
#define assert_nonexist(x) assert_rc_equal(-DER_NONEXIST, (x))

/* XXX Needs of testing _buf to avoid false positive error from gcc: '%s' directive argument is null
 * [-Werror=format-overflow=] */
#define assert_regex_match(str, regex)                                                             \
	do {                                                                                       \
		char   *_str_dup;                                                                  \
		char   *_token;                                                                    \
		regex_t _preg;                                                                     \
		int     _rc = regcomp(&_preg, regex, REG_NOSUB | REG_EXTENDED);                    \
		if (_rc != 0) {                                                                    \
			char  *_buf;                                                               \
			size_t _buf_size = regerror(_rc, NULL, NULL, 0);                           \
			D_ALLOC_ARRAY(_buf, _buf_size);                                            \
			assert_non_null(_buf);                                                     \
			regerror(_rc, NULL, _buf, _buf_size);                                      \
			if (_buf)                                                                  \
				print_error("ERROR: invalid regex '%s': %s\n", regex, _buf);       \
			D_FREE(_buf);                                                              \
			fail();                                                                    \
		}                                                                                  \
		D_STRNDUP(_str_dup, str, strlen(str));                                             \
		assert_non_null(_str_dup);                                                         \
		_token = strtok(_str_dup, "\n");                                                   \
		while (_token != NULL) {                                                           \
			_rc = regexec(&_preg, _token, 0, NULL, 0);                                 \
			if (_rc == 0)                                                              \
				break;                                                             \
			if (_rc != REG_NOMATCH) {                                                  \
				char  *_buf;                                                       \
				size_t _buf_size = regerror(_rc, &_preg, NULL, 0);                 \
				D_ALLOC_ARRAY(_buf, _buf_size);                                    \
				assert_non_null(_buf);                                             \
				regerror(_rc, &_preg, _buf, _buf_size);                            \
				if (_buf)                                                          \
					print_error("ERROR: invalid regex '%s': %s\n", regex,      \
						    _buf);                                         \
				D_FREE(_buf);                                                      \
				regfree(&_preg);                                                   \
				D_FREE(_str_dup);                                                  \
				fail();                                                            \
			}                                                                          \
			_token = strtok(NULL, "\n");                                               \
		}                                                                                  \
		D_FREE(_str_dup);                                                                  \
		regfree(&_preg);                                                                   \
		if (_rc == REG_NOMATCH)                                                            \
			fail_msg("'%s' regex not matched in '%s'", regex, str);                    \
	} while (0)

#define assert_nl_equal(str, cnt)                                                                  \
	do {                                                                                       \
		int _i;                                                                            \
		int _nl_cnt;                                                                       \
		_nl_cnt = 0;                                                                       \
		for (_i = 0; _i <= strlen(str); _i++) {                                            \
			if (str[_i] == '\n')                                                       \
				_nl_cnt++;                                                         \
		}                                                                                  \
		assert_int_equal(_nl_cnt++, cnt);                                                  \
	} while (0)

#endif /* DAOS_DDB_CMOCKA_H */
