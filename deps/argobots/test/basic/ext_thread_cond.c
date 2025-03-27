/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

/* This test checks if ABT_cond works with external threads or not.  This test
 * specifically focuses on whether ABT_con that internally uses pthread_cond_t
 * or futex works even if it spuriously wakes up because of signals. */

#define NUM_THREADS 4
#define DEFAULT_NUM_ITER 500

ABT_mutex_memory g_mutex_mem = ABT_MUTEX_INITIALIZER;
ABT_mutex_memory g_cond_mem = ABT_COND_INITIALIZER;

typedef struct {
    ABT_mutex mutex;
    ABT_cond cond;
    int counter;
    ABT_bool is_dynamic; /* Dynamically allocated? */
} mutex_cond_set;
mutex_cond_set g_mutex_cond_sets[4];
int g_iter = DEFAULT_NUM_ITER;

#define NUM_MUTEX_COND_SETS                                                    \
    ((int)(sizeof(g_mutex_cond_sets) / sizeof(g_mutex_cond_sets[0])))

void thread_func(void *arg)
{
    int i;
    for (i = 0; i < g_iter; i++) {
        int j;
        assert(NUM_MUTEX_COND_SETS % 2 == 0);
        for (j = 0; j < NUM_MUTEX_COND_SETS; j += 2) {
            if (g_mutex_cond_sets[j].mutex == ABT_MUTEX_NULL) {
                assert(g_mutex_cond_sets[j + 1].mutex == ABT_MUTEX_NULL);
                /* Not initialized, so let's skip. */
                continue;
            } else {
                assert(g_mutex_cond_sets[j + 1].mutex != ABT_MUTEX_NULL);
            }
            int k;
            for (k = 0; k < 2; k++) {
                ABT_mutex mutex1 = g_mutex_cond_sets[j + k].mutex;
                ABT_cond cond1 = g_mutex_cond_sets[j + k].cond;
                int counter;
                /* Check signal. */
                ABT_mutex_lock(mutex1);
                counter = g_mutex_cond_sets[j + k].counter++;
                if (counter % NUM_THREADS < NUM_THREADS / 2) {
                    ABT_cond_wait(cond1, mutex1);
                    assert(g_mutex_cond_sets[j + k].counter > counter + 1);
                } else if (counter % NUM_THREADS < (NUM_THREADS / 2) * 2) {
                    ABT_cond_signal(cond1);
                }
                ABT_mutex_unlock(mutex1);

                ABT_mutex mutex2 = g_mutex_cond_sets[j + 1 - k].mutex;
                ABT_cond cond2 = g_mutex_cond_sets[j + 1 - k].cond;
                /* Check broadcast.  This works as a "barrier". */
                ABT_mutex_lock(mutex2);
                counter = g_mutex_cond_sets[j + 1 - k].counter++;
                if (counter % NUM_THREADS < NUM_THREADS - 1) {
                    ABT_cond_wait(cond2, mutex2);
                    assert(g_mutex_cond_sets[j + 1 - k].counter > counter + 1);
                } else {
                    ABT_cond_broadcast(cond2);
                }
                ABT_mutex_unlock(mutex2);
            }
        }
    }
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

int main(int argc, char *argv[])
{
    int i, kind, ret;
    int expected = 0, expected_dynamic = 0;
    ABT_mutex_memory mutex_mem = ABT_MUTEX_INITIALIZER;
    ABT_cond_memory cond_mem = ABT_COND_INITIALIZER;

    /* Read arguments. */
    ATS_read_args(argc, argv);
    if (argc >= 2) {
        g_iter = ATS_get_arg_val(ATS_ARG_N_ITER);
    }

#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_init(0, NULL);
    ATS_ERROR(ret, "ABT_init");
#endif
    ABT_bool support_external_thread;
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                (void *)&support_external_thread);
    ATS_ERROR(ret, "ABT_info_query_config");
#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_finalize();
    ATS_ERROR(ret, "ABT_finalize");
#endif
    if (!support_external_thread) {
        ATS_ERROR(ABT_ERR_FEATURE_NA, "ABT_info_query_config");
    }

    /* Set up mutex and condition variable. */
    g_mutex_cond_sets[0].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_mutex_mem);
    g_mutex_cond_sets[0].cond = ABT_COND_MEMORY_GET_HANDLE(&g_cond_mem);
    g_mutex_cond_sets[0].counter = 0;
    g_mutex_cond_sets[0].is_dynamic = ABT_FALSE;
    g_mutex_cond_sets[1].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutex_mem);
    g_mutex_cond_sets[1].cond = ABT_COND_MEMORY_GET_HANDLE(&cond_mem);
    g_mutex_cond_sets[1].counter = 0;
    g_mutex_cond_sets[1].is_dynamic = ABT_FALSE;
    g_mutex_cond_sets[2].mutex = ABT_MUTEX_NULL;
    g_mutex_cond_sets[3].mutex = ABT_MUTEX_NULL;

    /* Use cond before ABT_Init(). */
    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * NUM_THREADS);
        for (i = 0; i < NUM_THREADS; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        for (i = 0; i < NUM_THREADS; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        ATS_destroy_timer();
        expected += 2 * NUM_THREADS * g_iter;
        /* This test does not check dynamically allocated data since they have
         * not been created. */
    }

    /* Initialize */
    ATS_init(argc, argv, 1);

    ATS_printf(1, "# of ULTs: %d\n", NUM_THREADS);
    ATS_printf(1, "# of iter: %d\n", g_iter);

    /* Allocate dynamically allocated cond and mutex. */
    for (i = 2; i < 4; i++) {
        ret = ABT_mutex_create(&g_mutex_cond_sets[i].mutex);
        ATS_ERROR(ret, "ABT_mutex_create");
        ret = ABT_cond_create(&g_mutex_cond_sets[i].cond);
        ATS_ERROR(ret, "ABT_cond_create");
        g_mutex_cond_sets[i].counter = 0;
        g_mutex_cond_sets[i].is_dynamic = ABT_TRUE;
    }

    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * NUM_THREADS);

    /* Set up an execution stream */
    ABT_xstream xstream;
    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_xstream_self");

    ABT_pool pool;
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        /* Create ULTs and Pthreads */
        pthread_t *pthreads = (pthread_t *)malloc(
            sizeof(pthread_t) * (NUM_THREADS - NUM_THREADS / 2));
        for (i = 0; i < NUM_THREADS / 2; i++) {
            ret = ABT_thread_create(pool, thread_func, NULL,
                                    ABT_THREAD_ATTR_NULL, &threads[i]);
            ATS_ERROR(ret, "ABT_thread_create");
        }
        for (i = 0; i < NUM_THREADS - NUM_THREADS / 2; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        /* Join and free ULTs and Pthreads */
        for (i = 0; i < NUM_THREADS / 2; i++) {
            ret = ABT_thread_free(&threads[i]);
            ATS_ERROR(ret, "ABT_thread_free");
        }
        for (i = 0; i < NUM_THREADS - NUM_THREADS / 2; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        ATS_destroy_timer();
        expected += 2 * NUM_THREADS * g_iter;
        expected_dynamic += 2 * NUM_THREADS * g_iter;
    }

    /* Free dynamically allocated mutex and cond. */
    for (i = 2; i < 4; i++) {
        ret = ABT_mutex_free(&g_mutex_cond_sets[i].mutex);
        ATS_ERROR(ret, "ABT_mutex_free");
        ret = ABT_cond_free(&g_mutex_cond_sets[i].cond);
        ATS_ERROR(ret, "ABT_cond_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    /* Use the mutex after finalization. */
    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * NUM_THREADS);
        for (i = 0; i < NUM_THREADS; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        for (i = 0; i < NUM_THREADS; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        ATS_destroy_timer();
        expected += 2 * NUM_THREADS * g_iter;
        /* This test does not check dynamically allocated data since they have
         * already been freed. */
    }

    /* Validation */
    for (i = 0; i < NUM_MUTEX_COND_SETS; i++) {
        if (g_mutex_cond_sets[i].is_dynamic) {
            assert(g_mutex_cond_sets[i].counter == expected_dynamic);
        } else {
            assert(g_mutex_cond_sets[i].counter == expected);
        }
    }

    free(threads);

    return ret;
}
