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
 * Each replica employs two daemon ULTs:
 *
 *   ~ rdb_timerd(): Call raft_periodic() periodically.
 *   ~ rdb_applyd(): Apply committed entries.
 *
 * rdb maintains its own last applied index persistently, instead of using
 * raft's volatile version.
 */

#define DD_SUBSYS DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <abt.h>
#include <raft.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_server.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

static int
rdb_raft_cb_send_requestvote(raft_server_t *raft, void *arg, raft_node_t *node,
			     msg_requestvote_t *msg)
{
	struct rdb	       *db = arg;
	crt_rpc_t	       *rpc;
	msg_requestvote_t      *in;
	int			rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_ANY, DF_DB": sending raft rv to node %d: term=%d\n",
		DP_DB(db), raft_node_get_id(node), msg->term);

	rc = rdb_create_raft_rpc(RDB_REQUESTVOTE, node, &rpc);
	if (rc != 0)
		return -1;
	in = crt_req_get(rpc);
	*in = *msg;

	rc = rdb_send_raft_rpc(rpc, db, node);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send RV to node %d: term=%d\n",
			DP_DB(db), raft_node_get_id(node), msg->term);
		crt_req_decref(rpc);
		return -1;
	}
	return 0;
}

static int
rdb_raft_cb_send_appendentries(raft_server_t *raft, void *arg,
			       raft_node_t *node, msg_appendentries_t *msg)
{
	struct rdb	       *db = arg;
	struct rdb_raft_node   *rdb_node = raft_node_get_udata(node);
	crt_rpc_t	       *rpc;
	msg_appendentries_t    *in;
	int			rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_ANY, DF_DB": sending ae to %u: term=%d\n", DP_DB(db),
		rdb_node->dn_rank, msg->term);

	rc = rdb_create_raft_rpc(RDB_APPENDENTRIES, node, &rpc);
	if (rc != 0)
		return -1;
	in = crt_req_get(rpc);
	*in = *msg;

	rc = rdb_send_raft_rpc(rpc, db, node);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send AE to node %d: term=%d\n",
			DP_DB(db), raft_node_get_id(node), msg->term);
		crt_req_decref(rpc);
		return -1;
	}
	return 0;
}

static int
rdb_raft_cb_persist_vote(raft_server_t *raft, void *arg, int vote)
{
	struct rdb     *db = arg;
	daos_iov_t	value;

	daos_iov_set(&value, &vote, sizeof(vote));
	return dbtree_update(db->d_attr, &rdb_attr_vote, &value);
}

static int
rdb_raft_cb_persist_term(raft_server_t *raft, void *arg, int term)
{
	struct rdb     *db = arg;
	daos_iov_t	value;

	daos_iov_set(&value, &term, sizeof(term));
	return dbtree_update(db->d_attr, &rdb_attr_term, &value);
}

struct rdb_raft_entry {
	uint64_t	dre_term;
	uint64_t	dre_id;
	uint32_t	dre_type;
	uint32_t	dre_size;	/* of dre_bytes[] */
	unsigned char	dre_bytes[];
};

static inline struct rdb_raft_entry *
rdb_raft_entry_buf(raft_entry_t *entry)
{
	return container_of(entry->data.buf, struct rdb_raft_entry, dre_bytes);
}

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
	if (buf == NULL)
		return -1;
	buf->dre_term = entry->term;
	buf->dre_id = entry->id;
	buf->dre_type = entry->type;
	buf->dre_size = entry->data.len;
	/* TODO: Eliminate this memcpy() call. */
	memcpy(buf->dre_bytes, entry->data.buf, entry->data.len);

	/* Persist the buffer. */
	daos_iov_set(&key, &i, sizeof(i));
	daos_iov_set(&value, buf, buf_size);
	rc = dbtree_update(db->d_log, &key, &value);
	D_FREE(buf, buf_size);
	if (rc != 0)
		return -1;
	return 0;
}

static int
rdb_raft_cb_log_delete(raft_server_t *raft, void *arg, raft_entry_t *entry,
		       int index)
{
	struct rdb	       *db = arg;
	uint64_t		i = index;
	daos_iov_t		key;
	struct rdb_raft_entry  *e = rdb_raft_entry_buf(entry);
	size_t			size = rdb_raft_entry_buf_size(entry);
	int			rc;

	daos_iov_set(&key, &i, sizeof(i));
	rc = dbtree_delete(db->d_log, &key, NULL);
	if (rc != 0)
		return -1;
	D_FREE(e, size);
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

/* raft state variables that rdb watches for changes */
struct rdb_raft_state {
	bool		drs_leader;
	uint64_t	drs_term;
	uint64_t	drs_committed;
};

static void rdb_raft_save_state(struct rdb *db, struct rdb_raft_state *state);
static void rdb_raft_check_state(struct rdb *db,
				 const struct rdb_raft_state *state);

static void
rdb_raft_step_up(struct rdb *db, uint64_t term)
{
	msg_entry_t		mentry;
	msg_entry_response_t	mresponse;
	struct rdb_raft_state	state;
	int			rc;

	D_WARN(DF_DB": became leader of term "DF_U64"\n", DP_DB(db), term);
	/* Commit an empty entry for an up-to-date last committed index. */
	mentry.term = raft_get_current_term(db->d_raft);
	mentry.id = 0; /* unused */
	mentry.type = RAFT_LOGTYPE_NORMAL;
	mentry.data.buf = NULL;
	mentry.data.len = 0;
	rdb_raft_save_state(db, &state);
	rc = raft_recv_entry(db->d_raft, &mentry, &mresponse);
	/* TODO: Handle errors. */
	D_ASSERTF(rc == 0, "%d\n", rc);
	/*
	 * Since raft_recv_entry() doesn't change the leader state, and this
	 * replica is already a leader, this rdb_raft_check_state() call won't
	 * trigger a recursive rdb_raft_step_up() call.
	 */
	rdb_raft_check_state(db, &state);
	db->d_debut = mresponse.idx;
	if (db->d_cbs != NULL && db->d_cbs->dc_step_up != NULL)
		db->d_cbs->dc_step_up(db, term, db->d_arg);
}

static void
rdb_raft_step_down(struct rdb *db, uint64_t term)
{
	D_WARN(DF_DB": no longer leader of term "DF_U64"\n", DP_DB(db),
	       term);
	db->d_debut = 0;
	if (db->d_cbs != NULL && db->d_cbs->dc_step_down != NULL)
		db->d_cbs->dc_step_down(db, term, db->d_arg);
}

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
 * state, and handle any changes.
 */
static void
rdb_raft_check_state(struct rdb *db, const struct rdb_raft_state *state)
{
	bool		leader = raft_is_leader(db->d_raft);
	uint64_t	term =  raft_get_current_term(db->d_raft);
	uint64_t	committed = raft_get_commit_idx(db->d_raft);

	/* Check the leader state. */
	D_ASSERTF(term >= state->drs_term, DF_U64" >= "DF_U64"\n", term,
		  state->drs_term);
	if (!state->drs_leader && leader)
		rdb_raft_step_up(db, term);
	else if (state->drs_leader && !leader)
		rdb_raft_step_down(db, state->drs_term);

	/* Check the commit state. */
	D_ASSERTF(committed >= state->drs_committed, DF_U64" >= "DF_U64"\n",
		  committed, state->drs_committed);
	if (committed != state->drs_committed) {
		ABT_cond_broadcast(db->d_committed_cv);
		D_DEBUG(DB_ANY, DF_DB": committed to entry "DF_U64"\n",
			DP_DB(db), committed);
	}
}

/* Result buffer for an entry */
struct rdb_raft_result {
	daos_list_t	drr_entry;
	uint64_t	drr_index;
	void	       *drr_buf;
};

static inline struct rdb_raft_result *
rdb_raft_result_obj(daos_list_t *rlink)
{
	return container_of(rlink, struct rdb_raft_result, drr_entry);
}

static bool
rdb_raft_result_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
			const void *key, unsigned int ksize)
{
	struct rdb_raft_result *result = rdb_raft_result_obj(rlink);

	D_ASSERTF(ksize == sizeof(result->drr_index), "%u\n", ksize);
	return memcmp(&result->drr_index, key, sizeof(result->drr_index)) == 0;
}

static dhash_table_ops_t rdb_raft_result_hash_ops = {
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
	rc = dhash_rec_insert(&db->d_results, &result->drr_index,
			      sizeof(result->drr_index), &result->drr_entry,
			      true /* exclusive */);
	if (rc != 0)
		D_FREE_PTR(result);
	return rc;
}

static void *
rdb_raft_lookup_result(struct rdb *db, uint64_t index)
{
	daos_list_t *entry;

	entry = dhash_rec_find(&db->d_results, &index, sizeof(index));
	if (entry == NULL)
		return NULL;
	return rdb_raft_result_obj(entry)->drr_buf;
}

static void
rdb_raft_unregister_result(struct rdb *db, uint64_t index)
{
	struct rdb_raft_result *result;
	daos_list_t	       *entry;
	bool			deleted;

	entry = dhash_rec_find(&db->d_results, &index, sizeof(index));
	D_ASSERT(entry != NULL);
	result = rdb_raft_result_obj(entry);
	deleted = dhash_rec_delete_at(&db->d_results, entry);
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
	rdb_raft_check_state(db, &state);
	if (rc == RAFT_ERR_NOT_LEADER)
		return -DER_NOTLEADER;
	else if (rc != 0)
		return -DER_IO;

	if (result != NULL) {
		/*
		 * Since rdb_timerd() won't be scheduled until this ULT yields,
		 * we won't be racing with rdb_applyd().
		 */
		rc = rdb_raft_register_result(db, mresponse.idx, result);
		if (rc != 0)
			return rc;
	}

	rc = rdb_raft_wait_applied(db, mresponse.idx);

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

/* Apply entries up to "index". For now, one NVML TX per entry. */
static void
rdb_apply_to(struct rdb *db, uint64_t index)
{
	while (db->d_applied < index) {
		uint64_t	i = db->d_applied + 1;
		raft_entry_t   *e;
		void	       *result;
		daos_list_t	destroyed = DAOS_LIST_HEAD_INIT(destroyed);
		int		rc;

		e = raft_get_entry_from_idx(db->d_raft, i);
		D_ASSERT(e != NULL);
		/* TODO: Revisit for configuration changes. */
		D_ASSERTF(e->type == RAFT_LOGTYPE_NORMAL, "%d\n", e->type);

		result = rdb_raft_lookup_result(db, i);

		rc = rdb_tx_apply(db, i, e->data.buf, e->data.len, result,
				  &destroyed);
		if (rc != 0)
			/*
			 * TODO: Try to resolve the issue or stop this replica
			 * eventually.
			 */
			break;

		ABT_mutex_lock(db->d_mutex);
		db->d_applied = i;
		ABT_cond_broadcast(db->d_applied_cv);
		ABT_mutex_unlock(db->d_mutex);
		D_DEBUG(DB_ANY, DF_DB": applied to entry "DF_U64"\n",
			DP_DB(db), i);
	}
}

/* Daemon ULT for applying committed entries */
static void
rdb_applyd(void *arg)
{
	struct rdb *db = arg;

	D_DEBUG(DB_ANY, DF_DB": applyd starting\n", DP_DB(db));
	for (;;) {
		uint64_t	committed;
		bool		stop;

		ABT_mutex_lock(db->d_mutex);
		for (;;) {
			committed = raft_get_commit_idx(db->d_raft);
			stop = db->d_applyd_stop;
			D_ASSERTF(db->d_applied <= committed,
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
		rdb_apply_to(db, committed);
		ABT_thread_yield();
	}
	D_DEBUG(DB_ANY, DF_DB": applyd stopping\n", DP_DB(db));
}

/* Daemon ULT for raft_periodic() */
static void
rdb_timerd(void *arg)
{
	struct rdb     *db = arg;
	const double	period = 1;	/* duration between beats (s) */
	double		t;		/* timestamp of beat (s) */
	double		t_prev;		/* timestamp of previous beat (s) */
	int		rc;

	D_DEBUG(DB_ANY, DF_DB": timerd starting\n", DP_DB(db));
	t = ABT_get_wtime();
	t_prev = t;
	do {
		struct rdb_raft_state state;

		rdb_raft_save_state(db, &state);
		rc = raft_periodic(db->d_raft, (t - t_prev) * 1000 /* ms */);
		D_ASSERTF(rc == 0, "%d\n", rc);
		rdb_raft_check_state(db, &state);

		t_prev = t;
		/* Wait for the next beat. */
		while ((t = ABT_get_wtime()) < t_prev + period &&
		       !db->d_timerd_stop)
			ABT_thread_yield();
	} while (!db->d_timerd_stop);
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
rdb_raft_log_load_cb(daos_iov_t *key, daos_iov_t *val, void *varg)
{
	raft_server_t	       *raft = varg;
	struct rdb_raft_entry  *buf = val->iov_buf;
	size_t			buf_size;
	raft_entry_t		entry;
	int			rc;

	/* Read the header. */
	if (key->iov_len != sizeof(uint64_t) ||
	    val->iov_len < sizeof(*buf))
		return -DER_IO;
	entry.term = buf->dre_term;
	entry.id = buf->dre_id;
	entry.type = buf->dre_type;
	entry.data.len = buf->dre_size;

	/* Prepare the buffer and read the bytes. */
	/* TODO: No chance to free this buffer during raft_clear()! */
	buf_size = rdb_raft_entry_buf_size(&entry);
	if (val->iov_len != buf_size)
		return -DER_IO;
	D_ALLOC(entry.data.buf, buf_size);
	if (entry.data.buf == NULL)
		return -DER_NOMEM;
	memcpy(entry.data.buf, buf, buf_size);

	rc = raft_append_entry(raft, &entry);
	if (rc != 0) {
		D_FREE(entry.data.buf, buf_size);
		return -DER_NOMEM;
	}

	return 0;
}

/* Load the whole log. */
static int
rdb_raft_log_load(raft_server_t *raft, daos_handle_t log)
{
	return dbtree_iterate(log, 0 /* backward */, rdb_raft_log_load_cb,
			      raft);
}

/* TODO: Implement a true random algorithm. */
static int
rdb_raft_rand(int low, int high, crt_rank_t rank, uint8_t nreplicas)
{
	return low + (high - low) / nreplicas * rank;
}

int
rdb_raft_start(struct rdb *db, const daos_rank_list_t *replicas)
{
	daos_iov_t	value;
	crt_rank_t	self;
	int		term;
	int		vote;
	int		i;
	int		election_timeout;
	int		request_timeout;
	int		rc;

	rc = dhash_table_create_inplace(DHASH_FT_NOLOCK, 4 /* bits */,
					NULL /* priv */,
					&rdb_raft_result_hash_ops,
					&db->d_results);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = ABT_mutex_create(&db->d_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_results, rc = dss_abterr2der(rc));

	daos_iov_set(&value, &db->d_applied, sizeof(db->d_applied));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_applied, &value);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(err_mutex, rc);

	rc = ABT_cond_create(&db->d_applied_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_mutex, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&db->d_committed_cv);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_applied_cv, rc = dss_abterr2der(rc));

	db->d_raft = raft_new();
	if (db->d_raft == NULL) {
		D_ERROR("failed to create raft object\n");
		D_GOTO(err_committed_cv, rc = -DER_NOMEM);
	}

	/*
	 * Read persistent state, if any. Done before setting the callbacks in
	 * order to avoid unnecessary I/Os.
	 */
	daos_iov_set(&value, &term, sizeof(term));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_term, &value);
	if (rc == 0)
		raft_set_current_term(db->d_raft, term);
	else if (rc != -DER_NONEXIST)
		D_GOTO(err_raft, rc);
	daos_iov_set(&value, &vote, sizeof(vote));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_vote, &value);
	if (rc == 0)
		raft_vote_for_nodeid(db->d_raft, vote);
	else if (rc != -DER_NONEXIST)
		D_GOTO(err_raft, rc);
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
	for (i = 0; i < replicas->rl_nr.num; i++) {
		struct rdb_raft_node   *n;
		raft_node_t	       *node;
		crt_rank_t		rank = replicas->rl_ranks[i];

		D_ALLOC_PTR(n);
		if (n == NULL)
			D_GOTO(err_nodes, rc = -DER_NOMEM);
		n->dn_rank = rank;
		node = raft_add_node(db->d_raft, n, i + 1 /* id */,
				     rank == self /* is_self */);
		if (node == NULL) {
			D_ERROR("failed to add raft node %d\n", i + 1);
			D_FREE_PTR(n);
			D_GOTO(err_nodes, rc = -DER_NOMEM);
		}
	}

	election_timeout = rdb_raft_rand(8 * 1000, 12 * 1000, self,
					 replicas->rl_nr.num);
	request_timeout = 3 * 1000;
	D_DEBUG(DB_ANY, DF_DB": election timeout %d ms\n", DP_DB(db),
		election_timeout);
	raft_set_election_timeout(db->d_raft, election_timeout);
	raft_set_request_timeout(db->d_raft, request_timeout);

	rc = dss_create_ult(rdb_applyd, db, &db->d_applyd);
	if (rc != 0)
		D_GOTO(err_nodes, rc);
	rc = dss_create_ult(rdb_recvd, db, &db->d_recvd);
	if (rc != 0)
		D_GOTO(err_applyd, rc);
	rc = dss_create_ult(rdb_timerd, db, &db->d_timerd);
	if (rc != 0)
		D_GOTO(err_recvd, rc);

	return 0;

err_recvd:
	db->d_recvd_stop = true;
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(db->d_recvd);
err_applyd:
	db->d_applyd_stop = true;
	ABT_cond_signal(db->d_committed_cv);
	rc = ABT_thread_join(db->d_applyd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(db->d_applyd);
err_nodes:
	for (i -= 1; i >= 0; i--) {
		raft_node_t	       *node;
		struct rdb_raft_node   *n;

		node = raft_get_node(db->d_raft, i + 1 /* id */);
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
err_committed_cv:
	ABT_cond_free(&db->d_committed_cv);
err_applied_cv:
	ABT_cond_free(&db->d_applied_cv);
err_mutex:
	ABT_mutex_free(&db->d_mutex);
err_results:
	dhash_table_destroy_inplace(&db->d_results, true /* force */);
err:
	return rc;
}

/* TODO: This is nothing more than a hack for the internal demo... */
void
rdb_raft_stop(struct rdb *db)
{
	int	nreplicas = raft_get_num_nodes(db->d_raft);
	int	i;
	int	rc;

	db->d_recvd_stop = true;
	db->d_timerd_stop = true;
	db->d_applyd_stop = true;
	ABT_cond_broadcast(db->d_applied_cv);
	ABT_cond_broadcast(db->d_committed_cv);
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	rc = ABT_thread_join(db->d_timerd);
	D_ASSERTF(rc == 0, "%d\n", rc);
	rc = ABT_thread_join(db->d_applyd);
	D_ASSERTF(rc == 0, "%d\n", rc);

	for (i = 0; i < nreplicas; i++) {
		crt_endpoint_t ep = {NULL, i, 0};

		rc = crt_ep_abort(ep);
		D_ASSERTF(rc == 0, "%d\n", rc);
	}

	for (i = 0; i < nreplicas; i++) {
		raft_node_t	       *node;
		struct rdb_raft_node   *n;

		node = raft_get_node(db->d_raft, i + 1 /* id */);
		if (node == NULL)
			continue;
		n = raft_node_get_udata(node);
		D_ASSERT(n != NULL);
		raft_remove_node(db->d_raft, node);
		D_FREE_PTR(n);
	}
	dbtree_close(db->d_log);
	raft_free(db->d_raft);
	ABT_cond_free(&db->d_committed_cv);
	ABT_cond_free(&db->d_applied_cv);
	ABT_mutex_free(&db->d_mutex);
	dhash_table_destroy_inplace(&db->d_results, true /* force */);
}

/* Wait for index to be applied. */
int
rdb_raft_wait_applied(struct rdb *db, uint64_t index)
{
	int rc = 0;

	D_DEBUG(DB_ANY, DF_DB": waiting for entry "DF_U64" to be applied\n",
		DP_DB(db), index);
	ABT_mutex_lock(db->d_mutex);
	for (;;) {
		if (!raft_is_leader(db->d_raft)) {
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

int
rdb_requestvote_handler(crt_rpc_t *rpc)
{
	msg_requestvote_t	       *in = crt_req_get(rpc);
	msg_requestvote_response_t     *out = crt_reply_get(rpc);
	struct rdb		       *db = the_one_rdb_hack;
	raft_node_t		       *node;
	struct rdb_raft_state		state;

	if (db == NULL)
		/* Drop the RPC. */
		return -DER_NONEXIST;
	D_DEBUG(DB_ANY, DF_DB": handling raft rv\n", DP_DB(db));
	node = raft_get_node(db->d_raft, rpc->cr_ep.ep_rank + 1);
	D_ASSERT(node != NULL);
	rdb_raft_save_state(db, &state);
	raft_recv_requestvote(db->d_raft, node, in, out);
	rdb_raft_check_state(db, &state);
	return crt_reply_send(rpc);
}

int
rdb_appendentries_handler(crt_rpc_t *rpc)
{
	msg_appendentries_t	       *in = crt_req_get(rpc);
	msg_appendentries_response_t   *out = crt_reply_get(rpc);
	struct rdb		       *db = the_one_rdb_hack;
	raft_node_t		       *node;
	struct rdb_raft_state		state;

	if (db == NULL)
		/* Drop the RPC. */
		return -DER_NONEXIST;
	D_DEBUG(DB_ANY, DF_DB": handling raft ae\n", DP_DB(db));
	node = raft_get_node(db->d_raft, rpc->cr_ep.ep_rank + 1);
	D_ASSERT(node != NULL);
	rdb_raft_save_state(db, &state);
	raft_recv_appendentries(db->d_raft, node, in, out);
	rdb_raft_check_state(db, &state);
	return crt_reply_send(rpc);
}

void
rdb_raft_process_reply(struct rdb *db, raft_node_t *node, crt_rpc_t *rpc)
{
	struct rdb_raft_state	state;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	void		       *out = crt_reply_get(rpc);
	int			rc;

	rdb_raft_save_state(db, &state);
	switch (opc) {
	case RDB_REQUESTVOTE:
		rc = raft_recv_requestvote_response(db->d_raft, node, out);
		break;
	case RDB_APPENDENTRIES:
		rc = raft_recv_appendentries_response(db->d_raft, node, out);
		break;
	default:
		D_ASSERTF(0, "unexpected opc: %u\n", opc);
	}
	if (rc != 0)
		D_ERROR(DF_DB": failed to process opc %u response: %d\n",
			DP_DB(db), opc, rc);
	rdb_raft_check_state(db, &state);
}
