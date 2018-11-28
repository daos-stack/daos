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

#define OBJ_BULK_LIMIT	(2 * 1024) /* 2KB bytes */

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
		0, &DQF_OBJ_UPDATE,					\
		ds_obj_rw_handler, NULL),				\
	X(DAOS_OBJ_RPC_FETCH,						\
		0, &DQF_OBJ_FETCH,					\
		ds_obj_rw_handler, NULL),				\
	X(DAOS_OBJ_DKEY_RPC_ENUMERATE,					\
		0, &DQF_ENUMERATE,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_AKEY_RPC_ENUMERATE,					\
		0, &DQF_ENUMERATE,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RECX_RPC_ENUMERATE,					\
		0, &DQF_ENUMERATE,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RPC_ENUMERATE,					\
		0, &DQF_ENUMERATE,					\
		ds_obj_enum_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH,						\
		0, &DQF_OBJ_PUNCH,					\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH_DKEYS,					\
		0, &DQF_OBJ_PUNCH_DKEYS,				\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_PUNCH_AKEYS,					\
		0, &DQF_OBJ_PUNCH_AKEYS,				\
		ds_obj_punch_handler, NULL),				\
	X(DAOS_OBJ_RPC_KEY_QUERY,					\
		0, &DQF_OBJ_KEY_QUERY,					\
		ds_obj_key_query_handler, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum obj_rpc_opc {
	OBJ_PROTO_CLI_RPC_LIST,
	OBJ_PROTO_CLI_COUNT,
	OBJ_PROTO_CLI_LAST = OBJ_PROTO_CLI_COUNT - 1,
};

#undef X

extern struct crt_proto_format obj_proto_fmt;

/* rw flags (orw_flags) */
#define ORW_FLAG_BULK_BIND	(0x1)

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
	struct crt_array	orw_shard_tgts;
	uint32_t		orw_flags;
};

/* reply for update/fetch */
struct obj_rw_out {
	int32_t			orw_ret;
	uint32_t		orw_map_version;
	uint64_t		orw_attr;
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
	daos_anchor_t		oei_anchor;
	daos_anchor_t		oei_dkey_anchor;
	daos_anchor_t		oei_akey_anchor;
	daos_sg_list_t		oei_sgl;
	crt_bulk_t		oei_bulk;
	crt_bulk_t		oei_kds_bulk;
};

struct obj_key_enum_out {
	int32_t			oeo_ret;
	uint32_t		oeo_map_version;
	uint32_t		oeo_num;
	uint32_t		oeo_padding;
	uint64_t		oeo_size;
	daos_anchor_t		oeo_anchor;
	daos_anchor_t		oeo_dkey_anchor;
	daos_anchor_t		oeo_akey_anchor;
	struct crt_array	oeo_kds;
	daos_sg_list_t		oeo_sgl;
	struct crt_array	oeo_recxs;
	struct crt_array	oeo_eprs;
};

struct obj_punch_in {
	uuid_t			opi_co_hdl;
	uuid_t			opi_co_uuid;
	daos_unit_oid_t		opi_oid;
	uint64_t		opi_epoch;
	uint32_t		opi_map_ver;
	uint32_t		opi_pad32_1;
	struct crt_array	opi_dkeys;
	struct crt_array	opi_akeys;
	struct crt_array	opi_shard_tgts;
};

/* reply for update/fetch */
struct obj_punch_out {
	int32_t			opo_ret;
	uint32_t		opo_map_version;
};

/* object key query in */
struct obj_key_query_in {
	uuid_t			okqi_co_hdl;
	uuid_t			okqi_co_uuid;
	daos_unit_oid_t		okqi_oid;
	uint64_t		okqi_epoch;
	uint32_t		okqi_map_ver;
	uint32_t		okqi_flags;
	daos_key_t		okqi_dkey;
	daos_key_t		okqi_akey;
	daos_recx_t		okqi_recx;
};

/* object key query out */
struct obj_key_query_out {
	int32_t			okqo_ret;
	uint32_t		okqo_map_version;
	uint32_t		okqo_flags;
	uint32_t		okqo_pad32_1;
	daos_key_t		okqo_dkey;
	daos_key_t		okqo_akey;
	daos_recx_t		okqo_recx;
};

/** to identify each obj shard's target */
struct daos_obj_shard_tgt {
	uint32_t		st_rank;	/* rank of the shard */
	uint32_t		st_shard;	/* shard index */
	uint32_t		st_tgt_idx;	/* target xstream index */
	uint32_t		st_pad;		/* padding */
};

static inline int
obj_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req)
{
	crt_opcode_t opcode;

	if (DAOS_FAIL_CHECK(DAOS_OBJ_REQ_CREATE_TIMEOUT))
		return -DER_TIMEDOUT;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_OBJ_MODULE, DAOS_OBJ_VERSION);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

void obj_reply_set_status(crt_rpc_t *rpc, int status);
int obj_reply_get_status(crt_rpc_t *rpc);
void obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version);
uint32_t obj_reply_map_version_get(crt_rpc_t *rpc);
int daos_proc_obj_shard_tgt(crt_proc_t proc, struct daos_obj_shard_tgt *st);
extern struct crt_msg_field DMF_OBJ_SHARD_TGTS;

#endif /* __DAOS_OBJ_RPC_H__ */
