/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(tests)

#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_errno.h>

#include "../dlck_args.h"
#include "../dlck_cmds.h"
#include "../dlck_engine.h"

#define NO_FILES_RC      (-DER_ENOENT)
#define GENERIC_ERROR_RC (-DER_MISC)

static int
			   mock_printf(const char *fmt, ...);

/** globals */

static char                Storage_path[] = "/mock/storage/path";

static struct dlck_control Ctrl;
static struct dlck_file    File1;
static struct dlck_file    File2;
static struct dlck_engine  Engine;
static struct dlck_xstream Xs;

static void
ctrl_default()
{
	memset(&Ctrl, 0, sizeof(Ctrl));
	Ctrl.common.write_mode = true;

	/** files - one file by default */
	memset(&File1, 0, sizeof(File1));
	memset(&File2, 0, sizeof(File2));
	D_INIT_LIST_HEAD(&Ctrl.files.list);
	d_list_add(&File1.link, &Ctrl.files.list);

	Ctrl.engine.storage_path = Storage_path;
	Ctrl.engine.targets      = 1;

	Ctrl.print.dp_printf = mock_printf;

	memset(&Engine, 0, sizeof(Engine));
	memset(&Xs, 0, sizeof(Xs));
	Engine.xss = &Xs;
}

/** mocks */

static int
mock_printf(const char *fmt, ...)
{
	function_called();
	return 0;
}

int
dlck_pool_mkdir(const char *storage_path, uuid_t po_uuid)
{
	assert_ptr_equal(storage_path, Storage_path);
	// assert_ptr_equal(po_uuid, mock_type(char *));
	char *po_uuid_exp = mock_type(char *);
	assert_ptr_equal(po_uuid, po_uuid_exp);

	return mock_type(int);
}

int
dlck_engine_start(struct dlck_args_engine *args, struct dlck_engine **engine_ptr)
{
	int rc;

	assert_ptr_equal(args, &Ctrl.engine);

	rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		*engine_ptr = &Engine;
	}

	return rc;
}

int
dlck_engine_exec_all(struct dlck_engine *engine, dlck_ult_func exec_one,
		     arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn)
{
	void *ult_args;
	int   rc;

	assert_ptr_equal(engine, &Engine);
	assert_ptr_equal(custom, &Ctrl);

	rc = mock_type(int);

	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = arg_alloc_fn(engine, 0, custom, &ult_args);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	exec_one(ult_args);

	rc = arg_free_fn(custom, &ult_args);

	return rc;
}

int
dlck_engine_stop(struct dlck_engine *engine)
{
	assert_ptr_equal(engine, &Engine);

	return mock_type(int);
}

void *
__real_d_calloc(size_t count, size_t eltsize);

void *
__wrap_d_calloc(size_t count, size_t eltsize)
{
	int rc = mock_type(int);

	if (rc == ENOMEM) {
		errno = ENOMEM;
		return NULL;
	}

	return __real_d_calloc(count, eltsize);
}

int
dlck_engine_xstream_init(struct dlck_xstream *xs)
{
	assert_ptr_equal(xs, &Xs);

	return mock_type(int);
}

/** tests */

static void
test_null(void **unused)
{
	int rc;

	rc = dlck_dtx_act_recs_recover(NULL);
	assert_int_equal(rc, -DER_INVAL);
}

/**
 * Make sure a message is printed in case the write mode is not enabled.
 */
static void
test_not_write_mode(void **unused)
{
	int rc;

	ctrl_default();
	Ctrl.common.write_mode = false;
	expect_function_call(mock_printf);
	/** remove files from the list */
	D_INIT_LIST_HEAD(&Ctrl.files.list);

	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, NO_FILES_RC);
}

static void
test_no_files(void **unused)
{
	int rc;

	ctrl_default();
	/** remove files from the list */
	D_INIT_LIST_HEAD(&Ctrl.files.list);

	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, NO_FILES_RC);
}

static void
test_two_files(void **unused)
{
	int rc;

	ctrl_default();
	d_list_add(&File2.link, &Ctrl.files.list);

	/** reverse order */
	will_return(dlck_pool_mkdir, File2.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_engine_start, GENERIC_ERROR_RC); /** stop early */
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, GENERIC_ERROR_RC);
}

static void
test_pool_mkdir_fails(void **unused)
{
	int rc;

	ctrl_default();

	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, GENERIC_ERROR_RC);
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, GENERIC_ERROR_RC);
}

static void
test_engine_start_fails(void **unused)
{
	int rc;

	ctrl_default();

	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_engine_start, GENERIC_ERROR_RC);
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, GENERIC_ERROR_RC);
}

static void
test_exec_all_fails(void **unused)
{
	int rc;

	ctrl_default();

	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_engine_start, DER_SUCCESS);
	will_return(dlck_engine_exec_all, GENERIC_ERROR_RC);
	will_return(dlck_engine_stop, DER_SUCCESS);
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, GENERIC_ERROR_RC);
}

static void
test_args_alloc_fails(void **unused)
{
	int rc;

	ctrl_default();

	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_engine_start, DER_SUCCESS);
	will_return(dlck_engine_exec_all, DER_SUCCESS);
	will_return(__wrap_d_calloc, ENOMEM);
	will_return(dlck_engine_stop, DER_SUCCESS);
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, -DER_NOMEM);
}

static void
test_xstream_init_fails(void **unused)
{
	int rc;

	ctrl_default();

	will_return(dlck_pool_mkdir, File1.po_uuid);
	will_return(dlck_pool_mkdir, DER_SUCCESS);
	will_return(dlck_engine_start, DER_SUCCESS);
	will_return(dlck_engine_exec_all, DER_SUCCESS);
	will_return(__wrap_d_calloc, 0);
	will_return(dlck_engine_xstream_init, GENERIC_ERROR_RC);
	will_return(dlck_engine_stop, DER_SUCCESS);
	expect_function_call(mock_printf);
	rc = dlck_dtx_act_recs_recover(&Ctrl);
	assert_int_equal(rc, GENERIC_ERROR_RC);
}

static const struct CMUnitTest tests_all[] = {
    {"D100: null", test_null, NULL, NULL},
    {"D101: !write_mode", test_not_write_mode, NULL, NULL},
    {"D102: no files", test_no_files, NULL, NULL},
    {"D103: two files", test_two_files, NULL, NULL},
    {"D104: pool mkdir fails", test_pool_mkdir_fails, NULL, NULL},
    {"D105: engine start fails", test_engine_start_fails, NULL, NULL},
    {"D106: exec_all fails", test_exec_all_fails, NULL, NULL},
    {"D107: arg_alloc_fn fails", test_args_alloc_fails, NULL, NULL},
    {"D108: xstream_init fails", test_xstream_init_fails, NULL, NULL},
};

int
main(int argc, char **argv)
{
	const char *test_name = "dlck_dtx_act_recs_recover tests";

	return cmocka_run_group_tests_name(test_name, tests_all, NULL, NULL);
}
