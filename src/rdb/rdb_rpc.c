/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rdb: RPCs
 */

#define D_LOGFAC DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <raft.h>
#include <daos_srv/daos_engine.h>
#include "rdb_internal.h"

static int
crt_proc_msg_requestvote_response_t(crt_proc_t proc, crt_proc_op_t proc_op,
				    msg_requestvote_response_t *p)
{
	int rc;

	rc = crt_proc_int64_t(proc, proc_op, &p->term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->vote_granted);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->prevote);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_msg_entry_t(crt_proc_t proc, crt_proc_op_t proc_op, msg_entry_t *p)
{
	int rc;

	if (FREEING(proc_op)) {
		D_FREE(p->data.buf);
		return 0;
	}

	rc = crt_proc_int64_t(proc, proc_op, &p->term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->id);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->type);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_uint32_t(proc, proc_op, &p->data.len);
	if (unlikely(rc))
		return rc;

	if (p->data.len == 0)
		return 0;

	if (DECODING(proc_op)) {
		D_ALLOC(p->data.buf, p->data.len);
		if (p->data.buf == NULL)
			return -DER_NOMEM;
	}
	rc = crt_proc_memcpy(proc, proc_op, p->data.buf, p->data.len);
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			D_FREE(p->data.buf);
		return rc;
	}

	return 0;
}

static int
crt_proc_msg_appendentries_t(crt_proc_t proc, crt_proc_op_t proc_op,
			     msg_appendentries_t *p)
{
	int		i;
	int		rc;

	rc = crt_proc_int64_t(proc, proc_op, &p->term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->prev_log_idx);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->prev_log_term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->leader_commit);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->n_entries);
	if (unlikely(rc))
		return rc;

	if (p->n_entries == 0 && proc_op != CRT_PROC_FREE)
		return 0;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(p->entries, p->n_entries);
		if (p->entries == NULL)
			return -DER_NOMEM;
	}
	for (i = 0; i < p->n_entries; i++) {
		rc = crt_proc_msg_entry_t(proc, proc_op, &p->entries[i]);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_FREE(p->entries);
			return rc;
		}
	}
	if (FREEING(proc_op))
		D_FREE(p->entries);

	return 0;
}

static int
crt_proc_msg_installsnapshot_t(crt_proc_t proc, crt_proc_op_t proc_op,
			       msg_installsnapshot_t *p)
{
	int rc;

	rc = crt_proc_int64_t(proc, proc_op, &p->term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->last_idx);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->last_term);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_msg_installsnapshot_response_t(crt_proc_t proc, crt_proc_op_t proc_op,
					msg_installsnapshot_response_t *p)
{
	int rc;

	rc = crt_proc_int64_t(proc, proc_op, &p->term);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int64_t(proc, proc_op, &p->last_idx);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_int32_t(proc, proc_op, &p->complete);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_struct_rdb_local(crt_proc_t proc, crt_proc_op_t proc_op,
			  struct rdb_local *p)
{
	/* Ignore this local field. */
	return 0;
}

CRT_RPC_DEFINE(rdb_op, DAOS_ISEQ_RDB_OP, DAOS_OSEQ_RDB_OP)

static int
crt_proc_struct_rdb_op_in(crt_proc_t proc, crt_proc_op_t proc_op,
			  struct rdb_op_in *data)
{
	return crt_proc_rdb_op_in(proc, data);
}

static int
crt_proc_struct_rdb_op_out(crt_proc_t proc, crt_proc_op_t proc_op,
			   struct rdb_op_out *data)
{
	return crt_proc_rdb_op_out(proc, data);
}

CRT_RPC_DEFINE(rdb_requestvote, DAOS_ISEQ_RDB_REQUESTVOTE,
		DAOS_OSEQ_RDB_REQUESTVOTE)
CRT_RPC_DEFINE(rdb_appendentries, DAOS_ISEQ_RDB_APPENDENTRIES,
		DAOS_OSEQ_RDB_APPENDENTRIES)
CRT_RPC_DEFINE(rdb_installsnapshot, DAOS_ISEQ_RDB_INSTALLSNAPSHOT,
		DAOS_OSEQ_RDB_INSTALLSNAPSHOT)

/* Define for cont_rpcs[] array population below.
 * See RDB_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format rdb_proto_rpc_fmt[] = {
	RDB_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format rdb_proto_fmt = {
	.cpf_name  = "rdb-proto",
	.cpf_ver   = DAOS_RDB_VERSION,
	.cpf_count = ARRAY_SIZE(rdb_proto_rpc_fmt),
	.cpf_prf   = rdb_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_RDB_MODULE, 0)
};

int
rdb_create_raft_rpc(crt_opcode_t opc, raft_node_t *node, crt_rpc_t **rpc)
{
	crt_opcode_t		opc_full;
	crt_endpoint_t		ep;
	struct dss_module_info *info = dss_get_module_info();

	opc_full = DAOS_RPC_OPCODE(opc, DAOS_RDB_MODULE, DAOS_RDB_VERSION);
	ep.ep_grp = NULL;
	ep.ep_rank = raft_node_get_id(node);
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_RDB, 0);
	return crt_req_create(info->dmi_ctx, &ep, opc_full, rpc);
}

struct rdb_raft_rpc {
	d_list_t	drc_entry;	/* in rdb::{d_requests,d_replies} */
	crt_rpc_t      *drc_rpc;
	struct rdb     *drc_db;
	double		drc_sent;
};

static struct rdb_raft_rpc *
rdb_alloc_raft_rpc(struct rdb *db, crt_rpc_t *rpc)
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
	return rrpc;
}

static void
rdb_free_raft_rpc(struct rdb_raft_rpc *rrpc)
{
	rdb_put(rrpc->drc_db);
	crt_req_decref(rrpc->drc_rpc);
	D_ASSERT(d_list_empty(&rrpc->drc_entry));
	D_FREE(rrpc);
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
			sched_cond_wait(db->d_replies_cv, db->d_mutex);
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
			rdb_raft_process_reply(db, rrpc->drc_rpc);
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
	d_rank_t		dstrank;
	int			rc;

	rc = crt_req_dst_rank_get(rrpc->drc_rpc, &dstrank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	rc = cb_info->cci_rc;
	D_DEBUG(DB_MD, DF_DB": opc=%u rank=%u rtt=%f\n", DP_DB(db), opc,
		dstrank, ABT_get_wtime() - rrpc->drc_sent);
	ABT_mutex_lock(db->d_mutex);
	if (rc != 0 || db->d_stop) {
		if (rc != -DER_CANCELED)
			D_ERROR(DF_DB": RPC %x to rank %u failed: "DF_RC"\n",
				DP_DB(rrpc->drc_db), opc,
				dstrank, DP_RC(rc));
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
	DABT_COND_BROADCAST(db->d_replies_cv);
	ABT_mutex_unlock(db->d_mutex);
}

/* Caller holds d_raft_mutex lock */
int
rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db)
{
	struct rdb_raft_rpc    *rrpc;
	int			timeout = raft_get_request_timeout(db->d_raft);
	const int		timeout_min = 1 /* s */;
	int			rc;

	rrpc = rdb_alloc_raft_rpc(db, rpc);
	if (rrpc == NULL)
		return -DER_NOMEM;

	if (db->d_stop) {
		rdb_free_raft_rpc(rrpc);
		return -DER_CANCELED;
	}

	ABT_mutex_lock(db->d_mutex);
	d_list_add_tail(&rrpc->drc_entry, &db->d_requests);
	ABT_mutex_unlock(db->d_mutex);

	timeout /= 1000; /* ms to s */
	if (timeout < timeout_min)
		timeout = timeout_min;
#if 0
	rc = crt_req_set_timeout(rpc, timeout);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
#endif
	rrpc->drc_sent = ABT_get_wtime();

	rc = crt_req_send(rpc, rdb_raft_rpc_cb, rrpc);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
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
			d_rank_t	dstrank;
			int		rc2;

			rc2 = crt_req_dst_rank_get(rrpc->drc_rpc, &dstrank);
			D_ASSERTF(rc2 == 0, ""DF_RC"\n", DP_RC(rc2));
			D_ERROR(DF_DB": failed to abort %x to rank %u: "
				""DF_RC"\n", DP_DB(rrpc->drc_db),
				rrpc->drc_rpc->cr_opc, dstrank, DP_RC(rc));
			return rc;
		}
	}
	return 0;
}
