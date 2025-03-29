/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_mutex. */
ABT_mutex_memory g_mutex_mem = ABT_MUTEX_INITIALIZER;
ABT_mutex_memory g_rec_mutex_mem = ABT_RECURSIVE_MUTEX_INITIALIZER;

typedef struct {
    ABT_mutex mutex;
    ABT_bool is_recursive;
    ABT_bool is_dynamic;
} mutex_set;
mutex_set g_mutex_sets[4];
#define NUM_MUTEX_SETS ((int)(sizeof(g_mutex_sets) / sizeof(g_mutex_sets[0])))

#define NUM_ITERS 5

static int trylock(ABT_mutex mutex)
{
    int ret;
    while (1) {
        ret = ABT_mutex_trylock(mutex);
        if (ret == ABT_SUCCESS)
            return ret;
        assert(ret == ABT_ERR_MUTEX_LOCKED);
        ABT_unit_type unit_type;
        ret = ABT_self_get_type(&unit_type);
#ifdef ABT_ENABLE_VER_20_API
        assert(ret == ABT_SUCCESS);
#else
        assert(ret == ABT_SUCCESS || ret == ABT_ERR_INV_XSTREAM);
#endif
        if (unit_type == ABT_UNIT_TYPE_THREAD) {
            ret = ABT_self_yield();
            assert(ret == ABT_SUCCESS);
        }
    }
}

void thread_func(void *arg)
{
    (void)arg;
    int (*lock_fs[])(ABT_mutex) = { ABT_mutex_lock, ABT_mutex_lock_high,
                                    ABT_mutex_lock_low, trylock,
                                    ABT_mutex_spinlock };
    int (*unlock_fs[])(ABT_mutex) = { ABT_mutex_unlock, ABT_mutex_unlock_se,
                                      ABT_mutex_unlock_de };

    int i;
    for (i = 0; i < NUM_ITERS; i++) {
        int j;
        for (j = 0; j < NUM_MUTEX_SETS; j++) {
            int k;
            if (g_mutex_sets[j].mutex == ABT_MUTEX_NULL)
                continue;
            int ret;
            for (k = 0; k < (g_mutex_sets[j].is_recursive ? 5 : 1); k++) {
                ret = lock_fs[i % (sizeof(lock_fs) / sizeof(lock_fs[0]))](
                    g_mutex_sets[j].mutex);
                assert(ret == ABT_SUCCESS);
            }
            for (k = 0; k < (g_mutex_sets[j].is_recursive ? 5 : 1); k++) {
                ret = unlock_fs[i % (sizeof(unlock_fs) / sizeof(unlock_fs[0]))](
                    g_mutex_sets[j].mutex);
                assert(ret == ABT_SUCCESS);
            }
        }
    }
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

void program(int must_succeed)
{
    int i, ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    /* Set up mutex. */
    g_mutex_sets[0].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_mutex_mem);
    g_mutex_sets[0].is_recursive = ABT_FALSE;
    g_mutex_sets[0].is_dynamic = ABT_FALSE;
    g_mutex_sets[1].mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&g_rec_mutex_mem);
    g_mutex_sets[1].is_recursive = ABT_TRUE;
    g_mutex_sets[1].is_dynamic = ABT_FALSE;

    g_mutex_sets[2].mutex = (ABT_mutex)RAND_PTR;
    ret = ABT_mutex_create(&g_mutex_sets[2].mutex);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        g_mutex_sets[2].is_recursive = ABT_FALSE;
        g_mutex_sets[2].is_dynamic = ABT_TRUE;
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(g_mutex_sets[2].mutex == (ABT_mutex)RAND_PTR);
        g_mutex_sets[2].mutex = ABT_MUTEX_NULL;
#else
        assert(g_mutex_sets[2].mutex == ABT_MUTEX_NULL);
#endif
    }
    g_mutex_sets[3].mutex = ABT_MUTEX_NULL;
    ABT_mutex_attr mutex_attr = (ABT_mutex_attr)RAND_PTR;
    ret = ABT_mutex_attr_create(&mutex_attr);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        ret = ABT_mutex_attr_set_recursive(mutex_attr, ABT_TRUE);
        assert(ret == ABT_SUCCESS);
        g_mutex_sets[3].mutex = (ABT_mutex)RAND_PTR;
        ret = ABT_mutex_create_with_attr(mutex_attr, &g_mutex_sets[3].mutex);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(g_mutex_sets[3].mutex == (ABT_mutex)RAND_PTR);
#else
            assert(g_mutex_sets[3].mutex == ABT_MUTEX_NULL);
#endif
            /* Maybe the second time will succeed. */
            ret =
                ABT_mutex_create_with_attr(mutex_attr, &g_mutex_sets[3].mutex);
        }
        if (ret == ABT_SUCCESS) {
            g_mutex_sets[3].is_recursive = ABT_TRUE;
            g_mutex_sets[3].is_dynamic = ABT_TRUE;
        } else {
            g_mutex_sets[3].mutex = ABT_MUTEX_NULL;
        }
        ret = ABT_mutex_attr_free(&mutex_attr);
        assert(ret == ABT_SUCCESS);
    } else {
        assert(mutex_attr == (ABT_mutex_attr)RAND_PTR);
    }
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
    /* Free mutex. */
    for (i = 0; i < NUM_MUTEX_SETS; i++) {
        if (g_mutex_sets[i].is_dynamic &&
            g_mutex_sets[i].mutex != ABT_MUTEX_NULL) {
            ret = ABT_mutex_free(&g_mutex_sets[i].mutex);
            assert(ret == ABT_SUCCESS &&
                   g_mutex_sets[i].mutex == ABT_MUTEX_NULL);
        }
    }

    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
}

int main()
{
    setup_env();
    rtrace_init();

    if (use_rtrace()) {
        do {
            rtrace_start();
            program(0);
        } while (!rtrace_stop());
    }

    /* If no failure, it should succeed again. */
    program(1);

    rtrace_finalize();
    return 0;
}
