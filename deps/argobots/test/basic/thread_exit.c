/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

#define NUM_THREADS 100

int g_count = 0;

void thread_func(void *arg)
{
    g_count++;
    if (arg == NULL) {
        ABT_thread_exit();
    } else {
        ABT_self_exit();
    }
    assert(0);
}

int main(int argc, char *argv[])
{
    int i, ret;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    /* Get the pool attached to the primary execution stream */
    ABT_xstream xstream;
    ret = ABT_self_get_xstream(&xstream);
    ATS_ERROR(ret, "ABT_self_get_xstream");

    ABT_pool pool;
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    /* Fork and join threads */
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * NUM_THREADS);
    for (i = 0; i < NUM_THREADS; i++) {
        void *arg = (i < NUM_THREADS / 2) ? NULL : ((void *)(intptr_t)1);
        ret = ABT_thread_create(pool, thread_func, arg, ABT_THREAD_ATTR_NULL,
                                &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }
    for (i = 0; i < NUM_THREADS; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }
    free(threads);

    /* Finalize */
    return ATS_finalize(0);
}
