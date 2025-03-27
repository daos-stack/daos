/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_UNIT_H
#define MERCURY_UNIT_H

#include "mercury_test.h"

#include "mercury_atomic.h"
#include "mercury_request.h"
#include "mercury_thread_pool.h"

#include "test_bulk.h"
#include "test_overflow.h"
#include "test_rpc.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct hg_unit_info {
    struct hg_test_info hg_test_info; /* HG test info */
    hg_class_t *hg_class;
    hg_context_t *context;
    hg_context_t **secondary_contexts;
    hg_request_class_t *request_class;
    hg_addr_t target_addr;
    hg_handle_t *handles;
    hg_thread_pool_t *thread_pool;
    size_t handle_max;
    size_t buf_size_max;
    hg_request_t *request;
};

struct hg_test_context_info {
    hg_atomic_int32_t finalizing;
};

struct hg_test_handle_info {
    struct hg_thread_work work;
    void *data;
};

/*****************/
/* Public Macros */
/*****************/

/* Test path */
#define HG_TEST_RPC_PATH (HG_TEST_TEMP_DIRECTORY "/test.txt")

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

hg_return_t
hg_unit_init(int argc, char *argv[], bool listen, struct hg_unit_info *info);

void
hg_unit_cleanup(struct hg_unit_info *info);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_UNIT_H */
