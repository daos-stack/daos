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
 * rebuild: RPC
 *
 * This file contains the RPC format for rebuild.
 */
#define DD_SUBSYS       DD_FAC(rebuild)

#include <daos/rpc.h>
#include "rpc.h"

static struct crt_msg_field *rebuild_scan_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_UUID,	/* rebuild pool hdl uuid */
	&CMF_UUID,	/* rebuild cont hdl uuid */
	&CMF_RANK_LIST,	/* failed targets */
	&CMF_UINT32,	/* pool map version */
};

static struct crt_msg_field *rebuild_out_fields[] = {
	&CMF_INT,	/* rebuild status */
};

static struct crt_msg_field *rebuild_objs_in_fields[] = {
	&CMF_UINT32,    /* pool map version */
	&CMF_UINT32,    /* padding */
	&CMF_UUID,
	&DMF_OID_ARRAY, /* obj ids to be rebuilt */
	&DMF_UUID_ARRAY, /* cont ids to be rebuilt */
	&DMF_UINT32_ARRAY, /* obj shards to be rebuilt */
};

static struct crt_msg_field *rebuild_tgt_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_RANK_LIST,	/* failed targets */
};

static struct crt_msg_field *rebuild_tgt_query_in_fields[] = {
	&CMF_UUID,
};

static struct crt_msg_field *rebuild_tgt_query_out_fields[] = {
	&CMF_INT,
	&CMF_UINT32,
};

static struct crt_msg_field *rebuild_query_in_fields[] = {
	&CMF_UUID,
	&CMF_RANK_LIST,
};

static struct crt_msg_field *rebuild_query_out_fields[] = {
	&CMF_INT,
	&CMF_INT,
};

static struct crt_msg_field *rebuild_fini_tgt_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_UINT32,	/* pool map version */
};

struct crt_req_format DQF_REBUILD_OBJECTS_SCAN =
	DEFINE_CRT_REQ_FMT("REBUILD_OBJECTS_SCAN", rebuild_scan_in_fields,
			   rebuild_out_fields);

struct crt_req_format DQF_REBUILD_OBJECTS =
	DEFINE_CRT_REQ_FMT("REBUILD_OBJS", rebuild_objs_in_fields,
			   rebuild_out_fields);

struct crt_req_format DQF_REBUILD_TGT =
	DEFINE_CRT_REQ_FMT("REBUILD_TGT", rebuild_tgt_in_fields,
			   rebuild_out_fields);

struct crt_req_format DQF_REBUILD_TGT_QUERY =
	DEFINE_CRT_REQ_FMT("REBUILD_TGT_QUERY", rebuild_tgt_query_in_fields,
			   rebuild_tgt_query_out_fields);

struct crt_req_format DQF_REBUILD_QUERY =
	DEFINE_CRT_REQ_FMT("REBUILD_QUERY", rebuild_query_in_fields,
			   rebuild_query_out_fields);

struct crt_req_format DQF_REBUILD_TGT_FINI =
	DEFINE_CRT_REQ_FMT("REBUILD_TGT_FINI", rebuild_fini_tgt_in_fields,
			   rebuild_out_fields);

int
rebuild_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
		   crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_REBUILD_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc rebuild_cli_rpcs[] = {
	{
		.dr_name	= "REBUILD_TGT",
		.dr_opc		= REBUILD_TGT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_TGT,
	}, {
		.dr_name	= "REBUILD_QUERY",
		.dr_opc		= REBUILD_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_QUERY,
	}, {
		.dr_name	= "REBUILD_FINI",
		.dr_opc		= REBUILD_FINI,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_TGT,
	}, {
		.dr_opc		= 0
	}
};

struct daos_rpc rebuild_rpcs[] = {
	{
		.dr_name	= "REBUILD_OBJECTS_SCAN",
		.dr_opc		= REBUILD_OBJECTS_SCAN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_OBJECTS_SCAN,
	}, {
		.dr_name	= "REBUILD_OBJECTS",
		.dr_opc		= REBUILD_OBJECTS,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_OBJECTS,
	}, {
		.dr_name	= "REBUILD_QUERY",
		.dr_opc		= REBUILD_TGT_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_TGT_QUERY,
	}, {
		.dr_name	= "REBUILD_TGT_FINI",
		.dr_opc		= REBUILD_TGT_FINI,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_REBUILD_TGT_FINI,
	}, {
		.dr_opc		= 0
	}
};
