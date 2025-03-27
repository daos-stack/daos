/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "abt.h"
#include "abttest.h"

#define DEFAULT_NUM_THREADS 10

volatile int g_go = 0;

typedef struct thread_arg {
    int id;
    int level;
    void *stack;
} thread_arg_t;

void user_thread_func_lv4(int level)
{
    while (g_go == 0) {
        int ret = ABT_thread_yield();
        ATS_ERROR(ret, "ABT_thread_yield");
    }
}

void user_thread_func_lv3(int level)
{
    if (level == 0) {
        while (g_go == 0) {
            int ret = ABT_thread_yield();
            ATS_ERROR(ret, "ABT_thread_yield");
        }
    } else {
        user_thread_func_lv4(level - 1);
    }
    user_thread_func_lv4(level - 1);
}

void user_thread_func_lv2(int level)
{
    if (level == 0) {
        while (g_go == 0) {
            int ret = ABT_thread_yield();
            ATS_ERROR(ret, "ABT_thread_yield");
        }
    } else {
        user_thread_func_lv3(level - 1);
    }
    user_thread_func_lv3(level - 1);
}

void user_thread_func(void *arg)
{
    thread_arg_t *t_arg = (thread_arg_t *)arg;
    int level = t_arg->level;
    if (level == 0) {
        while (g_go == 0) {
            int ret = ABT_thread_yield();
            ATS_ERROR(ret, "ABT_thread_yield");
        }
    } else {
        user_thread_func_lv2(level - 1);
    }
    user_thread_func_lv2(level - 1);
}

static inline void create_thread(ABT_pool pool, ABT_thread *threads,
                                 thread_arg_t *args, int i)
{
    int ret;
    args[i].id = i;
    args[i].level = i % 4;
    args[i].stack = NULL;
    if (i % 3 == 0) {
        ret = ABT_thread_create(pool, user_thread_func, (void *)&args[i],
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    } else if (i % 3 == 1) {
        ABT_thread_attr attr;
        ret = ABT_thread_attr_create(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_create");
        ret = ABT_thread_attr_set_stacksize(attr, 32768);
        ATS_ERROR(ret, "ABT_thread_attr_set_stacksize");
        ret = ABT_thread_create(pool, user_thread_func, (void *)&args[i], attr,
                                &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_attr_free(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_free");
    } else {
        const size_t stacksize = 32768;
        args[i].stack = malloc(stacksize);
        ABT_thread_attr attr;
        ret = ABT_thread_attr_create(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_create");
        ret = ABT_thread_attr_set_stack(attr, args[i].stack, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack");
        ret = ABT_thread_create(pool, user_thread_func, (void *)&args[i], attr,
                                &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_attr_free(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_free");
    }
}

int main(int argc, char *argv[])
{
    int i;
    int ret;
    int num_threads = DEFAULT_NUM_THREADS;

    /* Initialize */
    ATS_read_args(argc, argv);
    if (argc >= 2) {
        num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
    }
    ATS_init(argc, argv, 1);

    ATS_printf(2, "# of ESs : 1\n");
    ATS_printf(1, "# of ULTs: %d\n", num_threads);

    ABT_xstream xstream;
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);
    thread_arg_t *args =
        (thread_arg_t *)malloc(sizeof(thread_arg_t) * num_threads);

    /* Create execution streams */
    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_xstream_self");

    /* Get the pools attached to an execution stream */
    ABT_pool pool;
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    /* Create the first (num_threads - 4) threads, which will be executed. */
    for (i = 0; i < num_threads - 4; i++)
        create_thread(pool, threads, args, i);

    /* Execute some of the threads. */
    ret = ABT_thread_yield();
    ATS_ERROR(ret, "ABT_thread_yield");

    /* Create the last four threads, which are not executed. */
    for (i = num_threads - 4; i < num_threads; i++)
        create_thread(pool, threads, args, i);

    /* Print unwinded stacks. */
    for (i = 0; i < num_threads; i++) {
        printf("threads[%d]:\n", i);
        ret = ABT_info_print_thread_stack(stdout, threads[i]);
        ATS_ERROR(ret, "ABT_info_print_thread_stack");
        printf("\n");
    }
    g_go = 1;

    /* Join and free ULTs */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    for (i = 0; i < num_threads; i++) {
        if (args[i].stack)
            free(args[i].stack);
    }
    free(threads);
    free(args);

    return ret;
}
