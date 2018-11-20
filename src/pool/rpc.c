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
 * dc_pool/ds_pool: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos/rpc.h>
#include "rpc.h"

static int
proc_pool_target_addr(crt_proc_t proc, struct pool_target_addr *tgt)
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

struct crt_msg_field DMF_TGT_ADDR_LIST =
	DEFINE_CRT_MSG(CMF_ARRAY_FLAG, sizeof(struct pool_target_addr),
			proc_pool_target_addr);

struct crt_msg_field *pool_create_in_fields[] = {
	&CMF_UUID,		/* op.uuid */
	&CMF_UUID,		/* op.hdl */
	&CMF_UINT32,		/* uid */
	&CMF_UINT32,		/* gid */
	&CMF_UINT32,		/* mode */
	&CMF_UINT32,		/* ntgts */
	&DMF_UUID_ARRAY,	/* tgt_uuids */
	&CMF_RANK_LIST,		/* tgt_ranks */
	&CMF_UINT32,		/* ndomains */
	&CMF_UINT32,		/* padding */
	&DMF_UINT32_ARRAY	/* domains */
};

struct crt_msg_field *pool_create_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version (unused) */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_connect_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT64,	/* capas */
	&CMF_BULK	/* map_bulk */
};

struct crt_msg_field *pool_connect_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32,	/* map_buf_size */
	&CMF_UINT32,	/* rebuild_st.version */
	&CMF_UINT32,	/* rebuild_st.pad_32 */
	&CMF_INT,	/* rebuild_st.errno */
	&CMF_INT,	/* rebuild_st.done */
	&CMF_UINT64,	/* rebuild_st.toberb_obj_nr */
	&CMF_UINT64,	/* rebuild_st.obj_nr */
	&CMF_UINT64,	/* rebuild_st.rec_nr */
};

struct crt_msg_field *pool_disconnect_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_disconnect_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_query_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_BULK	/* map_bulk */
};

struct crt_msg_field *pool_query_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&CMF_UINT32,	/* uid */
	&CMF_UINT32,	/* gid */
	&CMF_UINT32,	/* mode */
	&CMF_UINT32,	/* map_buf_size */
	&CMF_UINT32,	/* rebuild_st.version */
	&CMF_UINT32,	/* rebuild_st.pad_32 */
	&CMF_INT,	/* rebuild_st.errno */
	&CMF_INT,	/* rebuild_st.done */
	&CMF_UINT64,	/* rebuild_st.toberb_obj_nr */
	&CMF_UINT64,	/* rebuild_st.obj_nr */
	&CMF_UINT64,	/* rebuild_st.rec_nr */
};

struct crt_msg_field *pool_attr_list_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *pool_attr_list_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
	&CMF_UINT64		/* names size */
};

struct crt_msg_field *pool_attr_get_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT64,	/* count */
	&CMF_UINT64,	/* key length */
	&CMF_BULK	/* attr bulk */
};

struct crt_msg_field *pool_attr_get_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *pool_attr_set_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&CMF_UINT64,	/* count */
	&CMF_BULK	/* bulk */
};

struct crt_msg_field *pool_attr_set_out_fields[] = {
	&CMF_INT,		/* op.rc */
	&CMF_UINT32,		/* op.map_version */
	&DMF_RSVC_HINT,		/* op.hint */
};

struct crt_msg_field *pool_tgt_update_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID,	/* op.handle */
	&DMF_TGT_ADDR_LIST,	/* tgt addr list */
};

struct crt_msg_field *pool_tgt_update_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT,	/* op.hint */
	&DMF_TGT_ADDR_LIST,	/* tgt addr list */
};

struct crt_msg_field *pool_evict_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_evict_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_svc_stop_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_UUID	/* op.handle */
};

struct crt_msg_field *pool_svc_stop_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.map_version */
	&DMF_RSVC_HINT	/* op.hint */
};

struct crt_msg_field *pool_tgt_connect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UUID,	/* pool_hdl */
	&CMF_UINT64,	/* capas */
	&CMF_UINT32,	/* pool_map_version */
	&CMF_UINT32,	/* iv class id */
	&CMF_UINT32,	/* master rank */
	&CMF_UINT32,	/* padding */
	&CMF_IOVEC	/* iv context */
};

struct crt_msg_field *pool_tgt_connect_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *pool_tgt_disconnect_in_fields[] = {
	&CMF_UUID,	/* pool */
	&DMF_UUID_ARRAY	/* hdls */
};

struct crt_msg_field *pool_tgt_disconnect_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_msg_field *pool_tgt_update_map_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UINT32	/* map_version */
};

struct crt_msg_field *pool_tgt_update_map_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_msg_field *pool_rdb_start_in_fields[] = {
	&CMF_UUID,	/* dbid */
	&CMF_UUID,	/* pool */
	&CMF_UINT32,	/* flags */
	&CMF_UINT32,	/* padding */
	&CMF_UINT64,	/* size */
	&CMF_RANK_LIST	/* ranks */
};

static struct crt_msg_field *pool_rdb_start_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_msg_field *pool_rdb_stop_in_fields[] = {
	&CMF_UUID,	/* pool */
	&CMF_UINT32	/* flags */
};

static struct crt_msg_field *pool_rdb_stop_out_fields[] = {
	&CMF_INT	/* rc */
};

struct crt_req_format DQF_POOL_CREATE =
	DEFINE_CRT_REQ_FMT(pool_create_in_fields, pool_create_out_fields);

struct crt_req_format DQF_POOL_CONNECT =
	DEFINE_CRT_REQ_FMT(pool_connect_in_fields, pool_connect_out_fields);

struct crt_req_format DQF_POOL_DISCONNECT =
	DEFINE_CRT_REQ_FMT(pool_disconnect_in_fields,
			   pool_disconnect_out_fields);

struct crt_req_format DQF_POOL_QUERY =
	DEFINE_CRT_REQ_FMT(pool_query_in_fields, pool_query_out_fields);

struct crt_req_format DQF_POOL_EXCLUDE =
	DEFINE_CRT_REQ_FMT(pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_EXCLUDE_OUT =
	DEFINE_CRT_REQ_FMT(pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_ADD =
	DEFINE_CRT_REQ_FMT(pool_tgt_update_in_fields,
			   pool_tgt_update_out_fields);

struct crt_req_format DQF_POOL_EVICT =
	DEFINE_CRT_REQ_FMT(pool_evict_in_fields, pool_evict_out_fields);

struct crt_req_format DQF_POOL_SVC_STOP =
	DEFINE_CRT_REQ_FMT(pool_svc_stop_in_fields, pool_svc_stop_out_fields);

struct crt_req_format DQF_POOL_ATTR_LIST =
	DEFINE_CRT_REQ_FMT(pool_attr_list_in_fields, pool_attr_list_out_fields);

struct crt_req_format DQF_POOL_ATTR_GET =
	DEFINE_CRT_REQ_FMT(pool_attr_get_in_fields, pool_attr_get_out_fields);

struct crt_req_format DQF_POOL_ATTR_SET =
	DEFINE_CRT_REQ_FMT(pool_attr_set_in_fields, pool_attr_set_out_fields);

struct crt_req_format DQF_POOL_TGT_CONNECT =
	DEFINE_CRT_REQ_FMT(pool_tgt_connect_in_fields,
			   pool_tgt_connect_out_fields);

struct crt_req_format DQF_POOL_TGT_DISCONNECT =
	DEFINE_CRT_REQ_FMT(pool_tgt_disconnect_in_fields,
			   pool_tgt_disconnect_out_fields);

struct crt_req_format DQF_POOL_TGT_UPDATE_MAP =
	DEFINE_CRT_REQ_FMT(pool_tgt_update_map_in_fields,
			   pool_tgt_update_map_out_fields);

struct crt_req_format DQF_POOL_RDB_START =
	DEFINE_CRT_REQ_FMT(pool_rdb_start_in_fields, pool_rdb_start_out_fields);

struct crt_req_format DQF_POOL_RDB_STOP =
	DEFINE_CRT_REQ_FMT(pool_rdb_stop_in_fields, pool_rdb_stop_out_fields);

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

	new_addrs = realloc(addr_list->pta_addrs, (addr_list->pta_number + 1) *
			    sizeof(*addr_list->pta_addrs));
	if (addr_list == NULL)
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
	D_ALLOC(addr_list->pta_addrs,
		num * sizeof(struct pool_target_addr));
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
