/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef na_test_common_H
#define na_test_common_H

#include "na_test.h"

#include "mercury_param.h"
#include "mercury_poll.h"
#include "mercury_request.h" /* For convenience */
#include "mercury_time.h"

#include <stdlib.h>
#include <string.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct na_test_common_info {
    struct na_test_info na_test_info; /* NA test info */
    struct na_test_common_class_info *class_info;
};

struct na_test_common_class_info {
    na_class_t *na_class;              /* NA class */
    na_context_t *context;             /* NA context */
    hg_poll_set_t *poll_set;           /* Poll set */
    hg_request_class_t *request_class; /* Request class */
    void *msg_unexp_buf;               /* Expected msg buffer */
    void *msg_exp_buf;                 /* Unexpected msg buffer */
    void *msg_unexp_data;              /* Plugin data */
    void *msg_exp_data;                /* Plugin data */
    na_op_id_t *msg_unexp_op_id;       /* Msg unexpected op ID */
    na_op_id_t *msg_exp_op_id;         /* Msg expected op ID */
    size_t msg_unexp_header_size;      /* Header size */
    size_t msg_exp_header_size;        /* Header size */
    size_t msg_unexp_size_max;         /* Max buffer size */
    size_t msg_exp_size_max;           /* Max buffer size */
    hg_request_t *request;             /* Request */
    int poll_fd;                       /* Poll fd */
};

/*****************/
/* Public Macros */
/*****************/

#define NA_TEST_COMMON_TAG_DONE     111
#define NA_TEST_COMMON_TAG_CONTINUE 112

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

int
na_test_common_request_progress(unsigned int timeout, void *arg);

int
na_test_common_request_trigger(
    unsigned int timeout, unsigned int *flag, void *arg);

void
na_test_common_request_complete(const struct na_cb_info *na_cb_info);

na_return_t
na_test_common_init(
    int argc, char *argv[], bool listen, struct na_test_common_info *info);

void
na_test_common_cleanup(struct na_test_common_info *info);

void
na_test_common_init_data(void *buf, size_t buf_size, size_t header_size);

na_return_t
na_test_common_verify_data(
    const void *buf, size_t buf_size, size_t header_size);

na_return_t
na_test_common_send_finalize(
    struct na_test_common_class_info *info, na_addr_t *target_addr);

#ifdef __cplusplus
}
#endif

#endif /* na_test_common_H */
