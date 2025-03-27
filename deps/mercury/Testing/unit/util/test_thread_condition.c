/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_thread.h"
#include "mercury_thread_condition.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef HG_TEST_NUM_THREADS_DEFAULT
#    define HG_TEST_NUM_THREADS_DEFAULT (8)
#endif

static hg_thread_cond_t thread_cond;
static hg_thread_mutex_t thread_mutex;
static int working = 0;

static HG_THREAD_RETURN_TYPE
thread_cb_cond(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    (void) arg;

    hg_thread_mutex_lock(&thread_mutex);

    while (working)
        hg_thread_cond_wait(&thread_cond, &thread_mutex);

    working = 1;
    hg_thread_mutex_unlock(&thread_mutex);

    hg_thread_mutex_lock(&thread_mutex);

    working = 0;
    hg_thread_cond_signal(&thread_cond);

    hg_thread_mutex_unlock(&thread_mutex);

    hg_thread_exit(thread_ret);
    return thread_ret;
}

static HG_THREAD_RETURN_TYPE
thread_cb_cond_all(void *arg)
{
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;
    (void) arg;

    hg_thread_mutex_lock(&thread_mutex);

    while (working)
        hg_thread_cond_timedwait(&thread_cond, &thread_mutex, 1000);

    hg_thread_mutex_unlock(&thread_mutex);

    hg_thread_exit(thread_ret);
    return thread_ret;
}

int
main(int argc, char *argv[])
{
    hg_thread_t thread[HG_TEST_NUM_THREADS_DEFAULT];
    int ret = EXIT_SUCCESS;
    int i;

    (void) argc;
    (void) argv;

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_init(&thread[i]);
    hg_thread_mutex_init(&thread_mutex);
    hg_thread_cond_init(&thread_cond);

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_create(&thread[i], thread_cb_cond, NULL);
    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_join(thread[i]);

    working = 1;

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_create(&thread[i], thread_cb_cond_all, NULL);

    hg_thread_mutex_lock(&thread_mutex);
    working = 0;
    hg_thread_cond_broadcast(&thread_cond);
    hg_thread_mutex_unlock(&thread_mutex);

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_join(thread[i]);

    return ret;
}
