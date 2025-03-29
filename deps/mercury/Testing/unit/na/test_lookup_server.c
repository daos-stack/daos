/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_test_common.h"

#include "mercury_thread.h"

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct na_test_common_recv_info {
    struct na_test_common_class_info *info;
    na_return_t ret;
    bool post_new_recv;
    bool done;
};

/********************/
/* Local Prototypes */
/********************/

static HG_THREAD_RETURN_TYPE
na_test_common_loop_thread(void *arg);

static na_return_t
na_test_common_loop(struct na_test_common_class_info *info);

static void
na_test_common_recv_cb(const struct na_cb_info *na_cb_info);

static void
na_test_common_process_recv(struct na_test_common_recv_info *recv_info,
    void *actual_buf, size_t actual_buf_size, na_addr_t *source, na_tag_t tag);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
na_test_common_loop_thread(void *arg)
{
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    na_return_t na_ret;

    na_ret = na_test_common_loop((struct na_test_common_class_info *) arg);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_test_common_loop() failed (%s)",
        NA_Error_to_string(na_ret));

error:
    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_common_loop(struct na_test_common_class_info *info)
{
    struct na_test_common_recv_info recv_info;
    na_return_t ret;

    memset(&recv_info, 0, sizeof(recv_info));
    recv_info.info = info;
    recv_info.post_new_recv = true;

    do {
        unsigned int actual_count = 0, timeout_progress = 0;

        if (recv_info.post_new_recv) {
            recv_info.post_new_recv = false;

            /* Post recv */
            ret = NA_Msg_recv_unexpected(info->na_class, info->context,
                na_test_common_recv_cb, &recv_info, info->msg_unexp_buf,
                info->msg_unexp_size_max, info->msg_unexp_data,
                info->msg_unexp_op_id);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "NA_Msg_recv_unexpected() failed (%s)",
                NA_Error_to_string(ret));
        }

        do {
            ret = NA_Trigger(info->context, 1, &actual_count);
            NA_TEST_CHECK_ERROR(recv_info.ret != NA_SUCCESS, error, ret,
                recv_info.ret, "NA_Msg_recv_unexpected() failed (%s)",
                NA_Error_to_string(recv_info.ret));
        } while ((ret == NA_SUCCESS) && actual_count);
        NA_TEST_CHECK_ERROR_NORET(ret != NA_SUCCESS, error,
            "NA_Trigger() failed (%s)", NA_Error_to_string(ret));

        if (recv_info.done) {
            printf("Exiting...\n");
            break;
        }

        /* Safe to block */
        if (NA_Poll_try_wait(info->na_class, info->context))
            timeout_progress = 1000;

        if (info->poll_set && timeout_progress > 0) {
            struct hg_poll_event poll_event = {.events = 0, .data.ptr = NULL};
            unsigned int actual_events = 0;

            hg_poll_wait(info->poll_set, timeout_progress, 1, &poll_event,
                &actual_events);
            if (actual_events == 0)
                continue;

            timeout_progress = 0;
        }

        ret = NA_Progress(info->na_class, info->context, timeout_progress);
    } while ((ret == NA_SUCCESS) || (ret == NA_TIMEOUT));
    NA_TEST_CHECK_ERROR_NORET(ret != NA_SUCCESS && ret != NA_TIMEOUT, error,
        "NA_Progress() failed (%s)", NA_Error_to_string(ret));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_test_common_recv_cb(const struct na_cb_info *na_cb_info)
{
    struct na_test_common_recv_info *recv_info =
        (struct na_test_common_recv_info *) na_cb_info->arg;
    const struct na_cb_info_recv_unexpected *msg_info =
        &na_cb_info->info.recv_unexpected;

    na_test_common_process_recv(recv_info, NULL, msg_info->actual_buf_size,
        msg_info->source, msg_info->tag);

    recv_info->post_new_recv = true;
}

/*---------------------------------------------------------------------------*/
static void
na_test_common_process_recv(struct na_test_common_recv_info *recv_info,
    void NA_UNUSED *actual_buf, size_t NA_UNUSED actual_buf_size,
    na_addr_t *source, na_tag_t tag)
{
    struct na_test_common_class_info *info = recv_info->info;
    na_return_t ret = NA_SUCCESS;

    switch (tag) {
        case NA_TEST_COMMON_TAG_DONE:
            recv_info->done = true;
            break;
        case NA_TEST_COMMON_TAG_CONTINUE:
            break;
        default:
            ret = NA_PROTOCOL_ERROR;
            break;
    }

    (void) NA_Addr_free(info->na_class, source);

    recv_info->ret = ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct na_test_common_info info;
    na_return_t na_ret;
    hg_thread_t *progress_threads = NULL;
    size_t i;

    /* Initialize the interface */
    na_ret = na_test_common_init(argc, argv, true, &info);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_test_common_init() failed (%s)",
        NA_Error_to_string(na_ret));

    progress_threads = (hg_thread_t *) malloc(
        sizeof(*progress_threads) * info.na_test_info.max_classes);
    NA_TEST_CHECK_ERROR_NORET(
        progress_threads == NULL, error, "Could not allocate progress threads");

    for (i = 0; i < info.na_test_info.max_classes; i++) {
        int rc = hg_thread_create(&progress_threads[i],
            na_test_common_loop_thread, &info.class_info[i]);
        NA_TEST_CHECK_ERROR_NORET(rc != 0, error, "hg_thread_create() failed");
    }

    for (i = 0; i < info.na_test_info.max_classes; i++)
        hg_thread_join(progress_threads[i]);

    /* Finalize interface */
    printf("Finalizing...\n");
    na_test_common_cleanup(&info);
    free(progress_threads);

    return EXIT_SUCCESS;

error:
    na_test_common_cleanup(&info);
    free(progress_threads);

    return EXIT_FAILURE;
}
