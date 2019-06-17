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
#include <daos/dtx.h>

/* It cannot exceed the mercury unexpected msg size (4KB), reserves half-KB
 * for other RPC fields and cart/HG headers.
 */
#define OBJ_BULK_LIMIT	(3584) /* (3K + 512) bytes */

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
#define DAOS_OBJ_VERSION 1
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define OBJ_PROTO_CLI_RPC_LIST						\
	X(DAOS_OBJ_RPC_UPDATE,						\
		0, &CQF_obj_update,					\
		ds_obj_rw_handler, NULL),				\
	X(DAOS_OBJ_RPC_FETCH,						\
		0, &CQF_obj_fetch,					\
		ds_obj_rw_handler, NULL),				\
	X(DAOS_OBJ_DKEY_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_AKEY_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RECX_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH,						\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH_DKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH_AKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_QUERY_KEY,					\
		0, &CQF_obj_query_key,					\
		ds_obj_query_key_handler, NULL),			\
	X(DAOS_OBJ_RPC_TGT_UPDATE,					\
		0, &CQF_obj_update,					\
		ds_obj_tgt_update_handler, NULL),			\
	X(DAOS_OBJ_RPC_TGT_PUNCH,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL),			\
	X(DAOS_OBJ_RPC_TGT_PUNCH_DKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL),			\
	X(DAOS_OBJ_RPC_TGT_PUNCH_AKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum obj_rpc_opc {
	OBJ_PROTO_CLI_RPC_LIST,
	OBJ_PROTO_CLI_COUNT,
	OBJ_PROTO_CLI_LAST = OBJ_PROTO_CLI_COUNT - 1,
};

#undef X

extern struct crt_proto_format obj_proto_fmt;

enum obj_rpc_flags {
	ORF_BULK_BIND		= (1 << 0),
	/** It is a resent RPC. */
	ORF_RESEND		= (1 << 1),
	/** Disable DTX or not. */
	ORF_DTX_DISABLED	= (1 << 3),
};

/* common for update/fetch */
#define DAOS_ISEQ_OBJ_RW	/* input fields */		 \
	((struct dtx_id)	(orw_dti)		CRT_VAR) \
	((daos_unit_oid_t)	(orw_oid)		CRT_VAR) \
	((uuid_t)		(orw_pool_uuid)		CRT_VAR) \
	((uuid_t)		(orw_co_hdl)		CRT_VAR) \
	((uuid_t)		(orw_co_uuid)		CRT_VAR) \
	((uint64_t)		(orw_epoch)		CRT_VAR) \
	((uint64_t)		(orw_dkey_hash)		CRT_VAR) \
	((uint32_t)		(orw_map_ver)		CRT_VAR) \
	((uint32_t)		(orw_nr)		CRT_VAR) \
	((uint32_t)		(orw_start_shard)	CRT_VAR) \
	((uint32_t)		(orw_flags)		CRT_VAR) \
	((daos_key_t)		(orw_dkey)		CRT_VAR) \
	((struct dtx_id)	(orw_dti_cos)		CRT_ARRAY) \
	((daos_iod_t)		(orw_iods)		CRT_ARRAY) \
	((d_sg_list_t)	(orw_sgls)		CRT_ARRAY) \
	((crt_bulk_t)		(orw_bulks)		CRT_ARRAY) \
	((struct daos_shard_tgt) (orw_shard_tgts)	CRT_ARRAY)

#define DAOS_OSEQ_OBJ_RW	/* output fields */		 \
	((int32_t)		(orw_ret)		CRT_VAR) \
	((uint32_t)		(orw_map_version)	CRT_VAR) \
	((uint64_t)		(orw_attr)		CRT_VAR) \
	((uint64_t)		(orw_dkey_conflict)	CRT_VAR) \
	((struct dtx_id)	(orw_dti_conflict)	CRT_VAR) \
	((daos_size_t)		(orw_sizes)		CRT_ARRAY) \
	((uint32_t)		(orw_nrs)		CRT_ARRAY) \
	((d_sg_list_t)	(orw_sgls)		CRT_ARRAY)

CRT_RPC_DECLARE(obj_rw, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DECLARE(obj_update, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DECLARE(obj_fetch, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)

/* object Enumerate in/out */
#define DAOS_ISEQ_OBJ_KEY_ENUM	/* input fields */		 \
	((daos_unit_oid_t)	(oei_oid)		CRT_VAR) \
	((uuid_t)		(oei_pool_uuid)		CRT_VAR) \
	((uuid_t)		(oei_co_hdl)		CRT_VAR) \
	((uuid_t)		(oei_co_uuid)		CRT_VAR) \
	((uint64_t)		(oei_epoch)		CRT_VAR) \
	((uint32_t)		(oei_map_ver)		CRT_VAR) \
	((uint32_t)		(oei_nr)		CRT_VAR) \
	((uint32_t)		(oei_rec_type)		CRT_VAR) \
	((uint32_t)		(oei_pad)		CRT_VAR) \
	((daos_key_t)		(oei_dkey)		CRT_VAR) \
	((daos_key_t)		(oei_akey)		CRT_VAR) \
	((daos_anchor_t)	(oei_anchor)		CRT_VAR) \
	((daos_anchor_t)	(oei_dkey_anchor)	CRT_VAR) \
	((daos_anchor_t)	(oei_akey_anchor)	CRT_VAR) \
	((d_sg_list_t)	(oei_sgl)		CRT_VAR) \
	((crt_bulk_t)		(oei_bulk)		CRT_VAR) \
	((crt_bulk_t)		(oei_kds_bulk)		CRT_VAR)

#define DAOS_OSEQ_OBJ_KEY_ENUM	/* output fields */		 \
	((int32_t)		(oeo_ret)		CRT_VAR) \
	((uint32_t)		(oeo_map_version)	CRT_VAR) \
	((uint32_t)		(oeo_num)		CRT_VAR) \
	((uint32_t)		(oeo_padding)		CRT_VAR) \
	((uint64_t)		(oeo_size)		CRT_VAR) \
	((daos_anchor_t)	(oeo_anchor)		CRT_VAR) \
	((daos_anchor_t)	(oeo_dkey_anchor)	CRT_VAR) \
	((daos_anchor_t)	(oeo_akey_anchor)	CRT_VAR) \
	((daos_key_desc_t)	(oeo_kds)		CRT_ARRAY) \
	((d_sg_list_t)	(oeo_sgl)		CRT_VAR) \
	((daos_recx_t)		(oeo_recxs)		CRT_ARRAY) \
	((daos_epoch_range_t)	(oeo_eprs)		CRT_ARRAY)

CRT_RPC_DECLARE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)

#define DAOS_ISEQ_OBJ_PUNCH	/* input fields */		 \
	((struct dtx_id)	(opi_dti)		CRT_VAR) \
	((uuid_t)		(opi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(opi_co_hdl)		CRT_VAR) \
	((uuid_t)		(opi_co_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(opi_oid)		CRT_VAR) \
	((uint64_t)		(opi_epoch)		CRT_VAR) \
	((uint64_t)		(opi_dkey_hash)		CRT_VAR) \
	((uint32_t)		(opi_map_ver)		CRT_VAR) \
	((uint32_t)		(opi_flags)		CRT_VAR) \
	((struct dtx_id)	(opi_dti_cos)		CRT_ARRAY) \
	((d_iov_t)		(opi_dkeys)		CRT_ARRAY) \
	((d_iov_t)		(opi_akeys)		CRT_ARRAY) \
	((struct daos_shard_tgt) (opi_shard_tgts)	CRT_ARRAY)

#define DAOS_OSEQ_OBJ_PUNCH	/* output fields */		 \
	((int32_t)		(opo_ret)		CRT_VAR) \
	((uint32_t)		(opo_map_version)	CRT_VAR) \
	((uint64_t)		(opo_dkey_conflict)	CRT_VAR) \
	((struct dtx_id)	(opo_dti_conflict)	CRT_VAR)

CRT_RPC_DECLARE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)

#define DAOS_ISEQ_OBJ_QUERY_KEY	/* input fields */		 \
	((uuid_t)		(okqi_co_hdl)		CRT_VAR) \
	((uuid_t)		(okqi_pool_uuid)	CRT_VAR) \
	((uuid_t)		(okqi_co_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(okqi_oid)		CRT_VAR) \
	((uint64_t)		(okqi_epoch)		CRT_VAR) \
	((uint32_t)		(okqi_map_ver)		CRT_VAR) \
	((uint32_t)		(okqi_flags)		CRT_VAR) \
	((daos_key_t)		(okqi_dkey)		CRT_VAR) \
	((daos_key_t)		(okqi_akey)		CRT_VAR) \
	((daos_recx_t)		(okqi_recx)		CRT_VAR)

#define DAOS_OSEQ_OBJ_QUERY_KEY	/* output fields */		 \
	((int32_t)		(okqo_ret)		CRT_VAR) \
	((uint32_t)		(okqo_map_version)	CRT_VAR) \
	((uint32_t)		(okqo_flags)		CRT_VAR) \
	((uint32_t)		(okqo_pad32_1)		CRT_VAR) \
	((daos_key_t)		(okqo_dkey)		CRT_VAR) \
	((daos_key_t)		(okqo_akey)		CRT_VAR) \
	((daos_recx_t)		(okqo_recx)		CRT_VAR)

CRT_RPC_DECLARE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)

static inline int
obj_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req)
{
	crt_opcode_t opcode;

	if (DAOS_FAIL_CHECK(DAOS_OBJ_REQ_CREATE_TIMEOUT))
		return -DER_TIMEDOUT;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_OBJ_MODULE, DAOS_OBJ_VERSION);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_IO, tgt_ep->ep_tag);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

void obj_reply_set_status(crt_rpc_t *rpc, int status);
int obj_reply_get_status(crt_rpc_t *rpc);
void obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version);
uint32_t obj_reply_map_version_get(crt_rpc_t *rpc);
void obj_reply_dtx_conflict_set(crt_rpc_t *rpc, struct dtx_conflict_entry *dce);

static inline bool
obj_is_modification_opc(uint32_t opc)
{
	return opc == DAOS_OBJ_RPC_UPDATE || opc == DAOS_OBJ_RPC_TGT_UPDATE ||
		opc == DAOS_OBJ_RPC_PUNCH || opc == DAOS_OBJ_RPC_TGT_PUNCH ||
		opc == DAOS_OBJ_RPC_PUNCH_DKEYS ||
		opc == DAOS_OBJ_RPC_TGT_PUNCH_DKEYS ||
		opc == DAOS_OBJ_RPC_PUNCH_AKEYS ||
		opc == DAOS_OBJ_RPC_TGT_PUNCH_AKEYS;
}

static inline bool
obj_is_tgt_modification_opc(uint32_t opc)
{
	return opc == DAOS_OBJ_RPC_TGT_UPDATE ||
	       opc == DAOS_OBJ_RPC_TGT_PUNCH ||
	       opc == DAOS_OBJ_RPC_TGT_PUNCH_DKEYS ||
	       opc == DAOS_OBJ_RPC_TGT_PUNCH_AKEYS;
}

#endif /* __DAOS_OBJ_RPC_H__ */
