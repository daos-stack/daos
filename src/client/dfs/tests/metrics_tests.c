/*
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/tests_lib.h>
#include <daos_fs.h>
#include <daos/metrics.h>
#include <daos/job.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <sys/utsname.h>
#include <uuid/uuid.h>
#include "../dfs_internal.h"

/* Global variable used to toggle client metrics. */
bool daos_client_metric;
/* Global variable for process name */
#define TEST_PROC_NAME "test_proc"
char *program_invocation_name = TEST_PROC_NAME;
/* Global variable for job id */
char *dc_jobid;

/* System mocks */
#define TEST_PID 1234
pid_t
getpid(void)
{
	return TEST_PID;
}

#define TEST_TIME 1234567890
time_t
time(time_t *t)
{
	if (t != NULL)
		*t = TEST_TIME;
	return TEST_TIME;
}

struct tm *
gmtime(const time_t *timer)
{
	static struct tm mock_tm;

	/* For failure case */
	if (mock_type(int) == 1)
		return NULL;

	/* For success case */
	mock_tm.tm_year = 109; /* 2009 - 1900 */
	mock_tm.tm_mon  = 1;   /* February */
	mock_tm.tm_mday = 13;
	mock_tm.tm_hour = 23;
	return &mock_tm;
}

#define TEST_HOSTNAME "test-hostname"
int
uname(struct utsname *buf)
{
	strncpy(buf->nodename, TEST_HOSTNAME, _UTSNAME_NODENAME_LENGTH);
	/* Ensure null termination */
	buf->nodename[_UTSNAME_NODENAME_LENGTH - 1] = '\0';

	return 0;
}

/* Mock for dc_pool_hdl2uuid() */
int
dc_pool_hdl2uuid(daos_handle_t poh, uuid_t *hdl_uuid, uuid_t *pool_uuid)
{
	if (pool_uuid != NULL)
		uuid_generate(*pool_uuid);
	return mock_type(int);
}

/* Mock for dc_cont_hdl2uuid() */
int
dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *cont_uuid)
{
	if (cont_uuid != NULL)
		uuid_generate(*cont_uuid);
	return mock_type(int);
}

/* Mock for dc_jobid_is_default() */
bool
dc_jobid_is_default(const char *jobid)
{
	return mock_type(bool);
}

/* from daos/job.c, simplified */
int
dc_set_default_jobid(const char *jobid)
{
	D_FREE(dc_jobid);
	D_ASPRINTF(dc_jobid, "%s", jobid);
	if (dc_jobid == NULL)
		return -DER_NOMEM;

	return 0;
}

/* Mocks for telemetry functions */
int
write_tm_csv(const char *tm_pool, const char *tm_cont, const char *csv_file_dir,
	     const char *csv_file_name, const char *csv_buf, size_t csv_buf_sz)
{
	check_expected(tm_pool);
	check_expected(tm_cont);

	return mock_type(int);
}

/* Flexible mock for daos_cont_get_attr() */
int
daos_cont_get_attr(daos_handle_t coh, int n, const char *const names[], void *const buffs[],
		   size_t sizes[], daos_event_t *ev)
{
	const char *pool_val = mock_ptr_type(const char *);
	const char *cont_val = mock_ptr_type(const char *);
	int         rc       = mock_type(int);

	if (rc != 0)
		return rc;

	for (int i = 0; i < n; i++) {
		if (strcmp(names[i], DAOS_CLIENT_METRICS_DUMP_POOL_ATTR) == 0) {
			if (buffs == NULL) { /* Size query */
				sizes[i] = (pool_val == NULL) ? 0 : strlen(pool_val);
			} else if (pool_val != NULL) { /* Value query */
				strncpy(buffs[i], pool_val, sizes[i]);
			}
		} else if (strcmp(names[i], DAOS_CLIENT_METRICS_DUMP_CONT_ATTR) == 0) {
			if (buffs == NULL) { /* Size query */
				sizes[i] = (cont_val == NULL) ? 0 : strlen(cont_val);
			} else if (cont_val != NULL) { /* Value query */
				strncpy(buffs[i], cont_val, sizes[i]);
			}
		} else if (strcmp(names[i], DAOS_CLIENT_METRICS_DUMP_DIR_ATTR) == 0) {
			/* For now, we don't test the dir attribute */
			if (buffs == NULL)
				sizes[i] = 0;
		}
	}
	return 0;
}

/*
 * ======================================================================
 * Test State and Setup/Teardown
 * ======================================================================
 */

struct test_state {
	dfs_t *dfs;
};

static int
setup(void **state)
{
	struct test_state *ts;

	D_ALLOC_PTR(ts);
	if (ts == NULL)
		return -1;

	D_ALLOC_PTR(ts->dfs);
	if (ts->dfs == NULL) {
		D_FREE(ts);
		return -1;
	}

	*state = ts;
	return 0;
}

static int
teardown(void **state)
{
	struct test_state *ts = *state;

	d_tm_fini();
	D_FREE(ts->dfs->metrics);
	D_FREE(ts->dfs);
	D_FREE(ts);
	D_FREE(dc_jobid);
	*state = NULL;
	return 0;
}

/*
 * ======================================================================
 * Test Cases
 * ======================================================================
 */

static void
test_metrics_enabled(void **state)
{
	struct test_state *ts = *state;

	/* Not enabled if metrics struct is NULL */
	ts->dfs->metrics = NULL;
	assert_false(dfs_metrics_enabled(ts->dfs));

	/* Enabled if metrics struct is allocated */
	D_ALLOC_PTR(ts->dfs->metrics);
	assert_non_null(ts->dfs->metrics);
	assert_true(dfs_metrics_enabled(ts->dfs));
}

static void
test_should_init_global_flag(void **state)
{
	struct test_state *ts = *state;

	/* Should init if the global flag is set */
	daos_client_metric = true;
	assert_true(dfs_metrics_should_init(ts->dfs));
	daos_client_metric = false;
}

static void
test_should_init_cont_attrs(void **state)
{
	struct test_state *ts = *state;

	daos_client_metric = false;

	/* Mock daos_cont_get_attr to return attributes (size query) */
	will_return(daos_cont_get_attr, "pool-label");
	will_return(daos_cont_get_attr, "cont-label");
	will_return(daos_cont_get_attr, 0);
	/* Mock daos_cont_get_attr to return attributes (value query) */
	will_return(daos_cont_get_attr, "pool-label");
	will_return(daos_cont_get_attr, "cont-label");
	will_return(daos_cont_get_attr, 0);

	assert_true(dfs_metrics_should_init(ts->dfs));
}

static void
test_should_not_init(void **state)
{
	struct test_state *ts = *state;

	daos_client_metric = false;

	/* Mock daos_cont_get_attr to return no attributes */
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, -DER_NONEXIST);

	assert_false(dfs_metrics_should_init(ts->dfs));
}

static void
test_init_success(void **state)
{
	struct test_state *ts = *state;

	will_return(dc_pool_hdl2uuid, 0);
	will_return(dc_cont_hdl2uuid, 0);

	dfs_metrics_init(ts->dfs);

	assert_non_null(ts->dfs->metrics);
}

static void
test_init_pool_uuid_fails(void **state)
{
	struct test_state *ts = *state;

	will_return(dc_pool_hdl2uuid, -DER_INVAL);

	dfs_metrics_init(ts->dfs);

	assert_null(ts->dfs->metrics);
}

static void
test_init_cont_uuid_fails(void **state)
{
	struct test_state *ts = *state;

	will_return(dc_pool_hdl2uuid, 0);
	will_return(dc_cont_hdl2uuid, -DER_INVAL);

	dfs_metrics_init(ts->dfs);

	assert_null(ts->dfs->metrics);
}

static void
test_fini_no_metrics(void **state)
{
	struct test_state *ts = *state;

	ts->dfs->metrics = NULL;
	/* Should not crash and do nothing */
	dfs_metrics_fini(ts->dfs);
}

static void
test_fini_no_dump_attrs(void **state)
{
	struct test_state *ts = *state;

	D_ALLOC_PTR(ts->dfs->metrics);
	assert_non_null(ts->dfs->metrics);

	/* Mock daos_cont_get_attr to return no attributes */
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, -DER_NONEXIST);

	/* dump_tm_container should NOT be called */
	dfs_metrics_fini(ts->dfs);

	/* The metrics struct should be freed */
	assert_null(ts->dfs->metrics);
}

#define TEST_POOL "pool1"
#define TEST_CONT "cont1"

static void
test_fini_with_dump_attrs_success(void **state)
{
	struct test_state *ts = *state;

	will_return(dc_pool_hdl2uuid, 0);
	will_return(dc_cont_hdl2uuid, 0);

	/* Mock daos_cont_get_attr to return attributes (size query) */
	will_return(daos_cont_get_attr, TEST_POOL);
	will_return(daos_cont_get_attr, TEST_CONT);
	will_return(daos_cont_get_attr, 0);
	/* Mock daos_cont_get_attr to return attributes (value query) */
	will_return(daos_cont_get_attr, TEST_POOL);
	will_return(daos_cont_get_attr, TEST_CONT);
	will_return(daos_cont_get_attr, 0);

	will_return(dc_jobid_is_default, true);
	will_return(gmtime, 0);

	expect_string(write_tm_csv, tm_pool, "pool1");
	expect_string(write_tm_csv, tm_cont, "cont1");
	will_return(write_tm_csv, 0);

	dfs_metrics_init(ts->dfs);
	assert_non_null(ts->dfs->metrics);

	dfs_metrics_fini(ts->dfs);

	assert_null(ts->dfs->metrics);
}

static void
test_fini_with_dump_attrs_fail_dump(void **state)
{
	struct test_state *ts = *state;

	will_return(dc_pool_hdl2uuid, 0);
	will_return(dc_cont_hdl2uuid, 0);

	/* Mock daos_cont_get_attr to return attributes (size query) */
	will_return(daos_cont_get_attr, TEST_POOL);
	will_return(daos_cont_get_attr, TEST_CONT);
	will_return(daos_cont_get_attr, 0);
	/* Mock daos_cont_get_attr to return attributes (value query) */
	will_return(daos_cont_get_attr, TEST_POOL);
	will_return(daos_cont_get_attr, TEST_CONT);
	will_return(daos_cont_get_attr, 0);

	will_return(dc_jobid_is_default, true);
	will_return(gmtime, 0);

	expect_string(write_tm_csv, tm_pool, "pool1");
	expect_string(write_tm_csv, tm_cont, "cont1");
	will_return(write_tm_csv, -DER_MISC);

	dfs_metrics_init(ts->dfs);
	assert_non_null(ts->dfs->metrics);
	dfs_metrics_fini(ts->dfs);

	assert_null(ts->dfs->metrics);
}

static void
test_fini_read_attrs_fails(void **state)
{
	struct test_state *ts = *state;

	D_ALLOC_PTR(ts->dfs->metrics);
	assert_non_null(ts->dfs->metrics);

	/* Mock daos_cont_get_attr to return an error */
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, NULL);
	will_return(daos_cont_get_attr, -DER_INVAL);

	dfs_metrics_fini(ts->dfs);

	assert_null(ts->dfs->metrics);
}

/*
 * ======================================================================
 * csv_file_path tests
 * ======================================================================
 */

static void
test_csv_file_path_default_jobid(void **state)
{
	char *file_dir  = NULL;
	char *file_name = NULL;
	char  expected_dir[PATH_MAX];
	char  expected_name[PATH_MAX];
	int   rc;

	/* Mock dc_jobid_is_default to return true */
	will_return(dc_jobid_is_default, true);
	will_return(gmtime, 0);

	rc = csv_file_path(TEST_PID, NULL, &file_dir, &file_name);
	assert_int_equal(rc, 0);
	assert_non_null(file_dir);
	assert_non_null(file_name);

	snprintf(expected_dir, sizeof(expected_dir), "/2009/02/13/23/proc/%s",
		 program_invocation_name);
	snprintf(expected_name, sizeof(expected_name), "%ld-%s-%d.csv", (long)TEST_TIME,
		 TEST_HOSTNAME, TEST_PID);

	assert_string_equal(file_dir, expected_dir);
	assert_string_equal(file_name, expected_name);

	D_FREE(file_dir);
	D_FREE(file_name);
}

static void
test_csv_file_path_custom_jobid_with_root(void **state)
{
	char       *file_dir  = NULL;
	char       *file_name = NULL;
	char        expected_dir[PATH_MAX];
	char        expected_name[PATH_MAX];
	const char *root_dir     = "/tmp/metrics";
	const char *custom_jobid = "my-custom-job";
	int         rc;

	/* Set custom job id */
	dc_set_default_jobid(custom_jobid);

	/* Mock dc_jobid_is_default to return false */
	will_return(dc_jobid_is_default, false);
	will_return(gmtime, 0);

	rc = csv_file_path(TEST_PID, root_dir, &file_dir, &file_name);
	assert_int_equal(rc, 0);
	assert_non_null(file_dir);
	assert_non_null(file_name);

	snprintf(expected_dir, sizeof(expected_dir), "%s/2009/02/13/23/job/%s/%s", root_dir,
		 custom_jobid, program_invocation_name);
	snprintf(expected_name, sizeof(expected_name), "%ld-%s-%d.csv", (long)TEST_TIME,
		 TEST_HOSTNAME, TEST_PID);

	assert_string_equal(file_dir, expected_dir);
	assert_string_equal(file_name, expected_name);

	D_FREE(file_dir);
	D_FREE(file_name);
}

static void
test_csv_file_path_root_with_slash(void **state)
{
	char       *file_dir  = NULL;
	char       *file_name = NULL;
	char        expected_dir[PATH_MAX];
	const char *root_dir     = "/tmp/metrics/";
	const char *custom_jobid = "my-custom-job";
	int         rc;

	/* Set custom job id */
	dc_set_default_jobid(custom_jobid);

	/* Mock dc_jobid_is_default to return false */
	will_return(dc_jobid_is_default, false);
	will_return(gmtime, 0);

	rc = csv_file_path(TEST_PID, root_dir, &file_dir, &file_name);
	assert_int_equal(rc, 0);
	assert_non_null(file_dir);
	assert_non_null(file_name);

	/* Note: no extra slash after root_dir */
	snprintf(expected_dir, sizeof(expected_dir), "%s2009/02/13/23/job/%s/%s", root_dir,
		 custom_jobid, program_invocation_name);

	assert_string_equal(file_dir, expected_dir);

	D_FREE(file_dir);
	D_FREE(file_name);
}

static void
test_csv_file_path_null_params(void **state)
{
	char *file_dir  = NULL;
	char *file_name = NULL;
	int   rc;

	will_return(gmtime, 0);
	will_return(gmtime, 0);

	rc = csv_file_path(TEST_PID, NULL, NULL, &file_name);
	assert_int_equal(rc, -DER_INVAL);

	rc = csv_file_path(TEST_PID, NULL, &file_dir, NULL);
	assert_int_equal(rc, -DER_INVAL);
}

static void
test_csv_file_path_path_too_long(void **state)
{
	char *file_dir  = NULL;
	char *file_name = NULL;
	char *long_root;
	int   rc;

	D_ALLOC(long_root, PATH_MAX);
	assert_non_null(long_root);
	memset(long_root, 'a', PATH_MAX - 1);
	long_root[PATH_MAX - 1] = '\0';

	will_return(dc_jobid_is_default, true);
	will_return(gmtime, 0);

	rc = csv_file_path(TEST_PID, long_root, &file_dir, &file_name);
	assert_int_equal(rc, -DER_INVAL);
	assert_null(file_dir);
	assert_null(file_name);

	D_FREE(long_root);
}

static void
test_csv_file_path_gmtime_fails(void **state)
{
	char *file_dir  = NULL;
	char *file_name = NULL;
	char  expected_dir[PATH_MAX];
	int   rc;

	will_return(dc_jobid_is_default, true);
	will_return(gmtime, 1); /* fail */

	rc = csv_file_path(TEST_PID, NULL, &file_dir, &file_name);
	assert_int_equal(rc, 0);

	snprintf(expected_dir, sizeof(expected_dir), "/proc/%s", program_invocation_name);
	assert_string_equal(file_dir, expected_dir);

	D_FREE(file_dir);
	D_FREE(file_name);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test_setup_teardown(test_metrics_enabled, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_should_init_global_flag, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_should_init_cont_attrs, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_should_not_init, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_init_success, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_init_pool_uuid_fails, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_init_cont_uuid_fails, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_fini_no_metrics, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_fini_no_dump_attrs, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_fini_with_dump_attrs_success, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_fini_with_dump_attrs_fail_dump, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_fini_read_attrs_fails, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_default_jobid, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_custom_jobid_with_root, setup,
					    teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_root_with_slash, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_null_params, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_path_too_long, setup, teardown),
	    cmocka_unit_test_setup_teardown(test_csv_file_path_gmtime_fails, setup, teardown),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dfs_metrics_public", tests, NULL, NULL);
}