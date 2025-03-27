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

/* Wait timeout in ms */
#define HG_TEST_WAIT_TIMEOUT (HG_TEST_TIMEOUT * 1000)

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct forward_cb_args {
    hg_request_t *request;
    rpc_handle_t *rpc_handle;
    hg_return_t ret;
    bool no_entry;
};

struct forward_multi_cb_args {
    rpc_handle_t *rpc_handle;
    hg_return_t *rets;
    hg_thread_mutex_t mutex;
    int32_t expected_count; /* Expected count */
    int32_t complete_count; /* Completed count */
    hg_request_t *request;  /* Request */
};

struct forward_no_req_cb_args {
    hg_atomic_int32_t done;
    rpc_handle_t *rpc_handle;
    hg_return_t ret;
};

struct hg_test_multi_thread {
    struct hg_unit_info *info;
    hg_thread_t thread;
    unsigned int thread_id;
    hg_return_t ret;
};

/********************/
/* Local Prototypes */
/********************/

static hg_return_t
hg_test_rpc_no_input(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request);

static hg_return_t
hg_test_rpc_input(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request);

static hg_return_t
hg_test_rpc_inv(
    hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id, hg_request_t *request);

static hg_return_t
hg_test_rpc_output_cb(const struct hg_cb_info *callback_info);

static hg_return_t
hg_test_rpc_no_output_cb(const struct hg_cb_info *callback_info);

#ifndef HG_HAS_XDR
static hg_return_t
hg_test_rpc_output_overflow_cb(const struct hg_cb_info *callback_info);
#endif

static hg_return_t
hg_test_rpc_cancel(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request);

static hg_return_t
hg_test_rpc_multi(hg_handle_t *handles, size_t handle_max, hg_addr_t addr,
    hg_uint8_t target_id, hg_id_t rpc_id, hg_cb_t callback,
    hg_request_t *request);

static hg_return_t
hg_test_rpc_multi_cb(const struct hg_cb_info *callback_info);

static hg_return_t
hg_test_rpc_launch_threads(struct hg_unit_info *info, hg_thread_func_t func);

static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_thread(void *arg);

static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_progress(void *arg);

static hg_return_t
hg_test_rpc_no_req(hg_context_t *context, hg_handle_t handle, hg_cb_t callback);

static hg_return_t
hg_test_rpc_no_req_cb(const struct hg_cb_info *callback_info);

static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_progress_create(void *arg);

static hg_return_t
hg_test_rpc_no_req_create(
    hg_context_t *context, hg_addr_t addr, hg_cb_t callback);

static hg_return_t
hg_test_rpc_no_req_create_cb(const struct hg_cb_info *callback_info);

/*******************/
/* Local Variables */
/*******************/

extern hg_id_t hg_test_rpc_null_id_g;
extern hg_id_t hg_test_rpc_open_id_g;
extern hg_id_t hg_test_rpc_open_id_no_resp_g;
extern hg_id_t hg_test_overflow_id_g;
extern hg_id_t hg_test_cancel_rpc_id_g;

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_input(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request)
{
    hg_return_t ret;
    struct forward_cb_args forward_cb_args = {.request = request,
        .rpc_handle = NULL,
        .ret = HG_SUCCESS,
        .no_entry = false};
    unsigned int flag;
    int rc;

    hg_request_reset(request);

    ret = HG_Reset(handle, addr, rpc_id);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    HG_TEST_LOG_DEBUG("Forwarding RPC, op id: %" PRIu64 "...", rpc_id);

    ret = HG_Forward(handle, callback, &forward_cb_args, NULL);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    rc = hg_request_wait(request, HG_TEST_WAIT_TIMEOUT, &flag);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "hg_request_wait() failed");

    HG_TEST_CHECK_ERROR(
        !flag, error, ret, HG_TIMEOUT, "hg_request_wait() timed out");
    ret = forward_cb_args.ret;
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "Error in HG callback (%s)", HG_Error_to_string(ret));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_input(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request)
{
    hg_return_t ret;
    rpc_handle_t rpc_open_handle = {.cookie = 100};
    struct forward_cb_args forward_cb_args = {.request = request,
        .rpc_handle = &rpc_open_handle,
        .ret = HG_SUCCESS,
        .no_entry = false};
    rpc_open_in_t in_struct = {
        .handle = rpc_open_handle, .path = HG_TEST_RPC_PATH};
    hg_size_t payload_size;
    size_t expected_string_payload_size =
        strlen(HG_TEST_RPC_PATH) + sizeof(uint64_t) + 3;
    unsigned int flag;
    int rc;

    hg_request_reset(request);

    ret = HG_Reset(handle, addr, rpc_id);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    /* Forward call to remote addr and get a new request */
    HG_TEST_LOG_DEBUG("Forwarding RPC, op id: %" PRIu64 "...", rpc_id);

    ret = HG_Forward(handle, callback, &forward_cb_args, &in_struct);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    rc = hg_request_wait(request, HG_TEST_WAIT_TIMEOUT, &flag);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "hg_request_wait() failed");

    HG_TEST_CHECK_ERROR(
        !flag, error, ret, HG_TIMEOUT, "hg_request_wait() timed out");
    ret = forward_cb_args.ret;
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "Error in HG callback (%s)", HG_Error_to_string(ret));

    payload_size = HG_Get_input_payload_size(handle);
    HG_TEST_CHECK_ERROR(
        payload_size != sizeof(rpc_handle_t) + expected_string_payload_size,
        error, ret, HG_FAULT,
        "invalid input payload size (%" PRId64 "), expected (%zu)",
        payload_size, sizeof(rpc_handle_t) + expected_string_payload_size);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_inv(
    hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id, hg_request_t *request)
{
    hg_return_t ret;
    struct forward_cb_args forward_cb_args = {.request = request,
        .rpc_handle = NULL,
        .ret = HG_SUCCESS,
        .no_entry = true};
    unsigned int flag;
    int rc;

    hg_request_reset(request);

    ret = HG_Reset(handle, addr, rpc_id);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    HG_TEST_LOG_DEBUG("Forwarding RPC, op id: %" PRIu64 "...", rpc_id);

    ret = HG_Forward(handle, hg_test_rpc_no_output_cb, &forward_cb_args, NULL);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    rc = hg_request_wait(request, HG_TEST_WAIT_TIMEOUT, &flag);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "hg_request_wait() failed");

    HG_TEST_CHECK_ERROR(
        !flag, error, ret, HG_TIMEOUT, "hg_request_wait() timed out");
    ret = forward_cb_args.ret;
    HG_TEST_CHECK_ERROR_NORET(ret != HG_NOENTRY, error,
        "Error in HG callback (%s)", HG_Error_to_string(ret));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_output_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_cb_args *args =
        (struct forward_cb_args *) callback_info->arg;
    int rpc_open_ret;
    int rpc_open_event_id;
    rpc_open_out_t rpc_open_out_struct;
    hg_return_t ret = callback_info->ret;
    hg_size_t payload_size = HG_Get_output_payload_size(handle);

    if (args->no_entry && ret == HG_NOENTRY)
        goto done;

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));
    HG_TEST_CHECK_ERROR(payload_size != sizeof(rpc_open_out_t), done, ret,
        HG_FAULT, "invalid output payload size (%" PRId64 "), expected (%zu)",
        payload_size, sizeof(rpc_open_out_t));

    /* Get output */
    ret = HG_Get_output(handle, &rpc_open_out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
    rpc_open_ret = rpc_open_out_struct.ret;
    rpc_open_event_id = rpc_open_out_struct.event_id;
    HG_TEST_LOG_DEBUG("rpc_open returned: %d with event_id: %d", rpc_open_ret,
        rpc_open_event_id);
    (void) rpc_open_ret;
    HG_TEST_CHECK_ERROR(rpc_open_event_id != (int) args->rpc_handle->cookie,
        free, ret, HG_FAULT, "Cookie did not match RPC response");

free:
    if (ret != HG_SUCCESS)
        (void) HG_Free_output(handle, &rpc_open_out_struct);
    else {
        /* Free output */
        ret = HG_Free_output(handle, &rpc_open_out_struct);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));
    }

done:
    args->ret = ret;

    hg_request_complete(args->request);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_output_cb(const struct hg_cb_info *callback_info)
{
    struct forward_cb_args *args =
        (struct forward_cb_args *) callback_info->arg;

    args->ret = callback_info->ret;

    hg_request_complete(args->request);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
#ifndef HG_HAS_XDR
static hg_return_t
hg_test_rpc_output_overflow_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_cb_args *args =
        (struct forward_cb_args *) callback_info->arg;
    overflow_out_t out_struct;
#    ifdef HG_HAS_DEBUG
    hg_string_t string;
    size_t string_len;
#    endif
    hg_return_t ret = callback_info->ret;
    hg_size_t payload_size = HG_Get_output_payload_size(handle);
    size_t expected_string_payload_size =
        HG_Class_get_output_eager_size(HG_Get_info(handle)->hg_class) * 2 + 3 +
        2 * sizeof(uint64_t);

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));
    HG_TEST_CHECK_ERROR(payload_size != expected_string_payload_size, done, ret,
        HG_FAULT, "invalid output payload size (%" PRId64 "), expected (%zu)",
        payload_size, expected_string_payload_size);

    /* Get output */
    ret = HG_Get_output(handle, &out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
#    ifdef HG_HAS_DEBUG
    string = out_struct.string;
    string_len = out_struct.string_len;
#    endif
    HG_TEST_LOG_DEBUG("Returned string (length %zu): %s", string_len, string);

    /* Free output */
    ret = HG_Free_output(handle, &out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));

done:
    args->ret = ret;

    hg_request_complete(args->request);

    return HG_SUCCESS;
}
#endif

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_cancel(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_request_t *request)
{
    hg_return_t ret;
    struct forward_cb_args forward_cb_args = {
        .request = request, .ret = HG_SUCCESS};
    unsigned int flag;
    int rc;

    hg_request_reset(request);

    ret = HG_Reset(handle, addr, rpc_id);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    HG_TEST_LOG_DEBUG("Forwarding RPC, op id: %" PRIu64 "...", rpc_id);

    ret = HG_Forward(handle, callback, &forward_cb_args, NULL);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    /* Cancel request before making progress, this ensures that the RPC has
     * not completed yet. */
    ret = HG_Cancel(handle);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Cancel() failed (%s)", HG_Error_to_string(ret));

    rc = hg_request_wait(request, HG_TEST_WAIT_TIMEOUT, &flag);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "hg_request_wait() failed");

    HG_TEST_CHECK_ERROR(
        !flag, error, ret, HG_TIMEOUT, "hg_request_wait() timed out");
    ret = forward_cb_args.ret;
    HG_TEST_CHECK_ERROR_NORET(ret != HG_CANCELED, error,
        "Error in HG callback (%s)", HG_Error_to_string(ret));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_multi(hg_handle_t *handles, size_t handle_max, hg_addr_t addr,
    hg_uint8_t target_id, hg_id_t rpc_id, hg_cb_t callback,
    hg_request_t *request)
{
    hg_return_t ret;
    rpc_handle_t rpc_open_handle = {.cookie = 100};
    struct forward_multi_cb_args forward_multi_cb_args = {
        .rpc_handle = &rpc_open_handle,
        .request = request,
        .rets = NULL,
        .mutex = HG_THREAD_MUTEX_INITIALIZER,
        .complete_count = 0,
        .expected_count = (int32_t) handle_max};
    rpc_open_in_t in_struct = {
        .handle = rpc_open_handle, .path = HG_TEST_RPC_PATH};
    size_t i;
    unsigned int flag;
    int rc;

    HG_TEST_CHECK_ERROR(handle_max == 0, error, ret, HG_INVALID_PARAM,
        "Handle max cannot be 0");

    hg_request_reset(request);

    forward_multi_cb_args.rets =
        (hg_return_t *) calloc(handle_max, sizeof(hg_return_t));
    HG_TEST_CHECK_ERROR(forward_multi_cb_args.rets == NULL, error, ret,
        HG_NOMEM, "Could not allocate array of return values");

    /**
     * Forwarding multiple requests
     */
    HG_TEST_LOG_DEBUG("Creating %zu requests...", handle_max);
    for (i = 0; i < handle_max; i++) {
        ret = HG_Reset(handles[i], addr, rpc_id);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

        ret = HG_Set_target_id(handles[i], target_id);
        HG_TEST_CHECK_HG_ERROR(error, ret, "HG_Set_target_id() failed (%s)",
            HG_Error_to_string(ret));

        HG_TEST_LOG_DEBUG(
            " %zu Forwarding rpc_open, op id: %" PRIu64 "...", i, rpc_id);

        ret = HG_Forward(
            handles[i], callback, &forward_multi_cb_args, &in_struct);
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));
    }

    rc = hg_request_wait(request, HG_TEST_WAIT_TIMEOUT, &flag);
    HG_TEST_CHECK_ERROR(rc != HG_UTIL_SUCCESS, error, ret, HG_PROTOCOL_ERROR,
        "hg_request_wait() failed");

    HG_TEST_CHECK_ERROR(
        !flag, error, ret, HG_TIMEOUT, "hg_request_wait() timed out");

    for (i = 0; i < handle_max; i++) {
        ret = forward_multi_cb_args.rets[i];
        HG_TEST_CHECK_HG_ERROR(
            error, ret, "Error in HG callback (%s)", HG_Error_to_string(ret));
    }

    HG_TEST_LOG_DEBUG("Done");

    free(forward_multi_cb_args.rets);

    return HG_SUCCESS;

error:
    free(forward_multi_cb_args.rets);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_multi_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_multi_cb_args *args =
        (struct forward_multi_cb_args *) callback_info->arg;
    int rpc_open_ret;
    int rpc_open_event_id;
    rpc_open_out_t rpc_open_out_struct;
    hg_return_t ret = callback_info->ret;
    int32_t complete_count;

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));

    /* Get output */
    ret = HG_Get_output(handle, &rpc_open_out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
    rpc_open_ret = rpc_open_out_struct.ret;
    rpc_open_event_id = rpc_open_out_struct.event_id;
    HG_TEST_LOG_DEBUG("rpc_open returned: %d with event_id: %d", rpc_open_ret,
        rpc_open_event_id);
    (void) rpc_open_ret;
    HG_TEST_CHECK_ERROR(rpc_open_event_id != (int) args->rpc_handle->cookie,
        free, ret, HG_FAULT, "Cookie did not match RPC response");

free:
    if (ret != HG_SUCCESS)
        (void) HG_Free_output(handle, &rpc_open_out_struct);
    else {
        /* Free output */
        ret = HG_Free_output(handle, &rpc_open_out_struct);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));
    }

done:
    hg_thread_mutex_lock(&args->mutex);
    args->rets[args->complete_count] = ret;
    complete_count = ++args->complete_count;
    hg_thread_mutex_unlock(&args->mutex);
    if (complete_count == args->expected_count)
        hg_request_complete(args->request);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_launch_threads(struct hg_unit_info *info, hg_thread_func_t func)
{
    struct hg_test_multi_thread *thread_infos;
    unsigned int i;
    hg_return_t ret;
    int rc;

    thread_infos =
        malloc(info->hg_test_info.thread_count * sizeof(*thread_infos));
    HG_TEST_CHECK_ERROR(thread_infos == NULL, error, ret, HG_NOMEM,
        "Could not allocate thread array (%u)",
        info->hg_test_info.thread_count);

    for (i = 0; i < info->hg_test_info.thread_count; i++) {
        thread_infos[i].info = info;
        thread_infos[i].thread_id = i;

        rc = hg_thread_create(&thread_infos[i].thread, func, &thread_infos[i]);
        HG_TEST_CHECK_ERROR(
            rc != 0, error, ret, HG_NOMEM, "hg_thread_create() failed");
    }

    for (i = 0; i < info->hg_test_info.thread_count; i++) {
        rc = hg_thread_join(thread_infos[i].thread);
        HG_TEST_CHECK_ERROR(
            rc != 0, error, ret, HG_FAULT, "hg_thread_join() failed");
    }
    for (i = 0; i < info->hg_test_info.thread_count; i++)
        HG_TEST_CHECK_ERROR(thread_infos[i].ret != HG_SUCCESS, error, ret,
            thread_infos[i].ret, "Error from thread %u (%s)",
            thread_infos->thread_id, HG_Error_to_string(thread_infos[i].ret));

    free(thread_infos);

    return HG_SUCCESS;

error:
    free(thread_infos);

    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_thread(void *arg)
{
    struct hg_test_multi_thread *thread_arg =
        (struct hg_test_multi_thread *) arg;
    struct hg_unit_info *info = thread_arg->info;
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    size_t handle_max = info->handle_max / info->hg_test_info.thread_count;
    hg_handle_t *handles = &info->handles[thread_arg->thread_id * handle_max];
    hg_request_t *request;
    hg_return_t ret;

    request = hg_request_create(info->request_class);
    HG_TEST_CHECK_ERROR(
        request == NULL, done, ret, HG_NOMEM, "Could not create request");

    ret = hg_test_rpc_multi(handles,
        (thread_arg->thread_id < (info->hg_test_info.thread_count - 1))
            ? handle_max
            : info->handle_max - thread_arg->thread_id * handle_max,
        info->target_addr, 0, hg_test_rpc_open_id_g, hg_test_rpc_multi_cb,
        request);
    HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_rpc_multiple() failed (%s)",
        HG_Error_to_string(ret));

done:
    hg_request_destroy(request);
    thread_arg->ret = ret;

    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_progress(void *arg)
{
    struct hg_test_multi_thread *thread_arg =
        (struct hg_test_multi_thread *) arg;
    struct hg_unit_info *info = thread_arg->info;
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    hg_return_t ret;
    int i;

    HG_TEST_CHECK_ERROR(info->handle_max < thread_arg->thread_id, done, ret,
        HG_INVALID_PARAM, "Handle max is too low (%zu)", info->handle_max);

    ret = HG_Reset(info->handles[thread_arg->thread_id], info->target_addr,
        hg_test_rpc_open_id_g);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Reset() failed (%s)", HG_Error_to_string(ret));

    for (i = 0; i < 100; i++) {
        ret = hg_test_rpc_no_req(info->context,
            info->handles[thread_arg->thread_id], hg_test_rpc_no_req_cb);
        HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_rpc_no_req() failed (%s)",
            HG_Error_to_string(ret));
    }

done:
    thread_arg->ret = ret;

    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
hg_test_rpc_multi_progress_create(void *arg)
{
    struct hg_test_multi_thread *thread_arg =
        (struct hg_test_multi_thread *) arg;
    struct hg_unit_info *info = thread_arg->info;
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    hg_return_t ret;
    int i;

    HG_TEST_CHECK_ERROR(info->handle_max < thread_arg->thread_id, done, ret,
        HG_INVALID_PARAM, "Handle max is too low (%zu)", info->handle_max);

    for (i = 0; i < 100; i++) {
        ret = hg_test_rpc_no_req_create(
            info->context, info->target_addr, hg_test_rpc_no_req_create_cb);
        HG_TEST_CHECK_HG_ERROR(done, ret,
            "hg_test_rpc_no_req_create() failed (%s)", HG_Error_to_string(ret));
    }

done:
    thread_arg->ret = ret;

    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_req(hg_context_t *context, hg_handle_t handle, hg_cb_t callback)
{
    hg_return_t ret;
    rpc_handle_t rpc_open_handle = {.cookie = 100};
    struct forward_no_req_cb_args forward_cb_args = {
        .done = HG_ATOMIC_VAR_INIT(0),
        .rpc_handle = &rpc_open_handle,
        .ret = HG_SUCCESS};
    rpc_open_in_t in_struct = {
        .handle = rpc_open_handle, .path = HG_TEST_RPC_PATH};

    ret = HG_Forward(handle, callback, &forward_cb_args, &in_struct);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    do {
        unsigned int actual_count = 0;

        do {
            ret = HG_Trigger(context, 0, 100, &actual_count);
        } while ((ret == HG_SUCCESS) && actual_count);
        HG_TEST_CHECK_ERROR_NORET(ret != HG_SUCCESS && ret != HG_TIMEOUT, error,
            "HG_Trigger() failed (%s)", HG_Error_to_string(ret));

        if (hg_atomic_get32(&forward_cb_args.done))
            break;

        ret = HG_Progress(context, 0);
    } while (ret == HG_SUCCESS || ret == HG_TIMEOUT);
    HG_TEST_CHECK_ERROR_NORET(ret != HG_SUCCESS && ret != HG_TIMEOUT, error,
        "HG_Progress() failed (%s)", HG_Error_to_string(ret));

    ret = forward_cb_args.ret;
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "Error in HG callback (%s)", HG_Error_to_string(ret));

    return HG_SUCCESS;

error:

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_req_create(
    hg_context_t *context, hg_addr_t addr, hg_cb_t callback)
{
    hg_return_t ret;
    hg_handle_t handle;
    rpc_handle_t rpc_open_handle = {.cookie = 100};
    struct forward_no_req_cb_args forward_cb_args = {
        .done = HG_ATOMIC_VAR_INIT(0),
        .rpc_handle = &rpc_open_handle,
        .ret = HG_SUCCESS};
    rpc_open_in_t in_struct = {
        .handle = rpc_open_handle, .path = HG_TEST_RPC_PATH};

    ret = HG_Create(context, addr, hg_test_rpc_open_id_g, &handle);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Create() failed (%s)", HG_Error_to_string(ret));

    ret = HG_Forward(handle, callback, &forward_cb_args, &in_struct);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Forward() failed (%s)", HG_Error_to_string(ret));

    do {
        unsigned int actual_count = 0;

        do {
            ret = HG_Trigger(context, 0, 100, &actual_count);
        } while ((ret == HG_SUCCESS) && actual_count);
        HG_TEST_CHECK_ERROR_NORET(ret != HG_SUCCESS && ret != HG_TIMEOUT, error,
            "HG_Trigger() failed (%s)", HG_Error_to_string(ret));

        if (hg_atomic_get32(&forward_cb_args.done))
            break;

        ret = HG_Progress(context, 0);
    } while (ret == HG_SUCCESS || ret == HG_TIMEOUT);
    HG_TEST_CHECK_ERROR_NORET(ret != HG_SUCCESS && ret != HG_TIMEOUT, error,
        "HG_Progress() failed (%s)", HG_Error_to_string(ret));

    ret = forward_cb_args.ret;
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "Error in HG callback (%s)", HG_Error_to_string(ret));

    return HG_SUCCESS;

error:

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_req_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_no_req_cb_args *args =
        (struct forward_no_req_cb_args *) callback_info->arg;
    int rpc_open_ret;
    int rpc_open_event_id;
    rpc_open_out_t rpc_open_out_struct;
    hg_return_t ret = callback_info->ret;

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));

    /* Get output */
    ret = HG_Get_output(handle, &rpc_open_out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
    rpc_open_ret = rpc_open_out_struct.ret;
    rpc_open_event_id = rpc_open_out_struct.event_id;
    HG_TEST_LOG_DEBUG("rpc_open returned: %d with event_id: %d", rpc_open_ret,
        rpc_open_event_id);
    (void) rpc_open_ret;
    HG_TEST_CHECK_ERROR(rpc_open_event_id != (int) args->rpc_handle->cookie,
        free, ret, HG_FAULT, "Cookie did not match RPC response");

free:
    if (ret != HG_SUCCESS)
        (void) HG_Free_output(handle, &rpc_open_out_struct);
    else {
        /* Free output */
        ret = HG_Free_output(handle, &rpc_open_out_struct);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));
    }

done:
    args->ret = ret;
    hg_atomic_set32(&args->done, 1);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_rpc_no_req_create_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_no_req_cb_args *args =
        (struct forward_no_req_cb_args *) callback_info->arg;
    int rpc_open_ret;
    int rpc_open_event_id;
    rpc_open_out_t rpc_open_out_struct;
    hg_return_t ret = callback_info->ret;

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));

    /* Get output */
    ret = HG_Get_output(handle, &rpc_open_out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
    rpc_open_ret = rpc_open_out_struct.ret;
    rpc_open_event_id = rpc_open_out_struct.event_id;
    HG_TEST_LOG_DEBUG("rpc_open returned: %d with event_id: %d", rpc_open_ret,
        rpc_open_event_id);
    (void) rpc_open_ret;
    HG_TEST_CHECK_ERROR(rpc_open_event_id != (int) args->rpc_handle->cookie,
        free, ret, HG_FAULT, "Cookie did not match RPC response");

free:
    if (ret != HG_SUCCESS)
        (void) HG_Free_output(handle, &rpc_open_out_struct);
    else {
        /* Free output */
        ret = HG_Free_output(handle, &rpc_open_out_struct);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));
    }

    HG_Destroy(handle);

done:
    args->ret = ret;
    hg_atomic_set32(&args->done, 1);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct hg_unit_info info;
    hg_return_t hg_ret;
    hg_id_t inv_id;
    hg_handle_t handle;

    /* Initialize the interface */
    hg_ret = hg_unit_init(argc, argv, false, &info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_unit_init() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* RPC test with unregistered ID */
    inv_id = MERCURY_REGISTER(info.hg_class, "unreg_id", void, void, NULL);
    HG_TEST_CHECK_ERROR_NORET(inv_id == 0, error, "HG_Register() failed");
    hg_ret = HG_Deregister(info.hg_class, inv_id);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Deregister() failed (%s)",
        HG_Error_to_string(hg_ret));

    HG_TEST("RPC with unregistered ID");
    HG_Test_log_disable(); // Expected to produce errors
    hg_ret = hg_test_rpc_input(info.handles[0], info.target_addr, inv_id,
        hg_test_rpc_output_cb, info.request);
    HG_Test_log_enable();
    HG_TEST_CHECK_ERROR_NORET(hg_ret != HG_NOENTRY, error,
        "hg_test_rpc_input() failed (%s, expected %s)",
        HG_Error_to_string(hg_ret), HG_Error_to_string(HG_NOENTRY));
    HG_PASSED();

    /* NULL RPC test */
    HG_TEST("NULL RPC");
    hg_ret = hg_test_rpc_no_input(info.handles[0], info.target_addr,
        hg_test_rpc_null_id_g, hg_test_rpc_no_output_cb, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_no_input() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_TEST_CHECK_ERROR(HG_Get_input_payload_size(info.handles[0]) != 0, error,
        hg_ret, HG_FAULT, "input payload non null (%" PRId64 ")",
        HG_Get_input_payload_size(info.handles[0]));
    HG_TEST_CHECK_ERROR(HG_Get_output_payload_size(info.handles[0]) != 0, error,
        hg_ret, HG_FAULT, "output payload non null (%" PRId64 ")",
        HG_Get_output_payload_size(info.handles[0]));
    HG_PASSED();

    /* Simple RPC test */
    HG_TEST("RPC with response");
    hg_ret = hg_test_rpc_input(info.handles[0], info.target_addr,
        hg_test_rpc_open_id_g, hg_test_rpc_output_cb, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_input() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* RPC test with lookup/free */
    if (!info.hg_test_info.na_test_info.self_send &&
        strcmp(HG_Class_get_name(info.hg_class), "mpi")) {
        int i;

        hg_ret = HG_Addr_set_remove(info.hg_class, info.target_addr);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret,
            "HG_Addr_set_remove() failed (%s)", HG_Error_to_string(hg_ret));

        hg_ret = HG_Addr_free(info.hg_class, info.target_addr);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Addr_free() failed (%s)",
            HG_Error_to_string(hg_ret));
        info.target_addr = HG_ADDR_NULL;

        HG_TEST("RPC with multiple lookup/free");
        for (i = 0; i < 32; i++) {
            hg_ret = HG_Addr_lookup2(info.hg_class,
                info.hg_test_info.na_test_info.target_name, &info.target_addr);
            HG_TEST_CHECK_HG_ERROR(error, hg_ret,
                "HG_Addr_lookup2() failed (%s)", HG_Error_to_string(hg_ret));

            hg_ret = hg_test_rpc_input(info.handles[0], info.target_addr,
                hg_test_rpc_open_id_g, hg_test_rpc_output_cb, info.request);
            HG_TEST_CHECK_HG_ERROR(error, hg_ret,
                "hg_test_rpc_input() failed (%s)", HG_Error_to_string(hg_ret));

            hg_ret = HG_Addr_set_remove(info.hg_class, info.target_addr);
            HG_TEST_CHECK_HG_ERROR(error, hg_ret,
                "HG_Addr_set_remove() failed (%s)", HG_Error_to_string(hg_ret));

            hg_ret = HG_Addr_free(info.hg_class, info.target_addr);
            HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Addr_free() failed (%s)",
                HG_Error_to_string(hg_ret));
            info.target_addr = HG_ADDR_NULL;
        }
        HG_PASSED();

        hg_ret = HG_Addr_lookup2(info.hg_class,
            info.hg_test_info.na_test_info.target_name, &info.target_addr);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Addr_lookup2() failed (%s)",
            HG_Error_to_string(hg_ret));
    }

    /* RPC test with no response */
    HG_TEST("RPC without response");
    if (info.hg_test_info.na_test_info.self_send) {
        hg_ret = HG_Create(info.context, info.target_addr,
            hg_test_rpc_open_id_no_resp_g, &handle);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Create() failed (%s)",
            HG_Error_to_string(hg_ret));
    } else
        handle = info.handles[0];
    hg_ret = hg_test_rpc_input(handle, info.target_addr,
        hg_test_rpc_open_id_no_resp_g, hg_test_rpc_no_output_cb, info.request);
    if (info.hg_test_info.na_test_info.self_send)
        HG_Destroy(handle);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_input() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    if (!info.hg_test_info.na_test_info.self_send) {
        /* RPC test with invalid ID (not registered on server) */
        inv_id = MERCURY_REGISTER(info.hg_class, "inv_id", void, void, NULL);
        HG_TEST_CHECK_ERROR_NORET(inv_id == 0, error, "HG_Register() failed");

        HG_TEST("RPC not registered on server");
        hg_ret = hg_test_rpc_inv(
            info.handles[0], info.target_addr, inv_id, info.request);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_inv() failed (%s)",
            HG_Error_to_string(hg_ret));
        HG_PASSED();
    }

#ifndef HG_HAS_XDR
    /* Overflow RPC test */
    HG_TEST("RPC with output overflow");
    hg_ret = hg_test_rpc_no_input(info.handles[0], info.target_addr,
        hg_test_overflow_id_g, hg_test_rpc_output_overflow_cb, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_no_input() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();
#endif

    /* Cancel RPC test (self cancelation is not supported) */
    if (!info.hg_test_info.na_test_info.self_send) {
        HG_TEST("RPC cancelation");
        hg_ret = hg_test_rpc_cancel(info.handles[0], info.target_addr,
            hg_test_cancel_rpc_id_g, hg_test_rpc_no_output_cb, info.request);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret,
            "hg_test_rpc_cancel() failed (%s)", HG_Error_to_string(hg_ret));
        HG_PASSED();
    }

    /* RPC test with multiple handles in flight */
    HG_TEST("multi RPCs");
    hg_ret = hg_test_rpc_multi(info.handles, info.handle_max, info.target_addr,
        0, hg_test_rpc_open_id_g, hg_test_rpc_multi_cb, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_rpc_multiple() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* RPC test with multiple handles in flight from multiple threads */
    HG_TEST("concurrent multi RPCs");
    hg_ret = hg_test_rpc_launch_threads(&info, hg_test_rpc_multi_thread);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret,
        "hg_test_rpc_launch_threads() failed (%s)", HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* RPC test from multiple threads with concurrent progress */
    HG_TEST("concurrent progress");
    hg_ret = hg_test_rpc_launch_threads(&info, hg_test_rpc_multi_progress);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret,
        "hg_test_rpc_launch_threads() failed (%s)", HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* RPC test from multiple threads with concurrent progress */
    HG_TEST("concurrent progress w/create");
    hg_ret =
        hg_test_rpc_launch_threads(&info, hg_test_rpc_multi_progress_create);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret,
        "hg_test_rpc_launch_threads() failed (%s)", HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* RPC test with multiple handles to multiple target contexts */
    if (info.hg_test_info.na_test_info.max_contexts) {
        hg_uint8_t i,
            context_count = info.hg_test_info.na_test_info.max_contexts;

        HG_TEST("multi context target RPCs");
        for (i = 0; i < context_count; i++) {
            hg_ret = hg_test_rpc_multi(info.handles, info.handle_max,
                info.target_addr, i, hg_test_rpc_open_id_g,
                hg_test_rpc_multi_cb, info.request);
            HG_TEST_CHECK_HG_ERROR(error, hg_ret,
                "hg_test_rpc_multiple() failed (%s)",
                HG_Error_to_string(hg_ret));
        }
        HG_PASSED();
    }

    hg_unit_cleanup(&info);

    return EXIT_SUCCESS;

error:
    hg_unit_cleanup(&info);

    return EXIT_FAILURE;
}
