/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include <daos/common.h>
#include <daos/tse.h>

#define TASK_COUNT		1000
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
} while (0)

static int
sched_test_1()
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
	return rc;
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

static int
sched_test_2()
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
	return rc;
}

#define REINITS 3000000

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


static int
sched_test_3()
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
	return rc;
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

static int
sched_test_4()
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
	return rc;
}

static int
empty_task_body_fn(tse_task_t *task)
{
	D_ASSERT(task != NULL);
	return 0;
}

static int
sched_test_5()
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
	return rc;
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

static int
sched_test_6()
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
	return rc;
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

static int
sched_test_7()
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
	return rc;
}

int
main(int argc, char **argv)
{
	int		test_fail = 0;
	int		rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = sched_test_1();
	if (rc != 0) {
		print_error("SCHED TEST 1 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_2();
	if (rc != 0) {
		print_error("SCHED TEST 2 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_3();
	if (rc != 0) {
		print_error("SCHED TEST 3 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_4();
	if (rc != 0) {
		print_error("SCHED TEST 4 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_5();
	if (rc != 0) {
		print_error("SCHED TEST 5 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_6();
	if (rc != 0) {
		print_error("SCHED TEST 6 failed: %d\n", rc);
		test_fail++;
	}

	rc = sched_test_7();
	if (rc != 0) {
		print_error("SCHED TEST 7 failed: %d\n", rc);
		test_fail++;
	}

	if (test_fail)
		print_error("ERROR, %d test(s) failed\n", test_fail);
	else
		print_message("SUCCESS, all tests passed\n");

	daos_debug_fini();
	return test_fail;
}
