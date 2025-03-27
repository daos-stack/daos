/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TEST_OVERFLOW_H
#define TEST_OVERFLOW_H

#include "mercury_macros.h"
#include "mercury_proc_string.h"

#ifdef HG_HAS_BOOST

MERCURY_GEN_PROC(
    overflow_out_t, ((hg_string_t) (string))((hg_uint64_t) (string_len)))
#else
/* Define overflow_out_t */
typedef struct {
    hg_string_t string;
    hg_uint64_t string_len;
} overflow_out_t;

/* Define hg_proc_overflow_out_t */
static HG_INLINE hg_return_t
hg_proc_overflow_out_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    overflow_out_t *struct_data = (overflow_out_t *) data;

    ret = hg_proc_hg_string_t(proc, &struct_data->string);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_hg_uint64_t(proc, &struct_data->string_len);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}
#endif

#endif /* TEST_OVERFLOW_H */
