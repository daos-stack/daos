/**
 * (C) Copyright 2017 Intel Corporation.
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
enum pool_operation {
	REBUILD_OBJECTS_SCAN	= 1,
	REBUILD_OBJECTS		= 2,
	REBUILD_TGT		= 3,
	REBUILD_QUERY		= 4,
	REBUILD_TGT_QUERY	= 5,
	REBUILD_FINI		= 6,
	REBUILD_TGT_FINI	= 7,
};

struct rebuild_fini_tgt_in {
	uuid_t		rfti_pool_uuid;
	uint32_t	rfti_pool_map_ver;
	uint32_t	rfti_pad_32;
};

struct rebuild_scan_in {
	uuid_t		rsi_pool_uuid;
	uuid_t		rsi_rebuild_pool_hdl_uuid;
	uuid_t		rsi_rebuild_cont_hdl_uuid;
	daos_rank_list_t *rsi_tgts_failed;
	daos_rank_list_t *rsi_svc_list;
	uint32_t	rsi_pool_map_ver;
};

struct rebuild_out {
	int	ro_status;
};

struct rebuild_objs_in {
	uint32_t                roi_map_ver;
	uint32_t                roi_pad;
	uuid_t			roi_pool_uuid;
	struct crt_array        roi_oids;
	struct crt_array        roi_uuids;
	struct crt_array	roi_shards;
};

struct rebuild_tgt_query_in {
	uuid_t		 rtqi_uuid;
};

struct rebuild_tgt_query_out {
	int		rtqo_status;
	uint32_t	rtqo_rebuilding;
	uint32_t	rtqo_rec_count;
	uint32_t	rtqo_obj_count;
};

struct rebuild_query_in {
	uuid_t		 rqi_pool_uuid;
	daos_rank_list_t *rqi_tgts_failed;
};

struct rebuild_query_out {
	struct daos_rebuild_status	rqo_st;
};

struct rebuild_tgt_in {
	uuid_t		 rti_pool_uuid;
	uint32_t	 rti_map_ver;
	uint32_t	 rti_pad_32;
	daos_rank_list_t *rti_failed_tgts;
};


int rebuild_req_create(crt_context_t dtp_ctx, crt_endpoint_t tgt_ep,
		       crt_opcode_t opc, crt_rpc_t **req);

extern struct daos_rpc rebuild_rpcs[];
extern struct daos_rpc rebuild_cli_rpcs[];


#endif /* __REBUILD_RPC_H__ */
