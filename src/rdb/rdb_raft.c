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
 * rdb: Raft Integration
 *
 * Each replica employs four daemon ULTs:
 *
 *   ~ rdb_timerd(): Call raft_periodic() periodically.
 *   ~ rdb_applyd(): Apply committed entries.
 *   ~ rdb_recvd(): Process RPC replies received.
 *   ~ rdb_callbackd(): Invoke user dc_step_{up,down} callbacks.
 *
 * rdb maintains its own last applied index persistently, instead of using
 * raft's volatile version.
 *
 * rdb's raft callbacks may return rdb errors (e.g., -DER_IO, -DER_NOSPACE,
 * etc.), rdb's and raft's error domains are disjoint (see the compile-time
 * assertion in rdb_raft_rc()).
 */
#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <abt.h>
#include <raft.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_server.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

/* Translate a raft error into an rdb error. */
static inline int
rdb_raft_rc(int raft_rc)
{
	/* See the file comment. */
	D_CASSERT(-DER_ERR_GURT_BASE < RAFT_ERR_LAST);
	if (raft_rc >= 0 || raft_rc < RAFT_ERR_LAST)
		return raft_rc;
	switch (raft_rc) {
	case RAFT_ERR_NOT_LEADER:		return -DER_NOTLEADER;
	case RAFT_ERR_ONE_VOTING_CHANGE_ONLY:	return -DER_BUSY;
	case RAFT_ERR_SHUTDOWN:			return -DER_SHUTDOWN;
	case RAFT_ERR_NOMEM:			return -DER_NOMEM;
	default:				return -DER_UNKNOWN;
	}
}

static int
rdb_raft_cb_send_requestvote(raft_server_t *raft, void *arg, raft_node_t *node,
			     msg_requestvote_t *msg)
{
	struct rdb		       *db = arg;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	crt_rpc_t		       *rpc;
	struct rdb_requestvote_in      *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_ANY, DF_DB": sending rv to node %d rank %u: term=%d\n",
		DP_DB(db), raft_node_get_id(node), rdb_node->dn_rank,
		msg->term);

	rc = rdb_create_raft_rpc(RDB_REQUESTVOTE, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create RV RPC to node %d: %d\n",
			DP_DB(db), raft_node_get_id(node), rc);
		return rc;
	}
	in = crt_req_get(rpc);
	uuid_copy(in->rvi_op.ri_uuid, db->d_uuid);
	in->rvi_msg = *msg;

	rc = rdb_send_raft_rpc(rpc, db, node);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send RV RPC to node %d: %d\n",
			DP_DB(db), raft_node_get_id(node), rc);
		crt_req_decref(rpc);
	}
	return rc;
}

static void
rdb_raft_fini_ae(msg_appendentries_t *ae)
{
	if (ae->entries != NULL) {
		int i;

		for (i = 0; i < ae->n_entries; i++) {
			msg_entry_t *e = &ae->entries[i];

			if (e->data.buf != NULL)
				D_FREE(e->data.buf);
		}
		D_FREE(ae->entries);
	}
}

static int
rdb_raft_clone_ae(const msg_appendentries_t *ae, msg_appendentries_t *ae_new)
{
	int i;

	*ae_new = *ae;
	ae_new->entries = NULL;
	D_ASSERTF(ae_new->n_entries >= 0, "%d\n", ae_new->n_entries);
	if (ae_new->n_entries == 0)
		return 0;

	D_ALLOC(ae_new->entries, sizeof(*ae_new->entries) * ae_new->n_entries);
	if (ae_new->entries == NULL)
		return -DER_NOMEM;
	for (i = 0; i < ae_new->n_entries; i++) {
		msg_entry_t    *e = &ae->entries[i];
		msg_entry_t    *e_new = &ae_new->entries[i];

		*e_new = *e;
		e_new->data.buf = NULL;
		if (e_new->data.len == 0)
			continue;

		D_ALLOC(e_new->data.buf, e_new->data.len);
		if (e_new->data.buf == NULL) {
			rdb_raft_fini_ae(ae_new);
			return -DER_NOMEM;
		}
		memcpy(e_new->data.buf, e->data.buf, e_new->data.len);
	}
	return 0;
}

static int
rdb_raft_cb_send_appendentries(raft_server_t *raft, void *arg,
			       raft_node_t *node, msg_appendentries_t *msg)
{
	struct rdb		       *db = arg;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	crt_rpc_t		       *rpc;
	struct rdb_appendentries_in    *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_ANY, DF_DB": sending ae to node %u rank %u: term=%d\n",
		DP_DB(db), raft_node_get_id(node), rdb_node->dn_rank,
		msg->term);

	rc = rdb_create_raft_rpc(RDB_APPENDENTRIES, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create AE RPC to node %d: %d\n",
			DP_DB(db), raft_node_get_id(node), rc);
		D_GOTO(err, rc);
	}
	in = crt_req_get(rpc);
	uuid_copy(in->aei_op.ri_uuid, db->d_uuid);
	rc = rdb_raft_clone_ae(msg, &in->aei_msg);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to allocate entry array\n", DP_DB(db));
		D_GOTO(err_rpc, rc);
	}

	rc = rdb_send_raft_rpc(rpc, db, node);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send AE RPC to node %d: %d\n",
			DP_DB(db), raft_node_get_id(node), rc);
		D_GOTO(err_in, rc);
	}
	return 0;

err_in:
	rdb_raft_fini_ae(&in->aei_msg);
err_rpc:
	crt_req_decref(rpc);
err:
	return rc;
}

static int
rdb_raft_cb_persist_vote(raft_server_t *raft, void *arg, int vote)
{
	struct rdb     *db = arg;
	daos_iov_t	value;
	int		rc;

	daos_iov_set(&value, &vote, sizeof(vote));
	rc = dbtree_update(db->d_attr, &rdb_attr_vote, &value);
	if (rc != 0)
		D_ERROR(DF_DB": failed to persist vote %d: %d\n", DP_DB(db),
			vote, rc);
	return rc;
}

static int
rdb_raft_cb_persist_term(raft_server_t *raft, void *arg, int term, int vote)
{
	struct rdb     *db = arg;
	daos_iov_t	value;
	volatile int	rc;

	TX_BEGIN(db->d_pmem) {
		/* Update rdb_attr_term. */
		daos_iov_set(&value, &term, sizeof(term));
		rc = dbtree_update(db->d_attr, &rdb_attr_term, &value);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to update term %d: %d\n",
				DP_DB(db), term, rc);
			pmemobj_tx_abort(rc);
		}
		/* Update rdb_attr_vote. */
		daos_iov_set(&value, &vote, sizeof(vote));
		rc = dbtree_update(db->d_attr, &rdb_attr_vote, &value);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to update vote for new term %d: "
				"%d\n", DP_DB(db), term, rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END
	return rc;
}

/* Persistent format of a log entry */
struct rdb_raft_entry {
	uint64_t	dre_term;
	uint64_t	dre_id;
	uint32_t	dre_type;
	uint32_t	dre_size;	/* of dre_bytes[] */
	unsigned char	dre_bytes[];
};

static inline size_t
rdb_raft_entry_buf_size(raft_entry_t *entry)
{
	return sizeof(struct rdb_raft_entry) + entry->data.len;
}

static int
rdb_raft_cb_log_offer(raft_server_t *raft, void *arg, raft_entry_t *entry,
		      int index)
{
	struct rdb	       *db = arg;
	struct rdb_raft_entry  *buf;
	size_t			buf_size = rdb_raft_entry_buf_size(entry);
	uint64_t		i = index;
	daos_iov_t		key;
	daos_iov_t		value;
	int			rc;

	/* Pack the entry into a buffer. */
	D_ALLOC(buf, buf_size);
	if (buf == NULL) {
		D_ERROR(DF_DB": failed to allocate entry buffer\n", DP_DB(db));
		return -DER_NOMEM;
	}
	buf->dre_term = entry->term;
	buf->dre_id = entry->id;
	buf->dre_type = entry->type;
	buf->dre_size = entry->data.len;
	memcpy(buf->dre_bytes, entry->data.buf, entry->data.len);

	/* Persist the buffer. */
	daos_iov_set(&key, &i, sizeof(i));
	daos_iov_set(&value, buf, buf_size);
	rc = dbtree_update(db->d_log, &key, &value);
	D_FREE(buf);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to persist entry buffer: %d\n",
			DP_DB(db), rc);
		return rc;
	}

	/* Replace the user buffer with the persistent memory address. */
	daos_iov_set(&value, NULL, 0);
	rc = dbtree_lookup(db->d_log, &key, &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to look up entry address: %d\n",
			DP_DB(db), rc);
		dbtree_delete(db->d_log, &key, NULL);
		return rc;
	}
	buf = value.iov_buf;
	entry->data.buf = buf->dre_bytes;

	D_DEBUG(DB_TRACE, DF_DB": appended entry "DF_U64": term=%d type=%d "
		"buf=%p len=%u\n", DP_DB(db), i, entry->term, entry->type,
		entry->data.buf, entry->data.len);
	return 0;
}

static int
rdb_raft_cb_log_delete(raft_server_t *raft, void *arg, raft_entry_t *entry,
		       int index)
{
	struct rdb     *db = arg;
	uint64_t	i = index;
	daos_iov_t	key;
	int		rc;

	daos_iov_set(&key, &i, sizeof(i));
	rc = dbtree_delete(db->d_log, &key, NULL);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to delete entry: %d\n", DP_DB(db), rc);
		return rc;
	}
	D_DEBUG(DB_TRACE, DF_DB": deleted entry "DF_U64": term=%d type=%d "
		"buf=%p len=%u\n", DP_DB(db), i, entry->term, entry->type,
		entry->data.buf, entry->data.len);
	return 0;
}

static void
rdb_raft_cb_debug(raft_server_t *raft, raft_node_t *node, void *arg,
		  const char *buf)
{
	struct rdb *db = raft_get_udata(raft);

	if (node != NULL) {
		struct rdb_raft_node *rdb_node = raft_node_get_udata(node);

		D_DEBUG(DB_ANY, DF_DB": %s: rank=%u\n", DP_DB(db), buf,
			rdb_node->dn_rank);
	} else {
		D_DEBUG(DB_ANY, DF_DB": %s\n", DP_DB(db), buf);
	}
}

static raft_cbs_t rdb_raft_cbs = {
	.send_requestvote	= rdb_raft_cb_send_requestvote,
	.send_appendentries	= rdb_raft_cb_send_appendentries,
	.persist_vote		= rdb_raft_cb_persist_vote,
	.persist_term		= rdb_raft_cb_persist_term,
	.log_offer		= rdb_raft_cb_log_offer,
	.log_poll		= rdb_raft_cb_log_delete,
	.log_pop		= rdb_raft_cb_log_delete,
	.log			= rdb_raft_cb_debug
};

static void
rdb_raft_queue_event(struct rdb *db, enum rdb_raft_event_type type,
		     uint64_t term)
{
	D_ASSERTF(db->d_nevents >= 0 &&
		  db->d_nevents <= ARRAY_SIZE(db->d_events),
		  "%d\n", db->d_nevents);

	if (db->d_nevents > 0) {
		struct rdb_raft_event *tail = &db->d_events[db->d_nevents - 1];

		switch (type) {
		case RDB_RAFT_STEP_UP:
			D_ASSERT(tail->dre_type == RDB_RAFT_STEP_DOWN);
			D_ASSERTF(tail->dre_term < term, DF_U64" < "DF_U64"\n",
				  tail->dre_term, term);
			break;
		case RDB_RAFT_STEP_DOWN:
			D_ASSERT(tail->dre_type == RDB_RAFT_STEP_UP);
			D_ASSERT(tail->dre_term == term);
			/*
			 * Since both of the matching events are still pending,
			 * cancel the UP and don't queue the DOWN, to avoid
			 * useless callbacks. This leaves us four possible
			 * states of the queue:
			 *
			 *   - empty
			 *   - UP(t)
			 *   - DOWN(t)
			 *   - DOWN(t), UP(t')
			 *
			 * where t' > t. The maximal queue size is therefore 2.
			 */
			db->d_nevents--;
			return;
		default:
			D_ASSERTF(0, "unknown event type: %d\n", type);
		}
	}

	/* Queue this new event. */
	D_ASSERTF(db->d_nevents < ARRAY_SIZE(db->d_events), "%d\n",
		  db->d_nevents);
	db->d_events[db->d_nevents].dre_term = term;
	db->d_events[db->d_nevents].dre_type = type;
	db->d_nevents++;
	ABT_cond_broadcast(db->d_events_cv);
}

static void
rdb_raft_dequeue_event(struct rdb *db, struct rdb_raft_event *event)
{
	D_ASSERTF(db->d_nevents > 0 &&
		  db->d_nevents <= ARRAY_SIZE(db->d_events),
		  "%d\n", db->d_nevents);
	*event = db->d_events[0];
	db->d_nevents--;
	if (db->d_nevents > 0)
		memmove(&db->d_events[0], &db->d_events[1],
			sizeof(db->d_events[0]) * db->d_nevents);
}

static void
rdb_raft_process_event(struct rdb *db, struct rdb_raft_event *event)
{
	int rc;

	switch (event->dre_type) {
	case RDB_RAFT_STEP_UP:
		if (db->d_cbs == NULL || db->d_cbs->dc_step_up == NULL)
			break;
		rc = db->d_cbs->dc_step_up(db, event->dre_term, db->d_arg);
		if (rc == 0)
			break;
		/* An error occurred. Step down if we are still that leader. */
		if (raft_is_leader(db->d_raft) &&
		    raft_get_current_term(db->d_raft) ==
		    event->dre_term) {
			D_DEBUG(DB_MD, DF_DB": stepping down from term "DF_U64
				"\n", DP_DB(db), event->dre_term);
			/* No need to generate a DOWN event. */
			raft_become_follower(db->d_raft);
		}
		/*
		 * If there are pending events, then the next one must be the
		 * matching DOWN. (See the assertions in
		 * rdb_raft_queue_event().) Discard it to reduce just a little
		 * burden on the service code.
		 */
		if (db->d_nevents > 0) {
			struct rdb_raft_event next;

			rdb_raft_dequeue_event(db, &next);
			D_ASSERTF(next.dre_type == RDB_RAFT_STEP_DOWN &&
				  next.dre_term == event->dre_term,
				  "%d "DF_U64" "DF_U64"\n", next.dre_type,
				  next.dre_term, event->dre_term);
		}
		break;
	case RDB_RAFT_STEP_DOWN:
		if (db->d_cbs == NULL || db->d_cbs->dc_step_down == NULL)
			break;
		db->d_cbs->dc_step_down(db, event->dre_term, db->d_arg);
		break;
	default:
		D_ASSERTF(0, "unknown event type: %d\n", event->dre_type);
	}
}

/* Daemon ULT for calling event callbacks */
static void
rdb_callbackd(void *arg)
{
	struct rdb *db = arg;

	D_DEBUG(DB_ANY, DF_DB": callbackd starting\n", DP_DB(db));
	for (;;) {
		struct rdb_raft_event	event;
		bool			stop;

		ABT_mutex_lock(db->d_mutex);
		for (;;) {
			stop = db->d_stop;
			if (db->d_nevents > 0) {
				rdb_raft_dequeue_event(db, &event);
				break;
			}
			if (stop)
				break;
			ABT_cond_wait(db->d_events_cv, db->d_mutex);
		}
		ABT_mutex_unlock(db->d_mutex);
		if (stop)
			break;
		rdb_raft_process_event(db, &event);
		ABT_thread_yield();
	}
	D_DEBUG(DB_ANY, DF_DB": callbackd stopping\n", DP_DB(db));
}

static int
rdb_raft_step_up(struct rdb *db, uint64_t term)
{
	msg_entry_t		mentry;
	msg_entry_response_t	mresponse;
	int			rc;

	D_WARN(DF_DB": became leader of term "DF_U64"\n", DP_DB(db), term);
	/* Commit an empty entry for an up-to-date last committed index. */
	mentry.term = raft_get_current_term(db->d_raft);
	mentry.id = 0; /* unused */
	mentry.type = RAFT_LOGTYPE_NORMAL;
	mentry.data.buf = NULL;
	mentry.data.len = 0;
	rc = raft_recv_entry(db->d_raft, &mentry, &mresponse);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to append debut entry for term "DF_U64
			": %d\n", DP_DB(db), term, rc);
		D_ASSERT(rc != RAFT_ERR_NOT_LEADER);
		return rdb_raft_rc(rc);
	}
	db->d_debut = mresponse.idx;
	rdb_raft_queue_event(db, RDB_RAFT_STEP_UP, term);
	return 0;
}

static void
rdb_raft_step_down(struct rdb *db, uint64_t term)
{
	D_WARN(DF_DB": no longer leader of term "DF_U64"\n", DP_DB(db),
	       term);
	db->d_debut = 0;
	rdb_raft_queue_event(db, RDB_RAFT_STEP_DOWN, term);
}

/* Raft state variables that rdb watches for changes */
struct rdb_raft_state {
	bool		drs_leader;
	uint64_t	drs_term;
	uint64_t	drs_committed;
};

/* Save the variables into "state". */
static void
rdb_raft_save_state(struct rdb *db, struct rdb_raft_state *state)
{
	state->drs_leader = raft_is_leader(db->d_raft);
	state->drs_term = raft_get_current_term(db->d_raft);
	state->drs_committed = raft_get_commit_idx(db->d_raft);
}

/*
 * Check the current state against "state", which shall be a previously-saved
 * state, and handle any changes and errors.
 */
static int
rdb_raft_check_state(struct rdb *db, const struct rdb_raft_state *state,
		     int raft_rc)
{
	bool		leader = raft_is_leader(db->d_raft);
	uint64_t	term = raft_get_current_term(db->d_raft);
	uint64_t	committed;
	int		step_up_rc = 0;
	int		rc;

	/* Check the leader state. */
	D_ASSERTF(term >= state->drs_term, DF_U64" >= "DF_U64"\n", term,
		  state->drs_term);
	if (!state->drs_leader && leader) {
		/* In this case, raft currently always returns zero. */
		D_ASSERTF(raft_rc == 0, "%d\n", raft_rc);
		step_up_rc = rdb_raft_step_up(db, term);
	} else if (state->drs_leader && !leader) {
		rdb_raft_step_down(db, state->drs_term);
	}

	/*
	 * Check the commit state. We query the commit index here instead of at
	 * the beginning of this function, as the rdb_raft_step_up() call above
	 * may have increased it.
	 */
	committed = raft_get_commit_idx(db->d_raft);
	D_ASSERTF(committed >= state->drs_committed, DF_U64" >= "DF_U64"\n",
		  committed, state->drs_committed);
	if (committed != state->drs_committed) {
		ABT_cond_broadcast(db->d_committed_cv);
		D_DEBUG(DB_ANY, DF_DB": committed to entry "DF_U64"\n",
			DP_DB(db), committed);
	}

	/* Check raft_rc and step_up_rc. */
	if (raft_rc == 0) {
		rc = step_up_rc;
	} else {
		D_ASSERTF(step_up_rc == 0, "%d\n", step_up_rc);
		rc = rdb_raft_rc(raft_rc);
	}
	switch (rc) {
	case -DER_NOMEM:
		if (leader) {
			raft_become_follower(db->d_raft);
			leader = false;
			/* If stepping up fails, don't step down. */
			if (step_up_rc != 0)
				break;
			rdb_raft_step_down(db, state->drs_term);
		}
		break;
	case -DER_SHUTDOWN:
	case -DER_NOSPACE:
	case -DER_IO:
		db->d_cbs->dc_stop(db, rc, db->d_arg);
		break;
	}

	/* Wake up waiters who care about term and leadership changes. */
	if (state->drs_term != term || state->drs_leader != leader)
		ABT_cond_broadcast(db->d_applied_cv);

	return rc;
}

/* Result buffer for an entry */
struct rdb_raft_result {
	d_list_t	drr_entry;
	uint64_t	drr_index;
	void	       *drr_buf;
};

static inline struct rdb_raft_result *
rdb_raft_result_obj(d_list_t *rlink)
{
	return container_of(rlink, struct rdb_raft_result, drr_entry);
}

static bool
rdb_raft_result_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
			const void *key, unsigned int ksize)
{
	struct rdb_raft_result *result = rdb_raft_result_obj(rlink);

	D_ASSERTF(ksize == sizeof(result->drr_index), "%u\n", ksize);
	return memcmp(&result->drr_index, key, sizeof(result->drr_index)) == 0;
}

static d_hash_table_ops_t rdb_raft_result_hash_ops = {
	.hop_key_cmp = rdb_raft_result_key_cmp
};

static int
rdb_raft_register_result(struct rdb *db, uint64_t index, void *buf)
{
	struct rdb_raft_result *result;
	int			rc;

	D_ALLOC_PTR(result);
	if (result == NULL)
		return -DER_NOMEM;
	result->drr_index = index;
	result->drr_buf = buf;
	rc = d_hash_rec_insert(&db->d_results, &result->drr_index,
			       sizeof(result->drr_index), &result->drr_entry,
			       true /* exclusive */);
	if (rc != 0)
		D_FREE_PTR(result);
	return rc;
}

static void *
rdb_raft_lookup_result(struct rdb *db, uint64_t index)
{
	d_list_t *entry;

	entry = d_hash_rec_find(&db->d_results, &index, sizeof(index));
	if (entry == NULL)
		return NULL;
	return rdb_raft_result_obj(entry)->drr_buf;
}

static void
rdb_raft_unregister_result(struct rdb *db, uint64_t index)
{
	struct rdb_raft_result *result;
	d_list_t	       *entry;
	bool			deleted;

	entry = d_hash_rec_find(&db->d_results, &index, sizeof(index));
	D_ASSERT(entry != NULL);
	result = rdb_raft_result_obj(entry);
	deleted = d_hash_rec_delete_at(&db->d_results, entry);
	D_ASSERT(deleted);
	D_FREE_PTR(result);
}

/* Append and wait for \a entry to be applied. */
int
rdb_raft_append_apply(struct rdb *db, void *entry, size_t size, void *result)
{
	msg_entry_t		mentry;
	msg_entry_response_t	mresponse;
	struct rdb_raft_state	state;
	int			rc;

	mentry.term = raft_get_current_term(db->d_raft);
	mentry.id = 0; /* unused */
	mentry.type = RAFT_LOGTYPE_NORMAL;
	mentry.data.buf = entry;
	mentry.data.len = size;

	rdb_raft_save_state(db, &state);
	rc = raft_recv_entry(db->d_raft, &mentry, &mresponse);
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0 && rc != -DER_NOTLEADER) {
		D_ERROR(DF_DB": failed to append entry: %d\n", DP_DB(db), rc);
		return rc;
	}

	if (result != NULL) {
		/*
		 * Since rdb_timerd() won't be scheduled until this ULT yields,
		 * we won't be racing with rdb_applyd().
		 */
		rc = rdb_raft_register_result(db, mresponse.idx, result);
		if (rc != 0)
			return rc;
	}

	rc = rdb_raft_wait_applied(db, mresponse.idx, mresponse.term);

	if (result != NULL)
		rdb_raft_unregister_result(db, mresponse.idx);
	return rc;
}

/* Verify the leadership with a quorum. */
int
rdb_raft_verify_leadership(struct rdb *db)
{
	/*
	 * raft does not provide this functionality yet; append an empty entry
	 * as a (slower) workaround.
	 */
	return rdb_raft_append_apply(db, NULL /* entry */, 0 /* size */,
				     NULL /* result */);
}

/* Apply entries up to "index". For now, one PMDK TX per entry. */
static int
rdb_apply_to(struct rdb *db, uint64_t index)
{
	while (db->d_applied < index) {
		uint64_t	i = db->d_applied + 1;
		raft_entry_t   *e;
		void	       *result;
		d_list_t	destroyed = D_LIST_HEAD_INIT(destroyed);
		int		rc;

		e = raft_get_entry_from_idx(db->d_raft, i);
		D_ASSERT(e != NULL);
		/* TODO: Revisit for configuration changes. */
		D_ASSERTF(e->type == RAFT_LOGTYPE_NORMAL, "%d\n", e->type);

		result = rdb_raft_lookup_result(db, i);

		rc = rdb_tx_apply(db, i, e->data.buf, e->data.len, result,
				  &destroyed);
		if (rc != 0) {
			/* TODO: Attempt to resolve the issue and retry. */
			D_ERROR(DF_DB": failed to apply entry "DF_U64": %d\n",
				DP_DB(db), i, rc);
			db->d_cbs->dc_stop(db, rc, db->d_arg);
			return rc;
		}

		ABT_mutex_lock(db->d_mutex);
		db->d_applied = i;
		ABT_cond_broadcast(db->d_applied_cv);
		ABT_mutex_unlock(db->d_mutex);
		D_DEBUG(DB_ANY, DF_DB": applied to entry "DF_U64"\n",
			DP_DB(db), i);
	}
	return 0;
}

/* Daemon ULT for applying committed entries */
static void
rdb_applyd(void *arg)
{
	struct rdb     *db = arg;
	int		rc;

	D_DEBUG(DB_ANY, DF_DB": applyd starting\n", DP_DB(db));
	for (;;) {
		uint64_t	committed;
		bool		stop;

		ABT_mutex_lock(db->d_mutex);
		for (;;) {
			committed = raft_get_commit_idx(db->d_raft);
			stop = db->d_stop;
			D_ASSERTF(committed == 0 || db->d_applied <= committed,
				  DF_U64" <= "DF_U64"\n", db->d_applied,
				  committed);
			if (db->d_applied < committed)
				break;
			if (stop)
				break;
			ABT_cond_wait(db->d_committed_cv, db->d_mutex);
		}
		ABT_mutex_unlock(db->d_mutex);
		if (stop)
			break;
		rc = rdb_apply_to(db, committed);
		if (rc != 0)
			break;
		ABT_thread_yield();
	}
	D_DEBUG(DB_ANY, DF_DB": applyd stopping: %d\n", DP_DB(db), rc);
}

/* Generate a random double in [0.0, 1.0]. */
static double
rdb_raft_rand(void)
{
	return (double)rand() / RAND_MAX;
}

/* Daemon ULT for raft_periodic() */
static void
rdb_timerd(void *arg)
{
	struct rdb     *db = arg;
	const double	d_min = 0.5;	/* min duration between beats (s) */
	const double	d_max = 1;	/* max duration between beats (s) */
	double		d = 0;		/* duration till next beat (s) */
	double		t;		/* timestamp of beat (s) */
	double		t_prev;		/* timestamp of previous beat (s) */
	int		rc;

	D_DEBUG(DB_ANY, DF_DB": timerd starting\n", DP_DB(db));
	t = ABT_get_wtime();
	t_prev = t;
	do {
		struct rdb_raft_state	state;
		double			d_prev = t - t_prev;

		if (d_prev - d > 1 /* s */)
			D_WARN(DF_DB": not scheduled for %f second\n",
			       DP_DB(db), d_prev - d);

		rdb_raft_save_state(db, &state);
		rc = raft_periodic(db->d_raft, d_prev * 1000 /* ms */);
		rc = rdb_raft_check_state(db, &state, rc);
		if (rc != 0)
			D_ERROR(DF_DB": raft_periodic() failed: %d\n",
				DP_DB(db), rc);

		t_prev = t;
		/* Wait for d in [d_min, d_max] before the next beat. */
		d = d_min + (d_max - d_min) * rdb_raft_rand();
		while ((t = ABT_get_wtime()) < t_prev + d && !db->d_stop)
			ABT_thread_yield();
	} while (!db->d_stop);
	D_DEBUG(DB_ANY, DF_DB": timerd stopping\n", DP_DB(db));
}

int
rdb_raft_init(daos_handle_t rdb_attr)
{
	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);
	return rdb_create_tree(rdb_attr, &rdb_attr_log, RDB_KVS_INTEGER,
			       0 /* feats */, 4 /* order */,
			       NULL /* child */);
}

/* Load an entry. */
static int
rdb_raft_log_load_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val,
		     void *varg)
{
	raft_server_t	       *raft = varg;
	uint64_t		index;
	struct rdb_raft_entry  *buf = val->iov_buf;
	size_t			buf_size;
	raft_entry_t		entry;
	int			rc;

	/* Read the key. */
	if (key->iov_len != sizeof(uint64_t)) {
		D_ERROR("invalid entry key length: "DF_U64"\n", key->iov_len);
		return -DER_IO;
	}
	index = *(uint64_t *)key->iov_buf;

	/* Read the header. */
	if (val->iov_len < sizeof(*buf)) {
		D_ERROR("truncated entry "DF_U64" header: "DF_U64" < %zu\n",
			index, val->iov_len, sizeof(*buf));
		return -DER_IO;
	}
	entry.term = buf->dre_term;
	entry.id = buf->dre_id;
	entry.type = buf->dre_type;
	entry.data.len = buf->dre_size;

	/* Prepare the buffer. */
	buf_size = rdb_raft_entry_buf_size(&entry);
	if (val->iov_len != buf_size) {
		D_ERROR("invalid entry "DF_U64" length: "DF_U64" != %zu\n",
			index, val->iov_len, buf_size);
		return -DER_IO;
	}
	entry.data.buf = buf->dre_bytes;

	/*
	 * Since rdb_raft_cbs is not registered yet, we won't enter
	 * rdb_raft_cb_log_offer().
	 */
	rc = raft_append_entry(raft, &entry);
	if (rc != 0) {
		D_ERROR("failed to load entry "DF_U64": %d\n", index, rc);
		return rdb_raft_rc(rc);
	}

	D_DEBUG(DB_TRACE, "loaded entry "DF_U64": term=%d type=%d len=%u\n",
		 index, entry.term, entry.type, entry.data.len);
	return 0;
}

/* Load the whole log. */
static int
rdb_raft_log_load(raft_server_t *raft, daos_handle_t log)
{
	return dbtree_iterate(log, 0 /* backward */, rdb_raft_log_load_cb,
			      raft);
}

static int
rdb_raft_get_election_timeout(d_rank_t self, uint8_t nreplicas)
{
	const char     *s;
	int		t;

	s = getenv("RDB_ELECTION_TIMEOUT");
	if (s == NULL)
		t = 7000;
	else
		t = atoi(s);
	return t;
}

static int
rdb_raft_get_request_timeout(void)
{
	const char     *s;
	int		t;

	s = getenv("RDB_REQUEST_TIMEOUT");
	if (s == NULL)
		t = 3000;
	else
		t = atoi(s);
	return t;
}

int
rdb_raft_start(struct rdb *db)
{
	daos_iov_t	value;
	d_rank_t	self;
	int		self_id = -1;
	int		term;
	int		vote;
	int		i;
	uint8_t		nreplicas = db->d_replicas->rl_nr;
	int		election_timeout;
	int		request_timeout;
	int		rc;

	D_INIT_LIST_HEAD(&db->d_requests);
	D_INIT_LIST_HEAD(&db->d_replies);

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 4 /* bits */,
					 NULL /* priv */,
					 &rdb_raft_result_hash_ops,
					 &db->d_results);
	if (rc != 0)
		D_GOTO(err, rc);

	daos_iov_set(&value, &db->d_applied, sizeof(db->d_applied));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_applied, &value);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(err_results, rc);

	rc = ABT_cond_create(&db->d_applied_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_results, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&db->d_committed_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_applied_cv, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&db->d_events_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_committed_cv, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&db->d_replies_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_events_cv, rc = dss_abterr2der(rc));

	db->d_raft = raft_new();
	if (db->d_raft == NULL) {
		D_ERROR("failed to create raft object\n");
		D_GOTO(err_replies_cv, rc = -DER_NOMEM);
	}

	/*
	 * Read persistent state, if any. Done before setting the callbacks in
	 * order to avoid unnecessary I/Os.
	 */
	daos_iov_set(&value, &term, sizeof(term));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_term, &value);
	if (rc == 0) {
		rc = raft_set_current_term(db->d_raft, term);
		D_ASSERTF(rc == 0, "%d\n", rc);
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(err_raft, rc);
	}
	daos_iov_set(&value, &vote, sizeof(vote));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_vote, &value);
	if (rc == 0) {
		rc = raft_vote_for_nodeid(db->d_raft, vote);
		D_ASSERTF(rc == 0, "%d\n", rc);
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(err_raft, rc);
	}
	rc = rdb_open_tree(db->d_attr, &rdb_attr_log, &db->d_log);
	if (rc != 0) {
		D_ERROR("failed to open db log tree: %d\n", rc);
		D_GOTO(err_raft, rc);
	}
	rc = rdb_raft_log_load(db->d_raft, db->d_log);
	if (rc != 0)
		D_GOTO(err_log, rc);

	/* Must be done after loading the persistent state. */
	raft_set_callbacks(db->d_raft, &rdb_raft_cbs, db);

	/* Add nodes. */
	rc = crt_group_rank(NULL, &self);
	D_ASSERTF(rc == 0, "%d\n", rc);
	for (i = 0; i < db->d_replicas->rl_nr; i++) {
		struct rdb_raft_node   *n;
		raft_node_t	       *node;
		d_rank_t		rank = db->d_replicas->rl_ranks[i];
		bool			is_self = false;

		D_ALLOC_PTR(n);
		if (n == NULL)
			D_GOTO(err_nodes, rc = -DER_NOMEM);
		n->dn_rank = rank;

		if (rank == self) {
			is_self = true;
			self_id = i;
		}

		node = raft_add_node(db->d_raft, n, i /* id */,
				     is_self /* is_self */);
		if (node == NULL) {
			D_ERROR("failed to add raft node %d\n", i);
			D_FREE_PTR(n);
			D_GOTO(err_nodes, rc = -DER_NOMEM);
		}
	}
	if (self_id == -1) {
		D_FATAL(DF_DB": rank not in replica rank list: "
			"server rank changed?\n", DP_DB(db));
		rc = -DER_OOG;
		goto err_nodes;
	}

	election_timeout = rdb_raft_get_election_timeout(self_id, nreplicas);
	request_timeout = rdb_raft_get_request_timeout();
	D_DEBUG(DB_ANY, DF_DB": election timeout %d ms\n", DP_DB(db),
		election_timeout);
	D_DEBUG(DB_ANY, DF_DB": request timeout %d ms\n", DP_DB(db),
		request_timeout);
	raft_set_election_timeout(db->d_raft, election_timeout);
	raft_set_request_timeout(db->d_raft, request_timeout);

	rc = dss_ult_create(rdb_applyd, db, -1, &db->d_applyd);
	if (rc != 0)
		D_GOTO(err_nodes, rc);
	rc = dss_ult_create(rdb_recvd, db, -1, &db->d_recvd);
	if (rc != 0)
		D_GOTO(err_applyd, rc);
	rc = dss_ult_create(rdb_timerd, db, -1, &db->d_timerd);
	if (rc != 0)
		D_GOTO(err_recvd, rc);
	rc = dss_ult_create(rdb_callbackd, db, -1, &db->d_callbackd);
	if (rc != 0)
		D_GOTO(err_timerd, rc);

	return 0;

err_timerd:
	db->d_stop = true;
	rc = ABT_thread_join(db->d_timerd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_timerd);
err_recvd:
	db->d_stop = true;
	ABT_cond_broadcast(db->d_replies_cv);
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_recvd);
err_applyd:
	db->d_stop = true;
	ABT_cond_broadcast(db->d_committed_cv);
	rc = ABT_thread_join(db->d_applyd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_applyd);
err_nodes:
	for (i -= 1; i >= 0; i--) {
		raft_node_t	       *node;
		struct rdb_raft_node   *n;

		node = raft_get_node(db->d_raft, i /* id */);
		D_ASSERT(node != NULL);
		n = raft_node_get_udata(node);
		D_ASSERT(n != NULL);
		raft_remove_node(db->d_raft, node);
		D_FREE_PTR(n);
	}
err_log:
	dbtree_close(db->d_log);
err_raft:
	raft_free(db->d_raft);
err_replies_cv:
	ABT_cond_free(&db->d_replies_cv);
err_events_cv:
	ABT_cond_free(&db->d_events_cv);
err_committed_cv:
	ABT_cond_free(&db->d_committed_cv);
err_applied_cv:
	ABT_cond_free(&db->d_applied_cv);
err_results:
	d_hash_table_destroy_inplace(&db->d_results, true /* force */);
err:
	return rc;
}

void
rdb_raft_stop(struct rdb *db)
{
	int	nreplicas = raft_get_num_nodes(db->d_raft);
	int	i;
	int	rc;

	ABT_mutex_lock(db->d_mutex);

	/* Stop sending any new RPCs. */
	db->d_stop = true;

	/* Wake up all daemons and TXs. */
	ABT_cond_broadcast(db->d_applied_cv);
	ABT_cond_broadcast(db->d_committed_cv);
	ABT_cond_broadcast(db->d_events_cv);
	ABT_cond_broadcast(db->d_replies_cv);

	/* Abort all in-flight RPCs. */
	rdb_abort_raft_rpcs(db);

	/* Wait for all extra references to be released. */
	for (;;) {
		D_ASSERTF(db->d_ref >= RDB_BASE_REFS, "%d >= %d\n", db->d_ref,
			  RDB_BASE_REFS);
		if (db->d_ref == RDB_BASE_REFS)
			break;
		D_DEBUG(DB_MD, DF_DB": waiting for %d references\n", DP_DB(db),
			db->d_ref - RDB_BASE_REFS);
		ABT_cond_wait(db->d_ref_cv, db->d_mutex);
	}

	ABT_mutex_unlock(db->d_mutex);

	/* Join and free all daemons. */
	rc = ABT_thread_join(db->d_callbackd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_callbackd);
	rc = ABT_thread_join(db->d_timerd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_timerd);
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_recvd);
	rc = ABT_thread_join(db->d_applyd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&db->d_applyd);

	/* Free the raft. */
	for (i = 0; i < nreplicas; i++) {
		raft_node_t	       *node;
		struct rdb_raft_node   *n;

		node = raft_get_node(db->d_raft, i /* id */);
		if (node == NULL)
			continue;
		n = raft_node_get_udata(node);
		D_ASSERT(n != NULL);
		raft_remove_node(db->d_raft, node);
		D_FREE_PTR(n);
	}
	raft_free(db->d_raft);

	/* Free the rest. */
	dbtree_close(db->d_log);
	ABT_cond_free(&db->d_replies_cv);
	ABT_cond_free(&db->d_events_cv);
	ABT_cond_free(&db->d_committed_cv);
	ABT_cond_free(&db->d_applied_cv);
	d_hash_table_destroy_inplace(&db->d_results, true /* force */);
}

/* Resign the leadership in term. */
void
rdb_raft_resign(struct rdb *db, uint64_t term)
{
	struct rdb_raft_state	state;
	int			rc;

	if (term != raft_get_current_term(db->d_raft) ||
	    !raft_is_leader(db->d_raft))
		return;
	rdb_raft_save_state(db, &state);
	raft_become_follower(db->d_raft);
	rc = rdb_raft_check_state(db, &state, 0 /* raft_rc */);
	D_ASSERTF(rc == 0, "%d\n", rc);
}

/* Wait for index to be applied in term. For leaders only. */
int
rdb_raft_wait_applied(struct rdb *db, uint64_t index, uint64_t term)
{
	int rc = 0;

	D_DEBUG(DB_ANY, DF_DB": waiting for entry "DF_U64" to be applied\n",
		DP_DB(db), index);
	ABT_mutex_lock(db->d_mutex);
	for (;;) {
		if (db->d_stop) {
			rc = -DER_CANCELED;
			break;
		}
		if (term != raft_get_current_term(db->d_raft) ||
		    !raft_is_leader(db->d_raft)) {
			rc = -DER_NOTLEADER;
			break;
		}
		if (index <= db->d_applied)
			break;
		ABT_cond_wait(db->d_applied_cv, db->d_mutex);
	}
	ABT_mutex_unlock(db->d_mutex);
	return rc;
}

static raft_node_t *
rdb_raft_find_node(struct rdb *db, d_rank_t rank)
{
	int i;

	if (daos_rank_list_find(db->d_replicas, rank, &i)) {
		raft_node_t *node;

		node = raft_get_node(db->d_raft, i);
		D_ASSERT(node != NULL);
		return node;
	} else {
		return NULL;
	}
}

void
rdb_requestvote_handler(crt_rpc_t *rpc)
{
	struct rdb_requestvote_in      *in = crt_req_get(rpc);
	struct rdb_requestvote_out     *out = crt_reply_get(rpc);
	struct rdb		       *db;
	raft_node_t		       *node;
	struct rdb_raft_state		state;
	int				rc;

	db = rdb_lookup(in->rvi_op.ri_uuid);
	if (db == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);
	if (db->d_stop)
		D_GOTO(out_db, rc = -DER_CANCELED);

	D_DEBUG(DB_ANY, DF_DB": handling raft rv from rank %u\n", DP_DB(db),
		rpc->cr_ep.ep_rank);
	node = rdb_raft_find_node(db, rpc->cr_ep.ep_rank);
	if (node == NULL)
		D_GOTO(out_db, rc = -DER_UNKNOWN);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_requestvote(db->d_raft, node, &in->rvi_msg,
				   &out->rvo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to process REQUESTVOTE from rank %u: "
			"%d\n", DP_DB(db), rpc->cr_ep.ep_rank, rc);
		/* raft_recv_requestvote() always generates a valid reply. */
		rc = 0;
	}

out_db:
	rdb_put(db);
out:
	out->rvo_op.ro_rc = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to send REQUESTVOTE reply to rank %u: "
			"%d\n", DP_UUID(in->rvi_op.ri_uuid), rpc->cr_ep.ep_rank,
			rc);
}

void
rdb_appendentries_handler(crt_rpc_t *rpc)
{
	struct rdb_appendentries_in    *in = crt_req_get(rpc);
	struct rdb_appendentries_out   *out = crt_reply_get(rpc);
	struct rdb		       *db;
	raft_node_t		       *node;
	struct rdb_raft_state		state;
	int				rc;

	db = rdb_lookup(in->aei_op.ri_uuid);
	if (db == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);
	if (db->d_stop)
		D_GOTO(out_db, rc = -DER_CANCELED);

	D_DEBUG(DB_ANY, DF_DB": handling raft ae from rank %u\n", DP_DB(db),
		rpc->cr_ep.ep_rank);
	node = rdb_raft_find_node(db, rpc->cr_ep.ep_rank);
	if (node == NULL)
		D_GOTO(out_db, rc = -DER_UNKNOWN);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_appendentries(db->d_raft, node, &in->aei_msg,
				     &out->aeo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to process APPENDENTRIES from rank %u: "
			"%d\n", DP_DB(db), rpc->cr_ep.ep_rank, rc);
		/* raft_recv_appendentries() always generates a valid reply. */
		rc = 0;
	}

out_db:
	rdb_put(db);
out:
	out->aeo_op.ro_rc = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to send APPENDENTRIES reply to rank "
			"%u: %d\n", DP_UUID(in->aei_op.ri_uuid),
			rpc->cr_ep.ep_rank, rc);
}

void
rdb_raft_process_reply(struct rdb *db, raft_node_t *node, crt_rpc_t *rpc)
{
	struct rdb_raft_state		state;
	crt_opcode_t			opc = opc_get(rpc->cr_opc);
	void			       *out = crt_reply_get(rpc);
	struct rdb_requestvote_out     *out_rv;
	struct rdb_appendentries_out   *out_ae;
	int				rc;

	rc = ((struct rdb_op_out *)out)->ro_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_DB": opc %u failed: %d\n", DP_DB(db), opc,
			rc);
		return;
	}

	rdb_raft_save_state(db, &state);
	switch (opc) {
	case RDB_REQUESTVOTE:
		out_rv = out;
		rc = raft_recv_requestvote_response(db->d_raft, node,
						    &out_rv->rvo_msg);
		break;
	case RDB_APPENDENTRIES:
		out_ae = out;
		rc = raft_recv_appendentries_response(db->d_raft, node,
						      &out_ae->aeo_msg);
		break;
	default:
		D_ASSERTF(0, DF_DB": unexpected opc: %u\n", DP_DB(db), opc);
	}
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0 && rc != -DER_NOTLEADER)
		D_ERROR(DF_DB": failed to process opc %u response: %d\n",
			DP_DB(db), opc, rc);
}

/* Free any additional memory we allocated for the request. */
void
rdb_raft_free_request(struct rdb *db, crt_rpc_t *rpc)
{
	crt_opcode_t			opc = opc_get(rpc->cr_opc);
	struct rdb_appendentries_in    *in_ae;

	switch (opc) {
	case RDB_REQUESTVOTE:
		/* Nothing to do. */
		break;
	case RDB_APPENDENTRIES:
		in_ae = crt_req_get(rpc);
		rdb_raft_fini_ae(&in_ae->aei_msg);
		break;
	default:
		D_ASSERTF(0, DF_DB": unexpected opc: %u\n", DP_DB(db), opc);
	}
}
