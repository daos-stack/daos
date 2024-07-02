/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
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
#include <daos/pool_map.h>
#include <daos/pool.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_POOL_VERSION              6
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */

#define POOL_PROTO_VER_WITH_SVC_OP_KEY 6

#define POOL_PROTO_CLI_RPC_LIST(ver)                                                               \
	X(POOL_CREATE, 0, &CQF_pool_create, ds_pool_create_handler, NULL)                          \
	X(POOL_CONNECT, 0, ver >= 6 ? &CQF_pool_connect_v6 : &CQF_pool_connect,                    \
	  ver >= 6 ? ds_pool_connect_handler_v6 : ds_pool_connect_handler_v5, NULL)                \
	X(POOL_DISCONNECT, 0, ver >= 6 ? &CQF_pool_disconnect_v6 : &CQF_pool_disconnect,           \
	  ver >= 6 ? ds_pool_disconnect_handler_v6 : ds_pool_disconnect_handler_v5, NULL)          \
	X(POOL_QUERY, 0, ver >= 6 ? &CQF_pool_query_v6 : &CQF_pool_query,                          \
	  ver >= 6 ? ds_pool_query_handler_v6 : ds_pool_query_handler_v5, NULL)                    \
	X(POOL_QUERY_INFO, 0, ver >= 6 ? &CQF_pool_query_info_v6 : &CQF_pool_query_info,           \
	  ver >= 6 ? ds_pool_query_info_handler_v6 : ds_pool_query_info_handler_v5, NULL)          \
	X(POOL_EXCLUDE, 0, ver >= 6 ? &CQF_pool_exclude_v6 : &CQF_pool_exclude,                    \
	  ver >= 6 ? ds_pool_update_handler_v6 : ds_pool_update_handler_v5, NULL)                  \
	X(POOL_DRAIN, 0, ver >= 6 ? &CQF_pool_drain_v6 : &CQF_pool_drain,                          \
	  ver >= 6 ? ds_pool_update_handler_v6 : ds_pool_update_handler_v5, NULL)                  \
	X(POOL_EXTEND, 0, &CQF_pool_extend, ds_pool_extend_handler, NULL)                          \
	X(POOL_EVICT, 0, &CQF_pool_evict, ds_pool_evict_handler, NULL)                             \
	X(POOL_REINT, 0, ver >= 6 ? &CQF_pool_add_v6 : &CQF_pool_add,                              \
	  ver >= 6 ? ds_pool_update_handler_v6 : ds_pool_update_handler_v5, NULL)                  \
	X(POOL_ADD_IN, 0, ver >= 6 ? &CQF_pool_add_in_v6 : &CQF_pool_add_in,                       \
	  ver >= 6 ? ds_pool_update_handler_v6 : ds_pool_update_handler_v5, NULL)                  \
	X(POOL_EXCLUDE_OUT, 0, ver >= 6 ? &CQF_pool_exclude_out_v6 : &CQF_pool_exclude_out,        \
	  ver >= 6 ? ds_pool_update_handler_v6 : ds_pool_update_handler_v5, NULL)                  \
	X(POOL_SVC_STOP, 0, ver >= 6 ? &CQF_pool_svc_stop_v6 : &CQF_pool_svc_stop,                 \
	  ver >= 6 ? ds_pool_svc_stop_handler_v6 : ds_pool_svc_stop_handler_v5, NULL)              \
	X(POOL_ATTR_LIST, 0, ver >= 6 ? &CQF_pool_attr_list_v6 : &CQF_pool_attr_list,              \
	  ver >= 6 ? ds_pool_attr_list_handler_v6 : ds_pool_attr_list_handler_v5, NULL)            \
	X(POOL_ATTR_GET, 0, ver >= 6 ? &CQF_pool_attr_get_v6 : &CQF_pool_attr_get,                 \
	  ver >= 6 ? ds_pool_attr_get_handler_v6 : ds_pool_attr_get_handler_v5, NULL)              \
	X(POOL_ATTR_SET, 0, ver >= 6 ? &CQF_pool_attr_set_v6 : &CQF_pool_attr_set,                 \
	  ver >= 6 ? ds_pool_attr_set_handler_v6 : ds_pool_attr_set_handler_v5, NULL)              \
	X(POOL_ATTR_DEL, 0, ver >= 6 ? &CQF_pool_attr_del_v6 : &CQF_pool_attr_del,                 \
	  ver >= 6 ? ds_pool_attr_del_handler_v6 : ds_pool_attr_del_handler_v5, NULL)              \
	X(POOL_REPLICAS_ADD, 0, &CQF_pool_replicas_add, ds_pool_replicas_update_handler, NULL)     \
	X(POOL_REPLICAS_REMOVE, 0, &CQF_pool_replicas_remove, ds_pool_replicas_update_handler,     \
	  NULL)                                                                                    \
	X(POOL_LIST_CONT, 0, ver >= 6 ? &CQF_pool_list_cont_v6 : &CQF_pool_list_cont,              \
	  ver >= 6 ? ds_pool_list_cont_handler_v6 : ds_pool_list_cont_handler_v5, NULL)            \
	X(POOL_TGT_QUERY_MAP, 0, &CQF_pool_tgt_query_map, ds_pool_tgt_query_map_handler, NULL)     \
	X(POOL_FILTER_CONT, 0, ver >= 6 ? &CQF_pool_filter_cont_v6 : &CQF_pool_filter_cont,        \
	  ver >= 6 ? ds_pool_filter_cont_handler_v6 : ds_pool_filter_cont_handler_v5, NULL)        \
	X(POOL_TGT_WARMUP, 0, &CQF_pool_tgt_warmup, ds_pool_tgt_warmup_handler, NULL)

#define POOL_PROTO_SRV_RPC_LIST                                                                    \
	X(POOL_TGT_DISCONNECT, 0, &CQF_pool_tgt_disconnect, ds_pool_tgt_disconnect_handler,        \
	  &ds_pool_tgt_disconnect_co_ops)                                                          \
	X(POOL_TGT_QUERY, 0, &CQF_pool_tgt_query, ds_pool_tgt_query_handler,                       \
	  &ds_pool_tgt_query_co_ops)                                                               \
	X(POOL_PROP_GET, 0, &CQF_pool_prop_get, ds_pool_prop_get_handler, NULL)                    \
	X(POOL_ADD_TGT, 0, &CQF_pool_add_v6, ds_pool_update_handler_v6, NULL)                      \
	X(POOL_PROP_SET, 0, &CQF_pool_prop_set, ds_pool_prop_set_handler, NULL)                    \
	X(POOL_ACL_UPDATE, 0, &CQF_pool_acl_update, ds_pool_acl_update_handler, NULL)              \
	X(POOL_ACL_DELETE, 0, &CQF_pool_acl_delete, ds_pool_acl_delete_handler, NULL)              \
	X(POOL_RANKS_GET, 0, &CQF_pool_ranks_get, ds_pool_ranks_get_handler, NULL)                 \
	X(POOL_UPGRADE, 0, &CQF_pool_upgrade, ds_pool_upgrade_handler, NULL)                       \
	X(POOL_TGT_DISCARD, 0, &CQF_pool_tgt_discard, ds_pool_tgt_discard_handler, NULL)

#define POOL_PROTO_RPC_LIST									\
	POOL_PROTO_CLI_RPC_LIST(DAOS_POOL_VERSION)						\
	POOL_PROTO_SRV_RPC_LIST

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a,

enum pool_operation {
	/* This list must stay consistent with POOL_PROTO_RPC_LIST. */
	POOL_PROTO_CLI_RPC_LIST(DAOS_POOL_VERSION)
	POOL_PROTO_CLI_COUNT,
	POOL_PROTO_CLI_LAST = POOL_PROTO_CLI_COUNT - 1,
	POOL_PROTO_SRV_RPC_LIST
};

#undef X

char *dc_pool_op_str(enum pool_operation op);

extern struct crt_proto_format pool_proto_fmt_v5;
extern struct crt_proto_format pool_proto_fmt_v6;
extern int dc_pool_proto_version;

/* clang-format off */

#define DAOS_ISEQ_POOL_OP	/* input fields */		 \
	((uuid_t)		(pi_uuid)		CRT_VAR) \
	((uuid_t)		(pi_hdl)		CRT_VAR)

#define DAOS_ISEQ_POOL_OP_V6 /* input fields */                                                    \
	((uuid_t)(pi_uuid)CRT_VAR)((uuid_t)(pi_hdl)CRT_VAR)((uuid_t)(pi_cli_id)CRT_VAR)(           \
	    (uint64_t)(pi_time)CRT_VAR)

#define DAOS_OSEQ_POOL_OP	/* output fields */		 \
	((int32_t)		(po_rc)			CRT_VAR) \
	((uint32_t)		(po_map_version)	CRT_VAR) \
	((struct rsvc_hint)	(po_hint)		CRT_VAR)

CRT_RPC_DECLARE(pool_op_v6, DAOS_ISEQ_POOL_OP_V6, DAOS_OSEQ_POOL_OP)
CRT_RPC_DECLARE(pool_op, DAOS_ISEQ_POOL_OP, DAOS_OSEQ_POOL_OP)

/* If pri_op.pi_hdl is not null, call rdb_campaign. */
#define DAOS_ISEQ_POOL_CREATE	/* input fields */			\
	((struct pool_op_v6_in)	(pri_op)		CRT_VAR)	\
	((d_rank_list_t)	(pri_tgt_ranks)		CRT_PTR)	\
	((daos_prop_t)		(pri_prop)		CRT_PTR)	\
	((uint32_t)		(pri_ndomains)		CRT_VAR)	\
	((uint32_t)		(pri_ntgts)		CRT_VAR)	\
	((uint32_t)		(pri_domains)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_CREATE	/* output fields */		 \
	((struct pool_op_out)	(pro_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_create, DAOS_ISEQ_POOL_CREATE, DAOS_OSEQ_POOL_CREATE)

/* clang-format on */
enum map_update_opc {
	MAP_EXCLUDE = 0,
	MAP_DRAIN,
	MAP_REINT,
	MAP_EXTEND,
	MAP_ADD_IN,
	MAP_EXCLUDE_OUT,
	MAP_FINISH_REBUILD,
	MAP_REVERT_REBUILD,
};

static inline uint32_t
pool_opc_2map_opc(uint32_t pool_opc)
{
	uint32_t opc = 0;

	switch(pool_opc) {
	case POOL_EXCLUDE:
		opc = MAP_EXCLUDE;
		break;
	case POOL_DRAIN:
		opc = MAP_DRAIN;
		break;
	case POOL_REINT:
		opc = MAP_REINT;
		break;
	case POOL_EXTEND:
		opc = MAP_EXTEND;
		break;
	case POOL_ADD_IN:
		opc = MAP_ADD_IN;
		break;
	case POOL_EXCLUDE_OUT:
		opc = MAP_EXCLUDE_OUT;
		break;
	default:
		D_ASSERTF(false, "invalid opc: 0x%x\n", pool_opc);
		break;
	}

	return opc;
}

static inline void
pool_create_in_get_data(crt_rpc_t *rpc, d_rank_list_t **pri_tgt_ranksp, daos_prop_t **pri_propp,
			uint32_t *pri_ndomainsp, uint32_t *pri_ntgtsp, uint32_t **pri_domainsp)
{
	struct pool_create_in *in      = crt_req_get(rpc);
	uint8_t                rpc_ver = opc_get_rpc_ver(rpc->cr_opc);

	D_ASSERT(rpc_ver >= POOL_PROTO_VER_WITH_SVC_OP_KEY);
	*pri_tgt_ranksp = in->pri_tgt_ranks;
	*pri_propp      = in->pri_prop;
	*pri_ndomainsp  = in->pri_ndomains;
	*pri_ntgtsp     = in->pri_ntgts;
	*pri_domainsp   = in->pri_domains.ca_arrays;
	D_ASSERT(*pri_ndomainsp == in->pri_domains.ca_count);
}

static inline void
pool_create_in_set_data(crt_rpc_t *rpc, d_rank_list_t *pri_tgt_ranks, daos_prop_t *pri_prop,
			uint32_t pri_ndomains, uint32_t pri_ntgts, uint32_t *pri_domains)
{
	struct pool_create_in *in      = crt_req_get(rpc);
	uint8_t                rpc_ver = opc_get_rpc_ver(rpc->cr_opc);

	D_ASSERT(rpc_ver >= POOL_PROTO_VER_WITH_SVC_OP_KEY);
	in->pri_tgt_ranks         = pri_tgt_ranks;
	in->pri_prop              = pri_prop;
	in->pri_ndomains          = pri_ndomains;
	in->pri_ntgts             = pri_ntgts;
	in->pri_domains.ca_arrays = pri_domains;
	in->pri_domains.ca_count  = pri_ndomains;
}

/* clang-format off */

#define DAOS_OSEQ_POOL_CONNECT		/* output fields */		 \
	((struct pool_op_out)		(pco_op)		CRT_VAR) \
	((struct daos_pool_space)	(pco_space)		CRT_RAW) \
	((struct daos_rebuild_status)	(pco_rebuild_st)	CRT_RAW) \
	/* only set on -DER_TRUNC */					 \
	((uint32_t)			(pco_map_buf_size)	CRT_VAR)

#define DAOS_ISEQ_POOL_CONNECT 		/* input fields */		 \
	((struct pool_op_in)		(pci_op)		CRT_VAR) \
	((d_iov_t)			(pci_cred)		CRT_VAR) \
	((uint64_t)			(pci_flags)		CRT_VAR) \
	((uint64_t)			(pci_query_bits)	CRT_VAR) \
	((crt_bulk_t)			(pci_map_bulk)		CRT_VAR) \
	((uint32_t)			(pci_pool_version)	CRT_VAR)

#define DAOS_ISEQ_POOL_CONNECT_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pci_op)		CRT_VAR) \
	((d_iov_t)			(pci_cred)		CRT_VAR) \
	((uint64_t)			(pci_flags)		CRT_VAR) \
	((uint64_t)			(pci_query_bits)	CRT_VAR) \
	((crt_bulk_t)			(pci_map_bulk)		CRT_VAR) \
	((uint32_t)			(pci_pool_version)	CRT_VAR)

CRT_RPC_DECLARE(pool_connect, DAOS_ISEQ_POOL_CONNECT, DAOS_OSEQ_POOL_CONNECT)
CRT_RPC_DECLARE(pool_connect_v6, DAOS_ISEQ_POOL_CONNECT_V6, DAOS_OSEQ_POOL_CONNECT)

/* clang-format on */

static inline bool
rpc_ver_atleast(crt_rpc_t *rpc, int min_ver)
{
	return (opc_get_rpc_ver(rpc->cr_opc) >= min_ver);
}

static inline void
pool_connect_in_get_cred(crt_rpc_t *rpc, d_iov_t **pci_credp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY))
		*pci_credp = &(((struct pool_connect_v6_in *)in)->pci_cred);
	else
		*pci_credp = &(((struct pool_connect_in *)in)->pci_cred);
}

static inline void
pool_connect_in_get_data(crt_rpc_t *rpc, uint64_t *pci_flagsp, uint64_t *pci_query_bitsp,
			 crt_bulk_t *pci_map_bulkp, uint32_t *pci_pool_versionp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		if (pci_flagsp)
			*pci_flagsp = ((struct pool_connect_v6_in *)in)->pci_flags;
		if (pci_query_bitsp)
			*pci_query_bitsp = ((struct pool_connect_v6_in *)in)->pci_query_bits;
		if (pci_map_bulkp)
			*pci_map_bulkp = ((struct pool_connect_v6_in *)in)->pci_map_bulk;
		if (pci_pool_versionp)
			*pci_pool_versionp = ((struct pool_connect_v6_in *)in)->pci_pool_version;
	} else {
		if (pci_flagsp)
			*pci_flagsp = ((struct pool_connect_in *)in)->pci_flags;
		if (pci_query_bitsp)
			*pci_query_bitsp = ((struct pool_connect_in *)in)->pci_query_bits;
		if (pci_map_bulkp)
			*pci_map_bulkp = ((struct pool_connect_in *)in)->pci_map_bulk;
		if (pci_pool_versionp)
			*pci_pool_versionp = ((struct pool_connect_in *)in)->pci_pool_version;
	}
}

static inline void
pool_connect_in_set_data(crt_rpc_t *rpc, uint64_t pci_flags, uint64_t pci_query_bits,
			 crt_bulk_t pci_map_bulk, uint32_t pci_pool_version)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_connect_v6_in *)in)->pci_flags        = pci_flags;
		((struct pool_connect_v6_in *)in)->pci_query_bits   = pci_query_bits;
		((struct pool_connect_v6_in *)in)->pci_map_bulk     = pci_map_bulk;
		((struct pool_connect_v6_in *)in)->pci_pool_version = pci_pool_version;
	} else {
		((struct pool_connect_in *)in)->pci_flags        = pci_flags;
		((struct pool_connect_in *)in)->pci_query_bits   = pci_query_bits;
		((struct pool_connect_in *)in)->pci_map_bulk     = pci_map_bulk;
		((struct pool_connect_in *)in)->pci_pool_version = pci_pool_version;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_DISCONNECT	/* input fields */		 \
	((struct pool_op_in)		(pdi_op)		CRT_VAR)

#define DAOS_ISEQ_POOL_DISCONNECT_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pdi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_DISCONNECT	/* output fields */		 \
	((struct pool_op_out)		(pdo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_disconnect, DAOS_ISEQ_POOL_DISCONNECT, DAOS_OSEQ_POOL_DISCONNECT)
CRT_RPC_DECLARE(pool_disconnect_v6, DAOS_ISEQ_POOL_DISCONNECT_V6, DAOS_OSEQ_POOL_DISCONNECT)

#define DAOS_ISEQ_POOL_QUERY		/* input fields */			 \
	((struct pool_op_in)		(pqi_op)			CRT_VAR) \
	((crt_bulk_t)			(pqi_map_bulk)			CRT_VAR) \
	((uint64_t)			(pqi_query_bits)		CRT_VAR)

#define DAOS_ISEQ_POOL_QUERY_V6		/* input fields */			 \
	((struct pool_op_v6_in)		(pqi_op)			CRT_VAR) \
	((crt_bulk_t)			(pqi_map_bulk)			CRT_VAR) \
	((uint64_t)			(pqi_query_bits)		CRT_VAR)

#define DAOS_OSEQ_POOL_QUERY		/* output fields */			 \
	((struct pool_op_out)		(pqo_op)			CRT_VAR) \
	((daos_prop_t)			(pqo_prop)			CRT_PTR) \
	((struct daos_pool_space)	(pqo_space)			CRT_RAW) \
	((struct daos_rebuild_status)	(pqo_rebuild_st)		CRT_RAW) \
	/* only set on -DER_TRUNC */						 \
	((uint32_t)			(pqo_map_buf_size)		CRT_VAR) \
	((uint32_t)			(pqo_pool_layout_ver)		CRT_VAR) \
	((uint32_t)			(pqo_upgrade_layout_ver)	CRT_VAR)

CRT_RPC_DECLARE(pool_query, DAOS_ISEQ_POOL_QUERY, DAOS_OSEQ_POOL_QUERY)
CRT_RPC_DECLARE(pool_query_v6, DAOS_ISEQ_POOL_QUERY_V6, DAOS_OSEQ_POOL_QUERY)

/* clang-format on */

static inline void
pool_query_in_get_data(crt_rpc_t *rpc, crt_bulk_t *pqi_map_bulkp, uint64_t *pqi_query_bitsp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pqi_map_bulkp   = ((struct pool_query_v6_in *)in)->pqi_map_bulk;
		*pqi_query_bitsp = ((struct pool_query_v6_in *)in)->pqi_query_bits;
	} else {
		*pqi_map_bulkp   = ((struct pool_query_in *)in)->pqi_map_bulk;
		*pqi_query_bitsp = ((struct pool_query_in *)in)->pqi_query_bits;
	}
}

static inline void
pool_query_in_set_data(crt_rpc_t *rpc, crt_bulk_t pqi_map_bulk, uint64_t pqi_query_bits)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_query_v6_in *)in)->pqi_map_bulk   = pqi_map_bulk;
		((struct pool_query_v6_in *)in)->pqi_query_bits = pqi_query_bits;
	} else {
		((struct pool_query_in *)in)->pqi_map_bulk   = pqi_map_bulk;
		((struct pool_query_in *)in)->pqi_query_bits = pqi_query_bits;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_QUERY_INFO	/* input fields */		 \
	((struct pool_op_in)		(pqii_op)		CRT_VAR) \
	((d_rank_t)			(pqii_rank)		CRT_VAR) \
	((uint32_t)			(pqii_tgt)		CRT_VAR)

#define DAOS_ISEQ_POOL_QUERY_INFO_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pqii_op)		CRT_VAR) \
	((d_rank_t)			(pqii_rank)		CRT_VAR) \
	((uint32_t)			(pqii_tgt)		CRT_VAR)

#define DAOS_OSEQ_POOL_QUERY_INFO	/* output fields */		 \
	((struct pool_op_out)		(pqio_op)		CRT_VAR) \
	((d_rank_t)			(pqio_rank)		CRT_VAR) \
	((uint32_t)			(pqio_tgt)		CRT_VAR) \
	((struct daos_space)		(pqio_space)		CRT_RAW) \
	((daos_target_state_t)		(pqio_state)		CRT_VAR)

CRT_RPC_DECLARE(pool_query_info, DAOS_ISEQ_POOL_QUERY_INFO, DAOS_OSEQ_POOL_QUERY_INFO)
CRT_RPC_DECLARE(pool_query_info_v6, DAOS_ISEQ_POOL_QUERY_INFO_V6, DAOS_OSEQ_POOL_QUERY_INFO)

/* clang-format on */

static inline void
pool_query_info_in_get_data(crt_rpc_t *rpc, d_rank_t *pqii_rankp, uint32_t *pqii_tgtp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pqii_rankp = ((struct pool_query_info_v6_in *)in)->pqii_rank;
		*pqii_tgtp  = ((struct pool_query_info_v6_in *)in)->pqii_tgt;
	} else {
		*pqii_rankp = ((struct pool_query_info_in *)in)->pqii_rank;
		*pqii_tgtp  = ((struct pool_query_info_in *)in)->pqii_tgt;
	}
}

static inline void
pool_query_info_in_set_data(crt_rpc_t *rpc, d_rank_t pqii_rank, uint32_t pqii_tgt)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_query_info_v6_in *)in)->pqii_rank = pqii_rank;
		((struct pool_query_info_v6_in *)in)->pqii_tgt  = pqii_tgt;
	} else {
		((struct pool_query_info_in *)in)->pqii_rank = pqii_rank;
		((struct pool_query_info_in *)in)->pqii_tgt  = pqii_tgt;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ATTR_LIST	/* input fields */		 \
	((struct pool_op_in)		(pali_op)		CRT_VAR) \
	((crt_bulk_t)			(pali_bulk)		CRT_VAR)

#define DAOS_ISEQ_POOL_ATTR_LIST_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pali_op)		CRT_VAR) \
	((crt_bulk_t)			(pali_bulk)		CRT_VAR)

#define DAOS_OSEQ_POOL_ATTR_LIST	/* output fields */		 \
	((struct pool_op_out)		(palo_op)		CRT_VAR) \
	((uint64_t)			(palo_size)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_list, DAOS_ISEQ_POOL_ATTR_LIST, DAOS_OSEQ_POOL_ATTR_LIST)
CRT_RPC_DECLARE(pool_attr_list_v6, DAOS_ISEQ_POOL_ATTR_LIST_V6, DAOS_OSEQ_POOL_ATTR_LIST)

#define DAOS_ISEQ_POOL_TGT_WARMUP	/* input fields */	 \
	((crt_bulk_t)		(tw_bulk)		CRT_VAR)
#define DAOS_OSEQ_POOL_TGT_WARMUP

CRT_RPC_DECLARE(pool_tgt_warmup, DAOS_ISEQ_POOL_TGT_WARMUP, DAOS_OSEQ_POOL_TGT_WARMUP)

/* clang-format on */

static inline void
pool_attr_list_in_get_data(crt_rpc_t *rpc, crt_bulk_t *pali_bulkp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pali_bulkp = ((struct pool_attr_list_v6_in *)in)->pali_bulk;
	} else {
		*pali_bulkp = ((struct pool_attr_list_in *)in)->pali_bulk;
	}
}

static inline void
pool_attr_list_in_set_data(crt_rpc_t *rpc, crt_bulk_t pali_bulk)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_attr_list_v6_in *)in)->pali_bulk = pali_bulk;
	} else {
		((struct pool_attr_list_in *)in)->pali_bulk = pali_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ATTR_GET		/* input fields */		 \
	((struct pool_op_in)		(pagi_op)		CRT_VAR) \
	((uint64_t)			(pagi_count)		CRT_VAR) \
	((uint64_t)			(pagi_key_length)	CRT_VAR) \
	((crt_bulk_t)			(pagi_bulk)		CRT_VAR)

#define DAOS_ISEQ_POOL_ATTR_GET_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pagi_op)		CRT_VAR) \
	((uint64_t)			(pagi_count)		CRT_VAR) \
	((uint64_t)			(pagi_key_length)	CRT_VAR) \
	((crt_bulk_t)			(pagi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_get, DAOS_ISEQ_POOL_ATTR_GET, DAOS_OSEQ_POOL_OP)
CRT_RPC_DECLARE(pool_attr_get_v6, DAOS_ISEQ_POOL_ATTR_GET_V6, DAOS_OSEQ_POOL_OP)

/* clang-format on */

static inline void
pool_attr_get_in_get_data(crt_rpc_t *rpc, uint64_t *pagi_countp, uint64_t *pagi_key_lengthp,
			  crt_bulk_t *pagi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pagi_countp      = ((struct pool_attr_get_v6_in *)in)->pagi_count;
		*pagi_key_lengthp = ((struct pool_attr_get_v6_in *)in)->pagi_key_length;
		*pagi_bulkp       = ((struct pool_attr_get_v6_in *)in)->pagi_bulk;
	} else {
		*pagi_countp      = ((struct pool_attr_get_in *)in)->pagi_count;
		*pagi_key_lengthp = ((struct pool_attr_get_in *)in)->pagi_key_length;
		*pagi_bulkp       = ((struct pool_attr_get_in *)in)->pagi_bulk;
	}
}

static inline void
pool_attr_get_in_set_data(crt_rpc_t *rpc, uint64_t pagi_count, uint64_t pagi_key_length,
			  crt_bulk_t pagi_bulk)

{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_attr_get_v6_in *)in)->pagi_count      = pagi_count;
		((struct pool_attr_get_v6_in *)in)->pagi_key_length = pagi_key_length;
		((struct pool_attr_get_v6_in *)in)->pagi_bulk       = pagi_bulk;
	} else {
		((struct pool_attr_get_in *)in)->pagi_count      = pagi_count;
		((struct pool_attr_get_in *)in)->pagi_key_length = pagi_key_length;
		((struct pool_attr_get_in *)in)->pagi_bulk       = pagi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ATTR_SET		/* input fields */		 \
	((struct pool_op_in)		(pasi_op)		CRT_VAR) \
	((uint64_t)			(pasi_count)		CRT_VAR) \
	((crt_bulk_t)			(pasi_bulk)		CRT_VAR)

#define DAOS_ISEQ_POOL_ATTR_SET_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pasi_op)		CRT_VAR) \
	((uint64_t)			(pasi_count)		CRT_VAR) \
	((crt_bulk_t)			(pasi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_set, DAOS_ISEQ_POOL_ATTR_SET, DAOS_OSEQ_POOL_OP)
CRT_RPC_DECLARE(pool_attr_set_v6, DAOS_ISEQ_POOL_ATTR_SET_V6, DAOS_OSEQ_POOL_OP)

/* clang-format on */

static inline void
pool_attr_set_in_get_data(crt_rpc_t *rpc, uint64_t *pasi_countp, crt_bulk_t *pasi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pasi_countp = ((struct pool_attr_set_v6_in *)in)->pasi_count;
		*pasi_bulkp  = ((struct pool_attr_set_v6_in *)in)->pasi_bulk;
	} else {
		*pasi_countp = ((struct pool_attr_set_in *)in)->pasi_count;
		*pasi_bulkp  = ((struct pool_attr_set_in *)in)->pasi_bulk;
	}
}

static inline void
pool_attr_set_in_set_data(crt_rpc_t *rpc, uint64_t pasi_count, crt_bulk_t pasi_bulk)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_attr_set_v6_in *)in)->pasi_count = pasi_count;
		((struct pool_attr_set_v6_in *)in)->pasi_bulk  = pasi_bulk;
	} else {
		((struct pool_attr_set_in *)in)->pasi_count = pasi_count;
		((struct pool_attr_set_in *)in)->pasi_bulk  = pasi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ATTR_DEL		/* input fields */		 \
	((struct pool_op_in)		(padi_op)		CRT_VAR) \
	((uint64_t)			(padi_count)		CRT_VAR) \
	((crt_bulk_t)			(padi_bulk)		CRT_VAR)

#define DAOS_ISEQ_POOL_ATTR_DEL_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(padi_op)		CRT_VAR) \
	((uint64_t)			(padi_count)		CRT_VAR) \
	((crt_bulk_t)			(padi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_del, DAOS_ISEQ_POOL_ATTR_DEL, DAOS_OSEQ_POOL_OP)
CRT_RPC_DECLARE(pool_attr_del_v6, DAOS_ISEQ_POOL_ATTR_DEL_V6, DAOS_OSEQ_POOL_OP)

/* clang-format on */

static inline void
pool_attr_del_in_get_data(crt_rpc_t *rpc, uint64_t *padi_countp, crt_bulk_t *padi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*padi_countp = ((struct pool_attr_del_v6_in *)in)->padi_count;
		*padi_bulkp  = ((struct pool_attr_del_v6_in *)in)->padi_bulk;
	} else {
		*padi_countp = ((struct pool_attr_del_in *)in)->padi_count;
		*padi_bulkp  = ((struct pool_attr_del_in *)in)->padi_bulk;
	}
}

static inline void
pool_attr_del_in_set_data(crt_rpc_t *rpc, uint64_t padi_count, crt_bulk_t padi_bulk)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_attr_del_v6_in *)in)->padi_count = padi_count;
		((struct pool_attr_del_v6_in *)in)->padi_bulk  = padi_bulk;
	} else {
		((struct pool_attr_del_in *)in)->padi_count = padi_count;
		((struct pool_attr_del_in *)in)->padi_bulk  = padi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_MEMBERSHIP	/* input fields */		 \
	((uuid_t)			(pmi_uuid)		CRT_VAR) \
	((d_rank_list_t)		(pmi_targets)		CRT_PTR)

#define DAOS_OSEQ_POOL_MEMBERSHIP	/* output fields */		 \
	((struct rsvc_hint)		(pmo_hint)		CRT_VAR) \
	((d_rank_list_t)		(pmo_failed)		CRT_PTR) \
	((int32_t)			(pmo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_membership, DAOS_ISEQ_POOL_MEMBERSHIP, DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DECLARE(pool_replicas_add, DAOS_ISEQ_POOL_MEMBERSHIP, DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DECLARE(pool_replicas_remove, DAOS_ISEQ_POOL_MEMBERSHIP, DAOS_OSEQ_POOL_MEMBERSHIP)

#define DAOS_ISEQ_POOL_TGT_UPDATE	/* input fields */			\
	((struct pool_op_in)		(pti_op)		CRT_VAR)	\
	((struct pool_target_addr)	(pti_addr_list)		CRT_ARRAY)

#define DAOS_ISEQ_POOL_TGT_UPDATE_V6	/* input fields */			\
	((struct pool_op_v6_in)		(pti_op)		CRT_VAR)	\
	((struct pool_target_addr)	(pti_addr_list)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_TGT_UPDATE	/* output fields */			\
	((struct pool_op_out)		(pto_op)		CRT_VAR)	\
	((struct pool_target_addr)	(pto_addr_list)		CRT_ARRAY)

#define DAOS_ISEQ_POOL_EXTEND_V6	/* input fields */			\
	((struct pool_op_v6_in)		(pei_op)		CRT_VAR)	\
	((d_rank_list_t)		(pei_tgt_ranks)		CRT_PTR)	\
	((uint32_t)			(pei_ntgts)		CRT_VAR)	\
	((uint32_t)			(pei_ndomains)		CRT_VAR)	\
	((uint32_t)			(pei_domains)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_EXTEND		/* output fields */			\
	((struct pool_op_out)		(peo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_update, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_tgt_update_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)

CRT_RPC_DECLARE(pool_extend, DAOS_ISEQ_POOL_EXTEND_V6, DAOS_OSEQ_POOL_EXTEND)
CRT_RPC_DECLARE(pool_add, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_add_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_add_in, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_add_in_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_drain, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_drain_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude_out, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude_out_v6, DAOS_ISEQ_POOL_TGT_UPDATE_V6, DAOS_OSEQ_POOL_TGT_UPDATE)

/* clang-format on */

static inline void
pool_tgt_update_in_get_data(crt_rpc_t *rpc, struct pool_target_addr **pti_addr_listp, int *countp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pti_addr_listp = ((struct pool_tgt_update_v6_in *)in)->pti_addr_list.ca_arrays;
		*countp         = (int)((struct pool_tgt_update_v6_in *)in)->pti_addr_list.ca_count;
	} else {
		*pti_addr_listp = ((struct pool_tgt_update_in *)in)->pti_addr_list.ca_arrays;
		*countp         = (int)((struct pool_tgt_update_in *)in)->pti_addr_list.ca_count;
	}
}

static inline void
pool_tgt_update_in_set_data(crt_rpc_t *rpc, struct pool_target_addr *pti_addr_list, uint64_t count)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_tgt_update_v6_in *)in)->pti_addr_list.ca_arrays = pti_addr_list;
		((struct pool_tgt_update_v6_in *)in)->pti_addr_list.ca_count  = count;
	} else {
		((struct pool_tgt_update_in *)in)->pti_addr_list.ca_arrays = pti_addr_list;
		((struct pool_tgt_update_in *)in)->pti_addr_list.ca_count  = count;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_EVICT_V6	/* input fields */				\
	((struct pool_op_v6_in)	(pvi_op)			CRT_VAR)	\
	((uint32_t)		(pvi_pool_destroy)		CRT_VAR)	\
	((uint32_t)		(pvi_pool_destroy_force)	CRT_VAR)	\
	((uuid_t)		(pvi_hdls)			CRT_ARRAY)	\
	((d_string_t)		(pvi_machine)CRT_VAR)

#define DAOS_OSEQ_POOL_EVICT	/* output fields */				\
	((struct pool_op_out)	(pvo_op)			CRT_VAR)	\
	((uint32_t)		(pvo_n_hdls_evicted)		CRT_VAR)

CRT_RPC_DECLARE(pool_evict, DAOS_ISEQ_POOL_EVICT_V6, DAOS_OSEQ_POOL_EVICT)

#define DAOS_ISEQ_POOL_SVC_STOP		/* input fields */		 \
	((struct pool_op_in)		(psi_op)		CRT_VAR)

#define DAOS_ISEQ_POOL_SVC_STOP_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(psi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_SVC_STOP		/* output fields */		 \
	((struct pool_op_out)		(pso_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_svc_stop, DAOS_ISEQ_POOL_SVC_STOP, DAOS_OSEQ_POOL_SVC_STOP)
CRT_RPC_DECLARE(pool_svc_stop_v6, DAOS_ISEQ_POOL_SVC_STOP_V6, DAOS_OSEQ_POOL_SVC_STOP)

#define DAOS_ISEQ_POOL_TGT_DISCONNECT	/* input fields */			\
	((uuid_t)			(tdi_uuid)		CRT_VAR)	\
	((uuid_t)			(tdi_hdls)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_TGT_DISCONNECT	/* output fields */			\
	((int32_t)			(tdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_disconnect, DAOS_ISEQ_POOL_TGT_DISCONNECT, DAOS_OSEQ_POOL_TGT_DISCONNECT)

#define DAOS_ISEQ_POOL_TGT_QUERY	/* input fields */		 \
	((struct pool_op_in)		(tqi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_TGT_QUERY	/* output fields */		 \
	((struct daos_pool_space)	(tqo_space)		CRT_RAW) \
	((uint32_t)			(tqo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_query, DAOS_ISEQ_POOL_TGT_QUERY, DAOS_OSEQ_POOL_TGT_QUERY)

#define DAOS_ISEQ_POOL_TGT_DIST_HDLS	/* input fields */		 \
	((uuid_t)			(tfi_pool_uuid)		CRT_VAR) \
	((d_iov_t)			(tfi_hdls)		CRT_VAR)

#define DAOS_OSEQ_POOL_TGT_DIST_HDLS	/* output fields */		 \
	((uint32_t)			(tfo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_dist_hdls, DAOS_ISEQ_POOL_TGT_DIST_HDLS, DAOS_OSEQ_POOL_TGT_DIST_HDLS)

#define DAOS_ISEQ_POOL_PROP_GET_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pgi_op)		CRT_VAR) \
	((uint64_t)			(pgi_query_bits)	CRT_VAR)

#define DAOS_OSEQ_POOL_PROP_GET		/* output fields */		 \
	((struct pool_op_out)		(pgo_op)		CRT_VAR) \
	((daos_prop_t)			(pgo_prop)		CRT_PTR)

CRT_RPC_DECLARE(pool_prop_get, DAOS_ISEQ_POOL_PROP_GET_V6, DAOS_OSEQ_POOL_PROP_GET)

/* clang-format on */

static inline void
pool_prop_get_in_get_data(crt_rpc_t *rpc, uint64_t *pgi_query_bitsp)
{
	void *in = crt_req_get(rpc);

	/* engine<->engine RPC, assume same protocol version between them */
	*pgi_query_bitsp = ((struct pool_prop_get_in *)in)->pgi_query_bits;
}

static inline void
pool_prop_get_in_set_data(crt_rpc_t *rpc, uint64_t pgi_query_bits)
{
	void *in = crt_req_get(rpc);

	((struct pool_prop_get_in *)in)->pgi_query_bits = pgi_query_bits;
}

/* clang-format off */

#define DAOS_ISEQ_POOL_PROP_SET_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(psi_op)		CRT_VAR) \
	((daos_prop_t)			(psi_prop)		CRT_PTR)

#define DAOS_OSEQ_POOL_PROP_SET		/* output fields */		 \
	((struct pool_op_out)		(pso_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_prop_set, DAOS_ISEQ_POOL_PROP_SET_V6, DAOS_OSEQ_POOL_PROP_SET)

/* clang-format on */

static inline void
pool_prop_set_in_get_data(crt_rpc_t *rpc, daos_prop_t **psi_propp)
{
	void *in = crt_req_get(rpc);

	/* engine<->engine RPC, assume same protocol version between them */
	*psi_propp = ((struct pool_prop_set_in *)in)->psi_prop;
}

static inline void
pool_prop_set_in_set_data(crt_rpc_t *rpc, daos_prop_t *psi_prop)
{
	void *in = crt_req_get(rpc);

	((struct pool_prop_set_in *)in)->psi_prop = psi_prop;
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ACL_UPDATE_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pui_op)		CRT_VAR) \
	((struct daos_acl)		(pui_acl)		CRT_PTR)

#define DAOS_OSEQ_POOL_ACL_UPDATE	/* output fields */		 \
	((struct pool_op_out)		(puo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_acl_update, DAOS_ISEQ_POOL_ACL_UPDATE_V6, DAOS_OSEQ_POOL_ACL_UPDATE)

/* clang-format on */

static inline void
pool_acl_update_in_get_data(crt_rpc_t *rpc, struct daos_acl **pui_aclp)
{
	void *in = crt_req_get(rpc);

	/* engine<->engine RPC, assume same protocol version between them */
	*pui_aclp = ((struct pool_acl_update_in *)in)->pui_acl;
}

static inline void
pool_acl_update_in_set_data(crt_rpc_t *rpc, struct daos_acl *pui_acl)
{
	void *in = crt_req_get(rpc);

	((struct pool_acl_update_in *)in)->pui_acl = pui_acl;
}

/* clang-format off */

#define DAOS_ISEQ_POOL_ACL_DELETE_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pdi_op)		CRT_VAR) \
	((d_const_string_t)		(pdi_principal)		CRT_VAR) \
	((uint8_t)			(pdi_type)		CRT_VAR)

#define DAOS_OSEQ_POOL_ACL_DELETE	/* output fields */		 \
	((struct pool_op_out)		(pdo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_acl_delete, DAOS_ISEQ_POOL_ACL_DELETE_V6, DAOS_OSEQ_POOL_ACL_DELETE)

/* clang-format on */

static inline void
pool_acl_delete_in_get_data(crt_rpc_t *rpc, d_const_string_t *pdi_principalp, uint8_t *pdi_typep)
{
	void *in = crt_req_get(rpc);

	/* engine<->engine RPC, assume same protocol version between them */
	*pdi_principalp = ((struct pool_acl_delete_in *)in)->pdi_principal;
	*pdi_typep      = ((struct pool_acl_delete_in *)in)->pdi_type;
}

static inline void
pool_acl_delete_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, d_const_string_t pdi_principal,
			    uint8_t pdi_type)
{
	void *in = crt_req_get(rpc);

	((struct pool_acl_delete_in *)in)->pdi_principal = pdi_principal;
	((struct pool_acl_delete_in *)in)->pdi_type      = pdi_type;
}

/* clang-format off */

#define DAOS_ISEQ_POOL_LIST_CONT	/* input fields */		 \
	((struct pool_op_in)		(plci_op)		CRT_VAR) \
	((crt_bulk_t)			(plci_cont_bulk)	CRT_VAR) \
	((uint64_t)			(plci_ncont)		CRT_VAR)

#define DAOS_ISEQ_POOL_LIST_CONT_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(plci_op)		CRT_VAR) \
	((crt_bulk_t)			(plci_cont_bulk)	CRT_VAR) \
	((uint64_t)			(plci_ncont)		CRT_VAR)

#define DAOS_OSEQ_POOL_LIST_CONT	/* output fields */		 \
	((struct pool_op_out)		(plco_op)		CRT_VAR) \
	((uint64_t)			(plco_ncont)		CRT_VAR)

CRT_RPC_DECLARE(pool_list_cont, DAOS_ISEQ_POOL_LIST_CONT, DAOS_OSEQ_POOL_LIST_CONT)
CRT_RPC_DECLARE(pool_list_cont_v6, DAOS_ISEQ_POOL_LIST_CONT_V6, DAOS_OSEQ_POOL_LIST_CONT)

/* clang-format on */

static inline void
pool_list_cont_in_get_data(crt_rpc_t *rpc, crt_bulk_t *plci_cont_bulkp, uint64_t *plci_ncontp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*plci_cont_bulkp = ((struct pool_list_cont_v6_in *)in)->plci_cont_bulk;
		*plci_ncontp     = ((struct pool_list_cont_v6_in *)in)->plci_ncont;
	} else {
		*plci_cont_bulkp = ((struct pool_list_cont_in *)in)->plci_cont_bulk;
		*plci_ncontp     = ((struct pool_list_cont_in *)in)->plci_ncont;
	}
}

static inline void
pool_list_cont_in_set_data(crt_rpc_t *rpc, crt_bulk_t plci_cont_bulk, uint64_t plci_ncont)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_list_cont_v6_in *)in)->plci_cont_bulk = plci_cont_bulk;
		((struct pool_list_cont_v6_in *)in)->plci_ncont     = plci_ncont;
	} else {
		((struct pool_list_cont_in *)in)->plci_cont_bulk = plci_cont_bulk;
		((struct pool_list_cont_in *)in)->plci_ncont     = plci_ncont;
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_FILTER_CONT	/* input fields */		 \
	((struct pool_op_in)		(pfci_op)		CRT_VAR) \
	((crt_bulk_t)			(pfci_cont_bulk)	CRT_VAR) \
	((uint64_t)			(pfci_ncont)		CRT_VAR) \
	((daos_pool_cont_filter_t)	(pfci_filt)		CRT_VAR)

#define DAOS_ISEQ_POOL_FILTER_CONT_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(pfci_op)		CRT_VAR) \
	((crt_bulk_t)			(pfci_cont_bulk)	CRT_VAR) \
	((uint64_t)			(pfci_ncont)		CRT_VAR) \
	((daos_pool_cont_filter_t)	(pfci_filt)		CRT_VAR)

#define DAOS_OSEQ_POOL_FILTER_CONT	/* output fields */		 \
	((struct pool_op_out)		(pfco_op)		CRT_VAR) \
	((uint64_t)			(pfco_ncont)		CRT_VAR)

CRT_RPC_DECLARE(pool_filter_cont, DAOS_ISEQ_POOL_FILTER_CONT, DAOS_OSEQ_POOL_FILTER_CONT)
CRT_RPC_DECLARE(pool_filter_cont_v6, DAOS_ISEQ_POOL_FILTER_CONT_V6, DAOS_OSEQ_POOL_FILTER_CONT)

/* clang-format on */

static inline void
pool_filter_cont_in_get_data(crt_rpc_t *rpc, crt_bulk_t *pfci_cont_bulkp, uint64_t *pfci_ncontp,
			     daos_pool_cont_filter_t **pfci_filtp)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		*pfci_cont_bulkp = ((struct pool_filter_cont_v6_in *)in)->pfci_cont_bulk;
		*pfci_ncontp     = ((struct pool_filter_cont_v6_in *)in)->pfci_ncont;
		*pfci_filtp      = &((struct pool_filter_cont_v6_in *)in)->pfci_filt;
	} else {
		*pfci_cont_bulkp = ((struct pool_filter_cont_in *)in)->pfci_cont_bulk;
		*pfci_ncontp     = ((struct pool_filter_cont_in *)in)->pfci_ncont;
		*pfci_filtp      = &((struct pool_filter_cont_in *)in)->pfci_filt;
	}
}

static inline void
pool_filter_cont_in_set_data(crt_rpc_t *rpc, crt_bulk_t pfci_cont_bulk, uint64_t pfci_ncont,
			     daos_pool_cont_filter_t *pfci_filt)
{
	void *in = crt_req_get(rpc);

	if (rpc_ver_atleast(rpc, POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		((struct pool_filter_cont_v6_in *)in)->pfci_cont_bulk = pfci_cont_bulk;
		((struct pool_filter_cont_v6_in *)in)->pfci_ncont     = pfci_ncont;
		if (pfci_filt)
			((struct pool_filter_cont_v6_in *)in)->pfci_filt = *pfci_filt;
		else
			memset(&((struct pool_filter_cont_v6_in *)in)->pfci_filt, 0,
			       sizeof(daos_pool_cont_filter_t));

	} else {
		((struct pool_filter_cont_in *)in)->pfci_cont_bulk = pfci_cont_bulk;
		((struct pool_filter_cont_in *)in)->pfci_ncont     = pfci_ncont;
		if (pfci_filt)
			((struct pool_filter_cont_in *)in)->pfci_filt = *pfci_filt;
		else
			memset(&((struct pool_filter_cont_in *)in)->pfci_filt, 0,
			       sizeof(daos_pool_cont_filter_t));
	}
}

/* clang-format off */

#define DAOS_ISEQ_POOL_RANKS_GET	/* input fields */		 \
	((struct pool_op_in)		(prgi_op)		CRT_VAR) \
	((crt_bulk_t)			(prgi_ranks_bulk)	CRT_VAR) \
	((uint32_t)			(prgi_nranks)		CRT_VAR)

#define DAOS_OSEQ_POOL_RANKS_GET	/* output fields */		 \
	((struct pool_op_out)		(prgo_op)		CRT_VAR) \
	((uint32_t)			(prgo_nranks)		CRT_VAR)

CRT_RPC_DECLARE(pool_ranks_get, DAOS_ISEQ_POOL_RANKS_GET, DAOS_OSEQ_POOL_RANKS_GET)

#define DAOS_ISEQ_POOL_UPGRADE_V6	/* input fields */		 \
	((struct pool_op_v6_in)		(poi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_UPGRADE		/* output fields */		 \
	((struct pool_op_out)		(poo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_upgrade, DAOS_ISEQ_POOL_UPGRADE_V6, DAOS_OSEQ_POOL_UPGRADE)

#define DAOS_ISEQ_POOL_TGT_QUERY_MAP	/* input fields */		 \
	((struct pool_op_in)		(tmi_op)		CRT_VAR) \
	((crt_bulk_t)			(tmi_map_bulk)		CRT_VAR) \
	((uint32_t)			(tmi_map_version)	CRT_VAR)

#define DAOS_OSEQ_POOL_TGT_QUERY_MAP	/* output fields */		 \
	((struct pool_op_out)		(tmo_op)		CRT_VAR) \
	/* only set on -DER_TRUNC */					 \
	((uint32_t)			(tmo_map_buf_size)	CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_query_map, DAOS_ISEQ_POOL_TGT_QUERY_MAP, DAOS_OSEQ_POOL_TGT_QUERY_MAP)

#define DAOS_ISEQ_POOL_TGT_DISCARD	/* input fields */		 \
	((uuid_t)			(ptdi_uuid)		CRT_VAR) \
	((struct pool_target_addr)	(ptdi_addrs)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_TGT_DISCARD	/* output fields */		 \
	((int32_t)			(ptdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_discard, DAOS_ISEQ_POOL_TGT_DISCARD, DAOS_OSEQ_POOL_TGT_DISCARD)

/* clang-format on */

static inline int
pool_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		const uuid_t pi_uuid, const uuid_t pi_hdl, uint64_t *req_timep, crt_rpc_t **req)
{
	int                    rc;
	crt_opcode_t           opcode;
	static __thread uuid_t cli_id;
	int                    proto_ver;
	struct pool_op_in     *in;

	proto_ver = dc_pool_proto_version ? dc_pool_proto_version : DAOS_POOL_VERSION;

	if (uuid_is_null(cli_id))
		uuid_generate(cli_id);

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, proto_ver);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_POOL, tgt_ep->ep_tag);

	rc = crt_req_create(crt_ctx, tgt_ep, opcode, req);
	if (rc != 0)
		return rc;
	in = crt_req_get(*req);
	uuid_copy(in->pi_uuid, pi_uuid);
	if (uuid_is_null(pi_hdl))
		uuid_clear(in->pi_hdl);
	else
		uuid_copy(in->pi_hdl, pi_hdl);

	if (req_timep && (*req_timep == 0))
		*req_timep = d_hlc_get();

	/* Temporary req_timep check: some opcodes aren't (yet) at v8 and don't have the op key */
	if (req_timep && (proto_ver >= POOL_PROTO_VER_WITH_SVC_OP_KEY)) {
		struct pool_op_v6_in *in6 = crt_req_get(*req);

		uuid_copy(in6->pi_cli_id, cli_id);
		in6->pi_time = *req_timep;
	}
	return 0;
}

uint64_t
pool_query_bits(daos_pool_info_t *po_info, daos_prop_t *prop);

void
pool_query_reply_to_info(uuid_t pool_uuid, struct pool_buf *map_buf,
			 uint32_t map_version, uint32_t leader_rank,
			 struct daos_pool_space *ps,
			 struct daos_rebuild_status *rs, daos_pool_info_t *info);

int list_cont_bulk_create(crt_context_t ctx, crt_bulk_t *bulk,
			  void *buf, daos_size_t ncont);
void list_cont_bulk_destroy(crt_bulk_t bulk);

int
map_bulk_create(crt_context_t ctx, crt_bulk_t *bulk, struct pool_buf **buf,
		unsigned int nr);
void
map_bulk_destroy(crt_bulk_t bulk, struct pool_buf *buf);

#endif /* __POOL_RPC_H__ */
