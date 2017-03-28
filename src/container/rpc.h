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

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
enum cont_operation {
	CONT_CREATE		 = 1,
	CONT_DESTROY		 = 2,
	CONT_OPEN		 = 3,
	CONT_CLOSE		 = 4,
	CONT_QUERY		 = 5,

	CONT_ATTR_LIST		 = 10,
	CONT_ATTR_SET		 = 11,
	CONT_ATTR_GET		 = 12,

	CONT_EPOCH_QUERY	 = 20,
	CONT_EPOCH_HOLD		 = 21,
	CONT_EPOCH_SLIP		 = 22,
	CONT_EPOCH_FLUSH	 = 23,
	CONT_EPOCH_DISCARD	 = 24,
	CONT_EPOCH_COMMIT	 = 25,
	CONT_EPOCH_WAIT		 = 26,

	CONT_SNAP_LIST		 = 30,
	CONT_SNAP_CREATE	 = 31,
	CONT_SNAP_DESTROY	 = 32,

	CONT_TGT_DESTROY	 = 44,
	CONT_TGT_OPEN		 = 45,
	CONT_TGT_CLOSE		 = 46,
	CONT_TGT_QUERY		 = 47,

	CONT_TGT_EPOCH_FLUSH	 = 50,
	CONT_TGT_EPOCH_DISCARD	 = 51,
	CONT_TGT_EPOCH_AGGREGATE = 52
};

struct cont_op_in {
	uuid_t	ci_pool_hdl;	/* pool handle UUID */
	uuid_t	ci_uuid;	/* container UUID */
	uuid_t	ci_hdl;		/* container handle UUID */
};

struct cont_op_out {
	int32_t		co_rc;		/* operation return code */
	uint32_t	co_map_version;	/* latest pool map version or zero */
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
	daos_epoch_t		tai_start_epoch;
	daos_epoch_t		tai_end_epoch;
};

struct cont_tgt_epoch_aggregate_out {
	int32_t	tao_rc;	/* number of errors */
};

int cont_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
		    crt_opcode_t opc, crt_rpc_t **req);

extern struct daos_rpc cont_rpcs[];
extern struct daos_rpc cont_srv_rpcs[];

#endif /* __CONTAINER_RPC_H__ */
