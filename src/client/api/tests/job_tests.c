/*
 * (C) Copyright 2020-2022 Intel Corporation.
 * (C) Copyright 2025 Google LLC
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

#include <daos/tests_lib.h>
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

int
setenv(const char *name, const char *value, int overwrite)
{
	if (strncmp(name, DEFAULT_JOBID_ENV, sizeof(DEFAULT_JOBID_ENV)) == 0) {
		if (!getenv_daos_jobid_return || overwrite != 0)
			getenv_daos_jobid_return = (char *)value;
	} else if (getenv_jobid_env_return &&
		   strncmp(name, getenv_jobid_env_return, MAX_ENV_NAME) == 0) {
		if (!getenv_jobid_return || overwrite != 0)
			getenv_jobid_return = (char *)value;
	} else {
		return -1;
	}
	return 0;
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
	getpid_pid               = 1000;
	uname_nodename           = "testhost";
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

	ret = craft_jobid(&default_jobid, uname_nodename, getpid_pid);
	assert_return_code(ret, 0);

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, DEFAULT_JOBID_ENV);

	/* Make sure we crafted a default jobid  */
	assert_string_equal(dc_jobid, default_jobid);

	if (default_jobid)
		free(default_jobid);
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

	getenv_jobid_env_return = "other-jobid-env";

	ret = craft_jobid(&default_jobid, uname_nodename, getpid_pid);
	assert_return_code(ret, 0);

	dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_string_equal(dc_jobid_env, getenv_jobid_env_return);

	/* Make sure we crafted a default jobid  */
	assert_string_equal(dc_jobid, default_jobid);

	if (default_jobid)
		free(default_jobid);
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
test_dc_job_init_with_default_jobid_and_env_unset(void **state)
{
	int   rc            = 0;
	char *default_jobid = "cool-default-jobid";

	rc = dc_set_default_jobid(default_jobid);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_string_equal(dc_jobid, default_jobid);

	dc_job_fini();
}

static void
test_dc_job_init_with_default_jobid_and_env_set(void **state)
{
	int   rc            = 0;
	char *exp_jobid     = NULL;
	char *default_jobid = "cool-default-jobid";

	getenv_daos_jobid_return = "test-jobid";
	D_STRNDUP(exp_jobid, getenv_daos_jobid_return, MAX_JOBID_LEN);

	rc = dc_set_default_jobid(default_jobid);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_string_equal(dc_jobid, exp_jobid);

	D_FREE(exp_jobid);
	dc_job_fini();
}

static void
test_dc_job_init_with_default_jobid_and_jobid_env_unset(void **state)
{
	int   rc            = 0;
	char *default_jobid = "cool-default-jobid";

	getenv_jobid_env_return = "other-jobid-env";

	rc = dc_set_default_jobid(default_jobid);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_string_equal(dc_jobid_env, getenv_jobid_env_return);

	assert_string_equal(dc_jobid, default_jobid);

	dc_job_fini();
}

static void
test_dc_job_init_with_default_jobid_and_jobid_env_set(void **state)
{
	int   rc            = 0;
	char *exp_jobid     = NULL;
	char *default_jobid = "cool-default-jobid";

	getenv_jobid_env_return = "other-jobid-env";
	getenv_jobid_return     = "test-jobid";
	D_STRNDUP(exp_jobid, getenv_jobid_return, MAX_JOBID_LEN);

	rc = dc_set_default_jobid(default_jobid);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_string_equal(dc_jobid_env, getenv_jobid_env_return);

	assert_string_equal(dc_jobid, exp_jobid);

	D_FREE(exp_jobid);
	dc_job_fini();
}

static void
test_dc_job_init_with_uname_fail(void **state)
{
	int ret = 0;

	uname_fail = 1;

	ret = dc_job_init();
	/* Make sure we checked the right environment variable */
	assert_rc_equal(ret, -DER_MISC);
}

static void
test_dc_set_default_jobid_null(void **state)
{
	int rc;

	rc = dc_set_default_jobid(NULL);
	assert_int_equal(rc, -DER_INVAL);
}

static void
test_dc_set_default_jobid_after_init(void **state)
{
	int rc;

	rc = dc_job_init();
	assert_return_code(rc, 0);

	rc = dc_set_default_jobid("some-jobid");
	assert_int_equal(rc, -DER_ALREADY);

	dc_job_fini();
}

static void
test_dc_jobid_is_default_crafted(void **state)
{
	int   rc;
	char *crafted_jobid = NULL;

	rc = craft_jobid(&crafted_jobid, uname_nodename, getpid_pid);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_true(dc_jobid_is_default(dc_jobid));
	assert_string_equal(dc_jobid, crafted_jobid);

	if (crafted_jobid)
		free(crafted_jobid);
	dc_job_fini();
}

static void
test_dc_jobid_is_default_from_env(void **state)
{
	int         rc;
	const char *env_jobid = "my-env-jobid";

	getenv_daos_jobid_return = (char *)env_jobid;

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_false(dc_jobid_is_default(dc_jobid));
	assert_string_equal(dc_jobid, env_jobid);

	dc_job_fini();
}

static void
test_dc_jobid_is_default_set(void **state)
{
	int         rc;
	const char *prog_default = "my-prog-default";

	rc = dc_set_default_jobid(prog_default);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_true(dc_jobid_is_default(dc_jobid));
	assert_string_equal(dc_jobid, prog_default);

	dc_job_fini();
}

static void
test_dc_jobid_is_default_set_with_env(void **state)
{
	int         rc;
	const char *prog_default = "my-prog-default";
	const char *env_jobid    = "my-env-jobid";

	getenv_daos_jobid_return = (char *)env_jobid;

	rc = dc_set_default_jobid(prog_default);
	assert_return_code(rc, 0);

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_false(dc_jobid_is_default(dc_jobid));
	assert_string_equal(dc_jobid, env_jobid);

	dc_job_fini();
}

static void
test_dc_jobid_is_default_other_string(void **state)
{
	int rc;

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_false(dc_jobid_is_default("some-other-id"));

	dc_job_fini();
}

static void
test_dc_jobid_is_default_null(void **state)
{
	int rc;

	rc = dc_job_init();
	assert_return_code(rc, 0);

	assert_false(dc_jobid_is_default(NULL));

	dc_job_fini();
}

/* Convenience macro for declaring unit tests in this suite */
#define JOB_UTEST(X) \
	cmocka_unit_test_setup_teardown(X, setup_job_mocks, \
			teardown_job_mocks)

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    JOB_UTEST(test_dc_job_init_no_env),
	    JOB_UTEST(test_dc_job_init_with_jobid),
	    JOB_UTEST(test_dc_job_init_with_jobid_env),
	    JOB_UTEST(test_dc_job_init_with_jobid_env_and_jobid),
	    JOB_UTEST(test_dc_job_init_with_default_jobid_and_env_unset),
	    JOB_UTEST(test_dc_job_init_with_default_jobid_and_env_set),
	    JOB_UTEST(test_dc_job_init_with_default_jobid_and_jobid_env_unset),
	    JOB_UTEST(test_dc_job_init_with_default_jobid_and_jobid_env_set),
	    JOB_UTEST(test_dc_job_init_with_uname_fail),
	    JOB_UTEST(test_dc_set_default_jobid_null),
	    JOB_UTEST(test_dc_set_default_jobid_after_init),
	    JOB_UTEST(test_dc_jobid_is_default_crafted),
	    JOB_UTEST(test_dc_jobid_is_default_from_env),
	    JOB_UTEST(test_dc_jobid_is_default_set),
	    JOB_UTEST(test_dc_jobid_is_default_set_with_env),
	    JOB_UTEST(test_dc_jobid_is_default_other_string),
	    JOB_UTEST(test_dc_jobid_is_default_null),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("client_job", tests, NULL, NULL);
}

#undef JOB_UTEST
