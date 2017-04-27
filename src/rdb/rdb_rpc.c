/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rdb: RPCs
 */

#define DD_SUBSYS DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <raft.h>
#include <daos_srv/daos_server.h>
#include "rdb_internal.h"

static struct crt_msg_field *rdb_requestvote_in_fields[] = {
	&CMF_INT,	/* term */
	&CMF_INT,	/* candidate_id */
	&CMF_INT,	/* last_log_idx */
	&CMF_INT	/* last_log_term */
};

static struct crt_msg_field *rdb_requestvote_out_fields[] = {
	&CMF_INT,	/* term */
	&CMF_INT	/* vote_granted */
};

static struct crt_req_format DQF_RDB_REQUESTVOTE =
	DEFINE_CRT_REQ_FMT("RDB_REQUESTVOTE", rdb_requestvote_in_fields,
			   rdb_requestvote_out_fields);

static int
rdb_proc_msg_entry_t(crt_proc_t proc, void *data)
{
	msg_entry_t    *e = data;
	crt_proc_op_t	proc_op;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_uint32_t(proc, &e->term);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_uint32_t(proc, &e->id);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &e->type);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_uint32_t(proc, &e->data.len);
	if (rc != 0)
		return -DER_CRT_HG;
	if (proc_op == CRT_PROC_DECODE) {
		D_ALLOC(e->data.buf, e->data.len);
		if (e->data.buf == NULL)
			return -DER_NOMEM;
	}
	rc = crt_proc_memcpy(proc, e->data.buf, e->data.len);
	if (rc != 0) {
		if (proc_op == CRT_PROC_DECODE)
			D_FREE(e->data.buf, e->data.len);
		return -DER_CRT_HG;
	}
#if 0 /* TODO: Change raft_recv_appendentries() to leave this buffer alone. */
	if (proc_op == CRT_PROC_FREE)
		D_FREE(e->data.buf, e->data.len);
#endif
	return 0;
}

static int
rdb_proc_msg_appendentries_t(crt_proc_t proc, void *data)
{
	msg_appendentries_t    *ae = data;
	crt_proc_op_t		proc_op;
	int			i;
	int			rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &ae->term);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &ae->prev_log_idx);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &ae->prev_log_term);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &ae->leader_commit);
	if (rc != 0)
		return -DER_CRT_HG;
	rc = crt_proc_int32_t(proc, &ae->n_entries);
	if (rc != 0)
		return -DER_CRT_HG;
	if (proc_op == CRT_PROC_DECODE) {
		D_ALLOC(ae->entries, sizeof(*ae->entries) * ae->n_entries);
		if (ae->entries == NULL)
			return -DER_NOMEM;
	}
	for (i = 0; i < ae->n_entries; i++) {
		rc = rdb_proc_msg_entry_t(proc, &ae->entries[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(ae->entries,
				       sizeof(*ae->entries) * ae->n_entries);
			return -DER_CRT_HG;
		}
	}
	if (proc_op == CRT_PROC_FREE)
		D_FREE(ae->entries, sizeof(*ae->entries) * ae->n_entries);
	return 0;
}

static struct crt_msg_field DMF_MSG_APPENDENTRIES_T =
	DEFINE_CRT_MSG("msg_appendentries_t", 0, sizeof(msg_appendentries_t),
		       rdb_proc_msg_appendentries_t);

static struct crt_msg_field *rdb_appendentries_in_fields[] = {
	&DMF_MSG_APPENDENTRIES_T
};

static struct crt_msg_field *rdb_appendentries_out_fields[] = {
	&CMF_INT,	/* term */
	&CMF_INT,	/* success */
	&CMF_INT,	/* current_idx */
	&CMF_INT	/* first_idx */
};

static struct crt_req_format DQF_RDB_APPENDENTRIES =
	DEFINE_CRT_REQ_FMT("RDB_APPENDENTRIES", rdb_appendentries_in_fields,
			   rdb_appendentries_out_fields);

static struct crt_msg_field *rdb_start_in_fields[] = {
	&CMF_UUID,	/* uuid */
	&CMF_UUID,	/* pool */
	&CMF_UINT32,	/* flags */
	&CMF_UINT32,	/* padding */
	&CMF_UINT64,	/* size */
	&CMF_RANK_LIST	/* ranks */
};

static struct crt_msg_field *rdb_start_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_req_format DQF_RDB_START =
	DEFINE_CRT_REQ_FMT("RDB_START", rdb_start_in_fields,
			   rdb_start_out_fields);

static struct crt_msg_field *rdb_stop_in_fields[] = {
	&CMF_UUID,	/* uuid */
	&CMF_UUID,	/* pool */
	&CMF_UINT32	/* flags */
};

static struct crt_msg_field *rdb_stop_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_req_format DQF_RDB_STOP =
	DEFINE_CRT_REQ_FMT("RDB_STOP", rdb_stop_in_fields, rdb_stop_out_fields);

struct daos_rpc rdb_srv_rpcs[] = {
	{
		.dr_name	= "RDB_REQUESTVOTE",
		.dr_opc		= RDB_REQUESTVOTE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDB_REQUESTVOTE
	}, {
		.dr_name	= "RDB_APPENDENTRIES",
		.dr_opc		= RDB_APPENDENTRIES,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDB_APPENDENTRIES
	}, {
		.dr_name	= "RDB_START",
		.dr_opc		= RDB_START,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDB_START
	}, {
		.dr_name	= "RDB_STOP",
		.dr_opc		= RDB_STOP,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDB_STOP
	}, {
	}
};

int
rdb_create_raft_rpc(crt_opcode_t opc, raft_node_t *node, crt_rpc_t **rpc)
{
	struct rdb_raft_node   *rdb_node = raft_node_get_udata(node);
	crt_opcode_t		opc_full;
	crt_endpoint_t		ep;
	struct dss_module_info *info = dss_get_module_info();

	opc_full = DAOS_RPC_OPCODE(opc, DAOS_RDB_MODULE, 1);
	ep.ep_grp = NULL;
	ep.ep_rank = rdb_node->dn_rank;
	ep.ep_tag = 0;
	return crt_req_create(info->dmi_ctx, ep, opc_full, rpc);
}

int
rdb_create_bcast(crt_opcode_t opc, crt_group_t *group, crt_rpc_t **rpc)
{
	struct dss_module_info *info = dss_get_module_info();
	crt_opcode_t		opc_full;

	opc_full = DAOS_RPC_OPCODE(opc, DAOS_RDB_MODULE, 1);
	return crt_corpc_req_create(info->dmi_ctx, group,
				    NULL /* excluded_ranks */, opc_full,
				    NULL /* co_bulk_hdl */, NULL /* priv */,
				    0 /* flags */,
				    crt_tree_topo(CRT_TREE_FLAT, 0), rpc);
}

struct rdb_raft_rpc_cb_arg {
	daos_list_t	drc_entry;	/* in rdb::d_replies */
	crt_rpc_t      *drc_rpc;
	struct rdb     *drc_db;
	raft_node_t    *drc_node;
};

/* Daemon ULT for processing RPC responses (see rdb_raft_rpc_cb()) */
void
rdb_recvd(void *arg)
{
	struct rdb *db = arg;

	D_DEBUG(DB_ANY, DF_DB": recvd starting\n", DP_DB(db));
	for (;;) {
		struct rdb_raft_rpc_cb_arg     *arg;
		struct rdb_raft_rpc_cb_arg     *tmp;

		for (;;) {
			if (!daos_list_empty(&db->d_replies))
				break;
			if (db->d_recvd_stop)
				return;
			ABT_thread_yield();
		}
		daos_list_for_each_entry_safe(arg, tmp, &db->d_replies,
					      drc_entry) {
			daos_list_del_init(&arg->drc_entry);
			rdb_raft_process_reply(db, arg->drc_node, arg->drc_rpc);
			crt_req_decref(arg->drc_rpc);
			D_FREE_PTR(arg);
		}
	}
	D_DEBUG(DB_ANY, DF_DB": recvd stopping\n", DP_DB(db));
}

/*
 * This may be called during a crt_req_create() call on the same context! For
 * instance:
 *
 *   rdb_raft_cb_send_requestvote()	// to rank x
 *   rdb_raft_cb_send_requestvote()	// to rank y
 *     rdb_create_raft_rpc()
 *       crt_req_create()
 *         rdb_raft_rpc_cb()		// for rpc to rank x
 *
 * To work around this issue, this callback just append the rpc to
 * rdb::d_replies, which rdb_recvd() consumes and completes the reply handling.
 */
static int
rdb_raft_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct rdb_raft_rpc_cb_arg     *arg = cb_info->cci_arg;
	struct rdb		       *db = arg->drc_db;
	crt_opcode_t			opc = opc_get(cb_info->cci_rpc->cr_opc);
	int				rc = cb_info->cci_rc;

	if (rc != 0) {
		/* Drop this RPC, assuming that raft will make a new one. */
		crt_req_decref(arg->drc_rpc);
		D_FREE_PTR(arg);
		return rc;
	}

	daos_list_add_tail(&arg->drc_entry, &db->d_replies);
	D_DEBUG(DB_ANY, DF_DB": queued response %u\n", DP_DB(db), opc);
	return 0;
}

int
rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db, raft_node_t *node)
{
	struct rdb_raft_rpc_cb_arg     *arg;
	int				rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;
	DAOS_INIT_LIST_HEAD(&arg->drc_entry);
	crt_req_addref(rpc);
	arg->drc_rpc = rpc;
	arg->drc_db = db;
	arg->drc_node = node;

	rc = crt_req_send(rpc, rdb_raft_rpc_cb, arg);
	if (rc != 0) {
		crt_req_decref(arg->drc_rpc);
		D_FREE_PTR(arg);
	}
	return rc;
}
