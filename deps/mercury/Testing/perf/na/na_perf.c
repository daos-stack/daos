/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_perf.h"

#include "mercury_mem.h"

/****************/
/* Local Macros */
/****************/

/* Default RMA size max if not specified */
#define NA_PERF_RMA_SIZE_MAX (1 << 24)

/* Default RMA count if not specified */
#define NA_PERF_RMA_COUNT (64)

#define STRING(s)  #s
#define XSTRING(s) STRING(s)
#define VERSION_NAME                                                           \
    XSTRING(NA_VERSION_MAJOR)                                                  \
    "." XSTRING(NA_VERSION_MINOR) "." XSTRING(NA_VERSION_PATCH)

#define NDIGITS 2
#define NWIDTH  24

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

/*******************/
/* Local Variables */
/*******************/

na_return_t
na_perf_request_wait(struct na_perf_info *info,
    struct na_perf_request_info *request_info, unsigned int timeout_ms,
    unsigned int *completed_p)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    bool completed = false;
    na_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        unsigned int count = 0, actual_count = 0;

        if (info->poll_set && NA_Poll_try_wait(info->na_class, info->context)) {
            struct hg_poll_event poll_event = {.events = 0, .data.ptr = NULL};
            unsigned int actual_events = 0;
            int rc;

            NA_TEST_LOG_DEBUG("Waiting for %u ms",
                hg_time_to_ms(hg_time_subtract(deadline, now)));

            rc = hg_poll_wait(info->poll_set,
                hg_time_to_ms(hg_time_subtract(deadline, now)), 1, &poll_event,
                &actual_events);
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

        if (hg_atomic_get32(&request_info->completed)) {
            completed = true;
            break;
        }

        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
    } while (hg_time_less(now, deadline));

    if (completed_p != NULL)
        *completed_p = completed;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_perf_request_complete(const struct na_cb_info *na_cb_info)
{
    struct na_perf_request_info *info =
        (struct na_perf_request_info *) na_cb_info->arg;

    if ((++info->complete_count) == info->expected_count)
        hg_atomic_set32(&info->completed, (int32_t) true);
}

/*---------------------------------------------------------------------------*/
na_return_t
na_perf_init(int argc, char *argv[], bool listen, struct na_perf_info *info)
{
    size_t page_size = (size_t) hg_mem_get_page_size();
    bool multi_recv;
    na_return_t ret;
    size_t i;

    /* Initialize the interface */
    memset(info, 0, sizeof(*info));
    if (listen)
        info->na_test_info.listen = true;
    ret = NA_Test_init(argc, argv, &info->na_test_info);
    NA_TEST_CHECK_NA_ERROR(
        error, ret, "NA_Test_init() failed (%s)", NA_Error_to_string(ret));
    info->na_class = info->na_test_info.na_class;

    /* Parallel is not supported */
    NA_TEST_CHECK_ERROR(info->na_test_info.mpi_info.size > 1, error, ret,
        NA_OPNOTSUPPORTED, "Not a parallel test");

    /* Multi-recv */
    multi_recv =
        (listen && NA_Has_opt_feature(info->na_class, NA_OPT_MULTI_RECV) &&
            !info->na_test_info.no_multi_recv);

    /* Set up */
    info->context = NA_Context_create(info->na_test_info.na_class);
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

    /* Lookup target addr */
    if (!listen) {
        ret = NA_Addr_lookup(
            info->na_class, info->na_test_info.target_name, &info->target_addr);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Addr_lookup(%s) failed (%s)",
            info->na_test_info.target_name, NA_Error_to_string(ret));
    }

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

    info->rma_size_min = info->na_test_info.buf_size_min;
    if (info->rma_size_min == 0)
        info->rma_size_min = 1;

    info->rma_size_max = info->na_test_info.buf_size_max;
    if (info->rma_size_max == 0)
        info->rma_size_max = NA_PERF_RMA_SIZE_MAX;

    info->rma_count = info->na_test_info.buf_count;
    if (info->rma_count == 0)
        info->rma_count = NA_PERF_RMA_COUNT;

    /* Check that sizes are power of 2 */
    NA_TEST_CHECK_ERROR(!powerof2(info->rma_size_min), error, ret,
        NA_INVALID_ARG, "RMA size min must be a power of 2 (%zu)",
        info->rma_size_min);
    NA_TEST_CHECK_ERROR(!powerof2(info->rma_size_max), error, ret,
        NA_INVALID_ARG, "RMA size max must be a power of 2 (%zu)",
        info->rma_size_max);

    /* Prepare Msg buffers */
    if (multi_recv) {
        size_t hugepage_size = (size_t) hg_mem_get_hugepage_size();

        /* Try to use hugepages */
        if (hugepage_size > 0)
            info->msg_unexp_size_max = hugepage_size;
        else
            info->msg_unexp_size_max *= 16;
        info->msg_unexp_buf = NA_Msg_buf_alloc(info->na_class,
            info->msg_unexp_size_max, NA_MULTI_RECV, &info->msg_unexp_data);
        NA_TEST_CHECK_ERROR(info->msg_unexp_buf == NULL, error, ret, NA_NOMEM,
            "NA_Msg_buf_alloc() failed");
        memset(info->msg_unexp_buf, 0, info->msg_unexp_size_max);
    } else {
        info->msg_unexp_buf =
            NA_Msg_buf_alloc(info->na_class, info->msg_unexp_size_max,
                (listen) ? NA_RECV : NA_SEND, &info->msg_unexp_data);
        NA_TEST_CHECK_ERROR(info->msg_unexp_buf == NULL, error, ret, NA_NOMEM,
            "NA_Msg_buf_alloc() failed");
        memset(info->msg_unexp_buf, 0, info->msg_unexp_size_max);
    }

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

    /* Prepare RMA buf */
    info->rma_buf =
        hg_mem_aligned_alloc(page_size, info->rma_size_max * info->rma_count);
    NA_TEST_CHECK_ERROR(info->rma_buf == NULL, error, ret, NA_NOMEM,
        "hg_mem_aligned_alloc(%zu, %zu) failed", page_size, info->rma_size_max);
    memset(info->rma_buf, 0, info->rma_size_max * info->rma_count);

    if (!info->na_test_info.force_register || listen) {
        ret = NA_Mem_handle_create(info->na_class, info->rma_buf,
            info->rma_size_max * info->rma_count, NA_MEM_READWRITE,
            &info->local_handle);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_handle_create() failed (%s)",
            NA_Error_to_string(ret));

        ret = NA_Mem_register(
            info->na_class, info->local_handle, NA_MEM_TYPE_HOST, 0);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_register() failed (%s)",
            NA_Error_to_string(ret));
    }

    if (info->na_test_info.verify) {
        info->verify_buf = hg_mem_aligned_alloc(
            page_size, info->rma_size_max * info->rma_count);
        NA_TEST_CHECK_ERROR(info->verify_buf == NULL, error, ret, NA_NOMEM,
            "hg_mem_aligned_alloc(%zu, %zu) failed", page_size,
            info->rma_size_max);
        memset(info->verify_buf, 0, info->rma_size_max * info->rma_count);

        ret = NA_Mem_handle_create(info->na_class, info->verify_buf,
            info->rma_size_max * info->rma_count, NA_MEM_READWRITE,
            &info->verify_handle);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_handle_create() failed (%s)",
            NA_Error_to_string(ret));

        ret = NA_Mem_register(
            info->na_class, info->verify_handle, NA_MEM_TYPE_HOST, 0);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_register() failed (%s)",
            NA_Error_to_string(ret));
    }

    /* Create msg operation IDs */
    info->msg_unexp_op_id =
        NA_Op_create(info->na_class, (multi_recv) ? NA_OP_MULTI : NA_OP_SINGLE);
    NA_TEST_CHECK_ERROR(info->msg_unexp_op_id == NULL, error, ret, NA_NOMEM,
        "NA_Op_create() failed");
    info->msg_exp_op_id = NA_Op_create(info->na_class, NA_OP_SINGLE);
    NA_TEST_CHECK_ERROR(info->msg_exp_op_id == NULL, error, ret, NA_NOMEM,
        "NA_Op_create() failed");

    /* Create RMA operation IDs */
    info->rma_op_ids =
        (na_op_id_t **) malloc(sizeof(na_op_id_t *) * info->rma_count);
    NA_TEST_CHECK_ERROR(info->rma_op_ids == NULL, error, ret, NA_NOMEM,
        "Could not allocate RMA op IDs");
    for (i = 0; i < info->rma_count; i++)
        info->rma_op_ids[i] = NULL;

    for (i = 0; i < info->rma_count; i++) {
        info->rma_op_ids[i] = NA_Op_create(info->na_class, NA_OP_SINGLE);
        NA_TEST_CHECK_ERROR(info->rma_op_ids[i] == NULL, error, ret, NA_NOMEM,
            "NA_Op_create() failed");
    }

    return NA_SUCCESS;

error:
    na_perf_cleanup(info);
    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_perf_cleanup(struct na_perf_info *info)
{
    if (info->msg_unexp_op_id != NULL)
        NA_Op_destroy(info->na_class, info->msg_unexp_op_id);

    if (info->msg_exp_op_id != NULL)
        NA_Op_destroy(info->na_class, info->msg_exp_op_id);

    if (info->rma_op_ids != NULL) {
        size_t i;

        for (i = 0; i < info->rma_count; i++)
            if (info->rma_op_ids[i] != NULL)
                NA_Op_destroy(info->na_class, info->rma_op_ids[i]);
        free(info->rma_op_ids);
    }

    if (info->msg_unexp_buf != NULL)
        NA_Msg_buf_free(
            info->na_class, info->msg_unexp_buf, info->msg_unexp_data);

    if (info->msg_exp_buf != NULL)
        NA_Msg_buf_free(info->na_class, info->msg_exp_buf, info->msg_exp_data);

    if (info->local_handle != NULL) {
        NA_Mem_deregister(info->na_class, info->local_handle);
        NA_Mem_handle_free(info->na_class, info->local_handle);
    }
    if (info->verify_handle != NULL) {
        NA_Mem_deregister(info->na_class, info->verify_handle);
        NA_Mem_handle_free(info->na_class, info->verify_handle);
    }
    if (info->remote_handle != NULL)
        NA_Mem_handle_free(info->na_class, info->remote_handle);
    hg_mem_aligned_free(info->rma_buf);
    hg_mem_aligned_free(info->verify_buf);

    if (info->target_addr != NULL)
        NA_Addr_free(info->na_class, info->target_addr);

    if (info->poll_fd > 0)
        hg_poll_remove(info->poll_set, info->poll_fd);

    if (info->poll_set != NULL)
        hg_poll_destroy(info->poll_set);

    if (info->context != NULL)
        NA_Context_destroy(info->na_class, info->context);

    NA_Test_finalize(&info->na_test_info);
}

/*---------------------------------------------------------------------------*/
void
na_perf_print_header_lat(
    const struct na_perf_info *info, const char *benchmark, size_t min_size)
{
    fprintf(stdout, "# %s v%s\n", benchmark, VERSION_NAME);
    fprintf(stdout, "# Loop %d times from size %zu to %zu byte(s)\n",
        info->na_test_info.loop, min_size, info->msg_unexp_size_max);
    if (info->na_test_info.verify)
        fprintf(stdout, "# WARNING verifying data, output will be slower\n");
    fprintf(stdout, "%-*s%*s\n", 10, "# Size", NWIDTH, "Avg Lat (us)");
    fflush(stdout);
}

/*---------------------------------------------------------------------------*/
void
na_perf_print_lat(const struct na_perf_info *info, size_t buf_size, hg_time_t t)
{
    double msg_lat;
    size_t loop = (size_t) info->na_test_info.loop;

    msg_lat = hg_time_to_double(t) * 1e6 / (double) (loop * 2);

    printf("%-*zu%*.*f\n", 10, buf_size, NWIDTH, NDIGITS, msg_lat);
}

/*---------------------------------------------------------------------------*/
void
na_perf_print_header_bw(const struct na_perf_info *info, const char *benchmark)
{
    const char *bw_label =
        (info->na_test_info.mbps) ? "Bandwidth (MB/s)" : "Bandwidth (MiB/s)";

    printf("# %s v%s\n", benchmark, VERSION_NAME);
    printf("# Loop %d times from size %zu to %zu byte(s), RMA count (%zu)\n",
        info->na_test_info.loop, info->rma_size_min, info->rma_size_max,
        info->rma_count);
    if (info->na_test_info.verify)
        printf("# WARNING verifying data, output will be slower\n");
    if (info->na_test_info.force_register) {
        printf("# WARNING forcing registration on every iteration\n");
        printf("%-*s%*s%*s%*s\n", 10, "# Size", NWIDTH, bw_label, NWIDTH,
            "Reg Time (us)", NWIDTH, "Dereg Time (us)");
    } else {
        printf("%-*s%*s%*s\n", 10, "# Size", NWIDTH, bw_label, NWIDTH,
            "Time (us)");
    }
    fflush(stdout);
}

/*---------------------------------------------------------------------------*/
void
na_perf_print_bw(const struct na_perf_info *info, size_t buf_size, hg_time_t t,
    hg_time_t t_reg, hg_time_t t_dereg)
{
    size_t loop = (size_t) info->na_test_info.loop,
           buf_count = (size_t) info->rma_count;
    double avg_bw;

    avg_bw = (double) (buf_size * loop * buf_count) / hg_time_to_double(t);

    if (info->na_test_info.mbps)
        avg_bw /= 1e6; /* MB/s, matches OSU benchmarks */
    else
        avg_bw /= (1024 * 1024); /* MiB/s */

    if (info->na_test_info.force_register) {
        double reg_time = hg_time_to_double(t_reg) * 1e6 / (double) loop;
        double dereg_time = hg_time_to_double(t_dereg) * 1e6 / (double) loop;

        printf("%-*zu%*.*f%*.*f%*.*f\n", 10, buf_size, NWIDTH, NDIGITS, avg_bw,
            NWIDTH, NDIGITS, reg_time, NWIDTH, NDIGITS, dereg_time);
    } else {
        double avg_time =
            hg_time_to_double(t) * 1e6 / (double) (loop * buf_count);

        printf("%-*zu%*.*f%*.*f\n", 10, buf_size, NWIDTH, NDIGITS, avg_bw,
            NWIDTH, NDIGITS, avg_time);
    }
}

/*---------------------------------------------------------------------------*/
void
na_perf_init_data(void *buf, size_t buf_size, size_t header_size)
{
    char *buf_ptr = (char *) buf + header_size;
    size_t data_size = buf_size - header_size;
    size_t i;

    for (i = 0; i < data_size; i++)
        buf_ptr[i] = (char) i;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_perf_verify_data(const void *buf, size_t buf_size, size_t header_size)
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
na_perf_mem_handle_send(
    struct na_perf_info *info, na_addr_t *src_addr, na_tag_t tag)
{
    na_return_t ret;

    /* Serialize local handle */
    ret = NA_Mem_handle_serialize(info->na_class, info->msg_exp_buf,
        info->msg_exp_size_max, info->local_handle);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Mem_handle_serialize() failed (%s)",
        NA_Error_to_string(ret));

    /* Send the serialized handle */
    ret = NA_Msg_send_expected(info->na_class, info->context, NULL, NULL,
        info->msg_exp_buf, info->msg_exp_size_max, info->msg_exp_data, src_addr,
        0, tag, info->msg_exp_op_id);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_send_expected() failed (%s)",
        NA_Error_to_string(ret));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_perf_mem_handle_recv(struct na_perf_info *info, na_tag_t tag)
{
    struct na_perf_request_info request_info = {
        .completed = HG_ATOMIC_VAR_INIT(0),
        .complete_count = 0,
        .expected_count = (int32_t) 2};
    na_return_t ret;

    /* Post recv */
    ret = NA_Msg_recv_expected(info->na_class, info->context,
        na_perf_request_complete, &request_info, info->msg_exp_buf,
        info->msg_exp_size_max, info->msg_exp_data, info->target_addr, 0, tag,
        info->msg_exp_op_id);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_recv_expected() failed (%s)",
        NA_Error_to_string(ret));

    /* Ask server to send its handle */
    ret = NA_Msg_send_unexpected(info->na_class, info->context,
        na_perf_request_complete, &request_info, info->msg_unexp_buf,
        info->msg_unexp_header_size, info->msg_unexp_data, info->target_addr, 0,
        tag, info->msg_unexp_op_id);
    NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Msg_send_unexpected() failed (%s)",
        NA_Error_to_string(ret));

    /* Wait for completion */
    ret = na_perf_request_wait(info, &request_info, NA_MAX_IDLE_TIME, NULL);
    NA_TEST_CHECK_NA_ERROR(error, ret, "na_perf_request_wait() failed (%s)",
        NA_Error_to_string(ret));

    /* Retrieve handle */
    ret = NA_Mem_handle_deserialize(info->na_class, &info->remote_handle,
        info->msg_exp_buf, info->msg_exp_size_max);
    NA_TEST_CHECK_NA_ERROR(error, ret,
        "NA_Mem_handle_deserialize() failed (%s)", NA_Error_to_string(ret));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_perf_send_finalize(struct na_perf_info *info)
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
        NA_PERF_TAG_DONE, info->msg_unexp_op_id);
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
