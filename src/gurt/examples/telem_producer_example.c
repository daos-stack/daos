/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file shows an example of using the telemetry API to produce metrics
 */

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"

/**
 * A sample function that creates and incremements a metric for a loop counter
 *
 * \param[in]	count	Number of loop iterations
 */
void test_function1(int count)
{
	static struct d_tm_node_t	*loop;
	int				rc;
	int				i;

	rc = d_tm_add_metric(&loop, D_TM_COUNTER, NULL, NULL, "loop counter");
	if (rc != 0) {
		printf("d_tm_add_metric counter failed: "DF_RC"\n", DP_RC(rc));
		return;
	}

	for (i = 0; i < count - 1; i++) {
		d_tm_inc_counter(loop, 1);
	}
}

/**
 * A sample function that creates and records a timestamp that indicates when
 * this function is called.
 */
void test_function2(void)
{
	static struct d_tm_node_t	*ts;
	int				rc;

	rc = d_tm_add_metric(&ts, D_TM_TIMESTAMP, NULL, NULL, "last executed");
	if (rc != 0) {
		printf("d_tm_add_metric timestamp failed: "DF_RC"\n",
		       DP_RC(rc));
		return;
	}

	d_tm_record_timestamp(ts);
}

/**
 * A sample function that shows how a gauge is incremented, say when opening
 * a handle.  Note that num_open_handles, like all other d_tm_node_t variables
 * is declared as static.  This allows the pointer to be initialized the first
 * time the d_tm_increment_gauge() is called, and is simply used on subsequent
 * function calls.
 */
void test_open_handle(void)
{
	static struct d_tm_node_t	*num_open_handles;
	int				rc;

	if (num_open_handles == NULL) {
		/**
		 * Create a gauge at a known location so that it can be used by
		 * test_close_handle() without sharing pointers.
		 */
		rc = d_tm_add_metric(&num_open_handles, D_TM_GAUGE, NULL, NULL,
				     "handle/open handles");
		if (rc != 0) {
			printf("d_tm_add_metric gauge failed: "DF_RC"\n",
			       DP_RC(rc));
			return;
		}
	}

	/**
	 * The gauge can be incremented by an arbitrary value.
	 */
	d_tm_inc_gauge(num_open_handles, 1);
}

/**
 * A sample function that shows how a gauge is decremented, say when closing
 * a handle.  It uses the same gauge as the one referenced in test_open_handle()
 * as it is initially referenced by the same name.  Had the pointer been shared,
 * it would have been used instead.
 */
void test_close_handle(void)
{
	static struct d_tm_node_t	*num_open_handles;
	int				rc;

	if (num_open_handles == NULL) {
		/**
		 * Create a gauge at a known location so that it can be used by
		 * test_close_handle() without sharing pointers.
		 */
		rc = d_tm_add_metric(&num_open_handles, D_TM_GAUGE, NULL, NULL,
				     "handle/open handles");
		if (rc != 0) {
			printf("d_tm_add_metric gauge failed: "DF_RC"\n",
			       DP_RC(rc));
			return;
		}
	}

	/**
	 * The full name of this gauge matches the name in test_open_handle() so
	 * that increments in test_open_handle() are changing the same metric
	 * as the one used here.
	 */
	d_tm_dec_gauge(num_open_handles, 1);
}

/**
 * Shows use of the timer snapshot.  It allows the developer to take
 * high resolution timer snapshots at various places within their code, which
 * can then be interpreted depending on the need.  A duration type metric
 * is a simplified version of this metric that does the interval calculation.
 * When the timer shapshot is created, specify the clock type from:
 * D_TM_CLOCK_REALTIME which is CLOCK_REALTIME
 * D_TM_CLOCK_PROCESS_CPUTIME which is CLOCK_PROCESS_CPUTIME_ID
 * D_TM_CLOCK_THREAD_CPUTIME which is CLOCK_THREAD_CPUTIME_ID
 */
void timer_snapshot(void)
{
	#define NUM_SNAPSHOTS 6
	static struct d_tm_node_t	*t[NUM_SNAPSHOTS];
	struct timespec			ts;
	int				rc;
	int				snap;

	for (snap = 0; snap < NUM_SNAPSHOTS; snap++) {
		rc = d_tm_add_metric(&t[snap], D_TM_TIMER_SNAPSHOT,
				     NULL, NULL, "snapshot %d", snap);
		if (rc != 0)
			printf("d_tm_add_metric snapshot %d failed: "
			       DF_RC "\n", snap, DP_RC(rc));
	}

	d_tm_take_timer_snapshot(t[0], D_TM_CLOCK_REALTIME);

	/** Do some stuff */
	sleep(1);

	d_tm_take_timer_snapshot(t[1], D_TM_CLOCK_REALTIME);

	/** Do some stuff */
	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	nanosleep(&ts, NULL);

	d_tm_take_timer_snapshot(t[2], D_TM_CLOCK_REALTIME);

	/** Do some stuff (10x longer) */
	ts.tv_sec = 0;
	ts.tv_nsec = 500000000;
	nanosleep(&ts, NULL);

	d_tm_take_timer_snapshot(t[3], D_TM_CLOCK_REALTIME);

	/**
	 * How long did the sleep(1) take?  That's t2 - t1
	 * How long did the 50000 iterations take?  That's t3 - t2
	 * How long did the 500000 iterations take?  That's t4 - t3.
	 * How long did the sleep(1) and the 50000 iterations take?  That's
	 * t3 - t1.
	 * When was function entry?  That's t1.
	 * When did the function exit the sleep(1)?  That's t t2.
	 */

	/** This is how to specify a high resolution process CPU timer */
	d_tm_take_timer_snapshot(t[4], D_TM_CLOCK_PROCESS_CPUTIME);

	/** This is how to specify a high resolution thread CPU timer */
	d_tm_take_timer_snapshot(t[5], D_TM_CLOCK_THREAD_CPUTIME);
}

/**
 * Demonstrates how to use d_tm_add_metric to create a metric explicitly.
 * When doing so, it allows the developer to add metadata to the metric.
 * Either create the metric in the function that will use it, or keep track of
 * the pointers with a d_tm_nodeList_t for use elsewhere.
 *
 * \return	Pointer to a d_tm_nodeList if successful
 *		NULL if failure
 */
struct d_tm_nodeList_t *add_metrics_manually(void)
{
	struct d_tm_nodeList_t	*node_list = NULL;
	struct d_tm_node_t	*counter1 = NULL;
	struct d_tm_node_t	*counter2 = NULL;
	int			rc;

	/**
	 * Create some metrics manually, and keep track of the pointers by
	 * adding them to a d_tm_nodeList_t for later usage.
	 */
	rc = d_tm_add_metric(&counter1, D_TM_COUNTER,
			     "A manually added counter",
			     D_TM_KILOBYTE,
			     "manually added/counter 1");
	if (rc != DER_SUCCESS) {
		printf("d_tm_add_metric failed: " DF_RC "\n", DP_RC(rc));
		return NULL;
	}

	rc = d_tm_list_add_node(counter1, &node_list);
	if (rc != DER_SUCCESS) {
		printf("d_tm_add_metric failed: " DF_RC "\n", DP_RC(rc));
		return NULL;
	}

	rc = d_tm_add_metric(&counter2, D_TM_COUNTER,
			     "Another manually added counter",
			     D_TM_MEGABYTE,
			     "manually added/counter 2");
	if (rc != DER_SUCCESS) {
		d_tm_list_free(node_list);
		printf("d_tm_add_metric failed: " DF_RC "\n", DP_RC(rc));
		return NULL;
	}

	rc = d_tm_list_add_node(counter2, &node_list);
	if (rc != DER_SUCCESS) {
		d_tm_list_free(node_list);
		printf("d_tm_add_metric failed: " DF_RC "\n", DP_RC(rc));
		return NULL;
	}

	return node_list;
}

/**
 * Iterate through a d_tm_nodeList_t and increment any counter found, just to
 * show one way of using pointers to metrics that were initialized explicitly
 * in some location other than exactly where it is being used.
 *
 * \param[in]	node_list	A list of metrics that were previously added
 */
void use_manually_added_metrics(struct d_tm_nodeList_t *node_list)
{
	while (node_list) {
		switch (node_list->dtnl_node->dtn_type) {
		case D_TM_DIRECTORY:
			break;
		case D_TM_COUNTER:
			d_tm_inc_counter(node_list->dtnl_node, 1);
			break;
		default:
			printf("Item: %s has unknown type: 0x%x\n",
			       node_list->dtnl_node->dtn_name,
			       node_list->dtnl_node->dtn_type);
			break;
		}
		node_list = node_list->dtnl_next;
	}
}

int
main(int argc, char **argv)
{
	static struct d_tm_node_t	*entry;
	static struct d_tm_node_t	*loop;
	static struct d_tm_node_t	*timer1;
	static struct d_tm_node_t	*timer2;
	struct d_tm_nodeList_t		*node_list;
	int				rc;
	int				simulated_srv_idx = 0;
	int				i;

	if (argc < 2) {
		printf("Specify an integer that identifies this producer's "
		       "sever instance.  "
		       "Specify the same value to the consumer.\n");
		exit(0);
	}

	simulated_srv_idx = atoi(argv[1]);
	printf("This simulated server instance has ID: %d\n",
	       simulated_srv_idx);

	/**
	 * Call d_tm_init() only once per process,
	 * i.e. in engine/init.c::server_init()
	 */
	rc = d_tm_init(simulated_srv_idx, D_TM_SHARED_MEMORY_SIZE,
		       D_TM_RETAIN_SHMEM);
	if (rc != 0)
		goto failure;

	/**
	 * The API is ready to use.  Add a counter that will be identified in
	 * the tree by this file name, function name, and the name "sample
	 * counter", i.e.:
	 * "src/gurt/examples/telem_producer_example.c/main/sample counter"
	 */
	rc = d_tm_add_metric(&entry, D_TM_COUNTER, NULL, NULL,
			     "sample counter");
	if (rc != 0) {
		printf("couldn't add sample counter: "DF_RC"\n", DP_RC(rc));
		goto failure;
	}

	d_tm_inc_counter(entry, 1);

	/**
	 * Increment another counter in a loop.
	 */
	rc = d_tm_add_metric(&loop, D_TM_COUNTER, NULL, NULL, "loop counter");
	if (rc != 0) {
		printf("couldn't add loop counter: "DF_RC"\n", DP_RC(rc));
		goto failure;
	}
	for (i = 0; i < 1000; i++) {
		d_tm_inc_counter(loop, 1);
	}

	/**
	 * How long does it take to execute test_function()?
	 * When the duration timer is created, specify the clock type from:
	 * D_TM_CLOCK_REALTIME which is CLOCK_REALTIME
	 * D_TM_CLOCK_PROCESS_CPUTIME which is CLOCK_PROCESS_CPUTIME_ID
	 * D_TM_CLOCK_THREAD_CPUTIME which is CLOCK_THREAD_CPUTIME_ID
	 */

	/** For the first timer, let's use the realtime clock */
	rc = d_tm_add_metric(&timer1, D_TM_DURATION, NULL, NULL,
			     "10000 iterations with rt clock");
	if (rc != 0) {
		printf("couldn't add duration timer1: "DF_RC"\n", DP_RC(rc));
		goto failure;
	}

	d_tm_mark_duration_start(timer1, D_TM_CLOCK_REALTIME);
	test_function1(10000);
	d_tm_mark_duration_end(timer1);

	/** For the second timer, let's use the process clock */
	rc = d_tm_add_metric(&timer2, D_TM_DURATION, NULL, NULL,
			     "10000 iterations with process clock");
	if (rc != 0) {
		printf("couldn't add duration timer2: "DF_RC"\n", DP_RC(rc));
		goto failure;
	}

	d_tm_mark_duration_start(timer2, D_TM_CLOCK_PROCESS_CPUTIME);
	test_function1(10000);
	d_tm_mark_duration_end(timer2);

	/**
	 * Notice that the test_function1() metric named 'loop counter'
	 * should have the value 20000 because test_function1(10000) was called
	 * twice and the counter persists in shared memory beyond the life of
	 * the function call itself.
	 */

	/**
	 * test_function2() records a timestamp that shows when the function was
	 * last executed.
	 */
	test_function2();

	/**
	 * Open a handle 1000 times.  The sample function increments a gauge
	 * that monitors how many open handles.
	 */
	for (i = 0; i < 1000; i++)
		test_open_handle();

	/**
	 * Close the same handle 750 times.  The sample function decrements the
	 * same gauge as above.
	 */
	for (i = 0; i < 750; i++)
		test_close_handle();

	/**
	 * The client application will show that the gauge shows 250 open
	 * handles.
	 */

	/** Trying out the high resolution timer snapshot*/
	timer_snapshot();

	/**
	 * Add some metrics with metadata.
	 */
	node_list = add_metrics_manually();
	if (node_list == NULL)
		goto failure;

	/**
	 * After calling add_metrics_manually, the counters have value = 0
	 * Each call to use_manually_added_metrics() increments the counters
	 * by 1.  After the three calls, they should have value = 3.
	 * This simply demonstrates how to use the node pointers that were
	 * initialized when adding the metrics manually.
	 */
	for (i = 0; i < 3; i++)
		use_manually_added_metrics(node_list);
	d_tm_list_free(node_list);

	d_tm_fini();

	printf("Metrics added and ready to read.  Try the example consumer.\n");
	return 0;

failure:
	d_tm_fini();
	return -1;
}
