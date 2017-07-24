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
 * removed crt's serialization process.
 *
 */

#ifndef __DAOS_OBJ_RPC_H__
#define __DAOS_OBJ_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/event.h>
#include <daos/rpc.h>

#define OBJ_BULK_LIMIT	(4 * 1024) /* 4KB bytes */

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
enum obj_rpc_opc {
	DAOS_OBJ_RPC_UPDATE	= 1,
	DAOS_OBJ_RPC_FETCH	= 2,
	DAOS_OBJ_DKEY_RPC_ENUMERATE = 3,
	DAOS_OBJ_AKEY_RPC_ENUMERATE = 4,
	DAOS_OBJ_RECX_RPC_ENUMERATE = 5,
};

struct obj_rw_in {
	daos_unit_oid_t		orw_oid;
	uuid_t			orw_co_hdl;
	uuid_t			orw_co_uuid;
	uint64_t		orw_epoch;
	uint32_t		orw_map_ver;
	uint32_t		orw_nr;
	daos_key_t		orw_dkey;
	struct crt_array	orw_iods;
	struct crt_array	orw_sgls;
	struct crt_array	orw_bulks;
};

/* reply for update/fetch */
struct obj_rw_out {
	int32_t			orw_ret;
	uint32_t		orw_map_version;
	struct crt_array	orw_sizes;
	struct crt_array	orw_nrs;
	struct crt_array	orw_sgls;
};

/* object Enumerate in/out */
struct obj_key_enum_in {
	daos_unit_oid_t		oei_oid;
	uuid_t			oei_co_hdl;
	uuid_t			oei_co_uuid;
	uint64_t		oei_epoch;
	uint32_t		oei_map_ver;
	uint32_t		oei_nr;
	uint32_t		oei_rec_type;
	uint32_t		oei_pad;
	daos_key_t		oei_dkey;
	daos_key_t		oei_akey;
	daos_hash_out_t		oei_anchor;
	daos_sg_list_t		oei_sgl;
	crt_bulk_t		oei_bulk;
};

struct obj_key_enum_out {
	int32_t			oeo_ret;
	uint32_t		oeo_map_version;
	daos_hash_out_t		oeo_anchor;
	daos_size_t		oeo_size;
	struct crt_array	oeo_kds;
	struct crt_array	oeo_recxs;
	struct crt_array	oeo_eprs;
	struct crt_array	oeo_cookies;
	struct crt_array	oeo_vers;
	daos_sg_list_t		oeo_sgl;
};

extern struct daos_rpc daos_obj_rpcs[];

int obj_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
		   crt_opcode_t opc, crt_rpc_t **req);
void obj_reply_set_status(crt_rpc_t *rpc, int status);
int obj_reply_get_status(crt_rpc_t *rpc);
void obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version);
uint32_t obj_reply_map_version_get(crt_rpc_t *rpc);

#endif /* __DAOS_OBJ_RPC_H__ */
