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

#include <daos/rpc.h>
#include "rpc.h"

struct dtp_msg_field *pool_connect_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool hdl */
	&DMF_UINT32,  /* uid */
	&DMF_UINT32,  /* gid */
	&DMF_UINT64,	/* capas */
	&DMF_BULK	/* pool map */
};

struct dtp_msg_field *pool_connect_out_fields[] = {
	&DMF_INT,	/* ret */
	&DMF_UINT32,	/* mode */
	&DMF_UINT32,	/* pool_map_version */
	&DMF_UINT32	/* pool_map_buf_size */
};

/*
 * "pool" helps the server side to quickly locate the file that should store
 * "pool_hdl".
 */
struct dtp_msg_field *pool_disconnect_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool hdl */
};

struct dtp_msg_field *pool_disconnect_out_fields[] = {
	&DMF_INT
};

struct dtp_msg_field *tgt_pool_connect_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool_hdl */
	&DMF_UINT64,	/* capas */
	&DMF_UINT32	/* pool_map_version */
};

struct dtp_msg_field *tgt_pool_connect_out_fields[] = {
	&DMF_INT	/* ret */
};

struct dtp_msg_field *tgt_pool_disconnect_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID	/* pool_hdl */
};

struct dtp_msg_field *tgt_pool_disconnect_out_fields[] = {
	&DMF_INT	/* ret */
};

struct dtp_req_format DQF_POOL_CONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_CONNECT", pool_connect_in_fields,
			   pool_connect_out_fields);

struct dtp_req_format DQF_POOL_DISCONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_DISCONNECT", pool_disconnect_in_fields,
			    pool_disconnect_out_fields);

struct dtp_req_format DQF_TGT_POOL_CONNECT =
	DEFINE_DTP_REQ_FMT("DSM_TGT_POOL_CONNECT", tgt_pool_connect_in_fields,
			   tgt_pool_connect_out_fields);

struct dtp_req_format DQF_TGT_POOL_DISCONNECT =
	DEFINE_DTP_REQ_FMT("DSM_TGT_POOL_DISCONNECT",
			   tgt_pool_disconnect_in_fields,
			   tgt_pool_disconnect_out_fields);

int
pool_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req)
{
	dtp_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, 1);

	return dtp_req_create(dtp_ctx, tgt_ep, opcode, req);
}

struct daos_rpc pool_rpcs[] = {
	{
		.dr_name	= "DSM_POOL_CONNECT",
		.dr_opc		= DSM_POOL_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_CONNECT,
	}, {
		.dr_name	= "DSM_POOL_DISCONNECT",
		.dr_opc		= DSM_POOL_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_POOL_DISCONNECT,
	}, {
		.dr_opc		= 0
	}
};

struct daos_rpc pool_srv_rpcs[] = {
	{
		.dr_name	= "DSM_TGT_POOL_CONNECT",
		.dr_opc		= DSM_TGT_POOL_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TGT_POOL_CONNECT
	}, {
		.dr_name	= "DSM_TGT_POOL_DISCONNECT",
		.dr_opc		= DSM_TGT_POOL_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TGT_POOL_DISCONNECT
	}, {
		.dr_opc		= 0
	}
};
