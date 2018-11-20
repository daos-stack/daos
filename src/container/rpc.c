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

/* Define for cont_rpcs[] array population below.
 * See CONT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format cont_proto_rpc_fmt[] = {
	CONT_PROTO_CLI_RPC_LIST,
	CONT_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format cont_proto_fmt = {
	.cpf_name  = "cont-proto",
	.cpf_ver   = DAOS_CONT_VERSION,
	.cpf_count = ARRAY_SIZE(cont_proto_rpc_fmt),
	.cpf_prf   = cont_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_CONT_MODULE, 0)
};
