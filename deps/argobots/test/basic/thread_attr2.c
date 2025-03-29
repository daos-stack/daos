/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include "abt.h"
#include "abttest.h"

#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_THREADS 4

#define DUMMY_SIZE ((int)(1024 / sizeof(double)))
void set_random_dummy(volatile double *dummy)
{
    int i;
    double base_value = ABT_get_wtime();
    for (i = 0; i < DUMMY_SIZE; i++)
        dummy[i] = base_value + i;
}

void update_random_dummy(volatile double *dummy)
{
    int i;
    double base_value = ABT_get_wtime();
    for (i = 0; i < DUMMY_SIZE; i++) {
        if (dummy[i] == base_value + i)
            dummy[i] *= 1.5;
    }
}

void dummy_rec(const volatile double *top_dummy, volatile double *prev_dummy,
               size_t stacksize)
{
    int i;
    volatile double dummy[DUMMY_SIZE];
    set_random_dummy(dummy);

    uintptr_t dummy_ptr = (uintptr_t)dummy;
    uintptr_t top_dummy_ptr = (uintptr_t)top_dummy;
    if (top_dummy_ptr > dummy_ptr) {
        if ((size_t)(top_dummy_ptr - dummy_ptr) > stacksize / 2)
            /* Consumed enough stack. */
            return;
    } else {
        if ((size_t)(dummy_ptr - top_dummy_ptr) > stacksize / 2)
            /* Consumed enough stack. */
            return;
    }
    /* Recursive call. */
    dummy_rec(top_dummy, dummy, stacksize);
    /* We need to avoid tail recursion elimination, so let's do something. */
    update_random_dummy(dummy);
    for (i = 0; i < DUMMY_SIZE; i++)
        prev_dummy[i] += dummy[i];
}

void thread_func(void *arg)
{
    size_t stacksize = *((size_t *)arg), stacksize2;
    ABT_thread thread;
    ABT_thread_attr attr;
    int ret, i;

    ret = ABT_thread_self(&thread);
    ATS_ERROR(ret, "ABT_thread_self");
    ret = ABT_thread_get_attr(thread, &attr);
    ATS_ERROR(ret, "ABT_thread_get_attr");

    ret = ABT_thread_attr_get_stacksize(attr, &stacksize2);
    ATS_ERROR(ret, "ABT_thread_attr_get_stacksize");
    /* This must be the same. */
    assert(stacksize == stacksize2);

    /*
     * Checking a real stack size is tricky.  Let's consume stack by recursion.
     * - Each dummy_rec() consumes at least "DUMMY_SIZE * sizeof(double)" bytes.
     * - Call dummy_rec() recursively until the total stack consumption gets
     *   more than half of stacksize.  We need a margin for safety since we
     *   cannot control the exact size of each function stack.
     * Note that we use neither alloca() nor variable-length array since they
     * are not portable.
     */
    volatile double dummy[DUMMY_SIZE];
    set_random_dummy(dummy);
    dummy_rec(dummy, dummy, stacksize);

    update_random_dummy(dummy);
    /* Use values of dummy to avoid possible compiler optimization */
    for (i = 0; i < DUMMY_SIZE; i++) {
        if (0.00001 < dummy[i] && dummy[i] < 0.00002)
            printf("%d %f", i, dummy[i]);
    }

    ret = ABT_thread_attr_free(&attr);
    ATS_ERROR(ret, "ABT_thread_attr_free");
}

int main(int argc, char *argv[])
{
    int ret, i;

    ABT_thread_attr attr;
    ABT_xstream xstream;
    ABT_pool pool;
    ABT_thread thread;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    /* Get a main pool. */
    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_xstream_self");
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    /* Get the default stack size. */
    size_t default_stacksize;
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE,
                                &default_stacksize);
    ATS_ERROR(ret, "ABT_info_query_config");

    /* Loop over different stack sizes. */
    size_t stacksizes[] = { default_stacksize, 1024 * 64, 1024 * 1024 };
    int num_stacksizes = sizeof(stacksizes) / sizeof(stacksizes[0]);
    for (i = 0; i < num_stacksizes; i++) {
        size_t stacksize = stacksizes[i];

        ret = ABT_thread_attr_create(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_create");

        /* Case 1: set it via ABT_thread_attr_set_stacksize() */
        ret = ABT_thread_attr_set_stacksize(attr, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stacksize");
        ret = ABT_thread_create(pool, thread_func, (void *)&stacksize, attr,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");

        /* Case 2: set it via ABT_thread_attr_set_stack() (stack: NULL) */
        ret = ABT_thread_attr_set_stack(attr, NULL, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack");
        ret = ABT_thread_create(pool, thread_func, (void *)&stacksize, attr,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");

        /* Case 3: set a different value once. */
        ret =
            ABT_thread_attr_set_stacksize(attr,
                                          stacksizes[(i + 1) % num_stacksizes]);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack_size");
        ret = ABT_thread_attr_set_stacksize(attr, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack_size");
        ret = ABT_thread_create(pool, thread_func, (void *)&stacksize, attr,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");

        /* Case 4: use ABT_thread_attr_set_stack() with stack. */
        void *p_stack1 = (void *)malloc(stacksize);
        ret = ABT_thread_attr_set_stack(attr, p_stack1, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack");
        ret = ABT_thread_create(pool, thread_func, (void *)&stacksize, attr,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        free(p_stack1);

        /* Case 5: set a different value once. */
        void *p_stack2 = (void *)malloc(stacksize);
        ret = ABT_thread_attr_set_stack(attr, p_stack2,
                                        stacksizes[(i + 1) % num_stacksizes]);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack");
        ret = ABT_thread_attr_set_stacksize(attr, stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_set_stack_size");
        ret = ABT_thread_create(pool, thread_func, (void *)&stacksize, attr,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        free(p_stack2);

        ret = ABT_thread_attr_free(&attr);
        ATS_ERROR(ret, "ABT_thread_attr_free");
    }

    /* Case 6: default attribute. */
    ret = ABT_thread_create(pool, thread_func, (void *)&default_stacksize,
                            ABT_THREAD_ATTR_NULL, &thread);
    ATS_ERROR(ret, "ABT_thread_create");
    ret = ABT_thread_free(&thread);
    ATS_ERROR(ret, "ABT_thread_free");

    /* Finalize */
    ret = ATS_finalize(0);

    return ret;
}
