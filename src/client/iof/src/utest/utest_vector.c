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
#include <stdio.h>
#include <pthread.h>
#include <CUnit/Basic.h>

#include <iof_atomic.h>
#include <iof_vector.h>

int init_suite(void)
{
	return CUE_SUCCESS;
}

int clean_suite(void)
{
	return CUE_SUCCESS;
}

#define ENTRIES 4122

/** test iof vector() */
static void test_iof_vector(void)
{
	vector_t vector;
	int i;
	int rc;
	int value = 10;
	int *valuep;

	CU_ASSERT(vector_init(&vector, sizeof(int), 100) == 0);
	CU_ASSERT(vector_destroy(&vector) == 0);

	CU_ASSERT(vector_init(&vector, sizeof(int), ENTRIES) == 0);

	/* Set every other entry to 10 */
	for (i = 0; i < ENTRIES; i += 2)
		CU_ASSERT(vector_set(&vector, i, &value) == 0);

	/* Retrieve every other entry */
	for (i = 0; i < ENTRIES; i++) {
		rc = vector_get(&vector, i, &valuep);
		CU_ASSERT((i & 1) ? (rc == -DER_NONEXIST) :
			  (rc == 0));
		if (i & 1) {
			CU_ASSERT(rc == -DER_NONEXIST);
			CU_ASSERT(valuep == NULL);
		} else {
			CU_ASSERT(rc == 0);
			CU_ASSERT_PTR_NOT_NULL(valuep);
			if (valuep != NULL)
				CU_ASSERT_EQUAL(*valuep, 10);
			CU_ASSERT(vector_decref(&vector, valuep) == 0);
		}
	}
	CU_ASSERT(vector_destroy(&vector) == 0);
}

#define NUM_THREADS 16

struct thread_info {
	int tid;
	int deadbeef;
	int baadf00d;
	int d00fd00d;
};

struct tpd {
	pthread_barrier_t *barrier;
	vector_t *vector;
	struct thread_info info;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define LOCKED_ASSERT(cond)                  \
	do {                                 \
		pthread_mutex_lock(&lock);   \
		CU_ASSERT(cond)              \
		pthread_mutex_unlock(&lock); \
	} while (0)

#define COUNT_FAILS(count, cond)                               \
	do {                                                   \
		if (!(cond)) {                                 \
			printf("Failure %s at %s:%d\n", #cond, \
			       __FILE__, __LINE__);            \
			(count)++;                             \
		}                                              \
	} while (0)

static void *thread_func(void *arg)
{
	struct tpd *tpd = (struct tpd *)arg;
	struct thread_info *info;
	vector_t *vector = tpd->vector;
	pthread_barrier_t *barrier = tpd->barrier;
	int num_found = 0;
	int i;
	int tid;
	int rc;
	int fail = 0;

	/* Threads set entries while other threads read them.  Should never
	 * see intermediate state
	 */
	for (i = 0; i < ENTRIES; i++) {
		if ((i % NUM_THREADS) == (tpd->info.tid - 1)) {
			rc = vector_set(vector, i, &tpd->info);
			COUNT_FAILS(fail, rc == 0);
			if (rc != 0)
				printf("rc = %d\n", rc);
		} else {
			rc = vector_get(vector, i, &info);
			if (rc != -DER_NONEXIST)
				COUNT_FAILS(fail, rc == 0);
			if (rc == 0) {
				tid = (i % NUM_THREADS) + 1;
				COUNT_FAILS(fail, info->tid == tid);
				COUNT_FAILS(fail, info->deadbeef == 0xdeadbeef);
				COUNT_FAILS(fail, info->baadf00d == 0xbaadf00d);
				COUNT_FAILS(fail, info->d00fd00d == 0xd00fd00d);
				rc = vector_decref(vector, info);
				COUNT_FAILS(fail, rc == 0);
				num_found++;
			}
		}
	}

	/* Ok, remove the entries.   Active readers should still work */
	for (i = 0; i < ENTRIES; i++)
		if ((i % NUM_THREADS) == (tpd->info.tid - 1)) {
			rc = vector_remove(vector, i, NULL);
			COUNT_FAILS(fail, rc == 0);
		}

	pthread_barrier_wait(barrier);
	fail = 0;

	/* should be empty now */
	for (i = 0; i < ENTRIES; i++) {
		rc = vector_get(vector, i, &info);
		COUNT_FAILS(fail, rc == -DER_NONEXIST);
	}

	LOCKED_ASSERT(fail == 0);

	return NULL;
}

struct entry {
	int tid;
	ATOMIC long inc;
	ATOMIC long dec;
	ATOMIC long inc2;
};

static void *thread_func_dup(void *arg)
{
	struct tpd *tpd = (struct tpd *)arg;
	struct entry *entry;
	int fail = 0;
	int rc;
	int i;

	for (i = 0; i < NUM_THREADS; i++) {
		rc = vector_get(tpd->vector, i, &entry);
		COUNT_FAILS(fail, rc == 0);
		COUNT_FAILS(fail, entry != NULL);
		if (entry == NULL)
			continue;
		COUNT_FAILS(fail, entry->tid == (i % NUM_THREADS));
		atomic_fetch_add(&entry->inc, 1);
		atomic_fetch_sub(&entry->dec, 1);
		atomic_fetch_add(&entry->inc2, 2);
	}

	for (i = NUM_THREADS; i < ENTRIES; i++) {
		rc = vector_dup(tpd->vector, i % NUM_THREADS, i, &entry);
		COUNT_FAILS(fail, rc == 0);
		COUNT_FAILS(fail, entry != NULL);
		if (entry == NULL)
			continue;
		COUNT_FAILS(fail, entry->tid == (i % NUM_THREADS));
		atomic_fetch_add(&entry->inc, 1);
		atomic_fetch_sub(&entry->dec, 1);
		atomic_fetch_add(&entry->inc2, 2);
	}

	LOCKED_ASSERT(fail == 0);

	return NULL;
}

static void test_iof_vector_threaded(void)
{
	pthread_barrier_t barrier;
	vector_t vector;
	pthread_t thread[NUM_THREADS];
	struct entry *entry;
	struct entry init;
	struct tpd tpd[NUM_THREADS];
	int expected;
	int i;
	int rc;

	init.inc = 0;
	init.dec = 0;
	init.inc2 = 0;

	pthread_barrier_init(&barrier, NULL, NUM_THREADS);

	CU_ASSERT(vector_init(&vector, sizeof(struct thread_info),
			      ENTRIES) == 0);

	for (i = 0; i < NUM_THREADS; i++) {
		tpd[i].info.tid = i + 1;
		tpd[i].info.deadbeef = 0xdeadbeef;
		tpd[i].info.baadf00d = 0xbaadf00d;
		tpd[i].info.d00fd00d = 0xd00fd00d;
		tpd[i].barrier = &barrier;
		tpd[i].vector = &vector;
		rc = pthread_create(&thread[i], NULL, thread_func, &tpd[i]);
		LOCKED_ASSERT(rc == 0);
	}

	for (i = 0; i < NUM_THREADS; i++) {
		rc = pthread_join(thread[i], NULL);
		LOCKED_ASSERT(rc == 0);
	}

	CU_ASSERT(vector_destroy(&vector) == 0);

	/* test dup */
	CU_ASSERT(vector_init(&vector, sizeof(struct entry), ENTRIES) == 0);

	for (i = 0; i < NUM_THREADS; i++) {
		init.tid = i;
		CU_ASSERT(vector_set(&vector, i, &init) == 0);
	}

	for (i = 0; i < NUM_THREADS; i++) {
		rc = pthread_create(&thread[i], NULL,
				    thread_func_dup, &tpd[i]);
		LOCKED_ASSERT(rc == 0);
	}

	for (i = 0; i < NUM_THREADS; i++) {
		rc = pthread_join(thread[i], NULL);
		LOCKED_ASSERT(rc == 0);
	}

	for (i = 0; i < NUM_THREADS; i++) {
		CU_ASSERT(vector_get(&vector, i, &entry) == 0);
		CU_ASSERT_PTR_NOT_NULL(entry);
		if (entry == NULL)
			continue;
		expected = ENTRIES - (ENTRIES % NUM_THREADS);
		if (i < ENTRIES % NUM_THREADS)
			expected += NUM_THREADS;
		CU_ASSERT(entry->inc == expected);
		CU_ASSERT(entry->dec == -expected);
		CU_ASSERT(entry->inc2 == (2 * expected));
	}

	CU_ASSERT(vector_destroy(&vector) == 0);
}

static void test_iof_vector_invalid(void)
{
	int rc;
	int *x;
	int value = 10;
	vector_t vector;

	rc = vector_init(NULL, sizeof(int), 10);
	CU_ASSERT(rc == -DER_INVAL);

	memset(&vector, 0xff, sizeof(vector));

	rc = vector_get(&vector, 4, &x);
	CU_ASSERT(rc == -DER_UNINIT);

	rc = vector_destroy(&vector);
	CU_ASSERT(rc == -DER_UNINIT);

	rc = vector_init(&vector, sizeof(int), 10);
	CU_ASSERT(rc == 0);

	rc = vector_get(&vector, -1, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_get(NULL, -1, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_set(&vector, -1, &value);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_set(&vector, 30, &value);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_set(NULL, 4, &value);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_set(&vector, 4, NULL);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(&vector, -1, 0, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(&vector, 30, 0, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(&vector, 0, -1, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(&vector, 0, 30, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(&vector, 0, 1, NULL);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_dup(NULL, 0, 1, &x);
	CU_ASSERT(rc == -DER_INVAL);

	rc = vector_set(&vector, 4, &value);
	CU_ASSERT(rc == 0);

	rc = vector_remove(&vector, 4, &x);
	CU_ASSERT(rc == 0);
	CU_ASSERT_PTR_NOT_NULL(x);
	if (x != NULL)
		CU_ASSERT(*x == value);
	rc = vector_decref(&vector, x);
	CU_ASSERT(rc == 0);

	rc = vector_remove(&vector, 4, &x);
	CU_ASSERT(rc == -DER_NONEXIST);
	CU_ASSERT_PTR_NULL(x);

	rc = vector_destroy(&vector);
	CU_ASSERT(rc == 0);

	rc = vector_destroy(&vector);
	CU_ASSERT(rc == -DER_UNINIT);

	rc = vector_destroy(NULL);
	CU_ASSERT(rc == -DER_INVAL);
}

int main(int argc, char **argv)
{
	CU_pSuite pSuite = NULL;

	if (CU_initialize_registry() != CUE_SUCCESS)
		return CU_get_error();
	pSuite = CU_add_suite("iof_vector API test", init_suite, clean_suite);
	if (!pSuite) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (!CU_add_test(pSuite, "iof_vector test",
			 test_iof_vector) ||
	    !CU_add_test(pSuite, "iof_vector threaded test",
		    test_iof_vector_threaded) ||
	    !CU_add_test(pSuite, "iof_vector invalid test",
		    test_iof_vector_invalid)) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_cleanup_registry();

	return CU_get_error();
}
