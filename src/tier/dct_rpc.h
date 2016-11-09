/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * dct: RPC Protocol Definitions
 *
 * This is naturally shared by both dctc and dcts. The in and out data
 * structures may safely contain compiler-generated paddings, which will be
 * removed crt's serialization process.
 *
 * Every pool operation shall pass in the UUID of the pool it intends to access
 * and the UUID of its pool handle. The pool UUID enables server to quickly
 * locate the right mpool.
 *
 * Every container operation shall pass in the UUID of the container and the
 * UUID of its container handle.
 */

#ifndef __DCT_RPC_H__
#define __DCT_RPC_H__

#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/event.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
enum dct_operation {
	DCT_PING		= 1,
	TIER_FETCH		= 2,
};

struct dct_ping_in {
	uint32_t	ping_in;
};

struct dct_ping_out {
	uint32_t	ping_out;
};

struct tier_fetch_in {
	uuid_t		   tfi_pool;
	uuid_t		   tfi_pool_hdl;
	uuid_t		   tfi_co_hdl;
	daos_epoch_t       tfi_ep;
};

struct tier_fetch_out {
	int32_t		tfo_ret;
};

int
dct_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req);


extern struct daos_rpc dct_rpcs[];
#endif /* __DCT_RPC_H__ */
