/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TEST_RPC_H
#define TEST_RPC_H

#include "mercury_macros.h"
#include "mercury_proc_string.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    hg_uint64_t cookie;
} rpc_handle_t;

#ifdef HG_HAS_BOOST

/* 1. Generate processor and struct for additional struct types
 * MERCURY_GEN_STRUCT_PROC( struct_type_name, fields )
 */
MERCURY_GEN_STRUCT_PROC(rpc_handle_t, ((hg_uint64_t) (cookie)))

/* Dummy function that needs to be shipped (already defined) */
/* int rpc_open(const char *path, rpc_handle_t handle, int *event_id); */

/* 2. Generate processor and struct for required input/output structs
 * MERCURY_GEN_PROC( struct_type_name, fields )
 */
MERCURY_GEN_PROC(
    rpc_open_in_t, ((hg_const_string_t) (path))((rpc_handle_t) (handle)))
MERCURY_GEN_PROC(rpc_open_out_t, ((hg_int32_t) (ret))((hg_int32_t) (event_id)))
#else
/* Dummy function that needs to be shipped (already defined) */
/* int rpc_open(const char *path, rpc_handle_t handle, int *event_id); */

/* Define hg_proc_rpc_handle_t */
static HG_INLINE hg_return_t
hg_proc_rpc_handle_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    rpc_handle_t *struct_data = (rpc_handle_t *) data;

    ret = hg_proc_uint64_t(proc, &struct_data->cookie);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

/* Define rpc_open_in_t */
typedef struct {
    hg_const_string_t path;
    rpc_handle_t handle;
} rpc_open_in_t;

/* Define hg_proc_rpc_open_in_t */
static HG_INLINE hg_return_t
hg_proc_rpc_open_in_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    rpc_open_in_t *struct_data = (rpc_open_in_t *) data;

    ret = hg_proc_hg_const_string_t(proc, &struct_data->path);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_rpc_handle_t(proc, &struct_data->handle);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

/* Define rpc_open_out_t */
typedef struct {
    hg_int32_t ret;
    hg_int32_t event_id;
} rpc_open_out_t;

/* Define hg_proc_rpc_open_out_t */
static HG_INLINE hg_return_t
hg_proc_rpc_open_out_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    rpc_open_out_t *struct_data = (rpc_open_out_t *) data;

    ret = hg_proc_int32_t(proc, &struct_data->ret);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_int32_t(proc, &struct_data->event_id);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}
#endif

#endif /* TEST_RPC_H */
