/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for testing task / scheduler
 *
 * common/tests/sched.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <pthread.h>
#include <daos/common.h>
#include <daos/tse.h>

#define TASK_COUNT		(D_ON_VALGRIND ? 10 : 1000)
#define SCHED_COUNT		5

#define TSE_TEST_FMT	"-------- %s test_%s: %s\n"

#define TSE_TEST_ENTRY(test_id, test_name)	\
	print_message(TSE_TEST_FMT, "SCHEDULER", test_id, test_name)

#define TSE_TEST_EXIT(rc)					\
do {								\
	if (rc == 0)						\
		print_message("-------- PASS\n");		\
	else							\
		print_message("-------- FAILED\n");		\
	sleep(1);						\
	assert_int_equal(rc, 0);				\
} while (0)

static void
sched_test_1(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	bool		flag;
	int		rc;

	TSE_TEST_ENTRY("1", "Scheduler create/complete/cancel");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_create(NULL, &sched, NULL, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	flag = tse_sched_check_complete(&sched);
	if (flag) {
		print_error("Scheduler should have 1 in-flight task\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	tse_task_complete(task, 0);

	print_message("Check Scheduler with completed tasks\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	print_message("COMPLETE Scheduler\n");
	tse_sched_complete(&sched, 0, false);

	print_message("Re-Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_create(NULL, &sched, NULL, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("CANCEL non empty scheduler\n");
	tse_sched_addref(&sched);
	tse_sched_complete(&sched, 0, true);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	tse_sched_decref(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	TSE_TEST_EXIT(rc);
}

int
assert_func(tse_task_t *task)
{
	D_ASSERTF(0, "SHOULD NOT BE HERE");
	return 0;
}

int
prep_fail_cb(tse_task_t *task, void *data)
{
	tse_task_complete(task, -1);
	return 0;
}
int
prep_assert_cb(tse_task_t *task, void *data)
{
	D_ASSERTF(0, "SHOULD NOT BE HERE");
	return 0;
}

int
verify_func(tse_task_t *task)
{
	int *verify_cnt = tse_task_get_priv(task);

	if (*verify_cnt != 2) {
		print_error("Failed verification of counter\n");
		return -1;
	}

	return 0;
}

int
prep1_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);

	print_message("Prep1 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 0) {
		print_error("Failed verification of prep cb ordering\n");
		return -1;
	}

	*verify_cnt = *verify_cnt + 1;
	return 0;
}

int
prep2_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);

	print_message("Prep2 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 1) {
		print_error("Failed verification of prep cb ordering\n");
		return -1;
	}

	*verify_cnt = *verify_cnt + 1;
	return 0;
}

int
comp1_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	if (rc != 0) {
		print_error("Task failed unexpectedly\n");
		return rc;
	}

	print_message("Comp1 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 3) {
		print_error("Failed verification of comp cb ordering\n");
		return -1;
	}

	*verify_cnt = *verify_cnt + 1;
	return 0;
}

int
comp2_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	if (rc != 0) {
		print_error("Task failed unexpectedly\n");
		return rc;
	}

	print_message("Comp2 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 2) {
		print_error("Failed verification of comp cb ordering\n");
		return -1;
	}

	*verify_cnt = *verify_cnt + 1;
	return 0;
}

static void
sched_test_2(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	bool		flag;
	int		*verify_cnt = NULL;
	int		rc;

	TSE_TEST_ENTRY("2", "Task Prep & Completion CBs");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Init task and complete in prep cb with a failure\n");
	rc = tse_task_create(assert_func, &sched, NULL, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_task_register_cbs(task, prep_fail_cb, NULL, 0, NULL, NULL, 0);
	tse_task_register_cbs(task, prep_assert_cb, NULL, 0, NULL, NULL, 0);

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should have no in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(verify_cnt);
	*verify_cnt = 0;

	rc = tse_task_create(verify_func, &sched, verify_cnt, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Register 2 prep and 2 completion cbs on task\n");
	tse_task_register_cbs(task, prep1_cb, &verify_cnt, sizeof(verify_cnt),
			       comp1_cb, &verify_cnt, sizeof(verify_cnt));
	tse_task_register_cbs(task, prep2_cb, &verify_cnt, sizeof(verify_cnt),
			       comp2_cb, &verify_cnt, sizeof(verify_cnt));

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	print_message("Check scheduler is not empty\n");
	flag = tse_sched_check_complete(&sched);
	if (flag) {
		print_error("Scheduler should have 1 in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (task->dt_result != 0) {
		print_error("Failed task processing\n");
		D_GOTO(out, rc = task->dt_result);
	}

	tse_task_complete(task, 0);

	print_message("COMPLETE Scheduler\n");
	tse_sched_addref(&sched);
	tse_sched_complete(&sched, 0, false);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	tse_sched_decref(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	if (verify_cnt)
		D_FREE(verify_cnt);
	TSE_TEST_EXIT(rc);
}

#define REINITS (D_ON_VALGRIND ? 3000 : 3000000)

static int
comp_reinit_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	if (*verify_cnt == REINITS)
		return rc;

	rc = tse_task_reinit(task);
	if (rc != 0) {
		print_error("Failed to reinit task (%d)\n", rc);
		return -1;
	}

	return rc;
}

static int
incr_count_func(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);

	*counter = *counter + 1;

	tse_task_register_cbs(task, NULL, NULL, 0, comp_reinit_cb, &counter,
			       sizeof(int *));

	if (*counter % (REINITS/3) == 0)
		print_message("Reinitialized %d times\n", *counter);

	tse_task_complete(task, 0);

	return 0;
}


static void
sched_test_3(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	int		*counter = NULL;
	bool		flag;
	int		rc;

	TSE_TEST_ENTRY("3", "Task Reinitialization in Completion CB");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(counter);
	*counter = 0;

	print_message("Init task and add comp cb to re-init it 3M times\n");
	rc = tse_task_create(incr_count_func, &sched, counter, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	print_message("Verify Counter\n");
	D_ASSERT(*counter == REINITS);

out:
	if (counter)
		D_FREE(counter);
	TSE_TEST_EXIT(rc);
}

#define NUM_REINITS 128

int
inc_reinit_func(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);
	int rc;

	*counter = *counter + 1;

	if (*counter == NUM_REINITS || *counter == NUM_REINITS * 2)
		return 0;

	rc = tse_task_reinit(task);
	if (rc != 0) {
		print_error("Failed task_reinit in body function (%d)\n", rc);
		return -1;
	}

	return 0;
}

int
prep_reinit_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	if (*verify_cnt != 0) {
		print_error("Prep CB Failed counter verification\n");
		return -1;
	}

	rc = tse_task_reinit(task);
	if (rc != 0) {
		print_error("Failed to reinit task in prep CB (%d)\n", rc);
		return -1;
	}

	return 0;
}

int
comp_reinit_cb2(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	print_message("VERIFY Counter = %d\n", *verify_cnt);

	if (*verify_cnt != NUM_REINITS && *verify_cnt != NUM_REINITS * 2) {
		print_error("COMP Failed counter verification\n");
		return -1;
	}

	if (*verify_cnt == NUM_REINITS) {
		rc = tse_task_reinit(task);
		if (rc != 0) {
			print_error("Failed task_reinit in comp CB (%d)\n", rc);
			return -1;
		}
	}

	return 0;
}

static void
sched_test_4(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	int		*counter = NULL;
	bool		flag;
	int		rc;

	TSE_TEST_ENTRY("4", "Task Reinitialization in Body Function");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(counter);
	*counter = 0;

	print_message("Init task and add prep/comp cbs to re-init it\n");
	rc = tse_task_create(inc_reinit_func, &sched, counter, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_task_register_cbs(task, prep_reinit_cb, &counter, sizeof(int *),
			       NULL, NULL, 0);
	tse_task_register_cbs(task, NULL, NULL, 0, comp_reinit_cb2, &counter,
			       sizeof(int *));

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	/** need to progress twice because of the re-init in the prep */
	tse_sched_progress(&sched);
	tse_sched_progress(&sched);

	print_message("Complete task - should be reinitialized in comp CB\n");
	tse_task_complete(task, 0);

	print_message("Check scheduler is not empty\n");
	flag = tse_sched_check_complete(&sched);
	if (flag) {
		print_error("Scheduler should have 1 in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	tse_sched_progress(&sched);
	print_message("Complete task again\n");
	tse_task_complete(task, 0);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	print_message("Verify Counter\n");
	D_ASSERT(*counter == NUM_REINITS * 2);

out:
	if (counter)
		D_FREE(counter);
	TSE_TEST_EXIT(rc);
}

static int
empty_task_body_fn(tse_task_t *task)
{
	D_ASSERT(task != NULL);
	return 0;
}

static void
sched_test_5(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	int		counter = 0;
	bool		flag;
	int		rc;

	TSE_TEST_ENTRY("5", "reinit completed task");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Init task\n");
	rc = tse_task_create(empty_task_body_fn, &sched, NULL, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("test reinit of completed task %d times\n", NUM_REINITS);
reinited:
	flag = tse_sched_check_complete(&sched);
	if (flag) {
		print_error("Scheduler should have 1 in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	tse_sched_progress(&sched);
	tse_task_addref(task);
	tse_task_complete(task, 0);

	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (counter++ < NUM_REINITS) {
		rc = tse_task_reinit(task);
		if (rc == 0) {
			tse_task_decref(task);
			D_GOTO(reinited, rc);
		} else {
			print_error("Failed reinit completed task (%d)\n", rc);
			D_GOTO(out, rc);
		}
	} else {
		tse_task_decref(task);
	}

out:
	TSE_TEST_EXIT(rc);
}

#define NUM_DEPS 128

int
inc_func(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);

	*counter = *counter + 1;

	return 0;
}

int
check_func_n(tse_task_t *task)
{
	int *verify_cnt = tse_task_get_priv(task);

	if (*verify_cnt != NUM_DEPS) {
		print_error("Failed Task dependencies\n");
		return -1;
	}

	return 0;
}

int
check_func_1(tse_task_t *task)
{
	int *verify_cnt = tse_task_get_priv(task);

	if (*verify_cnt != 1) {
		print_error("Failed Task dependencies\n");
		return -1;
	}

	return 0;
}

static void
sched_test_6(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task = NULL;
	tse_task_t	*tasks[NUM_DEPS];
	int		*counter = NULL;
	bool		flag;
	int		i, rc;

	TSE_TEST_ENTRY("6", "Task Dependencies");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(counter);
	*counter = 0;

	print_message("Test N -> 1 dependencies\n");
	rc = tse_task_create(check_func_n, &sched, counter, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	for (i = 0; i < NUM_DEPS; i++) {
		rc = tse_task_create(inc_func, &sched, counter, &tasks[i]);
		if (rc != 0) {
			print_error("Failed to init task: %d\n", rc);
			D_GOTO(out, rc);
		}

		rc = tse_task_schedule(tasks[i], false);
		if (rc != 0) {
			print_error("Failed to schedule task %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	print_message("Register Dependencies\n");
	rc = tse_task_register_deps(task, NUM_DEPS, tasks);
	if (rc != 0) {
		print_error("Failed to register task Deps: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	for (i = 0; i < NUM_DEPS; i++)
		tse_task_complete(tasks[i], 0);

	tse_sched_progress(&sched);
	tse_task_complete(task, 0);
	task = NULL; /* lost my refcount */

	print_message("Verify Counter\n");
	D_ASSERT(*counter == NUM_DEPS);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	*counter = 0;
	print_message("Test 1 -> N dependencies\n");
	rc = tse_task_create(inc_func, &sched, counter, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Init tasks with Dependencies\n");
	for (i = 0; i < NUM_DEPS; i++) {
		rc = tse_task_create(check_func_1, &sched, counter, &tasks[i]);
		if (rc != 0) {
			print_error("Failed to init task: %d\n", rc);
			D_GOTO(out, rc);
		}

		rc = tse_task_register_deps(tasks[i], 1, &task);
		if (rc != 0) {
			print_error("Failed to register task Deps: %d\n", rc);
			D_GOTO(out, rc);
		}

		rc = tse_task_schedule(tasks[i], false);
		if (rc != 0) {
			print_error("Failed to schedule task: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);
	tse_task_complete(task, 0);
	tse_sched_progress(&sched);
	task = NULL; /* lost my refcount */

	for (i = 0; i < NUM_DEPS; i++)
		tse_task_complete(tasks[i], 0);

	print_message("Verify Counter\n");
	D_ASSERT(*counter == 1);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	if (task)
		tse_task_decref(task);
	if (counter)
		D_FREE(counter);
	TSE_TEST_EXIT(rc);
}

int
inc_func1(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);

	*counter = *counter + 1;
	return 0;
}

int
inc_func2(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);

	*counter = *counter + 2;
	return 0;
}

int
inc_func3(tse_task_t *task)
{
	int *counter = tse_task_get_priv(task);

	*counter = *counter + 3;
	return 0;
}

static void
sched_test_7(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task = NULL;
	int		*counter = NULL;
	bool		flag;
	int		rc;

	TSE_TEST_ENTRY("7", "Task Reset");

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(counter);
	*counter = 0;

	rc = tse_task_create(inc_func1, &sched, counter, &task);
	if (rc != 0) {
		print_error("Failed to create task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);
	tse_task_addref(task); /* take extra ref count on task */
	tse_task_complete(task, 0);

	D_ASSERT(*counter == 1);

	rc = tse_task_reset(task, inc_func2, counter);
	if (rc != 0) {
		print_error("Failed to reset task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);
	tse_task_addref(task); /* take extra ref count on task */
	tse_task_complete(task, 0);
	D_ASSERT(*counter == 3);

	rc = tse_task_reset(task, inc_func3, counter);
	if (rc != 0) {
		print_error("Failed to reset task: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	tse_sched_progress(&sched);
	tse_task_complete(task, 0);
	task = NULL; /* lost my refcount */
	D_ASSERT(*counter == 6);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	if (task)
		tse_task_decref(task);
	if (counter)
		D_FREE(counter);
	TSE_TEST_EXIT(rc);
}

static int
just_complete_body_fn(tse_task_t *task)
{
	D_ASSERT(task != NULL);
	tse_task_complete(task, 0);
	return 0;
}

static void
sched_test_8(void **state)
{
	tse_sched_t	sched;
	tse_task_t	*task;
	uint64_t	delay = 5 * 1e6 /* 5 s */;
	uint64_t	scheduled_pre;
	uint64_t	scheduled_post;
	bool		completed;
	int		rc;

	TSE_TEST_ENTRY("8", "Delayed scheduling");

	print_message("Init scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Create task\n");
	rc = tse_task_create(just_complete_body_fn, &sched, NULL, &task);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Schedule task with a delay\n");
	scheduled_pre = daos_getutime();
	rc = tse_task_schedule_with_delay(task, false, delay);
	scheduled_post = daos_getutime();
	if (rc != 0) {
		print_error("Failed to insert task in scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Progress scheduler immediately\n");
	tse_sched_progress(&sched);

	print_message("Check that task has not been executed yet\n");
	completed = tse_sched_check_complete(&sched);
	if (daos_getutime() - scheduled_pre >= delay) {
		print_message("Test unexpectedly slow; skip this check\n");
	} else if (completed) {
		print_error("Scheduler should have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	print_message("Wait out the delay\n");
	while (daos_getutime() - scheduled_post < delay)
		sleep(1);

	print_message("Progress scheduler again\n");
	tse_sched_progress(&sched);

	print_message("Check that task has been executed\n");
	completed = tse_sched_check_complete(&sched);
	if (!completed) {
		print_error("Scheduler should have completed task\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	print_message("Complete scheduler\n");
	tse_sched_complete(&sched, 0, false);

out:
	TSE_TEST_EXIT(rc);
}

static int
prep_done_cb(tse_task_t *task, void *data)
{
	tse_task_complete(task, 0);
	return 0;
}

static int
comp_comp_cb(tse_task_t *task, void *data)
{
	tse_task_complete(task, 0);
	return 0;
}

static int
child_func(tse_task_t *task)
{
	tse_task_complete(task, 0);
	return 0;
}

static bool stop_progress;

struct sched_test_thread_arg {
	tse_sched_t	*sched;
	tse_task_t      **tasks;
	int		th_id;
};

#define NR_TASKS 10000

static void *
th_sched_progress(void *arg)
{
	tse_sched_t	*sched = arg;

	while (1) {
		if (stop_progress) {
			printf("progress thread exiting\n");
			pthread_exit(NULL);
		}
		tse_sched_progress(sched);
	}
}

static int
parent_func(tse_task_t *task)
{
	tse_task_t	*task1 = NULL;
	tse_task_t	*task2 = NULL;
	d_list_t	io_task_list;
	int		rc;

	D_INIT_LIST_HEAD(&io_task_list);

	rc = tse_task_create(child_func, tse_task2sched(task), NULL, &task1);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}
	tse_task_list_add(task1, &io_task_list);

	rc = tse_task_create(assert_func, tse_task2sched(task), NULL, &task2);
	if (rc != 0) {
		print_error("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}
	/** complete task2 in prep cb */
	tse_task_register_cbs(task2, prep_done_cb, NULL, 0, NULL, NULL, 0);
	tse_task_list_add(task2, &io_task_list);

	rc = tse_task_register_deps(task2, 1, &task1);
	if (rc != 0) {
		D_ERROR("Failed to register task 2 dependency\n");
		D_GOTO(out, rc);
	}

	rc = tse_task_register_deps(task, 1, &task2);
	if (rc != 0) {
		D_ERROR("Failed to register task dependency\n");
		D_GOTO(out, rc);
	}

	tse_task_list_sched(&io_task_list, false);
	return 0;

out:
	tse_task_complete(task, rc);
	return rc;
}

static void *
th_create_task(void *arg)
{
	tse_task_t			*task;
	struct sched_test_thread_arg	*args = arg;
	int				rc, i;

	for (i = 0; i < NR_TASKS; i++) {
		rc = tse_task_create(parent_func, args->sched, NULL, &task);
		if (rc != 0) {
			print_error("Failed to init task: %d\n", rc);
			D_GOTO(out, rc);
		}

		rc = tse_task_register_cbs(task, NULL, NULL, 0, comp_comp_cb, NULL, 0);
		if (rc != 0) {
			print_error("Failed to register comp cb\n");
			D_GOTO(out, rc);
		}

		rc = tse_task_schedule(task, true);
		if (rc != 0) {
			print_error("Failed to schedule task %d\n", rc);
			D_GOTO(out, rc);
		}
	}
out:
	pthread_exit(NULL);
}

static void
sched_test_9(void **state)
{
	pthread_t			th;
	pthread_t			*c_th = NULL;
	struct sched_test_thread_arg	*args = NULL;
	tse_task_t			**tasks = NULL;
	tse_sched_t			sched;
	bool				flag;
	cpu_set_t			cpuset;
	int				nr_threads;
	int				i;
	int				rc;

	TSE_TEST_ENTRY("9", "Multi threaded task dependency test");

	rc = sched_getaffinity(0, sizeof(cpuset), &cpuset);
	if (rc != 0) {
		printf("Failed to get cpuset information\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	nr_threads = CPU_COUNT(&cpuset);
	printf("Running with %d pthreads..\n", nr_threads);

	D_ALLOC_ARRAY(tasks, NR_TASKS * nr_threads);
	if (tasks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(c_th, nr_threads);
	if (c_th == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(args, nr_threads);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Creating progress thread..\n");

	rc = pthread_create(&th, NULL, th_sched_progress, &sched);
	if (rc != 0) {
		print_error("Failed to create pthread: %d\n", rc);
		D_GOTO(out, rc);
	}

	for (i = 0; i < nr_threads; i++) {
		args[i].sched = &sched;
		args[i].tasks = &tasks[i*NR_TASKS];
		args[i].th_id = i;
		rc = pthread_create(&c_th[i], NULL, th_create_task, &args[i]);
		if (rc != 0) {
			print_error("Failed to create pthread: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	do {
		flag = tse_sched_check_complete(&sched);
		if (flag)
			printf("sched not empty, sleeping\n");
		sleep(1);
	} while (!flag);

	for (i = 0; i < nr_threads; i++) {
		rc = pthread_join(c_th[i], NULL);
		if (rc != 0) {
			print_error("Failed pthread_join: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	stop_progress = true;
	rc = pthread_join(th, NULL);
	if (rc != 0) {
		print_error("Failed pthread_join: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("COMPLETE Scheduler\n");
	tse_sched_addref(&sched);
	tse_sched_complete(&sched, 0, false);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	tse_sched_decref(&sched);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	D_FREE(tasks);
	D_FREE(c_th);
	D_FREE(args);
	TSE_TEST_EXIT(rc);
}

static int
test_10_task_body(tse_task_t *task)
{
	tse_task_complete(task, 0);
	return 0;
}

static void
sched_test_10(void **state)
{
	pthread_t	th_1, th_2;
	tse_sched_t	sched_1, sched_2;
	tse_task_t	**tasks = NULL;
	bool		flag;
	int		ntask = 100;
	int		i, rc;

	TSE_TEST_ENTRY("10", "cross scheduler task dependency test");

	D_ALLOC_ARRAY(tasks, ntask * 2);
	if (tasks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	stop_progress = 0;
	print_message("Init Scheduler\n");
	rc = tse_sched_init(&sched_1, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = tse_sched_init(&sched_2, NULL, 0);
	if (rc != 0) {
		print_error("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("Creating progress thread..\n");

	rc = pthread_create(&th_1, NULL, th_sched_progress, &sched_1);
	if (rc != 0) {
		print_error("Failed to create pthread: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = pthread_create(&th_2, NULL, th_sched_progress, &sched_2);
	if (rc != 0) {
		print_error("Failed to create pthread: %d\n", rc);
		D_GOTO(out, rc);
	}

	for (i = 0; i < ntask * 2; i++) {
		if (i < ntask)
			rc = tse_task_create(test_10_task_body, &sched_1, NULL, &tasks[i]);
		else
			rc = tse_task_create(test_10_task_body, &sched_2, NULL, &tasks[i]);
		if (rc != 0) {
			print_error("Failed to create task: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	for (i = 0; i < ntask; i++) {
		rc = tse_task_register_deps(tasks[i], 1, &tasks[i + ntask]);
		if (rc != 0) {
			print_error("Failed to register task Deps: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	for (i = 0; i < ntask * 2; i++) {
		rc = tse_task_schedule(tasks[i], false);
		if (rc != 0) {
			print_error("Failed to schedule task: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	do {
		flag = tse_sched_check_complete(&sched_2);
		if (flag)
			printf("sched not empty, sleeping\n");
		sleep(1);
	} while (!flag);
	do {
		flag = tse_sched_check_complete(&sched_1);
		if (flag)
			printf("sched not empty, sleeping\n");
		sleep(1);
	} while (!flag);

	stop_progress = true;
	rc = pthread_join(th_1, NULL);
	if (rc != 0) {
		print_error("Failed pthread_join: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = pthread_join(th_2, NULL);
	if (rc != 0) {
		print_error("Failed pthread_join: %d\n", rc);
		D_GOTO(out, rc);
	}

	print_message("COMPLETE Scheduler\n");
	tse_sched_addref(&sched_1);
	tse_sched_complete(&sched_1, 0, false);
	tse_sched_addref(&sched_2);
	tse_sched_complete(&sched_2, 0, false);

	print_message("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched_1);
	tse_sched_decref(&sched_1);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	flag = tse_sched_check_complete(&sched_2);
	tse_sched_decref(&sched_2);
	if (!flag) {
		print_error("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	D_FREE(tasks);
	TSE_TEST_EXIT(rc);
}


static int
sched_ut_setup(void **state)
{
	return daos_debug_init(DAOS_LOG_DEFAULT);
}

static int
sched_ut_teardown(void **state)
{
	daos_debug_fini();
	return 0;
}

static const struct CMUnitTest sched_uts[] = {
	{ "SCHED_Test_1", sched_test_1, NULL, NULL},
	{ "SCHED_Test_2", sched_test_2, NULL, NULL},
	{ "SCHED_Test_3", sched_test_3, NULL, NULL},
	{ "SCHED_Test_4", sched_test_4, NULL, NULL},
	{ "SCHED_Test_5", sched_test_5, NULL, NULL},
	{ "SCHED_Test_6", sched_test_6, NULL, NULL},
	{ "SCHED_Test_7", sched_test_7, NULL, NULL},
	{ "SCHED_Test_8", sched_test_8, NULL, NULL},
	{ "SCHED_Test_9", sched_test_9, NULL, NULL},
	{ "SCHED_Test_10", sched_test_10, NULL, NULL}
};

int main(int argc, char **argv)
{
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("Event Queue unit tests", sched_uts,
					   sched_ut_setup, sched_ut_teardown);
}
