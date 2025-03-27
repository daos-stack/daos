/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_test_common.h"

#include "mercury_mem.h"

/****************/
/* Local Macros */
/****************/

#define STRING(s)  #s
#define XSTRING(s) STRING(s)
#define VERSION_NAME                                                           \
    XSTRING(NA_VERSION_MAJOR)                                                  \
    "." XSTRING(NA_VERSION_MINOR) "." XSTRING(NA_VERSION_PATCH)

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static na_return_t
na_test_common_class_init(
    struct na_test_common_class_info *info, na_class_t *na_class, bool listen);

static void
na_test_common_class_cleanup(struct na_test_common_class_info *info);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
int
na_test_common_request_progress(unsigned int timeout, void *arg)
{
    struct na_test_common_class_info *info =
        (struct na_test_common_class_info *) arg;
    unsigned int timeout_progress = 0;
    int ret = HG_UTIL_SUCCESS;

    /* Safe to block */
    if (NA_Poll_try_wait(info->na_class, info->context))
        timeout_progress = timeout;

    if (info->poll_set && timeout_progress > 0) {
        struct hg_poll_event poll_event = {.events = 0, .data.ptr = NULL};
        unsigned int actual_events = 0;

        hg_poll_wait(
            info->poll_set, timeout_progress, 1, &poll_event, &actual_events);
        if (actual_events == 0)
            return HG_UTIL_FAIL;

        timeout_progress = 0;
    }

    /* Progress */
    if (NA_Progress(info->na_class, info->context, timeout_progress) !=
        NA_SUCCESS)
        ret = HG_UTIL_FAIL;

    return ret;
}

/*---------------------------------------------------------------------------*/
int
na_test_common_request_trigger(
    unsigned int timeout, unsigned int *flag, void *arg)
{
    struct na_test_common_class_info *info =
        (struct na_test_common_class_info *) arg;
    unsigned int actual_count = 0;
    int ret = HG_UTIL_SUCCESS;

    (void) timeout;

    if (NA_Trigger(info->context, 1, &actual_count) != NA_SUCCESS)
        ret = HG_UTIL_FAIL;
    *flag = (actual_count) ? true : false;

    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_test_common_request_complete(const struct na_cb_info *na_cb_info)
{
    hg_request_complete((hg_request_t *) na_cb_info->arg);
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_common_init(
    int argc, char *argv[], bool listen, struct na_test_common_info *info)
{
    na_return_t ret;
    size_t i;

    /* Initialize the interface */
    memset(info, 0, sizeof(*info));
    if (listen)
        info->na_test_info.listen = true;

    ret = NA_Test_init(argc, argv, &info->na_test_info);
    NA_TEST_CHECK_NA_ERROR(
        error, ret, "NA_Test_init() failed (%s)", NA_Error_to_string(ret));

    info->class_info = (struct na_test_common_class_info *) calloc(
        info->na_test_info.max_classes, sizeof(*info->class_info));
    NA_TEST_CHECK_ERROR(info->class_info == NULL, error, ret, NA_NOMEM,
        "Could not allocate class infos");

    for (i = 0; i < info->na_test_info.max_classes; i++) {
        ret = na_test_common_class_init(
            &info->class_info[i], info->na_test_info.na_classes[i], listen);
        NA_TEST_CHECK_NA_ERROR(error, ret, "Could not initialize common class");
    }

    return NA_SUCCESS;

error:
    na_test_common_cleanup(info);
    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_test_common_cleanup(struct na_test_common_info *info)
{
    if (info->class_info) {
        size_t i;
        for (i = 0; i < info->na_test_info.max_classes; i++)
            na_test_common_class_cleanup(&info->class_info[i]);
        free(info->class_info);
    }
    info->class_info = NULL;
    NA_Test_finalize(&info->na_test_info);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_common_class_init(
    struct na_test_common_class_info *info, na_class_t *na_class, bool listen)
{
    na_return_t ret;

    info->na_class = na_class;

    /* Set up */
    info->context = NA_Context_create(info->na_class);
    NA_TEST_CHECK_ERROR(info->context == NULL, error, ret, NA_NOMEM,
        "NA_Context_create() failed");

    info->poll_fd = NA_Poll_get_fd(info->na_class, info->context);
    if (info->poll_fd > 0) {
        struct hg_poll_event poll_event = {
            .events = HG_POLLIN, .data.ptr = NULL};
        int rc;

        info->poll_set = hg_poll_create();
        NA_TEST_CHECK_ERROR(info->poll_set == NULL, error, ret, NA_NOMEM,
            "hg_poll_create() failed");

        rc = hg_poll_add(info->poll_set, info->poll_fd, &poll_event);
        NA_TEST_CHECK_ERROR(
            rc != 0, error, ret, NA_PROTOCOL_ERROR, "hg_poll_add() failed");
    }

    info->request_class = hg_request_init(
        na_test_common_request_progress, na_test_common_request_trigger, info);
    NA_TEST_CHECK_ERROR(info->request_class == NULL, error, ret, NA_NOMEM,
        "hg_request_init() failed");

    /* Set max sizes */
    info->msg_unexp_size_max = NA_Msg_get_max_unexpected_size(info->na_class);
    NA_TEST_CHECK_ERROR(info->msg_unexp_size_max == 0, error, ret,
        NA_INVALID_ARG, "max unexpected msg size cannot be zero");
    info->msg_unexp_header_size =
        NA_Msg_get_unexpected_header_size(info->na_class);

    info->msg_exp_size_max = NA_Msg_get_max_expected_size(info->na_class);
    NA_TEST_CHECK_ERROR(info->msg_exp_size_max == 0, error, ret, NA_INVALID_ARG,
        "max expected msg size cannot be zero");
    info->msg_exp_header_size =
        NA_Msg_get_unexpected_header_size(info->na_class);

    /* Prepare Msg buffers */
    info->msg_unexp_buf =
        NA_Msg_buf_alloc(info->na_class, info->msg_unexp_size_max,
            (listen) ? NA_RECV : NA_SEND, &info->msg_unexp_data);
    NA_TEST_CHECK_ERROR(info->msg_unexp_buf == NULL, error, ret, NA_NOMEM,
        "NA_Msg_buf_alloc() failed");
    memset(info->msg_unexp_buf, 0, info->msg_unexp_size_max);

    if (!listen) {
        ret = NA_Msg_init_unexpected(
            info->na_class, info->msg_unexp_buf, info->msg_unexp_size_max);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_init_expected() failed (%s)",
            NA_Error_to_string(ret));
    }

    info->msg_exp_buf = NA_Msg_buf_alloc(info->na_class, info->msg_exp_size_max,
        (listen) ? NA_SEND : NA_RECV, &info->msg_exp_data);
    NA_TEST_CHECK_ERROR(info->msg_exp_buf == NULL, error, ret, NA_NOMEM,
        "NA_Msg_buf_alloc() failed");
    memset(info->msg_exp_buf, 0, info->msg_exp_size_max);

    if (listen) {
        ret = NA_Msg_init_expected(
            info->na_class, info->msg_exp_buf, info->msg_exp_size_max);
        NA_TEST_CHECK_NA_ERROR(error, ret,
            "NA_Msg_init_unexpected() failed (%s)", NA_Error_to_string(ret));
    }

    /* Create msg operation IDs */
    info->msg_unexp_op_id = NA_Op_create(info->na_class, NA_OP_SINGLE);
    NA_TEST_CHECK_ERROR(info->msg_unexp_op_id == NULL, error, ret, NA_NOMEM,
        "NA_Op_create() failed");
    info->msg_exp_op_id = NA_Op_create(info->na_class, NA_OP_SINGLE);
    NA_TEST_CHECK_ERROR(info->msg_exp_op_id == NULL, error, ret, NA_NOMEM,
        "NA_Op_create() failed");

    /* Create request */
    info->request = hg_request_create(info->request_class);
    NA_TEST_CHECK_ERROR(info->request == NULL, error, ret, NA_NOMEM,
        "hg_request_create() failed");

    return NA_SUCCESS;

error:
    na_test_common_class_cleanup(info);
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_test_common_class_cleanup(struct na_test_common_class_info *info)
{
    if (info->msg_unexp_op_id != NULL)
        NA_Op_destroy(info->na_class, info->msg_unexp_op_id);

    if (info->msg_exp_op_id != NULL)
        NA_Op_destroy(info->na_class, info->msg_exp_op_id);

    if (info->msg_unexp_buf != NULL)
        NA_Msg_buf_free(
            info->na_class, info->msg_unexp_buf, info->msg_unexp_data);

    if (info->msg_exp_buf != NULL)
        NA_Msg_buf_free(info->na_class, info->msg_exp_buf, info->msg_exp_data);

    if (info->poll_fd > 0)
        hg_poll_remove(info->poll_set, info->poll_fd);

    if (info->poll_set != NULL)
        hg_poll_destroy(info->poll_set);

    if (info->request != NULL)
        hg_request_destroy(info->request);

    if (info->request_class != NULL)
        hg_request_finalize(info->request_class, NULL);

    if (info->context != NULL)
        NA_Context_destroy(info->na_class, info->context);
}

/*---------------------------------------------------------------------------*/
void
na_test_common_init_data(void *buf, size_t buf_size, size_t header_size)
{
    char *buf_ptr = (char *) buf + header_size;
    size_t data_size = buf_size - header_size;
    size_t i;

    for (i = 0; i < data_size; i++)
        buf_ptr[i] = (char) i;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_common_verify_data(const void *buf, size_t buf_size, size_t header_size)
{
    const char *buf_ptr = (const char *) buf + header_size;
    size_t data_size = buf_size - header_size;
    na_return_t ret;
    size_t i;

    for (i = 0; i < data_size; i++) {
        NA_TEST_CHECK_ERROR(buf_ptr[i] != (char) i, error, ret, NA_FAULT,
            "Error detected in bulk transfer, buf[%zu] = %d, "
            "was expecting %d!",
            i, buf_ptr[i], (char) i);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_common_send_finalize(
    struct na_test_common_class_info *info, na_addr_t *target_addr)
{
    na_return_t ret;

    /* Reset */
    hg_request_reset(info->request);

    /* Post one-way msg send */
    ret = NA_Msg_send_unexpected(info->na_class, info->context,
        na_test_common_request_complete, info->request, info->msg_unexp_buf,
        info->msg_unexp_header_size, info->msg_unexp_data, target_addr, 0,
        NA_TEST_COMMON_TAG_DONE, info->msg_unexp_op_id);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_send_unexpected() failed (%s)",
        NA_Error_to_string(ret));

    hg_request_wait(info->request, NA_MAX_IDLE_TIME, NULL);

    return NA_SUCCESS;

error:
    return ret;
}
