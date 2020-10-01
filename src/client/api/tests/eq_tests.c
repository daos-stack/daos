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
 * This file is for testing DAOS event queue
 *
 * common/tests/eq_tests.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <gurt/list.h>
#include <gurt/hash.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/* XXX For the testing purpose, this test case will use
 * some internal api of event queue, and for real use
 * cases, this daos_eq_internal should not be exposed */
#include "../client_internal.h"

#define EQT_EV_COUNT		1000
#define EQ_COUNT		5
#define EQT_SLEEP_INV		2

#define DAOS_TEST_FMT	"-------- %s test_%s: %s\n"

#define DAOS_TEST_ENTRY(test_id, test_name)	\
	print_message(DAOS_TEST_FMT, "EQ", test_id, test_name)

#define DAOS_TEST_EXIT(rc)					\
do {								\
	if (rc == 0)						\
		print_message("-------- PASS\n");		\
	else							\
		print_message("-------- FAILED\n");		\
	sleep(1);						\
} while (0)


static daos_handle_t	my_eqh;

static int
eq_test_1()
{
	struct daos_event	*ep;
	struct daos_event	ev;
	struct daos_event	abort_ev;
	daos_handle_t		eqh;
	int			rc;

	DAOS_TEST_ENTRY("1", "daos_eq_create/destroy");

	print_message("Create EQ\n");
	rc = daos_eq_create(&eqh);
	if (rc != 0) {
		print_error("Failed to create EQ: %d\n", rc);
		goto out;
	}

	rc = daos_event_init(&ev, eqh, NULL);
	D_ASSERT(rc == 0);

	rc = daos_event_launch(&ev);
	D_ASSERT(rc == 0);

	daos_event_complete(&ev, 0);

	rc = daos_event_init(&abort_ev, eqh, NULL);
	D_ASSERT(rc == 0);

	rc = daos_event_launch(&abort_ev);
	D_ASSERT(rc == 0);

	daos_event_abort(&abort_ev);

	print_message("Destroy non-empty EQ\n");
	rc = daos_eq_destroy(eqh, 0);
	if (rc != -DER_BUSY) {
		print_error("Failed to destroy non-empty EQ: %d\n", rc);
		goto out;
	}

	rc = daos_eq_poll(eqh, 0, 0, 1, &ep);
	if (rc != 1) {
		print_error("Failed to drain EQ: %d\n", rc);
		goto out;
	}
	daos_event_fini(&ev);
	daos_event_fini(&abort_ev);

	print_message("Destroy empty EQ\n");
	rc = daos_eq_destroy(eqh, 0);
	if (rc != 0) {
		print_error("Failed to destroy empty EQ: %d\n", rc);
		goto out;
	}

out:
	DAOS_TEST_EXIT(rc);
	return rc;
}

static int
eq_test_2()
{
	struct daos_event	*eps[EQT_EV_COUNT + 1] = { 0 };
	struct daos_event	*events[EQT_EV_COUNT + 1] = { 0 };
	int			rc;
	int			i;

	DAOS_TEST_ENTRY("2", "Event Query & Poll");

	for (i = 0; i < EQT_EV_COUNT; i++) {
		events[i] = malloc(sizeof(*events[i]));
		if (events[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		rc = daos_event_init(events[i], my_eqh, NULL);
		if (rc != 0)
			goto out;
	}

	print_message("Poll empty EQ w/o wait\n");
	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, EQT_EV_COUNT, eps);
	if (rc != 0) {
		print_error("Expect to poll zero event: %d\n", rc);
		goto out;
	}

	print_message("Test events / Query EQ with inflight events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		bool ev_flag;

		rc = daos_event_launch(events[i]);
		if (rc != 0) {
			print_error("Failed to launch event %d: %d\n", i, rc);
			goto out;
		}

		rc = daos_event_test(events[i], DAOS_EQ_NOWAIT, &ev_flag);
		if (rc != 0) {
			print_error("Test on child event returned %d\n", rc);
			goto out;
		}
		if (ev_flag) {
			print_error("Event %d should be inflight\n", i);
			rc = -1;
			goto out;
		}

		rc = daos_eq_query(my_eqh, DAOS_EQR_WAITING, 0, NULL);
		if (rc != i + 1) {
			print_error("Expect to see %d inflight event, "
				 "but got %d\n", i + 1, rc);
			rc = -1;
			goto out;
		}
	}

	print_message("Poll EQ with timeout\n");
	rc = daos_eq_poll(my_eqh, 1, 10, EQT_EV_COUNT, eps);
	if (rc != -DER_TIMEDOUT) {
		print_error("Expect to poll zero event: %d\n", rc);
		goto out;
	}

	print_message("Query EQ with completion events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		daos_event_complete(events[i], 0);
		rc = daos_eq_query(my_eqh, DAOS_EQR_COMPLETED,
				   EQT_EV_COUNT, eps);
		if (rc != i + 1) {
			print_error("Expect to see %d inflight event, "
				 "but got %d\n", i + 1, rc);
			rc = -1;
			goto out;
		}

		if (eps[rc - 1] != events[i]) {
			print_error("Unexpected result from query: %d %p %p\n",
				    i, eps[rc - 1], &events[i]);
			rc = -1;
			goto out;
		}
	}

	print_message("Poll EQ with completion events\n");
	rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, eps);
	if (rc != EQT_EV_COUNT) {
		print_error("Expect to poll %d event: %d\n",
			EQT_EV_COUNT, rc);
		goto out;
	}
	rc = 0;
out:
	for (i = 0; i < EQT_EV_COUNT; i++) {
		if (events[i] != NULL) {
			daos_event_fini(events[i]);
			free(events[i]);
		}
	}
	DAOS_TEST_EXIT(rc);
	return rc;
}

static int
eq_test_3()
{
	struct daos_event	*eps[2];
	struct daos_event	*child_events[EQT_EV_COUNT + 1] = { 0 };
	struct daos_event	event;
	struct daos_event	child_event;
	bool			ev_flag;
	int			i;
	int			rc;

	DAOS_TEST_ENTRY("3", "parent event");

	print_message("Initialize parent event\n");
	rc = daos_event_init(&event, my_eqh, NULL);
	D_ASSERT(rc == 0);

	print_message("Initialize & launch child events");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		child_events[i] = malloc(sizeof(*child_events[i]));
		if (child_events[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		rc = daos_event_init(child_events[i], DAOS_HDL_INVAL, &event);
		if (rc != 0)
			goto out_free;

		rc = daos_event_launch(child_events[i]);
		if (rc != 0)
			goto out_free;
	}

	print_message("launch parent event\n");
	rc = daos_event_launch(&event);
	if (rc != 0) {
		print_error("Launch parent event returned %d\n", rc);
		goto out_free;
	}

	print_message("Add a child when parent is launched. should fail.\n");
	rc = daos_event_init(&child_event, DAOS_HDL_INVAL, &event);
	if (rc != -DER_INVAL) {
		print_error("Add child to inflight parent should fail (%d)\n",
			    rc);
		goto out_free;
	}

	print_message("Complete parent before children complete\n");
	daos_event_complete(&event, 0);

	print_message("Add a child when parent is completed but not init\n");
	rc = daos_event_init(&child_event, DAOS_HDL_INVAL, &event);
	if (rc != -DER_INVAL) {
		print_error("Add child to inflight parent should fail (%d)\n",
			    rc);
		goto out_free;
	}

	print_message("Poll EQ, Parent should not be polled out of EQ.\n");
	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, 2, eps);
	if (rc != 0) {
		print_error("Expect to get inflight parent event: %d\n", rc);
		rc = -1;
		goto out_free;
	}

	print_message("Test parent completion - should return false\n");
	rc = daos_event_test(&event, DAOS_EQ_NOWAIT, &ev_flag);
	if (rc != 0 || ev_flag != false) {
		print_error("expect to get inflight parent (%d)\n", rc);
		rc = -1;
		goto out_free;
	}

	for (i = 0; i < EQT_EV_COUNT; i++)
		daos_event_complete(child_events[i], 0);

	print_message("Poll parent event\n");
	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, 2, eps);
	if (rc != 1 || eps[0] != &event) {
		print_error("Expect to get completion of parent EV: %d\n", rc);
		rc = -1;
		goto out_free;
	}

	print_message("re-launch child events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		daos_event_fini(child_events[i]);

		rc = daos_event_init(child_events[i], DAOS_HDL_INVAL, &event);
		if (rc != 0)
			goto out_free;

		rc = daos_event_launch(child_events[i]);
		if (rc != 0) {
			print_error("can't launch child event (%d)\n", rc);
			goto out_free;
		}

		if (i >= EQT_EV_COUNT/2)
			daos_event_complete(child_events[i], 0);
	}

	print_message("Insert barrier parent event\n");
	rc = daos_event_parent_barrier(&event);
	if (rc != 0) {
		print_error("Parent barrier event returned %d\n", rc);
		goto out_free;
	}

	print_message("Test on child event - should fail\n");
	rc = daos_event_test(child_events[0], DAOS_EQ_WAIT, NULL);
	if (rc != -DER_NO_PERM) {
		print_error("Test on child event returned %d\n", rc);
		goto out_free;
	}

	print_message("Add an EV when parent is not polled. should fail.\n");
	rc = daos_event_init(&child_event, DAOS_HDL_INVAL, &event);
	if (rc != -DER_INVAL) {
		print_error("Add child to inflight parent should fail (%d)\n",
			    rc);
		goto out_free;
	}

	print_message("Poll EQ, Parent should not be polled out of EQ.\n");
	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, 2, eps);
	if (rc != 0) {
		print_error("Expect to get inflight parent event: %d\n", rc);
		rc = -1;
		goto out_free;
	}

	for (i = 0; i < EQT_EV_COUNT/2; i++)
		daos_event_complete(child_events[i], 0);

	print_message("wait on parent barrier event\n");
	rc = daos_event_test(&event, DAOS_EQ_NOWAIT, &ev_flag);
	if (rc != 0) {
		print_error("Test on barrier event returned %d\n", rc);
		goto out_free;
	}
	if (!ev_flag) {
		print_error("Barrier event should be completed\n");
		rc = -1;
		goto out_free;
	}

	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, 2, eps);
	if (rc != 0) {
		print_error("EQ should be empty: %d\n", rc);
		rc = -1;
		goto out_free;
	}

	daos_event_fini(&event);
	rc = 0;

out_free:
	for (i = 0; i < EQT_EV_COUNT; i++) {
		if (child_events[i] != NULL) {
			free(child_events[i]);
		}
	}
out:
	DAOS_TEST_EXIT(rc);
	return rc;
}

typedef struct {
	pthread_mutex_t	epc_mutex;
	pthread_cond_t	epc_cond;
	unsigned int	epc_error;
	unsigned int	epc_barrier;
	unsigned int	epc_index;
} eq_pc_data_t;

static eq_pc_data_t	epc_data;

#define EQ_TEST_CHECK_EMPTY(eqh, rc, out)			\
do {								\
	if (epc_data.epc_error != 0) {				\
		rc = epc_data.epc_error;			\
		goto out;					\
	}							\
	rc = daos_eq_query(eqh, DAOS_EQR_ALL, 0, NULL);		\
	if (rc == 0) {						\
		print_error("\tProducer verified EQ empty\n");	\
		break;						\
	}							\
	print_error("\tQuery should return 0 but not: %d\n", rc);	\
	D_MUTEX_LOCK(&epc_data.epc_mutex);		\
	epc_data.epc_error = rc;				\
	pthread_cond_broadcast(&epc_data.epc_cond);		\
	D_MUTEX_UNLOCK(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_BARRIER(msg, out)				\
do {								\
	D_MUTEX_LOCK(&epc_data.epc_mutex);		\
	if (epc_data.epc_error != 0) {				\
		D_MUTEX_UNLOCK(&epc_data.epc_mutex);	\
		goto out;					\
	}							\
	epc_data.epc_barrier++;					\
	if (epc_data.epc_barrier == 1) {			\
		pthread_cond_wait(&epc_data.epc_cond,		\
				  &epc_data.epc_mutex);		\
	} else {						\
		pthread_cond_broadcast(&epc_data.epc_cond);	\
		epc_data.epc_barrier = 0;			\
		epc_data.epc_index++;				\
	}							\
	print_error(msg);						\
	D_MUTEX_UNLOCK(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_DONE(rc)					\
do {								\
	D_MUTEX_LOCK(&epc_data.epc_mutex);		\
	if (epc_data.epc_error == 0 && rc != 0)			\
		epc_data.epc_error = rc;			\
	pthread_cond_broadcast(&epc_data.epc_cond);		\
	D_MUTEX_UNLOCK(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_CHECK_SLEEP(name, then, intv, rc, out)		\
do {								\
	struct timeval	__now;					\
	unsigned int	__intv;					\
								\
	gettimeofday(&__now, NULL);				\
	__intv = (int)(__now.tv_sec - (then).tv_sec);		\
	if (__intv >= (intv) - 1) {				\
		print_error("\t%s slept for %d seconds\n",		\
			name, __intv);				\
		break;						\
	}							\
	print_error("%s should sleep for %d seconds not %d\n",	\
		name, intv, __intv);				\
	rc = -1;						\
	goto out;						\
} while (0)

static void *
eq_test_consumer(void *arg)
{
	struct daos_event	**evpps;
	struct timeval		then;
	int			total;
	int			rc = 0;

	EQ_TEST_BARRIER("EQ Consumer started\n", out);

	evpps = malloc(EQT_EV_COUNT * sizeof(*evpps));
	if (evpps == NULL) {
		rc = ENOMEM;
		goto out;
	}

	/* step-1 */
	print_message("\tConsumer should be blocked for %d seconds\n",
		      EQT_SLEEP_INV);
	gettimeofday(&then, NULL);

	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			print_error("EQ poll returned error: %d\n", rc);
			goto out;
		}
		total += rc;
	}
	EQ_TEST_CHECK_SLEEP("Consumer", then, EQT_SLEEP_INV, rc, out);

	print_message("\tConsumer got %d events\n", EQT_EV_COUNT);
	EQ_TEST_BARRIER("\tConsumer wake up producer for the next step\n", out);

	/* step-2 */
	EQ_TEST_BARRIER("\tConsumer wait for producer completing event\n", out);
	gettimeofday(&then, NULL);

	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 1, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			print_error("EQ poll returned error: %d\n", rc);
			goto out;
		}
		total += rc;
	}

	EQ_TEST_CHECK_SLEEP("Consumer", then, EQT_SLEEP_INV, rc, out);
	print_message("\tConsumer got %d events\n", EQT_EV_COUNT);
	EQ_TEST_BARRIER("\tConsumer wake up producer\n", out);

	/* step-3 */
	EQ_TEST_BARRIER("\tConsumer races with producer and tries "
			"to poll event\n", out);
	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			print_error("EQ poll returned error: %d\n", rc);
			goto out;
		}
		total += rc;
	}
	rc = 0;
	EQ_TEST_BARRIER("\tConsumer get all events\n", out);
out:
	EQ_TEST_DONE(rc);
	pthread_exit((void *)0);
}

static int
eq_test_4()
{
	struct daos_event	*events[EQT_EV_COUNT*3 + 1] = { 0 };
	pthread_t		thread;
	int			step = 0;
	int			rc;
	int			i;

	DAOS_TEST_ENTRY("4", "Producer & Consumer");

	for (i = 0; i < EQT_EV_COUNT * 3; i++) {
		events[i] = malloc(sizeof(*events[i]));
		if (events[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		daos_event_init(events[i], my_eqh, NULL);
	}

	pthread_cond_init(&epc_data.epc_cond, NULL);
	rc = D_MUTEX_INIT(&epc_data.epc_mutex, NULL);
	if (rc != 0)
		goto out;

	rc = pthread_create(&thread, NULL, eq_test_consumer, NULL);
	if (rc != 0)
		goto out;

	EQ_TEST_BARRIER("EQ Producer started\n", out);
	print_message("Step-1: launch & complete %d events\n", EQT_EV_COUNT);

	print_message("\tProducer sleep for %d seconds and block consumer\n",
		EQT_SLEEP_INV);
	sleep(EQT_SLEEP_INV);

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i]);
		if (rc != 0)
			goto out;
	}

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++)
		daos_event_complete(events[i], 0);

	EQ_TEST_BARRIER("\tProducer is waiting for consumer draning EQ\n", out);
	EQ_TEST_CHECK_EMPTY(my_eqh, rc, out);

	step++;
	print_message("Step-2: launch %d events, sleep for %d seconds and "
		"complete these events\n", EQT_EV_COUNT, EQT_SLEEP_INV);
	print_message("\tProducer launch %d events\n", EQT_EV_COUNT);
	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i]);
		if (rc != 0)
			goto out;
	}

	EQ_TEST_BARRIER("\tProducer wakes up consumer and sleep\n", out);
	sleep(EQT_SLEEP_INV);

	print_message("\tProducer complete %d events after %d seconds\n",
		EQT_EV_COUNT, EQT_SLEEP_INV);

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++)
		daos_event_complete(events[i], 0);

	EQ_TEST_BARRIER("\tProducer is waiting for EQ draining\n", out);
	EQ_TEST_CHECK_EMPTY(my_eqh, rc, out);

	step++;
	print_message("Step-3: Producer launch & complete %d events, "
		"race with consumer\n", EQT_EV_COUNT);

	EQ_TEST_BARRIER("\tProducer launch and complete all events\n", out);
	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i]);
		if (rc != 0)
			goto out;
	}

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++)
		daos_event_complete(events[i], 0);

	EQ_TEST_BARRIER("\tProducer is waiting for EQ draining\n", out);
	EQ_TEST_CHECK_EMPTY(my_eqh, rc, out);

out:
	EQ_TEST_DONE(rc);
	DAOS_TEST_EXIT(rc);

	D_MUTEX_DESTROY(&epc_data.epc_mutex);
	pthread_cond_destroy(&epc_data.epc_cond);

	for (i = 0; i < EQT_EV_COUNT * 3; i++) {
		if (events[i] != NULL) {
			daos_event_fini(events[i]);
			free(events[i]);
		}
	}

	return epc_data.epc_error;
}

static int
eq_test_5()
{
	struct daos_event	*eps[EQT_EV_COUNT + 1] = { 0 };
	struct daos_event	*events[EQT_EV_COUNT + 1] = { 0 };
	bool			ev_flag;
	int			rc;
	int			i;

	DAOS_TEST_ENTRY("5", "Event Test & Poll");

	for (i = 0; i < EQT_EV_COUNT; i++) {
		events[i] = malloc(sizeof(*events[i]));
		if (events[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		rc = daos_event_init(events[i], my_eqh, NULL);
		if (rc != 0)
			goto out;
	}

	print_message("Launch and test inflight events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		rc = daos_event_launch(events[i]);
		if (rc != 0) {
			print_error("Failed to launch event %d: %d\n", i, rc);
			goto out;
		}

		/** Complete half the events */
		if (i > EQT_EV_COUNT/2) {
			daos_event_complete(events[i], 0);

			/** Test completion which polls them out of the EQ */
			rc = daos_event_test(events[i], DAOS_EQ_NOWAIT,
					     &ev_flag);
			if (rc != 0) {
				print_error("Test on child event returns %d\n",
					    rc);
				goto out;
			}
			if (!ev_flag) {
				print_error("EV %d should be completed\n", i);
				rc = -1;
				goto out;
			}
		} else {
			rc = daos_event_test(events[i], DAOS_EQ_NOWAIT,
					     &ev_flag);
			if (rc != 0) {
				print_error("Test on child event returns %d\n",
					    rc);
				goto out;
			}
			if (ev_flag) {
				print_error("Event %d should be inflight\n", i);
				rc = -1;
				goto out;
			}
		}
	}

	print_message("Poll EQ with 1/2 the events\n");
	rc = daos_eq_poll(my_eqh, 1, 10, EQT_EV_COUNT/2, eps);
	if (rc != 0) {
		print_error("Expect to poll zero event: %d\n", rc);
		rc = -1;
		goto out;
	}

	print_message("Query EQ with completion events\n");
	for (i = 0; i < EQT_EV_COUNT/2; i++) {
		daos_event_complete(events[i], 0);
		rc = daos_eq_query(my_eqh, DAOS_EQR_COMPLETED,
				   EQT_EV_COUNT, eps);
		if (rc != i + 1) {
			print_error("Expected %d inflight event, but got %d\n",
				    i + 1, rc);
			rc = -1;
			goto out;
		}

		if (eps[rc - 1] != events[i]) {
			print_error("Unexpected result from query: %d %p %p\n",
				    i, eps[rc - 1], &events[i]);
			rc = -1;
			goto out;
		}
	}

	print_message("Poll EQ with completion events\n");
	rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, eps);
	if (rc != EQT_EV_COUNT/2) {
		print_error("Expect to poll %d event: %d\n",
			EQT_EV_COUNT, rc);
		rc = -1;
		goto out;
	}
	rc = 0;
out:
	for (i = 0; i < EQT_EV_COUNT; i++) {
		if (events[i] != NULL) {
			daos_event_fini(events[i]);
			free(events[i]);
		}
	}
	DAOS_TEST_EXIT(rc);
	return rc;
}

static int
eq_test_6()
{
	static daos_handle_t	eqh[EQ_COUNT];
	struct daos_event	*eps[EQ_COUNT][EQT_EV_COUNT];
	struct daos_event	*events[EQ_COUNT][EQT_EV_COUNT];
	bool			ev_flag;
	int			rc;
	int			i, j;

	DAOS_TEST_ENTRY("6", "Multiple EQs");

	print_message("Create EQs and initialize events.\n");
	for (i = 0; i < EQ_COUNT; i++) {
		rc = daos_eq_create(&eqh[i]);
		if (rc != 0) {
			print_error("Failed to create EQ: %d\n", rc);
			return rc;
		}

		for (j = 0; j < EQT_EV_COUNT; j++) {
			events[i][j] = malloc(sizeof(*events[i][j]));
			if (events[i][j] == NULL) {
				rc = -ENOMEM;
				goto out_eq;
			}
			rc = daos_event_init(events[i][j], eqh[i], NULL);
			if (rc != 0)
				goto out_eq;
		}
	}

	print_message("Launch and test inflight events\n");
	for (j = 0; j < EQT_EV_COUNT; j++) {
		for (i = 0; i < EQ_COUNT; i++) {
			rc = daos_event_launch(events[i][j]);
			if (rc != 0) {
				print_error("Failed to launch event %d: %d\n",
					    j, rc);
				goto out_ev;
			}

			/** Complete half the events */
			if (i > EQT_EV_COUNT/2) {
				daos_event_complete(events[i][j], 0);

				/** Test completion */
				rc = daos_event_test(events[i][j],
						     DAOS_EQ_NOWAIT, &ev_flag);
				if (rc != 0) {
					print_error("Test returns %d\n", rc);
					goto out_ev;
				}
				if (!ev_flag) {
					print_error("EV should be completed\n");
					rc = -1;
					goto out_ev;
				}
			} else {
				rc = daos_event_test(events[i][j],
						     DAOS_EQ_NOWAIT, &ev_flag);
				if (rc != 0) {
					print_error("Test returns %d\n", rc);
					goto out_ev;
				}
				if (ev_flag) {
					print_error("EV Should be inflight\n");
					rc = -1;
					goto out_ev;
				}
			}
		}
	}

	print_message("Poll EQs with 1/2 the events\n");
	for (i = 0; i < EQ_COUNT; i++) {
		rc = daos_eq_poll(eqh[i], 1, 10, EQT_EV_COUNT/2, eps[i]);
		if (rc != 0) {
			print_error("Expect to poll zero event: %d\n", rc);
			rc = -1;
			goto out_ev;
		}
	}

	print_message("Complete events\n");
	for (j = 0; j < EQT_EV_COUNT/2; j++)
		for (i = 0; i < EQ_COUNT; i++)
			daos_event_complete(events[i][j], 0);

	print_message("Poll EQ with completion events\n");
	for (i = 0; i < EQ_COUNT; i++) {
		rc = daos_eq_poll(eqh[i], 0, -1, EQT_EV_COUNT, eps[i]);
		if (rc != EQT_EV_COUNT/2) {
			print_error("Expect to poll %d event: %d\n",
				EQT_EV_COUNT, rc);
			rc = -1;
			goto out_ev;
		}
	}
	rc = 0;

out_ev:
	for (i = 0; i < EQ_COUNT; i++) {
		for (j = 0; j < EQT_EV_COUNT; j++) {
			if (events[i][j] != NULL) {
				daos_event_fini(events[i][j]);
				free(events[i][j]);
			}
		}
	}
out_eq:
	for (i = 0; i < EQ_COUNT; i++)
		daos_eq_destroy(eqh[i], 1);

	DAOS_TEST_EXIT(rc);
	return rc;
}

static int
eq_test_7()
{
	struct daos_event	*child_events[EQT_EV_COUNT];
	struct daos_event	*events[EQT_EV_COUNT];
	bool			ev_flag;
	int			rc;
	int			i;

	DAOS_TEST_ENTRY("7", "Events with no EQ");

	print_message("Initialize & launch parent and child events.\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		events[i] = malloc(sizeof(*events[i]));
		if (events[i] == NULL)
			return -ENOMEM;
		child_events[i] = malloc(sizeof(*child_events[i]));
		if (child_events[i] == NULL)
			return -ENOMEM;

		rc = daos_event_init(events[i], DAOS_HDL_INVAL, NULL);
		if (rc != 0)
			goto out_free;
		rc = daos_event_init(child_events[i], DAOS_HDL_INVAL,
				     events[i]);
		if (rc != 0)
			goto out_free;

		rc = daos_event_launch(child_events[i]);
		if (rc != 0)
			goto out_free;
		rc = daos_event_launch(events[i]);
		if (rc != 0)
			goto out_free;
	}

	print_message("Test events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		rc = daos_event_test(events[i], DAOS_EQ_NOWAIT, &ev_flag);
		if (rc != 0) {
			print_error("Test returns %d\n", rc);
			goto out_free;
		}
		if (ev_flag) {
			print_error("Event should be inflight\n");
			rc = -1;
			goto out_free;
		}
	}

	print_message("Complete Child & Parent events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		daos_event_complete(child_events[i], 0);

		rc = daos_event_test(events[i], DAOS_EQ_NOWAIT, &ev_flag);
		if (rc != 0) {
			print_error("Test returns %d\n", rc);
			goto out_free;
		}
		if (ev_flag) {
			print_error("Parent Event should still be inflight\n");
			rc = -1;
			goto out_free;
		}

		daos_event_complete(events[i], 0);

		/** Test completion */
		rc = daos_event_test(events[i], DAOS_EQ_NOWAIT, &ev_flag);
		if (rc != 0) {
			print_error("Test returns %d\n", rc);
			goto out_free;
		}
		if (!ev_flag) {
			print_error("Event should be completed\n");
			rc = -1;
			goto out_free;
		}
	}

	rc = 0;

out_free:
	for (i = 0; i < EQT_EV_COUNT; i++) {
		if (child_events[i] != NULL) {
			daos_event_fini(child_events[i]);
			free(child_events[i]);
		}
		if (events[i] != NULL) {
			daos_event_fini(events[i]);
			free(events[i]);
		}
	}
	DAOS_TEST_EXIT(rc);
	return rc;
}

static int
inc_cb(void *udata, daos_event_t *ev, int ret)
{
	int *num = (int *)udata;

	D_ASSERT(ret == 0);
	*num = 999;

	return 0;
}

static int
eq_test_8()
{
	struct daos_event	*ep;
	struct daos_event	ev;
	int			*udata;
	int			rc = 0;

	DAOS_TEST_ENTRY("8", "Event Completion Callback");

	rc = daos_event_init(&ev, my_eqh, NULL);
	if (rc) {
		print_error("daos_event_init() failed (%d)\n", rc);
		goto out;
	}

	D_ALLOC_ARRAY(udata, 1);
	D_ASSERT(udata != NULL);
	*udata = 0;

	rc = daos_event_register_comp_cb(&ev, inc_cb, udata);
	if (rc) {
		print_error("daos_event_register_comp_cb() failed (%d)\n", rc);
		goto out;
	}

	rc = daos_event_launch(&ev);
	if (rc) {
		print_error("daos_event_launch() failed (%d)\n", rc);
		goto out;
	}

	daos_event_complete(&ev, 0);
	if (*udata != 999) {
		print_error("invalid udata value (%d)\n", *udata);
		rc =  -DER_INVAL;
		goto out;
	}

	rc = daos_eq_poll(my_eqh, 0, 0, 1, &ep);
	if (rc != 1) {
		print_error("Failed to drain EQ: %d\n", rc);
		goto out;
	}
	rc = 0;

	daos_event_fini(&ev);
out:
	DAOS_TEST_EXIT(rc);
	return rc;
}

int
main(int argc, char **argv)
{
	int		test_fail = 0;
	int		rc;

	d_register_alt_assert(mock_assert);

	setenv("OFI_INTERFACE", "lo", 1);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0) {
		print_error("Failed daos_debug_init: %d\n", rc);
		return rc;
	}

	rc = daos_hhash_init();
	if (rc != 0) {
		print_error("Failed daos_hhash_init: %d\n", rc);
		goto out_debug;
	}

	rc = daos_eq_lib_init();
	if (rc != 0) {
		print_error("Failed daos_eq_lib_init: %d\n", rc);
		goto out_hhash;
	}

	rc = daos_eq_create(&my_eqh);
	if (rc != 0) {
		print_error("Failed daos_eq_create: %d\n", rc);
		goto out_lib;
	}

	rc = eq_test_1();
	if (rc != 0) {
		print_error("EQ TEST 1 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_2();
	if (rc != 0) {
		print_error("EQ TEST 2 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_3();
	if (rc != 0) {
		print_error("EQ TEST 3 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_4();
	if (rc != 0) {
		print_error("EQ TEST 4 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_5();
	if (rc != 0) {
		print_error("EQ TEST 5 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_6();
	if (rc != 0) {
		print_error("EQ TEST 6 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_7();
	if (rc != 0) {
		print_error("EQ TEST 7 failed: %d\n", rc);
		test_fail++;
	}

	rc = eq_test_8();
	if (rc != 0) {
		print_error("EQ TEST 8 failed: %d\n", rc);
		test_fail++;
	}

	if (test_fail)
		print_error("ERROR, %d test(s) failed\n", test_fail);
	else
		print_message("SUCCESS, all tests passed\n");

	daos_eq_destroy(my_eqh, 1);
out_lib:
	daos_eq_lib_fini();
out_hhash:
	daos_hhash_fini();
out_debug:
	daos_debug_fini();
	return test_fail;
}
