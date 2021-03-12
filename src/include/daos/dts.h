/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DTS_COMMON_H__
#define __DTS_COMMON_H__

/**
 * Initialize I/O test context:
 * - create and connect to pool based on the input pool uuid and size
 * - create and open container based on the input container uuid
 */
int dts_ctx_init(struct dts_context *tsc);

/**
 * Check if the I/O test context is for asynchronous test.
 */
bool dts_is_async(struct dts_context *tsc);

/**
 * Finalize I/O test context:
 * - close and destroy the test container
 * - disconnect and destroy the test pool
 */
void dts_ctx_fini(struct dts_context *tsc);

/**
 * Try to obtain a free credit from the I/O context.
 */
struct dts_io_credit *dts_credit_take(struct dts_context *tsc);
/**
 * Drain all the inflight I/O credits of @tsc.
 */
int dts_credit_drain(struct dts_context *tsc);

/** return an unused credit */
void dts_credit_return(struct dts_context *tsc, struct dts_io_credit *cred);

#endif /* __DTS_COMMON_H__ */
