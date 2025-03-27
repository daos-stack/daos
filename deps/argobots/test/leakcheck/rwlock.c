/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_rwlock. */

void thread_func(void *arg)
{
    int ret, i, repeat;
    ABT_rwlock rwlock = (ABT_rwlock)arg;
    for (repeat = 0; repeat < 100; repeat++) {
        for (i = 0; i < 5; i++) {
            ret = ABT_rwlock_rdlock(rwlock);
            assert(ret == ABT_SUCCESS);
        }
        for (i = 0; i < 5; i++) {
            ret = ABT_rwlock_unlock(rwlock);
            assert(ret == ABT_SUCCESS);
        }
        ret = ABT_rwlock_wrlock(rwlock);
        assert(ret == ABT_SUCCESS);
        ret = ABT_rwlock_unlock(rwlock);
        assert(ret == ABT_SUCCESS);
    }
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

void program(int must_succeed)
{
    int ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    ABT_rwlock rwlock = (ABT_rwlock)RAND_PTR;
    ret = ABT_rwlock_create(&rwlock);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        /* If an external thread is supported, use an external thread. */
        ABT_bool external_thread_support;
        ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                    &external_thread_support);
        assert(ret == ABT_SUCCESS);
        if (external_thread_support) {
            pthread_t pthread;
            ret = pthread_create(&pthread, NULL, pthread_func, (void *)rwlock);
            assert(!must_succeed || ret == 0);
            if (ret == 0) {
                thread_func((void *)rwlock);
                ret = pthread_join(pthread, NULL);
                assert(ret == 0);
            }
        }
        /* Create a ULT and synchronize it with rwlock. */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ABT_thread thread = (ABT_thread)RAND_PTR;
        ret = ABT_thread_create_on_xstream(self_xstream, thread_func,
                                           (void *)rwlock, ABT_THREAD_ATTR_NULL,
                                           &thread);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            thread_func((void *)rwlock);
            ret = ABT_thread_free(&thread);
            assert(ret == ABT_SUCCESS && thread == ABT_THREAD_NULL);
        } else {
#ifdef ABT_ENABLE_VER_20_API
            assert(thread == (ABT_thread)RAND_PTR);
#else
            assert(thread == ABT_THREAD_NULL);
#endif
        }
        /* Free rwlock. */
        ret = ABT_rwlock_free(&rwlock);
        assert(ret == ABT_SUCCESS && rwlock == ABT_RWLOCK_NULL);
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(rwlock == (ABT_rwlock)RAND_PTR);
#else
        assert(rwlock == ABT_RWLOCK_NULL);
#endif
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
