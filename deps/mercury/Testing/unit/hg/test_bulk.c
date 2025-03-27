/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_unit.h"

#include "mercury_param.h"

/****************/
/* Local Macros */
/****************/

/* Wait timeout in ms */
#define HG_TEST_WAIT_TIMEOUT (HG_TEST_TIMEOUT * 1000)

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct hg_test_bulk_info {
    void **buf_ptrs;
    hg_size_t *buf_sizes;
    size_t buf_count;
    hg_bulk_t bulk_handle;
};

struct forward_cb_args {
    hg_request_t *request;
    size_t expected_size;
    hg_return_t ret;
};

/********************/
/* Local Prototypes */
/********************/

static hg_return_t
hg_test_bulk_create(hg_class_t *hg_class, size_t segment_count,
    size_t segment_size, struct hg_test_bulk_info *bulk_info_p);

static hg_return_t
hg_test_bulk_destroy(struct hg_test_bulk_info *bulk_info);

static hg_return_t
hg_test_bulk_forward(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_bulk_t bulk_handle, size_t transfer_size,
    size_t origin_offset, size_t target_offset, hg_request_t *request);

static hg_return_t
hg_test_bulk_forward_cb(const struct hg_cb_info *callback_info);

/*******************/
/* Local Variables */
/*******************/

extern hg_id_t hg_test_bulk_write_id_g;
extern hg_id_t hg_test_bulk_bind_write_id_g;
extern hg_id_t hg_test_bulk_bind_forward_id_g;

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_bulk_create(hg_class_t *hg_class, size_t segment_count,
    size_t segment_size, struct hg_test_bulk_info *bulk_info_p)
{
    void **buf_ptrs = NULL;
    hg_size_t *buf_sizes = NULL;
    hg_bulk_t bulk_handle;
    size_t i;
    hg_return_t ret;

    buf_ptrs = (void **) malloc(segment_count * sizeof(*buf_ptrs));
    HG_TEST_CHECK_ERROR(
        buf_ptrs == NULL, error, ret, HG_NOMEM, "Could not allocate buf_ptrs");

    buf_sizes = (hg_size_t *) malloc(segment_count * sizeof(*buf_sizes));
    HG_TEST_CHECK_ERROR(buf_sizes == NULL, error, ret, HG_NOMEM,
        "Could not allocate buf_sizes");

    for (i = 0; i < segment_count; i++) {
        size_t j;

        buf_sizes[i] = (hg_size_t) segment_size;
        if (buf_sizes[i] == 0)
            buf_ptrs[i] = NULL;
        else {
            buf_ptrs[i] = malloc(buf_sizes[i]);
            HG_TEST_CHECK_ERROR(buf_ptrs == NULL, error, ret, HG_NOMEM,
                "Could not allocate bulk_buf");

            for (j = 0; j < buf_sizes[i]; j++)
                ((char **) buf_ptrs)[i][j] = (char) (i * buf_sizes[i] + j);
        }
    }

    ret = HG_Bulk_create(hg_class, (hg_uint32_t) segment_count, buf_ptrs,
        buf_sizes, HG_BULK_READ_ONLY, &bulk_handle);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Bulk_create() failed (%s)", HG_Error_to_string(ret));

    *bulk_info_p = (struct hg_test_bulk_info){.buf_count = segment_count,
        .buf_ptrs = buf_ptrs,
        .buf_sizes = buf_sizes,
        .bulk_handle = bulk_handle};

    return HG_SUCCESS;

error:
    if (buf_ptrs != NULL) {
        for (i = 0; i < segment_count; i++)
            free(buf_ptrs[i]);
        free(buf_ptrs);
    }
    if (buf_sizes != NULL)
        free(buf_sizes);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_bulk_destroy(struct hg_test_bulk_info *bulk_info)
{
    hg_return_t ret;
    size_t i;

    ret = HG_Bulk_free(bulk_info->bulk_handle);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Bulk_free() failed (%s)", HG_Error_to_string(ret));

    if (bulk_info->buf_ptrs != NULL) {
        for (i = 0; i < bulk_info->buf_count; i++)
            free(bulk_info->buf_ptrs[i]);
        free(bulk_info->buf_ptrs);
        bulk_info->buf_ptrs = NULL;
    }
    if (bulk_info->buf_sizes != NULL) {
        free(bulk_info->buf_sizes);
        bulk_info->buf_sizes = NULL;
    }
    bulk_info->buf_count = 0;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_bulk_forward(hg_handle_t handle, hg_addr_t addr, hg_id_t rpc_id,
    hg_cb_t callback, hg_bulk_t bulk_handle, size_t transfer_size,
    size_t origin_offset, size_t target_offset, hg_request_t *request)
{
    hg_return_t ret;
    struct forward_cb_args forward_cb_args = {
        .request = request, .expected_size = transfer_size, .ret = HG_SUCCESS};
    bulk_write_in_t in_struct = {.bulk_handle = bulk_handle,
        .fildes = 0,
        .origin_offset = (hg_size_t) origin_offset,
        .target_offset = (hg_size_t) target_offset,
        .transfer_size = (hg_size_t) transfer_size};
    unsigned int flag;
    int rc;

    HG_TEST_LOG_DEBUG(
        "Requesting transfer_size=%zu, origin_offset=%zu, target_offset=%zu",
        in_struct.transfer_size, in_struct.origin_offset,
        in_struct.target_offset);

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

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_bulk_forward_cb(const struct hg_cb_info *callback_info)
{
    hg_handle_t handle = callback_info->info.forward.handle;
    struct forward_cb_args *args =
        (struct forward_cb_args *) callback_info->arg;
    bulk_write_out_t bulk_write_out_struct;
    hg_return_t ret = callback_info->ret;

    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in HG callback (%s)",
        HG_Error_to_string(callback_info->ret));

    /* Get output */
    ret = HG_Get_output(handle, &bulk_write_out_struct);
    HG_TEST_CHECK_HG_ERROR(
        done, ret, "HG_Get_output() failed (%s)", HG_Error_to_string(ret));

    /* Get output parameters */
    HG_TEST_CHECK_ERROR(bulk_write_out_struct.ret != args->expected_size, free,
        ret, HG_MSGSIZE, "Returned: %" PRIu64 " bytes, was expecting %zu",
        bulk_write_out_struct.ret, args->expected_size);

free:
    /* Free output */
    if (ret != HG_SUCCESS)
        (void) HG_Free_output(handle, &bulk_write_out_struct);
    else {
        ret = HG_Free_output(handle, &bulk_write_out_struct);
        HG_TEST_CHECK_HG_ERROR(
            done, ret, "HG_Free_output() failed (%s)", HG_Error_to_string(ret));
    }

done:
    args->ret = ret;

    hg_request_complete(args->request);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct hg_unit_info info;
    struct hg_test_bulk_info bulk_info = {.buf_count = 0,
        .buf_ptrs = NULL,
        .buf_sizes = NULL,
        .bulk_handle = HG_BULK_NULL};
    hg_return_t hg_ret;
    size_t buf_size;

    /* Initialize the interface */
    hg_ret = hg_unit_init(argc, argv, false, &info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_unit_init() failed (%s)",
        HG_Error_to_string(hg_ret));
    buf_size = info.buf_size_max;

    /* Buf size required is at least 1024 */
    HG_TEST_CHECK_ERROR_NORET(!powerof2(buf_size), error,
        "Buffer size must be a power of 2 (%zu)", buf_size);
    HG_TEST_CHECK_ERROR_NORET(buf_size < 1024, error,
        "Buffer size must be at least 1024 (%zu)", buf_size);

    /**************************************************************************
     * NULL RPC bulk tests.
     *************************************************************************/

    /* Create bulk info */
    hg_ret = hg_test_bulk_create(info.hg_class, 2, 0, &bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_create() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Zero size RPC bulk test */
    HG_TEST("null RPC bulk");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        0, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Destroy bulk info */
    hg_ret = hg_test_bulk_destroy(&bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_destroy() failed (%s)",
        HG_Error_to_string(hg_ret));

    /**************************************************************************
     * Contiguous RPC bulk tests.
     *************************************************************************/

    /* Create bulk info */
    hg_ret = hg_test_bulk_create(info.hg_class, 1, buf_size, &bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_create() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Contigous bulk test (size 0, offsets 0, 0) */
    HG_TEST("zero size RPC bulk (size 0, offsets 0, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        0, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Contigous bulk test (size BUFSIZE, offsets 0, 0) */
    HG_TEST("contiguous RPC bulk (size BUFSIZE, offsets 0, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Contigous bulk test (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0) */
    HG_TEST("contiguous RPC bulk (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 4, buf_size / 2 + 1, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Contigous bulk test (size BUFSIZE/8, offsets BUFSIZE/2 + 1, BUFSIZE/4) */
    HG_TEST("contiguous RPC bulk (size BUFSIZE/8, offsets BUFSIZE/2 + 1, "
            "BUFSIZE/4)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 8, buf_size / 2 + 1, buf_size / 4, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Binding address info to bulk */
    if (strcmp(HG_Class_get_name(info.hg_class), "bmi") != 0 &&
        strcmp(HG_Class_get_name(info.hg_class), "mpi")) {
        /* Bind local context to bulk, it is only necessary if this bulk handle
         * will be shared to another server by the server of this RPC, but it
         * should also work for normal case. Add here just to test the
         * functionality. */
        hg_ret = HG_Bulk_bind(bulk_info.bulk_handle, info.context);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret, "HG_Bulk_bind() failed (%s)",
            HG_Error_to_string(hg_ret));

        /* Bulk bind test */
        HG_TEST("bind contiguous RPC bulk (size BUFSIZE, offsets 0, 0)");
        hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
            hg_test_bulk_bind_write_id_g, hg_test_bulk_forward_cb,
            bulk_info.bulk_handle, buf_size, 0, 0, info.request);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret,
            "hg_test_bulk_forward() failed (%s)", HG_Error_to_string(hg_ret));
        HG_PASSED();

        /* Forward bulk bind test */
        HG_TEST(
            "forward bind contiguous RPC bulk (size BUFSIZE, offsets 0, 0)");
        hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
            hg_test_bulk_bind_forward_id_g, hg_test_bulk_forward_cb,
            bulk_info.bulk_handle, buf_size, 0, 0, info.request);
        HG_TEST_CHECK_HG_ERROR(error, hg_ret,
            "hg_test_bulk_forward() failed (%s)", HG_Error_to_string(hg_ret));
        HG_PASSED();
    }

    /* Destroy bulk info */
    hg_ret = hg_test_bulk_destroy(&bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_destroy() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* BMI has issues with next tests */
    if (strcmp(HG_Class_get_name(info.hg_class), "bmi") == 0)
        goto cleanup;

    /**************************************************************************
     * Small RPC bulk tests.
     *************************************************************************/

    /* Create bulk info */
    hg_ret = hg_test_bulk_create(info.hg_class, 2, 8, &bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_create() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Small bulk test (size 8, offsets 0, 0) */
    HG_TEST("small segmented RPC bulk (size 16, offsets 0, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        16, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Small bulk test (size 4, offsets 8, 0) */
    HG_TEST("small segmented RPC bulk (size 8, offsets 8, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        8, 8, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Small bulk test (size 8, offsets 4, 2) */
    HG_TEST("small segmented RPC bulk (size 8, offsets 4, 2)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        8, 4, 2, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Destroy bulk info */
    hg_ret = hg_test_bulk_destroy(&bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_destroy() failed (%s)",
        HG_Error_to_string(hg_ret));

    /**************************************************************************
     * Segmented RPC bulk tests.
     *************************************************************************/

    /* Create bulk info */
    hg_ret = hg_test_bulk_create(info.hg_class, 16, buf_size / 16, &bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_create() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Segmented bulk test (size BUFSIZE, offsets 0, 0) */
    HG_TEST("segmented RPC bulk (size BUFSIZE, offsets 0, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Segmented bulk test (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0) */
    HG_TEST("segmented RPC bulk (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 4, buf_size / 2 + 1, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Segmented bulk test (size BUFSIZE/8, offsets BUFSIZE/2 + 1, BUFSIZE/4) */
    HG_TEST("segmented RPC bulk (size BUFSIZE/8, offsets BUFSIZE/2 + 1, "
            "BUFSIZE/4)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 8, buf_size / 2 + 1, buf_size / 4, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Destroy bulk info */
    hg_ret = hg_test_bulk_destroy(&bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_destroy() failed (%s)",
        HG_Error_to_string(hg_ret));

    /**************************************************************************
     * Over-segmented RPC bulk tests.
     *************************************************************************/

#ifndef HG_HAS_XDR
    /* Create bulk info */
    hg_ret =
        hg_test_bulk_create(info.hg_class, 1024, buf_size / 1024, &bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_create() failed (%s)",
        HG_Error_to_string(hg_ret));

    /* Over-segmented bulk test (size BUFSIZE, offsets 0, 0) */
    HG_TEST("over-segmented RPC bulk (size BUFSIZE, offsets 0, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size, 0, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Over-segmented bulk test (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0) */
    HG_TEST(
        "over-segmented RPC bulk (size BUFSIZE/4, offsets BUFSIZE/2 + 1, 0)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 4, buf_size / 2 + 1, 0, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();

    /* Over-segmented bulk test (size BUFSIZE/8, offsets BUFSIZE/2 + 1,
     * BUFSIZE/4) */
    HG_TEST("over-segmented RPC bulk (size BUFSIZE/8, offsets BUFSIZE/2 + 1,"
            "BUFSIZE/4)");
    hg_ret = hg_test_bulk_forward(info.handles[0], info.target_addr,
        hg_test_bulk_write_id_g, hg_test_bulk_forward_cb, bulk_info.bulk_handle,
        buf_size / 8, buf_size / 2 + 1, buf_size / 4, info.request);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_forward() failed (%s)",
        HG_Error_to_string(hg_ret));
    HG_PASSED();
#endif

    /* Destroy bulk info */
    hg_ret = hg_test_bulk_destroy(&bulk_info);
    HG_TEST_CHECK_HG_ERROR(error, hg_ret, "hg_test_bulk_destroy() failed (%s)",
        HG_Error_to_string(hg_ret));

cleanup:
    hg_unit_cleanup(&info);

    return EXIT_SUCCESS;

error:
    (void) hg_test_bulk_destroy(&bulk_info);
    hg_unit_cleanup(&info);

    return EXIT_FAILURE;
}
