/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
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
	&CMF_BULK_ARRAY,	/* BULK ARRAY */
	&DMF_OBJ_SHARD_TGTS,	/* forward shard tgt array */
	&CMF_UINT32,		/* orw_flags */
};

static struct crt_msg_field *obj_rw_out_fields[] = {
	&CMF_INT,	/* status */
	&CMF_UINT32,	/* map version */
	&CMF_UINT64,	/* object attribute */
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
	&DMF_ANCHOR,	/* hash anchor */
	&DMF_ANCHOR,	/* dkey anchor */
	&DMF_ANCHOR,	/* akey anchor */
	&DMF_SGL_DESC,	/* sgl_descriptor */
	&CMF_BULK,	/* BULK for key buf */
	&CMF_BULK,	/* BULK for kds arrary */
};

static struct crt_msg_field *obj_key_enum_out_fields[] = {
	&CMF_INT,		/* status of the request */
	&CMF_UINT32,		/* map version */
	&CMF_UINT32,		/* number of records */
	&CMF_UINT32,		/* padding */
	&CMF_UINT64,		/* rec size */
	&DMF_ANCHOR,		/* hash anchor */
	&DMF_ANCHOR,		/* dkey hash anchor */
	&DMF_ANCHOR,		/* akey hash anchor */
	&DMF_KEY_DESC_ARRAY,	/* kds array */
	&DMF_SGL,		/* SGL buffer */
	&DMF_RECX_ARRAY,	/* recx buffer */
	&DMF_EPR_ARRAY,		/* epoch range buffer */
};

static struct crt_msg_field *obj_punch_in_fields[] = {
	&CMF_UUID,		/* container handle uuid */
	&CMF_UUID,		/* container uuid */
	&DMF_OID,		/* object ID */
	&CMF_UINT64,		/* epoch */
	&CMF_UINT32,		/* map_version */
	&CMF_UINT32,		/* pad  */
	&DMF_KEY_ARRAY,		/* dkey array */
	&DMF_KEY_ARRAY,		/* akey array */
	&DMF_OBJ_SHARD_TGTS,	/* forward shard tgt array */
};

static struct crt_msg_field *obj_punch_out_fields[] = {
	&CMF_INT,	/* status */
	&CMF_UINT32,	/* map version */
};

static struct crt_msg_field *obj_key_query_in_fields[] = {
	&CMF_UUID,	/* container handle uuid */
	&CMF_UUID,	/* container uuid */
	&DMF_OID,	/* object ID */
	&CMF_UINT64,	/* epoch */
	&CMF_UINT32,	/* map_version */
	&CMF_UINT32,    /* flags */
	&DMF_IOVEC,     /* dkey */
	&DMF_IOVEC,     /* akey */
};

static struct crt_msg_field *obj_key_query_out_fields[] = {
	&CMF_INT,	/* status of the request */
	&CMF_UINT32,	/* map version */
	&CMF_UINT32,	/* number of records */
	&CMF_UINT32,	/* padding */
	&DMF_IOVEC,     /* dkey */
	&DMF_IOVEC,     /* akey */
	&DMF_RECX,	/* recx */
};

static struct crt_req_format DQF_OBJ_UPDATE =
	DEFINE_CRT_REQ_FMT(obj_rw_in_fields, obj_rw_out_fields);

static struct crt_req_format DQF_OBJ_FETCH =
	DEFINE_CRT_REQ_FMT(obj_rw_in_fields, obj_rw_out_fields);

static struct crt_req_format DQF_ENUMERATE =
	DEFINE_CRT_REQ_FMT(obj_key_enum_in_fields, obj_key_enum_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH =
	DEFINE_CRT_REQ_FMT(obj_punch_in_fields, obj_punch_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH_DKEYS =
	DEFINE_CRT_REQ_FMT(obj_punch_in_fields, obj_punch_out_fields);

static struct crt_req_format DQF_OBJ_PUNCH_AKEYS =
	DEFINE_CRT_REQ_FMT(obj_punch_in_fields, obj_punch_out_fields);

static struct crt_req_format DQF_OBJ_KEY_QUERY =
	DEFINE_CRT_REQ_FMT(obj_key_query_in_fields, obj_key_query_out_fields);

/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format obj_proto_rpc_fmt[] = {
	OBJ_PROTO_CLI_RPC_LIST,
};

#undef X

struct crt_proto_format obj_proto_fmt = {
	.cpf_name  = "daos-obj-proto",
	.cpf_ver   = DAOS_OBJ_VERSION,
	.cpf_count = ARRAY_SIZE(obj_proto_rpc_fmt),
	.cpf_prf   = obj_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_OBJ_MODULE, 0)
};

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
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_ret = status;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_ret = status;
		break;
	case DAOS_OBJ_RPC_KEY_QUERY:
		((struct obj_key_query_out *)reply)->okqo_ret = status;
		break;
	default:
		D_ASSERT(0);
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
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_ret;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_ret;
	case DAOS_OBJ_RPC_KEY_QUERY:
		return ((struct obj_key_query_out *)reply)->okqo_ret;
	default:
		D_ASSERT(0);
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
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_map_version =
								map_version;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_KEY_QUERY:
		((struct obj_key_query_out *)reply)->okqo_map_version =
			map_version;
		break;
	default:
		D_ASSERT(0);
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
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_map_version;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_map_version;
	case DAOS_OBJ_RPC_KEY_QUERY:
		return ((struct obj_key_query_out *)reply)->okqo_map_version;
	default:
		D_ASSERT(0);
	}
	return 0;
}

int
daos_proc_obj_shard_tgt(crt_proc_t proc, struct daos_obj_shard_tgt *st)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &st->st_rank);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_shard);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_idx);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_pad);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

struct crt_msg_field DMF_OBJ_SHARD_TGTS =
	DEFINE_CRT_MSG(CMF_ARRAY_FLAG, sizeof(struct daos_obj_shard_tgt),
		       daos_proc_obj_shard_tgt);
