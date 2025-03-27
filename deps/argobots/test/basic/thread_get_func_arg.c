/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include "abt.h"
#include "abttest.h"

#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_THREADS 8
#define DEFAULT_NUM_TASKS 4

void check_func_arg(void (*thread_f1)(void *), void *arg1)
{
    int ret;
    void *arg2, *arg3;
    void (*thread_f2)(void *), (*thread_f3)(void *);

    ret = ABT_self_get_thread_func(&thread_f2);
    ATS_ERROR(ret, "ABT_self_get_thread_func");
    assert(thread_f1 == thread_f2);
    ret = ABT_self_get_arg(&arg2);
    ATS_ERROR(ret, "ABT_self_get_arg");
    assert(arg1 == arg2);

    ABT_thread self_thread;
    ret = ABT_self_get_thread(&self_thread);
    ATS_ERROR(ret, "ABT_self_get_thread");
    ret = ABT_thread_get_thread_func(self_thread, &thread_f3);
    ATS_ERROR(ret, "ABT_thread_get_thread_func");
    assert(thread_f1 == thread_f3);
    ret = ABT_thread_get_arg(self_thread, &arg3);
    ATS_ERROR(ret, "ABT_thread_get_arg");
    assert(arg1 == arg3);
}

void thread_func(void *arg)
{
    check_func_arg(thread_func, arg);
    int ret = ABT_self_yield();
    ATS_ERROR(ret, "ABT_self_yield");
    check_func_arg(thread_func, arg);
}

void task_func(void *arg)
{
    check_func_arg(task_func, arg);
}

int main(int argc, char *argv[])
{
    int i;
    int ret;
    int num_xstreams, num_threads, num_tasks;

    /* Initialize */
    ATS_read_args(argc, argv);
    if (argc < 2) {
        num_xstreams = DEFAULT_NUM_XSTREAMS;
        num_threads = DEFAULT_NUM_THREADS;
        num_tasks = DEFAULT_NUM_TASKS;
    } else {
        num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
        num_tasks = ATS_get_arg_val(ATS_ARG_N_TASK);
    }

    ATS_init(argc, argv, num_xstreams);
    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);
    ABT_thread *tasks = (ABT_thread *)malloc(sizeof(ABT_thread) * num_tasks);

    /* Create Execution Streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    /* Create ULTs for each ES */
    for (i = 0; i < num_threads; i++) {
        void *arg = (void *)(uintptr_t)i;
        ret = ABT_thread_create_on_xstream(xstreams[i % num_xstreams],
                                           thread_func, arg,
                                           ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create_on_xstream");
    }

    /* Create tasklets for each ES */
    for (i = 0; i < num_tasks; i++) {
        void *arg = (void *)(uintptr_t)i;
        ret = ABT_task_create_on_xstream(xstreams[i % num_xstreams], task_func,
                                         arg, &tasks[i]);
        ATS_ERROR(ret, "ABT_task_create_on_xstream");
    }

    /* Join and free ULTs */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_join");
    }

    /* Join and free tasklets */
    for (i = 0; i < num_tasks; i++) {
        ret = ABT_thread_free(&tasks[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }

    /* Join and free execution streams */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    free(xstreams);
    free(threads);
    free(tasks);

    /* Finalize */
    ret = ATS_finalize(0);

    return ret;
}
