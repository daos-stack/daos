/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_perf.h"

/****************/
/* Local Macros */
/****************/
#define BENCHMARK_NAME "Message latency"

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static na_return_t
na_perf_run(struct na_perf_info *info, size_t buf_size, size_t skip);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static na_return_t
na_perf_send_init(struct na_perf_info *info)
{
    struct na_perf_request_info request_info = {
        .completed = HG_ATOMIC_VAR_INIT(0),
        .complete_count = 0,
        .expected_count = (int32_t) 1};
    na_return_t ret;

    /* Post one-way msg send */
    ret = NA_Msg_send_unexpected(info->na_class, info->context,
        na_perf_request_complete, &request_info, info->msg_unexp_buf,
        info->msg_unexp_header_size, info->msg_unexp_data, info->target_addr, 0,
        NA_PERF_TAG_LAT_INIT, info->msg_unexp_op_id);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_send_unexpected() failed (%s)",
        NA_Error_to_string(ret));

    /* Wait for completion */
    ret = na_perf_request_wait(info, &request_info, NA_MAX_IDLE_TIME, NULL);
    NA_TEST_CHECK_NA_ERROR(error, ret, "na_perf_request_wait() failed (%s)",
        NA_Error_to_string(ret));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_perf_run(struct na_perf_info *info, size_t buf_size, size_t skip)
{
    hg_time_t t1, t2;
    na_return_t ret;
    size_t i;

    /* Actual benchmark */
    for (i = 0; i < skip + (size_t) info->na_test_info.loop; i++) {
        struct na_perf_request_info request_info = {
            .completed = HG_ATOMIC_VAR_INIT(0),
            .complete_count = 0,
            .expected_count = (int32_t) 2};

        if (i == skip)
            hg_time_get_current(&t1);

        if (info->na_test_info.verify) {
            memset(info->msg_exp_buf, 0, buf_size);

            ret = NA_Msg_init_expected(
                info->na_class, info->msg_exp_buf, info->msg_exp_size_max);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "NA_Msg_init_unexpected() failed (%s)",
                NA_Error_to_string(ret));
        }

        /* Post recv */
        ret = NA_Msg_recv_expected(info->na_class, info->context,
            na_perf_request_complete, &request_info, info->msg_exp_buf,
            buf_size, info->msg_exp_data, info->target_addr, 0, NA_PERF_TAG_LAT,
            info->msg_exp_op_id);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_recv_expected() failed (%s)",
            NA_Error_to_string(ret));

        /* Post send */
        ret = NA_Msg_send_unexpected(info->na_class, info->context,
            na_perf_request_complete, &request_info, info->msg_unexp_buf,
            buf_size, info->msg_unexp_data, info->target_addr, 0,
            NA_PERF_TAG_LAT, info->msg_unexp_op_id);
        NA_TEST_CHECK_NA_ERROR(error, ret,
            "NA_Msg_send_unexpected() failed (%s)", NA_Error_to_string(ret));

        /* Wait for completion */
        ret = na_perf_request_wait(info, &request_info, NA_MAX_IDLE_TIME, NULL);
        NA_TEST_CHECK_NA_ERROR(error, ret, "na_perf_request_wait() failed (%s)",
            NA_Error_to_string(ret));

        if (info->na_test_info.verify) {
            ret = na_perf_verify_data(
                info->msg_exp_buf, buf_size, info->msg_exp_header_size);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "na_perf_verify_data() failed (%s)", NA_Error_to_string(ret));
        }
    }

    hg_time_get_current(&t2);

    na_perf_print_lat(info, buf_size, hg_time_subtract(t2, t1));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct na_perf_info info;
    size_t size, min_size;
    na_return_t na_ret;

    /* Initialize the interface */
    na_ret = na_perf_init(argc, argv, false, &info);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_perf_init() failed (%s)",
        NA_Error_to_string(na_ret));

    /* Init data */
    na_perf_init_data(info.msg_unexp_buf, info.msg_unexp_size_max,
        info.msg_unexp_header_size);
    na_perf_send_init(&info);

    min_size =
        (info.msg_unexp_header_size > 0) ? info.msg_unexp_header_size : 1;

    /* Header info */
    na_perf_print_header_lat(&info, BENCHMARK_NAME, min_size);

    /* Msg with different sizes */
    for (size = min_size; size <= info.msg_unexp_size_max; size *= 2) {
        na_ret = na_perf_run(&info, size,
            (size > NA_PERF_LARGE_SIZE) ? NA_PERF_LAT_SKIP_LARGE
                                        : NA_PERF_LAT_SKIP_SMALL);
        NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_perf_run(%zu) failed (%s)",
            size, NA_Error_to_string(na_ret));
    }

    /* Finalize interface */
    na_perf_send_finalize(&info);

    na_perf_cleanup(&info);

    return EXIT_SUCCESS;

error:
    na_perf_cleanup(&info);

    return EXIT_FAILURE;
}
