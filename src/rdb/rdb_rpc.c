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

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <raft.h>
#include <daos_srv/daos_server.h>
#include "rdb_internal.h"

static struct crt_msg_field *rdb_requestvote_in_fields[] = {
	&CMF_UUID,	/* op.uuid */
	&CMF_INT,	/* msg.term */
	&CMF_INT,	/* msg.candidate_id */
	&CMF_INT,	/* msg.last_log_idx */
	&CMF_INT	/* msg.last_log_term */
};

static struct crt_msg_field *rdb_requestvote_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.padding */
	&CMF_INT,	/* msg.term */
	&CMF_INT	/* msg.vote_granted */
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
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &e->term);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &e->id);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &e->type);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &e->data.len);
	if (rc != 0)
		return -DER_HG;
	if (proc_op == CRT_PROC_DECODE) {
		if (e->data.len > 0) {
			D_ALLOC(e->data.buf, e->data.len);
			if (e->data.buf == NULL)
				return -DER_NOMEM;
		} else {
			e->data.buf = NULL;
		}
	}
	if (e->data.len > 0) {
		rc = crt_proc_memcpy(proc, e->data.buf, e->data.len);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(e->data.buf);
			return -DER_HG;
		}
	}
	if (proc_op == CRT_PROC_FREE && e->data.buf != NULL)
		D_FREE(e->data.buf);
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
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &ae->term);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &ae->prev_log_idx);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &ae->prev_log_term);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &ae->leader_commit);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_int32_t(proc, &ae->n_entries);
	if (rc != 0)
		return -DER_HG;
	if (proc_op == CRT_PROC_DECODE) {
		if (ae->n_entries > 0) {
			D_ALLOC(ae->entries,
				sizeof(*ae->entries) * ae->n_entries);
			if (ae->entries == NULL)
				return -DER_NOMEM;
		} else {
			ae->entries = NULL;
		}
	}
	for (i = 0; i < ae->n_entries; i++) {
		rc = rdb_proc_msg_entry_t(proc, &ae->entries[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(ae->entries);
			return -DER_HG;
		}
	}
	if (proc_op == CRT_PROC_FREE && ae->entries != NULL)
		D_FREE(ae->entries);
	return 0;
}

static struct crt_msg_field DMF_MSG_APPENDENTRIES_T =
	DEFINE_CRT_MSG("msg_appendentries_t", 0, sizeof(msg_appendentries_t),
		       rdb_proc_msg_appendentries_t);

static struct crt_msg_field *rdb_appendentries_in_fields[] = {
	&CMF_UUID,			/* op.uuid */
	&DMF_MSG_APPENDENTRIES_T	/* msg */
};

static struct crt_msg_field *rdb_appendentries_out_fields[] = {
	&CMF_INT,	/* op.rc */
	&CMF_UINT32,	/* op.padding */
	&CMF_INT,	/* msg.term */
	&CMF_INT,	/* msg.success */
	&CMF_INT,	/* msg.current_idx */
	&CMF_INT	/* msg.first_idx */
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
	return crt_req_create(info->dmi_ctx, &ep, opc_full, rpc);
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

struct rdb_raft_rpc {
	d_list_t	drc_entry;	/* in rdb::{d_requests,d_replies} */
	crt_rpc_t      *drc_rpc;
	struct rdb     *drc_db;
	raft_node_t    *drc_node;
	double		drc_sent;
};

static struct rdb_raft_rpc *
rdb_alloc_raft_rpc(struct rdb *db, crt_rpc_t *rpc, raft_node_t *node)
{
	struct rdb_raft_rpc *rrpc;

	D_ALLOC_PTR(rrpc);
	if (rrpc == NULL)
		return NULL;
	D_INIT_LIST_HEAD(&rrpc->drc_entry);
	crt_req_addref(rpc);
	rrpc->drc_rpc = rpc;
	rdb_get(db);
	rrpc->drc_db = db;
	rrpc->drc_node = node;
	return rrpc;
}

static void
rdb_free_raft_rpc(struct rdb_raft_rpc *rrpc)
{
	rdb_put(rrpc->drc_db);
	crt_req_decref(rrpc->drc_rpc);
	D_ASSERT(d_list_empty(&rrpc->drc_entry));
	D_FREE_PTR(rrpc);
}

/* Daemon ULT for processing RPC replies */
void
rdb_recvd(void *arg)
{
	struct rdb *db = arg;

	D_DEBUG(DB_MD, DF_DB": recvd starting\n", DP_DB(db));
	for (;;) {
		struct rdb_raft_rpc    *rrpc = NULL;
		bool			stop;

		ABT_mutex_lock(db->d_mutex);
		for (;;) {
			stop = db->d_stop;
			if (!d_list_empty(&db->d_replies)) {
				rrpc = d_list_entry(db->d_replies.next,
						    struct rdb_raft_rpc,
						    drc_entry);
				d_list_del_init(&rrpc->drc_entry);
				break;
			}
			if (stop)
				break;
			ABT_cond_wait(db->d_replies_cv, db->d_mutex);
		}
		ABT_mutex_unlock(db->d_mutex);
		if (rrpc == NULL) {
			D_ASSERT(stop);
			/* The queue is empty and we are asked to stop. */
			break;
		}
		/*
		 * The queue has pending replies. If we are asked to stop, skip
		 * the processing but still free the RPCs until the queue
		 * become empty.
		 */
		if (!stop)
			rdb_raft_process_reply(db, rrpc->drc_node,
					       rrpc->drc_rpc);
		rdb_raft_free_request(db, rrpc->drc_rpc);
		rdb_free_raft_rpc(rrpc);
		ABT_thread_yield();
	}
	D_DEBUG(DB_MD, DF_DB": recvd stopping\n", DP_DB(db));
}

static void
rdb_raft_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct rdb_raft_rpc    *rrpc = cb_info->cci_arg;
	struct rdb	       *db = rrpc->drc_db;
	crt_opcode_t		opc = opc_get(cb_info->cci_rpc->cr_opc);
	int			rc = cb_info->cci_rc;

	D_DEBUG(DB_MD, DF_DB": opc=%u rank=%u rtt=%f\n", DP_DB(db), opc,
		rrpc->drc_rpc->cr_ep.ep_rank, ABT_get_wtime() - rrpc->drc_sent);
	ABT_mutex_lock(db->d_mutex);
	if (rc != 0 || db->d_stop) {
		if (rc != -DER_CANCELED)
			D_ERROR(DF_DB": RPC %x to rank %u failed: %d\n",
				DP_DB(rrpc->drc_db), opc,
				rrpc->drc_rpc->cr_ep.ep_rank, rc);
		/*
		 * Drop this RPC, assuming that raft will make a new one. If we
		 * are stopping, rdb_recvd() might have already stopped. Hence,
		 * we shall not add any new items to db->d_replies.
		 */
		d_list_del_init(&rrpc->drc_entry);
		ABT_mutex_unlock(db->d_mutex);
		rdb_raft_free_request(db, rrpc->drc_rpc);
		rdb_free_raft_rpc(rrpc);
		return;
	}
	/* Move this RPC to db->d_replies for rdb_recvd(). */
	d_list_move_tail(&rrpc->drc_entry, &db->d_replies);
	ABT_cond_broadcast(db->d_replies_cv);
	ABT_mutex_unlock(db->d_mutex);
}

int
rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db, raft_node_t *node)
{
	struct rdb_raft_rpc    *rrpc;
	int			timeout = raft_get_request_timeout(db->d_raft);
	const int		timeout_min = 1 /* s */;
	int			rc;

	rrpc = rdb_alloc_raft_rpc(db, rpc, node);
	if (rrpc == NULL)
		return -DER_NOMEM;

	ABT_mutex_lock(db->d_mutex);
	if (db->d_stop) {
		ABT_mutex_unlock(db->d_mutex);
		rdb_free_raft_rpc(rrpc);
		return -DER_CANCELED;
	}
	d_list_add_tail(&rrpc->drc_entry, &db->d_requests);
	ABT_mutex_unlock(db->d_mutex);

	timeout /= 1000; /* ms to s */
	if (timeout < timeout_min)
		timeout = timeout_min;
#if 0
	rc = crt_req_set_timeout(rpc, timeout);
	D_ASSERTF(rc == 0, "%d\n", rc);
#endif
	rrpc->drc_sent = ABT_get_wtime();

	rc = crt_req_send(rpc, rdb_raft_rpc_cb, rrpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	return 0;
}

/* Abort all in-flight RPCs. */
int
rdb_abort_raft_rpcs(struct rdb *db)
{
	struct rdb_raft_rpc    *rrpc;
	struct rdb_raft_rpc    *tmp;
	int			rc;

	d_list_for_each_entry_safe(rrpc, tmp, &db->d_requests, drc_entry) {
		d_list_del_init(&rrpc->drc_entry);
		rc = crt_req_abort(rrpc->drc_rpc);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to abort %x to rank %u: %d\n",
				DP_DB(rrpc->drc_db), rrpc->drc_rpc->cr_opc,
				rrpc->drc_rpc->cr_ep.ep_rank, rc);
			return rc;
		}
	}
	return 0;
}
