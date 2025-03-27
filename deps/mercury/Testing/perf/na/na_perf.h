/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_PERF_H
#define NA_PERF_H

#include "na_test.h"

#include "mercury_param.h"
#include "mercury_poll.h"
#include "mercury_time.h"

#include <stdlib.h>
#include <string.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct na_perf_info {
    struct na_test_info na_test_info; /* NA test info */
    na_class_t *na_class;             /* NA class */
    na_context_t *context;            /* NA context */
    hg_poll_set_t *poll_set;          /* Poll set */
    na_addr_t *target_addr;           /* Target address */
    void *msg_unexp_buf;              /* Expected msg buffer */
    void *msg_exp_buf;                /* Unexpected msg buffer */
    void *msg_unexp_data;             /* Plugin data */
    void *msg_exp_data;               /* Plugin data */
    na_op_id_t *msg_unexp_op_id;      /* Msg unexpected op ID */
    na_op_id_t *msg_exp_op_id;        /* Msg expected op ID */
    void *rma_buf;                    /* RMA buffer */
    void *verify_buf;                 /* Verify buffer */
    na_mem_handle_t *local_handle;    /* Local handle */
    na_mem_handle_t *remote_handle;   /* Remote handle */
    na_mem_handle_t *verify_handle;   /* Local handle to verify buffer */
    na_op_id_t **rma_op_ids;          /* RMA op IDs */
    size_t msg_unexp_header_size;     /* Header size */
    size_t msg_exp_header_size;       /* Header size */
    size_t msg_unexp_size_max;        /* Max buffer size */
    size_t msg_exp_size_max;          /* Max buffer size */
    size_t rma_size_min;              /* Min buffer size */
    size_t rma_size_max;              /* Max buffer size */
    size_t rma_count;                 /* Buffer count */
    int poll_fd;                      /* Poll fd */
};

struct na_perf_request_info {
    int32_t expected_count;      /* Expected count */
    int32_t complete_count;      /* Completed count */
    hg_atomic_int32_t completed; /* Request */
};

/*****************/
/* Public Macros */
/*****************/

#define NA_PERF_TAG_LAT_INIT 0
#define NA_PERF_TAG_LAT      1
#define NA_PERF_TAG_PUT      10
#define NA_PERF_TAG_GET      20
#define NA_PERF_TAG_DONE     111

#define NA_PERF_LAT_SKIP_SMALL 100
#define NA_PERF_LAT_SKIP_LARGE 10
#define NA_PERF_BW_SKIP_SMALL  10
#define NA_PERF_BW_SKIP_LARGE  2
#define NA_PERF_LARGE_SIZE     8192

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

na_return_t
na_perf_request_wait(struct na_perf_info *info,
    struct na_perf_request_info *request_info, unsigned int timeout_ms,
    unsigned int *completed_p);

void
na_perf_request_complete(const struct na_cb_info *na_cb_info);

na_return_t
na_perf_init(int argc, char *argv[], bool listen, struct na_perf_info *info);

void
na_perf_cleanup(struct na_perf_info *info);

void
na_perf_print_header_lat(
    const struct na_perf_info *info, const char *benchmark, size_t min_size);

void
na_perf_print_lat(
    const struct na_perf_info *info, size_t buf_size, hg_time_t t);

void
na_perf_print_header_bw(const struct na_perf_info *info, const char *benchmark);

void
na_perf_print_bw(const struct na_perf_info *info, size_t buf_size, hg_time_t t,
    hg_time_t t_reg, hg_time_t t_dereg);

void
na_perf_init_data(void *buf, size_t buf_size, size_t header_size);

na_return_t
na_perf_verify_data(const void *buf, size_t buf_size, size_t header_size);

na_return_t
na_perf_mem_handle_send(
    struct na_perf_info *info, na_addr_t *src_addr, na_tag_t tag);

na_return_t
na_perf_mem_handle_recv(struct na_perf_info *info, na_tag_t tag);

na_return_t
na_perf_send_finalize(struct na_perf_info *info);

#ifdef __cplusplus
}
#endif

#endif /* NA_PERF_H */
