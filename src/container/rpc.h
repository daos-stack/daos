/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont, ds_cont: RPC Protocol Definitions
 *
 * This is naturally shared by both dc_cont and ds_cont. The in and out data
 * structures must be absent of any compiler-generated paddings.
 */

#ifndef __CONTAINER_RPC_H__
#define __CONTAINER_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_CONT_VERSION              8
/* version in which metadata open/modify times, number of handles were added to open, query RPCs */
#define CONT_PROTO_VER_WITH_MDTIMES    7
#define CONT_PROTO_VER_WITH_NHANDLES   7
/* version in which cont_op_in includes a client operation key */
#define CONT_PROTO_VER_WITH_SVC_OP_KEY 8

/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define CONT_PROTO_CLI_RPC_LIST(ver, hdlr)                                                         \
	X(CONT_CREATE, 0, ver >= 8 ? &CQF_cont_create_v8 : &CQF_cont_create, hdlr, NULL)           \
	X(CONT_DESTROY, 0, ver >= 8 ? &CQF_cont_destroy_v8 : &CQF_cont_destroy, hdlr, NULL)        \
	X(CONT_OPEN, 0, ver >= 8 ? &CQF_cont_open_v8 : &CQF_cont_open, hdlr, NULL)                 \
	X(CONT_CLOSE, 0, ver >= 8 ? &CQF_cont_close_v8 : &CQF_cont_close, hdlr, NULL)              \
	X(CONT_QUERY, 0, ver >= 8 ? &CQF_cont_query_v8 : &CQF_cont_query, hdlr, NULL)              \
	X(CONT_OID_ALLOC, 0, &CQF_cont_oid_alloc, ds_cont_oid_alloc_handler, NULL)                 \
	X(CONT_ATTR_LIST, 0, ver >= 8 ? &CQF_cont_attr_list_v8 : &CQF_cont_attr_list, hdlr, NULL)  \
	X(CONT_ATTR_GET, 0, ver >= 8 ? &CQF_cont_attr_get_v8 : &CQF_cont_attr_get, hdlr, NULL)     \
	X(CONT_ATTR_SET, 0, ver >= 8 ? &CQF_cont_attr_set_v8 : &CQF_cont_attr_set, hdlr, NULL)     \
	X(CONT_ATTR_DEL, 0, ver >= 8 ? &CQF_cont_attr_del_v8 : &CQF_cont_attr_del, hdlr, NULL)     \
	X(CONT_EPOCH_AGGREGATE, 0, ver >= 8 ? &CQF_cont_epoch_op_v8 : &CQF_cont_epoch_op, hdlr,    \
	  NULL)                                                                                    \
	X(CONT_SNAP_LIST, 0, ver >= 8 ? &CQF_cont_snap_list_v8 : &CQF_cont_snap_list, hdlr, NULL)  \
	X(CONT_SNAP_CREATE, 0, ver >= 8 ? &CQF_cont_epoch_op_v8 : &CQF_cont_epoch_op, hdlr, NULL)  \
	X(CONT_SNAP_DESTROY, 0, ver >= 8 ? &CQF_cont_epoch_op_v8 : &CQF_cont_epoch_op, hdlr, NULL) \
	X(CONT_PROP_SET, 0, ver >= 8 ? &CQF_cont_prop_set_v8 : &CQF_cont_prop_set, hdlr, NULL)     \
	X(CONT_ACL_UPDATE, 0, ver >= 8 ? &CQF_cont_acl_update_v8 : &CQF_cont_acl_update, hdlr,     \
	  NULL)                                                                                    \
	X(CONT_ACL_DELETE, 0, ver >= 8 ? &CQF_cont_acl_delete_v8 : &CQF_cont_acl_delete, hdlr,     \
	  NULL)                                                                                    \
	X(CONT_OPEN_BYLABEL, 0, ver >= 8 ? &CQF_cont_open_bylabel_v8 : &CQF_cont_open_bylabel,     \
	  hdlr, NULL)                                                                              \
	X(CONT_DESTROY_BYLABEL, 0,                                                                 \
	  ver >= 8 ? &CQF_cont_destroy_bylabel_v8 : &CQF_cont_destroy_bylabel, hdlr, NULL)         \
	X(CONT_SNAP_OIT_OID_GET, 0,                                                                \
	  ver >= 8 ? &CQF_cont_snap_oit_oid_get_v8 : &CQF_cont_snap_oit_oid_get, hdlr, NULL)       \
	X(CONT_SNAP_OIT_CREATE, 0, ver >= 8 ? &CQF_cont_epoch_op_v8 : &CQF_cont_epoch_op, hdlr,    \
	  NULL)                                                                                    \
	X(CONT_SNAP_OIT_DESTROY, 0, ver >= 8 ? &CQF_cont_epoch_op_v8 : &CQF_cont_epoch_op, hdlr,   \
	  NULL)

#define CONT_PROTO_SRV_RPC_LIST                                                                    \
	X(CONT_TGT_DESTROY, 0, &CQF_cont_tgt_destroy, ds_cont_tgt_destroy_handler,                 \
	  &ds_cont_tgt_destroy_co_ops)                                                             \
	X(CONT_TGT_QUERY, 0, &CQF_cont_tgt_query, ds_cont_tgt_query_handler,                       \
	  &ds_cont_tgt_query_co_ops)                                                               \
	X(CONT_TGT_EPOCH_AGGREGATE, 0, &CQF_cont_tgt_epoch_aggregate,                              \
	  ds_cont_tgt_epoch_aggregate_handler, &ds_cont_tgt_epoch_aggregate_co_ops)                \
	X(CONT_TGT_SNAPSHOT_NOTIFY, 0, &CQF_cont_tgt_snapshot_notify,                              \
	  ds_cont_tgt_snapshot_notify_handler, &ds_cont_tgt_snapshot_notify_co_ops)                \
	X(CONT_PROP_SET_BYLABEL, 0, &CQF_cont_prop_set_bylabel, ds_cont_set_prop_srv_handler, NULL)

/* Define for RPC enum population below */
#define X(a, ...) a,

enum cont_operation {
	CONT_PROTO_CLI_RPC_LIST(DAOS_CONT_VERSION, ds_cont_op_handler_v8) CONT_PROTO_CLI_COUNT,
	CONT_PROTO_CLI_LAST = CONT_PROTO_CLI_COUNT - 1,
	CONT_PROTO_SRV_RPC_LIST
};

#undef X

extern struct crt_proto_format cont_proto_fmt_v8;
extern struct crt_proto_format cont_proto_fmt_v7;
extern int dc_cont_proto_version;

/* clang-format off */

#define DAOS_ISEQ_CONT_OP	/* input fields */		 \
				/* pool handle UUID */		 \
	((uuid_t)		(ci_pool_hdl)		CRT_VAR) \
				/* container UUID */		 \
	((uuid_t)		(ci_uuid)		CRT_VAR) \
				/* container handle UUID */	 \
	((uuid_t)		(ci_hdl)		CRT_VAR)

#define DAOS_ISEQ_CONT_OP_V8	/* input fields */		 \
				/* pool handle UUID */		 \
	((uuid_t)		(ci_pool_hdl)		CRT_VAR) \
				/* container UUID */		 \
	((uuid_t)		(ci_uuid)		CRT_VAR) \
				/* container handle UUID */	 \
	((uuid_t)		(ci_hdl)		CRT_VAR) \
	((uuid_t)		(ci_cli_id)		CRT_VAR) \
	((uint64_t)		(ci_time)		CRT_VAR)

#define DAOS_OSEQ_CONT_OP	/* output fields */		 \
				/* operation return code */	 \
	((int32_t)		(co_rc)			CRT_VAR) \
				/* latest map version or zero */ \
	((uint32_t)		(co_map_version)	CRT_VAR) \
				/* leadership info */		 \
	((struct rsvc_hint)	(co_hint)		CRT_VAR)

CRT_RPC_DECLARE(cont_op, DAOS_ISEQ_CONT_OP, DAOS_OSEQ_CONT_OP)
CRT_RPC_DECLARE(cont_op_v8, DAOS_ISEQ_CONT_OP_V8, DAOS_OSEQ_CONT_OP)

#define DAOS_ISEQ_CONT_CREATE		/* input fields */		 \
					/* .ci_hdl unused */		 \
	((struct cont_op_in)		(cci_op)		CRT_VAR) \
	((daos_prop_t)			(cci_prop)		CRT_PTR)

#define DAOS_ISEQ_CONT_CREATE_V8	/* input fields */		 \
					/* .ci_hdl unused */		 \
	((struct cont_op_v8_in)		(cci_op)		CRT_VAR) \
	((daos_prop_t)			(cci_prop)		CRT_PTR)

#define DAOS_OSEQ_CONT_CREATE		/* output fields */		 \
	((struct cont_op_out)		(cco_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_create, DAOS_ISEQ_CONT_CREATE, DAOS_OSEQ_CONT_CREATE)
CRT_RPC_DECLARE(cont_create_v8, DAOS_ISEQ_CONT_CREATE_V8, DAOS_OSEQ_CONT_CREATE)

/* clang-format on */

static inline void
cont_create_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			daos_prop_t **cci_propp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*cci_propp = ((struct cont_create_v8_in *)in)->cci_prop;
	else
		*cci_propp = ((struct cont_create_in *)in)->cci_prop;
}

static inline void
cont_create_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, daos_prop_t *cci_prop)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_create_v8_in *)in)->cci_prop = cci_prop;
	else
		((struct cont_create_in *)in)->cci_prop = cci_prop;
}

/* clang-format off */
#define DAOS_ISEQ_CONT_DESTROY		/* input fields */		 \
					/* .ci_hdl unused */		 \
	((struct cont_op_in)		(cdi_op)		CRT_VAR) \
					/* evict all handles */		 \
	((uint32_t)			(cdi_force)		CRT_VAR)

#define DAOS_ISEQ_CONT_DESTROY_V8	/* input fields */		 \
					/* .ci_hdl unused */		 \
	((struct cont_op_v8_in)		(cdi_op)		CRT_VAR) \
					/* evict all handles */		 \
	((uint32_t)			(cdi_force)		CRT_VAR)

#define DAOS_OSEQ_CONT_DESTROY		/* output fields */		 \
	((struct cont_op_out)		(cdo_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_destroy, DAOS_ISEQ_CONT_DESTROY, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DECLARE(cont_destroy_v8, DAOS_ISEQ_CONT_DESTROY_V8, DAOS_OSEQ_CONT_DESTROY)

/* Container destroy bylabel input
 * Must begin with what DAOS_ISEQ_CONT_DESTROY has, for reusing cont_destroy_in
 * in the common code. cdi_op.ci_uuid is ignored.
 */
#define DAOS_ISEQ_CONT_DESTROY_BYLABEL		/* input fields */	 \
	DAOS_ISEQ_CONT_DESTROY						 \
	((uint32_t)				(cdli_pad32)	CRT_VAR) \
	((d_const_string_t)			(cdli_label)	CRT_VAR)

#define DAOS_ISEQ_CONT_DESTROY_BYLABEL_V8	/* input fields */	 \
	DAOS_ISEQ_CONT_DESTROY_V8					 \
	((uint32_t)				(cdli_pad32)	CRT_VAR) \
	((d_const_string_t)			(cdli_label)	CRT_VAR)

/* Container destroy bylabel output same as destroy by uuid. */
CRT_RPC_DECLARE(cont_destroy_bylabel, DAOS_ISEQ_CONT_DESTROY_BYLABEL, DAOS_OSEQ_CONT_DESTROY)
CRT_RPC_DECLARE(cont_destroy_bylabel_v8, DAOS_ISEQ_CONT_DESTROY_BYLABEL_V8, DAOS_OSEQ_CONT_DESTROY)

/* clang-format on */

static inline void
cont_destroy_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint32_t *cdi_forcep,
			 const char **labelp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		if (opc == CONT_DESTROY_BYLABEL) {
			*cdi_forcep = ((struct cont_destroy_bylabel_v8_in *)in)->cdi_force;
			if (labelp)
				*labelp = ((struct cont_destroy_bylabel_v8_in *)in)->cdli_label;
		} else { /* CONT_DESTROY */
			*cdi_forcep = ((struct cont_destroy_v8_in *)in)->cdi_force;
			/* labelp should be NULL */
		}
	} else {
		if (opc == CONT_DESTROY_BYLABEL) {
			*cdi_forcep = ((struct cont_destroy_bylabel_in *)in)->cdi_force;
			if (labelp)
				*labelp = ((struct cont_destroy_bylabel_in *)in)->cdli_label;
		} else { /* CONT_DESTROY*/
			*cdi_forcep = ((struct cont_destroy_in *)in)->cdi_force;
			/* labelp should be NULL */
		}
	}
}

static inline void
cont_destroy_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t cdi_force,
			 const char *label)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		if (opc == CONT_DESTROY_BYLABEL) {
			((struct cont_destroy_bylabel_v8_in *)in)->cdi_force  = cdi_force;
			((struct cont_destroy_bylabel_v8_in *)in)->cdli_label = label;
		} else { /* CONT_DESTROY */
			((struct cont_destroy_v8_in *)in)->cdi_force = cdi_force;
		}
	} else {
		if (opc == CONT_DESTROY_BYLABEL) {
			((struct cont_destroy_bylabel_in *)in)->cdi_force  = cdi_force;
			((struct cont_destroy_bylabel_in *)in)->cdli_label = label;
		} else { /* CONT_DESTROY */
			((struct cont_destroy_in *)in)->cdi_force = cdi_force;
		}
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_OPEN	/* input fields */		 \
	((struct cont_op_in)	(coi_op)		CRT_VAR) \
	((uint64_t)		(coi_flags)		CRT_VAR) \
	((uint64_t)		(coi_sec_capas)		CRT_VAR) \
	((uint64_t)		(coi_prop_bits)		CRT_VAR)

#define DAOS_ISEQ_CONT_OPEN_V8	/* input fields */		 \
	((struct cont_op_v8_in)	(coi_op)		CRT_VAR) \
	((uint64_t)		(coi_flags)		CRT_VAR) \
	((uint64_t)		(coi_prop_bits)		CRT_VAR)

#define DAOS_OSEQ_CONT_OPEN	/* common fields */		 \
	((struct cont_op_out)	(coo_op)		CRT_VAR) \
	((daos_prop_t)		(coo_prop)		CRT_PTR) \
	((daos_epoch_t)		(coo_lsnapshot)		CRT_VAR) \
	((uint32_t)		(coo_snap_count)	CRT_VAR) \
	((uint32_t)		(coo_nhandles)		CRT_VAR) \
	((uint64_t)		(coo_md_otime)		CRT_VAR) \
	((uint64_t)		(coo_md_mtime)		CRT_VAR)

CRT_RPC_DECLARE(cont_open_v8, DAOS_ISEQ_CONT_OPEN_V8, DAOS_OSEQ_CONT_OPEN)
CRT_RPC_DECLARE(cont_open, DAOS_ISEQ_CONT_OPEN, DAOS_OSEQ_CONT_OPEN)

/* Container open bylabel input
 * Must begin with what DAOS_ISEQ_CONT_OPEN has, for reusing cont_open_in
 * in the common code. coi_op.ci_uuid is ignored.
 */
#define DAOS_ISEQ_CONT_OPEN_BYLABEL	/* input fields */		 \
	DAOS_ISEQ_CONT_OPEN				 		 \
	((d_const_string_t)		(coli_label)		CRT_VAR)

#define DAOS_ISEQ_CONT_OPEN_BYLABEL_V8	/* input fields */		 \
	DAOS_ISEQ_CONT_OPEN_V8						 \
	((d_const_string_t)		(coli_label)		CRT_VAR)

/* Container open bylabel output */
#define DAOS_OSEQ_CONT_OPEN_BYLABEL	/* output fields */		 \
	((struct cont_op_out)		(coo_op)		CRT_VAR) \
	((daos_prop_t)			(coo_prop)		CRT_PTR) \
	((daos_epoch_t)			(coo_lsnapshot)		CRT_VAR) \
	((uint32_t)			(coo_snap_count)	CRT_VAR) \
	((uint32_t)			(coo_nhandles)		CRT_VAR) \
	((uuid_t)			(colo_uuid)		CRT_VAR) \
	((uint64_t)			(coo_md_otime)		CRT_VAR) \
	((uint64_t)			(coo_md_mtime)		CRT_VAR)

CRT_RPC_DECLARE(cont_open_bylabel, DAOS_ISEQ_CONT_OPEN_BYLABEL, DAOS_OSEQ_CONT_OPEN_BYLABEL)
CRT_RPC_DECLARE(cont_open_bylabel_v8, DAOS_ISEQ_CONT_OPEN_BYLABEL_V8, DAOS_OSEQ_CONT_OPEN_BYLABEL)

/* clang-format on */

static inline void
cont_open_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t *coi_flagsp,
		      uint64_t *coi_prop_bitsp, const char **labelp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		if (opc == CONT_OPEN_BYLABEL) {
			*coi_flagsp     = ((struct cont_open_bylabel_v8_in *)in)->coi_flags;
			*coi_prop_bitsp = ((struct cont_open_bylabel_v8_in *)in)->coi_prop_bits;
			if (labelp)
				*labelp = ((struct cont_open_bylabel_v8_in *)in)->coli_label;
		} else { /* CONT_OPEN */
			*coi_flagsp     = ((struct cont_open_v8_in *)in)->coi_flags;
			*coi_prop_bitsp = ((struct cont_open_v8_in *)in)->coi_prop_bits;
			/* labelp should be NULL */
		}
	} else {
		if (opc == CONT_OPEN_BYLABEL) {
			*coi_flagsp     = ((struct cont_open_bylabel_in *)in)->coi_flags;
			*coi_prop_bitsp = ((struct cont_open_bylabel_in *)in)->coi_prop_bits;
			if (labelp)
				*labelp = ((struct cont_open_bylabel_in *)in)->coli_label;
		} else { /* CONT_OPEN */
			*coi_flagsp     = ((struct cont_open_in *)in)->coi_flags;
			*coi_prop_bitsp = ((struct cont_open_in *)in)->coi_prop_bits;
			/* labelp should be NULL */
		}
	}
}

static inline void
cont_open_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t coi_flags,
		      uint64_t coi_prop_bits, const char *label)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		if (opc == CONT_OPEN_BYLABEL) {
			((struct cont_open_bylabel_v8_in *)in)->coi_flags     = coi_flags;
			((struct cont_open_bylabel_v8_in *)in)->coi_prop_bits = coi_prop_bits;
			((struct cont_open_bylabel_v8_in *)in)->coli_label    = label;
		} else { /* CONT_OPEN */
			((struct cont_open_v8_in *)in)->coi_flags     = coi_flags;
			((struct cont_open_v8_in *)in)->coi_prop_bits = coi_prop_bits;
		}
	} else {
		if (opc == CONT_OPEN_BYLABEL) {
			((struct cont_open_bylabel_in *)in)->coi_flags     = coi_flags;
			((struct cont_open_bylabel_in *)in)->coi_prop_bits = coi_prop_bits;
			((struct cont_open_bylabel_in *)in)->coli_label    = label;
		} else { /* CONT_OPEN */
			((struct cont_open_in *)in)->coi_flags     = coi_flags;
			((struct cont_open_in *)in)->coi_prop_bits = coi_prop_bits;
		}
	}
}

static inline void
cont_op_in_get_label(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, const char **clbl_out)
{
	void *in = crt_req_get(rpc);

	D_ASSERT((opc == CONT_OPEN_BYLABEL) || (opc == CONT_DESTROY_BYLABEL));
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		if (opc == CONT_OPEN_BYLABEL)
			*clbl_out = ((struct cont_open_bylabel_v8_in *)in)->coli_label;
		else
			*clbl_out = ((struct cont_destroy_bylabel_v8_in *)in)->cdli_label;
	} else {
		if (opc == CONT_OPEN_BYLABEL)
			*clbl_out = ((struct cont_open_bylabel_in *)in)->coli_label;
		else
			*clbl_out = ((struct cont_destroy_bylabel_in *)in)->cdli_label;
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_CLOSE	/* input fields */		 \
	((struct cont_op_in)	(cci_op)		CRT_VAR)

#define DAOS_ISEQ_CONT_CLOSE_V8	/* input fields */		 \
	((struct cont_op_v8_in)	(cci_op)		CRT_VAR)

#define DAOS_OSEQ_CONT_CLOSE	/* output fields */		 \
	((struct cont_op_out)	(cco_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_close, DAOS_ISEQ_CONT_CLOSE, DAOS_OSEQ_CONT_CLOSE)
CRT_RPC_DECLARE(cont_close_v8, DAOS_ISEQ_CONT_CLOSE_V8, DAOS_OSEQ_CONT_CLOSE)

/* clang-format on */

/** container query request bits */
#define DAOS_CO_QUERY_PROP_LABEL		(1ULL << 0)
#define DAOS_CO_QUERY_PROP_LAYOUT_TYPE		(1ULL << 1)
#define DAOS_CO_QUERY_PROP_LAYOUT_VER		(1ULL << 2)
#define DAOS_CO_QUERY_PROP_CSUM			(1ULL << 3)
#define DAOS_CO_QUERY_PROP_CSUM_CHUNK		(1ULL << 4)
#define DAOS_CO_QUERY_PROP_CSUM_SERVER		(1ULL << 5)
#define DAOS_CO_QUERY_PROP_REDUN_FAC		(1ULL << 6)
#define DAOS_CO_QUERY_PROP_REDUN_LVL		(1ULL << 7)
#define DAOS_CO_QUERY_PROP_SNAPSHOT_MAX		(1ULL << 8)
#define DAOS_CO_QUERY_PROP_COMPRESS		(1ULL << 9)
#define DAOS_CO_QUERY_PROP_ENCRYPT		(1ULL << 10)
#define DAOS_CO_QUERY_PROP_ACL			(1ULL << 11)
#define DAOS_CO_QUERY_PROP_OWNER		(1ULL << 12)
#define DAOS_CO_QUERY_PROP_OWNER_GROUP		(1ULL << 13)
#define DAOS_CO_QUERY_PROP_DEDUP		(1ULL << 14)
#define DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD	(1ULL << 15)
#define DAOS_CO_QUERY_PROP_ROOTS		(1ULL << 16)
#define DAOS_CO_QUERY_PROP_CO_STATUS		(1ULL << 17)
#define DAOS_CO_QUERY_PROP_ALLOCED_OID		(1ULL << 18)
#define DAOS_CO_QUERY_PROP_EC_CELL_SZ		(1ULL << 19)
#define DAOS_CO_QUERY_PROP_EC_PDA		(1ULL << 20)
#define DAOS_CO_QUERY_PROP_RP_PDA		(1ULL << 21)
#define DAOS_CO_QUERY_PROP_GLOBAL_VERSION	(1ULL << 22)
#define DAOS_CO_QUERY_PROP_SCRUB_DIS		(1ULL << 23)
#define DAOS_CO_QUERY_PROP_OBJ_VERSION		(1ULL << 24)
#define DAOS_CO_QUERY_PROP_PERF_DOMAIN		(1ULL << 25)

#define DAOS_CO_QUERY_PROP_BITS_NR		(26)
#define DAOS_CO_QUERY_PROP_ALL					\
	((1ULL << DAOS_CO_QUERY_PROP_BITS_NR) - 1)

/** container query target bit, to satisfy querying of daos_cont_info_t */
#define DAOS_CO_QUERY_TGT		(1ULL << 31)

/* clang-format off */

#define DAOS_ISEQ_CONT_QUERY	/* input fields */		 \
	((struct cont_op_in)	(cqi_op)		CRT_VAR) \
	((uint64_t)		(cqi_bits)		CRT_VAR)

#define DAOS_ISEQ_CONT_QUERY_V8	/* input fields */		 \
	((struct cont_op_v8_in)	(cqi_op)		CRT_VAR) \
	((uint64_t)		(cqi_bits)		CRT_VAR)

#define DAOS_OSEQ_CONT_QUERY	/* common fields */		 \
	((struct cont_op_out)	(cqo_op)		CRT_VAR) \
	((daos_prop_t)		(cqo_prop)		CRT_PTR) \
	((daos_epoch_t)		(cqo_lsnapshot)		CRT_VAR) \
	((uint32_t)		(cqo_snap_count)	CRT_VAR) \
	((uint32_t)		(cqo_nhandles)		CRT_VAR) \
	((uint64_t)		(cqo_md_otime)		CRT_VAR) \
	((uint64_t)		(cqo_md_mtime)		CRT_VAR)

CRT_RPC_DECLARE(cont_query, DAOS_ISEQ_CONT_QUERY, DAOS_OSEQ_CONT_QUERY)
CRT_RPC_DECLARE(cont_query_v8, DAOS_ISEQ_CONT_QUERY_V8, DAOS_OSEQ_CONT_QUERY)

/* clang-format on */

static inline void
cont_query_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t *cqi_bitsp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*cqi_bitsp = ((struct cont_query_v8_in *)in)->cqi_bits;
	else
		*cqi_bitsp = ((struct cont_query_in *)in)->cqi_bits;
}

static inline void
cont_query_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t cqi_bits)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_query_v8_in *)in)->cqi_bits = cqi_bits;
	else
		((struct cont_query_in *)in)->cqi_bits = cqi_bits;
}

/** Add more items to query when needed */

/* clang-format off */

#define DAOS_ISEQ_CONT_OID_ALLOC /* input fields */		 \
	((struct cont_op_in)	(coai_op)		CRT_VAR) \
	((daos_size_t)		(num_oids)		CRT_VAR)

#define DAOS_OSEQ_CONT_OID_ALLOC /* output fields */		 \
	((struct cont_op_out)	(coao_op)		CRT_VAR) \
	((uint64_t)		(oid)			CRT_VAR)

CRT_RPC_DECLARE(cont_oid_alloc, DAOS_ISEQ_CONT_OID_ALLOC, DAOS_OSEQ_CONT_OID_ALLOC)

#define DAOS_ISEQ_CONT_ATTR_LIST 	/* input fields */		 \
	((struct cont_op_in)		(cali_op)		CRT_VAR) \
	((crt_bulk_t)			(cali_bulk)		CRT_VAR)

#define DAOS_ISEQ_CONT_ATTR_LIST_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(cali_op)		CRT_VAR) \
	((crt_bulk_t)			(cali_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_LIST	/* output fields */		 \
	((struct cont_op_out)		(calo_op)		CRT_VAR) \
	((uint64_t)			(calo_size)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_list, DAOS_ISEQ_CONT_ATTR_LIST, DAOS_OSEQ_CONT_ATTR_LIST)
CRT_RPC_DECLARE(cont_attr_list_v8, DAOS_ISEQ_CONT_ATTR_LIST_V8, DAOS_OSEQ_CONT_ATTR_LIST)

/* clang-format on */

static inline void
cont_attr_list_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			   crt_bulk_t *cali_bulkp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*cali_bulkp = ((struct cont_attr_list_v8_in *)in)->cali_bulk;
	else
		*cali_bulkp = ((struct cont_attr_list_in *)in)->cali_bulk;
}

static inline void
cont_attr_list_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			   crt_bulk_t cali_bulk)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_attr_list_v8_in *)in)->cali_bulk = cali_bulk;
	else
		((struct cont_attr_list_in *)in)->cali_bulk = cali_bulk;
}

/* clang-format off */

#define DAOS_ISEQ_CONT_ATTR_GET		/* input fields */		 \
	((struct cont_op_in)		(cagi_op)		CRT_VAR) \
	((uint64_t)			(cagi_count)		CRT_VAR) \
	((uint64_t)			(cagi_key_length)	CRT_VAR) \
	((crt_bulk_t)			(cagi_bulk)		CRT_VAR)

#define DAOS_ISEQ_CONT_ATTR_GET_V8	/* input fields */                                              \
	((struct cont_op_v8_in)		(cagi_op)		CRT_VAR) \
	((uint64_t)			(cagi_count)		CRT_VAR) \
	((uint64_t)			(cagi_key_length)	CRT_VAR) \
	((crt_bulk_t)			(cagi_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_GET		/* output fields */		 \
	((struct cont_op_out)		(cago_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_get, DAOS_ISEQ_CONT_ATTR_GET, DAOS_OSEQ_CONT_ATTR_GET)
CRT_RPC_DECLARE(cont_attr_get_v8, DAOS_ISEQ_CONT_ATTR_GET_V8, DAOS_OSEQ_CONT_ATTR_GET)

/* clang-format on */

static inline void
cont_attr_get_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  uint64_t *cagi_countp, uint64_t *cagi_key_lengthp, crt_bulk_t *cagi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*cagi_countp      = ((struct cont_attr_get_v8_in *)in)->cagi_count;
		*cagi_key_lengthp = ((struct cont_attr_get_v8_in *)in)->cagi_key_length;
		*cagi_bulkp       = ((struct cont_attr_get_v8_in *)in)->cagi_bulk;
	} else {
		*cagi_countp      = ((struct cont_attr_get_in *)in)->cagi_count;
		*cagi_key_lengthp = ((struct cont_attr_get_in *)in)->cagi_key_length;
		*cagi_bulkp       = ((struct cont_attr_get_in *)in)->cagi_bulk;
	}
}

static inline void
cont_attr_get_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t cagi_count,
			  uint64_t cagi_key_length, crt_bulk_t cagi_bulk)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_attr_get_v8_in *)in)->cagi_count      = cagi_count;
		((struct cont_attr_get_v8_in *)in)->cagi_key_length = cagi_key_length;
		((struct cont_attr_get_v8_in *)in)->cagi_bulk       = cagi_bulk;
	} else {
		((struct cont_attr_get_in *)in)->cagi_count      = cagi_count;
		((struct cont_attr_get_in *)in)->cagi_key_length = cagi_key_length;
		((struct cont_attr_get_in *)in)->cagi_bulk       = cagi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_ATTR_SET		/* input fields */		 \
	((struct cont_op_in)		(casi_op)		CRT_VAR) \
	((uint64_t)			(casi_count)		CRT_VAR) \
	((crt_bulk_t)			(casi_bulk)		CRT_VAR)

#define DAOS_ISEQ_CONT_ATTR_SET_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(casi_op)		CRT_VAR) \
	((uint64_t)			(casi_count)		CRT_VAR) \
	((crt_bulk_t)			(casi_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_SET		/* output fields */		 \
	((struct cont_op_out)		(caso_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_set, DAOS_ISEQ_CONT_ATTR_SET, DAOS_OSEQ_CONT_ATTR_SET)
CRT_RPC_DECLARE(cont_attr_set_v8, DAOS_ISEQ_CONT_ATTR_SET_V8, DAOS_OSEQ_CONT_ATTR_SET)

/* clang-format on */

static inline void
cont_attr_set_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  uint64_t *casi_countp, crt_bulk_t *casi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*casi_countp = ((struct cont_attr_set_v8_in *)in)->casi_count;
		*casi_bulkp  = ((struct cont_attr_set_v8_in *)in)->casi_bulk;
	} else {
		*casi_countp = ((struct cont_attr_set_in *)in)->casi_count;
		*casi_bulkp  = ((struct cont_attr_set_in *)in)->casi_bulk;
	}
}

static inline void
cont_attr_set_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t casi_count,
			  crt_bulk_t casi_bulk)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_attr_set_v8_in *)in)->casi_count = casi_count;
		((struct cont_attr_set_v8_in *)in)->casi_bulk  = casi_bulk;
	} else {
		((struct cont_attr_set_in *)in)->casi_count = casi_count;
		((struct cont_attr_set_in *)in)->casi_bulk  = casi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_ATTR_DEL		/* input fields */		 \
	((struct cont_op_in)		(cadi_op)		CRT_VAR) \
	((uint64_t)			(cadi_count)		CRT_VAR) \
	((crt_bulk_t)			(cadi_bulk)		CRT_VAR)

#define DAOS_ISEQ_CONT_ATTR_DEL_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(cadi_op)		CRT_VAR) \
	((uint64_t)			(cadi_count)		CRT_VAR) \
	((crt_bulk_t)			(cadi_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_ATTR_DEL		/* output fields */		 \
	((struct cont_op_out)		(cado_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_attr_del, DAOS_ISEQ_CONT_ATTR_DEL, DAOS_OSEQ_CONT_ATTR_DEL)
CRT_RPC_DECLARE(cont_attr_del_v8, DAOS_ISEQ_CONT_ATTR_DEL_V8, DAOS_OSEQ_CONT_ATTR_DEL)

/* clang-format on */

static inline void
cont_attr_del_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  uint64_t *cadi_countp, crt_bulk_t *cadi_bulkp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*cadi_countp = ((struct cont_attr_del_v8_in *)in)->cadi_count;
		*cadi_bulkp  = ((struct cont_attr_del_v8_in *)in)->cadi_bulk;
	} else {
		*cadi_countp = ((struct cont_attr_del_in *)in)->cadi_count;
		*cadi_bulkp  = ((struct cont_attr_del_in *)in)->cadi_bulk;
	}
}

static inline void
cont_attr_del_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver, uint64_t cadi_count,
			  crt_bulk_t cadi_bulk)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_attr_del_v8_in *)in)->cadi_count = cadi_count;
		((struct cont_attr_del_v8_in *)in)->cadi_bulk  = cadi_bulk;
	} else {
		((struct cont_attr_del_in *)in)->cadi_count = cadi_count;
		((struct cont_attr_del_in *)in)->cadi_bulk  = cadi_bulk;
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_EPOCH_OP		/* input fields */		 \
	((struct cont_op_in)		(cei_op)		CRT_VAR) \
	((daos_epoch_t)			(cei_epoch)		CRT_VAR) \
	((uint64_t)			(cei_opts)		CRT_VAR)

#define DAOS_ISEQ_CONT_EPOCH_OP_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(cei_op)		CRT_VAR) \
	((daos_epoch_t)			(cei_epoch)		CRT_VAR) \
	((uint64_t)			(cei_opts)		CRT_VAR)

#define DAOS_OSEQ_CONT_EPOCH_OP		/* output fields */		 \
	((struct cont_op_out)		(ceo_op)		CRT_VAR) \
	((daos_epoch_t)			(ceo_epoch)		CRT_VAR)

CRT_RPC_DECLARE(cont_epoch_op, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_epoch_op_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)

/* clang-format on */

static inline void
cont_epoch_op_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  daos_epoch_t *cei_epochp, uint64_t *cei_optsp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*cei_epochp = ((struct cont_epoch_op_v8_in *)in)->cei_epoch;
		*cei_optsp  = ((struct cont_epoch_op_v8_in *)in)->cei_opts;
	} else {
		*cei_epochp = ((struct cont_epoch_op_in *)in)->cei_epoch;
		*cei_optsp  = ((struct cont_epoch_op_in *)in)->cei_opts;
	}
}

static inline void
cont_epoch_op_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  daos_epoch_t cei_epoch, uint64_t cei_opts)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_epoch_op_v8_in *)in)->cei_epoch = cei_epoch;
		((struct cont_epoch_op_v8_in *)in)->cei_opts  = cei_opts;
	} else {
		((struct cont_epoch_op_in *)in)->cei_epoch = cei_epoch;
		((struct cont_epoch_op_in *)in)->cei_opts  = cei_opts;
	}
}

/* clang-format off */

#define DAOS_ISEQ_CONT_SNAP_LIST	/* input fields */		 \
	((struct cont_op_in)		(sli_op)		CRT_VAR) \
	((crt_bulk_t)			(sli_bulk)		CRT_VAR)

#define DAOS_ISEQ_CONT_SNAP_LIST_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(sli_op)		CRT_VAR) \
	((crt_bulk_t)			(sli_bulk)		CRT_VAR)

#define DAOS_OSEQ_CONT_SNAP_LIST	/* output fields */		 \
	((struct cont_op_out)		(slo_op)		CRT_VAR) \
	((uint32_t)			(slo_count)		CRT_VAR)

CRT_RPC_DECLARE(cont_snap_list, DAOS_ISEQ_CONT_SNAP_LIST, DAOS_OSEQ_CONT_SNAP_LIST)
CRT_RPC_DECLARE(cont_snap_list_v8, DAOS_ISEQ_CONT_SNAP_LIST_V8, DAOS_OSEQ_CONT_SNAP_LIST)

/* clang-format on */

static inline void
cont_snap_list_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			   crt_bulk_t *sli_bulkp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*sli_bulkp = ((struct cont_snap_list_v8_in *)in)->sli_bulk;
	else
		*sli_bulkp = ((struct cont_snap_list_in *)in)->sli_bulk;
}

static inline void
cont_snap_list_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			   crt_bulk_t sli_bulk)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_snap_list_v8_in *)in)->sli_bulk = sli_bulk;
	else
		((struct cont_snap_list_in *)in)->sli_bulk = sli_bulk;
}

/* clang-format off */

CRT_RPC_DECLARE(cont_snap_create, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_snap_create_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)

CRT_RPC_DECLARE(cont_snap_destroy, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_snap_destroy_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)

CRT_RPC_DECLARE(cont_snap_oit_create, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_snap_oit_create_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)

CRT_RPC_DECLARE(cont_snap_oit_destroy, DAOS_ISEQ_CONT_EPOCH_OP, DAOS_OSEQ_CONT_EPOCH_OP)
CRT_RPC_DECLARE(cont_snap_oit_destroy_v8, DAOS_ISEQ_CONT_EPOCH_OP_V8, DAOS_OSEQ_CONT_EPOCH_OP)

#define DAOS_ISEQ_CONT_SNAP_OIT_OID_GET		/* input fields */		 \
	((struct cont_op_in)			(ogi_op)		CRT_VAR) \
	((daos_epoch_t)				(ogi_epoch)		CRT_VAR)

#define DAOS_ISEQ_CONT_SNAP_OIT_OID_GET_V8	/* input fields */		 \
	((struct cont_op_v8_in)			(ogi_op)		CRT_VAR) \
	((daos_epoch_t)				(ogi_epoch)		CRT_VAR)

#define DAOS_OSEQ_CONT_SNAP_OIT_OID_GET 	/* output fields */		 \
	((struct cont_op_out)			(ogo_op)		CRT_VAR) \
	((daos_obj_id_t)			(ogo_oid)		CRT_VAR)

CRT_RPC_DECLARE(cont_snap_oit_oid_get, DAOS_ISEQ_CONT_SNAP_OIT_OID_GET,
		DAOS_OSEQ_CONT_SNAP_OIT_OID_GET)
CRT_RPC_DECLARE(cont_snap_oit_oid_get_v8, DAOS_ISEQ_CONT_SNAP_OIT_OID_GET_V8,
		DAOS_OSEQ_CONT_SNAP_OIT_OID_GET)

/* clang-format on */

static inline void
cont_snap_oit_oid_get_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
				  daos_epoch_t *ogi_epochp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*ogi_epochp = ((struct cont_snap_oit_oid_get_v8_in *)in)->ogi_epoch;
	else
		*ogi_epochp = ((struct cont_snap_oit_oid_get_in *)in)->ogi_epoch;
}

static inline void
cont_snap_oit_oid_get_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
				  daos_epoch_t ogi_epoch)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_snap_oit_oid_get_v8_in *)in)->ogi_epoch = ogi_epoch;
	else
		((struct cont_snap_oit_oid_get_in *)in)->ogi_epoch = ogi_epoch;
}

/* clang-format off */

#define DAOS_ISEQ_TGT_DESTROY	/* input fields */		 \
	((uuid_t)		(tdi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(tdi_uuid)		CRT_VAR)

#define DAOS_OSEQ_TGT_DESTROY	/* output fields */		 \
				/* number of errors */		 \
	((int32_t)		(tdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_destroy, DAOS_ISEQ_TGT_DESTROY, DAOS_OSEQ_TGT_DESTROY)

/* clang-format on */

struct cont_tgt_close_rec {
	uuid_t		tcr_hdl;
	daos_epoch_t	tcr_hce;
};

/* TODO: more tgt query information ; and decide if tqo_hae is needed at all
 * (e.g., CONT_QUERY cqo_hae has been removed).
 */

/* clang-format off */
#define DAOS_ISEQ_TGT_QUERY	/* input fields */		 \
	((uuid_t)		(tqi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(tqi_cont_uuid)		CRT_VAR)

#define DAOS_OSEQ_TGT_QUERY	/* output fields */		 \
	((int32_t)		(tqo_rc)		CRT_VAR) \
	((int32_t)		(tqo_pad32)		CRT_VAR) \
	((daos_epoch_t)		(tqo_hae)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_query, DAOS_ISEQ_TGT_QUERY, DAOS_OSEQ_TGT_QUERY)

#define DAOS_ISEQ_CONT_TGT_EPOCH_AGGREGATE /* input fields */	 \
	((uuid_t)		(tai_cont_uuid)		CRT_VAR) \
	((uuid_t)		(tai_pool_uuid)		CRT_VAR) \
	((daos_epoch_range_t)	(tai_epr_list)		CRT_ARRAY)

#define DAOS_OSEQ_CONT_TGT_EPOCH_AGGREGATE /* output fields */	 \
				/* number of errors */		 \
	((int32_t)		(tao_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_epoch_aggregate, DAOS_ISEQ_CONT_TGT_EPOCH_AGGREGATE,
		DAOS_OSEQ_CONT_TGT_EPOCH_AGGREGATE)

#define DAOS_ISEQ_CONT_TGT_SNAPSHOT_NOTIFY /* input fields */	 \
	((uuid_t)		(tsi_cont_uuid)		CRT_VAR) \
	((uuid_t)		(tsi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(tsi_coh_uuid)		CRT_VAR) \
	((daos_epoch_t)		(tsi_epoch)		CRT_VAR) \
	((uint64_t)		(tsi_opts)		CRT_VAR) \
	((daos_obj_id_t)	(tsi_oit_oid)		CRT_VAR)

#define DAOS_OSEQ_CONT_TGT_SNAPSHOT_NOTIFY /* output fields */	 \
				/* number of errors */		 \
	((int32_t)		(tso_rc)		CRT_VAR)

CRT_RPC_DECLARE(cont_tgt_snapshot_notify, DAOS_ISEQ_CONT_TGT_SNAPSHOT_NOTIFY,
		DAOS_OSEQ_CONT_TGT_SNAPSHOT_NOTIFY)

#define DAOS_ISEQ_CONT_PROP_SET		/* input fields */		 \
	((struct cont_op_in)		(cpsi_op)		CRT_VAR) \
	((daos_prop_t)			(cpsi_prop)		CRT_PTR) \
	((uuid_t)			(cpsi_pool_uuid)	CRT_VAR)

#define DAOS_ISEQ_CONT_PROP_SET_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(cpsi_op)		CRT_VAR) \
	((daos_prop_t)			(cpsi_prop)		CRT_PTR) \
	((uuid_t)			(cpsi_pool_uuid)	CRT_VAR)

#define DAOS_OSEQ_CONT_PROP_SET		/* output fields */		 \
	((struct cont_op_out)		(cpso_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_prop_set, DAOS_ISEQ_CONT_PROP_SET, DAOS_OSEQ_CONT_PROP_SET)
CRT_RPC_DECLARE(cont_prop_set_v8, DAOS_ISEQ_CONT_PROP_SET_V8, DAOS_OSEQ_CONT_PROP_SET)

#define DAOS_ISEQ_CONT_PROP_SET_BYLABEL		/* input fields */	 \
	DAOS_ISEQ_CONT_PROP_SET_V8					 \
	((d_const_string_t)		(cpsi_label)		CRT_VAR)

CRT_RPC_DECLARE(cont_prop_set_bylabel, DAOS_ISEQ_CONT_PROP_SET_BYLABEL, DAOS_OSEQ_CONT_PROP_SET)

/* clang-format on */

static inline void
cont_prop_set_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  daos_prop_t **cpsi_propp, uuid_t *cpsi_pool_uuidp, uuid_t *cpsi_co_uuidp,
			  const char **cont_label)
{
	void *in = crt_req_get(rpc);

	if (opc == CONT_PROP_SET_BYLABEL) {
		*cpsi_propp = ((struct cont_prop_set_bylabel_in *)in)->cpsi_prop;
		if (cpsi_pool_uuidp)
			uuid_copy(*cpsi_pool_uuidp,
				  ((struct cont_prop_set_bylabel_in *)in)->cpsi_pool_uuid);
		if (cont_label != NULL)
			*cont_label = ((struct cont_prop_set_bylabel_in *)in)->cpsi_label;
	} else if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*cpsi_propp = ((struct cont_prop_set_v8_in *)in)->cpsi_prop;
		if (cpsi_pool_uuidp)
			uuid_copy(*cpsi_pool_uuidp,
				  ((struct cont_prop_set_v8_in *)in)->cpsi_pool_uuid);
		if (cpsi_co_uuidp)
			uuid_copy(*cpsi_co_uuidp,
				  ((struct cont_prop_set_v8_in *)in)->cpsi_op.ci_uuid);
	} else {
		*cpsi_propp = ((struct cont_prop_set_in *)in)->cpsi_prop;
		if (cpsi_pool_uuidp)
			uuid_copy(*cpsi_pool_uuidp,
				  ((struct cont_prop_set_in *)in)->cpsi_pool_uuid);
		if (cpsi_co_uuidp)
			uuid_copy(*cpsi_co_uuidp, ((struct cont_prop_set_in *)in)->cpsi_op.ci_uuid);
	}
}

static inline void
cont_prop_set_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			  daos_prop_t *cpsi_prop, uuid_t cpsi_pool_uuid)
{
	void *in = crt_req_get(rpc);

	if (opc == CONT_PROP_SET_BYLABEL) {
		((struct cont_prop_set_bylabel_in *)in)->cpsi_prop = cpsi_prop;
		uuid_copy(((struct cont_prop_set_bylabel_in *)in)->cpsi_pool_uuid, cpsi_pool_uuid);
	} else if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_prop_set_v8_in *)in)->cpsi_prop = cpsi_prop;
		uuid_copy(((struct cont_prop_set_v8_in *)in)->cpsi_pool_uuid, cpsi_pool_uuid);
	} else {
		((struct cont_prop_set_in *)in)->cpsi_prop = cpsi_prop;
		uuid_copy(((struct cont_prop_set_in *)in)->cpsi_pool_uuid, cpsi_pool_uuid);
	}
}

static inline void
cont_prop_set_in_set_cont_uuid(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			       uuid_t cont_uuid)
{
	void *in = crt_req_get(rpc);

	D_ASSERT(opc == CONT_PROP_SET);
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		uuid_copy(((struct cont_prop_set_v8_in *)in)->cpsi_op.ci_uuid, cont_uuid);
	else
		uuid_copy(((struct cont_prop_set_in *)in)->cpsi_op.ci_uuid, cont_uuid);
}

static inline void
cont_prop_set_bylabel_in_set_label(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
				   const char *label)
{
	void *in = crt_req_get(rpc);

	D_ASSERT(opc == CONT_PROP_SET_BYLABEL);
	/* NB: prop set by label is on the server side only - no version variants */
	((struct cont_prop_set_bylabel_in *)in)->cpsi_label = label;
}

/* clang-format off */

#define DAOS_ISEQ_CONT_ACL_UPDATE	/* input fields */		 \
	((struct cont_op_in)		(caui_op)		CRT_VAR) \
	((struct daos_acl)		(caui_acl)		CRT_PTR)

#define DAOS_ISEQ_CONT_ACL_UPDATE_V8 	/* input fields */		 \
	((struct cont_op_v8_in)		(caui_op)		CRT_VAR) \
	((struct daos_acl)		(caui_acl)		CRT_PTR)

#define DAOS_OSEQ_CONT_ACL_UPDATE	/* output fields */		 \
	((struct cont_op_out)		(cauo_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_acl_update, DAOS_ISEQ_CONT_ACL_UPDATE, DAOS_OSEQ_CONT_ACL_UPDATE)
CRT_RPC_DECLARE(cont_acl_update_v8, DAOS_ISEQ_CONT_ACL_UPDATE_V8, DAOS_OSEQ_CONT_ACL_UPDATE)

/* clang-format on */

static inline void
cont_acl_update_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			    struct daos_acl **caui_aclp)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		*caui_aclp = ((struct cont_acl_update_v8_in *)in)->caui_acl;
	else
		*caui_aclp = ((struct cont_acl_update_in *)in)->caui_acl;
}

static inline void
cont_acl_update_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			    struct daos_acl *caui_acl)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)
		((struct cont_acl_update_v8_in *)in)->caui_acl = caui_acl;
	else
		((struct cont_acl_update_in *)in)->caui_acl = caui_acl;
}

/* clang-format off */

#define DAOS_ISEQ_CONT_ACL_DELETE	/* input fields */		 \
	((struct cont_op_in)		(cadi_op)		CRT_VAR) \
	((d_string_t)			(cadi_principal_name)	CRT_VAR) \
	((uint8_t)			(cadi_principal_type)	CRT_VAR)

#define DAOS_ISEQ_CONT_ACL_DELETE_V8	/* input fields */		 \
	((struct cont_op_v8_in)		(cadi_op)		CRT_VAR) \
	((d_string_t)			(cadi_principal_name)	CRT_VAR) \
	((uint8_t)			(cadi_principal_type)	CRT_VAR)

#define DAOS_OSEQ_CONT_ACL_DELETE	/* output fields */		 \
	((struct cont_op_out)		(cado_op)		CRT_VAR)

CRT_RPC_DECLARE(cont_acl_delete, DAOS_ISEQ_CONT_ACL_DELETE, DAOS_OSEQ_CONT_ACL_DELETE)
CRT_RPC_DECLARE(cont_acl_delete_v8, DAOS_ISEQ_CONT_ACL_DELETE_V8, DAOS_OSEQ_CONT_ACL_DELETE)

/* clang-format on */

static inline void
cont_acl_delete_in_get_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			    d_string_t *cadi_principal_namep, uint8_t *cadi_principal_typep)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		*cadi_principal_namep = ((struct cont_acl_delete_v8_in *)in)->cadi_principal_name;
		*cadi_principal_typep = ((struct cont_acl_delete_v8_in *)in)->cadi_principal_type;
	} else {
		*cadi_principal_namep = ((struct cont_acl_delete_in *)in)->cadi_principal_name;
		*cadi_principal_typep = ((struct cont_acl_delete_in *)in)->cadi_principal_type;
	}
}

static inline void
cont_acl_delete_in_set_data(crt_rpc_t *rpc, crt_opcode_t opc, int cont_proto_ver,
			    d_string_t cadi_principal_name, uint8_t cadi_principal_type)
{
	void *in = crt_req_get(rpc);

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY) {
		((struct cont_acl_delete_v8_in *)in)->cadi_principal_name = cadi_principal_name;
		((struct cont_acl_delete_v8_in *)in)->cadi_principal_type = cadi_principal_type;
	} else {
		((struct cont_acl_delete_in *)in)->cadi_principal_name = cadi_principal_name;
		((struct cont_acl_delete_in *)in)->cadi_principal_type = cadi_principal_type;
	}
}

static inline int
cont_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc, uuid_t ci_pool_hdl,
		uuid_t ci_uuid, uuid_t ci_hdl, uint64_t *req_timep, crt_rpc_t **req)
{
	int                rc;
	crt_opcode_t       opcode;
	int                proto_ver;
	struct cont_op_in *in;

	proto_ver = dc_cont_proto_version ? dc_cont_proto_version : DAOS_CONT_VERSION;
	opcode    = DAOS_RPC_OPCODE(opc, DAOS_CONT_MODULE, proto_ver);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_CONT, tgt_ep->ep_tag);

	rc = crt_req_create(crt_ctx, tgt_ep, opcode, req);
	if (rc != 0)
		return rc;
	in = crt_req_get(*req);

	uuid_copy(in->ci_pool_hdl, ci_pool_hdl);
	uuid_copy(in->ci_uuid, ci_uuid);
	if (uuid_is_null(ci_hdl))
		uuid_clear(in->ci_hdl);
	else
		uuid_copy(in->ci_hdl, ci_hdl);

	if (req_timep && (*req_timep == 0))
		*req_timep = d_hlc_get();

	/* Temporary req_timep check: some opcodes aren't (yet) at v8 and don't have the op key */
	if (req_timep && (proto_ver >= CONT_PROTO_VER_WITH_SVC_OP_KEY)) {
		struct cont_op_v8_in *in8 = crt_req_get(*req);

		daos_get_client_uuid(&in8->ci_cli_id);
		in8->ci_time = *req_timep;
	}

	return rc;
}
#endif /* __CONTAINER_RPC_H__ */
