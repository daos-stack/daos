/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include "mercury_perf.h"

#include "mercury_thread.h"

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static HG_THREAD_RETURN_TYPE
hg_perf_loop_thread(void *arg);

#if !defined(_WIN32) && !defined(__APPLE__)
static hg_return_t
hg_perf_loop_thread_set_affinity(struct hg_perf_class_info *info);
#endif

static hg_return_t
hg_perf_loop(struct hg_perf_class_info *info);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_perf_loop_thread(void *arg)
{
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    hg_return_t hg_ret;

#if !defined(_WIN32) && !defined(__APPLE__)
    (void) hg_perf_loop_thread_set_affinity((struct hg_perf_class_info *) arg);
#endif

    hg_ret = hg_perf_loop((struct hg_perf_class_info *) arg);
    HG_TEST_CHECK_HG_ERROR(
        done, hg_ret, "hg_perf_loop() failed (%s)", HG_Error_to_string(hg_ret));

done:
    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
#if !defined(_WIN32) && !defined(__APPLE__)
static hg_return_t
hg_perf_loop_thread_set_affinity(struct hg_perf_class_info *info)
{
    hg_cpu_set_t orig_cpu_set, new_cpu_set;
    size_t cpu, i = 0;
    hg_return_t ret;
    int rc;

    /* Retrive affinity set on main process */
    CPU_ZERO(&orig_cpu_set);
    rc = hg_thread_getaffinity(hg_thread_self(), &orig_cpu_set);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "Could not retrieve CPU affinity");
    HG_TEST_CHECK_ERROR(info->class_id > CPU_COUNT(&orig_cpu_set), error, ret,
        HG_PROTOCOL_ERROR,
        "Could not set affinity, class ID (%d) > CPU count (%d)",
        info->class_id, CPU_COUNT(&orig_cpu_set));

    CPU_ZERO(&new_cpu_set);
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (CPU_ISSET(cpu, &orig_cpu_set)) {
            if (i == (size_t) info->class_id) {
                CPU_SET(cpu, &new_cpu_set);
                break;
            }
            i++;
        }
    }

    rc = hg_thread_setaffinity(hg_thread_self(), &new_cpu_set);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "Could not set CPU affinity");

    CPU_ZERO(&orig_cpu_set);
    rc = hg_thread_getaffinity(hg_thread_self(), &orig_cpu_set);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "Could not retrieve CPU affinity");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET(cpu, &orig_cpu_set))
            HG_TEST_LOG_DEBUG(
                "Class ID %d bound to CPU %zu\n", info->class_id, cpu);

    return HG_SUCCESS;

error:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_perf_loop(struct hg_perf_class_info *info)
{
    hg_return_t ret;

    while (!info->done) {
        unsigned int count = 0, actual_count = 0;

        if (info->poll_set && !HG_Event_ready(info->context)) {
            struct hg_poll_event poll_event = {.events = 0, .data.ptr = NULL};
            unsigned int actual_events = 0;
            int rc;

            HG_TEST_LOG_DEBUG("Waiting for 1000 ms");

            rc = hg_poll_wait(
                info->poll_set, 1000, 1, &poll_event, &actual_events);
            HG_TEST_CHECK_ERROR(rc != 0, error, ret, HG_PROTOCOL_ERROR,
                "hg_poll_wait() failed");
        }

        ret = HG_Event_progress(info->context, &count);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Progress() failed (%s)", HG_Error_to_string(ret));

        if (count == 0)
            continue;

        ret = HG_Event_trigger(info->context, count, &actual_count);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Trigger() failed (%s)", HG_Error_to_string(ret));
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct hg_perf_info info;
    struct hg_test_info *hg_test_info;
    hg_return_t hg_ret;
    hg_thread_t *progress_threads = NULL;

    /* Initialize the interface */
    hg_ret = hg_perf_init(argc, argv, true, &info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_init() failed (%s)",
        HG_Error_to_string(hg_ret));
    hg_test_info = &info.hg_test_info;
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        printf("# %d server process(es)\n",
            hg_test_info->na_test_info.mpi_info.size);

    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        HG_TEST_READY_MSG();

    if (info.class_max > 1) {
        size_t i;

        progress_threads =
            (hg_thread_t *) malloc(sizeof(*progress_threads) * info.class_max);
        HG_TEST_CHECK_ERROR_NORET(progress_threads == NULL, error,
            "Could not allocate progress threads");

        for (i = 0; i < info.class_max; i++) {
            int rc = hg_thread_create(
                &progress_threads[i], hg_perf_loop_thread, &info.class_info[i]);
            HG_TEST_CHECK_ERROR_NORET(
                rc != 0, error, "hg_thread_create() failed");
        }

        for (i = 0; i < info.class_max; i++)
            hg_thread_join(progress_threads[i]);
    } else {
        hg_ret = hg_perf_loop(&info.class_info[0]);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_perf_loop() failed (%s)",
            HG_Error_to_string(hg_ret));
    }

    /* Finalize interface */
    if (hg_test_info->na_test_info.mpi_info.rank == 0)
        printf("Finalizing...\n");
    hg_perf_cleanup(&info);
    free(progress_threads);

    return EXIT_SUCCESS;

error:
    hg_perf_cleanup(&info);
    free(progress_threads);

    return EXIT_FAILURE;
}
