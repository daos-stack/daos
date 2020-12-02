/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * dc_pool/ds_pool: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_pool.h>
#include "rpc.h"

#define crt_proc_daos_target_state_t crt_proc_uint32_t

static int
crt_proc_struct_pool_target_addr(crt_proc_t proc, struct pool_target_addr *tgt)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &tgt->pta_rank);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &tgt->pta_target);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_rsvc_hint(crt_proc_t proc, struct rsvc_hint *hint)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &hint->sh_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &hint->sh_rank);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &hint->sh_term);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_daos_pool_space(crt_proc_t proc, struct daos_pool_space *ps)
{
	int i, rc;

	for (i = 0; i < DAOS_MEDIA_MAX; i++) {
		rc = crt_proc_uint64_t(proc, &ps->ps_space.s_total[i]);
		if (rc)
			return -DER_HG;

		rc = crt_proc_uint64_t(proc, &ps->ps_space.s_free[i]);
		if (rc)
			return -DER_HG;

		rc = crt_proc_uint64_t(proc, &ps->ps_free_min[i]);
		if (rc)
			return -DER_HG;

		rc = crt_proc_uint64_t(proc, &ps->ps_free_max[i]);
		if (rc)
			return -DER_HG;

		rc = crt_proc_uint64_t(proc, &ps->ps_free_mean[i]);
		if (rc)
			return -DER_HG;
	}

	rc = crt_proc_uint32_t(proc, &ps->ps_ntargets);
	if (rc)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &ps->ps_padding);
	if (rc)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_daos_rebuild_status(crt_proc_t proc,
				    struct daos_rebuild_status *drs)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &drs->rs_version);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &drs->rs_seconds);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_int32_t(proc, &drs->rs_errno);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_int32_t(proc, &drs->rs_done);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_int32_t(proc, &drs->rs_padding32);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_int32_t(proc, &drs->rs_fail_rank);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &drs->rs_toberb_obj_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &drs->rs_obj_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &drs->rs_rec_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &drs->rs_size);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

CRT_RPC_DEFINE(pool_op, DAOS_ISEQ_POOL_OP, DAOS_OSEQ_POOL_OP)
CRT_RPC_DEFINE(pool_create, DAOS_ISEQ_POOL_CREATE, DAOS_OSEQ_POOL_CREATE)
CRT_RPC_DEFINE(pool_connect, DAOS_ISEQ_POOL_CONNECT, DAOS_OSEQ_POOL_CONNECT)
CRT_RPC_DEFINE(pool_disconnect, DAOS_ISEQ_POOL_DISCONNECT,
		DAOS_OSEQ_POOL_DISCONNECT)
CRT_RPC_DEFINE(pool_query, DAOS_ISEQ_POOL_QUERY, DAOS_OSEQ_POOL_QUERY)
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
CRT_RPC_DEFINE(pool_list_cont, DAOS_ISEQ_POOL_LIST_CONT,
		DAOS_OSEQ_POOL_LIST_CONT)
CRT_RPC_DEFINE(pool_query_info, DAOS_ISEQ_POOL_QUERY_INFO,
		DAOS_OSEQ_POOL_QUERY_INFO)

/* Define for cont_rpcs[] array population below.
 * See POOL_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format pool_proto_rpc_fmt[] = {
	POOL_PROTO_CLI_RPC_LIST,
	POOL_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format pool_proto_fmt = {
	.cpf_name  = "pool-proto",
	.cpf_ver   = DAOS_POOL_VERSION,
	.cpf_count = ARRAY_SIZE(pool_proto_rpc_fmt),
	.cpf_prf   = pool_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_POOL_MODULE, 0)
};

static bool
pool_target_addr_equal(struct pool_target_addr *addr1,
		       struct pool_target_addr *addr2)
{
	return addr1->pta_rank == addr2->pta_rank &&
	       addr1->pta_target == addr2->pta_target;
}

static bool
pool_target_addr_found(struct pool_target_addr_list *addr_list,
		       struct pool_target_addr *tgt)
{
	int i;

	for (i = 0; i < addr_list->pta_number; i++)
		if (pool_target_addr_equal(&addr_list->pta_addrs[i], tgt))
			return true;
	return false;
}

int
pool_target_addr_list_append(struct pool_target_addr_list *addr_list,
			     struct pool_target_addr *addr)
{
	struct pool_target_addr	*new_addrs;

	if (pool_target_addr_found(addr_list, addr))
		return 0;

	D_REALLOC_ARRAY(new_addrs, addr_list->pta_addrs,
			addr_list->pta_number + 1);
	if (new_addrs == NULL)
		return -DER_NOMEM;

	new_addrs[addr_list->pta_number] = *addr;
	addr_list->pta_addrs = new_addrs;
	addr_list->pta_number++;

	return 0;
}

int
pool_target_addr_list_alloc(unsigned int num,
			    struct pool_target_addr_list *addr_list)
{
	D_ALLOC_ARRAY(addr_list->pta_addrs, num);
	if (addr_list->pta_addrs == NULL)
		return -DER_NOMEM;

	addr_list->pta_number = num;

	return 0;
}

void
pool_target_addr_list_free(struct pool_target_addr_list *addr_list)
{
	if (addr_list == NULL)
		return;

	if (addr_list->pta_addrs)
		D_FREE(addr_list->pta_addrs);
}

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
		case DAOS_PROP_PO_ACL:
			bits |= DAOS_PO_QUERY_PROP_ACL;
			break;
		case DAOS_PROP_PO_OWNER:
			bits |= DAOS_PO_QUERY_PROP_OWNER;
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			bits |= DAOS_PO_QUERY_PROP_OWNER_GROUP;
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
	info->pi_ntargets	= map_buf->pb_target_nr;
	info->pi_nnodes		= map_buf->pb_node_nr;
	info->pi_map_ver	= map_version;
	info->pi_leader		= leader_rank;
	if (info->pi_bits & DPI_SPACE)
		info->pi_space		= *ps;
	if (info->pi_bits & DPI_REBUILD_STATUS)
		info->pi_rebuild_st	= *rs;
}

int
list_cont_bulk_create(crt_context_t ctx, crt_bulk_t *bulk,
		      struct daos_pool_cont_info *buf, daos_size_t ncont)
{
	d_iov_t		iov;
	d_sg_list_t	sgl;

	d_iov_set(&iov, buf, ncont * sizeof(struct daos_pool_cont_info));
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

