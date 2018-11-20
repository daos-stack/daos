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
		0, &DQF_CONT_CREATE,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_DESTROY,							\
		0, &DQF_CONT_DESTROY,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_OPEN,							\
		0, &DQF_CONT_OPEN,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_CLOSE,							\
		0, &DQF_CONT_CLOSE,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_QUERY,							\
		0, &DQF_CONT_QUERY,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_OID_ALLOC,						\
		0, &DQF_CONT_OID_ALLOC,					\
		ds_cont_oid_alloc_handler, NULL),			\
	X(CONT_ATTR_LIST,						\
		0, &DQF_CONT_ATTR_LIST,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_ATTR_GET,						\
		0, &DQF_CONT_ATTR_GET,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_ATTR_SET,						\
		0, &DQF_CONT_ATTR_SET,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_QUERY,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_HOLD,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_SLIP,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_FLUSH,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_DISCARD,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_COMMIT,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_EPOCH_WAIT,						\
		0, &DQF_CONT_EPOCH_OP,					\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_LIST,						\
		0, &DQF_CONT_SNAP_LIST_OP,				\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_CREATE,						\
		0, &DQF_CONT_SNAP_CREATE_OP,				\
		ds_cont_op_handler, NULL),				\
	X(CONT_SNAP_DESTROY,						\
		0, &DQF_CONT_SNAP_DESTROY_OP,				\
		ds_cont_op_handler, NULL)

#define CONT_PROTO_SRV_RPC_LIST						\
	X(CONT_TGT_DESTROY,						\
		0, &DQF_CONT_TGT_DESTROY,				\
		ds_cont_tgt_destroy_handler,				\
		&ds_cont_tgt_destroy_co_ops),				\
	X(CONT_TGT_OPEN,						\
		0, &DQF_CONT_TGT_OPEN,					\
		ds_cont_tgt_open_handler,				\
		&ds_cont_tgt_open_co_ops),				\
	X(CONT_TGT_CLOSE,						\
		0, &DQF_CONT_TGT_CLOSE,					\
		ds_cont_tgt_close_handler,				\
		&ds_cont_tgt_close_co_ops),				\
	X(CONT_TGT_QUERY,						\
		0, &DQF_CONT_TGT_QUERY,					\
		ds_cont_tgt_query_handler,				\
		&ds_cont_tgt_query_co_ops),				\
	X(CONT_TGT_EPOCH_DISCARD,					\
		0, &DQF_CONT_TGT_EPOCH_DISCARD,				\
		ds_cont_tgt_epoch_discard_handler,			\
		&ds_cont_tgt_epoch_discard_co_ops),			\
	X(CONT_TGT_EPOCH_AGGREGATE,					\
		0, &DQF_CONT_TGT_EPOCH_AGGREGATE,			\
		ds_cont_tgt_epoch_aggregate_handler,			\
		&ds_cont_tgt_epoch_aggregate_co_ops)

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

struct cont_op_in {
	uuid_t	ci_pool_hdl;	/* pool handle UUID */
	uuid_t	ci_uuid;	/* container UUID */
	uuid_t	ci_hdl;		/* container handle UUID */
};

struct cont_op_out {
	int32_t			co_rc;		/* operation return code */
	uint32_t		co_map_version;	/* latest map version or zero */
	struct rsvc_hint	co_hint;	/* leadership info */
};

struct cont_create_in {
	struct cont_op_in	cci_op;	/* .ci_hdl unused */
};

struct cont_create_out {
	struct cont_op_out	cco_op;
};

struct cont_destroy_in {
	struct cont_op_in	cdi_op;		/* .ci_hdl unused */
	uint32_t		cdi_force;	/* evict all handles */
};

struct cont_destroy_out {
	struct cont_op_out	cdo_op;
};

struct cont_open_in {
	struct cont_op_in	coi_op;
	uint64_t		coi_capas;
};

struct cont_open_out {
	struct cont_op_out	coo_op;
	daos_epoch_state_t	coo_epoch_state;
};

struct cont_query_in {
	struct cont_op_in	cqi_op;
};

/** Add more items to query when needed */
struct cont_query_out {
	struct cont_op_out	cqo_op;
	/* min slipped epoch at all streams */
	uint64_t		cqo_min_slipped_epoch;
	daos_epoch_state_t	cqo_epoch_state;
};

struct cont_oid_alloc_in {
	struct cont_op_in	coai_op;
	daos_size_t		num_oids;
};

struct cont_oid_alloc_out {
	struct cont_op_out      coao_op;
	uint64_t		oid;
};

struct cont_attr_list_in {
	struct cont_op_in	cali_op;
	crt_bulk_t		cali_bulk;
};

struct cont_attr_list_out {
	struct cont_op_out	calo_op;
	uint64_t		calo_size;
};

struct cont_attr_get_in {
	struct cont_op_in	cagi_op;
	uint64_t		cagi_count;
	uint64_t		cagi_key_length;
	crt_bulk_t		cagi_bulk;
};

struct cont_attr_get_out {
	struct cont_op_out	cago_op;
};

struct cont_attr_set_in {
	struct cont_op_in	casi_op;
	uint64_t		casi_count;
	crt_bulk_t		casi_bulk;
};

struct cont_attr_set_out {
	struct cont_op_out	caso_op;
};

struct cont_close_in {
	struct cont_op_in	cci_op;
};

struct cont_close_out {
	struct cont_op_out	cco_op;
};

struct cont_epoch_op_in {
	struct cont_op_in	cei_op;
	daos_epoch_t		cei_epoch;	/* unused for EPOCH_QUERY */
};

struct cont_epoch_op_out {
	struct cont_op_out	ceo_op;
	daos_epoch_state_t	ceo_epoch_state;
};

struct cont_snap_list_in {
	struct cont_op_in	sli_op;
	crt_bulk_t		sli_bulk;
};

struct cont_snap_list_out {
	struct cont_op_out	slo_op;
	uint32_t		slo_count;
};

struct cont_tgt_destroy_in {
	uuid_t	tdi_pool_uuid;
	uuid_t	tdi_uuid;
};

struct cont_tgt_destroy_out {
	int32_t tdo_rc;	/* number of errors */
};

struct cont_tgt_open_in {
	uuid_t		toi_pool_uuid;
	uuid_t		toi_pool_hdl;
	uuid_t		toi_uuid;
	uuid_t		toi_hdl;
	uint64_t	toi_capas;
};

struct cont_tgt_open_out {
	int32_t	too_rc;	/* number of errors */
};

struct cont_tgt_close_rec {
	uuid_t		tcr_hdl;
	daos_epoch_t	tcr_hce;
};

struct cont_tgt_close_in {
	struct crt_array	tci_recs;	/* cont_tgt_close_rec[] */
};

struct cont_tgt_close_out {
	int32_t	tco_rc;	/* number of errors */
};

struct cont_tgt_query_in {
	uuid_t		tqi_pool_uuid;
	uuid_t		tqi_cont_uuid;
};

struct cont_tgt_query_out {
	int32_t		tqo_rc;
	int32_t		tqo_pad32;
	daos_epoch_t	tqo_min_purged_epoch;
};

struct cont_tgt_epoch_discard_in {
	uuid_t		tii_hdl;
	daos_epoch_t	tii_epoch;
};

struct cont_tgt_epoch_discard_out {
	int32_t	tio_rc;	/* number of errors */
};

struct cont_tgt_epoch_aggregate_in {
	uuid_t			tai_cont_uuid;
	uuid_t			tai_pool_uuid;
	struct crt_array	tai_epr_list;
};

struct cont_tgt_epoch_aggregate_out {
	int32_t	tao_rc;	/* number of errors */
};

static inline int
cont_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_CONT_MODULE, DAOS_CONT_VERSION);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

#endif /* __CONTAINER_RPC_H__ */
