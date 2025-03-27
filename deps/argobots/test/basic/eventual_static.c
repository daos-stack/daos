/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_PTHREADS 4
#define DEFAULT_NUM_THREADS 4
#define DEFAULT_NUM_ITER 40

ABT_eventual_memory g_eventual_mem = ABT_EVENTUAL_INITIALIZER;

typedef struct {
    ABT_eventual eventual;
    int counter;
} eventual_set;
eventual_set g_eventual_sets[2];
int g_iter = DEFAULT_NUM_ITER;

#define NUM_EVENTUAL_SETS                                                      \
    ((int)(sizeof(g_eventual_sets) / sizeof(g_eventual_sets[0])))

void barrier(int num_waiters)
{
    static ABT_mutex_memory mutex_mem = ABT_MUTEX_INITIALIZER;
    static ABT_cond_memory cond_mem = ABT_COND_INITIALIZER;
    static int wait_counter = 0;
    ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutex_mem);
    ABT_cond cond = ABT_COND_MEMORY_GET_HANDLE(&cond_mem);

    int ret;
    /* Increment a counter. */
    ret = ABT_mutex_lock(mutex);
    ATS_ERROR(ret, "ABT_mutex_lock");
    wait_counter++;
    if (wait_counter < num_waiters) {
        ret = ABT_cond_wait(cond, mutex);
        ATS_ERROR(ret, "ABT_cond_wait");
    } else {
        wait_counter = 0;
        ret = ABT_cond_broadcast(cond);
        ATS_ERROR(ret, "ABT_cond_broadcast");
    }
    ret = ABT_mutex_unlock(mutex);
    ATS_ERROR(ret, "ABT_mutex_unlock");
}

typedef struct {
    int tid;
    int num_total_threads;
} thread_arg_t;

void thread_func(void *arg)
{
    int i, ret;
    thread_arg_t *p_arg = (thread_arg_t *)arg;
    for (i = 0; i < g_iter; i++) {
        int j;
        for (j = 0; j < NUM_EVENTUAL_SETS; j++) {
            if (p_arg->tid == g_iter % p_arg->num_total_threads) {
                if (i == 0) {
                    g_eventual_sets[j].counter = 0;
                } else {
                    assert(g_eventual_sets[j].counter == i);
                }
                g_eventual_sets[j].counter += 1;
                ret = ABT_eventual_set(g_eventual_sets[j].eventual, NULL, 0);
                ATS_ERROR(ret, "ABT_eventual_set");
            } else {
                ret = ABT_eventual_wait(g_eventual_sets[j].eventual, NULL);
                ATS_ERROR(ret, "ABT_eventual_wait");
            }
            assert(g_eventual_sets[j].counter == i + 1);
            barrier(p_arg->num_total_threads);
            if (p_arg->tid == g_iter % p_arg->num_total_threads) {
                ret = ABT_eventual_reset(g_eventual_sets[j].eventual);
                ATS_ERROR(ret, "ABT_eventual_reset");
            }
            barrier(p_arg->num_total_threads);
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
    int i, j, ret;
    int num_xstreams = DEFAULT_NUM_XSTREAMS;
    int num_pthreads = DEFAULT_NUM_PTHREADS;
    int num_threads = DEFAULT_NUM_THREADS;
    ABT_eventual_memory eventual_mem = ABT_EVENTUAL_INITIALIZER;

    /* Read arguments. */
    ATS_read_args(argc, argv);
    if (argc >= 2) {
        num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
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

    /* Set up eventuals. */
    g_eventual_sets[0].eventual =
        ABT_EVENTUAL_MEMORY_GET_HANDLE(&g_eventual_mem);
    g_eventual_sets[1].eventual = ABT_EVENTUAL_MEMORY_GET_HANDLE(&eventual_mem);

    /* Allocate thread_args. */
    thread_arg_t *thread_args = (thread_arg_t *)malloc(
        sizeof(thread_arg_t) * (num_pthreads + num_xstreams * num_threads));

    /* Currently, eventual functions need Argobots initialization. */
#if 0
    /* Use eventuals before ABT_Init(). */
    if (support_external_thread) {
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            thread_args[i].tid = i;
            thread_args[i].num_total_threads = num_pthreads;
            ret = pthread_create(&pthreads[i], NULL, pthread_func, &thread_args[i]);
            assert(ret == 0);
        }
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
    }
#endif

    /* Initialize */
    ATS_init(argc, argv, num_xstreams);

    ATS_printf(2, "# of ESs : %d\n", num_xstreams);
    ATS_printf(1, "# of ULTs: %d\n", num_threads);
    ATS_printf(1, "# of iter: %d\n", g_iter);

    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_xstreams * num_threads);

    /* Create execution streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    /* Get the pools attached to an execution stream */
    ABT_pool *pools;
    pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
    for (i = 0; i < num_xstreams; i++) {
        ret = ABT_xstream_get_main_pools(xstreams[i], 1, pools + i);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");
    }

    /* Create ULTs */
    const int num_total_threads = (support_external_thread ? num_pthreads : 0) +
                                  num_xstreams * num_threads;
    for (i = 0; i < num_xstreams; i++) {
        for (j = 0; j < num_threads; j++) {
            int tid = i * num_threads + j;
            thread_args[tid].tid = tid;
            thread_args[tid].num_total_threads = num_total_threads;
            ret = ABT_thread_create(pools[i], thread_func, &thread_args[tid],
                                    ABT_THREAD_ATTR_NULL,
                                    &threads[i * num_threads + j]);
            ATS_ERROR(ret, "ABT_thread_create");
        }
    }

    /* Create Pthreads. */
    pthread_t *pthreads;
    if (support_external_thread) {
        pthreads = (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            int tid = num_xstreams * num_threads + i;
            thread_args[tid].tid = tid;
            thread_args[tid].num_total_threads = num_total_threads;
            ret = pthread_create(&pthreads[i], NULL, pthread_func,
                                 &thread_args[tid]);
            assert(ret == 0);
        }
    }

    /* Join and free ULTs */
    for (i = 0; i < num_xstreams; i++) {
        for (j = 0; j < num_threads; j++) {
            ret = ABT_thread_free(&threads[i * num_threads + j]);
            ATS_ERROR(ret, "ABT_thread_free");
        }
    }

    /* Join Pthreads. */
    if (support_external_thread) {
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
    }

    /* Join execution streams */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_join(xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_join");
    }

    /* Free execution streams */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    /* Currently, eventual functions need Argobots initialization. */
#if 0
    /* Use eventuals after finalization. */
    if (support_external_thread) {
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            thread_args[i].tid = i;
            thread_args[i].num_total_threads = num_pthreads;
            ret = pthread_create(&pthreads[i], NULL, pthread_func, &thread_args[i]);
            assert(ret == 0);
        }
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
    }
#endif

    free(threads);
    free(xstreams);
    free(pools);
    free(thread_args);

    return ret;
}
