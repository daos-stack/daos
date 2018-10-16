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
 * dc_cont, ds_cont: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos/rpc.h>
#include "rpc.h"

static int
proc_cont_tgt_close_rec(crt_proc_t proc, struct cont_tgt_close_rec *rec)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &rec->tcr_hdl);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &rec->tcr_hce);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static struct crt_msg_field DMF_CLOSE_RECS =
	DEFINE_CRT_MSG(CMF_ARRAY_FLAG, sizeof(struct cont_tgt_close_rec),
		       proc_cont_tgt_close_rec);

struct crt_msg_field *cont_op_out_fields[] = {
	&CMF_INT,		/* rc */
	&CMF_UINT32,		/* map_version */
	&DMF_RSVC_HINT		/* hint */
};

struct crt_msg_field *cont_create_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.hdl */
};

struct crt_msg_field *cont_create_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *cont_destroy_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT32	/* force */
};

struct crt_msg_field *cont_destroy_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *cont_open_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT64	/* capas */
};

struct crt_msg_field *cont_open_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&DMF_EPOCH_STATE	/* epoch_state */
};

struct crt_msg_field *cont_close_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.hdl */
};

struct crt_msg_field *cont_close_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *cont_query_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.hdl */
};

struct crt_msg_field *cont_query_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&CMF_UINT64,		/* min slipped epoch */
	&DMF_EPOCH_STATE	/* epoch state */
};

struct crt_msg_field *cont_oid_alloc_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT64,	/* num_oids */
};

struct crt_msg_field *cont_oid_alloc_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_UINT64,	/* oid value */
};

struct crt_msg_field *cont_attr_list_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *cont_attr_list_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&CMF_UINT64		/* names size */
};

struct crt_msg_field *cont_attr_get_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT64,	/* count */
	&CMF_UINT64,	/* key length */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *cont_attr_get_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *cont_attr_set_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT64,	/* count */
	&CMF_BULK	/* bulk */
};

struct crt_msg_field *cont_attr_set_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *cont_epoch_op_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_UINT64	/* epoch */
};

struct crt_msg_field *cont_epoch_op_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&DMF_EPOCH_STATE	/* epoch_state */
};

struct crt_msg_field *cont_snap_list_in_fields[] = {
	&CMF_UUID,	/* op.pool_hdl */
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.hdl */
	&CMF_BULK	/* snapshots bulk */
};

struct crt_msg_field *cont_snap_list_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&CMF_UINT32		/* list size */
};

struct crt_msg_field *cont_tgt_destroy_in_fields[] = {
	&CMF_UUID,	/* pool_uuid */
	&CMF_UUID	/* uuid */
};

struct crt_msg_field *cont_tgt_destroy_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *cont_tgt_open_in_fields[] = {
	&CMF_UUID,	/* pool_uuid */
	&CMF_UUID,	/* pool_hdl */
	&CMF_UUID,	/* uuid */
	&CMF_UUID,	/* hdl */
	&CMF_UINT64	/* capas */
};

struct crt_msg_field *cont_tgt_open_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *cont_tgt_close_in_fields[] = {
	&DMF_CLOSE_RECS	/* recs */
};

struct crt_msg_field *cont_tgt_close_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *cont_tgt_query_in_fields[] = {
	&CMF_UUID,	/* pool_uuid */
	&CMF_UUID,	/* container uuid */
};

struct crt_msg_field *cont_tgt_query_out_fields[] = {
	&CMF_INT,	/* rc */
	&CMF_INT,	/* padding */
	&CMF_UINT64	/* min purged epoch */
};

struct crt_msg_field *cont_tgt_epoch_discard_in_fields[] = {
	&CMF_UUID,	/* hdl */
	&CMF_UINT64	/* epoch */
};

struct crt_msg_field *cont_tgt_epoch_discard_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *cont_tgt_epoch_aggregate_in_fields[] = {
	&CMF_UUID,	/* container UUID */
	&CMF_UUID,	/* pool UUID */
	&DMF_EPR_ARRAY	/* EPR list */
};

struct crt_msg_field *cont_tgt_epoch_aggregate_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_req_format DQF_CONT_CREATE =
	DEFINE_CRT_REQ_FMT(cont_create_in_fields, cont_create_out_fields);

struct crt_req_format DQF_CONT_DESTROY =
	DEFINE_CRT_REQ_FMT(cont_destroy_in_fields, cont_destroy_out_fields);

struct crt_req_format DQF_CONT_OPEN =
	DEFINE_CRT_REQ_FMT(cont_open_in_fields, cont_open_out_fields);

struct crt_req_format DQF_CONT_CLOSE =
	DEFINE_CRT_REQ_FMT(cont_close_in_fields, cont_close_out_fields);

struct crt_req_format DQF_CONT_QUERY =
	DEFINE_CRT_REQ_FMT(cont_query_in_fields, cont_query_out_fields);

struct crt_req_format DQF_CONT_OID_ALLOC =
	DEFINE_CRT_REQ_FMT(cont_oid_alloc_in_fields, cont_oid_alloc_out_fields);

struct crt_req_format DQF_CONT_ATTR_LIST =
	DEFINE_CRT_REQ_FMT(cont_attr_list_in_fields, cont_attr_list_out_fields);

struct crt_req_format DQF_CONT_ATTR_GET =
	DEFINE_CRT_REQ_FMT(cont_attr_get_in_fields, cont_attr_get_out_fields);

struct crt_req_format DQF_CONT_ATTR_SET =
	DEFINE_CRT_REQ_FMT(cont_attr_set_in_fields, cont_attr_set_out_fields);

struct crt_req_format DQF_CONT_EPOCH_OP =
	DEFINE_CRT_REQ_FMT(cont_epoch_op_in_fields, cont_epoch_op_out_fields);

struct crt_req_format DQF_CONT_SNAP_LIST_OP =
	DEFINE_CRT_REQ_FMT(cont_snap_list_in_fields, cont_snap_list_out_fields);

struct crt_req_format DQF_CONT_SNAP_CREATE_OP =
	DEFINE_CRT_REQ_FMT(cont_epoch_op_in_fields, cont_op_out_fields);

struct crt_req_format DQF_CONT_SNAP_DESTROY_OP =
	DEFINE_CRT_REQ_FMT(cont_epoch_op_in_fields, cont_op_out_fields);

struct crt_req_format DQF_CONT_TGT_DESTROY =
	DEFINE_CRT_REQ_FMT(cont_tgt_destroy_in_fields,
			   cont_tgt_destroy_out_fields);

struct crt_req_format DQF_CONT_TGT_OPEN =
	DEFINE_CRT_REQ_FMT(cont_tgt_open_in_fields, cont_tgt_open_out_fields);

struct crt_req_format DQF_CONT_TGT_CLOSE =
	DEFINE_CRT_REQ_FMT(cont_tgt_close_in_fields, cont_tgt_close_out_fields);

struct crt_req_format DQF_CONT_TGT_QUERY =
	DEFINE_CRT_REQ_FMT(cont_tgt_query_in_fields, cont_tgt_query_out_fields);

struct crt_req_format DQF_CONT_TGT_EPOCH_DISCARD =
	DEFINE_CRT_REQ_FMT(cont_tgt_epoch_discard_in_fields,
			   cont_tgt_epoch_discard_out_fields);

struct crt_req_format DQF_CONT_TGT_EPOCH_AGGREGATE =
	DEFINE_CRT_REQ_FMT(cont_tgt_epoch_aggregate_in_fields,
			   cont_tgt_epoch_aggregate_out_fields);


int
cont_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_CONT_MODULE, 1);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

struct daos_rpc cont_rpcs[] = {
	{
		.dr_name	= "CONT_CREATE",
		.dr_opc		= CONT_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_CREATE
	}, {
		.dr_name	= "CONT_DESTROY",
		.dr_opc		= CONT_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_DESTROY
	}, {
		.dr_name	= "CONT_OPEN",
		.dr_opc		= CONT_OPEN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_OPEN
	}, {
		.dr_name	= "CONT_CLOSE",
		.dr_opc		= CONT_CLOSE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_CLOSE
	}, {
		.dr_name	= "CONT_QUERY",
		.dr_opc		= CONT_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_QUERY
	}, {
		.dr_name	= "CONT_OID_ALLOC",
		.dr_opc		= CONT_OID_ALLOC,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_OID_ALLOC
	}, {
		.dr_name	= "CONT_ATTR_LIST",
		.dr_opc		= CONT_ATTR_LIST,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_ATTR_LIST
	}, {
		.dr_name	= "CONT_ATTR_GET",
		.dr_opc		= CONT_ATTR_GET,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_ATTR_GET
	}, {
		.dr_name	= "CONT_ATTR_SET",
		.dr_opc		= CONT_ATTR_SET,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_ATTR_SET
	}, {
		.dr_name	= "CONT_EPOCH_QUERY",
		.dr_opc		= CONT_EPOCH_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_EPOCH_OP
	}, {
		.dr_name	= "CONT_EPOCH_HOLD",
		.dr_opc		= CONT_EPOCH_HOLD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_EPOCH_OP
	}, {
		.dr_name	= "CONT_EPOCH_SLIP",
		.dr_opc		= CONT_EPOCH_SLIP,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_EPOCH_OP
	}, {
		.dr_name	= "CONT_EPOCH_DISCARD",
		.dr_opc		= CONT_EPOCH_DISCARD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_EPOCH_OP
	}, {
		.dr_name	= "CONT_EPOCH_COMMIT",
		.dr_opc		= CONT_EPOCH_COMMIT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_EPOCH_OP
	}, {
		.dr_name	= "CONT_SNAP_LIST",
		.dr_opc		= CONT_SNAP_LIST,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_SNAP_LIST_OP
	}, {
		.dr_name	= "CONT_SNAP_CREATE",
		.dr_opc		= CONT_SNAP_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_SNAP_CREATE_OP
	}, {
		.dr_name	= "CONT_SNAP_DESTROY",
		.dr_opc		= CONT_SNAP_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_SNAP_DESTROY_OP
	}, {
		.dr_opc		= 0
	}
};

struct daos_rpc cont_srv_rpcs[] = {
	{
		.dr_name	= "CONT_TGT_DESTROY",
		.dr_opc		= CONT_TGT_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_DESTROY
	}, {
		.dr_name	= "CONT_TGT_OPEN",
		.dr_opc		= CONT_TGT_OPEN,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_OPEN
	}, {
		.dr_name	= "CONT_TGT_CLOSE",
		.dr_opc		= CONT_TGT_CLOSE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_CLOSE
	}, {
		.dr_name	= "CONT_TGT_QUERY",
		.dr_opc		= CONT_TGT_QUERY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_QUERY
	}, {
		.dr_name	= "CONT_TGT_EPOCH_DISCARD",
		.dr_opc		= CONT_TGT_EPOCH_DISCARD,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_EPOCH_DISCARD
	}, {
		.dr_name	= "CONT_TGT_EPOCH_AGGREGATE",
		.dr_opc		= CONT_TGT_EPOCH_AGGREGATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_CONT_TGT_EPOCH_AGGREGATE
	}, {
		.dr_opc		= 0
	}
};
