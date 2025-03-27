/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_unit.h"

/****************/
/* Local Macros */
/****************/

#define HG_TEST_PROGRESS_TIMEOUT 100
#define HG_TEST_TRIGGER_TIMEOUT  HG_MAX_IDLE_TIME

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct hg_test_worker {
    struct hg_thread_work thread_work;
    hg_class_t *hg_class;
    hg_context_t *context;
};

/********************/
/* Local Prototypes */
/********************/

static HG_THREAD_RETURN_TYPE
hg_test_progress_thread(void *arg);
static HG_THREAD_RETURN_TYPE
hg_test_progress_work(void *arg);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_progress_thread(void *arg)
{
    hg_context_t *context = (hg_context_t *) arg;
    struct hg_test_context_info *hg_test_context_info =
        (struct hg_test_context_info *) HG_Context_get_data(context);
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    hg_return_t ret = HG_SUCCESS;

    do {
        if (hg_atomic_get32(&hg_test_context_info->finalizing))
            break;

        ret = HG_Progress(context, HG_TEST_PROGRESS_TIMEOUT);
    } while (ret == HG_SUCCESS || ret == HG_TIMEOUT);
    HG_TEST_CHECK_ERROR(ret != HG_SUCCESS && ret != HG_TIMEOUT, done, tret,
        (hg_thread_ret_t) 0, "HG_Progress() failed (%s)",
        HG_Error_to_string(ret));

done:
    printf("Exiting\n");
    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_progress_work(void *arg)
{
    struct hg_test_worker *worker = (struct hg_test_worker *) arg;
    hg_context_t *context = worker->context;
    struct hg_test_context_info *hg_test_context_info =
        (struct hg_test_context_info *) HG_Context_get_data(context);
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    hg_return_t ret = HG_SUCCESS;

    do {
        unsigned int actual_count = 0;

        do {
            ret = HG_Trigger(context, 0, 1, &actual_count);
        } while ((ret == HG_SUCCESS) && actual_count);
        HG_TEST_CHECK_ERROR(ret != HG_SUCCESS && ret != HG_TIMEOUT, done, tret,
            (hg_thread_ret_t) 0, "HG_Trigger() failed (%s)",
            HG_Error_to_string(ret));

        if (hg_atomic_get32(&hg_test_context_info->finalizing)) {
            /* Make sure everything was progressed/triggered */
            do {
                ret = HG_Progress(context, 0);
                HG_Trigger(context, 0, 1, &actual_count);
            } while (ret == HG_SUCCESS);
            break;
        }

        /* Use same value as HG_TEST_TRIGGER_TIMEOUT for convenience */
        ret = HG_Progress(context, HG_TEST_TRIGGER_TIMEOUT);
    } while (ret == HG_SUCCESS || ret == HG_TIMEOUT);
    HG_TEST_CHECK_ERROR(ret != HG_SUCCESS && ret != HG_TIMEOUT, done, tret,
        (hg_thread_ret_t) 0, "HG_Progress() failed (%s)",
        HG_Error_to_string(ret));

done:
    return tret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct hg_unit_info info;
    struct hg_test_worker *progress_workers = NULL;
    struct hg_test_context_info *hg_test_context_info;
    hg_return_t ret;

    /* Force to listen */
    ret = hg_unit_init(argc, argv, true, &info);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "hg_unit_init() failed (%s)", HG_Error_to_string(ret));

    HG_TEST_READY_MSG();

    hg_test_context_info =
        (struct hg_test_context_info *) HG_Context_get_data(info.context);

    if (info.hg_test_info.na_test_info.max_contexts > 1) {
        hg_uint8_t context_count =
            (hg_uint8_t) (info.hg_test_info.na_test_info.max_contexts);
        hg_uint8_t i;

        progress_workers =
            malloc(sizeof(struct hg_test_worker) * context_count);
        HG_TEST_CHECK_ERROR_NORET(progress_workers == NULL, error,
            "Could not allocate progress_workers");

        progress_workers[0].thread_work.func = hg_test_progress_work;
        progress_workers[0].thread_work.args = &progress_workers[0];
        progress_workers[0].hg_class = info.hg_class;
        progress_workers[0].context = info.context;

        for (i = 0; i < context_count - 1; i++) {
            progress_workers[i + 1].thread_work.func = hg_test_progress_work;
            progress_workers[i + 1].thread_work.args = &progress_workers[i + 1];
            progress_workers[i + 1].hg_class = info.hg_class;
            progress_workers[i + 1].context = info.secondary_contexts[i];

            hg_thread_pool_post(
                info.thread_pool, &progress_workers[i + 1].thread_work);
        }
        /* Use main thread for progress on main context */
        hg_test_progress_work(&progress_workers[0]);
    } else {
        hg_thread_t progress_thread;

        hg_thread_create(
            &progress_thread, hg_test_progress_thread, info.context);

        do {
            if (hg_atomic_get32(&hg_test_context_info->finalizing))
                break;

            ret = HG_Trigger(info.context, HG_TEST_TRIGGER_TIMEOUT, 1, NULL);
        } while (ret == HG_SUCCESS || ret == HG_TIMEOUT);
        HG_TEST_CHECK_ERROR_NORET(ret != HG_SUCCESS && ret != HG_TIMEOUT, error,
            "HG_Trigger() failed (%s)", HG_Error_to_string(ret));

        hg_thread_join(progress_thread);
    }

    hg_unit_cleanup(&info);
    free(progress_workers);

    return EXIT_SUCCESS;

error:
    hg_unit_cleanup(&info);
    free(progress_workers);

    return EXIT_FAILURE;
}
