/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_thread.h"
#include "mercury_thread_mutex.h"

#include <stdio.h>
#include <stdlib.h>

static hg_thread_mutex_t thread_mutex;
static int thread_value = 0;

static HG_THREAD_RETURN_TYPE
thread_cb_mutex(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;

    (void) arg;

    if (hg_thread_mutex_try_lock(&thread_mutex) != HG_UTIL_SUCCESS)
        hg_thread_mutex_lock(&thread_mutex);

    thread_value++;

    hg_thread_mutex_unlock(&thread_mutex);

    hg_thread_exit(thread_ret);
    return thread_ret;
}

int
main(int argc, char *argv[])
{
    hg_thread_t thread1, thread2;
    int ret = EXIT_SUCCESS;

    (void) argc;
    (void) argv;

    hg_thread_init(&thread1);
    hg_thread_init(&thread2);
    hg_thread_mutex_init(&thread_mutex);

    hg_thread_create(&thread1, thread_cb_mutex, NULL);
    hg_thread_create(&thread2, thread_cb_mutex, NULL);
    hg_thread_join(thread1);
    hg_thread_join(thread2);

    if (thread_value != 2) {
        fprintf(stderr, "Error: value is %d\n", thread_value);
        ret = EXIT_FAILURE;
    }

    hg_thread_mutex_destroy(&thread_mutex);
    return ret;
}
