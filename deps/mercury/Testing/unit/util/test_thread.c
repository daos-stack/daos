/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_thread.h"
#include "mercury_time.h"

#include <stdio.h>
#include <stdlib.h>

static HG_THREAD_RETURN_TYPE
thread_cb_incr(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    int *incr = (int *) arg;

    *incr = 1;

    hg_thread_exit(thread_ret);
    return thread_ret;
}

#ifndef __SANITIZE_ADDRESS__
static HG_THREAD_RETURN_TYPE
thread_cb_sleep(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    hg_time_t sleep_time = {5, 0};

    (void) arg;
    hg_time_sleep(sleep_time);
    fprintf(stderr, "Error: did not cancel\n");

    hg_thread_exit(thread_ret);
    return thread_ret;
}
#endif

static HG_THREAD_RETURN_TYPE
thread_cb_key(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    hg_thread_key_t *thread_key = (hg_thread_key_t *) arg;
    int *value_ptr;
    int value = 1;

    hg_thread_setspecific(*thread_key, &value);

    value_ptr = (int *) hg_thread_getspecific(*thread_key);
    if (!value_ptr) {
        fprintf(stderr, "Error: No value associated to key\n");
        goto done;
    }
    if (*value_ptr != value) {
        fprintf(stderr, "Error: Value is %d\n", *value_ptr);
    }

done:
    hg_thread_exit(thread_ret);
    return thread_ret;
}

static HG_THREAD_RETURN_TYPE
thread_cb_equal(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    hg_thread_t *t1_ptr = (hg_thread_t *) arg;
    hg_thread_t t2 = hg_thread_self();

    if (hg_thread_equal(*t1_ptr, t2) == 0)
        fprintf(stderr, "Error: t1 is not equal to t2\n");

    hg_thread_exit(thread_ret);
    return thread_ret;
}

int
main(int argc, char *argv[])
{
    hg_thread_t thread;
    hg_thread_key_t thread_key;
    int incr = 0;
    int ret = EXIT_SUCCESS;

    (void) argc;
    (void) argv;

    hg_thread_init(&thread);
    hg_thread_create(&thread, thread_cb_incr, &incr);
    hg_thread_join(thread);

    if (!incr) {
        fprintf(stderr, "Error: Incr is %d\n", incr);
        ret = EXIT_FAILURE;
        goto done;
    }

/* Disable when running with address sanitizer because of CI issue with gcc 11:
 * AsanCheckFailed ../../../../src/libsanitizer/asan/asan_rtl.cpp:74 */
#ifndef __SANITIZE_ADDRESS__
    hg_thread_create(&thread, thread_cb_sleep, NULL);
    hg_thread_cancel(thread);
    hg_thread_join(thread);
#endif

    hg_thread_key_create(&thread_key);
    hg_thread_create(&thread, thread_cb_key, &thread_key);
    hg_thread_join(thread);
    hg_thread_key_delete(thread_key);

    hg_thread_create(&thread, thread_cb_equal, &thread);
    hg_thread_join(thread);

done:
    return ret;
}
