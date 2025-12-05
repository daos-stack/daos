/*
 * (C) Copyright 2017-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rdb: Raft Integration
 *
 * Each replica employs four or five daemon ULTs:
 *
 *   ~ rdb_timerd(): Call raft_periodic() periodically.
 *   ~ rdb_recvd(): Process RPC replies received.
 *   ~ rdb_callbackd(): Invoke user dc_step_{up,down} callbacks.
 *   ~ rdb_compactd(): Compact polled entries by calling rdb_lc_aggregate().
 *   ~ rdb_checkpointd(): Checkpoint RDB pool (MD on SSD only).
 *
 * rdb uses its own last applied index, which always equal to the last
 * committed index, instead of using raft's version.
 *
 * rdb's raft callbacks may return rdb errors (e.g., -DER_IO, -DER_NOSPACE,
 * etc.), rdb's and raft's error domains are disjoint (see the compile-time
 * assertion in rdb_raft_rc()).
 */

#define D_LOGFAC DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <abt.h>
#include <raft.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include <daos_srv/object.h>
#include <daos/object.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

static int rdb_raft_create_lc(daos_handle_t pool, daos_handle_t mc,
			      d_iov_t *key, uint64_t base,
			      uint64_t base_term, uint64_t term,
			      struct rdb_lc_record *record);
static int rdb_raft_destroy_lc(daos_handle_t pool, daos_handle_t mc,
			       d_iov_t *key, uuid_t uuid,
			       struct rdb_lc_record *record);
static void *rdb_raft_lookup_result(struct rdb *db, uint64_t index);

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
	case RAFT_ERR_SNAPSHOT_ALREADY_LOADED:	return -DER_ALREADY;
	case RAFT_ERR_INVALID_CFG_CHANGE:	return -DER_INVAL;
	case RAFT_ERR_MIGHT_VIOLATE_LEASE:	return -DER_NO_PERM;
	default:				return -DER_MISC;
	}
}

static char *
rdb_raft_entry_type_str(int type)
{
	switch (type) {
	case RAFT_LOGTYPE_NORMAL:			return "normal";
	case RAFT_LOGTYPE_ADD_NODE:			return "add-voting-node";
	case RAFT_LOGTYPE_ADD_NONVOTING_NODE:		return "add-nonvoting-node";
	case RAFT_LOGTYPE_PROMOTE_NODE:			return "promote-node";
	case RAFT_LOGTYPE_DEMOTE_NODE:			return "demote-node";
	case RAFT_LOGTYPE_REMOVE_NONVOTING_NODE:	return "remove-nonvoting-node";
	case RAFT_LOGTYPE_REMOVE_NODE:			return "remove-voting-node";
	default:					return "?";
	}
}

static int
rdb_raft_cb_send_requestvote(raft_server_t *raft, void *arg, raft_node_t *node,
			     msg_requestvote_t *msg)
{
	struct rdb		       *db = arg;
	rdb_replica_id_t                rdb_node_id;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	char			       *s = msg->prevote ? " (prevote)" : "";
	crt_rpc_t		       *rpc;
	struct rdb_requestvote_in      *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_ASSERT(node != NULL);
	D_ASSERT(rdb_node != NULL);
	rdb_node_id = rdb_replica_id_decode(raft_node_get_id(node));
	D_DEBUG(DB_TRACE, DF_DB ": sending rv%s to node " RDB_F_RID " rank %u: term=%ld\n",
		DP_DB(db), s, RDB_P_RID(rdb_node_id), rdb_node->dn_rank, msg->term);

	rc = rdb_create_raft_rpc(db, RDB_REQUESTVOTE, node, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to create RV%s RPC to node " RDB_F_RID, DP_DB(db), s,
			 RDB_P_RID(rdb_node_id));
		return rc;
	}
	in          = crt_req_get(rpc);
	in->rvi_msg = *msg;

	rc = rdb_send_raft_rpc(rpc, db);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to send RV%s RPC to node " RDB_F_RID, DP_DB(db), s,
			 RDB_P_RID(rdb_node_id));
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

			D_FREE(e->data.buf);
		}
		D_FREE(ae->entries);
	}
}

static int
rdb_raft_clone_ae(struct rdb *db, const msg_appendentries_t *ae, msg_appendentries_t *ae_new)
{
	size_t	size = 0;
	int	i;

	*ae_new = *ae;
	ae_new->entries = NULL;
	D_ASSERTF(ae_new->n_entries >= 0, "%d\n", ae_new->n_entries);
	if (ae_new->n_entries == 0)
		return 0;
	else if (ae_new->n_entries > db->d_ae_max_entries)
		ae_new->n_entries = db->d_ae_max_entries;

	D_ALLOC_ARRAY(ae_new->entries, ae_new->n_entries);
	if (ae_new->entries == NULL)
		return -DER_NOMEM;
	for (i = 0; i < ae_new->n_entries; i++) {
		msg_entry_t    *e = &ae->entries[i];
		msg_entry_t    *e_new = &ae_new->entries[i];

		*e_new = *e;
		e_new->data.buf = NULL;
		if (e_new->data.len == 0) {
			continue;
		} else if (i > 0 && size + e_new->data.len > db->d_ae_max_size) {
			/*
			 * If this is not the first entry, and we are going to
			 * exceed the size limit, then stop and return what we
			 * have cloned. If this _is_ the first entry, we have
			 * to ignore the size limit in order to make progress.
			 */
			ae_new->n_entries = i;
			break;
		}

		D_ALLOC(e_new->data.buf, e_new->data.len);
		if (e_new->data.buf == NULL) {
			rdb_raft_fini_ae(ae_new);
			return -DER_NOMEM;
		}
		memcpy(e_new->data.buf, e->data.buf, e_new->data.len);
		size += e_new->data.len;
	}
	return 0;
}

static int
rdb_raft_cb_send_appendentries(raft_server_t *raft, void *arg,
			       raft_node_t *node, msg_appendentries_t *msg)
{
	struct rdb		       *db = arg;
	rdb_replica_id_t                rdb_node_id;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	crt_rpc_t		       *rpc;
	struct rdb_appendentries_in    *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_ASSERT(node != NULL);
	D_ASSERT(rdb_node != NULL);
	rdb_node_id = rdb_replica_id_decode(raft_node_get_id(node));
	D_DEBUG(DB_TRACE, DF_DB ": sending ae to node " RDB_F_RID " rank %u: term=%ld\n", DP_DB(db),
		RDB_P_RID(rdb_node_id), rdb_node->dn_rank, msg->term);

	if (DAOS_FAIL_CHECK(DAOS_RDB_SKIP_APPENDENTRIES_FAIL))
		D_GOTO(err, rc = 0);

	rc = rdb_create_raft_rpc(db, RDB_APPENDENTRIES, node, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to create AE RPC to node " RDB_F_RID, DP_DB(db),
			 RDB_P_RID(rdb_node_id));
		D_GOTO(err, rc);
	}
	in = crt_req_get(rpc);
	rc = rdb_raft_clone_ae(db, msg, &in->aei_msg);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to allocate entry array\n", DP_DB(db));
		D_GOTO(err_rpc, rc);
	}

	rc = rdb_send_raft_rpc(rpc, db);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to send AE RPC to node " RDB_F_RID, DP_DB(db),
			 RDB_P_RID(rdb_node_id));
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
rdb_replica_record_compare_void(const void *vx, const void *vy)
{
	const struct rdb_replica_record *x = vx;
	const struct rdb_replica_record *y = vy;

	return rdb_replica_id_compare(x->drr_id, y->drr_id);
}

/* Just some defensive sanity checks. */
static int
rdb_raft_check_replicas(uuid_t db_uuid, uint32_t layout_version,
			struct rdb_replica_record *replicas, int replicas_len)
{
	struct rdb_replica_record *rs;
	int                        rs_len;
	int                        i;
	int                        rc;

	if (replicas_len <= 0 || replicas_len > UINT8_MAX) {
		D_ERROR(DF_UUID ": invalid replicas_len: %d\n", DP_UUID(db_uuid), replicas_len);
		rc = -DER_INVAL;
		goto out;
	}

	rs_len = replicas_len;
	D_ALLOC_ARRAY(rs, rs_len);
	if (rs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	memcpy(rs, replicas, sizeof(*rs) * replicas_len);
	qsort(rs, rs_len, sizeof(*rs), rdb_replica_record_compare_void);

	for (i = 0; i < replicas_len; i++) {
		if (i > 0 && rs[i].drr_id.rri_rank == rs[i - 1].drr_id.rri_rank) {
			D_ERROR(DF_UUID ": duplicate replica rank: %u\n", DP_UUID(db_uuid),
				rs[i].drr_id.rri_rank);
			rc = -DER_INVAL;
			goto out_rs;
		}
		if (layout_version < RDB_LAYOUT_VERSION_REPLICA_ID &&
		    replicas[i].drr_id.rri_gen != 0) {
			D_ERROR(DF_UUID ": unexpected replica gen: " RDB_F_RID "\n",
				DP_UUID(db_uuid), RDB_P_RID(replicas[i].drr_id));
			rc = -DER_INVAL;
			goto out_rs;
		}
	}

	rc = 0;
out_rs:
	D_FREE(rs);
out:
	return rc;
}

static int
rdb_raft_store_replicas(uuid_t db_uuid, daos_handle_t lc, uint64_t index, uint32_t layout_version,
			struct rdb_replica_record *replicas, int replicas_len, rdb_vos_tx_t vtx)
{
	d_iov_t   keys[2];
	d_iov_t   vals[2];
	uint8_t   nreplicas;
	d_rank_t *ranks = NULL;
	int       i;
	int       rc;

	rc = rdb_raft_check_replicas(db_uuid, layout_version, replicas, replicas_len);
	if (rc != 0)
		return rc;

	D_ASSERTF(0 < replicas_len && replicas_len <= UINT8_MAX, "replicas_len = %u", replicas_len);
	nreplicas = replicas_len;
	keys[0] = rdb_lc_nreplicas;
	d_iov_set(&vals[0], &nreplicas, sizeof(nreplicas));

	keys[1] = rdb_lc_replicas;
	if (layout_version < RDB_LAYOUT_VERSION_REPLICA_ID) {
		D_ALLOC_ARRAY(ranks, replicas_len);
		if (ranks == NULL)
			return -DER_NOMEM;
		for (i = 0; i < replicas_len; i++)
			ranks[i] = replicas[i].drr_id.rri_rank;
		d_iov_set(&vals[1], ranks, sizeof(*ranks) * replicas_len);
	} else {
		d_iov_set(&vals[1], replicas, sizeof(*replicas) * replicas_len);
	}

	rc = rdb_lc_update(lc, index, RDB_LC_ATTRS, true /* crit */, 2 /* n */, keys, vals, vtx);
	if (rc == 0) {
		D_DEBUG(DB_MD, DF_UUID ": stored nreplicas and replicas at " DF_U64 ":\n",
			DP_UUID(db_uuid), index);
		for (i = 0; i < replicas_len; i++)
			D_DEBUG(DB_MD, DF_UUID ":  [%d]: id=" RDB_F_RID " reserved=" DF_X64 "\n",
				DP_UUID(db_uuid), i, RDB_P_RID(replicas[i].drr_id),
				replicas[i].drr_reserved);
	} else {
		DL_ERROR(rc, DF_UUID ": failed to update nreplicas and replicas", DP_UUID(db_uuid));
	}

	D_FREE(ranks);
	return rc;
}

/* The caller must free *replicas_out with D_FREE. */
int
rdb_raft_load_replicas(uuid_t db_uuid, daos_handle_t lc, uint64_t index, uint32_t layout_version,
		       struct rdb_replica_record **replicas_out, int *replicas_len_out)
{
	d_iov_t                    value;
	uint8_t                    nreplicas;
	struct rdb_replica_record *replicas = NULL;
	d_rank_t                  *ranks    = NULL;
	int                        i;
	int                        rc;

	d_iov_set(&value, &nreplicas, sizeof(nreplicas));
	rc = rdb_lc_lookup(lc, index, RDB_LC_ATTRS, &rdb_lc_nreplicas, &value);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_UUID ": no replicas at " DF_U64 "\n", DP_UUID(db_uuid), index);
		nreplicas = 0;
		rc        = 0;
	} else if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to look up nreplicas", DP_UUID(db_uuid));
		goto out;
	}

	if (nreplicas > 0) {
		D_ALLOC_ARRAY(replicas, nreplicas);
		if (replicas == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}

		if (layout_version < RDB_LAYOUT_VERSION_REPLICA_ID) {
			D_ALLOC_ARRAY(ranks, nreplicas);
			if (ranks == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}
			d_iov_set(&value, ranks, sizeof(*ranks) * nreplicas);
		} else {
			d_iov_set(&value, replicas, sizeof(*replicas) * nreplicas);
		}

		rc = rdb_lc_lookup(lc, index, RDB_LC_ATTRS, &rdb_lc_replicas, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to look up replicas", DP_UUID(db_uuid));
			goto out;
		}

		if (layout_version < RDB_LAYOUT_VERSION_REPLICA_ID)
			for (i = 0; i < nreplicas; i++)
				replicas[i].drr_id.rri_rank = ranks[i];

		rc = rdb_raft_check_replicas(db_uuid, layout_version, replicas, nreplicas);
		if (rc != 0)
			goto out;
	}

out:
	D_FREE(ranks);
	if (rc == 0) {
		D_DEBUG(DB_MD, DF_UUID ": loaded nreplicas and replicas at " DF_U64 ":\n",
			DP_UUID(db_uuid), index);
		for (i = 0; i < nreplicas; i++)
			D_DEBUG(DB_MD, DF_UUID ":  [%d]: id=" RDB_F_RID " reserved=" DF_X64 "\n",
				DP_UUID(db_uuid), i, RDB_P_RID(replicas[i].drr_id),
				replicas[i].drr_reserved);
		*replicas_out     = replicas;
		*replicas_len_out = nreplicas;
	} else {
		D_FREE(replicas);
	}
	return rc;
}

/* Caller must hold d_raft_mutex. */
static int
rdb_raft_add_node(struct rdb *db, struct rdb_replica_record record)
{
	struct rdb_raft_node	*dnode;
	raft_node_t		*node;
	int			 rc = 0;

	/*
	 * Note that we are unable to handle failures from this allocation at
	 * the moment. See also rdb_raft_cb_notify_membership_event and
	 * rdb_raft_load_snapshot.
	 */
	dnode = calloc(1, sizeof(*dnode));
	if (dnode == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	dnode->dn_rank = record.drr_id.rri_rank;

	node = raft_add_node(db->d_raft, dnode, rdb_replica_id_encode(record.drr_id),
			     rdb_replica_id_compare(record.drr_id, db->d_replica_id) == 0);
	if (node == NULL) {
		D_ERROR(DF_DB ": failed to add node " RDB_F_RID "\n", DP_DB(db),
			RDB_P_RID(record.drr_id));
		free(dnode);
		D_GOTO(out, rc = -DER_NOMEM);
	}

out:
	return rc;
}

/* Load the LC base. */
static int
rdb_raft_load_snapshot(struct rdb *db)
{
	struct rdb_replica_record *replicas;
	int                        replicas_len;
	int                        i;
	int                        rc;

	D_DEBUG(DB_MD, DF_DB": loading snapshot: base="DF_U64" term="DF_U64"\n",
		DP_DB(db), db->d_lc_record.dlr_base,
		db->d_lc_record.dlr_base_term);

	/*
	 * Load the replicas first to minimize the chance of an error happening
	 * after the raft_begin_load_snapshot call, which removes all nodes in
	 * raft.
	 */
	rc = rdb_raft_load_replicas(db->d_uuid, db->d_lc, db->d_lc_record.dlr_base, db->d_version,
				    &replicas, &replicas_len);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to load replicas in snapshot "DF_U64" (term="DF_U64"): "
			DF_RC"\n", DP_DB(db), db->d_lc_record.dlr_base,
			db->d_lc_record.dlr_base_term, DP_RC(rc));
		goto out;
	}

	/*
	 * Since loading a snapshot is logically equivalent to an AE request
	 * that first pops all log entries and then offers those represented by
	 * the snapshot, we empty the KVS cache for any KVS create operations
	 * reverted by the popping.
	 */
	rdb_kvs_cache_evict(db->d_kvss);

	rc = raft_begin_load_snapshot(db->d_raft, db->d_lc_record.dlr_base_term,
				      db->d_lc_record.dlr_base);
	if (rc != 0) {
		if (rc == RAFT_ERR_SNAPSHOT_ALREADY_LOADED) {
			rc = 0;
			goto out_replicas;
		}
		D_ERROR(DF_DB": failed to load snapshot "DF_U64" (term="DF_U64"): "DF_RC"\n",
			DP_DB(db), db->d_lc_record.dlr_base, db->d_lc_record.dlr_base_term,
			DP_RC(rc));
		rc = rdb_raft_rc(rc);
		goto out_replicas;
	}

	/* Add the corresponding nodes to raft. */
	for (i = 0; i < replicas_len; i++) {
		rc = rdb_raft_add_node(db, replicas[i]);
		/* TODO: Freeze and shut down db. */
		D_ASSERTF(rc == 0, "failed to add node: "DF_RC"\n", DP_RC(rc));
	}

	rc = raft_end_load_snapshot(db->d_raft);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

out_replicas:
	D_FREE(replicas);
out:
	return rc;
}

/* Unload the current snapshot. */
static void
rdb_raft_unload_snapshot(struct rdb *db)
{
	while (raft_get_num_nodes(db->d_raft) > 0)
		raft_remove_node(db->d_raft, raft_get_node_from_idx(db->d_raft, 0));
}

static int
rdb_raft_pack_chunk(daos_handle_t lc, struct rdb_raft_is *is, d_iov_t *kds,
		    d_iov_t *data, struct rdb_anchor *anchor)
{
	d_sg_list_t			sgl;
	struct ds_obj_enum_arg		arg = { 0 };
	struct vos_iter_anchors		anchors = { 0 };
	vos_iter_param_t		param = { 0 };
	int				rc;

	/*
	 * Set up the iteration for everything in the log container at
	 * is->dis_index.
	 */
	param.ip_hdl = lc;
	rdb_anchor_to_hashes(&is->dis_anchor, &anchors.ia_obj, &anchors.ia_dkey,
			     &anchors.ia_akey, &anchors.ia_ev, &anchors.ia_sv);
	param.ip_epr.epr_lo = is->dis_index;
	param.ip_epr.epr_hi = is->dis_index;
	param.ip_epc_expr = VOS_IT_EPC_LE;
	arg.chk_key2big = true;	/* see fill_key() & fill_rec() */

	/* Set up the buffers. */
	arg.kds = kds->iov_buf;
	arg.kds_cap = kds->iov_buf_len / sizeof(*arg.kds);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = data;
	arg.sgl = &sgl;

	arg.copy_data_cb = vos_iter_copy;
	/* Attempt to inline all values until recx bulks are implemented. */
	arg.inline_thres = 1 * 1024 * 1024;

	/* Enumerate from the object level. */
	rc = ds_obj_enum_pack(&param, VOS_ITER_OBJ, true, &anchors, &arg,
			      vos_iterate, NULL /* dth */);
	if (rc < 0)
		return rc;

	/*
	 * Report the new anchor. When rc == 0, dc_obj_enum_pack doesn't guarantee
	 * all the anchors to be EOF.
	 */
	if (rc == 0)
		rdb_anchor_set_eof(anchor);
	else /* rc == 1 */
		rdb_anchor_from_hashes(anchor, &anchors.ia_obj,
				       &anchors.ia_dkey, &anchors.ia_akey,
				       &anchors.ia_ev, &anchors.ia_sv);

	/* Report the buffer lengths. data.iov_len is set by ds_obj_enum_pack. */
	kds->iov_len = sizeof(*arg.kds) * arg.kds_len;

	return 0;
}

static int
rdb_raft_cb_send_installsnapshot(raft_server_t *raft, void *arg,
				 raft_node_t *node, msg_installsnapshot_t *msg)
{
	struct rdb		       *db = arg;
	rdb_replica_id_t                rdb_node_id;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	struct rdb_raft_is	       *is = &rdb_node->dn_is;
	crt_rpc_t		       *rpc;
	struct rdb_installsnapshot_in  *in;
	d_iov_t			kds;
	d_iov_t			data;
	d_sg_list_t			sgl;
	struct dss_module_info	       *info = dss_get_module_info();
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_ASSERT(node != NULL);
	D_ASSERT(rdb_node != NULL);
	rdb_node_id = rdb_replica_id_decode(raft_node_get_id(node));

	rc = rdb_create_raft_rpc(db, RDB_INSTALLSNAPSHOT, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create IS RPC to rank %u: %d\n",
			DP_DB(db), rdb_node->dn_rank, rc);
		goto err;
	}

	/* Start filling the request. */
	in          = crt_req_get(rpc);
	in->isi_msg = *msg;

	/*
	 * Allocate the data buffers. The sizes mustn't change during the term
	 * of the leadership.
	 */
	kds.iov_buf_len = 4 * 1024;
	kds.iov_len = 0;
	D_ALLOC(kds.iov_buf, kds.iov_buf_len);
	if (kds.iov_buf == NULL)
		goto err_rpc;
	data.iov_buf_len = 1 * 1024 * 1024;
	data.iov_len = 0;
	D_ALLOC(data.iov_buf, data.iov_buf_len);
	if (data.iov_buf == NULL)
		goto err_kds;

	/*
	 * If the INSTALLSNAPSHOT state tracks a different term or snapshot,
	 * reinitialize it for the current term and snapshot.
	 */
	if (rdb_node->dn_term != raft_get_current_term(raft) ||
	    is->dis_index != msg->last_idx) {
		rdb_node->dn_term = raft_get_current_term(raft);
		is->dis_index = msg->last_idx;
		is->dis_seq = 0;
		rdb_anchor_set_zero(&is->dis_anchor);
	}

	/* Pack the chunk's data, anchor, and seq. */
	rc = rdb_raft_pack_chunk(db->d_lc, is, &kds, &data, &in->isi_anchor);
	if (rc != 0)
		goto err_data;
	in->isi_seq = is->dis_seq + 1;

	/*
	 * Create bulks for the buffers. crt_bulk_create looks at iov_buf_len
	 * instead of iov_len.
	 */
	kds.iov_buf_len = kds.iov_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &kds;
	rc = crt_bulk_create(info->dmi_ctx, &sgl, CRT_BULK_RO,
			     &in->isi_kds);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create key descriptor bulk for rank "
			"%u: %d\n", DP_DB(db), rdb_node->dn_rank, rc);
		goto err_data;
	}
	data.iov_buf_len = data.iov_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &data;
	rc = crt_bulk_create(info->dmi_ctx, &sgl, CRT_BULK_RO, &in->isi_data);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create key bulk for rank %u: %d\n",
			DP_DB(db), rdb_node->dn_rank, rc);
		goto err_kds_bulk;
	}

	rc = rdb_send_raft_rpc(rpc, db);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send IS RPC to rank %u: %d\n",
			DP_DB(db), rdb_node->dn_rank, rc);
		goto err_data_bulk;
	}

	D_DEBUG(DB_TRACE,
		DF_DB ": sent is to node " RDB_F_RID " rank %u: term=%ld last_idx=%ld seq=" DF_U64
		      " kds.len=" DF_U64 " data.len=" DF_U64 "\n",
		DP_DB(db), RDB_P_RID(rdb_node_id), rdb_node->dn_rank, in->isi_msg.term,
		in->isi_msg.last_idx, in->isi_seq, kds.iov_len, data.iov_len);
	return 0;

err_data_bulk:
	crt_bulk_free(in->isi_data);
err_kds_bulk:
	crt_bulk_free(in->isi_kds);
err_data:
	D_FREE(data.iov_buf);
err_kds:
	D_FREE(kds.iov_buf);
err_rpc:
	crt_req_decref(rpc);
err:
	return rc;
}

struct rdb_raft_bulk {
	ABT_eventual	drb_eventual;
	int		drb_n;
	int		drb_rc;
};

static int
rdb_raft_recv_is_bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct rdb_raft_bulk   *arg = cb_info->bci_arg;

	if (cb_info->bci_rc != 0) {
		if (arg->drb_rc == 0)
			arg->drb_rc = cb_info->bci_rc;
	}
	arg->drb_n--;
	if (arg->drb_n == 0) {
		int rc;

		rc = ABT_eventual_set(arg->drb_eventual, NULL /* value */,
				      0 /* nbytes */);
		D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	}
	return 0;
}

/*
 * Receive the bulk in->isi_kds and in->isi_data into kds and data,
 * respectively. The buffers are allocated with the exact sizes. Callers are
 * responsible for freeing these buffers.
 *
 * TODO: Implement and use a "parallel bulk" helper.
 */
static int
rdb_raft_recv_is(struct rdb *db, crt_rpc_t *rpc, d_iov_t *kds,
		 d_iov_t *data)
{
	struct rdb_installsnapshot_in  *in = crt_req_get(rpc);
	crt_bulk_t			kds_bulk;
	struct crt_bulk_desc		kds_desc;
	crt_bulk_opid_t			kds_opid;
	crt_bulk_t			data_bulk;
	struct crt_bulk_desc		data_desc;
	crt_bulk_opid_t			data_opid;
	d_sg_list_t			sgl;
	struct rdb_raft_bulk		arg;
	int				rc;

	/* Allocate the data buffers. */
	rc = crt_bulk_get_len(in->isi_kds, &kds->iov_buf_len);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	kds->iov_len = kds->iov_buf_len;
	D_ALLOC(kds->iov_buf, kds->iov_buf_len);
	if (kds->iov_buf == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	rc = crt_bulk_get_len(in->isi_data, &data->iov_buf_len);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	data->iov_len = data->iov_buf_len;
	D_ALLOC(data->iov_buf, data->iov_buf_len);
	if (data->iov_buf == NULL) {
		rc = -DER_NOMEM;
		goto out_kds;
	}

	/* Create bulks for the buffers. */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &in->isi_local.rl_kds_iov;
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &kds_bulk);
	if (rc != 0)
		goto out_data;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &in->isi_local.rl_data_iov;
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &data_bulk);
	if (rc != 0)
		goto out_kds_bulk;

	/* Prepare the bulk callback argument. */
	rc = ABT_eventual_create(0 /* nbytes */, &arg.drb_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_data_bulk;
	}
	arg.drb_n = 2;
	arg.drb_rc = 0;

	/* Transfer the data. */
	memset(&kds_desc, 0, sizeof(kds_desc));
	kds_desc.bd_rpc = rpc;
	kds_desc.bd_bulk_op = CRT_BULK_GET;
	kds_desc.bd_remote_hdl = in->isi_kds;
	kds_desc.bd_local_hdl = kds_bulk;
	kds_desc.bd_len = kds->iov_buf_len;
	rc = crt_bulk_transfer(&kds_desc, rdb_raft_recv_is_bulk_cb, &arg,
			       &kds_opid);
	if (rc != 0)
		goto out_eventual;
	memset(&data_desc, 0, sizeof(data_desc));
	data_desc.bd_rpc = rpc;
	data_desc.bd_bulk_op = CRT_BULK_GET;
	data_desc.bd_remote_hdl = in->isi_data;
	data_desc.bd_local_hdl = data_bulk;
	data_desc.bd_len = data->iov_buf_len;
	rc = crt_bulk_transfer(&data_desc, rdb_raft_recv_is_bulk_cb, &arg,
			       &data_opid);
	if (rc != 0) {
		if (arg.drb_rc == 0)
			arg.drb_rc = rc;
		arg.drb_n--;
		if (arg.drb_n == 0)
			goto out_eventual;
		crt_bulk_abort(rpc->cr_ctx, kds_opid);
	}

	/* Wait for all transfers to complete. */
	rc = ABT_eventual_wait(arg.drb_eventual, NULL /* value */);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	rc = arg.drb_rc;

out_eventual:
	ABT_eventual_free(&arg.drb_eventual);
out_data_bulk:
	crt_bulk_free(data_bulk);
out_kds_bulk:
	crt_bulk_free(kds_bulk);
out_data:
	if (rc != 0)
		D_FREE(data->iov_buf);
out_kds:
	if (rc != 0)
		D_FREE(kds->iov_buf);
out:
	return rc;
}

struct rdb_raft_unpack_arg {
	daos_epoch_t eph;
	daos_handle_t slc;
};

static int
rdb_raft_exec_unpack_io(struct dc_obj_enum_unpack_io *io, void *arg)
{
	struct rdb_raft_unpack_arg *unpack_arg = arg;

#if 0
	int i;

	D_ASSERT(daos_key_match(&io->ui_dkey, &rdb_dkey));
	D_ASSERTF(io->ui_version == RDB_PM_VER, "%u\n", io->ui_version);
	for (i = 0; i < io->ui_iods_len; i++) {
		D_ASSERT(io->ui_iods[i].iod_type == DAOS_IOD_SINGLE);
		D_ASSERTF(io->ui_iods[i].iod_nr == 1, "%u\n",
			  io->ui_iods[i].iod_nr);
		D_ASSERTF(io->ui_iods[i].iod_recxs[0].rx_idx == 0, DF_U64"\n",
			  io->ui_iods[i].iod_recxs[0].rx_idx);
		D_ASSERTF(io->ui_iods[i].iod_recxs[0].rx_nr == 1, DF_U64"\n",
			  io->ui_iods[i].iod_recxs[0].rx_nr);

		D_ASSERT(io->ui_sgls != NULL);
		D_ASSERTF(io->ui_sgls[i].sg_nr == 1, "%u\n",
			  io->ui_sgls[i].sg_nr);
		D_ASSERT(io->ui_sgls[i].sg_iovs[0].iov_buf != NULL);
		D_ASSERT(io->ui_sgls[i].sg_iovs[0].iov_len > 0);
	}
#endif

	if (io->ui_iods_top == -1)
		return 0;

	return vos_obj_update(unpack_arg->slc, io->ui_oid, unpack_arg->eph,
			      io->ui_version, VOS_OF_CRIT /* flags */,
			      &io->ui_dkey, io->ui_iods_top + 1, io->ui_iods,
			      NULL, io->ui_sgls);
}

static int
rdb_raft_unpack_chunk(daos_handle_t slc, d_iov_t *kds_iov, d_iov_t *data,
		      int index)
{
	struct rdb_raft_unpack_arg unpack_arg;
	daos_unit_oid_t		   invalid_oid = { 0 };
	d_sg_list_t		   sgl;

	/* Set up the buffers. */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = data;

	unpack_arg.eph = index;
	unpack_arg.slc = slc;

	return dc_obj_enum_unpack(invalid_oid, kds_iov->iov_buf,
				  kds_iov->iov_len / sizeof(daos_key_desc_t),
				  &sgl, NULL, rdb_raft_exec_unpack_io,
				  &unpack_arg);
}

static int
rdb_raft_cb_recv_installsnapshot(raft_server_t *raft, void *arg,
				 raft_node_t *node, msg_installsnapshot_t *msg,
				 msg_installsnapshot_response_t *resp)
{
	struct rdb		       *db = arg;
	struct rdb_installsnapshot_in  *in;
	struct rdb_installsnapshot_out *out;
	daos_handle_t		       *slc = &db->d_slc;
	struct rdb_lc_record	       *slc_record = &db->d_slc_record;
	uint64_t			seq;
	struct rdb_anchor		anchor;
	d_iov_t				keys[2];
	d_iov_t				values[2];
	int				rc;

	in = container_of(msg, struct rdb_installsnapshot_in, isi_msg);
	out = container_of(resp, struct rdb_installsnapshot_out, iso_msg);

	D_ASSERT(db->d_raft_loaded);

	/* Is there an existing SLC? */
	if (daos_handle_is_valid(*slc)) {
		bool destroy = false;

		/* As msg->term == currentTerm and currentTerm >= dlr_term... */
		D_ASSERTF(msg->term >= slc_record->dlr_term,
			  "%ld >= "DF_U64"\n", msg->term,
			  slc_record->dlr_term);

		if (msg->term == slc_record->dlr_term) {
			if (msg->last_idx < slc_record->dlr_base) {
				D_DEBUG(DB_TRACE,
					DF_DB": stale snapshot: %ld < "DF_U64
					"\n", DP_DB(db), msg->last_idx,
					slc_record->dlr_base);
				/* Ask the leader to fast-forward matchIndex. */
				return 1;
			} else if (msg->last_idx > slc_record->dlr_base) {
				D_DEBUG(DB_TRACE,
					DF_DB": new snapshot: %ld > "DF_U64"\n",
					DP_DB(db), msg->last_idx,
					slc_record->dlr_base);
				destroy = true;
			}
		} else {
			D_DEBUG(DB_TRACE,
				DF_DB": new leader: %ld != "DF_U64"\n",
				DP_DB(db), msg->term, slc_record->dlr_term);
			/*
			 * We destroy the SLC anyway, even when the index
			 * matches, as the new leader may use a different
			 * maximal chunk size (once tunable).
			 */
			destroy = true;
		}

		if (destroy) {
			D_DEBUG(DB_TRACE, DF_DB": destroying slc: "DF_U64"\n",
				DP_DB(db), slc_record->dlr_base);
			vos_cont_close(*slc);
			*slc = DAOS_HDL_INVAL;
			rc = rdb_raft_destroy_lc(db->d_pool, db->d_mc,
						 &rdb_mc_slc,
						 slc_record->dlr_uuid,
						 slc_record);
			if (rc != 0)
				return rc;
		}
	}

	/* If necessary, create a new SLC. */
	if (daos_handle_is_inval(*slc)) {
		D_DEBUG(DB_TRACE, DF_DB": creating slc: %ld\n", DP_DB(db),
			msg->last_idx);
		rc = rdb_raft_create_lc(db->d_pool, db->d_mc, &rdb_mc_slc,
					msg->last_idx, msg->last_term,
					msg->term, slc_record);
		if (rc != 0)
			return rc;
		rc = vos_cont_open(db->d_pool, slc_record->dlr_uuid, slc);
		/* Not good, but we've just created it ourself... */
		D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	}

	/* We have an SLC matching this chunk. */
	if (in->isi_seq <= slc_record->dlr_seq) {
		D_DEBUG(DB_TRACE, DF_DB": already has: "DF_U64" <= "DF_U64"\n",
			DP_DB(db), in->isi_seq, slc_record->dlr_seq);
		/* Ask the leader to fast-forward seq. */
		out->iso_success = 1;
		out->iso_seq = slc_record->dlr_seq;
		out->iso_anchor = slc_record->dlr_anchor;
		return 0;
	} else if (in->isi_seq > slc_record->dlr_seq + 1) {
		/* Chunks are sent one by one for now. */
		D_ERROR(DF_DB": might have lost chunks: "DF_U64" > "DF_U64"\n",
			DP_DB(db), in->isi_seq, slc_record->dlr_seq);
		return -DER_IO;
	}

	/* Save this chunk but do not update the SLC record yet. */
	rc = rdb_raft_unpack_chunk(*slc, &in->isi_local.rl_kds_iov,
				   &in->isi_local.rl_data_iov, msg->last_idx);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to unpack IS chunk %ld/"DF_U64": %d\n",
			DP_DB(db), in->isi_msg.last_idx, in->isi_seq, rc);
		return rc;
	}

	/*
	 * Update the seq and anchor in the SLC record. If the SLC is complete,
	 * promote it to LC.
	 */
	seq = slc_record->dlr_seq;
	anchor = slc_record->dlr_anchor;
	slc_record->dlr_seq = in->isi_seq;
	slc_record->dlr_anchor = in->isi_anchor;
	if (rdb_anchor_is_eof(&slc_record->dlr_anchor)) {
		daos_handle_t	       *lc = &db->d_lc;
		struct rdb_lc_record   *lc_record = &db->d_lc_record;
		struct rdb_lc_record	r;
		daos_handle_t		h;

		D_DEBUG(DB_TRACE, DF_DB": slc complete: "DF_U64"/"DF_U64"\n",
			DP_DB(db), slc_record->dlr_base, slc_record->dlr_seq);

		/* Swap the records. */
		keys[0] = rdb_mc_lc;
		d_iov_set(&values[0], slc_record, sizeof(*slc_record));
		keys[1] = rdb_mc_slc;
		d_iov_set(&values[1], lc_record, sizeof(*lc_record));
		rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 2 /* n */, keys, values, NULL /* vtx */);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to swap LC records: %d\n",
				DP_DB(db), rc);
			slc_record->dlr_seq = seq;
			slc_record->dlr_anchor = anchor;
			return rc;
		}
		r = *lc_record;
		*lc_record = *slc_record;
		*slc_record = r;

		/* Swap the handles. */
		h = *lc;
		*lc = *slc;
		*slc = h;

		/* The chunk is successfully stored. */
		out->iso_success = 1;
		out->iso_seq = lc_record->dlr_seq;
		out->iso_anchor = lc_record->dlr_anchor;

		/* Load this snapshot. */
		rc = rdb_raft_load_snapshot(db);
		if (rc != 0)
			return rc;

		/* Destroy the previous LC, which is the SLC now. */
		vos_cont_close(*slc);
		*slc = DAOS_HDL_INVAL;
		rc = rdb_raft_destroy_lc(db->d_pool, db->d_mc, &rdb_mc_slc,
					 slc_record->dlr_uuid, slc_record);
		if (rc != 0)
			return rc;

		/* Inform raft that this snapshot is complete. */
		rc = 1;
	} else {
		D_DEBUG(DB_TRACE, DF_DB": chunk complete: "DF_U64"/"DF_U64"\n",
			DP_DB(db), slc_record->dlr_base, slc_record->dlr_seq);

		d_iov_set(&values[0], slc_record, sizeof(*slc_record));
		rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_slc, &values[0],
				   NULL /* vtx */);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to update SLC record: %d\n",
				DP_DB(db), rc);
			slc_record->dlr_seq = seq;
			slc_record->dlr_anchor = anchor;
			return rc;
		}

		/* The chunk is successfully stored. */
		out->iso_success = 1;
		out->iso_seq = slc_record->dlr_seq;
		out->iso_anchor = slc_record->dlr_anchor;
	}

	return rc;
}

static int
rdb_raft_cb_recv_installsnapshot_resp(raft_server_t *raft, void *arg,
				      raft_node_t *node,
				      msg_installsnapshot_response_t *resp)
{
	struct rdb		       *db = arg;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	struct rdb_raft_is	       *is = &rdb_node->dn_is;
	struct rdb_installsnapshot_out *out;

	out = container_of(resp, struct rdb_installsnapshot_out, iso_msg);

	/* If no longer transferring this snapshot, ignore this response. */
	if (rdb_node->dn_term != raft_get_current_term(raft) ||
	    is->dis_index != resp->last_idx) {
		D_DEBUG(DB_TRACE,
			DF_DB": rank %u: stale term "DF_U64" != %ld or index "
			DF_U64" != %ld\n", DP_DB(db), rdb_node->dn_rank,
			rdb_node->dn_term, raft_get_current_term(raft),
			is->dis_index, resp->last_idx);
		return 0;
	}

	/* If this chunk isn't successfully stored, ... */
	if (!out->iso_success) {
		/*
		 * ... but the whole snapshot is complete, it means the
		 * follower already matches up my log to the index of this
		 * snapshot.
		 */
		if (resp->complete) {
			D_DEBUG(DB_TRACE, DF_DB": rank %u: completed snapshot %ld\n", DP_DB(db),
				rdb_node->dn_rank, resp->last_idx);
			return 0;
		}

		/*
		 * ... and the snapshot is not complete, return a generic error so
		 * that raft will not retry too eagerly.
		 */
		D_DEBUG(DB_TRACE,
			DF_DB": rank %u: unsuccessful chunk %ld/"DF_U64"("
			DF_U64")\n", DP_DB(db), rdb_node->dn_rank,
			resp->last_idx, out->iso_seq, is->dis_seq);
		return -DER_MISC;
	}

	/* Ignore this stale response. */
	if (out->iso_seq <= is->dis_seq) {
		D_DEBUG(DB_TRACE,
			DF_DB": rank %u: stale chunk %ld/"DF_U64"("DF_U64")\n",
			DP_DB(db), rdb_node->dn_rank, resp->last_idx,
			out->iso_seq, is->dis_seq);
		return 0;
	}

	D_DEBUG(DB_TRACE,
		DF_DB": rank %u: completed chunk %ld/"DF_U64"("DF_U64")\n",
		DP_DB(db), rdb_node->dn_rank, resp->last_idx, out->iso_seq,
		is->dis_seq);

	/* Update the last sequence number and anchor. */
	is->dis_seq = out->iso_seq;
	is->dis_anchor = out->iso_anchor;

	return 0;
}

static int
rdb_raft_cb_persist_vote(raft_server_t *raft, void *arg, raft_node_id_t vote)
{
	struct rdb      *db       = arg;
	rdb_replica_id_t rdb_vote = rdb_replica_id_decode(vote);
	d_iov_t          value;
	int              rc;

	if (!db->d_raft_loaded)
		return 0;

	rdb_set_mc_vote_update_buf(db->d_version, &rdb_vote, &value);
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_vote, &value, NULL /* vtx */);
	if (rc != 0)
		DL_ERROR(rc, DF_DB ": failed to persist vote " RDB_F_RID, DP_DB(db),
			 RDB_P_RID(rdb_vote));

	return rc;
}

static int
rdb_raft_cb_persist_term(raft_server_t *raft, void *arg, raft_term_t term,
			 raft_node_id_t vote)
{
	struct rdb      *db       = arg;
	rdb_replica_id_t rdb_vote = rdb_replica_id_decode(vote);
	d_iov_t          keys[2];
	d_iov_t          values[2];
	int              rc;

	if (!db->d_raft_loaded)
		return 0;

	/* Update rdb_mc_term and rdb_mc_vote atomically. */
	keys[0] = rdb_mc_term;
	d_iov_set(&values[0], &term, sizeof(term));
	keys[1] = rdb_mc_vote;
	rdb_set_mc_vote_update_buf(db->d_version, &rdb_vote, &values[1]);
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 2 /* n */, keys, values, NULL /* vtx */);
	if (rc != 0)
		DL_ERROR(rc, DF_DB ": failed to update term %ld and vote " RDB_F_RID, DP_DB(db),
			 term, RDB_P_RID(rdb_vote));

	return rc;
}

static rdb_replica_id_t
rdb_raft_cfg_entry_node_id(raft_entry_t *entry, uint32_t layout_version)
{
	rdb_replica_id_t id;

	D_ASSERT(entry->data.buf != NULL);
	if (layout_version < RDB_LAYOUT_VERSION_REPLICA_ID) {
		D_ASSERTF(entry->data.len == sizeof(id.rri_rank), "%u\n", entry->data.len);
		id.rri_rank = *(d_rank_t *)entry->data.buf;
		id.rri_gen  = 0;
	} else {
		D_ASSERTF(entry->data.len == sizeof(id), "%u\n", entry->data.len);
		id = *(rdb_replica_id_t *)entry->data.buf;
	}
	return id;
}

/* See rdb_raft_update_node. */
#define RDB_RAFT_UPDATE_NODE_NVOPS 1

/* Must invoke no more than RDB_RAFT_UPDATE_NODE_NVOPS VOS TX operations. */
static int
rdb_raft_update_node(struct rdb *db, uint64_t index, raft_entry_t *entry, rdb_vos_tx_t vtx)
{
	struct rdb_replica_record *replicas;
	int                        replicas_len;
	rdb_replica_id_t           id = rdb_raft_cfg_entry_node_id(entry, db->d_version);
	int                        i;
	struct rdb_replica_record *tmp;
	int                        tmp_len;
	void                      *result;
	int                        rc;

	D_DEBUG(DB_MD, DF_DB ": cfg entry " DF_U64 ": term=%ld type=%s node=" RDB_F_RID "\n",
		DP_DB(db), index, entry->term, rdb_raft_entry_type_str(entry->type), RDB_P_RID(id));

	rc = rdb_raft_load_replicas(db->d_uuid, db->d_lc, index, db->d_version, &replicas,
				    &replicas_len);
	if (rc != 0)
		goto out;

	switch (entry->type) {
	case RAFT_LOGTYPE_ADD_NODE:
		/*
		 * Ensure that no existing replica ID uses id.rri_rank or
		 * id.rri_gen (if nonzero). Note that nonzero generations
		 * are unique even for different ranks, because of how we
		 * produce them.
		 */
		for (i = 0; i < replicas_len; i++) {
			if (replicas[i].drr_id.rri_rank == id.rri_rank ||
			    (id.rri_gen != 0 && replicas[i].drr_id.rri_gen == id.rri_gen)) {
				D_ERROR(DF_DB ": %s: replica " RDB_F_RID
					      " already exists: " RDB_F_RID "\n",
					DP_DB(db), rdb_raft_entry_type_str(entry->type),
					RDB_P_RID(id), RDB_P_RID(replicas[i].drr_id));
				rc = -DER_INVAL;
				goto out_replicas;
			}
		}

		/* Append id to replicas. */
		tmp_len = replicas_len + 1;
		D_REALLOC_ARRAY(tmp, replicas, replicas_len, tmp_len);
		if (tmp == NULL) {
			rc = -DER_NOMEM;
			goto out_replicas;
		}
		replicas                                = tmp;
		replicas_len                            = tmp_len;
		replicas[replicas_len - 1].drr_id       = id;
		replicas[replicas_len - 1].drr_reserved = 0;
		break;
	case RAFT_LOGTYPE_REMOVE_NODE:
		/* Find id in replicas. */
		for (i = 0; i < replicas_len; i++)
			if (rdb_replica_id_compare(replicas[i].drr_id, id) == 0)
				break;
		if (i == replicas_len) {
			D_ERROR(DF_DB ": %s: replica " RDB_F_RID " does not exist\n", DP_DB(db),
				rdb_raft_entry_type_str(entry->type), RDB_P_RID(id));
			rc = -DER_INVAL;
			goto out_replicas;
		}

		/* Remove it. */
		if (replicas_len - i - 1 > 0)
			memmove(&replicas[i], &replicas[i + 1],
				(replicas_len - i - 1) * sizeof(*replicas));
		replicas_len--;
		break;
	default:
		D_ERROR(DF_DB ": entry type %s (%d) not supported: " RDB_F_RID "\n", DP_DB(db),
			rdb_raft_entry_type_str(entry->type), entry->type, RDB_P_RID(id));
		rc = -DER_NOTSUPPORTED;
		goto out_replicas;
	}

	rc = rdb_raft_store_replicas(db->d_uuid, db->d_lc, index, db->d_version, replicas,
				     replicas_len, vtx);

out_replicas:
	D_FREE(replicas);
out:
	result = rdb_raft_lookup_result(db, index);
	if (result != NULL)
		*(int *)result = rc;
	if (rc != 0)
		DL_ERROR(rc, DF_DB ": failed to do %s " RDB_F_RID " at index " DF_U64, DP_DB(db),
			 rdb_raft_entry_type_str(entry->type), RDB_P_RID(id), index);
	return rc;
}

/* See rdb_raft_log_offer_single. */
#define RDB_RAFT_ENTRY_NVOPS 2

static int
rdb_raft_entry_count_vops(struct rdb *db, raft_entry_t *entry)
{
	int count = 0;

	/* Count those that will be invoked when applying the entry. */
	if (entry->type == RAFT_LOGTYPE_NORMAL) {
		int rc;

		rc = rdb_tx_count_vops(db, entry->data.buf, entry->data.len);
		if (rc < 0)
			return rc;
		count += rc;
	} else if (raft_entry_is_cfg_change(entry)) {
		count += RDB_RAFT_UPDATE_NODE_NVOPS;
	} else {
		D_ERROR(DF_DB ": unknown entry type %d\n", DP_DB(db), entry->type);
		return -DER_IO;
	}

	/* Count those that will be invoked when storing the entry. */
	count += RDB_RAFT_ENTRY_NVOPS;

	return count;
}

/*
 * Must invoke no more than RDB_RAFT_ENTRY_NVOPS VOS TX operations directly
 * (i.e., not including those invoked by rdb_tx_apply and
 * rdb_raft_update_node) per VOS TX.
 */
static int
rdb_raft_log_offer_single(struct rdb *db, raft_entry_t *entry, uint64_t index)
{
	rdb_vos_tx_t     vtx;
	bool             skip_tx_apply = false;
	d_iov_t          keys[2];
	d_iov_t          values[2];
	struct rdb_entry header;
	int              n;
	bool             crit = true;
	bool             dirtied_tail;
	bool             dirtied_kvss;
	int              rc;

retry:
	/* Initialize or reset per TX variables. */
	dirtied_tail = false;
	dirtied_kvss = false;

	/* Begin a VOS TX. */
	if (skip_tx_apply) {
		D_ASSERTF(entry->type == RAFT_LOGTYPE_NORMAL, "%d == %d\n", entry->type,
			  RAFT_LOGTYPE_NORMAL);
		rc = RDB_RAFT_ENTRY_NVOPS;
	} else {
		rc = rdb_raft_entry_count_vops(db, entry);
		if (rc < 0) {
			DL_ERROR(rc, DF_DB ": failed to count VOS operations for entry %ld",
				 DP_DB(db), index);
			return rc;
		}
	}
	rc = rdb_vos_tx_begin(db, rc /* nvops */, &vtx);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to begin VOS TX for entry %ld", DP_DB(db), index);
		return rc;
	}

	/*
	 * Update the log tail. To get the same minor epoch for all MC updates
	 * across different VOS TXs, we update the MC first.
	 */
	D_ASSERTF(index == db->d_lc_record.dlr_tail, DF_U64 " == " DF_U64 "\n", index,
		  db->d_lc_record.dlr_tail);
	db->d_lc_record.dlr_tail = index + 1;
	dirtied_tail = true;
	d_iov_set(&values[0], &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc, &values[0], vtx);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to update log tail " DF_U64, DP_DB(db),
			 db->d_lc_record.dlr_tail);
		goto out_vtx;
	}

	if (entry->type == RAFT_LOGTYPE_NORMAL) {
		if (!skip_tx_apply) {
			/*
			 * If this is an rdb_tx entry, apply it. Note that the updates involved
			 * won't become visible to queries until entry index is committed.
			 * (Implicit queries resulted from rdb_kvs cache lookups won't happen
			 * until the TX releases the locks for the updates after the
			 * rdb_tx_commit() call returns.)
			 */
			rc = rdb_tx_apply(db, index, entry->data.buf, entry->data.len,
					  rdb_raft_lookup_result(db, index), &crit, vtx);
			if (rc == RDB_TX_APPLY_ERR_DETERMINISTIC) {
				/*
				 * We must abort VOS TX to discard any partial application of the
				 * entry, and begin a new VOS TX to store the entry without applying
				 * it. The new VOS TX will reuse crit.
				 */
				D_DEBUG(DB_TRACE, DF_DB ": deterministic error for entry %ld\n",
					DP_DB(db), index);
				rc = -DER_AGAIN;
				skip_tx_apply = true;
				goto out_vtx;
			} else if (rc != 0) {
				DL_ERROR(rc, DF_DB ": failed to apply entry " DF_U64, DP_DB(db),
					 index);
				goto out_vtx;
			}
			dirtied_kvss = true;
		}
	} else if (raft_entry_is_cfg_change(entry)) {
		rc = rdb_raft_update_node(db, index, entry, vtx);
		if (rc != 0) {
			DL_ERROR(rc, DF_DB ": failed to update replicas " DF_U64, DP_DB(db), index);
			goto out_vtx;
		}
	} else {
		D_ERROR(DF_DB ": unknown entry " DF_U64 " type: %d\n", DP_DB(db), index,
			entry->type);
		rc = -DER_IO;
		goto out_vtx;
	}

	/*
	 * Persist the header and the data (if nonempty). Discard the unused
	 * entry->id.
	 */
	n = 0;
	header.dre_term = entry->term;
	header.dre_type = entry->type;
	header.dre_size = entry->data.len;
	keys[n] = rdb_lc_entry_header;
	d_iov_set(&values[n], &header, sizeof(header));
	n++;
	if (entry->data.len > 0) {
		keys[n] = rdb_lc_entry_data;
		d_iov_set(&values[n], entry->data.buf, entry->data.len);
		n++;
	}
	rc = rdb_lc_update(db->d_lc, index, RDB_LC_ATTRS, crit, n, keys, values, vtx);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to persist entry " DF_U64, DP_DB(db), index);
		goto out_vtx;
	}

	/* Replace entry->data.buf with the data's persistent memory address. */
	if (entry->data.len > 0) {
		d_iov_set(&values[0], NULL, entry->data.len);
		rc = rdb_lc_lookup(db->d_lc, index, RDB_LC_ATTRS, &rdb_lc_entry_data, &values[0]);
		if (rc != 0) {
			DL_ERROR(rc, DF_DB ": failed to look up entry " DF_U64 " data", DP_DB(db),
				 index);
			goto out_vtx;
		}
		entry->data.buf = values[0].iov_buf;
	} else {
		entry->data.buf = NULL;
	}

out_vtx:
	/* End the VOS TX. If there's an error, revert all cache changes. */
	rc = rdb_vos_tx_end(db, vtx, rc);
	if (rc != 0) {
		if (dirtied_kvss)
			rdb_kvs_cache_evict(db->d_kvss);
		if (dirtied_tail)
			db->d_lc_record.dlr_tail = index;
		if (rc == -DER_AGAIN && skip_tx_apply) {
			D_DEBUG(DB_TRACE, DF_DB ": aborted before retrying entry %ld\n", DP_DB(db),
				index);
			goto retry;
		} else {
			DL_ERROR(rc, DF_DB ": failed to end VOS TX for entry %ld", DP_DB(db),
				 index);
			return rc;
		}
	}

	D_DEBUG(DB_TRACE, DF_DB ": appended entry %ld: term=%ld type=%s buf=%p len=%u\n", DP_DB(db),
		index, entry->term, rdb_raft_entry_type_str(entry->type), entry->data.buf,
		entry->data.len);
	return 0;
}

static int
rdb_raft_cb_log_offer(raft_server_t *raft, void *arg, raft_entry_t *entries, raft_index_t index,
		      int *n_entries)
{
	struct rdb *db = arg;
	int         i;
	int         rc = 0;

	if (!db->d_raft_loaded)
		return 0;

	/*
	 * Conservatively employ one VOS TX for each entry for now, so that if
	 * an entry encounters an error, we still end up making some progress
	 * by not rolling back prior entries in the batch. Once VOS supports
	 * batching TXs, we can optimize this process further.
	 */
	for (i = 0; i < *n_entries; i++) {
		rc = rdb_raft_log_offer_single(db, &entries[i], index + i);
		if (rc != 0)
			break;
	}
	*n_entries = i;

	return rc;
}

static int
rdb_raft_cb_log_poll(raft_server_t *raft, void *arg, raft_entry_t *entries,
		     raft_index_t index, int *n_entries)
{
	struct rdb     *db = arg;
	uint64_t	base = db->d_lc_record.dlr_base;
	uint64_t	base_term = db->d_lc_record.dlr_base_term;
	d_iov_t		value;
	int		rc;

	D_DEBUG(DB_TRACE, DF_DB": polling [%ld, %ld]\n", DP_DB(db), index,
		index + *n_entries - 1);

	D_ASSERT(db->d_raft_loaded);
	D_ASSERTF(index == db->d_lc_record.dlr_base + 1,
		  "%ld == "DF_U64" + 1\n", index, db->d_lc_record.dlr_base);

	/* Update the log base index and term. */
	db->d_lc_record.dlr_base = index + *n_entries - 1;
	db->d_lc_record.dlr_base_term = entries[*n_entries - 1].term;
	d_iov_set(&value, &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update log base from "DF_U64" to "
			DF_U64": %d\n", DP_DB(db), base,
			db->d_lc_record.dlr_base, rc);
		db->d_lc_record.dlr_base_term = base_term;
		db->d_lc_record.dlr_base = base;
		return rc;
	}

	/* Notify rdb_compactd(), who performs the real compaction. */
	ABT_cond_broadcast(db->d_compact_cv);

	return 0;
}

static int
rdb_raft_cb_log_pop(raft_server_t *raft, void *arg, raft_entry_t *entry,
		    raft_index_t index, int *n_entries)
{
	struct rdb     *db = arg;
	uint64_t	i = index;
	uint64_t	tail = db->d_lc_record.dlr_tail;
	d_iov_t		value;
	int		rc;

	D_ASSERT(db->d_raft_loaded);
	D_ASSERTF(i > db->d_lc_record.dlr_base, DF_U64" > "DF_U64"\n", i,
		  db->d_lc_record.dlr_base);
	D_ASSERTF(i + *n_entries <= db->d_lc_record.dlr_tail,
		  DF_U64" <= "DF_U64"\n", i + *n_entries,
		  db->d_lc_record.dlr_tail);

	/* Update the log tail. */
	db->d_lc_record.dlr_tail = i;
	d_iov_set(&value, &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update log tail "DF_U64": %d\n",
			DP_DB(db), db->d_lc_record.dlr_tail, rc);
		db->d_lc_record.dlr_tail = tail;
		return rc;
	}

	/*
	 * Since there may be KVS create operations being reverted by the
	 * rdb_lc_discard call below, empty the KVS cache.
	 */
	rdb_kvs_cache_evict(db->d_kvss);

	/* Ignore *n_entries; discard everything starting from index. */
	rc = rdb_lc_discard(db->d_lc, i, RDB_LC_INDEX_MAX);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to delete %d entries starting at "
			DF_U64": %d\n", DP_DB(db), *n_entries, i, rc);
		return rc;
	}

	/* Actual number of discarded entries is `tail - i` */
	D_DEBUG(DB_TRACE, DF_DB": deleted "DF_U64" entries"
		" starting at "DF_U64"\n", DP_DB(db), (tail - i), i);
	return 0;
}

static raft_node_id_t
rdb_raft_cb_log_get_node_id(raft_server_t *raft, void *arg, raft_entry_t *entry, raft_index_t index)
{
	struct rdb *db = arg;

	D_ASSERTF(raft_entry_is_cfg_change(entry), DF_DB ": index=%ld type=%s\n", DP_DB(db), index,
		  rdb_raft_entry_type_str(entry->type));
	return rdb_replica_id_encode(rdb_raft_cfg_entry_node_id(entry, db->d_version));
}

static void
rdb_raft_cb_notify_membership_event(raft_server_t *raft, void *udata, raft_node_t *node,
				    raft_entry_t *entry, raft_membership_e type)
{
	struct rdb           *db       = udata;
	struct rdb_raft_node *rdb_node = raft_node_get_udata(node);

	switch (type) {
	case RAFT_MEMBERSHIP_ADD:
		/*
		 * When loading a snapshot, we create the rdb_raft_node object
		 * based on our snapshot content before asking raft to create
		 * the raft_node_t object, because there is no entry for the
		 * current callback to work with.
		 */
		if (rdb_node != NULL)
			break;
		D_ASSERT(entry != NULL);
		rdb_node = calloc(1, sizeof(*rdb_node));
		/*
		 * Since we may be called from raft_offer_log or raft_pop_log,
		 * from where it's difficult to handle errors due to batching,
		 * assert that the allocation must succeed for the moment. Use
		 * calloc instead of D_ALLOC_PTR to avoid being fault-injected.
		 */
		D_ASSERT(rdb_node != NULL);
		rdb_node->dn_rank = rdb_raft_cfg_entry_node_id(entry, db->d_version).rri_rank;
		raft_node_set_udata(node, rdb_node);
		break;
	case RAFT_MEMBERSHIP_REMOVE:
		D_ASSERT(rdb_node != NULL);
		free(rdb_node);
		break;
	default:
		D_ASSERTF(false, DF_DB ": invalid raft membership event type %s\n", DP_DB(db),
			  rdb_raft_entry_type_str(type));
	}
}

static void
rdb_raft_cb_log(raft_server_t *raft, raft_node_t *node, void *arg, raft_loglevel_e level,
		const char *buf)
{
#define RRCL_LOG(flag)                                                                             \
	if (node == NULL)                                                                          \
		D_DEBUG(flag, DF_DB ": %s\n", DP_DB(db), buf);                                     \
	else                                                                                       \
		D_DEBUG(flag, DF_DB ": %s: node=" RDB_F_RID "\n", DP_DB(db), buf,                  \
			RDB_P_RID(rdb_replica_id_decode(raft_node_get_id(node))));

	struct rdb *db = raft_get_udata(raft);

	switch (level) {
	case RAFT_LOG_ERROR:
		RRCL_LOG(DLOG_ERR);
		break;
	case RAFT_LOG_INFO:
		/*
		 * Demote to D_DEBUG, because we don't have a mechanism yet to
		 * eventually stop replicas that have been excluded from the
		 * pool. Every 1--2 election timeouts, these replicas will log a
		 * few election messages, which might attract complaints if done
		 * with D_INFO.
		 */
		RRCL_LOG(DB_MD);
		break;
	default:
		RRCL_LOG(DB_IO);
	}

#undef RRCL_LOG
}

static raft_time_t
rdb_raft_cb_get_time(raft_server_t *raft, void *user_data)
{
	struct timespec	now;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	D_ASSERTF(rc == 0, "clock_gettime: %d\n", errno);
	return now.tv_sec * 1000 + now.tv_nsec / (1000 * 1000);
}

static double
rdb_raft_cb_get_rand(raft_server_t *raft, void *user_data)
{
	return d_randd();
}

/*
 * rdb's raft callback implementations
 *
 * Note that all callback implementations that write data shall check or
 * assert, depending on whether they are expected to be invoked during
 * rdb_raft_load, rdb.d_raft_loaded to avoid unwanted write I/Os. See
 * rdb_raft_load for more.
 */
static raft_cbs_t rdb_raft_cbs = {
	.send_requestvote		= rdb_raft_cb_send_requestvote,
	.send_appendentries		= rdb_raft_cb_send_appendentries,
	.send_installsnapshot		= rdb_raft_cb_send_installsnapshot,
	.recv_installsnapshot		= rdb_raft_cb_recv_installsnapshot,
	.recv_installsnapshot_response	= rdb_raft_cb_recv_installsnapshot_resp,
	.persist_vote			= rdb_raft_cb_persist_vote,
	.persist_term			= rdb_raft_cb_persist_term,
	.log_offer			= rdb_raft_cb_log_offer,
	.log_poll			= rdb_raft_cb_log_poll,
	.log_pop			= rdb_raft_cb_log_pop,
	.log_get_node_id		= rdb_raft_cb_log_get_node_id,
	.notify_membership_event	= rdb_raft_cb_notify_membership_event,
	.log				= rdb_raft_cb_log,
	.get_time			= rdb_raft_cb_get_time,
	.get_rand			= rdb_raft_cb_get_rand
};

static int
rdb_raft_compact_to_index(struct rdb *db, uint64_t index)
{
	int rc;

	D_DEBUG(DB_TRACE, DF_DB ": snapping " DF_U64 "\n", DP_DB(db), index);

	rc = raft_begin_snapshot(db->d_raft, index);
	if (rc != 0) {
		int rc2 = rdb_raft_rc(rc);
		D_ERROR(DF_DB ": raft_begin_snapshot() returned %d: " DF_RC, DP_DB(db), rc,
			DP_RC(rc2));
		return rc2;
	}
	/*
	 * VOS snaps every new index implicitly.
	 *
	 * raft_end_snapshot() only polls the log and wakes up
	 * rdb_compactd(), which does the real compaction (i.e., VOS
	 * aggregation) in the background.
	 */
	rc = raft_end_snapshot(db->d_raft);
	if (rc != 0) {
		int rc2 = rdb_raft_rc(rc);

		D_ERROR(DF_DB ": failed to poll entries: %d: " DF_RC, DP_DB(db), rc, DP_RC(rc2));
		rc = rc2;
	}

	return rc;
}

/*
 * Check if the log should be compacted. If so, trigger the compaction by
 * taking a snapshot (i.e., simply increasing the log base index in our
 * implementation).
 */
int
rdb_raft_trigger_compaction(struct rdb *db, bool compact_all, uint64_t *idx)
{
	uint64_t	base;
	int		n;
	int		rc = 0;

	/* Returning rc == 0 and idx nonzero means that compact/aggregation was started */
	if (idx)
		*idx = 0;

	/*
	 * If the number of applied entries reaches db->d_compact_thres,
	 * trigger compaction.
	 */
	base = raft_get_current_idx(db->d_raft) -
	       raft_get_log_count(db->d_raft);
	D_ASSERTF(db->d_applied >= base, DF_U64" >= "DF_U64"\n", db->d_applied,
		  base);
	n = db->d_applied - base;
	if ((n >= 1) && compact_all) {
		D_DEBUG(DB_TRACE, DF_DB": compact n=%d entries, to index "DF_U64"\n",
			DP_DB(db), n, (base + n));
		rc = rdb_raft_compact_to_index(db, (base + n));
		if (idx && (rc == 0))
			*idx = (base + n);
	} else if (n >= db->d_compact_thres) {
		uint64_t index;

		/*
		 * Compact half of the applied entries. For testing purposes,
		 * if db->d_compact_thres == 1 and n == 1, then compact the
		 * only applied entry.
		 */
		D_ASSERT(db->d_compact_thres >= 1);
		if (n < 2)
			index = base + 1;
		else
			index = base + n / 2;

		D_DEBUG(DB_TRACE, DF_DB": compact half of n=%d applied, up to index "DF_U64"\n",
			DP_DB(db), n, index);
		rc = rdb_raft_compact_to_index(db, index);
		if (idx && (rc == 0))
			*idx = index;
	}
	return rc;
}

/* Compact to index and yield from time to time (in rdb_lc_aggregate()). */
static int
rdb_raft_compact(struct rdb *db, uint64_t index)
{
	uint64_t	aggregated;
	d_iov_t		value;
	int		rc;

	D_DEBUG(DB_TRACE, DF_DB": compacting to "DF_U64"\n", DP_DB(db), index);

	rc = rdb_lc_aggregate(db->d_lc, index);
	if (rc != 0)
		return rc;

	/* Update the last aggregated index. */
	ABT_mutex_lock(db->d_raft_mutex);
	aggregated = db->d_lc_record.dlr_aggregated;
	db->d_lc_record.dlr_aggregated = index;
	d_iov_set(&value, &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update last aggregated index to "
			DF_U64": %d\n", DP_DB(db),
			db->d_lc_record.dlr_aggregated, rc);
		db->d_lc_record.dlr_aggregated = aggregated;
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}

	/* If requesting ULT is waiting synchronously, notify. */
	ABT_cond_broadcast(db->d_compacted_cv);
	ABT_mutex_unlock(db->d_raft_mutex);

	D_DEBUG(DB_TRACE, DF_DB": compacted to "DF_U64"\n", DP_DB(db), index);
	return 0;
}

static inline int
rdb_gc_yield(void *arg)
{
	struct dss_xstream	*dx = dss_current_xstream();

	if (dss_xstream_exiting(dx))
		return -1;

	ABT_thread_yield();
	return 0;
}

/* Daemon ULT for compacting polled entries (i.e., indices <= base). */
static void
rdb_compactd(void *arg)
{
	struct rdb *db = arg;

	D_DEBUG(DB_MD, DF_DB": compactd starting\n", DP_DB(db));
	for (;;) {
		uint64_t	base;
		bool		stop;
		int		rc;

		ABT_mutex_lock(db->d_raft_mutex);
		for (;;) {
			base = db->d_lc_record.dlr_base;
			stop = db->d_stop;
			if (db->d_lc_record.dlr_aggregated < base)
				break;
			if (stop)
				break;
			sched_cond_wait(db->d_compact_cv, db->d_raft_mutex);
		}
		ABT_mutex_unlock(db->d_raft_mutex);
		if (stop)
			break;
		rc = rdb_raft_compact(db, base);
		if (rc != 0) {
			D_ERROR(DF_DB ": failed to compact to base " DF_U64 ": " DF_RC "\n",
				DP_DB(db), base, DP_RC(rc));
			break;
		}
		vos_gc_pool(db->d_pool, -1, rdb_gc_yield, NULL);
	}
	D_DEBUG(DB_MD, DF_DB": compactd stopping\n", DP_DB(db));
}

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
		ABT_mutex_lock(db->d_raft_mutex);
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
		if (rc == -DER_SHUTDOWN) {
			D_DEBUG(DB_MD, DF_DB": requesting a replica stop\n",
				DP_DB(db));
			db->d_cbs->dc_stop(db, rc, db->d_arg);
		}
		ABT_mutex_unlock(db->d_raft_mutex);
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

	D_DEBUG(DB_MD, DF_DB": callbackd starting\n", DP_DB(db));
	for (;;) {
		struct rdb_raft_event	event;
		bool			stop;

		ABT_mutex_lock(db->d_raft_mutex);
		for (;;) {
			stop = db->d_stop;
			if (db->d_nevents > 0) {
				rdb_raft_dequeue_event(db, &event);
				break;
			}
			if (stop)
				break;
			sched_cond_wait(db->d_events_cv, db->d_raft_mutex);
		}
		ABT_mutex_unlock(db->d_raft_mutex);
		if (stop)
			break;
		rdb_raft_process_event(db, &event);
		ABT_thread_yield();
	}
	D_DEBUG(DB_MD, DF_DB": callbackd stopping\n", DP_DB(db));
}

static int
rdb_raft_step_up(struct rdb *db, uint64_t term)
{
	msg_entry_t		mentry;
	msg_entry_response_t	mresponse;
	int			rc;

	D_NOTE(DF_DB": became leader of term "DF_U64"\n", DP_DB(db), term);
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
	D_NOTE(DF_DB": no longer leader of term "DF_U64"\n", DP_DB(db), term);
	db->d_debut = 0;
	rdb_raft_queue_event(db, RDB_RAFT_STEP_DOWN, term);
}

/* Raft state variables that rdb watches for changes */
struct rdb_raft_state {
	bool		drs_leader;
	uint64_t	drs_term;
	uint64_t	drs_committed;
};

/* Save the variables into "state". Caller must hold d_raft_mutex. */
static void
rdb_raft_save_state(struct rdb *db, struct rdb_raft_state *state)
{
	state->drs_leader = raft_is_leader(db->d_raft);
	state->drs_term = raft_get_current_term(db->d_raft);
	state->drs_committed = raft_get_commit_idx(db->d_raft);
}

/*
 * Check the current state against "state", which shall be a previously-saved
 * state, and handle any changes and errors. Caller must hold d_raft_mutex.
 */
static int
rdb_raft_check_state(struct rdb *db, const struct rdb_raft_state *state,
		     int raft_rc)
{
	bool		leader = raft_is_leader(db->d_raft);
	uint64_t	term = raft_get_current_term(db->d_raft);
	uint64_t	committed;
	int		step_up_rc = 0;
	int		compaction_rc = 0;
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
		D_DEBUG(DB_TRACE, DF_DB": committed/applied to "DF_U64"\n",
			DP_DB(db), committed);
		db->d_applied = committed;
		compaction_rc = rdb_raft_trigger_compaction(db, false /* compact_all */,
							    NULL /* idx */);
	}

	/*
	 * Check raft_rc, step_up_rc, and compaction_rc in order. Then, handle
	 * the first error.
	 */
	if (raft_rc != 0)
		rc = rdb_raft_rc(raft_rc);
	else if (step_up_rc != 0)
		rc = step_up_rc;
	else
		rc = compaction_rc;
	switch (rc) {
	case -DER_NOMEM:
	case -DER_NOSPACE:
		if (leader) {
			/* No space / desperation: compact to committed idx */
			rdb_raft_compact_to_index(db, committed);

			raft_become_follower(db->d_raft);
			leader = false;
			/* If stepping up fails, don't step down. */
			if (step_up_rc != 0)
				break;
			rdb_raft_step_down(db, state->drs_term);
		}
		break;
	case -DER_SHUTDOWN:
	case -DER_IO:
		D_DEBUG(DB_MD, DF_DB": requesting a replica stop\n", DP_DB(db));
		db->d_cbs->dc_stop(db, rc, db->d_arg);
		break;
	}

	if (state->drs_term != term || state->drs_leader != leader ||
	    state->drs_committed != committed)
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
		D_FREE(result);
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
	D_FREE(result);
}

/* Append and wait for \a entry to be applied. Caller must hold d_raft_mutex. */
static int
rdb_raft_append_apply_internal(struct rdb *db, msg_entry_t *mentry,
			       void *result)
{
	msg_entry_response_t	mresponse;
	struct rdb_raft_state	state;
	uint64_t		index;
	int			rc;

	index = raft_get_current_idx(db->d_raft) + 1;
	if (result != NULL) {
		rc = rdb_raft_register_result(db, index, result);
		if (rc != 0)
			goto out;
	}

	rdb_raft_save_state(db, &state);
	rc = raft_recv_entry(db->d_raft, mentry, &mresponse);
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0) {
		if (rc != -DER_NOTLEADER)
			D_ERROR(DF_DB ": failed to append entry: " DF_RC "\n", DP_DB(db),
				DP_RC(rc));
		goto out_result;
	}

	/* The actual index must match the expected index. */
	D_ASSERTF(mresponse.idx == index, "%ld == "DF_U64"\n",
		  mresponse.idx, index);
	rc = rdb_raft_wait_applied(db, mresponse.idx, mresponse.term);
	raft_apply_all(db->d_raft);

out_result:
	if (result != NULL)
		rdb_raft_unregister_result(db, index);
out:
	return rc;
}

int
rdb_raft_append_apply_cfg(struct rdb *db, raft_logtype_e type, rdb_replica_id_t id)
{
	msg_entry_t entry = {.type = type};
	int         result;
	int         rc;

	D_ASSERTF(raft_entry_is_cfg_change(&entry), "invalid type: %d\n", type);
	D_DEBUG(DB_MD, DF_DB ": %s " RDB_F_RID "\n", DP_DB(db), rdb_raft_entry_type_str(type),
		RDB_P_RID(id));

	if (db->d_version >= RDB_LAYOUT_VERSION_REPLICA_ID) {
		entry.data.buf = &id;
		entry.data.len = sizeof(id);
	} else {
		entry.data.buf = &id.rri_rank;
		entry.data.len = sizeof(id.rri_rank);
	}

	rc = rdb_raft_append_apply_internal(db, &entry, &result);
	return (rc != 0) ? rc : result;
}

/* Caller must hold d_raft_mutex. */
int
rdb_raft_append_apply(struct rdb *db, void *entry, size_t size, void *result)
{
	msg_entry_t	 mentry = {};

	mentry.type = RAFT_LOGTYPE_NORMAL;
	mentry.data.buf = entry;
	mentry.data.len = size;
	return rdb_raft_append_apply_internal(db, &mentry, result);
}

/* Verify the leadership with a majority. */
int
rdb_raft_verify_leadership(struct rdb *db)
{
	if (db->d_use_leases && raft_has_majority_leases(db->d_raft))
		return 0;

	/*
	 * Since raft does not provide a function for verifying leadership via
	 * RPCs yet, append an empty entry as a (slower) workaround.
	 */
	return rdb_raft_append_apply(db, NULL /* entry */, 0 /* size */, NULL /* result */);
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
	struct sched_req_attr	 attr = { 0 };
	uuid_t			 anonym_uuid;
	struct sched_request	*sched_req;

	D_DEBUG(DB_MD, DF_DB": timerd starting\n", DP_DB(db));

	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	sched_req = sched_req_get(&attr, ABT_THREAD_NULL);
	if (sched_req == NULL) {
		D_ERROR(DF_DB": failed to get sched req.\n", DP_DB(db));
		return;
	}

	t = ABT_get_wtime();
	t_prev = t;
	do {
		struct rdb_raft_state	state;
		double			d_prev = t - t_prev;

		if (d_prev - d > 1 /* s */)
			D_WARN(DF_DB": not scheduled for %f second\n",
			       DP_DB(db), d_prev - d);

		ABT_mutex_lock(db->d_raft_mutex);
		rdb_raft_save_state(db, &state);
		rc = raft_periodic(db->d_raft);
		rc = rdb_raft_check_state(db, &state, rc);
		ABT_mutex_unlock(db->d_raft_mutex);
		if (rc != 0)
			D_ERROR(DF_DB ": raft_periodic() failed: " DF_RC "\n", DP_DB(db),
				DP_RC(rc));
		if (db->d_stop)
			break;

		t_prev = t;
		/* Wait for d in [d_min, d_max] before the next beat. */
		d = d_min + (d_max - d_min) * rdb_raft_rand();
		t = ABT_get_wtime();
		if (t < t_prev + d) {
			d_prev = t_prev + d - t;
			sched_req_sleep(sched_req, (uint32_t)(d_prev * 1000));
			t = ABT_get_wtime();
		}
	} while (!db->d_stop);

	sched_req_put(sched_req);

	D_DEBUG(DB_MD, DF_DB": timerd stopping\n", DP_DB(db));
}

/*
 * Create an LC or SLC, depending on key. If not NULL, record shall point to
 * the cache of the LC or SLC record.
 *
 * Note that this function doesn't attempt to rollback the record if the
 * container creation fails.
 */
static int
rdb_raft_create_lc(daos_handle_t pool, daos_handle_t mc, d_iov_t *key,
		   uint64_t base, uint64_t base_term, uint64_t term,
		   struct rdb_lc_record *record)
{
	struct rdb_lc_record	r = {
		.dlr_base	= base,
		.dlr_base_term	= base_term,
		.dlr_tail	= base + 1,
		.dlr_aggregated	= base,
		.dlr_term	= term
	};
	d_iov_t			value;
	int			rc;

	D_ASSERTF(key == &rdb_mc_lc || key == &rdb_mc_slc, "%p\n", key);

	if (key == &rdb_mc_lc) {
		/* A new LC is complete. */
		r.dlr_seq = 1;
		rdb_anchor_set_eof(&r.dlr_anchor);
	} else {
		/* A new SLC is empty. */
		r.dlr_seq = 0;
		rdb_anchor_set_zero(&r.dlr_anchor);
	}

	/* Create the record before creating the container. */
	uuid_generate(r.dlr_uuid);
	d_iov_set(&value, &r, sizeof(r));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, key, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR("failed to create %s record: %d\n",
			key == &rdb_mc_lc ? "LC" : "SLC", rc);
		return rc;
	}
	if (record != NULL)
		*record = r;

	/* Create the container. Ignore record rollbacks for now. */
	rc = vos_cont_create(pool, r.dlr_uuid);
	if (rc != 0) {
		D_ERROR("failed to create %s "DF_UUID": %d\n",
			key == &rdb_mc_lc ? "LC" : "SLC", DP_UUID(r.dlr_uuid),
			rc);
		return rc;
	}

	return 0;
}

static int
rdb_raft_destroy_lc(daos_handle_t pool, daos_handle_t mc, d_iov_t *key,
		    uuid_t uuid, struct rdb_lc_record *record)
{
	struct rdb_lc_record	r = {};
	d_iov_t			value;
	int			rc;

	D_ASSERTF(key == &rdb_mc_lc || key == &rdb_mc_slc, "%p\n", key);

	/* Destroy the container first. */
	rc = vos_cont_destroy(pool, uuid);
	if (rc != 0) {
		D_ERROR("failed to destroy %s "DF_UUID": %d\n",
			key == &rdb_mc_lc ? "LC" : "SLC", DP_UUID(uuid), rc);
		return rc;
	}

	/* Clear the record. We cannot rollback the destroy. */
	uuid_clear(r.dlr_uuid);
	d_iov_set(&value, &r, sizeof(r));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, key, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR("failed to clear %s record: %d\n",
			key == &rdb_mc_lc ? "LC" : "SLC", rc);
		return rc;
	}
	if (record != NULL)
		*record = r;

	return 0;
}

/*
 * The caller, rdb_create(), will remove the VOS pool file if we return an
 * error.
 */
int
rdb_raft_init(uuid_t db_uuid, daos_handle_t pool, daos_handle_t mc, rdb_replica_id_t *replicas,
	      int replicas_len, uint32_t layout_version)
{
	d_iov_t                    value;
	daos_handle_t              lc;
	struct rdb_lc_record       record;
	uint64_t                   base;
	struct rdb_replica_record *replica_records;
	int                        i;
	int                        rc;
	int                        rc_close;

	/*
	 * If replicas are specified, we are bootstrapping and shall initialize
	 * the LC at index 1 with replicas. Otherwise, we are not bootstrapping
	 * and shall initialize the LC to be empty.
	 */
	base = (replicas == NULL || replicas_len == 0) ? 0 : 1;

	rc = rdb_raft_create_lc(pool, mc, &rdb_mc_lc, base, 0 /* base_term */,
				0 /* term */, &record /* lc_record */);
	if (rc != 0)
		return rc;

	if (base == 0)
		return 0;

	rc = vos_cont_open(pool, record.dlr_uuid, &lc);
	/* We are opening a container that we've just created. */
	D_ASSERTF(rc == 0, "open LC: " DF_RC "\n", DP_RC(rc));

	D_ALLOC_ARRAY(replica_records, replicas_len);
	if (replica_records == NULL) {
		rc = -DER_NOMEM;
		goto out_lc;
	}
	for (i = 0; i < replicas_len; i++)
		replica_records[i].drr_id = replicas[i];
	rc = rdb_raft_store_replicas(db_uuid, lc, base, layout_version, replica_records,
				     replicas_len, NULL /* vtx */);
	D_FREE(replica_records);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to initialize replicas", DP_UUID(db_uuid));
		goto out_lc;
	}

	/* Initialize rdb_lc_replica_gen_next to max{replicas[].rri_gen} + 1. */
	if (layout_version >= RDB_LAYOUT_VERSION_REPLICA_ID) {
		uint32_t replica_gen_next = 0;

		for (i = 0; i < replicas_len; i++)
			if (replicas[i].rri_gen > replica_gen_next)
				replica_gen_next = replicas[i].rri_gen;
		replica_gen_next++;
		D_DEBUG(DB_MD, DF_UUID ": replica_gen_next=%u\n", DP_UUID(db_uuid),
			replica_gen_next);
		d_iov_set(&value, &replica_gen_next, sizeof(replica_gen_next));
		rc = rdb_lc_update(lc, base, RDB_LC_ATTRS, false /* crit */, 1,
				   &rdb_lc_replica_gen_next, &value, NULL /* vtx */);
		if (rc != 0)
			DL_ERROR(rc, DF_UUID ": failed to initialize next replica generation",
				 DP_UUID(db_uuid));
	}

out_lc:
	rc_close = vos_cont_close(lc);
	return (rc != 0) ? rc : rc_close;
}

static int
rdb_raft_open_lc(struct rdb *db)
{
	d_iov_t	value;
	int	rc;

	d_iov_set(&value, &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_lc, &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to look up LC: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		return rc;
	}

	rc = vos_cont_open(db->d_pool, db->d_lc_record.dlr_uuid, &db->d_lc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to open LC "DF_UUID": "DF_RC"\n", DP_DB(db),
			DP_UUID(db->d_lc_record.dlr_uuid), DP_RC(rc));
		return rc;
	}

	/*
	 * Recover the LC by discarding any partially appended entries. Yield
	 * after the call just in case we have occupied the xstream for a while
	 * since the last yield inside the call.
	 */
	rc = rdb_lc_discard(db->d_lc, db->d_lc_record.dlr_tail, RDB_LC_INDEX_MAX);
	ABT_thread_yield();
	if (rc != 0) {
		D_ERROR(DF_DB": failed to recover LC "DF_U64": "DF_RC"\n", DP_DB(db),
			db->d_lc_record.dlr_base, DP_RC(rc));
		vos_cont_close(db->d_lc);
		return rc;
	}

	return 0;
}

static void
rdb_raft_close_lc(struct rdb *db)
{
	int rc;

	rc = vos_cont_close(db->d_lc);
	D_ASSERTF(rc == 0, DF_DB": close LC: "DF_RC"\n", DP_DB(db), DP_RC(rc));
}

static int
rdb_raft_load_entry(struct rdb *db, uint64_t index)
{
	d_iov_t			value;
	struct rdb_entry	header;
	raft_entry_t		entry;
	int			n_entries;
	int			rc;

	/* Look up the header. */
	d_iov_set(&value, &header, sizeof(header));
	rc = rdb_lc_lookup(db->d_lc, index, RDB_LC_ATTRS, &rdb_lc_entry_header,
			   &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to look up entry "DF_U64" header: %d\n",
			DP_DB(db), index, rc);
		return rc;
	}
	entry.term = header.dre_term;
	entry.id = 0; /* unused */
	entry.type = header.dre_type;
	entry.data.len = header.dre_size;

	/* Look up the persistent memory address of the data (if nonempty). */
	if (entry.data.len > 0) {
		d_iov_set(&value, NULL, header.dre_size);
		rc = rdb_lc_lookup(db->d_lc, index, RDB_LC_ATTRS,
				   &rdb_lc_entry_data, &value);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to look up entry "DF_U64
				" data: %d\n", DP_DB(db), index, rc);
			return rc;
		}
		entry.data.buf = value.iov_buf;
	} else {
		entry.data.buf = NULL;
	}

	n_entries = 1;
	rc = raft_append_entries(db->d_raft, &entry, &n_entries);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to load entry "DF_U64": %d\n", DP_DB(db),
			index, rc);
		return rdb_raft_rc(rc);
	}

	if (raft_entry_is_cfg_change(&entry)) {
		D_DEBUG(DB_MD,
			DF_DB ": loaded cfg entry " DF_U64 ": term=%ld type=%s node=" RDB_F_RID
			      "\n",
			DP_DB(db), index, entry.term, rdb_raft_entry_type_str(entry.type),
			RDB_P_RID(rdb_raft_cfg_entry_node_id(&entry, db->d_version)));
	} else {
		D_DEBUG(DB_TRACE,
			DF_DB ": loaded entry " DF_U64 ": term=%ld type=%s buf=%p len=%u\n",
			DP_DB(db), index, entry.term, rdb_raft_entry_type_str(entry.type),
			entry.data.buf, entry.data.len);
	}
	return 0;
}

/* Load the LC and the SLC (if one exists). */
static int
rdb_raft_load_lc(struct rdb *db)
{
	d_iov_t		value;
	uint64_t	i;
	int		rc;

	/* Look up and open the SLC. */
	d_iov_set(&value, &db->d_slc_record, sizeof(db->d_slc_record));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_slc, &value);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_DB": no SLC record\n", DP_DB(db));
		db->d_slc = DAOS_HDL_INVAL;
		goto load_snapshot;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up SLC: "DF_RC"\n", DP_DB(db),
			DP_RC(rc));
		goto err;
	}
	if (uuid_is_null(db->d_slc_record.dlr_uuid)) {
		D_DEBUG(DB_MD, DF_DB": null SLC record\n", DP_DB(db));
		db->d_slc = DAOS_HDL_INVAL;
		goto load_snapshot;
	}
	rc = vos_cont_open(db->d_pool, db->d_slc_record.dlr_uuid, &db->d_slc);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_DB": dangling SLC record: "DF_UUID"\n",
			DP_DB(db), DP_UUID(db->d_slc_record.dlr_uuid));
		db->d_slc = DAOS_HDL_INVAL;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to open SLC "DF_UUID": %d\n", DP_DB(db),
			DP_UUID(db->d_slc_record.dlr_uuid), rc);
		goto err;
	}

load_snapshot:
	/* Load the LC base. */
	rc = rdb_raft_load_snapshot(db);
	if (rc != 0)
		goto err_slc;

	/* Load the log entries. */
	for (i = db->d_lc_record.dlr_base + 1; i < db->d_lc_record.dlr_tail; i++) {
		/*
		 * Yield before loading the first entry and every a few
		 * entries.
		 */
		if ((i - db->d_lc_record.dlr_base - 1) % 64 == 0)
			ABT_thread_yield();
		rc = rdb_raft_load_entry(db, i);
		if (rc != 0)
			goto err_snapshot;
	}

	return 0;

err_snapshot:
	rdb_raft_unload_snapshot(db);
err_slc:
	if (daos_handle_is_valid(db->d_slc))
		vos_cont_close(db->d_slc);
err:
	return rc;
}

static void
rdb_raft_unload_lc(struct rdb *db)
{
	rdb_raft_unload_snapshot(db);
	if (daos_handle_is_valid(db->d_slc))
		vos_cont_close(db->d_slc);
}

static int
rdb_raft_get_election_timeout(void)
{
	char	       *name = "RDB_ELECTION_TIMEOUT";
	unsigned int	default_value = 7000;
	unsigned int	value = default_value;

	d_getenv_uint(name, &value);
	if (value == 0 || value > INT_MAX) {
		D_WARN("%s not in (0, %d] (defaulting to %u)\n", name, INT_MAX, default_value);
		value = default_value;
	}
	return value;
}

static int
rdb_raft_get_request_timeout(void)
{
	char	       *name = "RDB_REQUEST_TIMEOUT";
	unsigned int	default_value = 3000;
	unsigned int	value = default_value;

	d_getenv_uint(name, &value);
	if (value == 0 || value > INT_MAX) {
		D_WARN("%s not in (0, %d] (defaulting to %u)\n", name, INT_MAX, default_value);
		value = default_value;
	}
	return value;
}

static int
rdb_raft_get_lease_maintenance_grace(void)
{
	char	       *name = "RDB_LEASE_MAINTENANCE_GRACE";
	unsigned int	default_value = 7000;
	unsigned int	value = default_value;

	d_getenv_uint(name, &value);
	if (value == 0 || value > INT_MAX) {
		D_WARN("%s not in (0, %d] (defaulting to %u)\n", name, INT_MAX, default_value);
		value = default_value;
	}
	return value;
}

static uint64_t
rdb_raft_get_compact_thres(void)
{
	char	       *name = "RDB_COMPACT_THRESHOLD";
	unsigned int	default_value = 256;
	unsigned int	value = default_value;

	d_getenv_uint(name, &value);
	if (value == 0) {
		D_WARN("%s not in (0, %u] (defaulting to %u)\n", name, UINT_MAX, default_value);
		value = default_value;
	}
	return value;
}

static unsigned int
rdb_raft_get_ae_max_entries(void)
{
	char	       *name = "RDB_AE_MAX_ENTRIES";
	unsigned int	default_value = 32;
	unsigned int	value = default_value;

	d_getenv_uint(name, &value);
	if (value == 0) {
		D_WARN("%s not in (0, %u] (defaulting to %u)\n", name, UINT_MAX, default_value);
		value = default_value;
	}
	return value;
}

static size_t
rdb_raft_get_ae_max_size(void)
{
	char	       *name = "RDB_AE_MAX_SIZE";
	uint64_t	default_value = (1ULL << 20);
	uint64_t	value = default_value;
	int		rc;

	rc = d_getenv_uint64_t(name, &value);
	if ((rc != -DER_NONEXIST && rc != 0) || value == 0) {
		D_WARN("%s not in (0, "DF_U64"] (defaulting to "DF_U64")\n", name, UINT64_MAX,
		       default_value);
		value = default_value;
	}
	return value;
}

/* For the rdb_raft_dictate case. */
static int
rdb_raft_discard_slc(struct rdb *db)
{
	struct rdb_lc_record	slc_record;
	d_iov_t			value;
	int			rc;

	d_iov_set(&value, &slc_record, sizeof(slc_record));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_slc, &value);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_DB": no SLC record\n", DP_DB(db));
		return 0;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up SLC: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		return rc;
	}
	if (uuid_is_null(slc_record.dlr_uuid)) {
		D_DEBUG(DB_MD, DF_DB": null SLC record\n", DP_DB(db));
		return 0;
	}

	return rdb_raft_destroy_lc(db->d_pool, db->d_mc, &rdb_mc_slc, slc_record.dlr_uuid,
				   NULL /* record */);
}

int
rdb_raft_dictate(struct rdb *db)
{
	struct rdb_lc_record      lc_record = db->d_lc_record;
	uint64_t                  term;
	struct rdb_replica_record replicas = {.drr_id = db->d_replica_id};
	d_iov_t                   keys[2];
	d_iov_t                   value;
	uint64_t                  index = lc_record.dlr_tail;
	int                       rc;

	/*
	 * If an SLC exists, discard it, since it must be either stale or
	 * incomplete. See rdb_raft_cb_recv_installsnapshot.
	 */
	rc = rdb_raft_discard_slc(db);
	if (rc != 0)
		return rc;

	/*
	 * Since we don't have an RDB fsck phase yet, do a basic check to avoid
	 * arithmetic issues.
	 */
	if (lc_record.dlr_base >= index) {
		D_ERROR(DF_DB": LC record corrupted: base "DF_U64" >= tail "DF_U64"\n", DP_DB(db),
			lc_record.dlr_base, index);
		return -DER_IO;
	}

	/* Get the term at the last index. */
	if (index - lc_record.dlr_base - 1 > 0) {
		struct rdb_entry header;

		/* The LC has entries. Get from the last entry. */
		d_iov_set(&value, &header, sizeof(header));
		rc = rdb_lc_lookup(db->d_lc, index - 1, RDB_LC_ATTRS, &rdb_lc_entry_header, &value);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to look up entry "DF_U64" header: "DF_RC"\n",
				DP_DB(db), index - 1, DP_RC(rc));
			return rc;
		}
		term = header.dre_term;
	} else {
		/* The LC has no entries. Get from the snapshot. */
		term = lc_record.dlr_base_term;
	}

	/*
	 * At a new index, reset the membership to only ourself. We also punch
	 * the entry header and data just for consistency, for this may be a
	 * membership change entry that, for instance, adds a node other than
	 * ourself, which contradicts with the new membership of only ourself.
	 */
	rc = rdb_raft_store_replicas(db->d_uuid, db->d_lc, index, db->d_version, &replicas,
				     1 /* replicas_len */, NULL /* vtx */);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to reset membership", DP_DB(db));
		return rc;
	}
	keys[0] = rdb_lc_entry_header;
	keys[1] = rdb_lc_entry_data;
	rc = rdb_lc_punch(db->d_lc, index, RDB_LC_ATTRS, 2 /* n */, keys, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to punch entry: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		return rc;
	}

	/*
	 * Update the LC base and tail. Note that, if successful, this
	 * "publishes" all the modifications above and effectively commits all
	 * entries.
	 */
	lc_record.dlr_base = index;
	lc_record.dlr_base_term = term;
	lc_record.dlr_tail = index + 1;
	d_iov_set(&value, &lc_record, sizeof(lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc, &value, NULL /* vtx */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update LC record: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		return rc;
	}
	D_INFO(DF_DB": updated LC reocrd: base="DF_U64"->"DF_U64" base_term="DF_U64"->"DF_U64
	       " tail="DF_U64"->"DF_U64"\n", DP_DB(db), db->d_lc_record.dlr_base,
	       lc_record.dlr_base, db->d_lc_record.dlr_base_term, lc_record.dlr_base_term,
	       db->d_lc_record.dlr_tail, lc_record.dlr_tail);
	db->d_lc_record = lc_record;

	return 0;
}

int
rdb_raft_open(struct rdb *db, uint64_t caller_term)
{
	int rc;

	D_INIT_LIST_HEAD(&db->d_requests);
	D_INIT_LIST_HEAD(&db->d_replies);
	db->d_compact_thres = rdb_raft_get_compact_thres();
	db->d_ae_max_size = rdb_raft_get_ae_max_size();
	db->d_ae_max_entries = rdb_raft_get_ae_max_entries();

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 4 /* bits */,
					 NULL /* priv */,
					 &rdb_raft_result_hash_ops,
					 &db->d_results);
	if (rc != 0)
		goto err;

	rc = ABT_cond_create(&db->d_applied_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create applied CV: %d\n", DP_DB(db),
			rc);
		rc = dss_abterr2der(rc);
		goto err_results;
	}

	rc = ABT_cond_create(&db->d_events_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create events CV: %d\n", DP_DB(db),
			rc);
		rc = dss_abterr2der(rc);
		goto err_applied_cv;
	}

	rc = ABT_cond_create(&db->d_replies_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create replies CV: %d\n", DP_DB(db),
			rc);
		rc = dss_abterr2der(rc);
		goto err_events_cv;
	}

	rc = ABT_cond_create(&db->d_compact_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create compact CV: %d\n", DP_DB(db),
			rc);
		rc = dss_abterr2der(rc);
		goto err_replies_cv;
	}

	rc = ABT_cond_create(&db->d_compacted_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create compacted CV: %d\n", DP_DB(db),
			rc);
		rc = dss_abterr2der(rc);
		goto err_compact_cv;
	}

	if (caller_term != RDB_NIL_TERM) {
		uint64_t	term;
		d_iov_t		value;

		d_iov_set(&value, &term, sizeof(term));
		rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_term, &value);
		if (rc == -DER_NONEXIST)
			term = 0;
		else if (rc != 0)
			goto err_compacted_cv;

		if (caller_term < term) {
			D_DEBUG(DB_MD, DF_DB": stale caller term: "DF_X64" < "DF_X64"\n", DP_DB(db),
				caller_term, term);
			rc = -DER_STALE;
			goto err_compacted_cv;
		} else if (caller_term > term) {
			D_DEBUG(DB_MD, DF_DB": updating term: "DF_X64" -> "DF_X64"\n", DP_DB(db),
				term, caller_term);
			d_iov_set(&value, &caller_term, sizeof(caller_term));
			rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_term, &value,
					   NULL /* vtx */);
			if (rc != 0)
				goto err_compacted_cv;
		}
	}

	rc = rdb_raft_open_lc(db);
	if (rc != 0)
		goto err_compacted_cv;

	return 0;

err_compacted_cv:
	ABT_cond_free(&db->d_compacted_cv);
err_compact_cv:
	ABT_cond_free(&db->d_compact_cv);
err_replies_cv:
	ABT_cond_free(&db->d_replies_cv);
err_events_cv:
	ABT_cond_free(&db->d_events_cv);
err_applied_cv:
	ABT_cond_free(&db->d_applied_cv);
err_results:
	d_hash_table_destroy_inplace(&db->d_results, true /* force */);
err:
	return rc;
}

void
rdb_raft_close(struct rdb *db)
{
	D_ASSERT(db->d_raft == NULL);
	rdb_raft_close_lc(db);
	ABT_cond_free(&db->d_compacted_cv);
	ABT_cond_free(&db->d_compact_cv);
	ABT_cond_free(&db->d_replies_cv);
	ABT_cond_free(&db->d_events_cv);
	ABT_cond_free(&db->d_applied_cv);
	d_hash_table_destroy_inplace(&db->d_results, true /* force */);
}

/*
 * Load raft persistent state, if any. Our raft callbacks must be registered
 * already, because rdb_raft_cb_notify_membership_event is required. We use
 * db->d_raft_loaded to instruct some of our raft callbacks to avoid
 * unnecessary write I/Os.
 */
static int
rdb_raft_load(struct rdb *db)
{
	d_iov_t          value;
	uint64_t         term;
	rdb_replica_id_t vote;
	int              rc;

	D_DEBUG(DB_MD, DF_DB": load persistent state: begin\n", DP_DB(db));
	D_ASSERT(!db->d_raft_loaded);

	d_iov_set(&value, &term, sizeof(term));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_term, &value);
	if (rc == 0) {
		rc = raft_set_current_term(db->d_raft, term);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	} else if (rc == -DER_NONEXIST) {
		term = 0;
	} else {
		goto out;
	}

	rdb_set_mc_vote_lookup_buf(db->d_version, &vote, &value);
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_vote, &value);
	if (rc == 0) {
		rc = raft_vote_for_nodeid(db->d_raft, rdb_replica_id_encode(vote));
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	} else if (rc == -DER_NONEXIST) {
		vote.rri_rank = -1;
		vote.rri_gen  = -1;
	} else {
		goto out;
	}

	rc = rdb_raft_load_lc(db);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_MD,
		DF_DB ": term=" DF_U64 " vote=" RDB_F_RID " lc.uuid=" DF_UUID " lc.base=" DF_U64
		      " lc.base_term=" DF_U64 " lc.tail=" DF_U64 " lc.aggregated=" DF_U64
		      " lc.term=" DF_U64 " lc.seq=" DF_U64 "\n",
		DP_DB(db), term, RDB_P_RID(vote), DP_UUID(db->d_lc_record.dlr_uuid),
		db->d_lc_record.dlr_base, db->d_lc_record.dlr_base_term, db->d_lc_record.dlr_tail,
		db->d_lc_record.dlr_aggregated, db->d_lc_record.dlr_term, db->d_lc_record.dlr_seq);

	db->d_raft_loaded = true;
out:
	D_DEBUG(DB_MD, DF_DB": load persistent state: end: "DF_RC"\n", DP_DB(db), DP_RC(rc));
	return rc;
}

static void
rdb_raft_unload(struct rdb *db)
{
	D_ASSERT(db->d_raft_loaded);
	rdb_raft_unload_lc(db);
	db->d_raft_loaded = false;
}

int
rdb_raft_start(struct rdb *db)
{
	int	election_timeout;
	int	request_timeout;
	int	lease_maintenance_grace;
	int	rc;

	D_ASSERT(db->d_raft == NULL);
	D_ASSERT(db->d_stop == false);

	db->d_raft = raft_new();
	if (db->d_raft == NULL) {
		D_ERROR(DF_DB": failed to create raft object\n", DP_DB(db));
		rc = -DER_NOMEM;
		goto err;
	}

	raft_set_nodeid(db->d_raft, rdb_replica_id_encode(db->d_replica_id));
	if (db->d_new)
		raft_set_first_start(db->d_raft);
	raft_set_callbacks(db->d_raft, &rdb_raft_cbs, db);

	rc = rdb_raft_load(db);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to load raft persistent state\n", DP_DB(db));
		goto err_raft;
	}

	election_timeout = rdb_raft_get_election_timeout();
	request_timeout = rdb_raft_get_request_timeout();
	lease_maintenance_grace = rdb_raft_get_lease_maintenance_grace();
	raft_set_election_timeout(db->d_raft, election_timeout);
	raft_set_request_timeout(db->d_raft, request_timeout);
	raft_set_lease_maintenance_grace(db->d_raft, lease_maintenance_grace);

	rc = dss_ult_create(rdb_recvd, db, DSS_XS_SELF, 0, 0, &db->d_recvd);
	if (rc != 0)
		goto err_raft_state;
	rc = dss_ult_create(rdb_timerd, db, DSS_XS_SELF, 0, 0, &db->d_timerd);
	if (rc != 0)
		goto err_recvd;
	rc = dss_ult_create(rdb_callbackd, db, DSS_XS_SELF, 0, 0,
			    &db->d_callbackd);
	if (rc != 0)
		goto err_timerd;
	rc = dss_ult_create(rdb_compactd, db, DSS_XS_SELF, 0, 0,
			    &db->d_compactd);
	if (rc != 0)
		goto err_callbackd;

	return 0;

err_callbackd:
	db->d_stop = true;
	ABT_cond_broadcast(db->d_events_cv);
	rc = ABT_thread_free(&db->d_callbackd);
	D_ASSERTF(rc == 0, "free rdb_callbackd: "DF_RC"\n", DP_RC(rc));
err_timerd:
	db->d_stop = true;
	rc = ABT_thread_free(&db->d_timerd);
	D_ASSERTF(rc == 0, "free rdb_timerd: "DF_RC"\n", DP_RC(rc));
err_recvd:
	db->d_stop = true;
	ABT_cond_broadcast(db->d_replies_cv);
	rc = ABT_thread_free(&db->d_recvd);
	D_ASSERTF(rc == 0, "free rdb_recvd: "DF_RC"\n", DP_RC(rc));
	db->d_stop = false;
err_raft_state:
	rdb_raft_unload(db);
err_raft:
	raft_free(db->d_raft);
	db->d_raft = NULL;
err:
	return rc;
}

void
rdb_raft_stop(struct rdb *db)
{
	int rc;

	D_ASSERT(db->d_raft != NULL);

	/* Stop sending any new RPCs. */
	db->d_stop = true;

	/* Wake up all daemons and TXs. */
	ABT_mutex_lock(db->d_raft_mutex);
	ABT_cond_broadcast(db->d_applied_cv);
	ABT_cond_broadcast(db->d_events_cv);
	ABT_cond_broadcast(db->d_compact_cv);
	ABT_mutex_unlock(db->d_raft_mutex);

	ABT_mutex_lock(db->d_mutex);
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
	rc = ABT_thread_free(&db->d_compactd);
	D_ASSERTF(rc == 0, "free rdb_compactd: "DF_RC"\n", DP_RC(rc));
	rc = ABT_thread_free(&db->d_callbackd);
	D_ASSERTF(rc == 0, "free rdb_callbackd: "DF_RC"\n", DP_RC(rc));
	rc = ABT_thread_free(&db->d_timerd);
	D_ASSERTF(rc == 0, "free rdb_timerd: "DF_RC"\n", DP_RC(rc));
	rc = ABT_thread_free(&db->d_recvd);
	D_ASSERTF(rc == 0, "free rdb_recvd: "DF_RC"\n", DP_RC(rc));

	rdb_raft_unload(db);
	raft_free(db->d_raft);
	db->d_raft = NULL;

	/* Restore this flag as we've finished stopping the DB. */
	db->d_stop = false;
}

/* Resign the leadership in term. */
void
rdb_raft_resign(struct rdb *db, uint64_t term)
{
	struct rdb_raft_state	state;
	int			rc;

	ABT_mutex_lock(db->d_raft_mutex);
	if (term != raft_get_current_term(db->d_raft) ||
	    !raft_is_leader(db->d_raft)) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return;
	}

	D_INFO(DF_DB ": resigning from term " DF_U64 "\n", DP_DB(db), term);
	rdb_raft_save_state(db, &state);
	raft_become_follower(db->d_raft);
	rc = rdb_raft_check_state(db, &state, 0 /* raft_rc */);
	ABT_mutex_unlock(db->d_raft_mutex);
	D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
}

/* Call a new election (campaign to be leader) by a voting follower. */
int
rdb_raft_campaign(struct rdb *db)
{
	raft_node_t	       *node;
	struct rdb_raft_state	state;
	int			rc;

	ABT_mutex_lock(db->d_raft_mutex);

	if (!raft_is_follower(db->d_raft)) {
		D_DEBUG(DB_MD, DF_DB": already candidate or leader\n", DP_DB(db));
		rc = 0;
		goto out_mutex;
	}

	node = raft_get_my_node(db->d_raft);
	if (node == NULL || !raft_node_is_voting(node)) {
		D_DEBUG(DB_MD, DF_DB": must be voting node\n", DP_DB(db));
		rc = -DER_NO_PERM;
		goto out_mutex;
	}

	D_DEBUG(DB_MD, DF_DB": calling election from current term %ld\n", DP_DB(db),
		raft_get_current_term(db->d_raft));
	rdb_raft_save_state(db, &state);
	rc = raft_election_start(db->d_raft);
	rc = rdb_raft_check_state(db, &state, rc);

out_mutex:
	ABT_mutex_unlock(db->d_raft_mutex);
	return rc;
}

int
rdb_raft_ping(struct rdb *db, uint64_t caller_term)
{
	msg_appendentries_t		ae = {.term = caller_term};
	msg_appendentries_response_t	ae_resp;
	struct rdb_raft_state		state;
	int				rc;

	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_appendentries(db->d_raft, NULL /* node */, &ae, &ae_resp);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0)
		return rc;

	if (caller_term < ae_resp.term) {
		D_DEBUG(DB_MD, DF_DB": stale caller term: "DF_X64" < "DF_X64"\n", DP_DB(db),
			caller_term, ae_resp.term);
		return -DER_STALE;
	}

	return 0;
}

/* Wait for index to be applied in term. For leaders only.
 * Caller initially holds d_raft_mutex.
 */
int
rdb_raft_wait_applied(struct rdb *db, uint64_t index, uint64_t term)
{
	int	rc = 0;

	D_DEBUG(DB_TRACE, DF_DB": waiting for entry "DF_U64" to be applied\n",
		DP_DB(db), index);
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
		ABT_cond_wait(db->d_applied_cv, db->d_raft_mutex);
	}
	return rc;
}

static int
rdb_replica_id_compare_void(const void *vx, const void *vy)
{
	const rdb_replica_id_t *x = vx;
	const rdb_replica_id_t *y = vy;

	return rdb_replica_id_compare(*x, *y);
}

int
rdb_raft_get_replicas(struct rdb *db, rdb_replica_id_t **replicas_out, int *replicas_len_out)
{
	rdb_replica_id_t *replicas;
	int               n;
	int               i;
	int               rc;

	ABT_mutex_lock(db->d_raft_mutex);

	n = raft_get_num_nodes(db->d_raft);

	D_ALLOC_ARRAY(replicas, n);
	if (replicas == NULL) {
		rc = -DER_NOMEM;
		goto mutex;
	}

	for (i = 0; i < n; i++) {
		raft_node_t   *node    = raft_get_node_from_idx(db->d_raft, i);
		raft_node_id_t node_id = raft_node_get_id(node);

		replicas[i] = rdb_replica_id_decode(node_id);
	}

	qsort(replicas, n, sizeof(*replicas), rdb_replica_id_compare_void);

	*replicas_out     = replicas;
	*replicas_len_out = n;
	rc = 0;
mutex:
	ABT_mutex_unlock(db->d_raft_mutex);
	return rc;
}

static int
rdb_lookup_for_request(struct rdb_op_in *in, struct rdb **db_out)
{
	struct rdb *db;

	db = rdb_lookup(in->ri_uuid);
	if (db == NULL)
		return -DER_NONEXIST;

	if (db->d_stop) {
		rdb_put(db);
		return -DER_CANCELED;
	}

	if (rdb_replica_id_compare(db->d_replica_id, in->ri_to) != 0) {
		D_DEBUG(DB_MD, DF_DB ": replica ID mismatch: self=" RDB_F_RID " to=" RDB_F_RID "\n",
			DP_DB(db), RDB_P_RID(db->d_replica_id), RDB_P_RID(in->ri_to));
		rdb_put(db);
		return -DER_BAD_TARGET;
	}

	*db_out = db;
	return 0;
}

void
rdb_requestvote_handler(crt_rpc_t *rpc)
{
	struct rdb_requestvote_in      *in = crt_req_get(rpc);
	struct rdb_requestvote_out     *out = crt_reply_get(rpc);
	struct rdb		       *db;
	char			       *s;
	struct rdb_raft_state           state;
	raft_node_id_t                  node_id = rdb_replica_id_encode(in->rvi_op.ri_from);
	int				rc;

	s = in->rvi_msg.prevote ? " (prevote)" : "";

	rc = rdb_lookup_for_request(&in->rvi_op, &db);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_TRACE, DF_DB ": handling raft rv%s from " RDB_F_RID "\n", DP_DB(db), s,
		RDB_P_RID(in->rvi_op.ri_from));
	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_requestvote(db->d_raft, raft_get_node(db->d_raft, node_id), &in->rvi_msg,
				   &out->rvo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to process REQUESTVOTE%s from " RDB_F_RID, DP_DB(db),
			 s, RDB_P_RID(in->rvi_op.ri_from));
		/* raft_recv_requestvote() always generates a valid reply. */
		rc = 0;
	}

	rdb_put(db);
out:
	out->rvo_op.ro_rc = rc;
	out->rvo_op.ro_from = in->rvi_op.ri_to;
	out->rvo_op.ro_to   = in->rvi_op.ri_from;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": failed to send REQUESTVOTE%s reply to " RDB_F_RID,
			 DP_UUID(in->rvi_op.ri_uuid), s, RDB_P_RID(in->rvi_op.ri_from));
}

void
rdb_appendentries_handler(crt_rpc_t *rpc)
{
	struct rdb_appendentries_in    *in = crt_req_get(rpc);
	struct rdb_appendentries_out   *out = crt_reply_get(rpc);
	struct rdb		       *db;
	struct rdb_raft_state           state;
	raft_node_id_t                  node_id = rdb_replica_id_encode(in->aei_op.ri_from);
	int				rc;

	rc = rdb_lookup_for_request(&in->aei_op, &db);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_TRACE, DF_DB ": handling raft ae from " RDB_F_RID "\n", DP_DB(db),
		RDB_P_RID(in->aei_op.ri_from));
	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_appendentries(db->d_raft, raft_get_node(db->d_raft, node_id), &in->aei_msg,
				     &out->aeo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to process APPENDENTRIES from " RDB_F_RID, DP_DB(db),
			 RDB_P_RID(in->aei_op.ri_from));
		/* raft_recv_appendentries() always generates a valid reply. */
		rc = 0;
	}

	rdb_put(db);
out:
	out->aeo_op.ro_rc = rc;
	out->aeo_op.ro_from = in->aei_op.ri_to;
	out->aeo_op.ro_to   = in->aei_op.ri_from;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": failed to send APPENDENTRIES reply to " RDB_F_RID,
			 DP_UUID(in->aei_op.ri_uuid), RDB_P_RID(in->aei_op.ri_from));
}

void
rdb_installsnapshot_handler(crt_rpc_t *rpc)
{
	struct rdb_installsnapshot_in  *in = crt_req_get(rpc);
	struct rdb_installsnapshot_out *out = crt_reply_get(rpc);
	struct rdb		       *db;
	struct rdb_raft_state           state;
	raft_node_id_t                  node_id = rdb_replica_id_encode(in->isi_op.ri_from);
	int				rc;

	rc = rdb_lookup_for_request(&in->isi_op, &db);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_TRACE, DF_DB ": handling raft is from " RDB_F_RID "\n", DP_DB(db),
		RDB_P_RID(in->isi_op.ri_from));

	/* Receive the bulk data buffers before entering raft. */
	rc = rdb_raft_recv_is(db, rpc, &in->isi_local.rl_kds_iov,
			      &in->isi_local.rl_data_iov);
	if (rc != 0) {
		DL_ERROR(rc,
			 DF_DB ": failed to receive INSTALLSNAPSHOT chunk %ld"
			       "/" DF_U64 " from " RDB_F_RID,
			 DP_DB(db), in->isi_msg.last_idx, in->isi_seq,
			 RDB_P_RID(in->isi_op.ri_from));
		goto out_db;
	}

	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_installsnapshot(db->d_raft, raft_get_node(db->d_raft, node_id), &in->isi_msg,
				       &out->iso_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		DL_ERROR(rc, DF_DB ": failed to process INSTALLSNAPSHOT from " RDB_F_RID, DP_DB(db),
			 RDB_P_RID(in->isi_op.ri_from));
		/*
		 * raft_recv_installsnapshot() always generates a valid reply.
		 */
		rc = 0;
	}

	D_FREE(in->isi_local.rl_data_iov.iov_buf);
	D_FREE(in->isi_local.rl_kds_iov.iov_buf);
out_db:
	rdb_put(db);
out:
	out->iso_op.ro_rc = rc;
	out->iso_op.ro_from = in->isi_op.ri_to;
	out->iso_op.ro_to   = in->isi_op.ri_from;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": failed to send INSTALLSNAPSHOT reply to " RDB_F_RID,
			 DP_UUID(in->isi_op.ri_uuid), RDB_P_RID(in->isi_op.ri_from));
}

void
rdb_raft_process_reply(struct rdb *db, crt_rpc_t *rpc)
{
	struct rdb_raft_state		state;
	crt_opcode_t			opc = opc_get(rpc->cr_opc);
	void			       *out = crt_reply_get(rpc);
	struct rdb_requestvote_out     *out_rv;
	struct rdb_appendentries_out   *out_ae;
	struct rdb_installsnapshot_out *out_is;
	struct rdb_op_out              *out_op = out;
	raft_node_t		       *node;
	raft_time_t		       *lease = NULL;
	int				rc;

	if (rdb_replica_id_compare(db->d_replica_id, out_op->ro_to) != 0) {
		D_DEBUG(DB_MD,
			DF_DB ": replica ID mismatch: self=" RDB_F_RID " to=" RDB_F_RID " opc=%u\n",
			DP_DB(db), RDB_P_RID(db->d_replica_id), RDB_P_RID(out_op->ro_to), opc);
		return;
	}

	rc = out_op->ro_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_DB": opc %u failed: %d\n", DP_DB(db), opc,
			rc);
		return;
	}

	/*
	 * If this is an AE or IS response, adjust the lease expiration time
	 * for clock offsets among replicas.
	 */
	switch (opc) {
	case RDB_APPENDENTRIES:
		out_ae = out;
		lease = &out_ae->aeo_msg.lease;
		break;
	case RDB_INSTALLSNAPSHOT:
		out_is = out;
		lease = &out_is->iso_msg.lease;
		break;
	}
	if (lease != NULL) {
		int adjustment = d_hlc2msec(d_hlc_epsilon_get()) + 1 /* ms margin */;

		if (*lease < adjustment) {
			D_ERROR(DF_DB ": dropping %s response from " RDB_F_RID
				      ": invalid lease: %ld\n",
				DP_DB(db), opc == RDB_APPENDENTRIES ? "AE" : "IS",
				RDB_P_RID(out_op->ro_from), *lease);
			return;
		}
		*lease -= adjustment;
	}

	ABT_mutex_lock(db->d_raft_mutex);

	node = raft_get_node(db->d_raft, rdb_replica_id_encode(out_op->ro_from));
	if (node == NULL) {
		D_DEBUG(DB_MD, DF_DB ": " RDB_F_RID " not in current membership\n", DP_DB(db),
			RDB_P_RID(out_op->ro_from));
		goto out_mutex;
	}

	rdb_raft_save_state(db, &state);
	switch (opc) {
	case RDB_REQUESTVOTE:
		out_rv = out;
		rc = raft_recv_requestvote_response(db->d_raft, node, &out_rv->rvo_msg);
		break;
	case RDB_APPENDENTRIES:
		out_ae = out;
		rc = raft_recv_appendentries_response(db->d_raft, node, &out_ae->aeo_msg);
		break;
	case RDB_INSTALLSNAPSHOT:
		out_is = out;
		rc = raft_recv_installsnapshot_response(db->d_raft, node, &out_is->iso_msg);
		break;
	default:
		D_ASSERTF(0, DF_DB": unexpected opc: %u\n", DP_DB(db), opc);
	}
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0 && rc != -DER_NOTLEADER)
		DL_ERROR(rc, DF_DB ": failed to process opc %u response from " RDB_F_RID, DP_DB(db),
			 opc, RDB_P_RID(out_op->ro_from));

out_mutex:
	ABT_mutex_unlock(db->d_raft_mutex);
}

/* The buffer belonging to bulk must a single d_iov_t. */
static void
rdb_raft_free_bulk_and_buffer(crt_bulk_t bulk)
{
	d_iov_t	iov;
	d_sg_list_t	sgl;
	int		rc;

	/* Save the buffer address in iov.iov_buf. */
	d_iov_set(&iov, NULL /* buf */, 0 /* size */);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;
	rc = crt_bulk_access(bulk, &sgl);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	D_ASSERTF(sgl.sg_nr_out == 1, "%d\n", sgl.sg_nr_out);
	D_ASSERT(iov.iov_buf != NULL);

	/* Free the bulk. */
	crt_bulk_free(bulk);

	/* Free the buffer. */
	D_FREE(iov.iov_buf);
}

/* Free any additional memory we allocated for the request. */
void
rdb_raft_free_request(struct rdb *db, crt_rpc_t *rpc)
{
	crt_opcode_t			opc = opc_get(rpc->cr_opc);
	struct rdb_appendentries_in    *in_ae;
	struct rdb_installsnapshot_in  *in_is;

	switch (opc) {
	case RDB_REQUESTVOTE:
		/* Nothing to do. */
		break;
	case RDB_APPENDENTRIES:
		in_ae = crt_req_get(rpc);
		rdb_raft_fini_ae(&in_ae->aei_msg);
		break;
	case RDB_INSTALLSNAPSHOT:
		in_is = crt_req_get(rpc);
		rdb_raft_free_bulk_and_buffer(in_is->isi_data);
		rdb_raft_free_bulk_and_buffer(in_is->isi_kds);
		break;
	default:
		D_ASSERTF(0, DF_DB": unexpected opc: %u\n", DP_DB(db), opc);
	}
}

void
rdb_raft_module_init(void)
{
	raft_set_log_level(RAFT_LOG_DEBUG);
}

void
rdb_raft_module_fini(void)
{
}