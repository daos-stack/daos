/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
		0, &CQF_pool_create,					\
		ds_pool_create_handler, NULL),				\
	X(POOL_CONNECT,							\
		0, &CQF_pool_connect,					\
		ds_pool_connect_handler, NULL),				\
	X(POOL_DISCONNECT,						\
		0, &CQF_pool_disconnect,				\
		ds_pool_disconnect_handler, NULL),			\
	X(POOL_QUERY,							\
		0, &CQF_pool_query,					\
		ds_pool_query_handler, NULL),				\
	X(POOL_QUERY_INFO,						\
		0, &CQF_pool_query_info,				\
		ds_pool_query_info_handler, NULL),			\
	X(POOL_EXCLUDE,							\
		0, &CQF_pool_exclude,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_DRAIN,							\
		0, &CQF_pool_drain,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_EXTEND,							\
		0, &CQF_pool_extend,					\
		ds_pool_extend_handler, NULL),				\
	X(POOL_EVICT,							\
		0, &CQF_pool_evict,					\
		ds_pool_evict_handler, NULL),				\
	X(POOL_REINT,							\
		0, &CQF_pool_add,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_ADD_IN,							\
		0, &CQF_pool_add_in,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_EXCLUDE_OUT,						\
		0, &CQF_pool_exclude_out,				\
		ds_pool_update_handler, NULL),				\
	X(POOL_SVC_STOP,						\
		0, &CQF_pool_svc_stop,					\
		ds_pool_svc_stop_handler, NULL),			\
	X(POOL_ATTR_LIST,						\
		0, &CQF_pool_attr_list,					\
		ds_pool_attr_list_handler, NULL),			\
	X(POOL_ATTR_GET,						\
		0, &CQF_pool_attr_get,					\
		ds_pool_attr_get_handler, NULL),			\
	X(POOL_ATTR_SET,						\
		0, &CQF_pool_attr_set,					\
		ds_pool_attr_set_handler, NULL),			\
	X(POOL_ATTR_DEL,						\
		0, &CQF_pool_attr_del,					\
		ds_pool_attr_del_handler, NULL),			\
	X(POOL_REPLICAS_ADD,						\
		0, &CQF_pool_replicas_add,				\
		ds_pool_replicas_update_handler, NULL),			\
	X(POOL_REPLICAS_REMOVE,						\
		0, &CQF_pool_replicas_remove,				\
		ds_pool_replicas_update_handler, NULL),			\
	X(POOL_LIST_CONT,						\
		0, &CQF_pool_list_cont,					\
		ds_pool_list_cont_handler, NULL)

#define POOL_PROTO_SRV_RPC_LIST						\
	X(POOL_TGT_DISCONNECT,						\
		0, &CQF_pool_tgt_disconnect,				\
		ds_pool_tgt_disconnect_handler,				\
		&ds_pool_tgt_disconnect_co_ops),			\
	X(POOL_TGT_QUERY,						\
		0, &CQF_pool_tgt_query,					\
		ds_pool_tgt_query_handler,				\
		&ds_pool_tgt_query_co_ops),				\
	X(POOL_PROP_GET,						\
		0, &CQF_pool_prop_get,					\
		ds_pool_prop_get_handler,				\
		NULL),							\
	X(POOL_ADD_TGT,							\
		0, &CQF_pool_add,					\
		ds_pool_update_handler, NULL),				\
	X(POOL_PROP_SET,						\
		0, &CQF_pool_prop_set,					\
		ds_pool_prop_set_handler,				\
		NULL),							\
	X(POOL_ACL_UPDATE,						\
		0, &CQF_pool_acl_update,				\
		ds_pool_acl_update_handler,				\
		NULL),							\
	X(POOL_ACL_DELETE,						\
		0, &CQF_pool_acl_delete,				\
		ds_pool_acl_delete_handler,				\
		NULL)

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

#define DAOS_ISEQ_POOL_OP	/* input fields */		 \
	((uuid_t)		(pi_uuid)		CRT_VAR) \
	((uuid_t)		(pi_hdl)		CRT_VAR)

#define DAOS_OSEQ_POOL_OP	/* output fields */		 \
	((int32_t)		(po_rc)			CRT_VAR) \
	((uint32_t)		(po_map_version)	CRT_VAR) \
	((struct rsvc_hint)	(po_hint)		CRT_VAR)

CRT_RPC_DECLARE(pool_op, DAOS_ISEQ_POOL_OP, DAOS_OSEQ_POOL_OP)

#define DAOS_ISEQ_POOL_CREATE	/* input fields */		 \
	((struct pool_op_in)	(pri_op)		CRT_VAR) \
	((uuid_t)		(pri_tgt_uuids)		CRT_ARRAY) \
	((d_rank_list_t)	(pri_tgt_ranks)		CRT_PTR) \
	((daos_prop_t)		(pri_prop)		CRT_PTR) \
	((uint32_t)		(pri_ndomains)		CRT_VAR) \
	((uint32_t)		(pri_ntgts)		CRT_VAR) \
	((int32_t)		(pri_domains)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_CREATE	/* output fields */		 \
	((struct pool_op_out)	(pro_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_create, DAOS_ISEQ_POOL_CREATE, DAOS_OSEQ_POOL_CREATE)

#define DAOS_ISEQ_POOL_CONNECT	/* input fields */		 \
	((struct pool_op_in)	(pci_op)		CRT_VAR) \
	((d_iov_t)		(pci_cred)		CRT_VAR) \
	((uint64_t)		(pci_flags)		CRT_VAR) \
	((uint64_t)		(pci_query_bits)	CRT_VAR) \
	((crt_bulk_t)		(pci_map_bulk)		CRT_VAR)

#define DAOS_OSEQ_POOL_CONNECT	/* output fields */		 \
	((struct pool_op_out)	(pco_op)		CRT_VAR) \
	((struct daos_pool_space) (pco_space)		CRT_VAR) \
	((struct daos_rebuild_status) (pco_rebuild_st)	CRT_VAR) \
	/* only set on -DER_TRUNC */				 \
	((uint32_t)		(pco_map_buf_size)	CRT_VAR)

CRT_RPC_DECLARE(pool_connect, DAOS_ISEQ_POOL_CONNECT, DAOS_OSEQ_POOL_CONNECT)

#define DAOS_ISEQ_POOL_DISCONNECT /* input fields */		 \
	((struct pool_op_in)	(pdi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_DISCONNECT /* output fields */		 \
	((struct pool_op_out)	(pdo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_disconnect, DAOS_ISEQ_POOL_DISCONNECT,
		DAOS_OSEQ_POOL_DISCONNECT)

#define DAOS_ISEQ_POOL_QUERY	/* input fields */		 \
	((struct pool_op_in)	(pqi_op)		CRT_VAR) \
	((crt_bulk_t)		(pqi_map_bulk)		CRT_VAR) \
	((uint64_t)		(pqi_query_bits)	CRT_VAR)

#define DAOS_OSEQ_POOL_QUERY	/* output fields */		 \
	((struct pool_op_out)	(pqo_op)		CRT_VAR) \
	((daos_prop_t)		(pqo_prop)		CRT_PTR) \
	((struct daos_pool_space) (pqo_space)		CRT_VAR) \
	((struct daos_rebuild_status) (pqo_rebuild_st)	CRT_VAR) \
	/* only set on -DER_TRUNC */				 \
	((uint32_t)		(pqo_map_buf_size)	CRT_VAR)

CRT_RPC_DECLARE(pool_query, DAOS_ISEQ_POOL_QUERY, DAOS_OSEQ_POOL_QUERY)

#define DAOS_ISEQ_POOL_QUERY_INFO	/* input fields */	 \
	((struct pool_op_in)	(pqii_op)		CRT_VAR) \
	((d_rank_t)		(pqii_rank)		CRT_VAR) \
	((uint32_t)		(pqii_tgt)		CRT_VAR)

#define DAOS_OSEQ_POOL_QUERY_INFO	/* output fields */	 \
	((struct pool_op_out)	  (pqio_op)		CRT_VAR) \
	((d_rank_t)		  (pqio_rank)		CRT_VAR) \
	((uint32_t)		  (pqio_tgt)		CRT_VAR) \
	((daos_target_state_t)	  (pqio_state)		CRT_VAR)

CRT_RPC_DECLARE(pool_query_info, DAOS_ISEQ_POOL_QUERY_INFO,
		DAOS_OSEQ_POOL_QUERY_INFO)

#define DAOS_ISEQ_POOL_ATTR_LIST /* input fields */		 \
	((struct pool_op_in)	(pali_op)		CRT_VAR) \
	((crt_bulk_t)		(pali_bulk)		CRT_VAR)

#define DAOS_OSEQ_POOL_ATTR_LIST /* output fields */		 \
	((struct pool_op_out)	(palo_op)		CRT_VAR) \
	((uint64_t)		(palo_size)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_list, DAOS_ISEQ_POOL_ATTR_LIST,
		DAOS_OSEQ_POOL_ATTR_LIST)

#define DAOS_ISEQ_POOL_ATTR_GET	/* input fields */		 \
	((struct pool_op_in)	(pagi_op)		CRT_VAR) \
	((uint64_t)		(pagi_count)		CRT_VAR) \
	((uint64_t)		(pagi_key_length)	CRT_VAR) \
	((crt_bulk_t)		(pagi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_get, DAOS_ISEQ_POOL_ATTR_GET, DAOS_OSEQ_POOL_OP)

#define DAOS_ISEQ_POOL_ATTR_SET	/* input fields */		 \
	((struct pool_op_in)	(pasi_op)		CRT_VAR) \
	((uint64_t)		(pasi_count)		CRT_VAR) \
	((crt_bulk_t)		(pasi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_set, DAOS_ISEQ_POOL_ATTR_SET, DAOS_OSEQ_POOL_OP)

#define DAOS_ISEQ_POOL_ATTR_DEL	/* input fields */		 \
	((struct pool_op_in)	(padi_op)		CRT_VAR) \
	((uint64_t)		(padi_count)		CRT_VAR) \
	((crt_bulk_t)		(padi_bulk)		CRT_VAR)

CRT_RPC_DECLARE(pool_attr_del, DAOS_ISEQ_POOL_ATTR_DEL, DAOS_OSEQ_POOL_OP)

#define DAOS_ISEQ_POOL_MEMBERSHIP /* input fields */		 \
	((uuid_t)		(pmi_uuid)		CRT_VAR) \
	((d_rank_list_t)	(pmi_targets)		CRT_PTR)

#define DAOS_OSEQ_POOL_MEMBERSHIP /* output fields */		 \
	((struct rsvc_hint)	(pmo_hint)		CRT_VAR) \
	((d_rank_list_t)	(pmo_failed)		CRT_PTR) \
	((int32_t)		(pmo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_membership, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DECLARE(pool_replicas_add, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DECLARE(pool_replicas_remove, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)

/** Target address for each pool target */
struct pool_target_addr {
	/** rank of the node where the target resides */
	d_rank_t	pta_rank;
	/** target index in each node */
	uint32_t	pta_target;
};

struct pool_target_addr_list {
	int			 pta_number;
	struct pool_target_addr	*pta_addrs;
};

#define DAOS_ISEQ_POOL_TGT_UPDATE /* input fields */		 \
	((struct pool_op_in)	(pti_op)		CRT_VAR) \
	((struct pool_target_addr) (pti_addr_list)	CRT_ARRAY)

#define DAOS_OSEQ_POOL_TGT_UPDATE /* output fields */		 \
	((struct pool_op_out)	(pto_op)		CRT_VAR) \
	((struct pool_target_addr) (pto_addr_list)	CRT_ARRAY)

#define DAOS_ISEQ_POOL_EXTEND /* input fields */		 \
	((struct pool_op_in)	(pei_op)		CRT_VAR) \
	((uint32_t)		(pei_ntgts)		CRT_VAR) \
	((uuid_t)		(pei_tgt_uuids)		CRT_ARRAY) \
	((d_rank_list_t)	(pei_tgt_ranks)		CRT_PTR) \
	((uint32_t)		(pei_ndomains)		CRT_VAR) \
	((int32_t)		(pei_domains)		CRT_ARRAY)


#define DAOS_OSEQ_POOL_EXTEND /* output fields */		 \
	((struct pool_op_out)	(peo_op)		CRT_VAR) \

CRT_RPC_DECLARE(pool_tgt_update, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_extend, DAOS_ISEQ_POOL_EXTEND,
		DAOS_OSEQ_POOL_EXTEND)
CRT_RPC_DECLARE(pool_add, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_add_in, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_drain, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DECLARE(pool_exclude_out, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)

#define DAOS_ISEQ_POOL_EVICT	/* input fields */			 \
	((struct pool_op_in)	(pvi_op)			CRT_VAR) \
	((uint32_t)		(pvi_pool_destroy)		CRT_VAR) \
	((uint32_t)		(pvi_pool_destroy_force)	CRT_VAR)

#define DAOS_OSEQ_POOL_EVICT	/* output fields */		 \
	((struct pool_op_out)	(pvo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_evict, DAOS_ISEQ_POOL_EVICT, DAOS_OSEQ_POOL_EVICT)

#define DAOS_ISEQ_POOL_SVC_STOP	/* input fields */		 \
	((struct pool_op_in)	(psi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_SVC_STOP	/* output fields */		 \
	((struct pool_op_out)	(pso_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_svc_stop, DAOS_ISEQ_POOL_SVC_STOP, DAOS_OSEQ_POOL_SVC_STOP)

#define DAOS_ISEQ_POOL_TGT_DISCONNECT /* input fields */	 \
	((uuid_t)		(tdi_uuid)		CRT_VAR) \
	((uuid_t)		(tdi_hdls)		CRT_ARRAY)

#define DAOS_OSEQ_POOL_TGT_DISCONNECT /* output fields */	 \
	((int32_t)		(tdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_disconnect, DAOS_ISEQ_POOL_TGT_DISCONNECT,
		DAOS_OSEQ_POOL_TGT_DISCONNECT)

#define DAOS_ISEQ_POOL_TGT_QUERY	/* input fields */	 \
	((struct pool_op_in)	(tqi_op)		CRT_VAR)

#define DAOS_OSEQ_POOL_TGT_QUERY	/* output fields */	 \
	((struct daos_pool_space) (tqo_space)		CRT_VAR) \
	((uint32_t)		(tqo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_query, DAOS_ISEQ_POOL_TGT_QUERY,
		DAOS_OSEQ_POOL_TGT_QUERY)

#define DAOS_ISEQ_POOL_TGT_DIST_HDLS	/* input fields */	 \
	((uuid_t)		(tfi_pool_uuid)		CRT_VAR) \
	((d_iov_t)		(tfi_hdls)		CRT_VAR)

#define DAOS_OSEQ_POOL_TGT_DIST_HDLS	/* output fields */	 \
	((uint32_t)		(tfo_rc)		CRT_VAR)

CRT_RPC_DECLARE(pool_tgt_dist_hdls, DAOS_ISEQ_POOL_TGT_DIST_HDLS,
		DAOS_OSEQ_POOL_TGT_DIST_HDLS)

#define DAOS_ISEQ_POOL_PROP_GET	/* input fields */		 \
	((struct pool_op_in)	(pgi_op)		CRT_VAR) \
	((uint64_t)		(pgi_query_bits)	CRT_VAR)

#define DAOS_OSEQ_POOL_PROP_GET	/* output fields */		 \
	((struct pool_op_out)	(pgo_op)		CRT_VAR) \
	((daos_prop_t)		(pgo_prop)		CRT_PTR)

CRT_RPC_DECLARE(pool_prop_get, DAOS_ISEQ_POOL_PROP_GET, DAOS_OSEQ_POOL_PROP_GET)

#define DAOS_ISEQ_POOL_PROP_SET	/* input fields */		 \
	((struct pool_op_in)	(psi_op)		CRT_VAR) \
	((daos_prop_t)		(psi_prop)		CRT_PTR)

#define DAOS_OSEQ_POOL_PROP_SET	/* output fields */		 \
	((struct pool_op_out)	(pso_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_prop_set, DAOS_ISEQ_POOL_PROP_SET, DAOS_OSEQ_POOL_PROP_SET)

#define DAOS_ISEQ_POOL_ACL_UPDATE	/* input fields */	 \
	((struct pool_op_in)	(pui_op)		CRT_VAR) \
	((struct daos_acl)	(pui_acl)		CRT_PTR)

#define DAOS_OSEQ_POOL_ACL_UPDATE	/* output fields */	 \
	((struct pool_op_out)	(puo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_acl_update, DAOS_ISEQ_POOL_ACL_UPDATE,
		DAOS_OSEQ_POOL_ACL_UPDATE)

#define DAOS_ISEQ_POOL_ACL_DELETE	/* input fields */	 \
	((struct pool_op_in)	(pdi_op)		CRT_VAR) \
	((uint8_t)		(pdi_type)		CRT_VAR) \
	((d_const_string_t)	(pdi_principal)		CRT_VAR)

#define DAOS_OSEQ_POOL_ACL_DELETE	/* output fields */	 \
	((struct pool_op_out)	(pdo_op)		CRT_VAR)

CRT_RPC_DECLARE(pool_acl_delete, DAOS_ISEQ_POOL_ACL_DELETE,
		DAOS_OSEQ_POOL_ACL_DELETE)

#define DAOS_ISEQ_POOL_LIST_CONT	/* input fields */		 \
	((struct pool_op_in)	(plci_op)			CRT_VAR) \
	((crt_bulk_t)		(plci_cont_bulk)		CRT_VAR) \
	((uint64_t)		(plci_ncont)			CRT_VAR)

#define DAOS_OSEQ_POOL_LIST_CONT	/* output fields */		 \
	((struct pool_op_out)	(plco_op)			CRT_VAR) \
	((uint64_t)		(plco_ncont)			CRT_VAR)

CRT_RPC_DECLARE(pool_list_cont, DAOS_ISEQ_POOL_LIST_CONT,
		DAOS_OSEQ_POOL_LIST_CONT)

static inline int
pool_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, DAOS_POOL_VERSION);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_POOL, tgt_ep->ep_tag);

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

uint64_t
pool_query_bits(daos_pool_info_t *po_info, daos_prop_t *prop);

void
pool_query_reply_to_info(uuid_t pool_uuid, struct pool_buf *map_buf,
			 uint32_t map_version, uint32_t leader_rank,
			 struct daos_pool_space *ps,
			 struct daos_rebuild_status *rs,
			 daos_pool_info_t *info);

int list_cont_bulk_create(crt_context_t ctx, crt_bulk_t *bulk,
			  struct daos_pool_cont_info *buf, daos_size_t ncont);
void list_cont_bulk_destroy(crt_bulk_t bulk);

int
map_bulk_create(crt_context_t ctx, crt_bulk_t *bulk, struct pool_buf **buf,
		unsigned int nr);
void
map_bulk_destroy(crt_bulk_t bulk, struct pool_buf *buf);

#endif /* __POOL_RPC_H__ */
