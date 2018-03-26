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
#define D_LOGFAC       DD_FAC(rebuild)

#include <daos/rpc.h>
#include "rpc.h"

static struct crt_msg_field *rebuild_scan_in_fields[] = {
	&CMF_UUID,	/* pool uuid */
	&CMF_UUID,	/* pool hdl uuid */
	&CMF_UUID,	/* cont hdl uuid */
	&CMF_RANK_LIST,	/* target failed list */
	&CMF_RANK_LIST,	/* service list */
	&CMF_IOVEC,	/* iv ns context */
	&CMF_UINT32,	/* pool iv ns id */
	&CMF_UINT32,	/* pool map version */
	&CMF_UINT32,	/* rebuild version */
	&CMF_UINT32,	/* master rank */
	&CMF_UINT64,	/* term of leader */
};

static struct crt_msg_field *rebuild_scan_out_fields[] = {
	&CMF_RANK_LIST,	/* failed list */
	&CMF_INT,	/* status */
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

struct crt_req_format DQF_REBUILD_OBJECTS_SCAN =
	DEFINE_CRT_REQ_FMT("REBUILD_OBJECTS_SCAN", rebuild_scan_in_fields,
			   rebuild_scan_out_fields);

struct crt_req_format DQF_REBUILD_OBJECTS =
	DEFINE_CRT_REQ_FMT("REBUILD_OBJS", rebuild_objs_in_fields,
			   rebuild_out_fields);

int
rebuild_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
		   crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_REBUILD_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

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
		.dr_opc		= 0
	}
};
