/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_atomic.h"
#include "mercury_mem_pool.h"
#include "mercury_thread.h"
#include "mercury_thread_condition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

#define CHUNK_SIZE1  (4096)
#define CHUNK_COUNT1 (2)
#define BLOCK_COUNT1 (1)

#ifndef HG_TEST_NUM_THREADS_DEFAULT
#    define HG_TEST_NUM_THREADS_DEFAULT (8)
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct thread_args {
    struct hg_mem_pool *mem_pool;
    hg_thread_mutex_t mutex;
    hg_thread_cond_t cond;
    unsigned int n_threads;
    hg_atomic_int32_t n_mr;
    int mr;
};

/********************/
/* Local Prototypes */
/********************/

static int
hg_test_mem_pool_register(
    const void *buf, size_t len, unsigned long flags, void **handle, void *arg);

static int
hg_test_mem_pool_deregister(void *handle, void *arg);

static void
hg_test_mem_pool_alloc(struct hg_mem_pool *hg_mem_pool, int mr);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static int
hg_test_mem_pool_register(
    const void *buf, size_t len, unsigned long flags, void **handle, void *arg)
{
    hg_atomic_int32_t *n_mr = (hg_atomic_int32_t *) arg;
    int *mr_id;

    (void) buf;
    (void) len;
    (void) flags;

    mr_id = (int *) malloc(sizeof(int));
    if (mr_id == NULL)
        return HG_UTIL_FAIL;

    *mr_id = (int) hg_atomic_incr32(n_mr);
    *handle = (void *) mr_id;

    return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static int
hg_test_mem_pool_deregister(void *handle, void *arg)
{
    hg_atomic_int32_t *n_mr = (hg_atomic_int32_t *) arg;
    int *mr_id = (int *) handle;

    free(mr_id);
    hg_atomic_decr32(n_mr);

    return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_alloc_thread(void *arg)
{
    struct thread_args *thread_args = (struct thread_args *) arg;
    hg_thread_ret_t thread_ret = (hg_thread_ret_t) 0;

    /* Wait for all threads to have reached that point */
    hg_thread_mutex_lock(&thread_args->mutex);
    if (++thread_args->n_threads == HG_TEST_NUM_THREADS_DEFAULT)
        hg_thread_cond_broadcast(&thread_args->cond);
    hg_thread_mutex_unlock(&thread_args->mutex);

    hg_thread_mutex_lock(&thread_args->mutex);
    while (thread_args->n_threads != HG_TEST_NUM_THREADS_DEFAULT)
        hg_thread_cond_wait(&thread_args->cond, &thread_args->mutex);
    hg_thread_mutex_unlock(&thread_args->mutex);

    hg_test_mem_pool_alloc(thread_args->mem_pool, thread_args->mr);

    hg_thread_exit(thread_ret);
    return thread_ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_test_mem_pool_alloc(struct hg_mem_pool *hg_mem_pool, int mr)
{
    char data[CHUNK_SIZE1];
    void *mem_ptr, *mr_handle = NULL, *mr_handle_ptr;
    int i;

    memset(data, 1, CHUNK_SIZE1);
    mr_handle_ptr = (mr) ? &mr_handle : NULL;

    for (i = 0; i < (CHUNK_COUNT1 * 2); i++) {
        /* Allocate and free */
        mem_ptr = hg_mem_pool_alloc(hg_mem_pool, CHUNK_SIZE1, mr_handle_ptr);

        memcpy(mem_ptr, data, CHUNK_SIZE1);

        hg_mem_pool_free(hg_mem_pool, mem_ptr, mr_handle);
    }
}

/*---------------------------------------------------------------------------*/
int
main(void)
{
    struct thread_args thread_args;
    hg_thread_t threads[HG_TEST_NUM_THREADS_DEFAULT];
    int i;
    int ret = EXIT_SUCCESS;

    hg_thread_mutex_init(&thread_args.mutex);
    hg_thread_cond_init(&thread_args.cond);
    thread_args.n_threads = 0;
    hg_atomic_init32(&thread_args.n_mr, 0);

    /* Create memory pool without registration */
    thread_args.mem_pool = hg_mem_pool_create(
        CHUNK_SIZE1, CHUNK_COUNT1, BLOCK_COUNT1, NULL, 0, NULL, NULL);
    thread_args.mr = 0;

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_create(&threads[i], hg_test_alloc_thread, &thread_args);

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_join(threads[i]);

    hg_mem_pool_destroy(thread_args.mem_pool);

    /* Create memory pool with registration */
    thread_args.n_threads = 0;
    thread_args.mem_pool = hg_mem_pool_create(CHUNK_SIZE1, CHUNK_COUNT1,
        BLOCK_COUNT1, hg_test_mem_pool_register, 0, hg_test_mem_pool_deregister,
        &thread_args.n_mr);
    if (thread_args.mem_pool == NULL) {
        ret = EXIT_FAILURE;
        goto done;
    }
    thread_args.mr = 1;

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_create(&threads[i], hg_test_alloc_thread, &thread_args);

    for (i = 0; i < HG_TEST_NUM_THREADS_DEFAULT; i++)
        hg_thread_join(threads[i]);

    hg_mem_pool_destroy(thread_args.mem_pool);
    if (hg_atomic_get32(&thread_args.n_mr) != 0) {
        fprintf(stderr, "Error: memory still registered (%d)\n",
            (int) hg_atomic_get32(&thread_args.n_mr));
    }

done:
    hg_thread_mutex_destroy(&thread_args.mutex);
    hg_thread_cond_destroy(&thread_args.cond);

    return ret;
}
