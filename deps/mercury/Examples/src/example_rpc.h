/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EXAMPLE_RPC_H
#define EXAMPLE_RPC_H

#include <mercury_macros.h>

#ifdef HG_HAS_BOOST
/* visible API for example RPC operation */
MERCURY_GEN_PROC(my_rpc_out_t, ((int32_t) (ret)))
MERCURY_GEN_PROC(
    my_rpc_in_t, ((int32_t) (input_val))((hg_bulk_t) (bulk_handle)))
#else
typedef struct {
    int32_t ret;
} my_rpc_out_t;

typedef struct {
    int32_t input_val;
    hg_bulk_t bulk_handle;
} my_rpc_in_t;

static HG_INLINE hg_return_t
hg_proc_my_rpc_in_t(hg_proc_t proc, void *data)
{
    my_rpc_in_t *in = (my_rpc_in_t *) data;

    (void) hg_proc_int32_t(proc, &in->input_val);
    (void) hg_proc_hg_bulk_t(proc, &in->bulk_handle);

    return HG_SUCCESS;
}

static HG_INLINE hg_return_t
hg_proc_my_rpc_out_t(hg_proc_t proc, void *data)
{
    my_rpc_out_t *out = (my_rpc_out_t *) data;

    (void) hg_proc_int32_t(proc, &out->ret);

    return HG_SUCCESS;
}
#endif

hg_id_t
my_rpc_register(void);

#endif /* EXAMPLE_RPC_H */
