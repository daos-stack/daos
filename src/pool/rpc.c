/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include "rpc.h"

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
CRT_RPC_DEFINE(pool_replicas_add, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DEFINE(pool_replicas_remove, DAOS_ISEQ_POOL_MEMBERSHIP,
		DAOS_OSEQ_POOL_MEMBERSHIP)
CRT_RPC_DEFINE(pool_add, DAOS_ISEQ_POOL_TGT_UPDATE, DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_exclude, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_exclude_out, DAOS_ISEQ_POOL_TGT_UPDATE,
		DAOS_OSEQ_POOL_TGT_UPDATE)
CRT_RPC_DEFINE(pool_evict, DAOS_ISEQ_POOL_EVICT, DAOS_OSEQ_POOL_EVICT)
CRT_RPC_DEFINE(pool_svc_stop, DAOS_ISEQ_POOL_SVC_STOP, DAOS_OSEQ_POOL_SVC_STOP)
CRT_RPC_DEFINE(pool_tgt_connect, DAOS_ISEQ_POOL_TGT_CONNECT,
		DAOS_OSEQ_POOL_TGT_CONNECT)
CRT_RPC_DEFINE(pool_tgt_disconnect, DAOS_ISEQ_POOL_TGT_DISCONNECT,
		DAOS_OSEQ_POOL_TGT_DISCONNECT)
CRT_RPC_DEFINE(pool_tgt_update_map, DAOS_ISEQ_POOL_TGT_UPDATE_MAP,
		DAOS_OSEQ_POOL_TGT_UPDATE_MAP)
CRT_RPC_DEFINE(pool_tgt_query, DAOS_ISEQ_POOL_TGT_QUERY,
		DAOS_OSEQ_POOL_TGT_QUERY)
CRT_RPC_DEFINE(pool_get_acl, DAOS_ISEQ_POOL_GET_ACL, DAOS_OSEQ_POOL_GET_ACL)
CRT_RPC_DEFINE(pool_prop_set, DAOS_ISEQ_POOL_PROP_SET, DAOS_OSEQ_POOL_PROP_SET)

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

	D_REALLOC(new_addrs, addr_list->pta_addrs, (addr_list->pta_number + 1) *
			    sizeof(*addr_list->pta_addrs));
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
