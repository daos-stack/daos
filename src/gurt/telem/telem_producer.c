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
/*
 * This file shows an example of using the telemetry API to produce metrics
 */

#include <stdio.h>
#include <time.h>
#include "gurt/common.h"
#include "gurt/telemetry.h"

/*
 * A sample function that creates and incremements a metric for a loop counter
 */
void test_function1(int count)
{
	static struct d_tm_node_t *loop;
	int rc;
	int i;

	for (i = 0; i < count - 1; i++) {
		rc = d_tm_increment_counter(&loop, __FILE__, __func__,
					    "loop counter", NULL);
		if (rc != 0) {
			return;
		}
	}

	/*
	 * Demonstrates how the metric is accessed by the initialized pointer.
	 * No name is required.  The API uses the initialized pointer if it is
	 * provided, and only uses the name if the pointer doesn't reference
	 * anything.
	 */
	rc = d_tm_increment_counter(&loop, NULL);


}

/*
 * A sample function that creates and records a timestamp when this function is
 * called.
 */
void test_function2(void)
{
	static struct d_tm_node_t *ts;

	d_tm_record_timestamp(&ts, __FILE__, __func__, "last executed", NULL);
}

/*
 * A sample function that shows how a gauge is incremented, say when opening
 * a handle.
 */
void test_open_handle(void)
{
	static struct d_tm_node_t *numOpenHandles;

	/*
	 * Create / use a gauge at a known location so that it can be used by
	 * test_close_handle() without sharing pointers.  The gauge can be
	 * incremented by an arbitrary value.  We will incrememt by one for this
	 * example.
	 */
	d_tm_increment_gauge(&numOpenHandles, 1, __FILE__, "open handles",
			     NULL);
}

/*
 * A sample function that shows how a gauge is decremented, say when closing
 * a handle.  It uses the same gauge as the one referenced in test_open_handle()
 */
void test_close_handle(void)
{
	static struct d_tm_node_t *numOpenHandles;

	/*
	 * The full name of this gauge matches the name in test_open_handle() so
	 * that increments in test_open_handle() are changing the same metric
	 * as the one used here.
	 */
	d_tm_decrement_gauge(&numOpenHandles, 1, __FILE__, "open handles",
			     NULL);
}

/*
 * Shows use of the high resolution timer.  It allows the developer to take
 * high resolution timer snapshots at various places within their code, which
 * can then be interpreted depending on the need.  A duration type metric
 * is a simplified version of this metric that does the interval calculation.
 */
void highres_timer(void)
{
	static struct d_tm_node_t *t1;
	static struct d_tm_node_t *t2;
	static struct d_tm_node_t *t3;
	static struct d_tm_node_t *t4;
	struct timespec ts;

	d_tm_record_high_res_timer(&t1, __FILE__, __func__, "timer 1", NULL);

	/*
	 * Do some stuff
	 */
	sleep(1);

	d_tm_record_high_res_timer(&t2, __FILE__, __func__, "timer 2", NULL);

	/*
	 * Do some stuff
	 */
	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	nanosleep(&ts, NULL);

	d_tm_record_high_res_timer(&t3, __FILE__, __func__, "timer 3", NULL);


	/*
	 * Do some stuff (10x longer)
	 */
	ts.tv_sec = 0;
	ts.tv_nsec = 500000000;
	nanosleep(&ts, NULL);


	d_tm_record_high_res_timer(&t4, __FILE__, __func__, "timer 4", NULL);

	/*
	 * How long did the sleep(1) take?  That's timer 2 - timer 1
	 * How long did the 50000 iterations take?  That's timer 3 - timer 2
	 * How long did the 500000 iterations take?  That's timer 4 - timer 3.
	 * How long did the sleep(1) and the 50000 iterations take?  That's
	 * timer 3 - timer 1.
	 * When was function entry?  That's timer 1.
	 * When did the function exit the sleep(1)?  That's t timer 2.
	 */
}


int
main(int argc, char **argv)
{
	static struct d_tm_node_t *entry;
	static struct d_tm_node_t *loop;
	static struct d_tm_node_t *timer1;
	static struct d_tm_node_t *timer2;
	int rc;
	int simulatedRank = 0;
	int i;

	if (argc < 2) {
		printf("Specify an integer that identifies this producer's "
		       "rank.  Specify the same value to the consumer.\n");
		exit(0);
	}

	simulatedRank = atoi(argv[1]);
	printf("This simulatedRank has ID: %d\n", simulatedRank);

	/*
	 * Call d_tm_init() only once per process,
	 * i.e. in iosrv/init.c/server_init()
	 */
	rc = d_tm_init(simulatedRank, D_TM_SHARED_MEMORY_SIZE);
	if (rc != 0) {
		D_GOTO(failure, rc);
	}

	/*
	 * The API is ready to use.  Add a counter that will be identified in
	 * the tree by this file name, function name, and the name "sample
	 * counter", i.e.:
	 * "src/gurt/telem/telem_producer.c/main/sample counter"
	 *
	 * On the first call through, the pointer to this metric is NULL, and
	 * the API looks up the metric by name.  It won't find it, so it creates
	 * it.  The counter is created, and incremented by one.  It now has the
	 * value 1.
	 */
	rc = d_tm_increment_counter(&entry, __FILE__, __func__,
				    "sample counter", NULL);
	if (rc != 0) {
		D_GOTO(failure, rc);
	}

	/*
	 * Increment another counter in a loop.
	 * On the first iteration, the API finds the metric by name and
	 * initializes 'loop'.  On subsequent iterations, the pointer is used
	 * for faster lookup.
	 */
	for (i = 0; i < 1000; i++) {
		rc = d_tm_increment_counter(&loop, __FILE__, __func__,
					    "loop counter", NULL);
		if (rc != 0) {
			D_GOTO(failure, rc);
		}
	}

	/*
	 * How long does it take to execute test_function()?
	 * We can create duration timers that use the system-wide realtime
	 * clock (D_TM_CLOCK_REALTIME), a high resolution timer provided for
	 * each process (D_TM_CLOCK_PROCESS_CPUTIME) or a high resolution timer
	 * for each thread (D_TM_CLOCK_THREAD_CPUTIME)
	 */

	/*
	 * For the first timer, let's use the realtime clock
	 */
	rc = d_tm_mark_duration_start(&timer1, D_TM_CLOCK_REALTIME, __FILE__,
				      __func__,
				      "10000 iterations - REALTIME",
				      NULL);
	test_function1(10000);
	rc = d_tm_mark_duration_end(&timer1, NULL);


	/*
	 * For the second timer, let's use the process clock
	 */
	rc = d_tm_mark_duration_start(&timer2, D_TM_CLOCK_PROCESS_CPUTIME,
				      __FILE__, __func__,
				      "10000 iterations - PROCESS_CPUTIME",
				      NULL);
	test_function1(10000);
	rc = d_tm_mark_duration_end(&timer2, NULL);

	/*
	 * Notice that the test_function1() metric named 'loop counter'
	 * should have the value 20000 because test_function1(10000) was called
	 * twice and the counter persists in shared memory beyond the life of
	 * the function call itself.
	 */

	/*
	 * test_function2() records a timestamp that shows when the function was
	 * last executed.
	 */
	test_function2();


	/*
	 * Open a handle 1000 times.  The sample function increments a gauge
	 * that monitors how many open handles.
	 */
	for (i = 0; i < 1000; i++)
		test_open_handle();


	/*
	 * Close the same handle 750 times.  The sample function decrements the
	 * same gauge as above.
	 */
	for (i = 0; i < 750; i++)
		test_close_handle();

	/*
	 * The client application will show that the gauge shows 250 open
	 * handles.
	 */

	/*
	 * Trying out the high resolution timer
	 */
	highres_timer();

	d_tm_fini();
	return 0;
failure:
	d_tm_fini();
	return -1;
}
