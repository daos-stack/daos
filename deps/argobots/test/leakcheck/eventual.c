/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_eventual. */

void thread_func(void *arg)
{
    int ret;
    ret = ABT_eventual_wait((ABT_eventual)arg, NULL);
    assert(ret == ABT_SUCCESS);
    ABT_bool is_ready;
    ret = ABT_eventual_test((ABT_eventual)arg, NULL, &is_ready);
    assert(ret == ABT_SUCCESS && is_ready == ABT_TRUE);
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

void program(int must_succeed)
{
    int ret, i;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    for (i = 0; i < 2; i++) {
        int nbytes = i * 128;
        ABT_eventual eventual = (ABT_eventual)RAND_PTR;
        ret = ABT_eventual_create(nbytes, &eventual);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            /* If an external thread is supported, use an external thread. */
            ABT_bool external_thread_support;
            ret = ABT_info_query_config(
                ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                &external_thread_support);
            assert(ret == ABT_SUCCESS);
            if (external_thread_support) {
                pthread_t pthread;
                ret = pthread_create(&pthread, NULL, pthread_func,
                                     (void *)eventual);
                assert(!must_succeed || ret == 0);
                if (ret == 0) {
                    ret = ABT_eventual_set(eventual, NULL, 0);
                    assert(ret == ABT_SUCCESS);
                    ret = pthread_join(pthread, NULL);
                    assert(ret == 0);
                    ret = ABT_eventual_reset(eventual);
                    assert(ret == ABT_SUCCESS);
                }
            }
            /* Create a ULT and synchronize it with eventual. */
            ABT_xstream self_xstream;
            ret = ABT_self_get_xstream(&self_xstream);
            assert(ret == ABT_SUCCESS);
            ABT_thread thread = (ABT_thread)RAND_PTR;
            ret = ABT_thread_create_on_xstream(self_xstream, thread_func,
                                               (void *)eventual,
                                               ABT_THREAD_ATTR_NULL, &thread);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret == ABT_SUCCESS) {
                ret = ABT_eventual_set(eventual, NULL, 0);
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
            /* Free eventual. */
            ret = ABT_eventual_free(&eventual);
            assert(ret == ABT_SUCCESS && eventual == ABT_EVENTUAL_NULL);
        } else {
            assert(eventual == (ABT_eventual)RAND_PTR);
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
