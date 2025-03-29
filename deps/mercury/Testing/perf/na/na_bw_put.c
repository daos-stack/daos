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
#define BENCHMARK_NAME "NA_Put() Bandwidth"

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
na_perf_run(struct na_perf_info *info, size_t buf_size, size_t skip)
{
    hg_time_t t1, t2, t3, t4, t_reg = hg_time_from_ms(0),
                              t_dereg = hg_time_from_ms(0);
    na_return_t ret;
    size_t i, j;

    /* Actual benchmark */
    for (i = 0; i < skip + (size_t) info->na_test_info.loop; i++) {
        struct na_perf_request_info request_info = {
            .completed = HG_ATOMIC_VAR_INIT(0),
            .complete_count = 0,
            .expected_count = (int32_t) info->rma_count};

        if (i == skip)
            hg_time_get_current(&t1);

        if (info->na_test_info.verify)
            memset(info->verify_buf, 0, buf_size);

        if (info->na_test_info.force_register) {
            ret = NA_Mem_handle_create(info->na_class, info->rma_buf,
                info->rma_size_max * info->rma_count, NA_MEM_READ_ONLY,
                &info->local_handle);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "NA_Mem_handle_create() failed (%s)", NA_Error_to_string(ret));

            if (i >= skip)
                hg_time_get_current(&t3);
            ret = NA_Mem_register(
                info->na_class, info->local_handle, NA_MEM_TYPE_HOST, 0);
            NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_register() failed (%s)",
                NA_Error_to_string(ret));
            if (i >= skip) {
                hg_time_get_current(&t4);
                t_reg = hg_time_add(t_reg, hg_time_subtract(t4, t3));
            }
        }

        /* Post puts */
        for (j = 0; j < info->rma_count; j++) {
            ret =
                NA_Put(info->na_class, info->context, na_perf_request_complete,
                    &request_info, info->local_handle, j * info->rma_size_max,
                    info->remote_handle, j * info->rma_size_max, buf_size,
                    info->target_addr, 0, info->rma_op_ids[j]);
            NA_TEST_CHECK_NA_ERROR(
                error, ret, "NA_Put() failed (%s)", NA_Error_to_string(ret));
        }

        /* Wait for completion */
        ret = na_perf_request_wait(info, &request_info, NA_MAX_IDLE_TIME, NULL);
        NA_TEST_CHECK_NA_ERROR(error, ret, "na_perf_request_wait() failed (%s)",
            NA_Error_to_string(ret));

        if (info->na_test_info.verify) {
            request_info.complete_count = 0;
            hg_atomic_init32(&request_info.completed, 0);

            /* Post gets */
            for (j = 0; j < info->rma_count; j++) {
                ret = NA_Get(info->na_class, info->context,
                    na_perf_request_complete, &request_info,
                    info->verify_handle, j * info->rma_size_max,
                    info->remote_handle, j * info->rma_size_max, buf_size,
                    info->target_addr, 0, info->rma_op_ids[j]);
                NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Get() failed (%s)",
                    NA_Error_to_string(ret));
            }

            /* Wait for completion */
            ret = na_perf_request_wait(
                info, &request_info, NA_MAX_IDLE_TIME, NULL);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "na_perf_request_wait() failed (%s)", NA_Error_to_string(ret));

            for (j = 0; j < info->rma_count; j++) {
                ret = na_perf_verify_data(
                    (const char *) info->verify_buf + j * info->rma_size_max,
                    buf_size, 0);
                NA_TEST_CHECK_NA_ERROR(error, ret,
                    "na_perf_verify_data() failed (%s)",
                    NA_Error_to_string(ret));
            }
        }

        if (info->na_test_info.force_register) {
            if (i >= skip)
                hg_time_get_current(&t3);
            NA_Mem_deregister(info->na_class, info->local_handle);
            if (i >= skip) {
                hg_time_get_current(&t4);
                t_dereg = hg_time_add(t_dereg, hg_time_subtract(t4, t3));
            }
            NA_Mem_handle_free(info->na_class, info->local_handle);
            info->local_handle = NULL;
        }
    }

    hg_time_get_current(&t2);

    na_perf_print_bw(info, buf_size, hg_time_subtract(t2, t1), t_reg, t_dereg);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct na_perf_info info;
    size_t size, i;
    na_return_t na_ret;

    /* Initialize the interface */
    na_ret = na_perf_init(argc, argv, false, &info);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_perf_init() failed (%s)",
        NA_Error_to_string(na_ret));

    /* Init data */
    for (i = 0; i < info.rma_count; i++)
        na_perf_init_data((char *) info.rma_buf + i * info.rma_size_max,
            info.rma_size_max, 0);

    /* Retrieve server memory handle */
    na_ret = na_perf_mem_handle_recv(&info, NA_PERF_TAG_PUT);
    NA_TEST_CHECK_NA_ERROR(error, na_ret,
        "na_perf_mem_handle_recv() failed (%s)", NA_Error_to_string(na_ret));

    /* Header info */
    na_perf_print_header_bw(&info, BENCHMARK_NAME);

    /* Msg with different sizes */
    for (size = info.rma_size_min; size <= info.rma_size_max; size *= 2) {
        na_ret = na_perf_run(&info, size,
            (size > NA_PERF_LARGE_SIZE) ? NA_PERF_BW_SKIP_LARGE
                                        : NA_PERF_BW_SKIP_SMALL);
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
