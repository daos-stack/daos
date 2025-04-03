/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

/* This test checks if ABT_barrier works with external threads or not.  This
 * test specifically focuses on whether ABT_barrier that internally uses
 * pthread_cond_t or futex works even if it spuriously wakes up because of
 * signals. */

#define NUM_PTHREADS 3
#define NUM_CHILD_XSTEARMS 2 /* Don't create too many ESs */
#define DEFAULT_NUM_THREADS 5
#define DEFAULT_NUM_XSTREAMS 2
#define DEFAULT_NUM_ITER 100

ABT_barrier g_barrier;
int g_iter = DEFAULT_NUM_ITER;
int g_num_threads = DEFAULT_NUM_THREADS;
int g_num_xstreams = DEFAULT_NUM_XSTREAMS;
ABT_pool *g_pools;

#define NUM_BARRIER_SETS                                                       \
    ((int)(sizeof(g_barrier_sets) / sizeof(g_barrier_sets[0])))

typedef struct {
    int counter;
} thread_arg_t;

void thread_func(void *arg)
{
    thread_arg_t *p_arg = (thread_arg_t *)arg;
    p_arg->counter += 1;
}

void *pthread_func(void *arg)
{
    int i, step, ret;
    ABT_xstream xstreams[NUM_CHILD_XSTEARMS];
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * g_num_threads);
    thread_arg_t *thread_args =
        (thread_arg_t *)malloc(sizeof(thread_arg_t) * g_num_threads);
    for (i = 0; i < g_num_threads; i++) {
        thread_args[i].counter = 0;
    }
    for (step = 0; step < g_iter; step++) {
        /* Create ULTs and execution streams. */
        for (i = 0; i < g_num_threads; i++) {
            ret = ABT_thread_create(g_pools[i % g_num_xstreams], thread_func,
                                    &thread_args[i], ABT_THREAD_ATTR_NULL,
                                    &threads[i]);
            ATS_ERROR(ret, "ABT_thread_create");
        }
        if (step % 10 == 0) { /* ES creation is heavy. */
            for (i = 0; i < NUM_CHILD_XSTEARMS; i++) {
                ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[i]);
                ATS_ERROR(ret, "ABT_xstream_create");
            }
        }
        /* Join and free ULTs and execution streams. */
        for (i = 0; i < g_num_threads; i++) {
            ret = ABT_thread_free(&threads[i]);
            ATS_ERROR(ret, "ABT_thread_free");
            assert(thread_args[i].counter == step + 1);
        }
        if (step % 10 == 0) {
            for (i = 0; i < NUM_CHILD_XSTEARMS; i++) {
                ret = ABT_xstream_free(&xstreams[i]);
                ATS_ERROR(ret, "ABT_xstream_free");
            }
        }
    }

    free(threads);
    free(thread_args);
    ret = ABT_barrier_wait(g_barrier);
    ATS_ERROR(ret, "ABT_barrier_wait");
    return NULL;
}

int main(int argc, char *argv[])
{
    int i, kind, ret;

    /* Read arguments. */
    ATS_read_args(argc, argv);
    if (argc >= 2) {
        g_num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
        g_num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        g_iter = ATS_get_arg_val(ATS_ARG_N_ITER);
    }
    assert(g_num_threads >= 1);
    assert(g_num_xstreams >= 1);

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
    ATS_init(argc, argv, g_num_xstreams);

    ATS_printf(2, "# of ESs : %d\n", g_num_xstreams);
    ATS_printf(1, "# of ULTs: %d\n", g_num_threads);
    ATS_printf(1, "# of iter: %d\n", g_iter);

    /* Set up a barrier. */
    ret = ABT_barrier_create(NUM_PTHREADS, &g_barrier);
    ATS_ERROR(ret, "ABT_barrier_create");

    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * g_num_xstreams);
    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * NUM_PTHREADS);
    g_pools = (ABT_pool *)malloc(sizeof(ABT_pool) * g_num_xstreams);

    /* Create Execution Streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    for (i = 1; i < g_num_xstreams; i++) {
        ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    /* Get the pools attached to an execution stream */
    for (i = 0; i < g_num_xstreams; i++) {
        ret = ABT_xstream_get_main_pools(xstreams[i], 1, g_pools + i);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");
    }

    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        /* Create Pthreads. */
        for (i = 1; i < NUM_PTHREADS; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        /* Note that this thread is a primary ES. */
        pthread_func(NULL);
        /* Join Pthreads. */
        for (i = 1; i < NUM_PTHREADS; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        ATS_destroy_timer();
    }

    /* Join Execution Streams */
    for (i = 1; i < g_num_xstreams; i++) {
        ret = ABT_xstream_join(xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_join");
    }

    /* Free Execution Streams */
    for (i = 1; i < g_num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Free barrier. */
    ret = ABT_barrier_free(&g_barrier);
    ATS_ERROR(ret, "ABT_barrier_free");

    /* Finalize */
    ret = ATS_finalize(0);

    free(xstreams);
    free(pthreads);
    free(g_pools);

    return ret;
}
