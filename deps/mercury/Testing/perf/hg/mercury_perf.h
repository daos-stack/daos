/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_PERF_H
#define MERCURY_PERF_H

#include "mercury_test.h"

#include "mercury_bulk.h"
#include "mercury_param.h"
#include "mercury_poll.h"
#include "mercury_time.h"

#include <stdlib.h>
#include <string.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

enum hg_perf_rpc_id {
    HG_PERF_RATE_INIT = 1,
    HG_PERF_RATE,
    HG_PERF_FIRST,
    HG_PERF_BW_INIT,
    HG_PERF_BW_READ,
    HG_PERF_BW_WRITE,
    HG_PERF_DONE
};

struct hg_perf_info {
    struct hg_test_info hg_test_info; /* HG test info */
    struct hg_perf_class_info *class_info;
    size_t class_max;
};

struct hg_perf_class_info {
    hg_class_t *hg_class;    /* HG class */
    hg_context_t *context;   /* HG context */
    hg_poll_set_t *poll_set; /* Poll set */
    hg_addr_t *target_addrs; /* Target addresses */
    hg_handle_t *handles;    /* Handles */
    void *rpc_buf;
    void *rpc_verify_buf;
    void **bulk_bufs;
    size_t bulk_count;
    size_t target_addr_max;
    size_t handle_max;
    size_t handle_target;
    size_t buf_size_min;
    size_t buf_size_max;
    hg_bulk_t *local_bulk_handles;
    hg_bulk_t *remote_bulk_handles;
    int wait_fd; /* Wait fd */
    int class_id;
    bool done;
    bool verify;
    bool bidir;
};

struct hg_perf_request {
    int32_t expected_count;      /* Expected count */
    int32_t complete_count;      /* Completed count */
    hg_atomic_int32_t completed; /* Request */
};

struct hg_perf_bulk_init_info {
    hg_bulk_t bulk;
    uint32_t bulk_op;
    uint32_t handle_id;
    uint32_t handle_max;
    uint32_t bulk_count;
    uint32_t size_max;
    uint32_t comm_size;
    uint32_t target_rank;
    uint32_t target_addr_max;
};

struct hg_perf_bulk_info {
    hg_bulk_t bulk;     /* Bulk handle */
    uint32_t handle_id; /* Source handle ID */
    uint32_t size;      /* Transfer size*/
};

/*****************/
/* Public Macros */
/*****************/

#define HG_PERF_LAT_SKIP_SMALL 100
#define HG_PERF_LAT_SKIP_LARGE 10
#define HG_PERF_LARGE_SIZE     8192

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

hg_return_t
hg_perf_request_wait(struct hg_perf_class_info *info,
    struct hg_perf_request *request, unsigned int timeout_ms,
    unsigned int *completed_p);

hg_return_t
hg_perf_request_complete(const struct hg_cb_info *hg_cb_info);

hg_return_t
hg_perf_init(int argc, char *argv[], bool listen, struct hg_perf_info *info);

void
hg_perf_cleanup(struct hg_perf_info *info);

hg_return_t
hg_perf_set_handles(const struct hg_test_info *hg_test_info,
    struct hg_perf_class_info *info, enum hg_perf_rpc_id rpc_id);

hg_return_t
hg_perf_rpc_buf_init(
    const struct hg_test_info *hg_test_info, struct hg_perf_class_info *info);

hg_return_t
hg_perf_bulk_buf_init(const struct hg_test_info *hg_test_info,
    struct hg_perf_class_info *info, hg_bulk_op_t bulk_op);

hg_return_t
hg_perf_verify_data(const void *buf, size_t buf_size);

void
hg_perf_print_header_lat(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, const char *benchmark);

void
hg_perf_print_lat(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, size_t buf_size, hg_time_t t);

void
hg_perf_print_header_time(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, const char *benchmark);

void
hg_perf_print_time(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, size_t buf_size, hg_time_t t);

void
hg_perf_print_header_bw(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, const char *benchmark);

void
hg_perf_print_bw(const struct hg_test_info *hg_test_info,
    const struct hg_perf_class_info *info, size_t buf_size, hg_time_t t,
    hg_time_t t_reg, hg_time_t t_dereg);

hg_return_t
hg_perf_send_done(struct hg_perf_class_info *info);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_PERF_H */
