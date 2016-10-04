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
	&DMF_UINT32,		/* padding */
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

struct dtp_msg_field *cont_op_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* cont */
	&DMF_UUID	/* cont_hdl */
};

struct dtp_msg_field *epoch_op_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* cont */
	&DMF_UUID,	/* cont_hdl */
	&DMF_UINT64	/* epoch */
};

struct dtp_msg_field *epoch_op_out_fields[] = {
	&DMF_INT,		/* ret */
	&DMF_UINT32,		/* padding */
	&DMF_EPOCH_STATE	/* epoch_state */
};

struct dtp_msg_field *tgt_cont_destroy_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID	/* cont */
};

struct dtp_msg_field *tgt_cont_destroy_out_fields[] = {
	&DMF_INT	/* ret */
};

struct dtp_msg_field *tgt_cont_open_in_fields[] = {
	&DMF_UUID,	/* pool */
	&DMF_UUID,	/* pool_hdl */
	&DMF_UUID,	/* cont */
	&DMF_UUID,	/* cont_hdl */
	&DMF_UINT64	/* capas */
};

struct dtp_msg_field *tgt_cont_open_out_fields[] = {
	&DMF_INT	/* ret */
};

struct dtp_msg_field *tgt_cont_close_in_fields[] = {
	&DMF_UUID	/* cont_hdl */
};

struct dtp_msg_field *tgt_cont_close_out_fields[] = {
	&DMF_INT	/* ret */
};

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

struct dtp_req_format DQF_EPOCH_QUERY =
	DEFINE_DTP_REQ_FMT("DSM_EPOCH_QUERY", cont_op_in_fields,
			   epoch_op_out_fields);

struct dtp_req_format DQF_EPOCH_OP =
	DEFINE_DTP_REQ_FMT("DSM_EPOCH_OP", epoch_op_in_fields,
			   epoch_op_out_fields);

struct dtp_req_format DQF_TGT_CONT_DESTROY =
	DEFINE_DTP_REQ_FMT("DSM_TGT_CONT_DESTROY", tgt_cont_destroy_in_fields,
			   tgt_cont_destroy_out_fields);

struct dtp_req_format DQF_TGT_CONT_OPEN =
	DEFINE_DTP_REQ_FMT("DSM_TGT_CONT_OPEN", tgt_cont_open_in_fields,
			   tgt_cont_open_out_fields);

struct dtp_req_format DQF_TGT_CONT_CLOSE =
	DEFINE_DTP_REQ_FMT("DSM_TGT_CONT_CLOSE", tgt_cont_close_in_fields,
			   tgt_cont_close_out_fields);

int
cont_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req)
{
	dtp_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_CONT_MODULE, 1);

	return dtp_req_create(dtp_ctx, tgt_ep, opcode, req);
}

struct daos_rpc cont_rpcs[] = {
	{
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
		.dr_name	= "DSM_CONT_EPOCH_QUERY",
		.dr_opc		= DSM_CONT_EPOCH_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_EPOCH_QUERY
	}, {
		.dr_name	= "DSM_CONT_EPOCH_HOLD",
		.dr_opc		= DSM_CONT_EPOCH_HOLD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_EPOCH_OP
	}, {
		.dr_name	= "DSM_CONT_EPOCH_COMMIT",
		.dr_opc		= DSM_CONT_EPOCH_COMMIT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_EPOCH_OP
	}, {
		.dr_name	= "DSM_TGT_CONT_DESTROY",
		.dr_opc		= DSM_TGT_CONT_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TGT_CONT_DESTROY
	}, {
		.dr_opc		= 0
	}
};

struct daos_rpc cont_srv_rpcs[] = {
	{
		.dr_name	= "DSM_TGT_CONT_OPEN",
		.dr_opc		= DSM_TGT_CONT_OPEN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TGT_CONT_OPEN
	}, {
		.dr_name	= "DSM_TGT_CONT_CLOSE",
		.dr_opc		= DSM_TGT_CONT_CLOSE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TGT_CONT_CLOSE
	}, {
		.dr_opc		= 0
	}
};
