/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define DAOS_POOL_VERSION 1
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define POOL_PROTO_CLI_RPC_LIST						\
	X(POOL_CREATE,							\
		0, &DQF_POOL_CREATE,					\
		ds_pool_create_handler, NULL),				\
	X(POOL_CONNECT,							\
		0, &DQF_POOL_CONNECT,					\
		ds_pool_connect_handler, NULL),				\
	X(POOL_DISCONNECT,						\
		0, &DQF_POOL_DISCONNECT,				\
		ds_pool_disconnect_handler, NULL),			\
	X(POOL_QUERY,							\
		0, &DQF_POOL_QUERY,					\
		ds_pool_query_handler, NULL),				\
	X(POOL_EXCLUDE,							\
		0, &DQF_POOL_EXCLUDE,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_EVICT,							\
		0, &DQF_POOL_EVICT,					\
		ds_pool_evict_handler, NULL),				\
	X(POOL_ADD,							\
		0, &DQF_POOL_ADD,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_EXCLUDE_OUT,						\
		0, &DQF_POOL_EXCLUDE_OUT,				\
		ds_pool_update_handler, NULL),				\
	X(POOL_SVC_STOP,						\
		0, &DQF_POOL_SVC_STOP,					\
		ds_pool_svc_stop_handler, NULL),			\
	X(POOL_ATTR_LIST,						\
		0, &DQF_POOL_ATTR_LIST,					\
		ds_pool_attr_list_handler, NULL),			\
	X(POOL_ATTR_GET,						\
		0, &DQF_POOL_ATTR_GET,					\
		ds_pool_attr_get_handler, NULL),			\
	X(POOL_ATTR_SET,						\
		0, &DQF_POOL_ATTR_SET,					\
		ds_pool_attr_set_handler, NULL)

#define POOL_PROTO_SRV_RPC_LIST						\
	X(POOL_TGT_CONNECT,						\
		0, &DQF_POOL_TGT_CONNECT,				\
		ds_pool_tgt_connect_handler,				\
		&ds_pool_tgt_connect_co_ops),				\
	X(POOL_TGT_DISCONNECT,						\
		0, &DQF_POOL_TGT_DISCONNECT,				\
		ds_pool_tgt_disconnect_handler,				\
		&ds_pool_tgt_disconnect_co_ops),			\
	X(POOL_TGT_UPDATE_MAP,						\
		0, &DQF_POOL_TGT_UPDATE_MAP,				\
		ds_pool_tgt_update_map_handler,				\
		&ds_pool_tgt_update_map_co_ops),			\
	X(POOL_RDB_START,						\
		0, &DQF_POOL_RDB_START,					\
		ds_pool_rdb_start_handler,				\
		&ds_pool_rdb_start_co_ops),				\
	X(POOL_RDB_STOP,						\
		0, &DQF_POOL_RDB_STOP,					\
		ds_pool_rdb_stop_handler,				\
		&ds_pool_rdb_stop_co_ops)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum pool_operation {
	POOL_PROTO_CLI_RPC_LIST,
	POOL_PROTO_CLI_COUNT,
	POOL_PROTO_CLI_LAST = POOL_PROTO_CLI_COUNT - 1,
	POOL_PROTO_SRV_RPC_LIST,
};

#undef X

extern struct crt_proto_format pool_proto_fmt;

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
	struct pool_op_out		pco_op;
	uint32_t			pco_uid;
	uint32_t			pco_gid;
	uint32_t			pco_mode;
	/* only set on -DER_TRUNC */
	uint32_t			pco_map_buf_size;
	struct daos_rebuild_status	pco_rebuild_st;
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
	struct pool_op_out		pqo_op;
	uint32_t			pqo_uid;
	uint32_t			pqo_gid;
	uint32_t			pqo_mode;
	/* only set on -DER_TRUNC */
	uint32_t			pqo_map_buf_size;
	struct daos_rebuild_status	pqo_rebuild_st;
};

struct pool_attr_list_in {
	struct pool_op_in	pali_op;
	crt_bulk_t		pali_bulk;
};

struct pool_attr_list_out {
	struct pool_op_out	palo_op;
	uint64_t		palo_size;
};

struct pool_attr_get_in {
	struct pool_op_in	pagi_op;
	uint64_t		pagi_count;
	uint64_t		pagi_key_length;
	crt_bulk_t		pagi_bulk;
};

struct pool_attr_set_in {
	struct pool_op_in	pasi_op;
	uint64_t		pasi_count;
	crt_bulk_t		pasi_bulk;
};

/** Target address for each pool target */
struct pool_target_addr {
	/** rank of the node where the target resides */
	d_rank_t	pta_rank;
	/** target index in each node */
	uint32_t	pta_target;
};

struct pool_target_addr_list {
	int			pta_number;
	struct pool_target_addr	*pta_addrs;
};

struct pool_tgt_update_in {
	struct pool_op_in	pti_op;		/* .pi_hdl unused */
	struct crt_array	pti_addr_list; /* addr list */
};

struct pool_tgt_update_out {
	struct pool_op_out	pto_op;
	struct crt_array	pto_addr_list;
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
	uint32_t	tci_iv_ns_id;
	uint32_t	tci_master_rank;
	uint32_t	tci_pad;
	daos_iov_t	tci_iv_ctxt;
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

struct pool_rdb_start_in {
	uuid_t		 dai_dbid;
	uuid_t		 dai_pool;
	uint32_t	 dai_flags;
	uint32_t	 dai_padding;
	uint64_t	 dai_size;
	d_rank_list_t	*dai_ranks;
};

struct pool_rdb_start_out {
	int		dao_rc;
};

struct pool_rdb_stop_in {
	uuid_t		doi_pool;
	uint32_t	doi_flags;
};

struct pool_rdb_stop_out {
	int		doo_rc;
};

static inline int
pool_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, DAOS_POOL_VERSION);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

int
pool_target_addr_list_alloc(unsigned int num,
			    struct pool_target_addr_list *list);
int
pool_target_addr_list_append(struct pool_target_addr_list *dst_list,
			     struct pool_target_addr *src);
void
pool_target_addr_list_free(struct pool_target_addr_list *list);

#endif /* __POOL_RPC_H__ */
