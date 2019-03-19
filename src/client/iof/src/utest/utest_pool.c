/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <inttypes.h>
#include <stdio.h>
#include <pthread.h>
#include <gurt/list.h>
#include <CUnit/Basic.h>

#include <iof_obj_pool.h>

int init_suite(void)
{
	return CUE_SUCCESS;
}

int clean_suite(void)
{
	return CUE_SUCCESS;
}

#define ENTRIES 20000

struct item {
	d_list_t link;
	int value;
};

/** test iof obj_pool() */
static void test_iof_obj_pool(void)
{
	obj_pool_t pool;
	d_list_t head;
	struct item *item;
	struct item *tmp;
	int sum = 0;
	int i;

	D_INIT_LIST_HEAD(&head);

	CU_ASSERT(obj_pool_initialize(&pool, 4) == 0);
	CU_ASSERT(obj_pool_destroy(&pool) == 0);

	CU_ASSERT(obj_pool_initialize(&pool, sizeof(struct item)) == 0);

	for (i = 0; i < ENTRIES; i++) {
		CU_ASSERT(obj_pool_get(&pool, &item) == 0);
		if (item != NULL) {
			item->value = i + 1;
			d_list_add(&item->link, &head);
		}
	}

	d_list_for_each_entry_safe(item, tmp, &head, link) {
		sum += item->value;
		d_list_del(&item->link);
		CU_ASSERT(obj_pool_put(&pool, item) == 0);
	}

	CU_ASSERT(obj_pool_destroy(&pool) == 0);
}

#define NUM_THREADS 64

struct thread_info {
	d_list_t entries;
	int tid;
	pthread_barrier_t *barrier;
	obj_pool_t *pool;
	struct thread_info *global_info;
};

static uint64_t magic_number = 0xdeadbeefbaadf00d;
struct entry {
	uint64_t magic;
	d_list_t link;
	int value;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define LOCKED_ASSERT(cond)                  \
	do {                                 \
		pthread_mutex_lock(&lock);   \
		CU_ASSERT(cond)              \
		pthread_mutex_unlock(&lock); \
	} while (0)

#define COUNT_FAILS(count, cond)                                       \
	do {                                                           \
		if (!(cond)) {                                         \
			printf("Assertion " #cond "failed at %s:%d\n", \
			       __FILE__, __LINE__);                    \
			count++;                                       \
		}                                                      \
	} while (0)

static void *thread_func(void *arg)
{
	struct thread_info *tpd = (struct thread_info *)arg;
	struct entry *entry;
	struct entry *tmp;
	struct thread_info *othertpd;
	int i;
	int rc;
	int count = 0;

	/* Get a bunch of entries from the pool */
	for (i = 0; i < ENTRIES; i++) {
		rc = obj_pool_get(tpd->pool, &entry);
		COUNT_FAILS(count, rc == 0);

		entry->value = tpd->tid;
		entry->magic = magic_number;
		d_list_add(&entry->link, &tpd->entries);
	}

	if (tpd->tid & 1) {
		d_list_for_each_entry_safe(entry, tmp, &tpd->entries, link) {
			d_list_del(&entry->link);
			rc = obj_pool_put(tpd->pool, entry);
			COUNT_FAILS(count, rc == 0);
		}

		LOCKED_ASSERT(count == 0);
		LOCKED_ASSERT(d_list_empty(&tpd->entries));
	} else {
		/* Allocate more entries */
		for (i = 0; i < ENTRIES; i++) {
			rc = obj_pool_get(tpd->pool, &entry);
			COUNT_FAILS(count, rc == 0);

			entry->value = tpd->tid;
			entry->magic = magic_number;
			d_list_add(&entry->link, &tpd->entries);
		}
	}

	pthread_barrier_wait(tpd->barrier);

	for (i = 0; i < NUM_THREADS; i++) {
		othertpd = &tpd->global_info[i];

		if (i & 1) {
			COUNT_FAILS(count, d_list_empty(&othertpd->entries));
		} else {
			COUNT_FAILS(count,
				    !d_list_empty(&othertpd->entries));
			d_list_for_each_entry(entry, &othertpd->entries, link) {
				COUNT_FAILS(count,
					    entry->value == othertpd->tid);
				COUNT_FAILS(count,
					    entry->magic == magic_number);
			}
		}
	}

	pthread_barrier_wait(tpd->barrier);

	d_list_for_each_entry_safe(entry, tmp, &tpd->entries, link) {
		d_list_del(&entry->link);
		rc = obj_pool_put(tpd->pool, entry);
		COUNT_FAILS(count, rc == 0);
	}

	pthread_barrier_wait(tpd->barrier);

	LOCKED_ASSERT(count == 0);
	LOCKED_ASSERT(d_list_empty(&tpd->entries));

	return NULL;
}

static void test_iof_obj_pool_threaded(void)
{
	pthread_barrier_t barrier;
	obj_pool_t pool;
	pthread_t thread[NUM_THREADS];
	struct thread_info tpd[NUM_THREADS];
	int i;
	int rc;

	pthread_barrier_init(&barrier, NULL, NUM_THREADS);

	CU_ASSERT(obj_pool_initialize(&pool, sizeof(struct entry)) == 0);

	for (i = 0; i < NUM_THREADS; i++) {
		tpd[i].tid = i;
		D_INIT_LIST_HEAD(&tpd[i].entries);
		tpd[i].barrier = &barrier;
		tpd[i].pool = &pool;
		tpd[i].global_info = &tpd[0];
		rc = pthread_create(&thread[i], NULL, thread_func, &tpd[i]);
		LOCKED_ASSERT(rc == 0);
	}

	for (i = 0; i < NUM_THREADS; i++) {
		rc = pthread_join(thread[i], NULL);
		LOCKED_ASSERT(rc == 0);
	}

	CU_ASSERT(obj_pool_destroy(&pool) == 0);
}

static void test_iof_obj_pool_invalid(void)
{
	int rc;
	obj_pool_t pool;
	int *x;
	double *p;

	rc = obj_pool_initialize(NULL, 10);
	CU_ASSERT(rc == -DER_INVAL);

	rc = obj_pool_initialize(&pool, 0);
	CU_ASSERT(rc == -DER_INVAL);

	rc = obj_pool_get(NULL, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = obj_pool_put(NULL, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = obj_pool_put(NULL, x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = obj_pool_initialize(&pool, sizeof(int));
	CU_ASSERT(rc == 0);

	rc = obj_pool_get(&pool, &p);
	CU_ASSERT(rc == -DER_INVAL);
}

int main(int argc, char **argv)
{
	CU_pSuite pSuite = NULL;

	if (CU_initialize_registry() != CUE_SUCCESS)
		return CU_get_error();
	pSuite = CU_add_suite("iof_obj_pool API test", init_suite, clean_suite);
	if (!pSuite) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (!CU_add_test(pSuite, "iof_obj_pool test",
			 test_iof_obj_pool) ||
	    !CU_add_test(pSuite, "iof_obj_pool threaded test",
		    test_iof_obj_pool_threaded) ||
	    !CU_add_test(pSuite, "iof_obj_pool invalid test",
		    test_iof_obj_pool_invalid)) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_cleanup_registry();

	return CU_get_error();
}
