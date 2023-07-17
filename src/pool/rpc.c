/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_pool/ds_pool: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_pool.h>
#include "rpc.h"

char *
dc_pool_op_str(enum pool_operation op)
{
#define X(a, b, c, d, e) case a: return #a;
	switch (op) {
	POOL_PROTO_RPC_LIST
	default: return "POOL_UNKNOWN_OPERATION";
	}
#undef X
}

#define crt_proc_daos_target_state_t crt_proc_uint32_t

static int
crt_proc_struct_pool_target_addr(crt_proc_t proc, crt_proc_op_t proc_op,
				 struct pool_target_addr *tgt)
{
	int rc;

	rc = crt_proc_uint32_t(proc, proc_op, &tgt->pta_rank);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, proc_op, &tgt->pta_target);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

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

CRT_RPC_DEFINE(pool_op, DAOS_ISEQ_POOL_OP, DAOS_OSEQ_POOL_OP)

static int
crt_proc_struct_pool_op_in(crt_proc_t proc, crt_proc_op_t proc_op,
			   struct pool_op_in *data)
{
	return crt_proc_pool_op_in(proc, data);
}

static int
crt_proc_struct_pool_op_out(crt_proc_t proc, crt_proc_op_t proc_op,
			    struct pool_op_out *data)
{
	return crt_proc_pool_op_out(proc, data);
}

CRT_RPC_DEFINE(pool_create, DAOS_ISEQ_POOL_CREATE, DAOS_OSEQ_POOL_CREATE)
CRT_RPC_DEFINE(pool_connect_v4, DAOS_ISEQ_POOL_CONNECT_V4, DAOS_OSEQ_POOL_CONNECT)
CRT_RPC_DEFINE(pool_connect_v5, DAOS_ISEQ_POOL_CONNECT_V5, DAOS_OSEQ_POOL_CONNECT)
CRT_RPC_DEFINE(pool_disconnect, DAOS_ISEQ_POOL_DISCONNECT,
		DAOS_OSEQ_POOL_DISCONNECT)
CRT_RPC_DEFINE(pool_query_v4, DAOS_ISEQ_POOL_QUERY, DAOS_OSEQ_POOL_QUERY_V4)
CRT_RPC_DEFINE(pool_query_v5, DAOS_ISEQ_POOL_QUERY, DAOS_OSEQ_POOL_QUERY_V5)
CRT_RPC_DEFINE(pool_attr_list, DAOS_ISEQ_POOL_ATTR_LIST,
		DAOS_OSEQ_POOL_ATTR_LIST)
CRT_RPC_DEFINE(pool_attr_get, DAOS_ISEQ_POOL_ATTR_GET, DAOS_OSEQ_POOL_OP)
CRT_RPC_DEFINE(pool_attr_set, DAOS_ISEQ_POOL_ATTR_SET, DAOS_OSEQ_POOL_OP)
CRT_RPC_DEFINE(pool_attr_del, DAOS_ISEQ_POOL_ATTR_DEL, DAOS_OSEQ_POOL_OP)
CRT_RPC_DEFINE(pool_replicas_add, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DEFINE(pool_replicas_remove, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DEFINE(pool_extend, DAOS_ISEQ_POOL_EXTEND,
		DAOS_OSEQ_POOL_EXTEND)
CRT_RPC_DEFINE(pool_add, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_add_in, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_exclude, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_drain, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_exclude_out, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_evict, DAOS_ISEQ_POOL_EVICT, DAOS_OSEQ_POOL_EVICT)
CRT_RPC_DEFINE(pool_svc_stop, DAOS_ISEQ_POOL_SVC_STOP, DAOS_OSEQ_POOL_SVC_STOP)
CRT_RPC_DEFINE(pool_tgt_disconnect, DAOS_ISEQ_POOL_TGT_DISCONNECT,
		DAOS_OSEQ_POOL_TGT_DISCONNECT)
CRT_RPC_DEFINE(pool_tgt_query, DAOS_ISEQ_POOL_TGT_QUERY,
		DAOS_OSEQ_POOL_TGT_QUERY)
CRT_RPC_DEFINE(pool_tgt_dist_hdls, DAOS_ISEQ_POOL_TGT_DIST_HDLS,
		DAOS_OSEQ_POOL_TGT_DIST_HDLS)
CRT_RPC_DEFINE(pool_prop_get, DAOS_ISEQ_POOL_PROP_GET, DAOS_OSEQ_POOL_PROP_GET)
CRT_RPC_DEFINE(pool_prop_set, DAOS_ISEQ_POOL_PROP_SET, DAOS_OSEQ_POOL_PROP_SET)
CRT_RPC_DEFINE(pool_acl_update, DAOS_ISEQ_POOL_ACL_UPDATE,
		DAOS_OSEQ_POOL_ACL_UPDATE)
CRT_RPC_DEFINE(pool_acl_delete, DAOS_ISEQ_POOL_ACL_DELETE,
		DAOS_OSEQ_POOL_ACL_DELETE)
CRT_RPC_DEFINE(pool_ranks_get, DAOS_ISEQ_POOL_RANKS_GET,
		DAOS_OSEQ_POOL_RANKS_GET)
CRT_RPC_DEFINE(pool_upgrade, DAOS_ISEQ_POOL_UPGRADE,
		DAOS_OSEQ_POOL_UPGRADE)
CRT_RPC_DEFINE(pool_list_cont, DAOS_ISEQ_POOL_LIST_CONT,
		DAOS_OSEQ_POOL_LIST_CONT)

static int
pool_cont_filter_t_proc_parts(crt_proc_t proc, crt_proc_op_t proc_op,
			      uint32_t n_parts, daos_pool_cont_filter_part_t ***parts)
{
	int				rc = 0;
	uint32_t			p = 0;
	uint32_t			j;
	daos_pool_cont_filter_part_t   *part;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(*parts, n_parts);
		if (*parts == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	while (p < n_parts) {
		if (DECODING(proc_op)) {
			D_ALLOC((*parts)[p], sizeof(daos_pool_cont_filter_part_t));
			if ((*parts)[p] == NULL)
				D_GOTO(out_free, rc = -DER_NOMEM);
		}
		part = (*parts)[p++];

		rc = crt_proc_uint32_t(proc, proc_op, &part->pcfp_func);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				goto out_free;
			goto out;
		}

		rc = crt_proc_uint32_t(proc, proc_op, &part->pcfp_key);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				goto out_free;
			goto out;
		}

		/* all keys have uint64_t values for now */
		rc = crt_proc_uint64_t(proc, proc_op, &part->pcfp_val64);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				goto out_free;
			goto out;
		}
	}

	if (FREEING(proc_op)) {
out_free:
		for (j = 0; j < p; j++)
			D_FREE((*parts)[j]);
		D_FREE(*parts);
	}
out:
	return rc;
}

static int
crt_proc_daos_pool_cont_filter_t(crt_proc_t proc, crt_proc_op_t proc_op,
				 daos_pool_cont_filter_t *filt)
{
	int	rc;

	rc = crt_proc_uint32_t(proc, proc_op, &filt->pcf_combine_func);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &filt->pcf_nparts);
	if (unlikely(rc))
		return rc;

	rc = pool_cont_filter_t_proc_parts(proc, proc_op, filt->pcf_nparts, &filt->pcf_parts);
	if (unlikely(rc))
		return rc;

	return 0;
}

CRT_RPC_DEFINE(pool_filter_cont, DAOS_ISEQ_POOL_FILTER_CONT, DAOS_OSEQ_POOL_FILTER_CONT)
CRT_RPC_DEFINE(pool_query_info, DAOS_ISEQ_POOL_QUERY_INFO, DAOS_OSEQ_POOL_QUERY_INFO)
CRT_RPC_DEFINE(pool_tgt_query_map, DAOS_ISEQ_POOL_TGT_QUERY_MAP, DAOS_OSEQ_POOL_TGT_QUERY_MAP)
CRT_RPC_DEFINE(pool_tgt_discard, DAOS_ISEQ_POOL_TGT_DISCARD, DAOS_OSEQ_POOL_TGT_DISCARD)

/* Define for cont_rpcs[] array population below.
 * See POOL_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
},

static struct crt_proto_rpc_format pool_proto_rpc_fmt_v4[] = {
	POOL_PROTO_CLI_RPC_LIST(4)
	POOL_PROTO_SRV_RPC_LIST
};

static struct crt_proto_rpc_format pool_proto_rpc_fmt_v5[] = {
	POOL_PROTO_CLI_RPC_LIST(5)
	POOL_PROTO_SRV_RPC_LIST
};

#undef X

struct crt_proto_format pool_proto_fmt_v4 = {
	.cpf_name  = "pool",
	.cpf_ver   = 4,
	.cpf_count = ARRAY_SIZE(pool_proto_rpc_fmt_v4),
	.cpf_prf   = pool_proto_rpc_fmt_v4,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_POOL_MODULE, 0)
};

struct crt_proto_format pool_proto_fmt_v5 = {
	.cpf_name  = "pool",
	.cpf_ver   = 5,
	.cpf_count = ARRAY_SIZE(pool_proto_rpc_fmt_v5),
	.cpf_prf   = pool_proto_rpc_fmt_v5,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_POOL_MODULE, 0)
};

uint64_t
pool_query_bits(daos_pool_info_t *po_info, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	uint64_t		 bits = 0;
	int			 i;

	if (po_info != NULL) {
		if (po_info->pi_bits & DPI_SPACE)
			bits |= DAOS_PO_QUERY_SPACE;
		if (po_info->pi_bits & DPI_REBUILD_STATUS)
			bits |= DAOS_PO_QUERY_REBUILD_STATUS;
	}

	if (prop == NULL)
		goto out;
	if (prop->dpp_entries == NULL) {
		bits |= DAOS_PO_QUERY_PROP_ALL;
		goto out;
	}

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
			bits |= DAOS_PO_QUERY_PROP_LABEL;
			break;
		case DAOS_PROP_PO_SPACE_RB:
			bits |= DAOS_PO_QUERY_PROP_SPACE_RB;
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			bits |= DAOS_PO_QUERY_PROP_SELF_HEAL;
			break;
		case DAOS_PROP_PO_RECLAIM:
			bits |= DAOS_PO_QUERY_PROP_RECLAIM;
			break;
		case DAOS_PROP_PO_EC_CELL_SZ:
			bits |= DAOS_PO_QUERY_PROP_EC_CELL_SZ;
			break;
		case DAOS_PROP_PO_REDUN_FAC:
			bits |= DAOS_PO_QUERY_PROP_REDUN_FAC;
		case DAOS_PROP_PO_EC_PDA:
			bits |= DAOS_PO_QUERY_PROP_EC_PDA;
			break;
		case DAOS_PROP_PO_RP_PDA:
			bits |= DAOS_PO_QUERY_PROP_RP_PDA;
			break;
		case DAOS_PROP_PO_PERF_DOMAIN:
			bits |= DAOS_PO_QUERY_PROP_PERF_DOMAIN;
			break;
		case DAOS_PROP_PO_ACL:
			bits |= DAOS_PO_QUERY_PROP_ACL;
			break;
		case DAOS_PROP_PO_OWNER:
			bits |= DAOS_PO_QUERY_PROP_OWNER;
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			bits |= DAOS_PO_QUERY_PROP_OWNER_GROUP;
			break;
		case DAOS_PROP_PO_POLICY:
			bits |= DAOS_PO_QUERY_PROP_POLICY;
			break;
		case DAOS_PROP_PO_GLOBAL_VERSION:
			bits |= DAOS_PO_QUERY_PROP_GLOBAL_VERSION;
			break;
		case DAOS_PROP_PO_UPGRADE_STATUS:
			bits |= DAOS_PO_QUERY_PROP_UPGRADE_STATUS;
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			bits |= DAOS_PO_QUERY_PROP_SCRUB_MODE;
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			bits |= DAOS_PO_QUERY_PROP_SCRUB_FREQ;
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			bits |= DAOS_PO_QUERY_PROP_SCRUB_THRESH;
			break;
		case DAOS_PROP_PO_SVC_REDUN_FAC:
			bits |= DAOS_PO_QUERY_PROP_SVC_REDUN_FAC;
			break;
		case DAOS_PROP_PO_SVC_LIST:
			bits |= DAOS_PO_QUERY_PROP_SVC_LIST;
			break;
		case DAOS_PROP_PO_OBJ_VERSION:
			bits |= DAOS_PO_QUERY_PROP_OBJ_VERSION;
			break;
		case DAOS_PROP_PO_CHECKPOINT_MODE:
			bits |= DAOS_PO_QUERY_PROP_CHECKPOINT_MODE;
			break;
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
			bits |= DAOS_PO_QUERY_PROP_CHECKPOINT_FREQ;
			break;
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
			bits |= DAOS_PO_QUERY_PROP_CHECKPOINT_THRESH;
			break;
		default:
			D_ERROR("ignore bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}

out:
	return bits;
}

/**
 * Translates the response from a pool query RPC to a pool_info structure.
 *
 * \param[in]		pool_uuid	UUID of the pool
 * \param[in]		map_buf		Map buffer for pool
 * \param[in]		map_version	Pool map version
 * \param[in]		leader_rank	Pool leader rank
 * \param[in]		ps		Pool space
 * \param[in]		rs		Rebuild status
 * @param[in][out]	info		Pool info - pass in with pi_bits set
 *					Returned populated with inputs
 */
void
pool_query_reply_to_info(uuid_t pool_uuid, struct pool_buf *map_buf,
			 uint32_t map_version, uint32_t leader_rank,
			 struct daos_pool_space *ps,
			 struct daos_rebuild_status *rs,
			 daos_pool_info_t *info)
{
	D_ASSERT(ps != NULL);
	D_ASSERT(rs != NULL);

	uuid_copy(info->pi_uuid, pool_uuid);
	info->pi_ntargets		= map_buf->pb_target_nr;
	info->pi_nnodes			= map_buf->pb_node_nr;
	info->pi_map_ver		= map_version;
	info->pi_leader			= leader_rank;
	if (info->pi_bits & DPI_SPACE)
		info->pi_space		= *ps;
	if (info->pi_bits & DPI_REBUILD_STATUS)
		info->pi_rebuild_st	= *rs;
}

int
list_cont_bulk_create(crt_context_t ctx, crt_bulk_t *bulk,
		      void *buf, daos_size_t buf_nbytes)
{
	d_iov_t		iov;
	d_sg_list_t	sgl;

	d_iov_set(&iov, buf, buf_nbytes);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	return crt_bulk_create(ctx, &sgl, CRT_BULK_RW, bulk);
}

void
list_cont_bulk_destroy(crt_bulk_t bulk)
{
	if (bulk != CRT_BULK_NULL)
		crt_bulk_free(bulk);
}

int
map_bulk_create(crt_context_t ctx, crt_bulk_t *bulk, struct pool_buf **buf,
		unsigned int nr)
{
	d_iov_t	iov;
	d_sg_list_t	sgl;
	int		rc;

	*buf = pool_buf_alloc(nr);
	if (*buf == NULL)
		return -DER_NOMEM;

	d_iov_set(&iov, *buf, pool_buf_size((*buf)->pb_nr));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	rc = crt_bulk_create(ctx, &sgl, CRT_BULK_RW, bulk);
	if (rc != 0) {
		pool_buf_free(*buf);
		*buf = NULL;
	}

	return rc;
}

void
map_bulk_destroy(crt_bulk_t bulk, struct pool_buf *buf)
{
	crt_bulk_free(bulk);
	pool_buf_free(buf);
}

