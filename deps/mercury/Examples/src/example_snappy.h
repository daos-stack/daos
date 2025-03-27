/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EXAMPLE_SNAPPY_H
#define EXAMPLE_SNAPPY_H

#include <mercury_macros.h>

#define TEMP_DIRECTORY   "."
#define CONFIG_FILE_NAME "/port.cfg"

extern hg_bool_t snappy_compress_done_target_g;

/**
 * If this is the snappy interface we wish to ship:
 *
 *   snappy_status snappy_compress(const char* input,
 *                                 size_t input_length,
 *                                 char* compressed,
 *                                 size_t* compressed_length);
 */

#ifdef HG_HAS_BOOST
/* The MERCURY_GEN_PROC macro creates a new compound type consisting of
 * the members listed.
 *
 * snappy_compress_in_t will contain input/output members:
 * - input_bulk_handle: describes input/intput_length
 * - compressed_bulk_handle: describes compressed/compressed_length
 */
MERCURY_GEN_PROC(snappy_compress_in_t,
    ((hg_bulk_t) (input_bulk_handle))((hg_bulk_t) (compressed_bulk_handle)))

/* snappy_compress_out_t will contain output members:
 * - ret: snappy_status enum, the return type uses hg_int32_t as a base type
 */
MERCURY_GEN_PROC(snappy_compress_out_t,
    ((hg_int32_t) (ret))((hg_size_t) (compressed_length)))
#else
typedef struct {
    hg_bulk_t input_bulk_handle;
    hg_bulk_t compressed_bulk_handle;
} snappy_compress_in_t;

typedef struct {
    hg_int32_t ret;
    hg_size_t compressed_length;
} snappy_compress_out_t;

static HG_INLINE hg_return_t
hg_proc_snappy_compress_in_t(hg_proc_t proc, void *data)
{
    snappy_compress_in_t *in = (snappy_compress_in_t *) data;

    (void) hg_proc_hg_bulk_t(proc, &in->input_bulk_handle);
    (void) hg_proc_hg_bulk_t(proc, &in->compressed_bulk_handle);

    return HG_SUCCESS;
}

static HG_INLINE hg_return_t
hg_proc_snappy_compress_out_t(hg_proc_t proc, void *data)
{
    snappy_compress_out_t *out = (snappy_compress_out_t *) data;

    (void) hg_proc_int32_t(proc, &out->ret);
    (void) hg_proc_hg_size_t(proc, &out->compressed_length);

    return HG_SUCCESS;
}
#endif

/**
 * Convenient to have both origin and target call a "register" routine
 * that sets up all forwarded functions.
 */
hg_id_t
snappy_compress_register(hg_class_t *hg_class);

void
print_buf(int n, int *buf);

#endif /* EXAMPLE_SNAPPY_H */
