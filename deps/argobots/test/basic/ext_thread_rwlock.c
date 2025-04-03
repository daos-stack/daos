/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

/* This test checks if ABT_rwlock works with external threads or not.  This test
 * specifically focuses on whether ABT_rwlock that internally uses
 * pthread_cond_t or futex works even if it spuriously wakes up because of
 * signals. */

#define DEFAULT_NUM_TOTAL_THREADS 4
#define DEFAULT_NUM_XSTREAMS 2
#define DEFAULT_NUM_ITER 500

typedef struct {
    ABT_rwlock rwlock;
    int counter;
} rwlock_set;
rwlock_set g_rwlock_sets[1];
int g_iter = DEFAULT_NUM_ITER;

#define NUM_RWLOCK_SETS                                                        \
    ((int)(sizeof(g_rwlock_sets) / sizeof(g_rwlock_sets[0])))

void thread_func(void *arg)
{
    const int is_reader = arg ? 1 : 0;
    int i;
    for (i = 0; i < g_iter; i++) {
        int j;
        for (j = 0; j < NUM_RWLOCK_SETS; j++) {
            if (is_reader) {
                /* Only one reader to avoid a conflict. */
                ABT_rwlock_rdlock(g_rwlock_sets[j].rwlock);
            } else {
                ABT_rwlock_wrlock(g_rwlock_sets[j].rwlock);
            }
            g_rwlock_sets[j].counter += 1;
            ABT_rwlock_unlock(g_rwlock_sets[j].rwlock);
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
    int num_total_threads = DEFAULT_NUM_TOTAL_THREADS;
    int num_xstreams = DEFAULT_NUM_XSTREAMS;
    int expected = 0;

    /* Read arguments. */
    ATS_read_args(argc, argv);
    if (argc >= 2) {
        num_total_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
        num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        g_iter = ATS_get_arg_val(ATS_ARG_N_ITER);
    }
    assert(num_total_threads >= 1);
    assert(num_xstreams >= 1);

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

    /* Initialize */
    ATS_init(argc, argv, num_xstreams);

    ATS_printf(2, "# of ESs : %d\n", num_xstreams);
    ATS_printf(1, "# of ULTs: %d\n", num_total_threads);
    ATS_printf(1, "# of iter: %d\n", g_iter);

    /* Set up rwlock  */
    ret = ABT_rwlock_create(&g_rwlock_sets[0].rwlock);
    ATS_ERROR(ret, "ABT_rwlock_create");

    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_total_threads);
    pthread_t *pthreads =
        (pthread_t *)malloc(sizeof(pthread_t) * num_total_threads);
    ABT_pool *pools;
    pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);

    /* Create Execution Streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    /* Get the pools attached to an execution stream */
    for (i = 0; i < num_xstreams; i++) {
        ret = ABT_xstream_get_main_pools(xstreams[i], 1, pools + i);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");
    }

    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        int reader_tid;
        for (reader_tid = 0; reader_tid < num_total_threads; reader_tid++) {
            /* Create ULTs */
            for (i = 0; i < num_total_threads / 2; i++) {
                ret = ABT_thread_create(pools[i % num_xstreams], thread_func,
                                        (i == reader_tid) ? &reader_tid : NULL,
                                        ABT_THREAD_ATTR_NULL, &threads[i]);
                ATS_ERROR(ret, "ABT_thread_create");
            }
            /* Create Pthreads too. */
            for (i = num_total_threads / 2; i < num_total_threads; i++) {
                ret = pthread_create(&pthreads[i], NULL, pthread_func,
                                     (i == reader_tid) ? &reader_tid : NULL);
                assert(ret == 0);
            }
            /* Join and free ULTs */
            for (i = 0; i < num_total_threads / 2; i++) {
                ret = ABT_thread_free(&threads[i]);
                ATS_ERROR(ret, "ABT_thread_free");
            }
            /* Join Pthreads too. */
            for (i = num_total_threads / 2; i < num_total_threads; i++) {
                ret = pthread_join(pthreads[i], NULL);
                assert(ret == 0);
            }
            expected += num_total_threads * g_iter;
        }
        ATS_destroy_timer();
    }

    /* Join Execution Streams */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_join(xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_join");
    }

    /* Free Execution Streams */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Free rwlock. */
    ret = ABT_rwlock_free(&g_rwlock_sets[0].rwlock);
    ATS_ERROR(ret, "ABT_rwlock_free");

    /* Finalize */
    ret = ATS_finalize(0);

    /* Validation */
    for (i = 0; i < NUM_RWLOCK_SETS; i++) {
        assert(g_rwlock_sets[i].counter == expected);
    }

    free(xstreams);
    free(threads);
    free(pthreads);
    free(pools);

    return ret;
}
