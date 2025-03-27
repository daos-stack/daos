/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

/* This test checks if ABT_mutex works with external threads or not.  This test
 * specifically focuses on whether ABT_mutex that internally uses pthread_cond_t
 * or futex works even if it spuriously wakes up because of signals. */

#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_PTHREADS 4
#define DEFAULT_NUM_THREADS 4
#define DEFAULT_NUM_ITER 5000

ABT_mutex_memory g_mutex_mem = ABT_MUTEX_INITIALIZER;
ABT_mutex_memory g_rec_mutex_mem = ABT_RECURSIVE_MUTEX_INITIALIZER;

typedef struct {
    ABT_mutex mutex;
    int counter;
    ABT_bool is_recursive;
    ABT_bool is_dynamic; /* Dynamically allocated? */
} mutex_set;
mutex_set g_mutex_sets[6];
int g_iter = DEFAULT_NUM_ITER;

#define NUM_MUTEX_SETS ((int)(sizeof(g_mutex_sets) / sizeof(g_mutex_sets[0])))

static int trylock(ABT_mutex mutex)
{
    while (ABT_mutex_trylock(mutex) != ABT_SUCCESS)
        ;
    return ABT_SUCCESS;
}

void thread_func(void *arg)
{
    int (*lock_fs[])(ABT_mutex) = { ABT_mutex_lock, ABT_mutex_lock_high,
                                    ABT_mutex_lock_low, trylock,
                                    ABT_mutex_spinlock };
    int (*unlock_fs[])(ABT_mutex) = { ABT_mutex_unlock, ABT_mutex_unlock_se,
                                      ABT_mutex_unlock_de };

    int i;
    for (i = 0; i < g_iter; i++) {
        int j;
        for (j = 0; j < NUM_MUTEX_SETS; j++) {
            int k;
            if (g_mutex_sets[j].mutex == ABT_MUTEX_NULL)
                continue;
            for (k = 0; k < (g_mutex_sets[j].is_recursive ? 5 : 1); k++)
                lock_fs[i % (sizeof(lock_fs) / sizeof(lock_fs[0]))](
                    g_mutex_sets[j].mutex);
            g_mutex_sets[j].counter++;
            for (k = 0; k < (g_mutex_sets[j].is_recursive ? 5 : 1); k++)
                unlock_fs[i % (sizeof(unlock_fs) / sizeof(unlock_fs[0]))](
                    g_mutex_sets[j].mutex);
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
    int i, j, kind, ret;
    int num_xstreams = DEFAULT_NUM_XSTREAMS;
    int num_pthreads = DEFAULT_NUM_PTHREADS;
    int num_threads = DEFAULT_NUM_THREADS;
    int expected = 0, expected_dynamic = 0;
    ABT_mutex_memory mutex_mem = ABT_MUTEX_INITIALIZER;
    ABT_mutex_memory rec_mutex_mem = ABT_RECURSIVE_MUTEX_INITIALIZER;

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
    if (!support_external_thread) {
        ATS_ERROR(ABT_ERR_FEATURE_NA, "ABT_info_query_config");
    }

    /* Set up mutex. */
    g_mutex_sets[0].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_mutex_mem);
    g_mutex_sets[0].counter = 0;
    g_mutex_sets[0].is_recursive = ABT_FALSE;
    g_mutex_sets[0].is_dynamic = ABT_FALSE;
    g_mutex_sets[1].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_rec_mutex_mem);
    g_mutex_sets[1].counter = 0;
    g_mutex_sets[1].is_recursive = ABT_TRUE;
    g_mutex_sets[1].is_dynamic = ABT_FALSE;
    g_mutex_sets[2].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutex_mem);
    g_mutex_sets[2].counter = 0;
    g_mutex_sets[2].is_recursive = ABT_FALSE;
    g_mutex_sets[2].is_dynamic = ABT_FALSE;
    g_mutex_sets[3].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&rec_mutex_mem);
    g_mutex_sets[3].counter = 0;
    g_mutex_sets[3].is_recursive = ABT_TRUE;
    g_mutex_sets[4].is_dynamic = ABT_FALSE;
    /* ABT_mutex_create() cannot be called before ABT_init() and after
     * ABT_finalize() */
    g_mutex_sets[4].mutex = ABT_MUTEX_NULL;
    g_mutex_sets[5].mutex = ABT_MUTEX_NULL;

    /* Use mutex before ABT_Init(). */
    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        ATS_destroy_timer();
        /* This loop does not test dynamically allocated mutex. */
        expected += num_pthreads * g_iter;
    }

    /* Initialize */
    ATS_init(argc, argv, num_xstreams);

    ATS_printf(2, "# of ESs : %d\n", num_xstreams);
    ATS_printf(1, "# of ULTs: %d\n", num_threads);
    ATS_printf(1, "# of iter: %d\n", g_iter);

    /* Allocate dynamically allocated mutex. */
    ret = ABT_mutex_create(&g_mutex_sets[4].mutex);
    ATS_ERROR(ret, "ABT_mutex_create");
    g_mutex_sets[4].counter = 0;
    g_mutex_sets[4].is_recursive = ABT_FALSE;
    g_mutex_sets[4].is_dynamic = ABT_TRUE;
    ABT_mutex_attr mutex_attr;
    ret = ABT_mutex_attr_create(&mutex_attr);
    ATS_ERROR(ret, "ABT_mutex_attr_create");
    ret = ABT_mutex_attr_set_recursive(mutex_attr, ABT_TRUE);
    ATS_ERROR(ret, "ABT_mutex_attr_set_recursive");
    ret = ABT_mutex_create_with_attr(mutex_attr, &g_mutex_sets[5].mutex);
    ATS_ERROR(ret, "ABT_mutex_create_with_attr");
    ret = ABT_mutex_attr_free(&mutex_attr);
    ATS_ERROR(ret, "ABT_mutex_attr_free");
    g_mutex_sets[5].counter = 0;
    g_mutex_sets[5].is_recursive = ABT_TRUE;
    g_mutex_sets[5].is_dynamic = ABT_TRUE;

    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_xstreams * num_threads);

    /* Create Execution Streams */
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

    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        /* Create ULTs */
        for (i = 0; i < num_xstreams; i++) {
            for (j = 0; j < num_threads; j++) {
                ret = ABT_thread_create(pools[i], thread_func, NULL,
                                        ABT_THREAD_ATTR_NULL,
                                        &threads[i * num_threads + j]);
                ATS_ERROR(ret, "ABT_thread_create");
            }
        }
        expected += num_xstreams * num_threads * g_iter;
        expected_dynamic += num_xstreams * num_threads * g_iter;
        /* Create Pthreads too. */
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        expected += num_pthreads * g_iter;
        expected_dynamic += num_pthreads * g_iter;

        /* Join and free ULTs */
        for (i = 0; i < num_xstreams; i++) {
            for (j = 0; j < num_threads; j++) {
                ret = ABT_thread_free(&threads[i * num_threads + j]);
                ATS_ERROR(ret, "ABT_thread_free");
            }
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

    /* Free dynamically allocated mutex. */
    ret = ABT_mutex_free(&g_mutex_sets[4].mutex);
    ATS_ERROR(ret, "ABT_mutex_free");
    ret = ABT_mutex_free(&g_mutex_sets[5].mutex);
    ATS_ERROR(ret, "ABT_mutex_free");

    /* Finalize */
    ret = ATS_finalize(0);

    /* Use the mutex after finalization. */
    for (kind = 0; kind < ATS_TIMER_KIND_LAST_; kind++) {
        ATS_create_timer((ATS_timer_kind)kind);
        pthread_t *pthreads =
            (pthread_t *)malloc(sizeof(pthread_t) * num_pthreads);
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_create(&pthreads[i], NULL, pthread_func, NULL);
            assert(ret == 0);
        }
        for (i = 0; i < num_pthreads; i++) {
            ret = pthread_join(pthreads[i], NULL);
            assert(ret == 0);
        }
        free(pthreads);
        ATS_destroy_timer();
        expected += num_pthreads * g_iter;
        /* This test does not check dynamically allocated mutex since they have
         * already been freed. */
    }

    /* Validation */
    for (i = 0; i < NUM_MUTEX_SETS; i++) {
        if (g_mutex_sets[i].is_dynamic) {
            assert(g_mutex_sets[i].counter == expected_dynamic);
        } else {
            assert(g_mutex_sets[i].counter == expected);
        }
    }

    free(threads);
    free(xstreams);
    free(pools);

    return ret;
}
