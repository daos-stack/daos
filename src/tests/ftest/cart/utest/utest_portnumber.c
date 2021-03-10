/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define CHILD1_INIT_ERR			10
#define CHILD1_CONTEXT_DESTROY_ERR	11
#define CHILD1_FINALIZE_ERR		12
#define CHILD1_TIMEOUT_ERR		30
#define CHILD2_INIT_ERR			20
#define CHILD2_CONTEXT_DESTROY_ERR	21
#define CHILD2_FINALIZE_ERR		22
#define CHILD2_TIMEOUT_ERR		31

/* global semaphores */
sem_t *child1_sem;
sem_t *child2_sem;

int shmid_c1;
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
	struct timespec timeout;

	/* drain and lock the semaphore */
	do {
		rc = sem_trywait(child1_sem);
	} while (rc == 0);
	do {
		rc = sem_trywait(child2_sem);
	} while (rc == 0);

	/* Set timeout to 60 seconds. */
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 60;

	/* fork first child process */
	pid1 = fork();
	if (pid1 == 0) {
		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0) {
			sem_post(child2_sem);
			rc = CHILD1_INIT_ERR;
			goto child1_error1;
		}
		child_result = crt_context_create(&crt_context);

		/* Signal second child to continue and wait */
		sem_post(child2_sem);
		rc = sem_timedwait(child1_sem, &timeout);
		if (rc != 0) {
			sem_post(child2_sem);
			rc = crt_context_destroy(crt_context, false);
			rc = CHILD1_TIMEOUT_ERR;
			goto child1_error2;
		}

		/* Expected results from crt_context_create */
		if (child_result == 0) {
			rc = crt_context_destroy(crt_context, false);
			if (rc != 0) {
				rc = CHILD1_CONTEXT_DESTROY_ERR;
				goto child1_error2;
			}
		}

		/* Continue for either case of crt_context_create */
		rc = crt_finalize();
		if (rc != 0) {
			rc = CHILD1_FINALIZE_ERR;
			goto child1_error1;
		}
		exit(child_result);
child1_error2:
		crt_finalize();
child1_error1:
		exit(rc);
	}

	/* fork second child process */
	pid2 = fork();
	if (pid2 == 0) {
		/* wait for signal from child 1 */
		rc = sem_timedwait(child2_sem, &timeout);
		if (rc != 0) {
			rc = CHILD2_TIMEOUT_ERR;
			goto child2_error;
		}

		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0) {
			rc = CHILD2_INIT_ERR;
			goto child2_error;
		}
		child_result = crt_context_create(&crt_context);

		/* Context created, close it it */
		if (child_result == 0) {
			rc = crt_context_destroy(crt_context, false);
			if (rc != 0) {
				rc = CHILD2_CONTEXT_DESTROY_ERR;
				rc = crt_finalize();
				goto child2_error;
			}
		}

		/* signal child 1 to finish up */
		sem_post(child1_sem);
		rc = crt_finalize();
		if (rc != 0) {
			rc = CHILD2_FINALIZE_ERR;
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
	assert_false(result2 == CHILD2_INIT_ERR);
	assert_false(result2 == CHILD2_CONTEXT_DESTROY_ERR);
	assert_false(result2 == CHILD2_FINALIZE_ERR);
	assert_false(result2 == CHILD2_TIMEOUT_ERR);
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
	int rc = 0;

	/* create shared memory regions for semaphores */
	shmid_c1 = shmget(IPC_PRIVATE, size, flag);
	shmid_c2 = shmget(IPC_PRIVATE, size, flag);
	if ((shmid_c1 == -1) || (shmid_c2 == -1)) {
		printf(" SHMID not created\n");
		rc = -1;
		goto cleanup;
	}

	/* Attach share memory regions */
	child1_sem = (sem_t *)shmat(shmid_c1, NULL, 0);
	child2_sem = (sem_t *)shmat(shmid_c2, NULL, 0);

	if ((child1_sem  == (sem_t *)-1) ||
	    (child2_sem  == (sem_t *)-1)) {
		printf(" SHMID: cannot attach memory\n");
		rc = -1;
		goto cleanup;
	}

	/* Initialize semaphores */
	rc = sem_init(child1_sem, pshare, init_value);
	rc |= sem_init(child2_sem, pshare, init_value);
	if (rc != 0) {
		printf("SEM: cannot create semaphore\n");
	}

	return rc;
cleanup:
	printf(" Error creating semaphores\n");
	return -1;
}

/*******************/
static int
fini_tests(void **state)
{
	struct shmid_ds smds;
	int rc;

	/* detach shared memory region */
	if (child1_sem != NULL)
		shmdt(child1_sem);
	if (child2_sem != NULL)
		shmdt(child2_sem);

	/* eliminate the shared memory regions */
	rc = shmctl(shmid_c1, IPC_STAT, &smds);
	rc |= shmctl(shmid_c1, IPC_RMID, &smds);

	rc |= shmctl(shmid_c2, IPC_STAT, &smds);
	rc |= shmctl(shmid_c2, IPC_RMID, &smds);

	if (rc != 0)
		printf(" Error closing share memory\n");

	return rc;
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

