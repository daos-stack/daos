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
#define DD_SUBSYS	DD_FAC(tier)

#include <daos/rpc.h>
#include <daos/event.h>
#include "dct_rpc.h"

struct crt_msg_field *dct_ping_in_fields[] = {
	&CMF_INT
};

struct crt_msg_field *dct_ping_out_fields[] = {
	&CMF_INT
};

struct crt_req_format DCT_RF_PING =
	DEFINE_CRT_REQ_FMT("DCT_PING", dct_ping_in_fields, dct_ping_out_fields);

struct crt_msg_field *tier_fetch_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_UUID,	/* pool handle uuid */
	&CMF_UUID,	/* Container handle uuid */
	&CMF_UINT64,    /* epoch */
};

struct crt_msg_field *tier_fetch_out_fields[] = {
	&CMF_INT,	/* status */
};

struct crt_req_format DQF_TIER_FETCH =
	DEFINE_CRT_REQ_FMT("TIER_FETCH", tier_fetch_in_fields,
			   tier_fetch_out_fields);
int
dct_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_TIER_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc dct_rpcs[] = {
	{
		.dr_name	= "DCT_PING",
		.dr_opc		= DCT_PING,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DCT_RF_PING,
	}, {
		.dr_name	= "TIER_FETCH",
		.dr_opc		= TIER_FETCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_TIER_FETCH,
	}, {
		.dr_opc		= 0
	}
};

