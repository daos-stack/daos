/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT. It it the common header file which be included by
 * all other .c files of CaRT.
 */

#ifndef __CRT_INTERNAL_H__
#define __CRT_INTERNAL_H__

#include "crt_debug.h"

#include <gurt/common.h>
#include <gurt/fault_inject.h>
#include <cart/api.h>

#include "crt_hg.h"
#include "crt_internal_types.h"
#include "crt_internal_fns.h"
#include "crt_rpc.h"
#include "crt_group.h"
#include "crt_tree.h"
#include "crt_self_test.h"
#include "crt_ctl.h"
#include "crt_swim.h"

/* A wrapper around D_TRACE_DEBUG that ensures the ptr option is a RPC */
#define RPC_TRACE(mask, rpc, fmt, ...)					\
	do {								\
		D_TRACE_DEBUG(mask, (rpc),				\
			"[opc=0x%x rpcid=0x%lx rank:tag=%d:%d] " fmt,	\
			(rpc)->crp_pub.cr_opc,				\
			(rpc)->crp_req_hdr.cch_rpcid,			\
			(rpc)->crp_pub.cr_ep.ep_rank,			\
			(rpc)->crp_pub.cr_ep.ep_tag,			\
			## __VA_ARGS__);				\
	} while (0)

/* Log an error with a RPC descriptor */
#define RPC_ERROR(rpc, fmt, ...)					\
	do {								\
		D_TRACE_ERROR((rpc),					\
			"[opc=0x%x rpcid=0x%lx rank:tag=%d:%d] " fmt,	\
			(rpc)->crp_pub.cr_opc,				\
			(rpc)->crp_req_hdr.cch_rpcid,			\
			(rpc)->crp_pub.cr_ep.ep_rank,			\
			(rpc)->crp_pub.cr_ep.ep_tag,			\
			## __VA_ARGS__);				\
	} while (0)

extern uint32_t crt_swim_rpc_timeout;

#endif /* __CRT_INTERNAL_H__ */
