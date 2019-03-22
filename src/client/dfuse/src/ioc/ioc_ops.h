/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __IOC_OPS_H__
#define __IOC_OPS_H__

#define STR_H(s) #s
#define TO_STR(s) STR_H(s)
#define TRACE_TYPE TO_STR(TYPE_NAME)
#define TRACE_FREQ TO_STR(STAT_KEY) "_fuse_req"
#define TRACE_REQ TO_STR(STAT_KEY) "_req"
#define TRACE_RPC TO_STR(STAT_KEY) "_rpc"

#define IOC_REQ_INIT(src, FSH, api, in, rc)				\
	do {								\
		rc = 0;							\
		STAT_ADD(FSH->stats, STAT_KEY);				\
		if (FS_IS_OFFLINE(FSH)) {				\
			rc = (FSH)->offline_reason;			\
			break;						\
		}							\
		/* Acquire new object only if NULL */			\
		if (!src) {						\
			src = iof_pool_acquire(FSH->POOL_NAME);		\
			IOF_TRACE_UP(src, FSH, TRACE_TYPE);		\
		}							\
		if (!src) {						\
			rc = ENOMEM;					\
			break;						\
		}							\
		(src)->REQ_NAME.ir_api = &api;				\
		in = crt_req_get((src)->REQ_NAME.rpc);			\
	} while (0)

/* Initialise a descriptor and make the fuse request a child of it */
#define IOC_REQ_INIT_LL(src, fsh, api, in, fuse_req, rc)		\
	do {								\
		IOC_REQ_INIT(src, fsh, api, in, rc);			\
		if (rc)	{						\
			IOF_TRACE_UP(fuse_req, fsh, TRACE_FREQ);	\
			break;						\
		}							\
		(src)->REQ_NAME.req = fuse_req;				\
		IOF_TRACE_UP(fuse_req, src, TRACE_FREQ);		\
		IOF_TRACE_LINK((src)->REQ_NAME.rpc, src, TRACE_RPC);	\
	} while (0)

/* Initialise a descriptor and make the ioc request a child of it */
#define IOC_REQ_INIT_REQ(src, fsh, api, fuse_req, rc)			\
	do {								\
		rc = 0;							\
		STAT_ADD((fsh)->stats, STAT_KEY);			\
		/* Acquire new object only if NULL */			\
		if (!(src)) {						\
			src = iof_pool_acquire((fsh)->POOL_NAME);	\
			if (!(src)) {					\
				rc = ENOMEM;				\
				break;					\
			}						\
			IOF_TRACE_UP(src, fsh, TRACE_TYPE);		\
		}							\
		(src)->REQ_NAME.ir_api = &(api);			\
		(src)->REQ_NAME.req = fuse_req;				\
		IOF_TRACE_UP(&(src)->REQ_NAME, src, TRACE_REQ);		\
		IOF_TRACE_LINK((src)->REQ_NAME.rpc, &(src)->REQ_NAME, TRACE_RPC); \
	} while (0)

#define CONTAINER(req) container_of(req, struct TYPE_NAME, REQ_NAME)

#endif
