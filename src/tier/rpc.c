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
/*
 * dct: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(tier)

#include <daos/rpc.h>
#include <daos/event.h>
#include "rpc.h"

struct crt_msg_field *tier_ping_in_fields[] = {
	&CMF_INT
};

struct crt_msg_field *tier_ping_out_fields[] = {
	&CMF_INT
};

struct crt_req_format DQF_TIER_PING =
	DEFINE_CRT_REQ_FMT("TIER_PING", tier_ping_in_fields,
			   tier_ping_out_fields);

struct crt_msg_field *tier_fetch_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_UUID,	/* Container handle uuid */
	&CMF_UINT64,	/* epoch */
};

struct crt_msg_field *tier_fetch_out_fields[] = {
	&CMF_INT,	/* status */
};

struct crt_req_format DQF_TIER_FETCH =
	DEFINE_CRT_REQ_FMT("TIER_FETCH", tier_fetch_in_fields,
			   tier_fetch_out_fields);

struct crt_msg_field *tier_bcast_fetch_in_fields[] = {
	&CMF_UUID,	/* pool id    */
	&CMF_UUID,	/* cont id    */
	&CMF_UINT64,	/* epoch      */
	&CMF_IOVEC	/* global coh */
};

struct crt_req_format DQF_TIER_BCAST_FETCH =
	DEFINE_CRT_REQ_FMT("TIER_BCAST_FETCH", tier_bcast_fetch_in_fields,
			   tier_fetch_out_fields);

struct crt_msg_field *tier_cross_conn_in_fields[] = {
	&CMF_UUID,	/*cci_warm_id*/
	&CMF_STRING,	/*cci_warm_grp*/
};

struct crt_msg_field *tier_cross_conn_out_fields[] = {
	&CMF_INT,	/*cco_ret*/
};

struct crt_req_format DQF_TIER_CROSS_CONN =
	DEFINE_CRT_REQ_FMT("TIER_CROSS_CONN", tier_cross_conn_in_fields,
			   tier_cross_conn_out_fields);

struct crt_msg_field *tier_upstream_conn_in_fields[] = {
	&CMF_UUID,	/*ui_warm_id*/
	&CMF_UUID,	/*ui_cold_id*/
	&CMF_STRING,	/*ui_warm_grp*/
	&CMF_STRING,	/*ui_cold_grp*/
};

struct crt_msg_field *tier_upstream_conn_out_fields[] = {
	&CMF_INT,	/*uo_ret*/
};

struct crt_req_format DQF_TIER_UPSTREAM_CONN =
	DEFINE_CRT_REQ_FMT("TIER_UPSTREAM_CONN", tier_upstream_conn_in_fields,
			   tier_upstream_conn_out_fields);
struct crt_msg_field *tier_register_cold_in_fields[] = {
	&CMF_UUID,	/*rci_cold_id*/
	&CMF_STRING,	/*rci_cold_grp*/
	&CMF_STRING	/*tgt_grp*/
};

struct crt_msg_field *tier_register_cold_out_fields[] = {
	&CMF_INT	/*rco_ret*/
};

struct crt_req_format DQF_TIER_REGISTER_COLD =
	DEFINE_CRT_REQ_FMT("TIER_REGISTER_COLD", tier_register_cold_in_fields,
			   tier_register_cold_out_fields);

struct crt_msg_field *tier_hdl_bcast_in_fields[]  = {
	&CMF_IOVEC,	/*hbi_pool_hdl*/
	&CMF_INT	/*hbi_type*/
};

struct crt_msg_field *tier_hdl_bcast_out_fields[] = {
	&CMF_INT	/*hbo_ret*/
};

struct crt_req_format DQF_TIER_BCAST_HDL =
	DEFINE_CRT_REQ_FMT("TIER_BCAST_HDL", tier_hdl_bcast_in_fields,
			   tier_register_cold_out_fields);

int
tier_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_TIER_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc tier_rpcs[] = {
	{
		.dr_name	= "TIER_PING",
		.dr_opc		= TIER_PING,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_PING,
	}, {
		.dr_name	= "TIER_FETCH",
		.dr_opc		= TIER_FETCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_FETCH,
	}, {
		.dr_name	= "TIER_BCAST_FETCH",
		.dr_opc		= TIER_BCAST_FETCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_BCAST_FETCH,
	}, {
		.dr_name	= "TIER_CROSS_CONN",
		.dr_opc		= TIER_CROSS_CONN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_CROSS_CONN,
	}, {
		.dr_name	= "TIER_UPSTREAM_CONN",
		.dr_opc		= TIER_UPSTREAM_CONN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_UPSTREAM_CONN,
	}, {
		.dr_name	= "TIER_REGISTER_COLD",
		.dr_opc		= TIER_REGISTER_COLD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_REGISTER_COLD,
	}, {
		.dr_name	= "TIER_BCAST_HDL",
		.dr_opc		= TIER_BCAST_HDL,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_BCAST_HDL,
	}, {
		.dr_opc		= 0
	}
};

