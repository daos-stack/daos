/**
 * (C) Copyright 2016 Intel Corporation.
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
 * common/tests/sched_tests.c
 */
#define DDSUBSYS	DDFAC(tests)

#include <daos/common.h>
#include <daos/tse.h>

#define TASK_COUNT		1000
#define SCHED_COUNT		5

#define TSE_TEST_FMT	"-------- %s test_%s: %s\n"

#define TSE_TEST_ENTRY(test_id, test_name)	\
	printf(TSE_TEST_FMT, "SCHEDULER", test_id, test_name)

#define TSE_TEST_EXIT(rc)					\
do {								\
	if (rc == 0)						\
		printf("-------- PASS\n");			\
	else							\
		printf("-------- FAILED\n");			\
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

	printf("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_create(NULL, &sched, NULL, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	flag = tse_sched_check_complete(&sched);
	if (flag) {
		printf("Scheduler should have 1 in-flight task\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	tse_task_complete(task, 0);

	printf("Check Scheduler with completed tasks\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	printf("COMPLETE Scheduler\n");
	tse_sched_complete(&sched, 0, false);

	printf("Re-Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_create(NULL, &sched, NULL, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	printf("CANCEL non empty scheduler\n");
	tse_sched_complete(&sched, 0, true);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

out:
	TSE_TEST_EXIT(rc);
	return rc;
}

int
assert_func(tse_task_t *task)
{
	D__ASSERTF(0, "SHOULD NOT BE HERE");
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
	D__ASSERTF(0, "SHOULD NOT BE HERE");
	return 0;
}

int
verify_func(tse_task_t *task)
{
	int *verify_cnt = tse_task_get_priv(task);

	if (*verify_cnt != 2) {
		printf("Failed verification of counter\n");
		return -1;
	}

	return 0;
}

int
prep1_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);

	printf("Prep1 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 0) {
		printf("Failed verification of prep cb ordering\n");
		return -1;
	}

	*verify_cnt = *verify_cnt + 1;
	return 0;
}

int
prep2_cb(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);

	printf("Prep2 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 1) {
		printf("Failed verification of prep cb ordering\n");
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
		printf("Task failed unexpectedly\n");
		return rc;
	}

	printf("Comp1 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 3) {
		printf("Failed verification of comp cb ordering\n");
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
		printf("Task failed unexpectedly\n");
		return rc;
	}

	printf("Comp2 CB: counter = %d\n", *verify_cnt);
	if (*verify_cnt != 2) {
		printf("Failed verification of comp cb ordering\n");
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

	printf("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	printf("Init task and complete it in prep callback with a failure\n");
	rc = tse_task_create(assert_func, &sched, NULL, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_task_register_cbs(task, prep_fail_cb, NULL, 0, NULL, NULL, 0);
	tse_task_register_cbs(task, prep_assert_cb, NULL, 0, NULL, NULL, 0);

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should have no in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	D__ALLOC_PTR(verify_cnt);
	*verify_cnt = 0;

	rc = tse_task_create(verify_func, &sched, verify_cnt, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	printf("Register 2 prep and 2 completion cbs on task\n");
	tse_task_register_cbs(task, prep1_cb, &verify_cnt, sizeof(verify_cnt),
			       comp1_cb, &verify_cnt, sizeof(verify_cnt));
	tse_task_register_cbs(task, prep2_cb, &verify_cnt, sizeof(verify_cnt),
			       comp2_cb, &verify_cnt, sizeof(verify_cnt));

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	printf("Check scheduler is not empty\n");
	flag = tse_sched_check_complete(&sched);
	if (flag) {
		printf("Scheduler should have 1 in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	if (task->dt_result != 0) {
		printf("Failed task processing\n");
		D__GOTO(out, rc = task->dt_result);
	}

	tse_task_complete(task, 0);

	printf("COMPLETE Scheduler\n");
	tse_sched_complete(&sched, 0, false);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

out:
	if (verify_cnt)
		D__FREE_PTR(verify_cnt);
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
		printf("Failed to reinit task (%d)\n", rc);
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
		printf("Reinitialized %d times\n", *counter);

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

	printf("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	D__ALLOC_PTR(counter);
	*counter = 0;

	printf("Init task and add comp cb to re-init it 3M times\n");
	rc = tse_task_create(incr_count_func, &sched, counter, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	printf("Verify Counter\n");
	D__ASSERT(*counter == REINITS);

out:
	if (counter)
		D__FREE_PTR(counter);
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
		printf("Failed to reinit task in body function (%d)\n", rc);
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
		printf("Prep CB Failed counter verification\n");
		return -1;
	}

	rc = tse_task_reinit(task);
	if (rc != 0) {
		printf("Failed to reinit task in prep CB (%d)\n", rc);
		return -1;
	}

	return 0;
}

int
comp_reinit_cb2(tse_task_t *task, void *data)
{
	int *verify_cnt = *((int **)data);
	int rc = task->dt_result;

	printf("VERIFY Counter = %d\n", *verify_cnt);

	if (*verify_cnt != NUM_REINITS && *verify_cnt != NUM_REINITS * 2) {
		printf("COMP Failed counter verification\n");
		return -1;
	}

	if (*verify_cnt == NUM_REINITS) {
		rc = tse_task_reinit(task);
		if (rc != 0) {
			printf("Failed to reinit task in comp CB (%d)\n", rc);
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

	printf("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	D__ALLOC_PTR(counter);
	*counter = 0;

	printf("Init task and add prep/comp cbs to re-init it\n");
	rc = tse_task_create(inc_reinit_func, &sched, counter, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_task_register_cbs(task, prep_reinit_cb, &counter, sizeof(int *),
			       NULL, NULL, 0);
	tse_task_register_cbs(task, NULL, NULL, 0, comp_reinit_cb2, &counter,
			       sizeof(int *));

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	/** need to progress twice because of the re-init in the prep */
	tse_sched_progress(&sched);
	tse_sched_progress(&sched);

	printf("Complete task - should be reinitialized in comp CB\n");
	tse_task_complete(task, 0);

	printf("Check scheduler is not empty\n");
	flag = tse_sched_check_complete(&sched);
	if (flag) {
		printf("Scheduler should have 1 in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	tse_sched_progress(&sched);
	printf("Complete task again\n");
	tse_task_complete(task, 0);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	printf("Verify Counter\n");
	D__ASSERT(*counter == NUM_REINITS * 2);

out:
	if (counter)
		D__FREE_PTR(counter);
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
		printf("Failed Task dependencies\n");
		return -1;
	}

	return 0;
}

int
check_func_1(tse_task_t *task)
{
	int *verify_cnt = tse_task_get_priv(task);

	if (*verify_cnt != 1) {
		printf("Failed Task dependencies\n");
		return -1;
	}

	return 0;
}

static int
sched_test_5()
{
	tse_sched_t	sched;
	tse_task_t	*task = NULL;
	tse_task_t	*tasks[NUM_DEPS];
	int		*counter = NULL;
	bool		flag;
	int		i, rc;

	TSE_TEST_ENTRY("5", "Task Dependencies");

	printf("Init Scheduler\n");
	rc = tse_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	D__ALLOC_PTR(counter);
	*counter = 0;

	printf("Test N -> 1 dependencies\n");
	rc = tse_task_create(check_func_n, &sched, counter, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	for (i = 0; i < NUM_DEPS; i++) {
		rc = tse_task_create(inc_func, &sched, counter, &tasks[i]);
		if (rc != 0) {
			printf("Failed to init task: %d\n", rc);
			D__GOTO(out, rc);
		}

		rc = tse_task_schedule(tasks[i], false);
		if (rc != 0) {
			printf("Failed to insert task in scheduler: %d\n", rc);
			D__GOTO(out, rc);
		}
	}

	printf("Register Dependecies\n");
	rc = tse_task_register_deps(task, NUM_DEPS, tasks);
	if (rc != 0) {
		printf("Failed to register task Deps: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_sched_progress(&sched);

	for (i = 0; i < NUM_DEPS; i++)
		tse_task_complete(tasks[i], 0);

	tse_sched_progress(&sched);
	tse_task_complete(task, 0);

	printf("Verify Counter\n");
	D__ASSERT(*counter == NUM_DEPS);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	*counter = 0;
	printf("Test 1 -> N dependencies\n");
	rc = tse_task_create(inc_func, &sched, counter, &task);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D__GOTO(out, rc);
	}

	printf("Init tasks with Dependecies\n");
	for (i = 0; i < NUM_DEPS; i++) {
		rc = tse_task_create(check_func_1, &sched, counter, &tasks[i]);
		if (rc != 0) {
			printf("Failed to init task: %d\n", rc);
			D__GOTO(out, rc);
		}

		rc = tse_task_register_deps(tasks[i], 1, &task);
		if (rc != 0) {
			printf("Failed to register task Deps: %d\n", rc);
			D__GOTO(out, rc);
		}

		rc = tse_task_schedule(tasks[i], false);
		if (rc != 0) {
			printf("Failed to insert task in scheduler: %d\n", rc);
			D__GOTO(out, rc);
		}
	}

	rc = tse_task_schedule(task, false);
	if (rc != 0) {
		printf("Failed to insert task in scheduler: %d\n", rc);
		D__GOTO(out, rc);
	}

	tse_sched_progress(&sched);
	tse_task_complete(task, 0);
	tse_sched_progress(&sched);

	for (i = 0; i < NUM_DEPS; i++)
		tse_task_complete(tasks[i], 0);

	printf("Verify Counter\n");
	D__ASSERT(*counter == 1);

	printf("Check scheduler is empty\n");
	flag = tse_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

out:
	if (task)
		tse_task_decref(task);
	if (counter)
		D__FREE_PTR(counter);
	TSE_TEST_EXIT(rc);
	return rc;
}

int
main(int argc, char **argv)
{
	int		rc;

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = sched_test_1();
	if (rc != 0)
		goto out;

	rc = sched_test_2();
	if (rc != 0)
		goto out;

	rc = sched_test_3();
	if (rc != 0)
		goto out;

	rc = sched_test_4();
	if (rc != 0)
		goto out;

	rc = sched_test_5();
	if (rc != 0)
		goto out;

out:
	daos_debug_fini();
	return rc;
}
