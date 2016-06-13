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
 * This file is for testing DAOS event queue
 *
 * common/tests/eq_tests.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */
#include <pthread.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/list.h>
#include <daos/hash.h>

/* XXX For the testing purpose, this test case will use
 * some internal api of event queue, and for real use
 * cases, this daos_eq_internal should not be exposed */
#include <event_internal.h>

#define EQT_EV_COUNT		10000
#define EQT_SLEEP_INV		2

#define DAOS_TEST_FMT	"-------- %s test_%s: %s\n"

#define DAOS_TEST_ENTRY(test_id, test_name)	\
	D_ERROR(DAOS_TEST_FMT, "EQ", test_id, test_name)

#define DAOS_TEST_EXIT(rc)					\
do {								\
	if (rc == 0)						\
		D_ERROR("-------- PASS\n");			\
	else							\
		D_ERROR("-------- FAILED\n");			\
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

	D_ERROR("Create EQ\n");
	rc = daos_eq_create(&eqh);
	if (rc != 0) {
		D_ERROR("Failed to create EQ: %d\n", rc);
		goto out;
	}

	rc = daos_event_init(&ev, eqh, NULL);
	D_ASSERT(rc == 0);

	rc = daos_event_launch(&ev, NULL, NULL);
	D_ASSERT(rc == 0);

	daos_event_complete(&ev, 0);

	rc = daos_event_init(&abort_ev, eqh, NULL);
	D_ASSERT(rc == 0);

	rc = daos_event_launch(&abort_ev, NULL, NULL);
	D_ASSERT(rc == 0);

	daos_event_abort(&abort_ev);

	D_ERROR("Destroy non-empty EQ\n");
	rc = daos_eq_destroy(eqh, 0);
	if (rc != -EBUSY) {
		D_ERROR("Failed to destroy non-empty EQ: %d\n", rc);
		goto out;
	}

	rc = daos_eq_poll(eqh, 0, 0, 1, &ep);
	if (rc != 1) {
		D_ERROR("Failed to drain EQ: %d\n", rc);
		goto out;
	}
	daos_event_fini(&ev);
	daos_event_fini(&abort_ev);

	D_ERROR("Destroy empty EQ\n");
	rc = daos_eq_destroy(eqh, 0);
	if (rc != 0) {
		D_ERROR("Failed to destroy empty EQ: %d\n", rc);
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

	D_ERROR("Poll empty EQ w/o wait\n");
	rc = daos_eq_poll(my_eqh, 0, DAOS_EQ_NOWAIT, EQT_EV_COUNT, eps);
	if (rc != 0) {
		D_ERROR("Expect to poll zero event: %d\n", rc);
		goto out;
	}

	D_ERROR("Query EQ with inflight events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		rc = daos_event_launch(events[i], NULL, NULL);
		if (rc != 0) {
			D_ERROR("Failed to launch event %d: %d\n", i, rc);
			goto out;
		}

		rc = daos_eq_query(my_eqh, DAOS_EQR_DISPATCH, 0, NULL);
		if (rc != i + 1) {
			D_ERROR("Expect to see %d inflight event, "
				 "but got %d\n", i + 1, rc);
			rc = -1;
			goto out;
		}
	}

	D_ERROR("Poll empty EQ with timeout\n");
	rc = daos_eq_poll(my_eqh, 1, 10, EQT_EV_COUNT, eps);
	if (rc != -ETIMEDOUT) {
		D_ERROR("Expect to poll zero event: %d\n", rc);
		goto out;
	}

	D_ERROR("Query EQ with completion events\n");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		daos_event_complete(events[i], 0);
		rc = daos_eq_query(my_eqh, DAOS_EQR_COMPLETED,
				   EQT_EV_COUNT, eps);
		if (rc != i + 1) {
			D_ERROR("Expect to see %d inflight event, "
				 "but got %d\n", i + 1, rc);
			rc = -1;
			goto out;
		}

		if (eps[rc - 1] != events[i]) {
			D_ERROR("Unexpected results from query: %d %p %p\n", i,
				 eps[rc - 1], &events[i]);
			rc = -1;
			goto out;
		}
	}

	D_ERROR("Poll EQ with completion events\n");
	rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, eps);
	if (rc != EQT_EV_COUNT) {
		D_ERROR("Expect to poll %d event: %d\n",
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
	int			rc;
	int			i;

	DAOS_TEST_ENTRY("3", "parent event");

	D_ERROR("Initialize events with parent\n");
	rc = daos_event_init(&event, my_eqh, NULL);
	D_ASSERT(rc == 0);

	for (i = 0; i < EQT_EV_COUNT; i++) {
		child_events[i] = malloc(sizeof(*child_events[i]));
		if (child_events[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		rc = daos_event_init(child_events[i], my_eqh, &event);
		if (rc != 0)
			goto out;
	}

	D_ERROR("launch parent events\n");
	/* try to launch parent event, should always fail */
	rc = daos_event_launch(&event, NULL, NULL);
	if (rc != -DER_NO_PERM) {
		D_ERROR("Launch parent event returned %d\n", rc);
		goto out_free;
	}

	D_ERROR("launch child events");
	for (i = 0; i < EQT_EV_COUNT; i++) {
		rc = daos_event_launch(child_events[i], NULL, NULL);
		if (rc != 0)
			goto out_free;
	}

	for (i = 0; i < EQT_EV_COUNT; i++)
		daos_event_complete(child_events[i], 0);

	D_ERROR("Poll parent event\n");
	rc = daos_eq_poll(my_eqh, 0, 0, 2, eps);
	if (rc != 1 || eps[0] != &event) {
		D_ERROR("Expect to get completion of parent event: %d\n", rc);
		rc = -1;
		goto out;
	}

	rc = 0;
out_free:
	for (i = 0; i < EQT_EV_COUNT; i++) {
		if (child_events[i] != NULL) {
			daos_event_fini(child_events[i]);
			free(child_events[i]);
		}
	}
out:
	daos_event_fini(&event);
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
		D_ERROR("\tProducer verified EQ empty\n");	\
		break;						\
	}							\
	D_ERROR("\tQuery should return 0 but not: %d\n", rc);	\
	pthread_mutex_lock(&epc_data.epc_mutex);		\
	epc_data.epc_error = rc;				\
	pthread_cond_broadcast(&epc_data.epc_cond);		\
	pthread_mutex_unlock(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_BARRIER(msg, out)				\
do {								\
	pthread_mutex_lock(&epc_data.epc_mutex);		\
	if (epc_data.epc_error != 0) {				\
		pthread_mutex_unlock(&epc_data.epc_mutex);	\
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
	D_ERROR(msg);						\
	pthread_mutex_unlock(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_DONE(rc)					\
do {								\
	pthread_mutex_lock(&epc_data.epc_mutex);		\
	if (epc_data.epc_error == 0 && rc != 0)			\
		epc_data.epc_error = rc;			\
	pthread_cond_broadcast(&epc_data.epc_cond);		\
	pthread_mutex_unlock(&epc_data.epc_mutex);		\
} while (0)

#define EQ_TEST_CHECK_SLEEP(name, then, intv, rc, out)		\
do {								\
	struct timeval	__now;					\
	unsigned int	__intv;					\
								\
	gettimeofday(&__now, NULL);				\
	__intv = (int)(__now.tv_sec - (then).tv_sec);		\
	if (__intv >= (intv) - 1) {				\
		D_ERROR("\t%s slept for %d seconds\n",		\
			name, __intv);				\
		break;						\
	}							\
	D_ERROR("%s should sleep for %d seconds not %d\n",	\
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
	D_ERROR("\tConsumer should be blocked for %d seconds\n", EQT_SLEEP_INV);
	gettimeofday(&then, NULL);

	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			D_ERROR("EQ poll returned error: %d\n", rc);
			goto out;
		}
		total += rc;
	}
	EQ_TEST_CHECK_SLEEP("Consumer", then, EQT_SLEEP_INV, rc, out);

	D_ERROR("\tConsumer got %d events\n", EQT_EV_COUNT);
	EQ_TEST_BARRIER("\tConsumer wake up producer for the next step\n", out);

	/* step-2 */
	EQ_TEST_BARRIER("\tConsumer wait for producer completing event\n", out);
	gettimeofday(&then, NULL);

	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 1, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			D_ERROR("EQ poll returned error: %d\n", rc);
			goto out;
		}
		total += rc;
	}

	EQ_TEST_CHECK_SLEEP("Consumer", then, EQT_SLEEP_INV, rc, out);
	D_ERROR("\tConsumer got %d events\n", EQT_EV_COUNT);
	EQ_TEST_BARRIER("\tConsumer wake up producer\n", out);

	/* step-3 */
	EQ_TEST_BARRIER("\tConsumer races with producer and tries "
			"to poll event\n", out);
	for (total = 0; total < EQT_EV_COUNT; ) {
		rc = daos_eq_poll(my_eqh, 0, -1, EQT_EV_COUNT, evpps);
		if (rc < 0) {
			D_ERROR("EQ poll returned error: %d\n", rc);
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
	pthread_mutex_init(&epc_data.epc_mutex, NULL);

	rc = pthread_create(&thread, NULL, eq_test_consumer, NULL);
	if (rc != 0)
		goto out;

	EQ_TEST_BARRIER("EQ Producer started\n", out);
	D_ERROR("Step-1: launch & complete %d events\n", EQT_EV_COUNT);

	D_ERROR("\tProducer sleep for %d seconds and block consumer\n",
		EQT_SLEEP_INV);
	sleep(EQT_SLEEP_INV);

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i], NULL, NULL);
		if (rc != 0)
			goto out;
	}

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++)
		daos_event_complete(events[i], 0);

	EQ_TEST_BARRIER("\tProducer is waiting for consumer draning EQ\n", out);
	EQ_TEST_CHECK_EMPTY(my_eqh, rc, out);

	step++;
	D_ERROR("Step-2: launch %d events, sleep for %d seconds and "
		"complete these events\n", EQT_EV_COUNT, EQT_SLEEP_INV);
	D_ERROR("\tProducer launch %d events\n", EQT_EV_COUNT);
	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i], NULL, NULL);
		if (rc != 0)
			goto out;
	}

	EQ_TEST_BARRIER("\tProducer wakes up consumer and sleep\n", out);
	sleep(EQT_SLEEP_INV);

	D_ERROR("\tProducer complete %d events after %d seconds\n",
		EQT_EV_COUNT, EQT_SLEEP_INV);

	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++)
		daos_event_complete(events[i], 0);

	EQ_TEST_BARRIER("\tProducer is waiting for EQ draining\n", out);
	EQ_TEST_CHECK_EMPTY(my_eqh, rc, out);

	step++;
	D_ERROR("Step-3: Producer launch & complete %d events, "
		"race with consumer\n", EQT_EV_COUNT);

	EQ_TEST_BARRIER("\tProducer launch and complete all events\n", out);
	for (i = EQT_EV_COUNT * step; i < EQT_EV_COUNT * (step + 1); i++) {
		rc = daos_event_launch(events[i], NULL, NULL);
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

	pthread_mutex_destroy(&epc_data.epc_mutex);
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
grp_comp(void *args, int rc)
{
	D_PRINT("group completed\n");
	return 0;
}

#define GRP_SIZE	1000

static int
eq_test_5(void)
{
	struct daos_event	*evps[GRP_SIZE];
	struct daos_oper_grp	*grp;
	daos_event_t		 ev;
	int			 rc;
	int			 i;

	DAOS_TEST_ENTRY("5", "operation group");

	rc = daos_event_init(&ev, my_eqh, NULL);
	if (rc != 0) {
		D_ERROR("failed 1\n");
		return rc;
	}

	rc = daos_oper_grp_create(&ev, grp_comp, NULL, &grp);
	D_ASSERTF(rc == 0, "rc = %d\n", rc);

	for (i = 0; i < GRP_SIZE; i++) {
		rc = daos_oper_grp_new_ev(grp, &evps[i]);
		D_ASSERTF(rc == 0, "rc = %d\n", rc);

		rc = daos_event_launch(evps[i], NULL, NULL);
		D_ASSERTF(rc == 0, "rc = %d\n", rc);
	}

	D_PRINT("Launch oper group now\n");
	rc = daos_oper_grp_launch(grp);
	D_ASSERTF(rc == 0, "rc = %d\n", rc);

	for (i = 0; i < GRP_SIZE; i++)
		daos_event_complete(evps[i], 0);

	D_PRINT("Poll empty EQ with timeout\n");
	rc = daos_eq_poll(my_eqh, 1, 1, GRP_SIZE, evps);
	D_ASSERTF(rc == 1, "rc = %d\n", rc);

	rc = daos_event_fini(&ev);
	DAOS_TEST_EXIT(rc);
	return rc;
}


int
main(int argc, char **argv)
{
	int		rc;

	rc = daos_eq_lib_init(NULL);
	if (rc != 0) {
		D_ERROR("Failed to initailiz DAOS/event library: %d\n", rc);
		return rc;
	}

	rc = daos_eq_create(&my_eqh);
	if (rc != 0) {
		D_ERROR("Failed to create EQ: %d\n", rc);
		goto out_lib;
	}

	rc = eq_test_1();
	if (rc != 0)
		goto failed;

	rc = eq_test_2();
	if (rc != 0)
		goto failed;

	rc = eq_test_3();
	if (rc != 0)
		goto failed;

	rc = eq_test_4();
	if (rc != 0)
		goto failed;

	rc = eq_test_5();
	if (rc != 0)
		goto failed;
failed:
	daos_eq_destroy(my_eqh, 1);
out_lib:
	daos_eq_lib_fini();

	return rc;
}
