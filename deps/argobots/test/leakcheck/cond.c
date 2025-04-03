/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* This test checks if ABT_cond works with external threads or not.  This test
 * specifically focuses on whether ABT_con that internally uses pthread_cond_t
 * or futex works even if it spuriously wakes up because of signals. */

#define DEFAULT_NUM_ITER 20

ABT_mutex_memory g_mutex_mem = ABT_MUTEX_INITIALIZER;
ABT_mutex_memory g_cond_mem = ABT_COND_INITIALIZER;

typedef struct {
    ABT_mutex mutex;
    ABT_cond cond;
    ABT_bool is_dynamic;
} mutex_cond_set;
mutex_cond_set g_mutex_cond_sets[2];

#define NUM_MUTEX_COND_SETS                                                    \
    ((int)(sizeof(g_mutex_cond_sets) / sizeof(g_mutex_cond_sets[0])))
#define NUM_ITERS 5

int g_val = 0;
void thread_func(void *arg)
{
    (void)arg;
    int i;
    for (i = 0; i < NUM_ITERS; i++) {
        int j;
        for (j = 0; j < NUM_MUTEX_COND_SETS; j++) {
            if (g_mutex_cond_sets[j].mutex == ABT_MUTEX_NULL)
                continue;
            int ret;
            /* Check signal. */
            ret = ABT_mutex_lock(g_mutex_cond_sets[j].mutex);
            assert(ret == ABT_SUCCESS);
            g_val++;
            if (g_val % 2 == 1) {
                ret = ABT_cond_wait(g_mutex_cond_sets[j].cond,
                                    g_mutex_cond_sets[j].mutex);
                assert(ret == ABT_SUCCESS);
                ret = ABT_cond_broadcast(g_mutex_cond_sets[j].cond);
                assert(ret == ABT_SUCCESS);
            } else {
                ret = ABT_cond_signal(g_mutex_cond_sets[j].cond);
                assert(ret == ABT_SUCCESS);
                ret = ABT_cond_wait(g_mutex_cond_sets[j].cond,
                                    g_mutex_cond_sets[j].mutex);
                assert(ret == ABT_SUCCESS);
            }
            ret = ABT_mutex_unlock(g_mutex_cond_sets[j].mutex);
            assert(ret == ABT_SUCCESS);
        }
    }
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

void program(int type, int must_succeed)
{
    int i, ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    if (type == 0)
        rtrace_set_enabled(1);

    /* Set up mutex and condition variable. */
    g_mutex_cond_sets[0].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_mutex_mem);
    g_mutex_cond_sets[0].cond = ABT_COND_MEMORY_GET_HANDLE(&g_cond_mem);
    g_mutex_cond_sets[0].is_dynamic = ABT_FALSE;

    g_mutex_cond_sets[1].mutex = (ABT_mutex)RAND_PTR;
    ret = ABT_mutex_create(&g_mutex_cond_sets[1].mutex);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        g_mutex_cond_sets[1].cond = (ABT_cond)RAND_PTR;
        ret = ABT_cond_create(&g_mutex_cond_sets[1].cond);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            g_mutex_cond_sets[1].is_dynamic = ABT_TRUE;
        } else {
#ifdef ABT_ENABLE_VER_20_API
            assert(g_mutex_cond_sets[1].cond == (ABT_cond)RAND_PTR);
            g_mutex_cond_sets[1].cond = ABT_COND_NULL;
#else
            assert(g_mutex_cond_sets[1].cond == ABT_COND_NULL);
#endif
            ret = ABT_mutex_free(&g_mutex_cond_sets[1].mutex);
            assert(ret == ABT_SUCCESS &&
                   g_mutex_cond_sets[1].mutex == ABT_MUTEX_NULL);
        }
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(g_mutex_cond_sets[1].mutex == (ABT_mutex)RAND_PTR);
        g_mutex_cond_sets[1].mutex = ABT_MUTEX_NULL;
#else
        assert(g_mutex_cond_sets[1].mutex == ABT_MUTEX_NULL);
#endif
    }
    if (type == 1)
        rtrace_set_enabled(1);

    if (type == 0) {
        /* Create a ULT and synchronize it with mutex. */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ABT_thread thread = (ABT_thread)RAND_PTR;
        ret = ABT_thread_create_on_xstream(self_xstream, thread_func, NULL,
                                           ABT_THREAD_ATTR_NULL, &thread);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            thread_func(NULL);
            ret = ABT_thread_free(&thread);
            assert(ret == ABT_SUCCESS && thread == ABT_THREAD_NULL);
        } else {
#ifdef ABT_ENABLE_VER_20_API
            assert(thread == (ABT_thread)RAND_PTR);
#else
            assert(thread == ABT_THREAD_NULL);
#endif
        }
    } else if (type == 1) {
        /* If an external thread is supported, use an external thread. */
        ABT_bool external_thread_support;
        ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                    &external_thread_support);
        assert(ret == ABT_SUCCESS);
        if (external_thread_support) {
            pthread_t pthread;
            ret = pthread_create(&pthread, NULL, pthread_func, NULL);
            assert(!must_succeed || ret == 0);
            if (ret == 0) {
                thread_func(NULL);
                ret = pthread_join(pthread, NULL);
                assert(ret == 0);
            }
        }
    }
    /* Free data structure. */
    for (i = 0; i < NUM_MUTEX_COND_SETS; i++) {
        if (g_mutex_cond_sets[i].is_dynamic &&
            g_mutex_cond_sets[i].mutex != ABT_MUTEX_NULL) {
            ret = ABT_mutex_free(&g_mutex_cond_sets[i].mutex);
            assert(ret == ABT_SUCCESS &&
                   g_mutex_cond_sets[i].mutex == ABT_MUTEX_NULL);
            ret = ABT_cond_free(&g_mutex_cond_sets[i].cond);
            assert(ret == ABT_SUCCESS &&
                   g_mutex_cond_sets[i].cond == ABT_COND_NULL);
        }
    }

    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
}

int main()
{
    setup_env();
    rtrace_init();

    int type;
    for (type = 0; type < 2; type++) {
        if (use_rtrace()) {
            do {
                rtrace_start();
                program(type, 0);
            } while (!rtrace_stop());
        }

        /* If no failure, it should succeed again. */
        program(type, 1);
    }

    rtrace_finalize();
    return 0;
}
