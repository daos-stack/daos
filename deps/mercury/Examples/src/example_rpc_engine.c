/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_rpc_engine.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

/* example_rpc_engine: API of generic utilities and progress engine hooks that
 * are reused across many RPC functions.  init and finalize() manage a
 * dedicated thread that will drive all HG progress
 */

static hg_context_t *hg_context = NULL;
static hg_class_t *hg_class = NULL;

static pthread_t hg_progress_tid;
static int hg_progress_shutdown_flag = 0;
static void *
hg_progress_fn(void *foo);

void
hg_engine_init(hg_bool_t listen, const char *local_addr)
{
    int ret;

    HG_Set_log_level("warning");

    /* boilerplate HG initialization steps */
    hg_class = HG_Init(local_addr, listen);
    assert(hg_class);

    hg_context = HG_Context_create(hg_class);
    assert(hg_context);

    /* start up thread to drive progress */
    ret = pthread_create(&hg_progress_tid, NULL, hg_progress_fn, NULL);
    assert(ret == 0);
    (void) ret;

    return;
}

void
hg_engine_finalize(void)
{
    int ret;

    /* tell progress thread to wrap things up */
    hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ret = pthread_join(hg_progress_tid, NULL);
    assert(ret == 0);
    (void) ret;

    return;
}

/* dedicated thread function to drive Mercury progress */
static void *
hg_progress_fn(void *foo)
{
    hg_return_t ret;
    unsigned int actual_count;
    (void) foo;

    while (!hg_progress_shutdown_flag) {
        do {
            ret = HG_Trigger(hg_context, 0, 1, &actual_count);
        } while (
            (ret == HG_SUCCESS) && actual_count && !hg_progress_shutdown_flag);

        if (!hg_progress_shutdown_flag)
            HG_Progress(hg_context, 100);
    }

    return (NULL);
}

hg_class_t *
hg_engine_get_class(void)
{
    return (hg_class);
}

void
hg_engine_print_self_addr(void)
{
    hg_return_t ret;
    hg_addr_t addr;
    char buf[64] = {'\0'};
    hg_size_t buf_size = 64;

    ret = HG_Addr_self(hg_class, &addr);
    assert(ret == HG_SUCCESS);
    (void) ret;

    ret = HG_Addr_to_string(hg_class, buf, &buf_size, addr);
    assert(ret == HG_SUCCESS);
    (void) ret;

    printf("svr address string: \"%s\"\n", buf);

    ret = HG_Addr_free(hg_class, addr);
    assert(ret == HG_SUCCESS);
    (void) ret;

    return;
}

void
hg_engine_addr_lookup(const char *name, hg_addr_t *addr)
{
    hg_return_t ret;

    ret = HG_Addr_lookup2(hg_class, name, addr);
    assert(ret == HG_SUCCESS);
    (void) ret;

    return;
}

void
hg_engine_addr_free(hg_addr_t addr)
{
    hg_return_t ret;

    ret = HG_Addr_free(hg_class, addr);
    assert(ret == HG_SUCCESS);
    (void) ret;

    return;
}

void
hg_engine_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle)
{
    hg_return_t ret;

    ret = HG_Create(hg_context, addr, id, handle);
    assert(ret == HG_SUCCESS);
    (void) ret;

    return;
}
