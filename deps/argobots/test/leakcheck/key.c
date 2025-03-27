/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdlib.h>
#include <assert.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_key. */

ABT_key g_keys[3];

void destructor(void *val)
{
    free(val);
}

void set_self_data(int i, int must_succeed)
{
    if (g_keys[i] != ABT_KEY_NULL) {
        void *data;
        int ret;
        ret = ABT_self_get_specific(g_keys[i], &data);
        assert(ret == ABT_SUCCESS);
        if (data)
            free(data);
        data = malloc(128);
        ret = ABT_self_set_specific(g_keys[i], data);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS)
            free(data);
    }
}

void set_thread_data(ABT_thread thread, int i, int must_succeed)
{
    if (g_keys[i] != ABT_KEY_NULL) {
        void *data;
        int ret;
        ret = ABT_thread_get_specific(thread, g_keys[i], &data);
        assert(ret == ABT_SUCCESS);
        if (data)
            free(data);
        data = malloc(128);
        ret = ABT_thread_set_specific(thread, g_keys[i], data);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS)
            free(data);
    }
}

void thread_func(void *must_succeed)
{
    set_self_data(1, must_succeed ? 1 : 0);
    set_self_data(2, must_succeed ? 1 : 0);
}

void program(int must_succeed)
{
    int ret, i;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    /* Create keys. */
    for (i = 0; i < 3; i++) {
        g_keys[i] = (ABT_key)RAND_PTR;
        ret = ABT_key_create(destructor, &g_keys[i]);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            assert(g_keys[i] == (ABT_key)RAND_PTR);
            g_keys[i] = ABT_KEY_NULL;
        }
    }

    /* Create a ULT and use those keys. */
    ABT_xstream self_xstream;
    ret = ABT_self_get_xstream(&self_xstream);
    assert(ret == ABT_SUCCESS);

    ABT_thread threads[4] = { ABT_THREAD_NULL, ABT_THREAD_NULL, ABT_THREAD_NULL,
                              ABT_THREAD_NULL };
    for (i = 0; i < 4; i++) {
        if (i == 2 && g_keys[2] != ABT_KEY_NULL) {
            /* The destructor must be called even after ABT_key is freed. */
            ret = ABT_key_free(&g_keys[2]);
            assert(ret == ABT_SUCCESS && g_keys[2] == ABT_KEY_NULL);
        }
        if (i % 2 == 0) {
            /* Named */
            threads[i] = (ABT_thread)RAND_PTR;
            ret =
                ABT_thread_create_on_xstream(self_xstream, thread_func,
                                             must_succeed ? &must_succeed
                                                          : NULL,
                                             ABT_THREAD_ATTR_NULL, &threads[i]);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret == ABT_SUCCESS) {
                set_thread_data(threads[i], 0, must_succeed);
                set_thread_data(threads[i], 2, must_succeed);
            } else {
#ifdef ABT_ENABLE_VER_20_API
                assert(threads[i] == (ABT_thread)RAND_PTR);
                threads[i] = ABT_THREAD_NULL;
#else
                assert(threads[i] == ABT_THREAD_NULL);
#endif
            }
        } else {
            /* Unnamed */
            ret = ABT_thread_create_on_xstream(self_xstream, thread_func, NULL,
                                               ABT_THREAD_ATTR_NULL, NULL);
            assert(!must_succeed || ret == ABT_SUCCESS);
        }
    }
    /* Run thread_func() on the primary ULT. */
    thread_func(must_succeed ? &must_succeed : NULL);
    for (i = 0; i < 4; i++) {
        if (threads[i] != ABT_THREAD_NULL) {
            ret = ABT_thread_free(&threads[i]);
            assert(ret == ABT_SUCCESS && threads[i] == ABT_THREAD_NULL);
        }
    }

    /* Free all the keys. */
    for (i = 0; i < 3; i++) {
        if (g_keys[i] != ABT_KEY_NULL) {
            ret = ABT_key_free(&g_keys[i]);
            assert(ret == ABT_SUCCESS && g_keys[i] == ABT_KEY_NULL);
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
