/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_future. */

#define NUM_COMPARTMENTS 64

void thread_func(void *arg)
{
    int ret, i;
    for (i = 0; i < NUM_COMPARTMENTS / 2; i++) {
        ret = ABT_future_set((ABT_future)arg, &i);
    }
    ret = ABT_future_wait((ABT_future)arg);
    assert(ret == ABT_SUCCESS);
    ABT_bool is_ready;
    ret = ABT_future_test((ABT_future)arg, &is_ready);
    assert(ret == ABT_SUCCESS && is_ready == ABT_TRUE);
}

void *pthread_func(void *arg)
{
    thread_func(arg);
    return NULL;
}

void cb_func(void **arg)
{
    /* Only two values. */
    void *ptr1 = NULL, *ptr2 = NULL;
    int i, num1 = 0, num2 = 0;
    for (i = 0; i < NUM_COMPARTMENTS; i++) {
        assert(arg[i] != NULL);
        if (ptr1 == NULL) {
            ptr1 = arg[i];
            num1++;
        } else if (ptr1 == arg[i]) {
            num1++;
        } else if (ptr2 == NULL) {
            ptr2 = arg[i];
            num2++;
        } else if (ptr2 == arg[i]) {
            num2++;
        } else {
            assert(0);
        }
    }
    assert(num1 == NUM_COMPARTMENTS / 2 && num2 == NUM_COMPARTMENTS / 2);
}

void program(int must_succeed)
{
    int ret, i, use_cb_func;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    for (use_cb_func = 0; use_cb_func < 2; use_cb_func++) {
        ABT_future future = (ABT_future)RAND_PTR;
        ret = ABT_future_create(NUM_COMPARTMENTS, use_cb_func ? cb_func : NULL,
                                &future);
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
                                     (void *)future);
                assert(!must_succeed || ret == 0);
                if (ret == 0) {
                    for (i = 0; i < NUM_COMPARTMENTS / 2; i++) {
                        ret = ABT_future_set(future, &i);
                        assert(ret == ABT_SUCCESS);
                    }
                    ret = ABT_future_wait(future);
                    assert(ret == ABT_SUCCESS);
                    ret = pthread_join(pthread, NULL);
                    assert(ret == 0);
                    ret = ABT_future_reset(future);
                    assert(ret == ABT_SUCCESS);
                }
            }
            /* Create a ULT and synchronize it with future. */
            ABT_xstream self_xstream;
            ret = ABT_self_get_xstream(&self_xstream);
            assert(ret == ABT_SUCCESS);
            ABT_thread thread = (ABT_thread)RAND_PTR;
            ret = ABT_thread_create_on_xstream(self_xstream, thread_func,
                                               (void *)future,
                                               ABT_THREAD_ATTR_NULL, &thread);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret == ABT_SUCCESS) {
                for (i = 0; i < NUM_COMPARTMENTS / 2; i++) {
                    ret = ABT_future_set(future, &i);
                    assert(ret == ABT_SUCCESS);
                }
                ret = ABT_future_wait(future);
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
            /* Free future. */
            ret = ABT_future_free(&future);
            assert(ret == ABT_SUCCESS && future == ABT_FUTURE_NULL);
        } else {
            assert(future == (ABT_future)RAND_PTR);
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
