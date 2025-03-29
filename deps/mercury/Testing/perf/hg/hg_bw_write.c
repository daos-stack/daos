/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_perf.h"

/****************/
/* Local Macros */
/****************/
#define BENCHMARK_NAME "Write BW (server bulk pull)"

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static hg_return_t
hg_perf_run(const struct hg_test_info *hg_test_info,
    struct hg_perf_class_info *info, size_t buf_size, size_t skip);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_perf_run(const struct hg_test_info *hg_test_info,
    struct hg_perf_class_info *info, size_t buf_size, size_t skip)
{
    size_t comm_rank = (size_t) hg_test_info->na_test_info.mpi_info.rank,
           comm_size = (size_t) hg_test_info->na_test_info.mpi_info.size,
           loop = (size_t) hg_test_info->na_test_info.loop;
    hg_time_t t1, t2, t3, t4, t_reg = hg_time_from_ms(0),
                              t_dereg = hg_time_from_ms(0);
    hg_return_t ret;
    size_t i;

    /* Warm up for RPC */
    for (i = 0; i < skip + loop; i++) {
        struct hg_perf_request request = {
            .expected_count = (int32_t) info->handle_max,
            .complete_count = 0,
            .completed = HG_ATOMIC_VAR_INIT(0)};
        size_t j;

        if (i == skip) {
            if (comm_size > 1)
                NA_Test_barrier(&hg_test_info->na_test_info);
            hg_time_get_current(&t1);
        }

        if (hg_test_info->na_test_info.force_register) {
            if (i >= skip)
                hg_time_get_current(&t3);
            for (j = 0; j < info->handle_max; j++) {
                hg_size_t bulk_size = info->buf_size_max * info->bulk_count;
                ret = HG_Bulk_create(info->hg_class, 1, &info->bulk_bufs[j],
                    &bulk_size, HG_BULK_READ_ONLY,
                    &info->local_bulk_handles[j]);
                HG_TEST_CHECK_HG_ERROR(error, ret,
                    "HG_Bulk_create() failed (%s)", HG_Error_to_string(ret));
            }
            if (i >= skip) {
                hg_time_get_current(&t4);
                t_reg = hg_time_add(t_reg, hg_time_subtract(t4, t3));
            }
        }

        for (j = 0; j < info->handle_max; j++) {
            struct hg_perf_bulk_info in_struct = {
                .bulk = (hg_test_info->na_test_info.force_register)
                            ? info->local_bulk_handles[j]
                            : HG_BULK_NULL,
                .handle_id = (uint32_t) ((comm_rank + j * comm_size) /
                                         info->target_addr_max),
                .size = (uint32_t) buf_size};

            ret = HG_Forward(info->handles[j], hg_perf_request_complete,
                &request, &in_struct);
            HG_TEST_CHECK_HG_ERROR(error, ret, "HG_Forward() failed (%s)",
                HG_Error_to_string(ret));
        }

        ret = hg_perf_request_wait(info, &request, HG_MAX_IDLE_TIME, NULL);
        HG_TEST_CHECK_HG_ERROR(error, ret, "hg_perf_request_wait() failed (%s)",
            HG_Error_to_string(ret));

        if (hg_test_info->na_test_info.force_register) {
            if (i >= skip)
                hg_time_get_current(&t3);
            for (j = 0; j < info->handle_max; j++) {
                (void) HG_Bulk_free(info->local_bulk_handles[j]);
                info->local_bulk_handles[j] = HG_BULK_NULL;
            }
            if (i >= skip) {
                hg_time_get_current(&t4);
                t_dereg = hg_time_add(t_dereg, hg_time_subtract(t4, t3));
            }
        }
    }

    if (comm_size > 1)
        NA_Test_barrier(&hg_test_info->na_test_info);

    hg_time_get_current(&t2);

    if (comm_rank == 0)
        hg_perf_print_bw(hg_test_info, info, buf_size, hg_time_subtract(t2, t1),
            t_reg, t_dereg);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct hg_perf_info perf_info;
    struct hg_test_info *hg_test_info;
    struct hg_perf_class_info *info;
    size_t size;
    hg_return_t hg_ret;

    /* Initialize the interface */
    hg_ret = hg_perf_init(argc, argv, false, &perf_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_init() failed (%s)",
        HG_Error_to_string(hg_ret));
    hg_test_info = &perf_info.hg_test_info;
    info = &perf_info.class_info[0];

    /* Allocate bulk buffers */
    hg_ret = hg_perf_bulk_buf_init(hg_test_info, info, HG_BULK_PULL);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_bulk_buf_init() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Set HG handles */
    hg_ret = hg_perf_set_handles(hg_test_info, info, HG_PERF_BW_WRITE);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_set_handles() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Header info */
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        hg_perf_print_header_bw(hg_test_info, info, BENCHMARK_NAME);

    /* Bulk RPC with different sizes */
    for (size = MAX(1, info->buf_size_min); size <= info->buf_size_max;
         size *= 2) {
        hg_ret = hg_perf_run(hg_test_info, info, size,
            (size > HG_PERF_LARGE_SIZE) ? HG_PERF_LAT_SKIP_LARGE
                                        : HG_PERF_LAT_SKIP_SMALL);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_run() failed (%s)",
            HG_Error_to_string(hg_ret));
    }

    /* Finalize interface */
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        hg_perf_send_done(info);

    hg_perf_cleanup(&perf_info);

    return EXIT_SUCCESS;

error:
    hg_perf_cleanup(&perf_info);

    return EXIT_FAILURE;
}
