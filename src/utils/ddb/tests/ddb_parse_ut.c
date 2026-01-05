/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>
#include <uuid/uuid.h>
#include <errno.h>
#include <regex.h>

#include <daos/tests_lib.h>

#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_cmocka.h"

/*
 * -----------------------------------------------
 * Mock implementations
 * -----------------------------------------------
 */

#define MOCKED_POOL_UUID_STR "12345678-1234-1234-1234-123456789012"

static bool mock_regcomp    = false;
static bool mock_uuid_parse = false;
static bool mock_strtoull   = false;

extern int
__real_regcomp(regex_t *restrict preg, const char *restrict regex, int cflags);

int
__wrap_regcomp(regex_t *restrict preg, const char *restrict regex, int cflags)
{
	if (mock_regcomp)
		return mock();
	return __real_regcomp(preg, regex, cflags);
}

extern int
__real_uuid_parse(const char *uuid_str, uuid_t uuid);

int
__wrap_uuid_parse(const char *uuid_str, uuid_t uuid)
{
	if (mock_uuid_parse)
		return mock();
	return __real_uuid_parse(uuid_str, uuid);
}

extern int
__real_strtoull(const char *nptr, char **endptr, int base);

unsigned long long
__wrap_strtoull(const char *nptr, char **endptr, int base)
{
	if (mock_strtoull) {
		errno = mock();
		return mock();
	}
	return __real_strtoull(nptr, endptr, base);
}

/*
 * -----------------------------------------------
 * Test implementations
 * -----------------------------------------------
 */

#define MOCK_SETUP(x)                                                                              \
	static int vos_file_parse_test_crit_##x##_setup(void **unused)                             \
	{                                                                                          \
		mock_##x = true;                                                                   \
		return 0;                                                                          \
	}

#define MOCK_TEARDOWN(x)                                                                           \
	static int vos_file_parse_test_crit_##x##_teardown(void **unused)                          \
	{                                                                                          \
		mock_##x = false;                                                                  \
		return 0;                                                                          \
	}

MOCK_SETUP(regcomp)
MOCK_TEARDOWN(regcomp)
static void
vos_file_parse_test_crit_regcomp(void **state)
{
	struct vos_file_parts parts = {0};
	int                   rc;

	/* Testing regcomp failure */
	will_return(__wrap_regcomp, REG_ESPACE);
	rc = vos_path_parse(MOCKED_POOL_UUID_STR "/vos-0", &parts);
	assert_rc_equal(rc, -DER_INVAL);
}

MOCK_SETUP(uuid_parse)
MOCK_TEARDOWN(uuid_parse)
static void
vos_file_parse_test_crit_uuid_parse(void **state)
{
	struct vos_file_parts parts = {0};
	int                   rc;

	/* Testing uuid_parse failure */
	will_return(__wrap_uuid_parse, -1);
	rc = vos_path_parse(MOCKED_POOL_UUID_STR "/vos-0", &parts);
	assert_rc_equal(rc, -DER_INVAL);
}

MOCK_SETUP(strtoull)
MOCK_TEARDOWN(strtoull)
static void
vos_file_parse_test_crit_strtoull(void **state)
{
	struct vos_file_parts parts = {0};
	int                   rc;

	/* Testing strtoull failure */
	will_return(__wrap_strtoull, ERANGE);
	will_return(__wrap_strtoull, ULLONG_MAX);
	rc = vos_path_parse(MOCKED_POOL_UUID_STR "/vos-0", &parts);
	assert_rc_equal(rc, -DER_INVAL);
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
#define TEST(x)                                                                                    \
	{                                                                                          \
		#x, x, x##_setup, x##_teardown                                                     \
	}

int
ddb_parse_ut_run()
{
	static const struct CMUnitTest tests[] = {TEST(vos_file_parse_test_crit_regcomp),
						  TEST(vos_file_parse_test_crit_uuid_parse),
						  TEST(vos_file_parse_test_crit_strtoull)};
	return cmocka_run_group_tests_name("DDB helper parsing function tests", tests, NULL, NULL);
}
