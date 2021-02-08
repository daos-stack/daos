/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the job API for the client lib
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/job.h>
#include <string.h>
#include <sys/utsname.h>


/*
 * Mocks
 */

static char *getenv_daos_jobid_return; /* value returned for DAOS_JOBID */
static char *getenv_jobid_env_return; /* value returned for DAOS_JOBID_ENV */
static char *getenv_jobid_return; /* value stored in location DAOS_JOBID_ENV */
char *getenv(const char *name)
{
	if (strncmp(name, DEFAULT_JOBID_ENV, sizeof(DEFAULT_JOBID_ENV)) == 0) {
		return getenv_daos_jobid_return;
	} else if (strncmp(name, JOBID_ENV, sizeof(JOBID_ENV)) == 0) {
		return getenv_jobid_env_return;
	} else if (getenv_jobid_env_return &&
		strncmp(name, getenv_jobid_env_return, MAX_ENV_NAME) == 0) {
		return getenv_jobid_return;
	} else {
		return NULL;
	}
}

static pid_t getpid_pid;
pid_t getpid(void)
{
	return getpid_pid;
}

static int uname_fail;
static char *uname_nodename;
int uname(struct utsname *buf)
{
	if (uname_fail) {
		errno = EFAULT;
		return -1;
	}

	strncpy(buf->nodename, uname_nodename, _UTSNAME_LENGTH - 1);
	buf->nodename[_UTSNAME_LENGTH - 1] = '\0';
	return 0;
}

/*
 * Unit test setup and teardown
 */

static int
setup_job_mocks(void **state)
{
	/* Initialize mock values to something sane */
	getenv_daos_jobid_return = NULL;
	getenv_jobid_env_return = NULL;
	getenv_jobid_return = NULL;
	getpid_pid = 0;
	uname_nodename = NULL;
	uname_fail = 0;
	return 0;
}

static int
teardown_job_mocks(void **state)
{
	return 0;
}

static int
craft_jobid(char **jobid, char *nodename, pid_t pid)
{
	int   ret = 0;
	char *tmp_jobid;

	assert_non_null(jobid);

	ret = asprintf(&tmp_jobid, "%s-%d", nodename, pid);
	assert_int_not_equal(ret, -1);

	*jobid = tmp_jobid;
	return 0;
}

/*
 * Client lib job function tests
 */
static void
test_dc_job_init_no_env(void **state)
{
	int	 ret = 0;
	char	*default_jobid = NULL;

	uname_nodename = "testhost";
	getpid_pid = 1000;

	ret = craft_jobid(&default_jobid, uname_nodename, getpid_pid);
	assert_return_code(ret, 0);

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, DEFAULT_JOBID_ENV);

	/* Make sure we crafted a default jobid  */
	assert_string_equal(dc_jobid, default_jobid);

	dc_job_fini();
}

static void
test_dc_job_init_with_jobid(void **state)
{
	getenv_daos_jobid_return = "test-jobid";

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, DEFAULT_JOBID_ENV);

	/* Make sure we get the jobid in DAOS_JOBID  */
	assert_string_equal(dc_jobid, getenv_daos_jobid_return);

	dc_job_fini();
}

static void
test_dc_job_init_with_jobid_env(void **state)
{
	int	 ret = 0;
	char	*default_jobid = NULL;

	uname_nodename = "testhost";
	getpid_pid = 1000;
	getenv_jobid_env_return = "other-jobid-env";

	ret = craft_jobid(&default_jobid, uname_nodename, getpid_pid);
	assert_return_code(ret, 0);

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, getenv_jobid_env_return);

	/* Make sure we crafted a default jobid  */
	assert_string_equal(dc_jobid, default_jobid);

	dc_job_fini();
}

static void
test_dc_job_init_with_jobid_env_and_jobid(void **state)
{
	getenv_jobid_env_return = "other-jobid-env";
	getenv_jobid_return = "test-jobid";

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, getenv_jobid_env_return);

	/* Make sure we used the jobid in other-jobid-env */
	assert_string_equal(dc_jobid, getenv_jobid_return);

	dc_job_fini();
}

static void
test_dc_job_init_with_uname_fail(void **state)
{
	int ret = 0;

	uname_fail = 1;

	ret = dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_int_equal(ret, -DER_MISC);
}

/* Convenience macro for declaring unit tests in this suite */
#define JOB_UTEST(X) \
	cmocka_unit_test_setup_teardown(X, setup_job_mocks, \
			teardown_job_mocks)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		JOB_UTEST(
			test_dc_job_init_no_env),
		JOB_UTEST(
			test_dc_job_init_with_jobid),
		JOB_UTEST(
			test_dc_job_init_with_jobid_env),
		JOB_UTEST(
			test_dc_job_init_with_jobid_env_and_jobid),
		JOB_UTEST(
			test_dc_job_init_with_uname_fail),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef JOB_UTEST
