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
/**
 * dsr: RPC Protocol Definitions
 *
 * This is naturally shared by both dsrc and dsrs. The in and out data
 * structures may safely contain compiler-generated paddings, which will be
 * removed dtp's serialization process.
 *
 */

#ifndef __DSR_RPC_H__
#define __DSR_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/event.h>
#include <daos/rpc.h>
#include <daos/transport.h>
#include <daos_event.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * dtp_req_create(..., opc, ...). See daos_rpc.h.
 */
enum dsr_operation {
	DSR_TGT_OBJ_UPDATE	= 1,
	DSR_TGT_OBJ_FETCH	= 2,
	DSR_TGT_OBJ_ENUMERATE	= 3,
};

struct object_update_in {
	daos_unit_oid_t oui_oid;
	uuid_t		oui_co_hdl;
	uint64_t	oui_epoch;
	uint32_t	oui_nr;
	uint32_t	oui_pad;
	daos_dkey_t	oui_dkey;
	struct dtp_array oui_iods;
	struct dtp_array oui_bulks;
};

struct object_fetch_out {
	int		 ofo_ret;
	int		 ofo_pad;
	struct dtp_array ofo_sizes;
};

/* object Enumerate in/out */
struct object_enumerate_in {
	daos_unit_oid_t oei_oid;
	uuid_t		oei_co_hdl;
	uint64_t	oei_epoch;
	uint32_t	oei_nr;
	uint32_t	oei_pad;
	daos_hash_out_t oei_anchor;
	dtp_bulk_t	oei_bulk;
};

struct object_enumerate_out {
	int	 oeo_ret;
	int	 oeo_pad;
	daos_hash_out_t oeo_anchor;
	struct dtp_array oeo_kds;
};

int
dsr_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req);

extern struct daos_rpc dsr_rpcs[];

static inline void
dsr_set_reply_status(dtp_rpc_t *rpc, int status)
{
	int *ret;

	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	D_ASSERT(rpc != NULL);
	ret = dtp_reply_get(rpc);
	D_ASSERT(ret != NULL);
	*ret = status;
}

static inline int
dsr_get_reply_status(dtp_rpc_t *rpc)
{
	int *ret;
	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	ret = dtp_reply_get(rpc);
	return *ret;
}

#endif /* __DSR_RPC_H__ */
