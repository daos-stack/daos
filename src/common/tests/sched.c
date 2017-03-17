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
#define DD_SUBSYS	DD_FAC(tests)

#include <daos/common.h>
#include <daos/scheduler.h>
#include <daos/list.h>
#include <daos/hash.h>

#define TASK_COUNT		1000
#define SCHED_COUNT		5

#define DAOS_TEST_FMT	"-------- %s test_%s: %s\n"

#define DAOS_TEST_ENTRY(test_id, test_name)	\
	printf(DAOS_TEST_FMT, "SCHEDULER", test_id, test_name)

#define DAOS_TEST_EXIT(rc)					\
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
	struct daos_sched	sched;
	struct daos_task	*task;
	bool			flag;
	int			rc;

	DAOS_TEST_ENTRY("1", "Scheduler create/complete/cancel");

	printf("Init Scheduler\n");
	rc = daos_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(task);
	rc = daos_task_init(task, NULL, NULL, 0, &sched);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	flag = daos_sched_check_complete(&sched);
	if (flag) {
		printf("Scheduler should have 1 in-flight task\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	daos_task_complete(task, 0);

	printf("Check Scheduler with completed tasks\n");
	flag = daos_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	printf("COMPLETE Scheduler\n");
	daos_sched_complete(&sched, 0, false);

	printf("Re-Init Scheduler\n");
	rc = daos_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(task);
	rc = daos_task_init(task, NULL, NULL, 0, &sched);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	printf("CANCEL non empty scheduler\n");
	daos_sched_complete(&sched, 0, true);

	printf("Check scheduler is empty\n");
	flag = daos_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	DAOS_TEST_EXIT(rc);
	return rc;
}

int
assert_func(struct daos_task *task)
{
	D_ASSERTF(0, "SHOULD NOT BE HERE");
	return 0;
}

int
prep_fail_cb(struct daos_task *task, void *data)
{
	daos_task_complete(task, -1);
	return 0;
}
int
prep_assert_cb(struct daos_task *task, void *data)
{
	D_ASSERTF(0, "SHOULD NOT BE HERE");
	return 0;
}

int
verify_func(struct daos_task *task)
{
	int *verify_cnt = *((int **)daos_task2arg(task));

	if (*verify_cnt != 2) {
		printf("Failed verification of counter\n");
		return -1;
	}

	return 0;
}

int
prep1_cb(struct daos_task *task, void *data)
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
prep2_cb(struct daos_task *task, void *data)
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
comp1_cb(struct daos_task *task, void *data)
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
comp2_cb(struct daos_task *task, void *data)
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
	struct daos_sched	sched;
	struct daos_task	*task;
	bool			flag;
	int			*verify_cnt = NULL;
	int			rc;

	DAOS_TEST_ENTRY("2", "Task Prep & Completion CBs");

	printf("Init Scheduler\n");
	rc = daos_sched_init(&sched, NULL, 0);
	if (rc != 0) {
		printf("Failed to init scheduler: %d\n", rc);
		D_GOTO(out, rc);
	}

	printf("Init task and complete it in prep callback with a failure\n");
	D_ALLOC_PTR(task);
	printf("task %p Allocated\n", task);
	rc = daos_task_init(task, assert_func, NULL, 0, &sched);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	daos_task_register_cbs(task, prep_fail_cb, NULL, 0, NULL, NULL, 0);
	daos_task_register_cbs(task, prep_assert_cb, NULL, 0, NULL, NULL, 0);

	daos_sched_progress(&sched);

	printf("Check scheduler is empty\n");
	flag = daos_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should have no in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(verify_cnt);
	*verify_cnt = 0;

	D_ALLOC_PTR(task);
	rc = daos_task_init(task, verify_func, &verify_cnt, sizeof(int *),
			    &sched);
	if (rc != 0) {
		printf("Failed to init task: %d\n", rc);
		D_GOTO(out, rc);
	}

	printf("Register 2 prep and 2 completion cbs on task\n");
	daos_task_register_cbs(task, prep1_cb, &verify_cnt, sizeof(verify_cnt),
			       comp1_cb, &verify_cnt, sizeof(verify_cnt));
	daos_task_register_cbs(task, prep2_cb, &verify_cnt, sizeof(verify_cnt),
			       comp2_cb, &verify_cnt, sizeof(verify_cnt));

	daos_sched_progress(&sched);

	printf("Check scheduler is not empty\n");
	flag = daos_sched_check_complete(&sched);
	if (flag) {
		printf("Scheduler should have 1 in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (task->dt_result != 0) {
		printf("Failed task processing\n");
		D_GOTO(out, rc = task->dt_result);
	}

	daos_task_complete(task, 0);

	printf("COMPLETE Scheduler\n");
	daos_sched_complete(&sched, 0, false);

	printf("Check scheduler is empty\n");
	flag = daos_sched_check_complete(&sched);
	if (!flag) {
		printf("Scheduler should not have in-flight tasks\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	if (verify_cnt)
		D_FREE_PTR(verify_cnt);
	DAOS_TEST_EXIT(rc);
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

out:
	daos_debug_fini();
	return rc;
}
