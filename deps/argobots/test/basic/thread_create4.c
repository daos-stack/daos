/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "abt.h"
#include "abttest.h"

/* This test checks the order of execution. */

int g_counter = 0;
void thread_func(void *arg)
{
    g_counter++;
}

int main(int argc, char *argv[])
{
    int i;
    int ret;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    ABT_pool main_pool;
    ret = ABT_self_get_last_pool(&main_pool);
    ATS_ERROR(ret, "ABT_self_get_last_pool");

    for (i = 0; i < 5; i++) {
        ABT_thread thread;
        size_t size;
        g_counter = 0;

        /* Parent-first (ABT_thread_create) */
        ret = ABT_thread_create(main_pool, thread_func, NULL,
                                ABT_THREAD_ATTR_NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        assert(g_counter == 0);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 1);

        ret = ABT_thread_join(thread);
        ATS_ERROR(ret, "ABT_thread_join");
        assert(g_counter == 1);

        /* Parent-first (ABT_thread_revive) */
        ret = ABT_thread_revive(main_pool, thread_func, NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_revive");
        assert(g_counter == 1);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 1);

        ret = ABT_thread_join(thread);
        ATS_ERROR(ret, "ABT_thread_join");
        assert(g_counter == 2);

        /* Child-first (ABT_thread_revive_to) */
        ret = ABT_thread_revive_to(main_pool, thread_func, NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_revive_to");
        assert(g_counter == 3);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 0);

        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_counter == 3);

        /* Child-first (ABT_thread_create_to) */
        ret = ABT_thread_create_to(main_pool, thread_func, NULL,
                                   ABT_THREAD_ATTR_NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_create_to");
        assert(g_counter == 4);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 0);

        ret = ABT_thread_join(thread);
        ATS_ERROR(ret, "ABT_thread_join");
        assert(g_counter == 4);

        /* Parent-first (ABT_thread_revive) */
        ret = ABT_thread_revive(main_pool, thread_func, NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_revive");
        assert(g_counter == 4);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 1);

        ret = ABT_thread_join(thread);
        ATS_ERROR(ret, "ABT_thread_join");
        assert(g_counter == 5);

        /* Child-first (ABT_thread_revive_to) */
        ret = ABT_thread_revive_to(main_pool, thread_func, NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_revive_to");
        assert(g_counter == 6);

        ret = ABT_pool_get_size(main_pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(size == 0);

        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_counter == 6);
    }

    /* Finalize */
    ret = ATS_finalize(0);
    return ret;
}
