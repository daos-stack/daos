/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_xstream_barrier. */

void thread_func(void *arg)
{
    int ret = ABT_xstream_barrier_wait((ABT_xstream_barrier)arg);
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

    ABT_bool external_thread_support;
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                &external_thread_support);
    assert(ret == ABT_SUCCESS);

    ABT_xstream_barrier barrier = (ABT_xstream_barrier)RAND_PTR;
    ret = ABT_xstream_barrier_create(external_thread_support ? 2 : 1, &barrier);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        if (external_thread_support) {
            /* num_waiters = 2.  If an external thread is supported, use an
             * external thread. */
            pthread_t pthread;
            ret = pthread_create(&pthread, NULL, pthread_func, (void *)barrier);
            assert(!must_succeed || ret == 0);
            if (ret == 0) {
                ret = ABT_xstream_barrier_wait(barrier);
                assert(ret == ABT_SUCCESS);
                ret = pthread_join(pthread, NULL);
                assert(ret == 0);
            }
        } else {
            /* num_waiters = 1 */
            ret = ABT_xstream_barrier_wait(barrier);
            assert(ret == ABT_SUCCESS);
        }
        /* Free barrier. */
        ret = ABT_xstream_barrier_free(&barrier);
        assert(ret == ABT_SUCCESS && barrier == ABT_XSTREAM_BARRIER_NULL);
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(barrier == (ABT_xstream_barrier)RAND_PTR);
#else
        assert(barrier == ABT_XSTREAM_BARRIER_NULL);
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
