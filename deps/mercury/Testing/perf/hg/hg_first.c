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
#define BENCHMARK_NAME "Time of first RPC"

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static hg_return_t
hg_perf_run(
    const struct hg_test_info *hg_test_info, struct hg_perf_class_info *info);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_perf_run(
    const struct hg_test_info *hg_test_info, struct hg_perf_class_info *info)
{
    struct hg_perf_request request = {
        .expected_count = (int32_t) info->handle_max,
        .complete_count = 0,
        .completed = HG_ATOMIC_VAR_INIT(0)};
    unsigned int i;
    hg_time_t t1, t2;
    hg_return_t ret;

    if (hg_test_info->na_test_info.mpi_info.size > 1)
        NA_Test_barrier(&hg_test_info->na_test_info);
    hg_time_get_current(&t1);

    for (i = 0; i < info->handle_max; i++) {
        ret = HG_Forward(
            info->handles[i], hg_perf_request_complete, &request, NULL);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));
    }

    ret = hg_perf_request_wait(info, &request, HG_MAX_IDLE_TIME, NULL);
    HG_TEST_CHECK_HG_ERROR(error, ret, "hg_perf_request_wait() failed (%s)",
        HG_Error_to_string(ret));

    if (hg_test_info->na_test_info.mpi_info.size > 1)
        NA_Test_barrier(&hg_test_info->na_test_info);

    hg_time_get_current(&t2);

    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        hg_perf_print_time(hg_test_info, info, 0, hg_time_subtract(t2, t1));

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
    hg_return_t hg_ret;

    /* Initialize the interface */
    hg_ret = hg_perf_init(argc, argv, false, &perf_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_init() failed (%s)",
        HG_Error_to_string(hg_ret));
    hg_test_info = &perf_info.hg_test_info;
    info = &perf_info.class_info[0];

    /* Set HG handles */
    hg_ret = hg_perf_set_handles(hg_test_info, info, HG_PERF_FIRST);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_set_handles() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Header info */
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        hg_perf_print_header_time(hg_test_info, info, BENCHMARK_NAME);

    /* Always a NULL RPC */
    hg_ret = hg_perf_run(hg_test_info, info);
    HG_TEST_CHECK_HG_ERROR(
        error, hg_ret, "hg_perf_run() failed (%s)", HG_Error_to_string(hg_ret));

    /* Finalize interface */
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        hg_perf_send_done(info);

    hg_perf_cleanup(&perf_info);

    return EXIT_SUCCESS;

error:
    hg_perf_cleanup(&perf_info);

    return EXIT_FAILURE;
}
