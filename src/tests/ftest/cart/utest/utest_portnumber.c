/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT testing.
 */

/*
 * This code tests whether a provider returns an error if 2 independent
 * instances attempts to open the same port number.
 *
 * Two child process are forked with the same provider information.
 * The test is setup so that first child opens the port and then
 * the second child should fail.
 * Note, synchronization between child process is performed via sleep
 * function calls.  The sleep time should prevent any problems, but
 * beware if an issue arise where the results are swapped.
 */
/*
 * Reference jira ticket DAOS-5732 to include socket and verb tests.
 */
#define MY_TESTS_NOT_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>
#include <sys/wait.h>

#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>

#include <cmocka.h>
#include <cart/api.h>
#include "gurt/debug.h"

/* global semaphores */
sem_t *child1_sem;
sem_t *child2_sem;

int shmid_c1 = 1;
int shmid_c2;

static void
run_test_fork(void **state)
{
	int		result1 = -1;
	int		result2 = -1;
	int		status = -1;
	int		rc = 0;
	int		child_result;
	pid_t		pid1 = 0;
	pid_t		pid2 = 0;
	crt_context_t	crt_context = NULL;

	/* lock the semaphore */
	sem_trywait(child1_sem);
	sem_trywait(child2_sem);


	pid1 = fork();
	/* fork first child process */
	if (pid1 == 0) {
		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0) {
			sem_post(child2_sem);
			rc = 10;
			goto child1_error;
		}
		child_result = crt_context_create(&crt_context);

		/* Signal second child to continue and wait */
		sem_post(child2_sem);
		sem_wait(child1_sem);

		if (child_result == 0) {
			rc = crt_context_destroy(crt_context, false);
			if (rc != 0) {
				rc = 11;
				goto child1_error;
			}
		}
		rc = crt_finalize();
		if (rc != 0) {
			rc = 12;
			goto child1_error;
		}
		exit(child_result);
child1_error:
		exit(rc);
	}

	/* fork second child process */
	pid2 = fork();
	if (pid2 == 0) {
		/* wait for signal from child 1 */
		sem_wait(child2_sem);

		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0) {
			rc = 20;
			goto child2_error;
		}
		child_result = crt_context_create(&crt_context);
		if (child_result == 0) {
			rc = crt_context_destroy(crt_context, false);
			if (rc != 0) {
				rc = 21;
				goto child2_error;
			}
		}
		/* signal child 1 to finish up */
		sem_post(child1_sem);
		rc = crt_finalize();
		if (rc != 0) {
			rc = 22;
			goto child2_error;
		}
		exit(child_result);
child2_error:
		sem_post(child1_sem);
		exit(rc);

	}

	/* Wait for first child and get results */
	waitpid(pid1, &status, 0);
	if (WIFEXITED(status)) {
		result1 = WEXITSTATUS(status);
	}

	/* Wait for second child and get results */
	waitpid(pid2, &status, 0);
	if (WIFEXITED(status)) {
		result2 = WEXITSTATUS(status);
	}

	/* Test results.  first child should should succeed. */
	assert_true(result1 == 0);
	assert_true(result2 != 0);
	assert_false(result2 == 20);
	assert_false(result2 == 21);
	assert_false(result2 == 22);

}

static void
test_port_tcp(void **state)
{
	setenv("OFI_INTERFACE", "lo", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+tcp;ofi_rxm", 1);
	run_test_fork(state);
}

#ifndef MY_TESTS_NOT_INCLUDED
static void
test_port_sockets(void **state)
{
	setenv("OFI_INTERFACE", "eth0", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+sockets", 1);
	run_test_fork(state);
};

static void
test_port_verb(void **state)
{
	setenv("OFI_INTERFACE", "eth0", 1);
	setenv("OFI_DOMAIN", "Must define here", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+verbs;ofi_rxm", 1);
	run_test_fork(state);
};
#endif

/*******************/
static int
init_tests(void **state)
{
	int size = sizeof(sem_t);
	int flag = IPC_CREAT | 0666;
	int pshare = 1;
	int init_value = 1;
	int rc;

	/* create shared memory regions for semaphores */
	shmid_c1 = shmget(IPC_PRIVATE, size, flag);
	assert_true(shmid_c1  != -1);
	shmid_c2 = shmget(IPC_PRIVATE, size, flag);
	assert_true(shmid_c2  != -1);
	if ((shmid_c1 == -1) || (shmid_c2 == -1)) {
		printf(" SHMID not created\n");
		goto cleanup;
	}

	/* Attach share memeory regions */
	child1_sem = (sem_t *)shmat(shmid_c1, NULL, 0);
	child2_sem = (sem_t *)shmat(shmid_c2, NULL, 0);

	assert_true(child1_sem  != (sem_t *)-1);
	assert_true(child2_sem  != (sem_t *)-1);

	/* Initialize semaphores */
	rc = sem_init(child1_sem, pshare, init_value);
	assert_true(rc == 0);

	rc = sem_init(child2_sem, pshare, init_value);
	assert_true(rc == 0);

	return 0;

cleanup:
	printf(" Error creating semaphoes \n");
	return -1;
}

/*******************/
static int
fini_tests(void **state)
{
	struct shmid_ds smds;
	int rc;

	/* detatch shared memory region */
	if (child1_sem != NULL)
		shmdt(child1_sem);
	if (child2_sem != NULL)
		shmdt(child2_sem);

	/* elimiante the shared memory regions */
	rc = shmctl(shmid_c1, IPC_STAT, &smds);
	assert_true(rc == 0);
	rc = shmctl(shmid_c1, IPC_RMID, &smds);
	assert_true(rc == 0);

	rc = shmctl(shmid_c2, IPC_STAT, &smds);
	assert_true(rc == 0);
	rc = shmctl(shmid_c2, IPC_RMID, &smds);
	assert_true(rc == 0);

	return 0;
}

/*******************/
int main(int argc, char **argv)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_port_tcp),
#ifndef MY_TESTS_NOT_INCLUDED
		cmocka_unit_test(test_port_sockets),
		cmocka_unit_test(test_port_verb),
#endif
	};

	setenv("FI_UNIVERSE_SIZE", "2048", 1);
	setenv("FI_OFI_RXM_USE_SRX", "1", 1);
	setenv("D_LOG_MASK", "CRIT", 1);
	setenv("OFI_PORT", "34571", 1);

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_portnumber", tests,
				init_tests, fini_tests);
}

