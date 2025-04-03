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

/************************************/
/* Local Type and Struct Definition */
/************************************/

typedef na_return_t (*na_perf_recv_op_t)(na_class_t *na_class,
    na_context_t *context, na_cb_t callback, void *arg, void *buf,
    size_t buf_size, void *plugin_data, na_op_id_t *op_id);

struct na_perf_recv_info {
    struct na_perf_info *info;
    na_perf_recv_op_t recv_op;
    na_cb_t recv_op_cb;
    na_return_t ret;
    const char *recv_op_name;
    bool post_new_recv;
    bool done;
};

/********************/
/* Local Prototypes */
/********************/

static na_return_t
na_perf_loop(struct na_perf_info *info, na_perf_recv_op_t recv_op,
    na_cb_t recv_op_cb, const char *recv_op_name);

static void
na_perf_recv_cb(const struct na_cb_info *na_cb_info);

static void
na_perf_multi_recv_cb(const struct na_cb_info *na_cb_info);

static void
na_perf_process_recv(struct na_perf_recv_info *recv_info, void *actual_buf,
    size_t actual_buf_size, na_addr_t *source, na_tag_t tag);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static na_return_t
na_perf_loop(struct na_perf_info *info, na_perf_recv_op_t recv_op,
    na_cb_t recv_op_cb, const char *recv_op_name)
{
    struct na_perf_recv_info recv_info;
    na_return_t ret;

    memset(&recv_info, 0, sizeof(recv_info));
    recv_info.info = info;
    recv_info.recv_op = recv_op;
    recv_info.recv_op_cb = recv_op_cb;
    recv_info.recv_op_name = recv_op_name;

    /* Post initial recv */
    ret = recv_op(info->na_class, info->context, recv_op_cb, &recv_info,
        info->msg_unexp_buf, info->msg_unexp_size_max, info->msg_unexp_data,
        info->msg_unexp_op_id);
    NA_TEST_CHECK_NA_ERROR(
        error, ret, "%s() failed (%s)", recv_op_name, NA_Error_to_string(ret));

    /* Progress loop */
    while (!recv_info.done) {
        unsigned int count = 0, actual_count = 0;

        if (info->poll_set && NA_Poll_try_wait(info->na_class, info->context)) {
            struct hg_poll_event poll_event = {.events = 0, .data.ptr = NULL};
            unsigned int actual_events = 0;
            int rc;

            NA_TEST_LOG_DEBUG("Waiting for 1000 ms");

            rc = hg_poll_wait(
                info->poll_set, 1000, 1, &poll_event, &actual_events);
            NA_TEST_CHECK_ERROR(rc != 0, error, ret, NA_PROTOCOL_ERROR,
                "hg_poll_wait() failed");
        }

        ret = NA_Poll(info->na_class, info->context, &count);
        NA_TEST_CHECK_NA_ERROR(
            error, ret, "NA_Poll() failed (%s)", NA_Error_to_string(ret));

        if (count == 0)
            continue;

        ret = NA_Trigger(info->context, count, &actual_count);
        NA_TEST_CHECK_NA_ERROR(
            error, ret, "NA_Trigger() failed (%s)", NA_Error_to_string(ret));

        NA_TEST_CHECK_ERROR(recv_info.ret != NA_SUCCESS, error, ret,
            recv_info.ret, "%s() failed (%s)", recv_op_name,
            NA_Error_to_string(recv_info.ret));
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_perf_recv_cb(const struct na_cb_info *na_cb_info)
{
    struct na_perf_recv_info *recv_info =
        (struct na_perf_recv_info *) na_cb_info->arg;
    const struct na_cb_info_recv_unexpected *msg_info =
        &na_cb_info->info.recv_unexpected;

    recv_info->post_new_recv = true;
    na_perf_process_recv(recv_info, NULL, msg_info->actual_buf_size,
        msg_info->source, msg_info->tag);
}

/*---------------------------------------------------------------------------*/
static void
na_perf_multi_recv_cb(const struct na_cb_info *na_cb_info)
{
    struct na_perf_recv_info *recv_info =
        (struct na_perf_recv_info *) na_cb_info->arg;
    const struct na_cb_info_multi_recv_unexpected *msg_info =
        &na_cb_info->info.multi_recv_unexpected;

    recv_info->post_new_recv = msg_info->last;
    na_perf_process_recv(recv_info, msg_info->actual_buf,
        msg_info->actual_buf_size, msg_info->source, msg_info->tag);
}

/*---------------------------------------------------------------------------*/
static void
na_perf_process_recv(struct na_perf_recv_info *recv_info,
    void NA_UNUSED *actual_buf, size_t actual_buf_size, na_addr_t *source,
    na_tag_t tag)
{
    struct na_perf_info *info = recv_info->info;
    na_return_t ret = NA_SUCCESS;
    size_t i;

    /* Repost recv in advance to prevent buffering of unexpected msg */
    if (recv_info->post_new_recv && tag != NA_PERF_TAG_DONE) {
        recv_info->post_new_recv = false;

        /* Post recv */
        ret = recv_info->recv_op(info->na_class, info->context,
            recv_info->recv_op_cb, recv_info, info->msg_unexp_buf,
            info->msg_unexp_size_max, info->msg_unexp_data,
            info->msg_unexp_op_id);
        NA_TEST_CHECK_NA_ERROR(done, ret, "%s() failed (%s)",
            recv_info->recv_op_name, NA_Error_to_string(ret));
    }

    switch (tag) {
        case NA_PERF_TAG_LAT_INIT:
            /* init data separately to avoid a memcpy */
            na_perf_init_data(info->msg_exp_buf, info->msg_exp_size_max,
                info->msg_exp_header_size);
            break;
        case NA_PERF_TAG_LAT:
            /* Respond with same data */
            ret = NA_Msg_send_expected(info->na_class, info->context, NULL,
                NULL, info->msg_exp_buf, actual_buf_size, info->msg_exp_data,
                source, 0, tag, info->msg_exp_op_id);
            NA_TEST_CHECK_NA_ERROR(done, ret,
                "NA_Msg_send_expected() failed (%s)", NA_Error_to_string(ret));
            break;
        case NA_PERF_TAG_PUT:
            ret = na_perf_mem_handle_send(info, source, tag);
            NA_TEST_CHECK_NA_ERROR(done, ret,
                "na_perf_mem_handle_send() failed (%s)",
                NA_Error_to_string(ret));
            break;
        case NA_PERF_TAG_GET:
            /* Init data */
            for (i = 0; i < info->rma_count; i++)
                na_perf_init_data(
                    (char *) info->rma_buf + i * info->rma_size_max,
                    info->rma_size_max, 0);

            ret = na_perf_mem_handle_send(info, source, tag);
            NA_TEST_CHECK_NA_ERROR(done, ret,
                "na_perf_mem_handle_send() failed (%s)",
                NA_Error_to_string(ret));
            break;
        case NA_PERF_TAG_DONE:
            recv_info->done = true;
            break;
        default:
            ret = NA_PROTOCOL_ERROR;
            break;
    }

    NA_Addr_free(info->na_class, source);

done:
    recv_info->ret = ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct na_perf_info info;
    na_return_t na_ret;

    /* Initialize the interface */
    na_ret = na_perf_init(argc, argv, true, &info);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_perf_init() failed (%s)",
        NA_Error_to_string(na_ret));

    HG_TEST_READY_MSG();

    /* Loop */
    if (NA_Has_opt_feature(info.na_class, NA_OPT_MULTI_RECV) &&
        !info.na_test_info.no_multi_recv)
        na_ret = na_perf_loop(&info, NA_Msg_multi_recv_unexpected,
            na_perf_multi_recv_cb, "NA_Msg_multi_recv_unexpected");
    else
        na_ret = na_perf_loop(&info, NA_Msg_recv_unexpected, na_perf_recv_cb,
            "NA_Msg_recv_unexpected");
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_perf_loop() failed (%s)",
        NA_Error_to_string(na_ret));

    /* Finalize interface */
    printf("Finalizing...\n");
    na_perf_cleanup(&info);

    return EXIT_SUCCESS;

error:
    na_perf_cleanup(&info);

    return EXIT_FAILURE;
}
