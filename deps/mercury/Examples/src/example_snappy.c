/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_snappy.h"

#include <inttypes.h>
#include <snappy-c.h>
#include <stdio.h>
#include <stdlib.h>

/* wrapping a compression routine is a little different than a read or
 * write, as one is transforming the data and needs to send it back to the
 * initiator */

/* Hold parameters for the bulk transfer callbacks */
struct snappy_transfer_args {
    hg_handle_t handle;
    snappy_compress_in_t snappy_compress_input;
    hg_bulk_t local_input_bulk_handle;
    void *compressed;
    size_t compressed_length;
    hg_bulk_t local_compressed_bulk_handle;
    snappy_status ret;
};

hg_bool_t snappy_compress_done_target_g = HG_FALSE;

static hg_return_t
snappy_pull_cb(const struct hg_cb_info *hg_cb_info);

static hg_return_t
snappy_push_cb(const struct hg_cb_info *hg_cb_info);

static hg_return_t
snappy_compress_done_cb(const struct hg_cb_info *hg_cb_info);

static hg_return_t
snappy_compress_cb(hg_handle_t handle);

void
print_buf(int n, int *buf)
{
    int i;
    printf("First %d elements of buffer: ", n);
    for (i = 0; i < n; i++) {
        printf("%d ", buf[i]);
    }
    printf("\n");
}

static hg_return_t
snappy_pull_cb(const struct hg_cb_info *hg_cb_info)
{
    struct snappy_transfer_args *snappy_transfer_args =
        (struct snappy_transfer_args *) hg_cb_info->arg;
    hg_return_t ret = HG_SUCCESS;
    void *input;
    hg_size_t input_length;
    size_t source_length =
        HG_Bulk_get_size(snappy_transfer_args->local_input_bulk_handle);

    /* Get pointer to input buffer from local handle */
    HG_Bulk_access(hg_cb_info->info.bulk.local_handle, 0, source_length,
        HG_BULK_READ_ONLY, 1, &input, &input_length, NULL);
    printf("Transferred input buffer of length: %" PRIu64 "\n", input_length);
    print_buf(20, (int *) input);

    /* Allocate compressed buffer for compressing input data */
    snappy_transfer_args->compressed_length =
        snappy_max_compressed_length(input_length);
    snappy_transfer_args->compressed =
        malloc(snappy_transfer_args->compressed_length);

    /* Compress data */
    printf("Compressing buffer...\n");
    snappy_transfer_args->ret =
        snappy_compress(input, input_length, snappy_transfer_args->compressed,
            &snappy_transfer_args->compressed_length);
    printf(
        "Return value of snappy_compress is: %d\n", snappy_transfer_args->ret);
    printf("Compressed buffer length is: %zu\n",
        snappy_transfer_args->compressed_length);
    print_buf(5, (int *) snappy_transfer_args->compressed);

    /* Free bulk handles */
    HG_Bulk_free(snappy_transfer_args->local_input_bulk_handle);

    if (snappy_validate_compressed_buffer(snappy_transfer_args->compressed,
            snappy_transfer_args->compressed_length) == SNAPPY_OK) {
        printf("Compressed buffer validated: compressed successfully\n");
    }

    /* Now set up bulk transfer for "push to origin" callback */
    HG_Bulk_create(HG_Get_info(snappy_transfer_args->handle)->hg_class, 1,
        &snappy_transfer_args->compressed,
        &snappy_transfer_args->compressed_length, HG_BULK_READ_ONLY,
        &snappy_transfer_args->local_compressed_bulk_handle);

    HG_Bulk_transfer(HG_Get_info(snappy_transfer_args->handle)->context,
        snappy_push_cb, snappy_transfer_args, HG_BULK_PUSH,
        HG_Get_info(snappy_transfer_args->handle)->addr,
        snappy_transfer_args->snappy_compress_input.compressed_bulk_handle,
        0,                                                     /* origin */
        snappy_transfer_args->local_compressed_bulk_handle, 0, /* local */
        snappy_transfer_args->compressed_length, HG_OP_ID_IGNORE);

    return ret;
}

/* data was compressed in the "pull from initiator" function.  This callback
 * pushes the compressed data back */

static hg_return_t
snappy_push_cb(const struct hg_cb_info *hg_cb_info)
{
    struct snappy_transfer_args *snappy_transfer_args =
        (struct snappy_transfer_args *) hg_cb_info->arg;
    hg_return_t ret = HG_SUCCESS;
    snappy_compress_out_t snappy_compress_output;

    /* Set output parameters to inform origin */
    snappy_compress_output.ret = (hg_int32_t) snappy_transfer_args->ret;
    snappy_compress_output.compressed_length =
        snappy_transfer_args->compressed_length;
    printf("Transferred compressed buffer of length %zu\n",
        snappy_transfer_args->compressed_length);

    printf("Sending output parameters back to origin\n");
    HG_Respond(snappy_transfer_args->handle, snappy_compress_done_cb, NULL,
        &snappy_compress_output);

    /* Free bulk handles */
    printf("Freeing resources\n");
    HG_Bulk_free(snappy_transfer_args->local_compressed_bulk_handle);
    free(snappy_transfer_args->compressed);

    /* Free input */
    HG_Free_input(snappy_transfer_args->handle,
        &snappy_transfer_args->snappy_compress_input);

    /* Destroy handle (no longer need it, safe because of reference count) */
    HG_Destroy(snappy_transfer_args->handle);
    free(snappy_transfer_args);

    return ret;
}

static hg_return_t
snappy_compress_done_cb(const struct hg_cb_info *callback_info)
{
    /* We're done */
    snappy_compress_done_target_g = HG_TRUE;

    return callback_info->ret;
}

/**
 * The routine that sets up the routines that actually do the work.
 * This 'handle' parameter is the only value passed to this callback, but
 * Mercury routines allow us to query information about the context in which
 * we are called. */
static hg_return_t
snappy_compress_cb(hg_handle_t handle)
{
    struct snappy_transfer_args *snappy_transfer_args;
    size_t input_length;

    snappy_transfer_args = (struct snappy_transfer_args *) malloc(
        sizeof(struct snappy_transfer_args));
    snappy_transfer_args->handle = handle;

    /* Get input parameters sent on origin through on HG_Forward() */
    HG_Get_input(handle, &snappy_transfer_args->snappy_compress_input);

    /* Now set up the bulk transfer and get the input length */
    input_length = HG_Bulk_get_size(
        snappy_transfer_args->snappy_compress_input.input_bulk_handle);

    /* The bulk 'handle' is basically a pointer, with the addition that 'handle'
     * could refer to more than one memory region. */
    HG_Bulk_create(HG_Get_info(handle)->hg_class, 1, NULL, &input_length,
        HG_BULK_READWRITE, &snappy_transfer_args->local_input_bulk_handle);

    /* Pull data from origin's memory into our own */
    /* Another way to do this is via HG_Bulk_access, which would allow mercury,
     * if "co-resident" to avoid a copy of data */
    HG_Bulk_transfer(HG_Get_info(handle)->context, snappy_pull_cb,
        snappy_transfer_args, HG_BULK_PULL, HG_Get_info(handle)->addr,
        snappy_transfer_args->snappy_compress_input.input_bulk_handle,
        0,                                                /* origin */
        snappy_transfer_args->local_input_bulk_handle, 0, /* local */
        input_length, HG_OP_ID_IGNORE);

    return HG_SUCCESS;
}

hg_id_t
snappy_compress_register(hg_class_t *hg_class)
{
    return MERCURY_REGISTER(hg_class, "snappy_compress", snappy_compress_in_t,
        snappy_compress_out_t, snappy_compress_cb);
}
