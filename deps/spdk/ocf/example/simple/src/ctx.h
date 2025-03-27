/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __CTX_H__
#define __CTX_H__

#include <ocf/ocf.h>

#define VOL_TYPE 1

ctx_data_t *ctx_data_alloc(uint32_t pages);
void ctx_data_free(ctx_data_t *ctx_data);

int ctx_init(ocf_ctx_t *ocf_ctx);
void ctx_cleanup(ocf_ctx_t ctx);

#endif
