/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

#define NUM_THREADS 4

ABT_bool g_is_check_error, g_support_external_thread;
int g_count = 0;

void thread_func(void *arg)
{
    int i, my_count, ret;

    for (i = 0; i < 100; i++) {
        my_count = g_count++;
        ret = ABT_thread_yield();
        ATS_ERROR(ret, "ABT_thread_yield");
        /* After yield, all the other threads are scheduled, so g_count must be
         * my_count + NUM_THREADS.
         * Note that this does not hold for the last iteration because of the
         * join optimization. */
        if (i < 99)
            assert(g_count == my_count + NUM_THREADS);

        my_count = g_count++;
        ret = ABT_self_yield();
        ATS_ERROR(ret, "ABT_self_yield");
        /* For the same reason, g_count must be my_count + NUM_THREADS. */
        if (i < 99)
            assert(g_count == my_count + NUM_THREADS);
    }
}

void task_func(void *arg)
{
    int ret, my_count;

    my_count = ++g_count;
    /* Task cannot yield. */
#ifndef ABT_ENABLE_VER_20_API
    /* ABT_thread_yield() does nothing. */
    ret = ABT_thread_yield();
    ATS_ERROR(ret, "ABT_thread_yield");
#else
    /* ABT_thread_yield() returns an error. */
    if (g_is_check_error) {
        ret = ABT_thread_yield();
        assert(ret != ABT_SUCCESS);
    }
#endif
    if (g_is_check_error) {
        ret = ABT_self_yield();
        assert(ret != ABT_SUCCESS);
    }
    assert(my_count == g_count);
}

void *pthread_func(void *arg)
{
    int ret;
#ifndef ABT_ENABLE_VER_20_API
    /* ABT_thread_yield() does nothing. */
    ret = ABT_thread_yield();
    ATS_ERROR(ret, "ABT_thread_yield");
#else
    /* ABT_thread_yield() returns an error. */
    if (g_is_check_error) {
        ret = ABT_thread_yield();
        assert(ret != ABT_SUCCESS);
    }
#endif
    if (g_is_check_error) {
        ret = ABT_self_yield();
        assert(ret != ABT_SUCCESS);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int i, ret;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    /* Get the configuration. */
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR,
                                (void *)&g_is_check_error);
    ATS_ERROR(ret, "ABT_info_query_config");
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                (void *)&g_support_external_thread);
    ATS_ERROR(ret, "ABT_info_query_config");

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
        ret = ABT_thread_create(pool, thread_func, NULL, ABT_THREAD_ATTR_NULL,
                                &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }
    for (i = 0; i < NUM_THREADS; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }
    free(threads);

    /* Fork and join tasks */
    ABT_task *tasks = (ABT_task *)malloc(sizeof(ABT_task) * NUM_THREADS);
    for (i = 0; i < NUM_THREADS; i++) {
        ret = ABT_task_create(pool, task_func, NULL, &tasks[i]);
        ATS_ERROR(ret, "ABT_task_create");
    }
    for (i = 0; i < NUM_THREADS; i++) {
        ret = ABT_task_free(&tasks[i]);
        ATS_ERROR(ret, "ABT_tasks_free");
    }
    free(tasks);

    /* Fork and join a pthread */
    if (g_support_external_thread) {
        pthread_t pthread;
        ret = pthread_create(&pthread, NULL, pthread_func, NULL);
        assert(ret == 0);
        ret = pthread_join(pthread, NULL);
        assert(ret == 0);
    }

    /* Finalize */
    return ATS_finalize(0);
}
