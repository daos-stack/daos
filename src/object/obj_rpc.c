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
 * DSR: RPC Protocol Serialization Functions
 */
#define DDSUBSYS	DDFAC(object)

#include <daos/event.h>
#include <daos/rpc.h>
#include "obj_rpc.h"

static struct crt_msg_field *obj_rw_in_fields[] = {
	&DMF_OID,	/* object ID */
	&CMF_UUID,	/* container handle uuid */
	&CMF_UUID,	/* container uuid */
	&CMF_UINT64,	/* epoch */
	&CMF_UINT32,	/* map_version */
	&CMF_UINT32,	/* count of iod and sg */
	&CMF_IOVEC,	/* dkey */
	&DMF_IOD_ARRAY, /* I/O descriptor array */
	&DMF_SGL_ARRAY, /* scatter/gather array */
	&CMF_BULK_ARRAY,    /* BULK ARRAY */
};

static struct crt_msg_field *obj_rw_out_fields[] = {
	&CMF_INT,	/* status */
	&CMF_UINT32,	/* map version */
	&DMF_REC_SIZE_ARRAY, /* actual size of records */
	&DMF_NR_ARRAY, /* array of sgl nr */
	&DMF_SGL_ARRAY, /* return buffer */
};

static struct crt_msg_field *obj_key_enum_in_fields[] = {
	&DMF_OID,	/* object ID */
	&CMF_UUID,	/* container handle uuid */
	&CMF_UUID,	/* container uuid */
	&CMF_UINT64,	/* epoch */
	&CMF_UINT32,	/* map_version */
	&CMF_UINT32,	/* number of kds */
	&CMF_UINT32,	/* list type SINGLE/ARRAY/NONE */
	&CMF_UINT32,	/* pad  */
	&DMF_IOVEC,     /* dkey */
	&DMF_IOVEC,     /* akey */
	&DMF_HASH_OUT,	/* hash anchor */
	&DMF_SGL_DESC,	/* sgl_descriptor */
	&CMF_BULK,	/* BULK array for dkey */
};

static struct crt_msg_field *obj_key_enum_out_fields[] = {
	&CMF_INT,		/* status of the request */
	&CMF_UINT32,		/* map version */
	&DMF_HASH_OUT,		/* hash anchor */
	&CMF_UINT64,		/* size of the record */
	&DMF_KEY_DESC_ARRAY,	/* kds array */
	&DMF_RECX_ARRAY,	/* recx array */
	&DMF_EPR_ARRAY,		/* epoch range array */
	&DMF_UUID_ARRAY,	/* cookie array */
	&DMF_UINT32_ARRAY,	/* vers array */
	&DMF_SGL,		/* SGL buffer */
};

static struct crt_msg_field *obj_punch_in_fields[] = {
	&CMF_UUID,	/* container handle uuid */
	&CMF_UUID,	/* container uuid */
	&DMF_OID,	/* object ID */
	&CMF_UINT64,	/* epoch */
	&CMF_UINT32,	/* map_version */
	&CMF_UINT32,	/* pad  */
	&DMF_KEY_ARRAY,	/* dkey array */
	&DMF_KEY_ARRAY,	/* akey array */
};

static struct crt_msg_field *obj_punch_out_fields[] = {
	&CMF_INT,	/* status */
	&CMF_UINT32,	/* map version */
};

static struct crt_req_format DQF_OBJ_UPDATE =
	DEFINE_CRT_REQ_FMT("DAOS_OBJ_UPDATE",
			   obj_rw_in_fields,
			   obj_rw_out_fields);

static struct crt_req_format DQF_OBJ_FETCH =
	DEFINE_CRT_REQ_FMT("DAOS_OBJ_FETCH",
			   obj_rw_in_fields,
			   obj_rw_out_fields);

static struct crt_req_format DQF_DKEY_ENUMERATE =
	DEFINE_CRT_REQ_FMT("DAOS_DKEY_ENUM",
			   obj_key_enum_in_fields,
			   obj_key_enum_out_fields);

struct crt_req_format DQF_AKEY_ENUMERATE =
	DEFINE_CRT_REQ_FMT("DSR_DKEY_AKEY_ENUMERATE",
			   obj_key_enum_in_fields,
			   obj_key_enum_out_fields);

struct crt_req_format DQF_REC_ENUMERATE =
	DEFINE_CRT_REQ_FMT("DSR_REC_ENUMERATE",
			   obj_key_enum_in_fields,
			   obj_key_enum_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH =
	DEFINE_CRT_REQ_FMT("DAOS_OBJ_PUNCH",
			   obj_punch_in_fields,
			   obj_punch_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH_DKEYS =
	DEFINE_CRT_REQ_FMT("DAOS_OBJ_PUNCH_DKEYS",
			   obj_punch_in_fields,
			   obj_punch_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH_AKEYS =
	DEFINE_CRT_REQ_FMT("DAOS_OBJ_PUNCH_AKEYS",
			   obj_punch_in_fields,
			   obj_punch_out_fields);

struct daos_rpc daos_obj_rpcs[] = {
	{
		.dr_name	= "DAOS_OBJ_UPDATE",
		.dr_opc		= DAOS_OBJ_RPC_UPDATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_UPDATE,
	}, {
		.dr_name	= "DAOS_OBJ_FETCH",
		.dr_opc		= DAOS_OBJ_RPC_FETCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_FETCH,
	}, {
		.dr_name	= "DAOS_DKEY_ENUM",
		.dr_opc		= DAOS_OBJ_DKEY_RPC_ENUMERATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DKEY_ENUMERATE,
	}, {
		.dr_name        = "DAOS_AKEY_ENUM",
		.dr_opc         = DAOS_OBJ_AKEY_RPC_ENUMERATE,
		.dr_ver         = 1,
		.dr_flags       = 0,
		.dr_req_fmt     = &DQF_AKEY_ENUMERATE,
	}, {
		.dr_name        = "DAOS_REC_ENUM",
		.dr_opc         = DAOS_OBJ_RECX_RPC_ENUMERATE,
		.dr_ver         = 1,
		.dr_flags       = 0,
		.dr_req_fmt     = &DQF_REC_ENUMERATE,
	}, {
		.dr_name	= "DAOS_OBJ_PUNCH",
		.dr_opc		= DAOS_OBJ_RPC_PUNCH,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_PUNCH,
	}, {
		.dr_name	= "DAOS_OBJ_PUNCH_DKEYS",
		.dr_opc		= DAOS_OBJ_RPC_PUNCH_DKEYS,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_PUNCH_DKEYS,
	}, {
		.dr_name	= "DAOS_OBJ_PUNCH_AKEYS",
		.dr_opc		= DAOS_OBJ_RPC_PUNCH_AKEYS,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_OBJ_PUNCH_AKEYS,
	}, {
		.dr_opc		= 0
	}
};

int
obj_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t opcode;

	if (DAOS_FAIL_CHECK(DAOS_OBJ_REQ_CREATE_TIMEOUT))
		return -DER_TIMEDOUT;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_OBJ_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

void
obj_reply_set_status(crt_rpc_t *rpc, int status)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		((struct obj_rw_out *)reply)->orw_ret = status;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_ret = status;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_ret = status;
		break;
	default:
		D__ASSERT(0);
	}
}

int
obj_reply_get_status(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_ret;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_ret;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_ret;
	default:
		D__ASSERT(0);
	}
	return 0;
}

void
obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		((struct obj_rw_out *)reply)->orw_map_version = map_version;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_map_version =
								map_version;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_map_version = map_version;
		break;
	default:
		D__ASSERT(0);
	}
}

uint32_t
obj_reply_map_version_get(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_map_version;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_map_version;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_map_version;
	default:
		D__ASSERT(0);
	}
	return 0;
}
