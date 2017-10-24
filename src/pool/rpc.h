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
 * dc_pool, ds_pool: RPC Protocol Definitions
 *
 * This is naturally shared by both dc_pool and ds_pool. The in and out data
 * structures must be absent of any compiler-generated paddings.
 */

#ifndef __POOL_RPC_H__
#define __POOL_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
enum pool_operation {
	POOL_CREATE		= 1,
	POOL_DESTROY		= 2,
	POOL_CONNECT		= 3,
	POOL_DISCONNECT		= 4,
	POOL_QUERY		= 5,
	POOL_EXCLUDE		= 6,
	POOL_EVICT		= 7,
	POOL_ADD		= 8,
	POOL_EXCLUDE_OUT	= 9,
	POOL_SVC_STOP		= 10,

	POOL_TGT_CONNECT	= 11,
	POOL_TGT_DISCONNECT	= 12,
	POOL_TGT_UPDATE_MAP	= 13
};

struct pool_op_in {
	uuid_t	pi_uuid;	/* pool UUID */
	uuid_t	pi_hdl;		/* pool handle UUID */
};

struct pool_op_out {
	int32_t			po_rc;		/* operation return code */
	uint32_t		po_map_version;	/* latest map version or zero */
	struct rsvc_hint	po_hint;	/* leadership info */
};

struct pool_create_in {
	struct pool_op_in	pri_op;		/* .pi_hdl unused */
	uint32_t		pri_uid;
	uint32_t		pri_gid;
	uint32_t		pri_mode;
	uint32_t		pri_ntgts;
	struct crt_array	pri_tgt_uuids;	/* [pri_ntgts] */
	d_rank_list_t       *pri_tgt_ranks;	/* [pri_ntgts] */
	uint32_t		pri_ndomains;
	uint32_t		pri_padding;
	struct crt_array	pri_domains;	/* [pri_ndomains] */
};

struct pool_create_out {
	struct pool_op_out	pro_op;	/* .map_version unused */
};

struct pool_connect_in {
	struct pool_op_in	pci_op;
	uint32_t		pci_uid;
	uint32_t		pci_gid;
	uint64_t		pci_capas;
	crt_bulk_t		pci_map_bulk;
};

struct pool_connect_out {
	struct pool_op_out	pco_op;
	uint32_t		pco_mode;
	uint32_t		pco_map_buf_size;   /* only set on -DER_TRUNC */
};

struct pool_disconnect_in {
	struct pool_op_in	pdi_op;
};

struct pool_disconnect_out {
	struct pool_op_out	pdo_op;	/* .po_map_version not set */
};

struct pool_query_in {
	struct pool_op_in	pqi_op;
	crt_bulk_t		pqi_map_bulk;
};

struct pool_query_out {
	struct pool_op_out	pqo_op;
	uint32_t		pqo_mode;
	uint32_t		pqo_map_buf_size;   /* only set on -DER_TRUNC */
	struct daos_rebuild_status pqo_rebuild_st;
};

struct pool_tgt_update_in {
	struct pool_op_in	pti_op;		/* .pi_hdl unused */
	d_rank_list_t       *pti_targets;
};

struct pool_tgt_update_out {
	struct pool_op_out	pto_op;
	d_rank_list_t       *pto_targets;	/* that are not found in pool */
};

struct pool_evict_in {
	struct pool_op_in	pvi_op;	/* .pi_hdl unused */
};

struct pool_evict_out {
	struct pool_op_out	pvo_op;
};

struct pool_svc_stop_in {
	struct pool_op_in	psi_op;	/* .pi_hdl unused */
};

struct pool_svc_stop_out {
	struct pool_op_out	pso_op;	/* .po_map_version unused */
};

struct pool_tgt_connect_in {
	uuid_t		tci_uuid;		/* pool UUID */
	uuid_t		tci_hdl;
	uint64_t	tci_capas;
	uint32_t	tci_map_version;
};

struct pool_tgt_connect_out {
	int32_t	tco_rc;	/* number of errors */
};

struct pool_tgt_disconnect_in {
	uuid_t			tdi_uuid;	/* pool UUID */
	struct crt_array	tdi_hdls;
};

struct pool_tgt_disconnect_out {
	int32_t	tdo_rc;	/* number of errors */
};

struct pool_tgt_update_map_in {
	uuid_t		tui_uuid;		/* pool UUID */
	uint32_t	tui_map_version;
};

struct pool_tgt_update_map_out {
	int32_t	tuo_rc;	/* number of errors */
};

int pool_req_create(crt_context_t dtp_ctx, crt_endpoint_t *tgt_ep,
		    crt_opcode_t opc, crt_rpc_t **req);

extern struct daos_rpc pool_rpcs[];
extern struct daos_rpc pool_srv_rpcs[];

#endif /* __POOL_RPC_H__ */
