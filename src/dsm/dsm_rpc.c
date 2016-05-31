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
 * dsm: RPC Protocol Serialization Functions
 */

#include <daos/event.h>
#include <daos/rpc.h>
#include "dsm_rpc.h"

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

struct dtp_msg_field *cont_create_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool_hdl */
	&DMF_UUID	/* cont */
};

struct dtp_msg_field *cont_create_out_fields[] = {
	&DMF_INT	/* rc */
};

struct dtp_msg_field *cont_destroy_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool_hdl */
	&DMF_UUID,	/* cont */
	&DMF_UINT32	/* force */
};

struct dtp_msg_field *cont_destroy_out_fields[] = {
	&DMF_INT	/* rc */
};

struct dtp_msg_field *cont_open_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool_hdl */
	&DMF_UUID,	/* cont */
	&DMF_UUID,	/* cont_hdl */
	&DMF_UINT64	/* capas */
};

struct dtp_msg_field *cont_open_out_fields[] = {
	&DMF_INT,		/* ret */
	&DMF_EPOCH_STATE	/* epoch_state */
};

struct dtp_msg_field *cont_close_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* cont */
	&DMF_UUID	/* cont_hdl */
};

struct dtp_msg_field *cont_close_out_fields[] = {
	&DMF_INT	/* ret */
};

struct dtp_req_format DQF_POOL_CONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_CONNECT", pool_connect_in_fields,
			   pool_connect_out_fields);

struct dtp_req_format DQF_POOL_DISCONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_DISCONNECT", pool_disconnect_in_fields,
			    pool_disconnect_out_fields);

struct dtp_req_format DQF_CONT_CREATE =
	DEFINE_DTP_REQ_FMT("DSM_CONT_CREATE", cont_create_in_fields,
			   cont_create_out_fields);

struct dtp_req_format DQF_CONT_DESTROY =
	DEFINE_DTP_REQ_FMT("DSM_CONT_DESTROY", cont_destroy_in_fields,
			   cont_destroy_out_fields);

struct dtp_req_format DQF_CONT_OPEN =
	DEFINE_DTP_REQ_FMT("DSM_CONT_OPEN", cont_open_in_fields,
			   cont_open_out_fields);

struct dtp_req_format DQF_CONT_CLOSE =
	DEFINE_DTP_REQ_FMT("DSM_CONT_CLOSE", cont_close_in_fields,
			   cont_close_out_fields);

struct dtp_msg_field *dsm_obj_update_in_fields[] = {
	&DMF_OID,	/* object ID */
	&DMF_UUID,	/* container uuid */
	&DMF_UUID,	/* pool uuid */
	&DMF_UINT64,	/* epoch */
	&DMF_UINT32,	/* count of vec_iod and sg */
	&DMF_UINT32,	/* pad */
	&DMF_IOVEC,	/* dkey */
	&DMF_VEC_IOD_ARRAY, /* daos_vector */
	&DMF_BULK_ARRAY,    /* BULK ARRAY */
};

struct dtp_msg_field *dsm_dkey_enumerate_in_fields[] = {
	&DMF_OID,	/* object ID */
	&DMF_UUID,	/* container uuid */
	&DMF_UUID,	/* pool uuid */
	&DMF_UINT64,	/* epoch */
	&DMF_UINT32,	/* number of kds */
	&DMF_UINT32,	/* pad */
	&DMF_DAOS_HASH_OUT, /* hash anchor */
	&DMF_BULK, /* BULK array for dkey */
};

struct dtp_msg_field *dsm_dkey_enumerate_out_fields[] = {
	&DMF_INT,		/* status of the request */
	&DMF_UINT32,		/* pad */
	&DMF_DAOS_HASH_OUT,	/* hash anchor */
	&DMF_KEY_DESC_ARRAY,	/* kds array */
};

struct dtp_req_format DQF_OBJ_RW =
	DEFINE_DTP_REQ_FMT_ARRAY("DSM_OBJ_UPDATE",
				 dsm_obj_update_in_fields,
				 ARRAY_SIZE(dsm_obj_update_in_fields),
				 dtp_single_out_fields, 1);

struct dtp_req_format DQF_DKEY_ENUMERATE =
	DEFINE_DTP_REQ_FMT("DSM_DKEY_ENUMERATE",
			   dsm_dkey_enumerate_in_fields,
			   dsm_dkey_enumerate_out_fields);

int
dsm_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req)
{
	dtp_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_DSM_MODULE, 1);

	return dtp_req_create(dtp_ctx, tgt_ep, opcode, req);
}

struct daos_rpc dsm_rpcs[] = {
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
		.dr_name	= "DSM_CONT_CREATE",
		.dr_opc		= DSM_CONT_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_CREATE
	}, {
		.dr_name	= "DSM_CONT_DESTROY",
		.dr_opc		= DSM_CONT_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_DESTROY
	}, {
		.dr_name	= "DSM_CONT_OPEN",
		.dr_opc		= DSM_CONT_OPEN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_OPEN
	}, {
		.dr_name	= "DSM_CONT_CLOSE",
		.dr_opc		= DSM_CONT_CLOSE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_CLOSE
	}, {
		.dr_name	= "DSM_OBJ_UPDATE",
		.dr_opc		= DSM_TGT_OBJ_UPDATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_RW,
	}, {
		.dr_name	= "DSM_OBJ_FETCH",
		.dr_opc		= DSM_TGT_OBJ_FETCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_RW,
	}, {
		.dr_name	= "DSM_OBJ_ENUMERATE",
		.dr_opc		= DSM_TGT_OBJ_ENUMERATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DKEY_ENUMERATE,
	}, {
		.dr_opc		= 0
	}
};
