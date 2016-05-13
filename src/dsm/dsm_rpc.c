/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsm: RPC Protocol Serialization Functions
 */

#include <daos/event.h>
#include <daos/rpc.h>
#include "dsm_rpc.h"

struct dtp_msg_field DMF_POOL_MAP =
	DEFINE_DTP_MSG("dtp_pool", 0, sizeof(struct pool_map),
			proc_pool_map);

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
	&DMF_POOL_MAP	/* pool map */
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

struct dtp_msg_field *dsm_ping_in_fields[] = {
	&DMF_INT
};

struct dtp_msg_field *dsm_ping_out_fields[] = {
	&DMF_INT
};

struct dtp_req_format DQF_POOL_CONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_CONNECT", pool_connect_in_fields,
			   pool_connect_out_fields);

struct dtp_req_format DQF_POOL_DISCONNECT =
	DEFINE_DTP_REQ_FMT("DSM_POOL_DISCONNECT", pool_disconnect_in_fields,
			    pool_disconnect_out_fields);

struct dtp_req_format DQF_PING =
	DEFINE_DTP_REQ_FMT("DSM_PING", dsm_ping_in_fields, dsm_ping_out_fields);

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
		.dr_name	= "DSM_PING",
		.dr_opc		= DSM_PING,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_PING,
	}, {
		.dr_opc		= 0
	}
};
