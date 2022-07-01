/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DTS_COMMON_H__
#define __DTS_COMMON_H__

#include <daos/credit.h>

/**
 * Initialize I/O test context:
 * - create and connect to pool based on the input pool uuid and size
 * - create and open container based on the input container uuid
 */
int
dts_ctx_init(struct credit_context *tsc, struct io_engine *engine);

/**
 * Check if the I/O test context is for asynchronous test.
 */
bool
dts_is_async(struct credit_context *tsc);

/**
 * Finalize I/O test context:
 * - close and destroy the test container
 * - disconnect and destroy the test pool
 */
void
dts_ctx_fini(struct credit_context *tsc);

#endif /* __DTS_COMMON_H__ */
