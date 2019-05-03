/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * dc_cont, ds_cont: RPC Protocol Definitions
 *
 * This is naturally shared by both dc_cont and ds_cont. The in and out data
 * structures must be absent of any compiler-generated paddings.
 */

#ifndef __CONTAINER_RPC_H__
#define __CONTAINER_RPC_H__

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
#define DAOS_CONT_VERSION 1
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define CONT_PROTO_CLI_RPC_LIST						\
	X(CONT_CREATE,							\
		0, &CQF_cont_create,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_DESTROY,							\
		0, &CQF_cont_destroy,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_OPEN,							\
		0, &CQF_cont_open,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_CLOSE,							\
		0, &CQF_cont_close,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_QUERY,							\
		0, &CQF_cont_query,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_OID_ALLOC,						\
		0, &CQF_cont_oid_alloc,					\
		ds_cont_oid_alloc_handler, NULL),			\
	X(CONT_ATTR_LIST,						\
		0, &CQF_cont_attr_list,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_ATTR_GET,						\
		0, &CQF_cont_attr_get,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_ATTR_SET,						\
		0, &CQF_cont_attr_set,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_DISCARD,						\
		0, &CQF_cont_epoch_op,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_COMMIT,						\
		0, &CQF_cont_epoch_op,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_AGGREGATE,						\
		0, &CQF_cont_epoch_op,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_LIST,						\
		0, &CQF_cont_snap_list,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_CREATE,						\
		0, &CQF_cont_epoch_op,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_DESTROY,						\
		0, &CQF_cont_snap_destroy,				\
		ds_cont_op_handler, NULL)

#define CONT_PROTO_SRV_RPC_LIST						\
	X(CONT_TGT_DESTROY,						\
		0, &CQF_cont_tgt_destroy,				\
		ds_cont_tgt_destroy_handler,				\
		&ds_cont_tgt_destroy_co_ops),				\
	X(CONT_TGT_CLOSE,						\
		0, &CQF_cont_tgt_close,					\
		ds_cont_tgt_close_handler,				\
		&ds_cont_tgt_close_co_ops),				\
	X(CONT_TGT_QUERY,						\
		0, &CQF_cont_tgt_query,					\
		ds_cont_tgt_query_handler,				\
		&ds_cont_tgt_query_co_ops),				\
	X(CONT_TGT_EPOCH_DISCARD,					\
		0, &CQF_cont_tgt_epoch_discard,				\
		ds_cont_tgt_epoch_discard_handler,			\
		&ds_cont_tgt_epoch_discard_co_ops),			\
	X(CONT_TGT_EPOCH_AGGREGATE,					\
		0, &CQF_cont_tgt_epoch_aggregate,			\
		ds_cont_tgt_epoch_aggregate_handler,			\
		&ds_cont_tgt_epoch_aggregate_co_ops),			\
	X(CONT_TGT_SNAPSHOT_NOTIFY,					\
		0, &CQF_cont_tgt_snapshot_notify,			\
		ds_cont_tgt_snapshot_notify_handler,			\
		&ds_cont_tgt_snapshot_notify_co_ops)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum cont_operation {
	CONT_PROTO_CLI_RPC_LIST,
	CONT_PROTO_CLI_COUNT,
	CONT_PROTO_CLI_LAST = CONT_PROTO_CLI_COUNT - 1,
	CONT_PROTO_SRV_RPC_LIST,
};

#undef X

extern struct crt_proto_format cont_proto_fmt;

#define DAOS_ISEQ_CONT_OP	/* input fields */		 \
				/* pool handle UUID */		 \
	((uuid_t)		(ci_pool_hdl)		CRT_VAR) \
				/* container UUID */		 \
	((uuid_t)		(ci_uuid)		CRT_VAR) \
				/* container handle UUID */	 \
	((uuid_t)		(ci_hdl)		CRT_VAR)

#define DAOS_OSEQ_CONT_OP	/* output fields */		 \
				/* operation return code */	 \
	((int32_t)		(co_rc)			CRT_VAR) \
				/* latest map version or zero */ \
	((uint32_t)		(co_map_version)	CRT_VAR) \
				/* leadership info */		 \
	((struct rsvc_hint)	(co_hint)		CRT_VAR)

CRT_RPC_DECLARE(cont_op, DAOS_ISEQ_CONT_OP, DAOS_OSEQ_CONT_OP)

#define DAOS_ISEQ_CONT_CREATE	/* input fields */		 \
				/* .ci_hdl unused */		 \
	((struct cont_op_in)	(cci_op)		CRT_VAR) \
	((daos_prop_t)		(cci_prop)		CRT_PTR)

#define DAOS_OSEQ_CONT_CREATE	/* output fields */		 \
	((struct cont_op_out)	(cco_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_create, DAOS_ISEQ_CONT_CREATE, DAOS_OSEQ_CONT_CREATE)

#define DAOS_ISEQ_CONT_DESTROY	/* input fields */		 \
				/* .ci_hdl unused */		 \
	((struct cont_op_in)	(cdi_op)		CRT_VAR) \
				/* evict all handles */		 \
	((uint32_t)		(cdi_force)		CRT_VAR)

#define DAOS_OSEQ_CONT_DESTROY	/* output fields */		 \
	((struct cont_op_out)	(cdo_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_destroy, DAOS_ISEQ_CONT_DESTROY, DAOS_OSEQ_CONT_DESTROY)

#define DAOS_ISEQ_CONT_OPEN	/* input fields */		 \
	((struct cont_op_in)	(coi_op)		CRT_VAR) \
	((uint64_t)		(coi_capas)		CRT_VAR)

#define DAOS_OSEQ_CONT_OPEN	/* output fields */		 \
	((struct cont_op_out)	(coo_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_open, DAOS_ISEQ_CONT_OPEN, DAOS_OSEQ_CONT_OPEN)

#define DAOS_ISEQ_CONT_CLOSE	/* input fields */		 \
	((struct cont_op_in)	(cci_op)		CRT_VAR)

#define DAOS_OSEQ_CONT_CLOSE	/* output fields */		 \
	((struct cont_op_out)	(cco_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_close, DAOS_ISEQ_CONT_CLOSE, DAOS_OSEQ_CONT_CLOSE)

/** container query request bits */
#define DAOS_CO_QUERY_PROP_LABEL	(1ULL << 0)
#define DAOS_CO_QUERY_PROP_LAYOUT_TYPE	(1ULL << 1)
#define DAOS_CO_QUERY_PROP_LAYOUT_VER	(1ULL << 2)
#define DAOS_CO_QUERY_PROP_CSUM		(1ULL << 3)
#define DAOS_CO_QUERY_PROP_REDUN_FAC	(1ULL << 4)
#define DAOS_CO_QUERY_PROP_REDUN_LVL	(1ULL << 5)
#define DAOS_CO_QUERY_PROP_SNAPSHOT_MAX	(1ULL << 6)
#define DAOS_CO_QUERY_PROP_COMPRESS	(1ULL << 7)
#define DAOS_CO_QUERY_PROP_ENCRYPT	(1ULL << 8)
#define DAOS_CO_QUERY_PROP_ACL		(1ULL << 9)

#define DAOS_CO_QUERY_PROP_BITS_NR	(10)
#define DAOS_CO_QUERY_PROP_ALL					\
	((1ULL << DAOS_CO_QUERY_PROP_BITS_NR) - 1)

#define DAOS_ISEQ_CONT_QUERY	/* input fields */		 \
	((struct cont_op_in)	(cqi_op)		CRT_VAR) \
	((uint64_t)		(cqi_bits)		CRT_VAR)

/** Add more items to query when needed */
#define DAOS_OSEQ_CONT_QUERY	/* output fields */		 \
	((struct cont_op_out)	(cqo_op)		CRT_VAR) \
	((daos_prop_t)		(cqo_prop)		CRT_PTR)

CRT_RPC_DECLARE(cont_query, DAOS_ISEQ_CONT_QUERY, DAOS_OSEQ_CONT_QUERY)

#define DAOS_ISEQ_CONT_OID_ALLOC /* input fields */		 \
	((struct cont_op_in)	(coai_op)		CRT_VAR) \
	((daos_size_t)		(num_oids)		CRT_VAR)

#define DAOS_OSEQ_CONT_OID_ALLOC /* output fields */		 \
	((struct cont_op_out)	(coao_op)		CRT_VAR) \
	((uint64_t)		(oid)			CRT_VAR)

CRT_RPC_DECLARE(cont_oid_alloc, DAOS_ISEQ_CONT_OID_ALLOC,
		DAOS_OSEQ_CONT_OID_ALLOC)

#define DAOS_ISEQ_CONT_ATTR_LIST /* input fields */		 \
	((struct cont_op_in)	(cali_op)		CRT_VAR) \
	((crt_bulk_t)		(cali_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_LIST /* output fields */		 \
	((struct cont_op_out)	(calo_op)		CRT_VAR) \
	((uint64_t)		(calo_size)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_list, DAOS_ISEQ_CONT_ATTR_LIST,
		DAOS_OSEQ_CONT_ATTR_LIST)

#define DAOS_ISEQ_CONT_ATTR_GET	/* input fields */		 \
	((struct cont_op_in)	(cagi_op)		CRT_VAR) \
	((uint64_t)		(cagi_count)		CRT_VAR) \
	((uint64_t)		(cagi_key_length)	CRT_VAR) \
	((crt_bulk_t)		(cagi_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_GET	/* output fields */		 \
	((struct cont_op_out)	(cago_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_get, DAOS_ISEQ_CONT_ATTR_GET, DAOS_OSEQ_CONT_ATTR_GET)

#define DAOS_ISEQ_CONT_ATTR_SET	/* input fields */		 \
	((struct cont_op_in)	(casi_op)		CRT_VAR) \
	((uint64_t)		(casi_count)		CRT_VAR) \
	((crt_bulk_t)		(casi_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_SET	/* output fields */		 \
	((struct cont_op_out)	(caso_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_set, DAOS_ISEQ_CONT_ATTR_SET, DAOS_OSEQ_CONT_ATTR_SET)

#define DAOS_ISEQ_CONT_EPOCH_OP	/* input fields */		 \
	((struct cont_op_in)	(cei_op)		CRT_VAR) \
	((daos_epoch_t)		(cei_epoch)		CRT_VAR)

#define DAOS_OSEQ_CONT_EPOCH_OP	/* output fields */		 \
	((struct cont_op_out)	(ceo_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_epoch_op, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)

#define DAOS_ISEQ_CONT_SNAP_LIST /* input fields */		 \
	((struct cont_op_in)	(sli_op)		CRT_VAR) \
	((crt_bulk_t)		(sli_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_SNAP_LIST /* output fields */		 \
	((struct cont_op_out)	(slo_op)		CRT_VAR) \
	((uint32_t)		(slo_count)		CRT_VAR)

CRT_RPC_DECLARE(cont_snap_list, DAOS_ISEQ_CONT_SNAP_LIST,
		DAOS_OSEQ_CONT_SNAP_LIST)

CRT_RPC_DECLARE(cont_snap_create, DAOS_ISEQ_CONT_EPOCH_OP,
		DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_snap_destroy, DAOS_ISEQ_CONT_EPOCH_OP,
		DAOS_OSEQ_CONT_EPOCH_OP)

#define DAOS_ISEQ_TGT_DESTROY	/* input fields */		 \
	((uuid_t)		(tdi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(tdi_uuid)		CRT_VAR)

#define DAOS_OSEQ_TGT_DESTROY	/* output fields */		 \
				/* number of errors */		 \
	((int32_t)		(tdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_destroy, DAOS_ISEQ_TGT_DESTROY, DAOS_OSEQ_TGT_DESTROY)

struct cont_tgt_close_rec {
	uuid_t		tcr_hdl;
	daos_epoch_t	tcr_hce;
};

#define DAOS_ISEQ_TGT_CLOSE	/* input fields */		 \
	((struct cont_tgt_close_rec) (tci_recs)		CRT_ARRAY)

#define DAOS_OSEQ_TGT_CLOSE	/* output fields */		 \
				/* number of errors */		 \
	((int32_t)		(tco_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_close, DAOS_ISEQ_TGT_CLOSE, DAOS_OSEQ_TGT_CLOSE)

#define DAOS_ISEQ_TGT_QUERY	/* input fields */		 \
	((uuid_t)		(tqi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(tqi_cont_uuid)		CRT_VAR)

#define DAOS_OSEQ_TGT_QUERY	/* output fields */		 \
	((int32_t)		(tqo_rc)		CRT_VAR) \
	((int32_t)		(tqo_pad32)		CRT_VAR) \
	((daos_epoch_t)		(tqo_min_purged_epoch)	CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_query, DAOS_ISEQ_TGT_QUERY, DAOS_OSEQ_TGT_QUERY)

#define DAOS_ISEQ_CONT_TGT_EPOCH_DISCARD /* input fields */	 \
	((uuid_t)		(tii_hdl)		CRT_VAR) \
	((daos_epoch_t)		(tii_epoch)		CRT_VAR)

#define DAOS_OSEQ_CONT_TGT_EPOCH_DISCARD /* output fields */	 \
				/* number of errors */		 \
	((int32_t)		(tio_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_epoch_discard, DAOS_ISEQ_CONT_TGT_EPOCH_DISCARD,
		DAOS_OSEQ_CONT_TGT_EPOCH_DISCARD)

#define DAOS_ISEQ_CONT_TGT_EPOCH_AGGREGATE /* input fields */	 \
	((uuid_t)		(tai_cont_uuid)		CRT_VAR) \
	((uuid_t)		(tai_pool_uuid)		CRT_VAR) \
	((daos_epoch_range_t)	(tai_epr_list)		CRT_ARRAY)

#define DAOS_OSEQ_CONT_TGT_EPOCH_AGGREGATE /* output fields */	 \
				/* number of errors */		 \
	((int32_t)		(tao_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_epoch_aggregate, DAOS_ISEQ_CONT_TGT_EPOCH_AGGREGATE,
		DAOS_OSEQ_CONT_TGT_EPOCH_AGGREGATE)

#define DAOS_ISEQ_CONT_TGT_SNAPSHOT_NOTIFY /* input fields */	 \
	((uuid_t)		(tsi_cont_uuid)		CRT_VAR) \
	((uuid_t)		(tsi_pool_uuid)		CRT_VAR)

#define DAOS_OSEQ_CONT_TGT_SNAPSHOT_NOTIFY /* output fields */	 \
				/* number of errors */		 \
	((int32_t)		(tso_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_snapshot_notify, DAOS_ISEQ_CONT_TGT_SNAPSHOT_NOTIFY,
		DAOS_OSEQ_CONT_TGT_SNAPSHOT_NOTIFY)

static inline int
cont_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_CONT_MODULE, DAOS_CONT_VERSION);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_CONT, tgt_ep->ep_tag);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

#endif /* __CONTAINER_RPC_H__ */
