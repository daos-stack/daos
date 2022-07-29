/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: rebuild  RPC protocol Definitions
 *
 * This is naturally shared by both dc_pool and ds_pool. The in and out data
 * structures must be absent of any compiler-generated paddings.
 */

#ifndef __REBUILD_RPC_H__
#define __REBUILD_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_REBUILD_VERSION 3
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define REBUILD_PROTO_SRV_RPC_LIST					\
	X(REBUILD_OBJECTS_SCAN,						\
		0, &CQF_rebuild_scan,					\
		rebuild_tgt_scan_handler, &rebuild_tgt_scan_co_ops)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum rebuild_operation {
	REBUILD_PROTO_SRV_RPC_LIST,
};

#undef X

extern struct crt_proto_format rebuild_proto_fmt;

#define DAOS_ISEQ_REBUILD_SCAN	/* input fields */		 \
	((uuid_t)		(rsi_pool_uuid)		CRT_VAR) \
	((uint64_t)		(rsi_leader_term)	CRT_VAR) \
	((int32_t)		(rsi_rebuild_op)	CRT_VAR) \
	((uint32_t)		(rsi_tgts_num)		CRT_VAR) \
	((uint32_t)		(rsi_ns_id)		CRT_VAR) \
	((uint32_t)		(rsi_rebuild_ver)	CRT_VAR) \
	((uint32_t)		(rsi_master_rank)	CRT_VAR) \
	((uint32_t)		(rsi_rebuild_gen)	CRT_VAR)

#define DAOS_OSEQ_REBUILD_SCAN	/* output fields */		 \
	((uint64_t)		(rso_stable_epoch)	CRT_VAR) \
	((int32_t)		(rso_status)		CRT_VAR)

CRT_RPC_DECLARE(rebuild_scan, DAOS_ISEQ_REBUILD_SCAN, DAOS_OSEQ_REBUILD_SCAN)

#define DAOS_ISEQ_REBUILD	/* input fields */		 \
	((uint32_t)		(roi_rebuild_ver)	CRT_VAR) \
	((uint32_t)		(roi_tgt_idx)		CRT_VAR) \
	((uuid_t)		(roi_pool_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(roi_oids)		CRT_ARRAY) \
	((uint64_t)		(roi_ephs)		CRT_ARRAY) \
	((uuid_t)		(roi_uuids)		CRT_ARRAY) \
	((uint32_t)		(roi_shards)		CRT_ARRAY)

#define DAOS_OSEQ_REBUILD	/* output fields */		 \
	((int32_t)		(roo_status)		CRT_VAR)

CRT_RPC_DECLARE(rebuild, DAOS_ISEQ_REBUILD, DAOS_OSEQ_REBUILD)

static inline int
rebuild_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
		   crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_REBUILD_MODULE,
				 DAOS_REBUILD_VERSION);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_REBUILD, tgt_ep->ep_tag);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

#endif /* __REBUILD_RPC_H__ */
