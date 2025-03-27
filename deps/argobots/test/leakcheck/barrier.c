/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_barrier. */

void thread_func(void *arg)
{
    int ret = ABT_barrier_wait((ABT_barrier)arg);
    assert(ret == ABT_SUCCESS);
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

    ABT_barrier barrier = (ABT_barrier)RAND_PTR;
    ret = ABT_barrier_create(2, &barrier);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        /* Make it large. */
        ret = ABT_barrier_reinit(barrier, 128);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            /* Shrink again. */
            ret = ABT_barrier_reinit(barrier, 2);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret != ABT_SUCCESS) {
                /* If shrink fails, we cannot continue this test. */
                ret = ABT_barrier_free(&barrier);
                assert(ret == ABT_SUCCESS && barrier == ABT_BARRIER_NULL);
                goto FINISH_TEST;
            }
        }
        /* Now # of waiters must be 2. */
        uint32_t num_waiters;
        ret = ABT_barrier_get_num_waiters(barrier, &num_waiters);
        assert(ret == ABT_SUCCESS && num_waiters == 2);
        /* If an external thread is supported, use an external thread. */
        ABT_bool external_thread_support;
        ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                    &external_thread_support);
        assert(ret == ABT_SUCCESS);
        if (external_thread_support) {
            pthread_t pthread;
            ret = pthread_create(&pthread, NULL, pthread_func, (void *)barrier);
            assert(!must_succeed || ret == 0);
            if (ret == 0) {
                ret = ABT_barrier_wait(barrier);
                assert(ret == ABT_SUCCESS);
                ret = pthread_join(pthread, NULL);
                assert(ret == 0);
            }
        }
        /* Create a ULT and wait on a barrier together. */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ABT_thread thread = (ABT_thread)RAND_PTR;
        ret = ABT_thread_create_on_xstream(self_xstream, thread_func,
                                           (void *)barrier,
                                           ABT_THREAD_ATTR_NULL, &thread);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            ret = ABT_barrier_wait(barrier);
            assert(ret == ABT_SUCCESS);
            ret = ABT_thread_free(&thread);
            assert(ret == ABT_SUCCESS && thread == ABT_THREAD_NULL);
        } else {
#ifdef ABT_ENABLE_VER_20_API
            assert(thread == (ABT_thread)RAND_PTR);
#else
            assert(thread == ABT_THREAD_NULL);
#endif
        }
        /* Free barrier. */
        ret = ABT_barrier_free(&barrier);
        assert(ret == ABT_SUCCESS && barrier == ABT_BARRIER_NULL);
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(barrier == (ABT_barrier)RAND_PTR);
#else
        assert(barrier == ABT_BARRIER_NULL);
#endif
    }

FINISH_TEST:
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
