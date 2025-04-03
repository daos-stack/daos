/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_snappy.h"

#include <mercury.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <snappy-c.h>

/**
 * This example will send a buffer to the remote target, target will compress
 * it and send back the compressed data.
 */

#define NR_ITEMS 1024 * 1024

struct snappy_compress_rpc_args {
    int *input;
    size_t input_length;
    hg_bulk_t input_bulk_handle;
    void *compressed;
    hg_bulk_t compressed_bulk_handle;
};

static hg_id_t snappy_compress_id_g;
static hg_bool_t snappy_compress_done_g = HG_FALSE;

/* This routine gets executed after a call to HG_Trigger and
 * the RPC has completed */
static hg_return_t
snappy_compress_rpc_cb(const struct hg_cb_info *callback_info)
{
    struct snappy_compress_rpc_args *snappy_compress_rpc_args =
        (struct snappy_compress_rpc_args *) callback_info->arg;
    hg_handle_t handle = callback_info->info.forward.handle;

    int *input;
    size_t source_length;

    void *compressed;
    size_t compressed_length;

    int *uncompressed;
    size_t uncompressed_length;

    snappy_compress_out_t snappy_compress_output;
    snappy_status ret;

    /* Get output */
    printf("Received output from target\n");
    HG_Get_output(handle, &snappy_compress_output);

    /* Get Snappy output parameters */
    ret = (snappy_status) snappy_compress_output.ret;
    compressed_length = snappy_compress_output.compressed_length;
    compressed = snappy_compress_rpc_args->compressed;
    input = snappy_compress_rpc_args->input;
    source_length = snappy_compress_rpc_args->input_length;

    /* Check ret */
    if (ret != SNAPPY_OK) {
        fprintf(stderr, "Error: snappy_compressed failed with ret %d\n", ret);
    }

    /* The output data is now in the bulk buffer */
    printf("Compressed buffer length is: %zu\n", compressed_length);
    print_buf(5, (int *) compressed);
    if (snappy_validate_compressed_buffer(compressed, compressed_length) ==
        SNAPPY_OK) {
        printf("Compressed buffer validated: compressed successfully\n");
    }

    uncompressed_length = source_length * sizeof(int);
    uncompressed = (int *) malloc(uncompressed_length);

    /* Uncompress data and check uncompressed_length */
    printf("Uncompressing buffer...\n");
    snappy_uncompress(compressed, compressed_length, (char *) uncompressed,
        &uncompressed_length);
    printf("Uncompressed buffer length is: %zu\n", uncompressed_length);
    print_buf(20, uncompressed);

    /* Free output and handles */
    HG_Free_output(handle, &snappy_compress_output);
    HG_Bulk_free(snappy_compress_rpc_args->input_bulk_handle);
    HG_Bulk_free(snappy_compress_rpc_args->compressed_bulk_handle);

    /* Free data */
    free(uncompressed);
    free(compressed);
    free(input);
    free(snappy_compress_rpc_args);

    /* We're done */
    snappy_compress_done_g = HG_TRUE;

    return HG_SUCCESS;
}

static int
snappy_compress_rpc(
    hg_class_t *hg_class, hg_context_t *hg_context, hg_addr_t hg_target_addr)
{
    int *input;
    size_t source_length = NR_ITEMS * sizeof(int);
    hg_bulk_t input_bulk_handle;

    void *compressed;
    size_t max_compressed_length;
    hg_bulk_t compressed_bulk_handle;

    snappy_compress_in_t snappy_compress_input;
    struct snappy_compress_rpc_args *snappy_compress_rpc_args;
    hg_handle_t handle;
    int i;

    /**
     * We are going to take a buffer and send it to the server for compression.
     * Mercury works better when you know how much (or an uppper bound on) data
     * to expect.
     */
    max_compressed_length = snappy_max_compressed_length(source_length);
    printf("Input buffer length is: %zu\n", source_length);
    printf("Max compressed length is: %zu\n", max_compressed_length);

    /* Generate input buffer */
    input = (int *) malloc(source_length);
    for (i = 0; i < NR_ITEMS; i++) {
        input[i] = rand() % 10;
    }
    print_buf(20, input);

    /* Allocate compressed buffer */
    compressed = malloc(max_compressed_length);
    memset(compressed, '\0', max_compressed_length);

    /* Create HG handle bound to target */
    HG_Create(hg_context, hg_target_addr, snappy_compress_id_g, &handle);

    /**
     * Associate 'handle' with a region of memory. Mercury's bulk transfer is
     * going to get/put data from this region.
     */
    HG_Bulk_create(hg_class, 1, (void **) &input, &source_length,
        HG_BULK_READ_ONLY, &input_bulk_handle);
    HG_Bulk_create(hg_class, 1, &compressed, &max_compressed_length,
        HG_BULK_READWRITE, &compressed_bulk_handle);

    /* Create struct to keep arguments as the call will be executed
     * asynchronously */
    snappy_compress_rpc_args = (struct snappy_compress_rpc_args *) malloc(
        sizeof(struct snappy_compress_rpc_args));
    snappy_compress_rpc_args->input = input;
    snappy_compress_rpc_args->input_length = source_length;
    snappy_compress_rpc_args->input_bulk_handle = input_bulk_handle;
    snappy_compress_rpc_args->compressed = compressed;
    snappy_compress_rpc_args->compressed_bulk_handle = compressed_bulk_handle;

    /* Set input arguments that will be passed to HG_Forward */
    snappy_compress_input.input_bulk_handle = input_bulk_handle;
    snappy_compress_input.compressed_bulk_handle = compressed_bulk_handle;

    /* Forward the call */
    printf("Sending input to target\n");
    HG_Forward(handle, snappy_compress_rpc_cb, snappy_compress_rpc_args,
        &snappy_compress_input);

    /* Handle will be destroyed when call completes (reference count) */
    HG_Destroy(handle);

    return 0;
}

int
main(void)
{
    const char *info_string = NULL;

    char target_addr_string[PATH_MAX], *p;
    FILE *na_config = NULL;

    hg_class_t *hg_class;
    hg_context_t *hg_context;
    hg_addr_t hg_target_addr;

    hg_return_t hg_ret;

    /* Get info string */
    info_string = getenv("HG_PORT_NAME");
    if (!info_string) {
        fprintf(stderr, "HG_PORT_NAME environment variable must be set\n");
        exit(0);
    }
    printf("Using %s\n", info_string);

    HG_Set_log_level("warning");

    /* Initialize Mercury with the desired network abstraction class */
    hg_class = HG_Init(info_string, HG_FALSE);

    /* Create HG context */
    hg_context = HG_Context_create(hg_class);

    /* The connection string is generated after
     * NA_Addr_self()/NA_Addr_to_string(), we must get that string and pass it
     * to  NA_Addr_lookup() */
    na_config = fopen(TEMP_DIRECTORY CONFIG_FILE_NAME, "r");
    if (!na_config) {
        fprintf(stderr, "Could not open config file from: %s\n",
            TEMP_DIRECTORY CONFIG_FILE_NAME);
        exit(0);
    }
    fgets(target_addr_string, PATH_MAX, na_config);
    p = strrchr(target_addr_string, '\n');
    if (p != NULL)
        *p = '\0';
    printf("Target address is: %s\n", target_addr_string);
    fclose(na_config);

    /* Look up target address */
    HG_Addr_lookup2(hg_class, target_addr_string, &hg_target_addr);

    /* Register RPC */
    snappy_compress_id_g = snappy_compress_register(hg_class);

    /* Send RPC to target */
    snappy_compress_rpc(hg_class, hg_context, hg_target_addr);

    /* Poke progress engine and check for events */
    do {
        unsigned int actual_count = 0;
        do {
            hg_ret = HG_Trigger(
                hg_context, 0 /* timeout */, 1 /* max count */, &actual_count);
        } while ((hg_ret == HG_SUCCESS) && actual_count);

        /* Do not try to make progress anymore if we're done */
        if (snappy_compress_done_g)
            break;

        hg_ret = HG_Progress(hg_context, HG_MAX_IDLE_TIME);
    } while (hg_ret == HG_SUCCESS);

    /* Finalize */
    HG_Addr_free(hg_class, hg_target_addr);

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return EXIT_SUCCESS;
}
