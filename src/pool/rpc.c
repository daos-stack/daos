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
 * dc_pool/ds_pool: RPC Protocol Serialization Functions
 */
#define DD_SUBSYS	DD_FAC(pool)

#include <daos/rpc.h>
#include "rpc.h"

static struct crt_msg_field DMF_UUIDS =
	DEFINE_CRT_MSG("uuid_t[]", CMF_ARRAY_FLAG, sizeof(uuid_t),
		       crt_proc_uuid_t);

struct crt_msg_field *pool_connect_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT64,	/* capas */
	&CMF_BULK	/* map_bulk */
};

struct crt_msg_field *pool_connect_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32	/* map_buf_size */
};

struct crt_msg_field *pool_disconnect_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_disconnect_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32	/* op.map_version */
};

struct crt_msg_field *pool_query_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_BULK	/* map_bulk */
};

struct crt_msg_field *pool_query_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32	/* map_buf_size */
};

struct crt_msg_field *pool_exclude_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_RANK_LIST	/* targets */
};

struct crt_msg_field *pool_exclude_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&CMF_RANK_LIST	/* targets */
};

struct crt_msg_field *pool_evict_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_evict_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32	/* op.map_version */
};

struct crt_msg_field *pool_tgt_connect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UUID,	/* pool_hdl */
	&CMF_UINT64,	/* capas */
	&CMF_UINT32	/* pool_map_version */
};

struct crt_msg_field *pool_tgt_connect_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *pool_tgt_disconnect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&DMF_UUIDS	/* hdls */
};

struct crt_msg_field *pool_tgt_disconnect_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *pool_tgt_update_map_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UINT32	/* map_version */
};

struct crt_msg_field *pool_tgt_update_map_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_req_format DQF_POOL_CONNECT =
	DEFINE_CRT_REQ_FMT("POOL_CONNECT", pool_connect_in_fields,
			   pool_connect_out_fields);

struct crt_req_format DQF_POOL_DISCONNECT =
	DEFINE_CRT_REQ_FMT("POOL_DISCONNECT", pool_disconnect_in_fields,
			   pool_disconnect_out_fields);

struct crt_req_format DQF_POOL_QUERY =
	DEFINE_CRT_REQ_FMT("POOL_QUERY", pool_query_in_fields,
			   pool_query_out_fields);

struct crt_req_format DQF_POOL_EXCLUDE =
	DEFINE_CRT_REQ_FMT("POOL_EXCLUDE", pool_exclude_in_fields,
			   pool_exclude_out_fields);

struct crt_req_format DQF_POOL_EVICT =
	DEFINE_CRT_REQ_FMT("POOL_EVICT", pool_evict_in_fields,
			   pool_evict_out_fields);

struct crt_req_format DQF_POOL_TGT_CONNECT =
	DEFINE_CRT_REQ_FMT("POOL_TGT_CONNECT", pool_tgt_connect_in_fields,
			   pool_tgt_connect_out_fields);

struct crt_req_format DQF_POOL_TGT_DISCONNECT =
	DEFINE_CRT_REQ_FMT("POOL_TGT_DISCONNECT", pool_tgt_disconnect_in_fields,
			   pool_tgt_disconnect_out_fields);

struct crt_req_format DQF_POOL_TGT_UPDATE_MAP =
	DEFINE_CRT_REQ_FMT("POOL_TGT_UPDATE_MAP", pool_tgt_update_map_in_fields,
			   pool_tgt_update_map_out_fields);

int
pool_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc pool_rpcs[] = {
	{
		.dr_name	= "POOL_CONNECT",
		.dr_opc		= POOL_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_CONNECT,
	}, {
		.dr_name	= "POOL_DISCONNECT",
		.dr_opc		= POOL_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_DISCONNECT,
	}, {
		.dr_name	= "POOL_QUERY",
		.dr_opc		= POOL_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_QUERY
	}, {
		.dr_name	= "POOL_EXCLUDE",
		.dr_opc		= POOL_EXCLUDE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_EXCLUDE
	}, {
		.dr_name	= "POOL_EVICT",
		.dr_opc		= POOL_EVICT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_EVICT
	}, {
		.dr_opc		= 0
	}
};

struct daos_rpc pool_srv_rpcs[] = {
	{
		.dr_name	= "POOL_TGT_CONNECT",
		.dr_opc		= POOL_TGT_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_TGT_CONNECT
	}, {
		.dr_name	= "POOL_TGT_DISCONNECT",
		.dr_opc		= POOL_TGT_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_TGT_DISCONNECT
	}, {
		.dr_name	= "POOL_TGT_UPDATE_MAP",
		.dr_opc		= POOL_TGT_UPDATE_MAP,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_TGT_UPDATE_MAP
	}, {
		.dr_opc		= 0
	}
};
