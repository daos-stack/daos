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
#define D_LOGFAC	DD_FAC(pool)

#include <daos/rpc.h>
#include "rpc.h"

struct crt_msg_field *pool_create_in_fields[] = {
	&CMF_UUID,		/* op.uuid */
	&CMF_UUID,		/* op.hdl */
	&CMF_UINT32,		/* uid */
	&CMF_UINT32,		/* gid */
	&CMF_UINT32,		/* mode */
	&CMF_UINT32,		/* ntgts */
	&DMF_UUID_ARRAY,	/* tgt_uuids */
	&CMF_RANK_LIST,		/* tgt_ranks */
	&CMF_UINT32,		/* ndomains */
	&CMF_UINT32,		/* padding */
	&DMF_UINT32_ARRAY	/* domains */
};

struct crt_msg_field *pool_create_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version (unused) */
	&DMF_RSVC_HINT	/* op.hint */
};

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
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32,	/* map_buf_size */
	&CMF_UINT32,	/* rebuild_st.version */
	&CMF_UINT32,	/* rebuild_st.pad_32 */
	&CMF_INT,	/* rebuild_st.errno */
	&CMF_INT,	/* rebuild_st.done */
	&CMF_UINT64,	/* rebuild_st.toberb_obj_nr */
	&CMF_UINT64,	/* rebuild_st.obj_nr */
	&CMF_UINT64,	/* rebuild_st.rec_nr */
};

struct crt_msg_field *pool_disconnect_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_disconnect_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_query_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_BULK	/* map_bulk */
};

struct crt_msg_field *pool_query_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32,	/* map_buf_size */
	&CMF_UINT32,	/* rebuild_st.version */
	&CMF_UINT32,	/* rebuild_st.pad_32 */
	&CMF_INT,	/* rebuild_st.errno */
	&CMF_INT,	/* rebuild_st.done */
	&CMF_UINT64,	/* rebuild_st.toberb_obj_nr */
	&CMF_UINT64,	/* rebuild_st.obj_nr */
	&CMF_UINT64,	/* rebuild_st.rec_nr */
};

struct crt_msg_field *pool_attr_list_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *pool_attr_list_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&CMF_UINT64		/* names size */
};

struct crt_msg_field *pool_attr_get_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT64,	/* count */
	&CMF_UINT64,	/* key length */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *pool_attr_get_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *pool_attr_set_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT64,	/* count */
	&CMF_BULK	/* bulk */
};

struct crt_msg_field *pool_attr_set_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *pool_tgt_update_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_RANK_LIST	/* targets */
};

struct crt_msg_field *pool_tgt_update_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_RANK_LIST	/* targets */
};

struct crt_msg_field *pool_evict_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_evict_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_svc_stop_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_svc_stop_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_tgt_connect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UUID,	/* pool_hdl */
	&CMF_UINT64,	/* capas */
	&CMF_UINT32,	/* pool_map_version */
	&CMF_UINT32,	/* iv class id */
	&CMF_UINT32,	/* master rank */
	&CMF_UINT32,	/* padding */
	&CMF_IOVEC	/* iv context */
};

struct crt_msg_field *pool_tgt_connect_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *pool_tgt_disconnect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&DMF_UUID_ARRAY	/* hdls */
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

struct crt_req_format DQF_POOL_CREATE =
	DEFINE_CRT_REQ_FMT("POOL_CREATE", pool_create_in_fields,
			   pool_create_out_fields);

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
	DEFINE_CRT_REQ_FMT("POOL_EXCLUDE", pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_EXCLUDE_OUT =
	DEFINE_CRT_REQ_FMT("POOL_EXCLUDE_OUT", pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_ADD =
	DEFINE_CRT_REQ_FMT("POOL_ADD", pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_EVICT =
	DEFINE_CRT_REQ_FMT("POOL_EVICT", pool_evict_in_fields,
			   pool_evict_out_fields);

struct crt_req_format DQF_POOL_SVC_STOP =
	DEFINE_CRT_REQ_FMT("POOL_SVC_STOP", pool_svc_stop_in_fields,
			   pool_svc_stop_out_fields);

struct crt_req_format DQF_POOL_ATTR_LIST =
	DEFINE_CRT_REQ_FMT("POOL_ATTR_LIST",
			   pool_attr_list_in_fields,
			   pool_attr_list_out_fields);

struct crt_req_format DQF_POOL_ATTR_GET =
	DEFINE_CRT_REQ_FMT("POOL_ATTR_GET",
			   pool_attr_get_in_fields,
			   pool_attr_get_out_fields);

struct crt_req_format DQF_POOL_ATTR_SET =
	DEFINE_CRT_REQ_FMT("POOL_ATTR_SET",
			   pool_attr_set_in_fields,
			   pool_attr_set_out_fields);

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
pool_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc pool_rpcs[] = {
	{
		.dr_name	= "POOL_CREATE",
		.dr_opc		= POOL_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_CREATE,
	}, {
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
		.dr_name	= "POOL_ADD",
		.dr_opc		= POOL_ADD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_ADD,
	}, {
		.dr_name	= "POOL_EXCLUDE_OUT",
		.dr_opc		= POOL_EXCLUDE_OUT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_EXCLUDE_OUT,
	}, {
		.dr_name	= "POOL_SVC_STOP",
		.dr_opc		= POOL_SVC_STOP,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_SVC_STOP,
	}, {
		.dr_name	= "POOL_ATTR_LIST",
		.dr_opc		= POOL_ATTR_LIST,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_ATTR_LIST,
	}, {
		.dr_name	= "POOL_ATTR_GET",
		.dr_opc		= POOL_ATTR_GET,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_ATTR_GET,
	}, {
		.dr_name	= "POOL_ATTR_SET",
		.dr_opc		= POOL_ATTR_SET,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_ATTR_SET,
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
