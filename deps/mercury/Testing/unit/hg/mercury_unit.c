/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_unit.h"

#include "mercury_bulk.h"
#include "mercury_macros.h"

#include "mercury_rpc_cb.h"

#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Wait max 5s */
#define HG_TEST_TIMEOUT_MAX (5000)

/* Max handle default */
#define HG_TEST_HANDLE_MAX (32)

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static int
hg_test_request_progress(unsigned int timeout, void *arg);

static int
hg_test_request_trigger(unsigned int timeout, unsigned int *flag, void *arg);

static hg_return_t
hg_test_handle_create_cb(hg_handle_t handle, void *arg);

static hg_return_t
hg_test_finalize_rpc(struct hg_unit_info *info, hg_uint8_t target_id);

static hg_return_t
hg_test_finalize_rpc_cb(const struct hg_cb_info *callback_info);

static hg_return_t
hg_test_finalize_cb(hg_handle_t handle);

static void
hg_test_register(hg_class_t *hg_class);

/*******************/
/* Local Variables */
/*******************/

/* test_rpc */
hg_id_t hg_test_rpc_null_id_g = 0;
hg_id_t hg_test_rpc_open_id_g = 0;
hg_id_t hg_test_rpc_open_id_no_resp_g = 0;
hg_id_t hg_test_overflow_id_g = 0;
hg_id_t hg_test_cancel_rpc_id_g = 0;

/* test_bulk */
hg_id_t hg_test_bulk_write_id_g = 0;
hg_id_t hg_test_bulk_bind_write_id_g = 0;
hg_id_t hg_test_bulk_bind_forward_id_g = 0;

/* test_kill */
hg_id_t hg_test_killed_rpc_id_g = 0;

/* test_nested */
hg_id_t hg_test_nested1_id_g = 0;
hg_id_t hg_test_nested2_id_g = 0;

/* test_finalize */
static hg_id_t hg_test_finalize_id_g = 0;

/*---------------------------------------------------------------------------*/
static int
hg_test_request_progress(unsigned int timeout, void *arg)
{
    if (HG_Progress((hg_context_t *) arg, timeout) != HG_SUCCESS)
        return HG_UTIL_FAIL;

    return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static int
hg_test_request_trigger(unsigned int timeout, unsigned int *flag, void *arg)
{
    unsigned int count = 0;

    if (HG_Trigger((hg_context_t *) arg, timeout, 1, &count) != HG_SUCCESS)
        return HG_UTIL_FAIL;

    if (flag)
        *flag = (count > 0) ? true : false;

    return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_handle_create_cb(hg_handle_t handle, void *arg)
{
    struct hg_test_handle_info *hg_test_handle_info;
    hg_return_t ret = HG_SUCCESS;

    hg_test_handle_info = malloc(sizeof(struct hg_test_handle_info));
    HG_TEST_CHECK_ERROR(hg_test_handle_info == NULL, done, ret, HG_NOMEM_ERROR,
        "Could not allocate hg_test_handle_info");
    memset(hg_test_handle_info, 0, sizeof(struct hg_test_handle_info));

    (void) arg;
    HG_Set_data(handle, hg_test_handle_info, free);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_finalize_rpc(struct hg_unit_info *info, hg_uint8_t target_id)
{
    hg_return_t ret;
    unsigned int completed;

    hg_request_reset(info->request);

    ret = HG_Reset(info->handles[0], info->target_addr, hg_test_finalize_id_g);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    /* Set target ID */
    ret = HG_Set_target_id(info->handles[0], target_id);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Set_target_id() failed (%s)", HG_Error_to_string(ret));

    /* Forward call to target addr */
    ret = HG_Forward(
        info->handles[0], hg_test_finalize_rpc_cb, info->request, NULL);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    hg_request_wait(info->request, HG_TEST_TIMEOUT_MAX, &completed);
    if (!completed) {
        HG_TEST_LOG_WARNING("Canceling finalize, no response from server");

        ret = HG_Cancel(info->handles[0]);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Cancel() failed (%s)", HG_Error_to_string(ret));

        hg_request_wait(info->request, HG_TEST_TIMEOUT_MAX, &completed);
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_finalize_rpc_cb(const struct hg_cb_info *callback_info)
{
    hg_request_complete((hg_request_t *) callback_info->arg);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_finalize_cb(hg_handle_t handle)
{
    struct hg_test_context_info *hg_test_context_info =
        (struct hg_test_context_info *) HG_Context_get_data(
            HG_Get_info(handle)->context);
    hg_return_t ret = HG_SUCCESS;

    /* Set finalize for context data */
    hg_atomic_set32(&hg_test_context_info->finalizing, 1);

    /* Free handle and send response back */
    ret = HG_Respond(handle, NULL, NULL, NULL);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Respond() failed (%s)", HG_Error_to_string(ret));

done:
    ret = HG_Destroy(handle);
    HG_TEST_CHECK_ERROR_DONE(
        ret != HG_SUCCESS, "HG_Destroy() failed (%s)", HG_Error_to_string(ret));

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_test_register(hg_class_t *hg_class)
{
    /* test_rpc */
    hg_test_rpc_null_id_g = MERCURY_REGISTER(
        hg_class, "hg_test_rpc_null", void, void, hg_test_rpc_null_cb);
    hg_test_rpc_open_id_g = MERCURY_REGISTER(hg_class, "hg_test_rpc_open",
        rpc_open_in_t, rpc_open_out_t, hg_test_rpc_open_cb);
    hg_test_rpc_open_id_no_resp_g =
        MERCURY_REGISTER(hg_class, "hg_test_rpc_open_no_resp", rpc_open_in_t,
            rpc_open_out_t, hg_test_rpc_open_no_resp_cb);

    /* Disable response */
    HG_Registered_disable_response(
        hg_class, hg_test_rpc_open_id_no_resp_g, HG_TRUE);

    hg_test_overflow_id_g = MERCURY_REGISTER(hg_class, "hg_test_overflow", void,
        overflow_out_t, hg_test_overflow_cb);
    hg_test_cancel_rpc_id_g = MERCURY_REGISTER(
        hg_class, "hg_test_cancel_rpc", void, void, hg_test_cancel_rpc_cb);

    /* test_bulk */
    hg_test_bulk_write_id_g = MERCURY_REGISTER(hg_class, "hg_test_bulk_write",
        bulk_write_in_t, bulk_write_out_t, hg_test_bulk_write_cb);
    hg_test_bulk_bind_write_id_g =
        MERCURY_REGISTER(hg_class, "hg_test_bulk_bind_write", bulk_write_in_t,
            bulk_write_out_t, hg_test_bulk_bind_write_cb);
    hg_test_bulk_bind_forward_id_g =
        MERCURY_REGISTER(hg_class, "hg_test_bulk_bind_forward", bulk_write_in_t,
            bulk_write_out_t, hg_test_bulk_bind_forward_cb);

    /* test_kill */
    hg_test_killed_rpc_id_g = MERCURY_REGISTER(
        hg_class, "hg_test_killed_rpc", void, void, hg_test_killed_rpc_cb);

    /* test_nested */
    //    hg_test_nested1_id_g = MERCURY_REGISTER(hg_class, "hg_test_nested",
    //            void, void, hg_test_nested1_cb);
    //    hg_test_nested2_id_g = MERCURY_REGISTER(hg_class,
    //    "hg_test_nested_forward",
    //            void, void, hg_test_nested2_cb);

    /* test_finalize */
    hg_test_finalize_id_g = MERCURY_REGISTER(
        hg_class, "hg_test_finalize", void, void, hg_test_finalize_cb);
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_unit_init(int argc, char *argv[], bool listen, struct hg_unit_info *info)
{
    struct hg_test_context_info *context_info = NULL;
    hg_return_t ret;

    /* Initialize the interface */
    memset(info, 0, sizeof(*info));
    if (listen)
        info->hg_test_info.na_test_info.listen = true;
    info->hg_test_info.na_test_info.use_threads = true;
    ret = HG_Test_init(argc, argv, &info->hg_test_info);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Test_init() failed (%s)", HG_Error_to_string(ret));

    /* Set buf size min / max */
    info->buf_size_max = (info->hg_test_info.na_test_info.buf_size_max == 0)
                             ? (1 << 20)
                             : info->hg_test_info.na_test_info.buf_size_max;

    /* Init HG with init options */
    info->hg_class = info->hg_test_info.hg_class;

    /* Attach test info to class */
    ret = HG_Class_set_data(info->hg_class, info, NULL);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Class_set_data() failed (%s)", HG_Error_to_string(ret));

    /* Attach handle created */
    ret = HG_Class_set_handle_create_callback(
        info->hg_class, hg_test_handle_create_cb, info->hg_class);
    HG_TEST_CHECK_HG_ERROR(error, ret,
        "HG_Class_set_handle_create_callback() failed (%s)",
        HG_Error_to_string(ret));

    /* Set header */
    /*
    HG_Class_set_input_offset(hg_test_info->hg_class, sizeof(hg_uint64_t));
    HG_Class_set_output_offset(hg_test_info->hg_class, sizeof(hg_uint64_t));
    */

    /* Create primary context */
    info->context = HG_Context_create(info->hg_class);
    HG_TEST_CHECK_ERROR(info->context == NULL, error, ret, HG_FAULT,
        "Could not create HG context");

    /* Create additional contexts (do not exceed total max contexts) */
    if (info->hg_test_info.na_test_info.max_contexts > 1) {
        hg_uint8_t secondary_contexts_count =
            (hg_uint8_t) (info->hg_test_info.na_test_info.max_contexts - 1);
        hg_uint8_t i;

        info->secondary_contexts =
            malloc(secondary_contexts_count * sizeof(hg_context_t *));
        HG_TEST_CHECK_ERROR(info->secondary_contexts == NULL, error, ret,
            HG_NOMEM, "Could not allocate secondary contexts");
        for (i = 0; i < secondary_contexts_count; i++) {
            hg_uint8_t context_id = (hg_uint8_t) (i + 1);
            info->secondary_contexts[i] =
                HG_Context_create_id(info->hg_class, context_id);
            HG_TEST_CHECK_ERROR(info->secondary_contexts[i] == NULL, error, ret,
                HG_FAULT, "HG_Context_create_id() failed");

            /* Attach context info to context */
            context_info = malloc(sizeof(*context_info));
            HG_TEST_CHECK_ERROR(context_info == NULL, error, ret, HG_NOMEM,
                "Could not allocate HG test context info");

            hg_atomic_init32(&context_info->finalizing, 0);
            ret = HG_Context_set_data(
                info->secondary_contexts[i], context_info, free);
            HG_TEST_CHECK_HG_ERROR(error, ret,
                "HG_Context_set_data() failed (%s)", HG_Error_to_string(ret));
        }
    }

    /* Create request class */
    info->request_class = hg_request_init(
        hg_test_request_progress, hg_test_request_trigger, info->context);
    HG_TEST_CHECK_ERROR(info->request_class == NULL, error, ret, HG_FAULT,
        "Could not create request class");

    /* Attach context info to context */
    context_info = malloc(sizeof(*context_info));
    HG_TEST_CHECK_ERROR(context_info == NULL, error, ret, HG_NOMEM,
        "Could not allocate HG test context info");

    hg_atomic_init32(&context_info->finalizing, 0);
    ret = HG_Context_set_data(info->context, context_info, free);
    HG_TEST_CHECK_HG_ERROR(error, ret, "HG_Context_set_data() failed (%s)",
        HG_Error_to_string(ret));

    /* Register routines */
    hg_test_register(info->hg_class);

    if (listen || info->hg_test_info.na_test_info.self_send) {
        /* Make sure that thread count is at least max_contexts */
        if (info->hg_test_info.thread_count <
            info->hg_test_info.na_test_info.max_contexts)
            info->hg_test_info.thread_count =
                info->hg_test_info.na_test_info.max_contexts;

        /* Create thread pool */
        hg_thread_pool_init(
            info->hg_test_info.thread_count, &info->thread_pool);
        printf("# Starting server with %d threads...\n",
            info->hg_test_info.thread_count);
    }

    if (!listen) {
        unsigned int i;

        if (info->hg_test_info.na_test_info.self_send) {
            /* Self addr is target */
            ret = HG_Addr_self(info->hg_class, &info->target_addr);
            HG_TEST_CHECK_HG_ERROR(error, ret, "HG_Addr_self() failed (%s)",
                HG_Error_to_string(ret));
        } else {
            /* Forward call to remote addr and get a new request */
            ret = HG_Addr_lookup2(info->hg_class,
                info->hg_test_info.na_test_info.target_name,
                &info->target_addr);
            HG_TEST_CHECK_HG_ERROR(error, ret, "HG_Addr_lookup() failed (%s)",
                HG_Error_to_string(ret));
        }

        /* Create handles */
        info->handle_max = info->hg_test_info.handle_max;
        if (info->handle_max == 0)
            info->handle_max = HG_TEST_HANDLE_MAX;

        info->handles = malloc(info->handle_max * sizeof(hg_handle_t));
        HG_TEST_CHECK_ERROR(info->handles == NULL, error, ret, HG_NOMEM,
            "Could not allocate array of %zu handles", info->handle_max);

        for (i = 0; i < info->handle_max; i++) {
            ret = HG_Create(
                info->context, info->target_addr, 0, &info->handles[i]);
            HG_TEST_CHECK_HG_ERROR(
                error, ret, "HG_Create() failed (%s)", HG_Error_to_string(ret));
        }

        info->request = hg_request_create(info->request_class);
        HG_TEST_CHECK_ERROR(info->request == NULL, error, ret, HG_NOMEM,
            "hg_request_create() failed");
    }

    return HG_SUCCESS;

error:
    hg_unit_cleanup(info);

    return ret;
}

/*---------------------------------------------------------------------------*/
void
hg_unit_cleanup(struct hg_unit_info *info)
{
    uint8_t max_contexts = info->hg_test_info.na_test_info.max_contexts;
    hg_return_t ret;

    NA_Test_barrier(&info->hg_test_info.na_test_info);

    /* Client sends request to terminate server */
    if (!info->hg_test_info.na_test_info.listen) {
        if (info->hg_test_info.na_test_info.mpi_info.rank == 0) {
            hg_uint8_t i, context_count = max_contexts ? max_contexts : 1;
            for (i = 0; i < context_count; i++)
                hg_test_finalize_rpc(info, i);
        }
    }

    NA_Test_barrier(&info->hg_test_info.na_test_info);

    if (info->handles != NULL) {
        size_t i;
        for (i = 0; i < info->handle_max; i++)
            HG_Destroy(info->handles[i]);

        free(info->handles);
    }

    /* Free target addr */
    if (info->target_addr != HG_ADDR_NULL) {
        ret = HG_Addr_free(info->hg_class, info->target_addr);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Addr_free() failed (%s)", HG_Error_to_string(ret));
        info->target_addr = HG_ADDR_NULL;
    }

    if (info->request != NULL)
        hg_request_destroy(info->request);

    if (info->request_class) {
        hg_request_finalize(info->request_class, NULL);
        info->request_class = NULL;
    }

    ret = HG_Context_unpost(info->context);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Context_unpost() failed (%s)", HG_Error_to_string(ret));

    /* Make sure we triggered everything */
    do {
        unsigned int actual_count;

        do {
            ret = HG_Trigger(info->context, 0, 1, &actual_count);
        } while ((ret == HG_SUCCESS) && actual_count);
        HG_TEST_CHECK_ERROR(ret != HG_SUCCESS && ret != HG_TIMEOUT, done, ret,
            ret, "Could not trigger callback (%s)", HG_Error_to_string(ret));

        ret = HG_Progress(info->context, 100);
    } while (ret == HG_SUCCESS);
    HG_TEST_CHECK_ERROR(ret != HG_SUCCESS && ret != HG_TIMEOUT, done, ret, ret,
        "HG_Progress failed (%s)", HG_Error_to_string(ret));

    if (info->thread_pool) {
        hg_thread_pool_destroy(info->thread_pool);
        info->thread_pool = NULL;
    }

    /* Destroy secondary contexts */
    if (info->secondary_contexts) {
        hg_uint8_t secondary_contexts_count = (hg_uint8_t) (max_contexts - 1);
        hg_uint8_t i;

        for (i = 0; i < secondary_contexts_count; i++) {
            ret = HG_Context_destroy(info->secondary_contexts[i]);
            HG_TEST_CHECK_HG_ERROR(done, ret,
                "HG_Context_destroy() failed (%s)", HG_Error_to_string(ret));
        }
        free(info->secondary_contexts);
        info->secondary_contexts = NULL;
    }

    /* Destroy context */
    if (info->context) {
        ret = HG_Context_destroy(info->context);
        HG_TEST_CHECK_HG_ERROR(done, ret, "HG_Context_destroy() failed (%s)",
            HG_Error_to_string(ret));
        info->context = NULL;
    }

    /* Finalize interface */
    (void) HG_Test_finalize(&info->hg_test_info);

done:
    return;
}
