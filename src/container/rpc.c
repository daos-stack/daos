/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont, ds_cont: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos/rpc.h>
#include "rpc.h"

static int
crt_proc_struct_rsvc_hint(crt_proc_t proc, crt_proc_op_t proc_op,
			  struct rsvc_hint *hint)
{
	int rc;

	rc = crt_proc_uint32_t(proc, proc_op, &hint->sh_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, proc_op, &hint->sh_rank);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, proc_op, &hint->sh_term);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, crt_proc_op_t proc_op,
			    daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_obj_id_t(crt_proc_t proc, crt_proc_op_t proc_op,
		       daos_obj_id_t *oid)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &oid->lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, proc_op, &oid->hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

CRT_RPC_DEFINE(cont_op, DAOS_ISEQ_CONT_OP, DAOS_OSEQ_CONT_OP)
CRT_RPC_DEFINE(cont_op_v8, DAOS_ISEQ_CONT_OP_V8, DAOS_OSEQ_CONT_OP)
CRT_RPC_DEFINE(cont_op_v9, DAOS_ISEQ_CONT_OP_V9, DAOS_OSEQ_CONT_OP)

static int
crt_proc_struct_cont_op_in(crt_proc_t proc, crt_proc_op_t proc_op,
			   struct cont_op_in *data)
{
	return crt_proc_cont_op_in(proc, data);
}

static int
crt_proc_struct_cont_op_out(crt_proc_t proc, crt_proc_op_t proc_op,
			    struct cont_op_out *data)
{
	return crt_proc_cont_op_out(proc, data);
}

static int
crt_proc_struct_cont_op_v8_in(crt_proc_t proc, crt_proc_op_t proc_op, struct cont_op_v8_in *data)
{
	return crt_proc_cont_op_v8_in(proc, data);
}

static int
crt_proc_struct_cont_op_v9_in(crt_proc_t proc, crt_proc_op_t proc_op, struct cont_op_v9_in *data)
{
	return crt_proc_cont_op_v9_in(proc, data);
}

CRT_RPC_DEFINE(cont_create_v8, DAOS_ISEQ_CONT_CREATE_V8, DAOS_OSEQ_CONT_CREATE)
CRT_RPC_DEFINE(cont_create_v9, DAOS_ISEQ_CONT_CREATE_V9, DAOS_OSEQ_CONT_CREATE)
CRT_RPC_DEFINE(cont_destroy_v8, DAOS_ISEQ_CONT_DESTROY_V8, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DEFINE(cont_destroy_v9, DAOS_ISEQ_CONT_DESTROY_V9, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DEFINE(cont_destroy_bylabel_v8, DAOS_ISEQ_CONT_DESTROY_BYLABEL_V8, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DEFINE(cont_destroy_bylabel_v9, DAOS_ISEQ_CONT_DESTROY_BYLABEL_V9, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DEFINE(cont_open_v8, DAOS_ISEQ_CONT_OPEN_V8, DAOS_OSEQ_CONT_OPEN)
CRT_RPC_DEFINE(cont_open_v9, DAOS_ISEQ_CONT_OPEN_V9, DAOS_OSEQ_CONT_OPEN)
CRT_RPC_DEFINE(cont_open_bylabel_v8, DAOS_ISEQ_CONT_OPEN_BYLABEL_V8, DAOS_OSEQ_CONT_OPEN_BYLABEL)
CRT_RPC_DEFINE(cont_open_bylabel_v9, DAOS_ISEQ_CONT_OPEN_BYLABEL_V9, DAOS_OSEQ_CONT_OPEN_BYLABEL)
CRT_RPC_DEFINE(cont_close_v8, DAOS_ISEQ_CONT_CLOSE_V8, DAOS_OSEQ_CONT_CLOSE)
CRT_RPC_DEFINE(cont_close_v9, DAOS_ISEQ_CONT_CLOSE_V9, DAOS_OSEQ_CONT_CLOSE)
CRT_RPC_DEFINE(cont_query_v8, DAOS_ISEQ_CONT_QUERY_V8, DAOS_OSEQ_CONT_QUERY)
CRT_RPC_DEFINE(cont_query_v9, DAOS_ISEQ_CONT_QUERY_V9, DAOS_OSEQ_CONT_QUERY)
CRT_RPC_DEFINE(cont_oid_alloc, DAOS_ISEQ_CONT_OID_ALLOC, DAOS_OSEQ_CONT_OID_ALLOC)
CRT_RPC_DEFINE(cont_oid_alloc_v9, DAOS_ISEQ_CONT_OID_ALLOC_V9, DAOS_OSEQ_CONT_OID_ALLOC)
CRT_RPC_DEFINE(cont_attr_list_v8, DAOS_ISEQ_CONT_ATTR_LIST_V8, DAOS_OSEQ_CONT_ATTR_LIST)
CRT_RPC_DEFINE(cont_attr_list_v9, DAOS_ISEQ_CONT_ATTR_LIST_V9, DAOS_OSEQ_CONT_ATTR_LIST)
CRT_RPC_DEFINE(cont_attr_get_v8, DAOS_ISEQ_CONT_ATTR_GET_V8, DAOS_OSEQ_CONT_ATTR_GET)
CRT_RPC_DEFINE(cont_attr_get_v9, DAOS_ISEQ_CONT_ATTR_GET_V9, DAOS_OSEQ_CONT_ATTR_GET)
CRT_RPC_DEFINE(cont_attr_set_v8, DAOS_ISEQ_CONT_ATTR_SET_V8, DAOS_OSEQ_CONT_ATTR_SET)
CRT_RPC_DEFINE(cont_attr_set_v9, DAOS_ISEQ_CONT_ATTR_SET_V9, DAOS_OSEQ_CONT_ATTR_SET)
CRT_RPC_DEFINE(cont_attr_del_v8, DAOS_ISEQ_CONT_ATTR_DEL_V8, DAOS_OSEQ_CONT_ATTR_DEL)
CRT_RPC_DEFINE(cont_attr_del_v9, DAOS_ISEQ_CONT_ATTR_DEL_V9, DAOS_OSEQ_CONT_ATTR_DEL)
CRT_RPC_DEFINE(cont_epoch_op_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_epoch_op_v9, DAOS_ISEQ_CONT_EPOCH_OP_V9, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_list_v8, DAOS_ISEQ_CONT_SNAP_LIST_V8, DAOS_OSEQ_CONT_SNAP_LIST)
CRT_RPC_DEFINE(cont_snap_list_v9, DAOS_ISEQ_CONT_SNAP_LIST_V9, DAOS_OSEQ_CONT_SNAP_LIST)
CRT_RPC_DEFINE(cont_snap_create_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_create_v9, DAOS_ISEQ_CONT_EPOCH_OP_V9, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_destroy_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_destroy_v9, DAOS_ISEQ_CONT_EPOCH_OP_V9, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_oit_oid_get_v8, DAOS_ISEQ_CONT_SNAP_OIT_OID_GET_V8,
	       DAOS_OSEQ_CONT_SNAP_OIT_OID_GET)
CRT_RPC_DEFINE(cont_snap_oit_oid_get_v9, DAOS_ISEQ_CONT_SNAP_OIT_OID_GET_V9,
	       DAOS_OSEQ_CONT_SNAP_OIT_OID_GET)
CRT_RPC_DEFINE(cont_snap_oit_destroy_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_snap_oit_destroy_v9, DAOS_ISEQ_CONT_EPOCH_OP_V9, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DEFINE(cont_tgt_destroy, DAOS_ISEQ_TGT_DESTROY, DAOS_OSEQ_TGT_DESTROY)
CRT_RPC_DEFINE(cont_tgt_query, DAOS_ISEQ_TGT_QUERY, DAOS_OSEQ_TGT_QUERY)
CRT_RPC_DEFINE(cont_tgt_epoch_aggregate, DAOS_ISEQ_CONT_TGT_EPOCH_AGGREGATE,
		DAOS_OSEQ_CONT_TGT_EPOCH_AGGREGATE)
CRT_RPC_DEFINE(cont_tgt_snapshot_notify, DAOS_ISEQ_CONT_TGT_SNAPSHOT_NOTIFY,
	       DAOS_OSEQ_CONT_TGT_SNAPSHOT_NOTIFY)
CRT_RPC_DEFINE(cont_prop_set_v8, DAOS_ISEQ_CONT_PROP_SET_V8, DAOS_OSEQ_CONT_PROP_SET)
CRT_RPC_DEFINE(cont_prop_set_v9, DAOS_ISEQ_CONT_PROP_SET_V9, DAOS_OSEQ_CONT_PROP_SET)
CRT_RPC_DEFINE(cont_prop_set_bylabel, DAOS_ISEQ_CONT_PROP_SET_BYLABEL, DAOS_OSEQ_CONT_PROP_SET)
CRT_RPC_DEFINE(cont_acl_update_v8, DAOS_ISEQ_CONT_ACL_UPDATE_V8, DAOS_OSEQ_CONT_ACL_UPDATE)
CRT_RPC_DEFINE(cont_acl_update_v9, DAOS_ISEQ_CONT_ACL_UPDATE_V9, DAOS_OSEQ_CONT_ACL_UPDATE)
CRT_RPC_DEFINE(cont_acl_delete_v8, DAOS_ISEQ_CONT_ACL_DELETE_V8, DAOS_OSEQ_CONT_ACL_DELETE)
CRT_RPC_DEFINE(cont_acl_delete_v9, DAOS_ISEQ_CONT_ACL_DELETE_V9, DAOS_OSEQ_CONT_ACL_DELETE)

/* Define for cont_rpcs[] array population below.
 * See CONT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)                                                                           \
	{                                                                                          \
	    .prf_flags   = b,                                                                      \
	    .prf_req_fmt = c,                                                                      \
	    .prf_hdlr    = NULL,                                                                   \
	    .prf_co_ops  = NULL,                                                                   \
	},

static struct crt_proto_rpc_format cont_proto_rpc_fmt_v9[] = {CONT_PROTO_CLI_RPC_LIST(9)
								  CONT_PROTO_SRV_RPC_LIST};

static struct crt_proto_rpc_format cont_proto_rpc_fmt_v8[] = {CONT_PROTO_CLI_RPC_LIST(8)
								  CONT_PROTO_SRV_RPC_LIST};

#undef X

struct crt_proto_format cont_proto_fmt_v9 = {.cpf_name  = "cont",
					     .cpf_ver   = 9,
					     .cpf_count = ARRAY_SIZE(cont_proto_rpc_fmt_v9),
					     .cpf_prf   = cont_proto_rpc_fmt_v9,
					     .cpf_base  = DAOS_RPC_OPCODE(0, DAOS_CONT_MODULE, 0)};

struct crt_proto_format cont_proto_fmt_v8 = {.cpf_name  = "cont",
					     .cpf_ver   = 8,
					     .cpf_count = ARRAY_SIZE(cont_proto_rpc_fmt_v8),
					     .cpf_prf   = cont_proto_rpc_fmt_v8,
					     .cpf_base  = DAOS_RPC_OPCODE(0, DAOS_CONT_MODULE, 0)};
