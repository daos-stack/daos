/*
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rdb: Raft Integration
 *
 * Each replica employs four daemon ULTs:
 *
 *   ~ rdb_timerd(): Call raft_periodic() periodically.
 *   ~ rdb_recvd(): Process RPC replies received.
 *   ~ rdb_callbackd(): Invoke user dc_step_{up,down} callbacks.
 *   ~ rdb_compactd(): Compact polled entries by calling rdb_lc_aggregate().
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
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	char			       *s = msg->prevote ? " (prevote)" : "";
	crt_rpc_t		       *rpc;
	struct rdb_requestvote_in      *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_TRACE, DF_DB": sending rv%s to node %d rank %u: term=%ld\n",
		DP_DB(db), s, raft_node_get_id(node), rdb_node->dn_rank,
		msg->term);

	rc = rdb_create_raft_rpc(RDB_REQUESTVOTE, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create RV%s RPC to node %d: %d\n",
			DP_DB(db), s, raft_node_get_id(node), rc);
		return rc;
	}
	in = crt_req_get(rpc);
	uuid_copy(in->rvi_op.ri_uuid, db->d_uuid);
	in->rvi_msg = *msg;

	rc = rdb_send_raft_rpc(rpc, db);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to send RV%s RPC to node %d: %d\n",
			DP_DB(db), s, raft_node_get_id(node), rc);
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
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	crt_rpc_t		       *rpc;
	struct rdb_appendentries_in    *in;
	int				rc;

	D_ASSERT(db->d_raft == raft);
	D_DEBUG(DB_TRACE, DF_DB": sending ae to node %u rank %u: term=%ld\n",
		DP_DB(db), raft_node_get_id(node), rdb_node->dn_rank,
		msg->term);

	if (DAOS_FAIL_CHECK(DAOS_RDB_SKIP_APPENDENTRIES_FAIL))
		D_GOTO(err, rc = 0);

	rc = rdb_create_raft_rpc(RDB_APPENDENTRIES, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create AE RPC to node %d: %d\n",
			DP_DB(db), raft_node_get_id(node), rc);
		D_GOTO(err, rc);
	}
	in = crt_req_get(rpc);
	uuid_copy(in->aei_op.ri_uuid, db->d_uuid);
	rc = rdb_raft_clone_ae(db, msg, &in->aei_msg);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to allocate entry array\n", DP_DB(db));
		D_GOTO(err_rpc, rc);
	}

	rc = rdb_send_raft_rpc(rpc, db);
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
rdb_raft_store_replicas(daos_handle_t lc, uint64_t index, const d_rank_list_t *replicas)
{
	d_iov_t	keys[2];
	d_iov_t	vals[2];
	uint8_t	nreplicas;

	D_ASSERTF(replicas->rl_nr <= UINT8_MAX, "nreplicas = %u",
		  replicas->rl_nr);
	nreplicas = replicas->rl_nr;
	keys[0] = rdb_lc_nreplicas;
	d_iov_set(&vals[0], &nreplicas, sizeof(nreplicas));
	keys[1] = rdb_lc_replicas;
	d_iov_set(&vals[1], replicas->rl_ranks,
		  sizeof(*replicas->rl_ranks) * nreplicas);
	return rdb_lc_update(lc, index, RDB_LC_ATTRS, true /* crit */,
			     2 /* n */, keys, vals);
}

static int
rdb_raft_load_replicas(daos_handle_t lc, uint64_t index, d_rank_list_t **replicas)
{
	d_iov_t		value;
	uint8_t		nreplicas;
	d_rank_list_t  *r;
	int		rc;

	d_iov_set(&value, &nreplicas, sizeof(nreplicas));
	rc = rdb_lc_lookup(lc, index, RDB_LC_ATTRS, &rdb_lc_nreplicas, &value);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, "no replicas in "DF_U64"\n", index);
		rc = 0;
		nreplicas = 0;
	} else if (rc != 0) {
		return rc;
	}

	r = daos_rank_list_alloc(nreplicas);
	if (r == NULL)
		return -DER_NOMEM;

	if (nreplicas > 0) {
		d_iov_set(&value, r->rl_ranks, sizeof(*r->rl_ranks) * nreplicas);
		rc = rdb_lc_lookup(lc, index, RDB_LC_ATTRS, &rdb_lc_replicas, &value);
		if (rc != 0) {
			d_rank_list_free(r);
			return rc;
		}
	}

	*replicas = r;
	return 0;
}

/* Caller must hold d_raft_mutex. */
static int
rdb_raft_add_node(struct rdb *db, d_rank_t rank)
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
	dnode->dn_rank = rank;
	node = raft_add_node(db->d_raft, dnode, rank, rank == dss_self_rank());
	if (node == NULL) {
		D_ERROR(DF_DB": failed to add node %u\n", DP_DB(db), rank);
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
	d_rank_list_t  *replicas;
	int		i;
	int		rc;

	D_DEBUG(DB_MD, DF_DB": loading snapshot: base="DF_U64" term="DF_U64"\n",
		DP_DB(db), db->d_lc_record.dlr_base,
		db->d_lc_record.dlr_base_term);

	/*
	 * Load the replicas first to minimize the chance of an error happening
	 * after the raft_begin_load_snapshot call, which removes all nodes in
	 * raft.
	 */
	rc = rdb_raft_load_replicas(db->d_lc, db->d_lc_record.dlr_base, &replicas);
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
	for (i = 0; i < replicas->rl_nr; i++) {
		rc = rdb_raft_add_node(db, replicas->rl_ranks[i]);
		/* TODO: Freeze and shut down db. */
		D_ASSERTF(rc == 0, "failed to add node: "DF_RC"\n", DP_RC(rc));
	}

	rc = raft_end_load_snapshot(db->d_raft);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

out_replicas:
	d_rank_list_free(replicas);
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
	d_sg_list_t		sgl;
	struct dss_enum_arg	arg = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	vos_iter_param_t	param = { 0 };
	int			rc;

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
	rc = dss_enum_pack(&param, VOS_ITER_OBJ, true, &anchors, &arg,
			   vos_iterate, NULL /* dth */);
	if (rc < 0)
		return rc;

	/*
	 * Report the new anchor. When rc == 0, dss_enum_pack doesn't guarantee
	 * all the anchors to be EOF.
	 */
	if (rc == 0)
		rdb_anchor_set_eof(anchor);
	else /* rc == 1 */
		rdb_anchor_from_hashes(anchor, &anchors.ia_obj,
				       &anchors.ia_dkey, &anchors.ia_akey,
				       &anchors.ia_ev, &anchors.ia_sv);

	/* Report the buffer lengths. data.iov_len is set by dss_enum_pack. */
	kds->iov_len = sizeof(*arg.kds) * arg.kds_len;

	return 0;
}

static int
rdb_raft_cb_send_installsnapshot(raft_server_t *raft, void *arg,
				 raft_node_t *node, msg_installsnapshot_t *msg)
{
	struct rdb		       *db = arg;
	struct rdb_raft_node	       *rdb_node = raft_node_get_udata(node);
	struct rdb_raft_is	       *is = &rdb_node->dn_is;
	crt_rpc_t		       *rpc;
	struct rdb_installsnapshot_in  *in;
	d_iov_t			kds;
	d_iov_t			data;
	d_sg_list_t			sgl;
	struct dss_module_info	       *info = dss_get_module_info();
	int				rc;

	rc = rdb_create_raft_rpc(RDB_INSTALLSNAPSHOT, node, &rpc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to create IS RPC to rank %u: %d\n",
			DP_DB(db), rdb_node->dn_rank, rc);
		goto err;
	}

	/* Start filling the request. */
	in = crt_req_get(rpc);
	uuid_copy(in->isi_op.ri_uuid, db->d_uuid);
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
		DF_DB": sent is to node %u rank %u: term=%ld last_idx=%ld seq="
		DF_U64" kds.len="DF_U64" data.len="DF_U64"\n",
		DP_DB(db), raft_node_get_id(node), rdb_node->dn_rank,
		in->isi_msg.term, in->isi_msg.last_idx, in->isi_seq,
		kds.iov_len, data.iov_len);
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
rdb_raft_exec_unpack_io(struct dss_enum_unpack_io *io, void *arg)
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

	return dss_enum_unpack(invalid_oid, kds_iov->iov_buf,
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
		rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 2 /* n */, keys,
				   values);
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
		rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */,
				   &rdb_mc_slc, &values[0]);
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
	struct rdb     *db = arg;
	d_iov_t		value;
	int		rc;

	if (!db->d_raft_loaded)
		return 0;

	d_iov_set(&value, &vote, sizeof(vote));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_vote,
			   &value);
	if (rc != 0)
		D_ERROR(DF_DB": failed to persist vote %d: %d\n", DP_DB(db),
			vote, rc);

	return rc;
}

static int
rdb_raft_cb_persist_term(raft_server_t *raft, void *arg, raft_term_t term,
			 raft_node_id_t vote)
{
	struct rdb     *db = arg;
	d_iov_t		keys[2];
	d_iov_t		values[2];
	int		rc;

	if (!db->d_raft_loaded)
		return 0;

	/* Update rdb_mc_term and rdb_mc_vote atomically. */
	keys[0] = rdb_mc_term;
	d_iov_set(&values[0], &term, sizeof(term));
	keys[1] = rdb_mc_vote;
	d_iov_set(&values[1], &vote, sizeof(vote));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 2 /* n */, keys, values);
	if (rc != 0)
		D_ERROR(DF_DB": failed to update term %ld and vote %d: %d\n",
			DP_DB(db), term, vote, rc);

	return rc;
}

static d_rank_t
rdb_raft_cfg_entry_rank(raft_entry_t *entry)
{
	D_ASSERT(entry->data.buf != NULL);
	D_ASSERTF(entry->data.len == sizeof(d_rank_t), "%u\n", entry->data.len);
	return *((d_rank_t *)entry->data.buf);
}

static int
rdb_raft_update_node(struct rdb *db, uint64_t index, raft_entry_t *entry)
{
	d_rank_list_t  *replicas;
	d_rank_t	rank = rdb_raft_cfg_entry_rank(entry);
	bool		found;
	void	       *result;
	int		rc;

	D_DEBUG(DB_MD, DF_DB": cfg entry "DF_U64": term=%ld type=%s rank=%u\n", DP_DB(db), index,
		entry->term, rdb_raft_entry_type_str(entry->type), rank);

	rc = rdb_raft_load_replicas(db->d_lc, index, &replicas);
	if (rc != 0)
		goto out;

	found = d_rank_list_find(replicas, rank, NULL);
	if (found && entry->type == RAFT_LOGTYPE_ADD_NODE) {
		D_WARN(DF_DB": %s: rank %u already exists\n", DP_DB(db),
		       rdb_raft_entry_type_str(entry->type), rank);
		rc = 0;
		goto out_replicas;
	} else if (!found && entry->type == RAFT_LOGTYPE_REMOVE_NODE) {
		D_WARN(DF_DB": %s: rank %u does not exist\n", DP_DB(db),
		       rdb_raft_entry_type_str(entry->type), rank);
		rc = 0;
		goto out_replicas;
	}

	if (entry->type == RAFT_LOGTYPE_ADD_NODE)
		rc = d_rank_list_append(replicas, rank);
	else if (entry->type == RAFT_LOGTYPE_REMOVE_NODE)
		rc = d_rank_list_del(replicas, rank);
	if (rc != 0)
		goto out_replicas;

	/*
	 * Since this is one VOS operation, we don't need to call
	 * rdb_lc_discard upon an error.
	 */
	rc = rdb_raft_store_replicas(db->d_lc, index, replicas);

out_replicas:
	d_rank_list_free(replicas);
out:
	result = rdb_raft_lookup_result(db, index);
	if (result != NULL)
		*(int *)result = rc;
	if (rc != 0)
		D_ERROR(DF_DB": failed to perform %s on rank %u at index "DF_U64": "DF_RC"\n",
			DP_DB(db), rdb_raft_entry_type_str(entry->type), rank, index, DP_RC(rc));
	return rc;
}

static int
rdb_raft_log_offer_single(struct rdb *db, raft_entry_t *entry, uint64_t index)
{
	d_iov_t			keys[2];
	d_iov_t			values[2];
	struct rdb_entry	header;
	int			n = 0;
	bool			crit;
	int			rc;
	int			rc_tmp;

	D_ASSERTF(index == db->d_lc_record.dlr_tail, DF_U64" == "DF_U64"\n",
		  index, db->d_lc_record.dlr_tail);

	/*
	 * If this is an rdb_tx entry, apply it. Note that the updates involved
	 * won't become visible to queries until entry index is committed.
	 * (Implicit queries resulted from rdb_kvs cache lookups won't happen
	 * until the TX releases the locks for the updates after the
	 * rdb_tx_commit() call returns.)
	 */
	if (entry->type == RAFT_LOGTYPE_NORMAL) {
		rc = rdb_tx_apply(db, index, entry->data.buf, entry->data.len,
				  rdb_raft_lookup_result(db, index), &crit);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to apply entry "DF_U64": %d\n",
				DP_DB(db), index, rc);
			goto err;
		}
	} else if (raft_entry_is_cfg_change(entry)) {
		crit = true;
		rc = rdb_raft_update_node(db, index, entry);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to update replicas "DF_U64": %d\n",
				DP_DB(db), index, rc);
			goto err;
		}
	} else {
		D_ASSERTF(0, "Unknown entry type %d\n", entry->type);
	}

	/*
	 * Persist the header and the data (if nonempty). Discard the unused
	 * entry->id.
	 */
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
	rc = rdb_lc_update(db->d_lc, index, RDB_LC_ATTRS, crit, n,
			   keys, values);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to persist entry "DF_U64": %d\n",
			DP_DB(db), index, rc);
		goto err_discard;
	}

	/* Replace entry->data.buf with the data's persistent memory address. */
	if (entry->data.len > 0) {
		d_iov_set(&values[0], NULL, entry->data.len);
		rc = rdb_lc_lookup(db->d_lc, index, RDB_LC_ATTRS,
				   &rdb_lc_entry_data, &values[0]);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to look up entry "DF_U64
				" data: %d\n", DP_DB(db), index, rc);
			goto err_discard;
		}
		entry->data.buf = values[0].iov_buf;
	} else {
		entry->data.buf = NULL;
	}

	/* Update the log tail. See the log tail assertion above. */
	db->d_lc_record.dlr_tail++;
	d_iov_set(&values[0], &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc,
			   &values[0]);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update log tail "DF_U64": %d\n",
			DP_DB(db), db->d_lc_record.dlr_tail, rc);
		db->d_lc_record.dlr_tail--;
		goto err_discard;
	}

	D_DEBUG(DB_TRACE, DF_DB": appended entry "DF_U64": term=%ld type=%s buf=%p len=%u\n",
		DP_DB(db), index, entry->term, rdb_raft_entry_type_str(entry->type),
		entry->data.buf, entry->data.len);
	return 0;

err_discard:
	rc_tmp = rdb_lc_discard(db->d_lc, index, index);
	if (rc_tmp != 0)
		D_ERROR(DF_DB": failed to discard entry "DF_U64": %d\n",
			DP_DB(db), index, rc_tmp);
err:
	return rc;
}

static int
rdb_raft_cb_log_offer(raft_server_t *raft, void *arg, raft_entry_t *entries,
		      raft_index_t index, int *n_entries)
{
	struct rdb     *db = arg;
	int		i;
	int		rc = 0;

	if (!db->d_raft_loaded)
		return 0;

	for (i = 0; i < *n_entries; ++i) {
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
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc,
			   &value);
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
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc,
			   &value);
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
rdb_raft_cb_log_get_node_id(raft_server_t *raft, void *arg, raft_entry_t *entry,
			    raft_index_t index)
{
	D_ASSERTF(raft_entry_is_cfg_change(entry), "index=%ld type=%s\n", index,
		  rdb_raft_entry_type_str(entry->type));
	return rdb_raft_cfg_entry_rank(entry);
}

static void
rdb_raft_cb_notify_membership_event(raft_server_t *raft, void *udata, raft_node_t *node,
				    raft_entry_t *entry, raft_membership_e type)
{
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
		rdb_node->dn_rank = rdb_raft_cfg_entry_rank(entry);
		raft_node_set_udata(node, rdb_node);
		break;
	case RAFT_MEMBERSHIP_REMOVE:
		D_ASSERT(rdb_node != NULL);
		free(rdb_node);
		break;
	default:
		D_ASSERTF(false, "invalid raft membership event type %s\n",
			  rdb_raft_entry_type_str(type));
	}
}

static void
rdb_raft_cb_debug(raft_server_t *raft, raft_node_t *node, void *arg,
		  const char *buf)
{
	struct rdb *db = raft_get_udata(raft);

	if (node != NULL) {
		struct rdb_raft_node *rdb_node = raft_node_get_udata(node);

		D_DEBUG(DB_TRACE, DF_DB": %s: rank=%u\n", DP_DB(db), buf,
			rdb_node->dn_rank);
	} else {
		D_DEBUG(DB_TRACE, DF_DB": %s\n", DP_DB(db), buf);
	}
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
	.log				= rdb_raft_cb_debug
};

static int
rdb_raft_compact_to_index(struct rdb *db, uint64_t index)
{
	int rc;

	D_DEBUG(DB_TRACE, DF_DB": snapping "DF_U64"\n", DP_DB(db),
		index);
	rc = raft_begin_snapshot(db->d_raft, index);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	/*
	 * VOS snaps every new index implicitly.
	 *
	 * raft_end_snapshot() only polls the log and wakes up
	 * rdb_compactd(), which does the real compaction (i.e., VOS
	 * aggregation) in the background.
	 */
	rc = raft_end_snapshot(db->d_raft);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to poll entries: %d\n",
			DP_DB(db), rc);
		rc = rdb_raft_rc(rc);
	}

	return rc;
}

/*
 * Check if the log should be compacted. If so, trigger the compaction by
 * taking a snapshot (i.e., simply increasing the log base index in our
 * implementation).
 */
static int
rdb_raft_trigger_compaction(struct rdb *db)
{
	uint64_t	base;
	int		n;
	int		rc = 0;

	/*
	 * If the number of applied entries reaches db->d_compact_thres,
	 * trigger compaction.
	 */
	base = raft_get_current_idx(db->d_raft) -
	       raft_get_log_count(db->d_raft);
	D_ASSERTF(db->d_applied >= base, DF_U64" >= "DF_U64"\n", db->d_applied,
		  base);
	n = db->d_applied - base;
	if (n >= db->d_compact_thres) {
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

		rc = rdb_raft_compact_to_index(db, index);
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
	rc = rdb_mc_update(db->d_mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_lc,
			   &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update last aggregated index to "
			DF_U64": %d\n", DP_DB(db),
			db->d_lc_record.dlr_aggregated, rc);
		db->d_lc_record.dlr_aggregated = aggregated;
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}
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
			D_ERROR(DF_DB": failed to compact to base "DF_U64
				": %d\n", DP_DB(db), base, rc);
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
		compaction_rc = rdb_raft_trigger_compaction(db);
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
			D_ERROR(DF_DB": failed to append entry: %d\n",
				DP_DB(db), rc);
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
rdb_raft_add_replica(struct rdb *db, d_rank_t rank)
{
	msg_entry_t	 entry = {};
	int		 result;
	int		 rc;

	D_DEBUG(DB_MD, DF_DB": Replica Rank: %d\n", DP_DB(db), rank);
	entry.type = RAFT_LOGTYPE_ADD_NODE;
	entry.data.buf = &rank;
	entry.data.len = sizeof(d_rank_t);
	rc = rdb_raft_append_apply_internal(db, &entry, &result);
	return (rc != 0) ? rc : result;
}

int
rdb_raft_remove_replica(struct rdb *db, d_rank_t rank)
{
	msg_entry_t	 entry = {};
	int		 result;
	int		 rc;

	D_DEBUG(DB_MD, DF_DB": Replica Rank: %d\n", DP_DB(db), rank);
	entry.type = RAFT_LOGTYPE_REMOVE_NODE;
	entry.data.buf = &rank;
	entry.data.len = sizeof(d_rank_t);
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
		rc = raft_periodic(db->d_raft, d_prev * 1000 /* ms */);
		rc = rdb_raft_check_state(db, &state, rc);
		ABT_mutex_unlock(db->d_raft_mutex);
		if (rc != 0)
			D_ERROR(DF_DB": raft_periodic() failed: %d\n",
				DP_DB(db), rc);
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
	d_iov_t		value;
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
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, key, &value);
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
	d_iov_t		value;
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
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, key, &value);
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
rdb_raft_init(daos_handle_t pool, daos_handle_t mc,
	      const d_rank_list_t *replicas)
{
	daos_handle_t		lc;
	struct rdb_lc_record    record;
	uint64_t		base;
	int			rc;
	int			rc_close;

	base = (replicas == NULL || replicas->rl_nr == 0) ? 0 : 1;

	/* Create log container; base is 1 since we store replicas at idx 1 */
	rc = rdb_raft_create_lc(pool, mc, &rdb_mc_lc, base, 0 /* base_term */,
				0 /* term */, &record /* lc_record */);
	/* Return on failure or if there are no replicas to be stored */
	if (base == 0 || rc != 0)
		return rc;

	/* Record the configuration in the LC at index 1. */
	rc = vos_cont_open(pool, record.dlr_uuid, &lc);
	/* This really should not be happening.. */
	D_ASSERTF(rc == 0, "Open VOS container: "DF_RC"\n", DP_RC(rc));

	/* No initial configuration if rank list empty */
	rc = rdb_raft_store_replicas(lc, 1 /* base */, replicas);
	if (rc != 0)
		D_ERROR("failed to create list of replicas: "DF_RC"\n",
			DP_RC(rc));
	rc_close = vos_cont_close(lc);
	return (rc != 0) ? rc : rc_close;
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

	D_DEBUG(DB_TRACE,
		DF_DB": loaded entry "DF_U64": term=%ld type=%d buf=%p "
		"len=%u\n", DP_DB(db), index, entry.term, entry.type,
		entry.data.buf, entry.data.len);
	return 0;
}

/* Load the LC and the SLC (if one exists). */
static int
rdb_raft_load_lc(struct rdb *db)
{
	d_iov_t	value;
	uint64_t	i;
	int		rc;

	/* Look up and open the SLC. */
	d_iov_set(&value, &db->d_slc_record, sizeof(db->d_slc_record));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_slc, &value);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_DB": no SLC record\n", DP_DB(db));
		db->d_slc = DAOS_HDL_INVAL;
		goto lc;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up SLC: "DF_RC"\n", DP_DB(db),
			DP_RC(rc));
		goto err;
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

lc:
	/* Look up and open the LC. */
	d_iov_set(&value, &db->d_lc_record, sizeof(db->d_lc_record));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_lc, &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to look up LC: "DF_RC"\n", DP_DB(db),
			DP_RC(rc));
		goto err_slc;
	}
	rc = vos_cont_open(db->d_pool, db->d_lc_record.dlr_uuid, &db->d_lc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to open LC "DF_UUID": %d\n", DP_DB(db),
			DP_UUID(db->d_lc_record.dlr_uuid), rc);
		goto err_slc;
	}

	/* Recover the LC by discarding any partially appended entries. */
	rc = rdb_lc_discard(db->d_lc, db->d_lc_record.dlr_tail,
			    RDB_LC_INDEX_MAX);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to recover LC "DF_U64": %d\n", DP_DB(db),
			db->d_lc_record.dlr_base, rc);
		goto err_lc;
	}

	/* Load the LC base. */
	rc = rdb_raft_load_snapshot(db);
	if (rc != 0)
		goto err_lc;

	/* Load the log entries. */
	for (i = db->d_lc_record.dlr_base + 1; i < db->d_lc_record.dlr_tail;
	     i++) {
		/*
		 * Yield before loading the first entry (for the rdb_lc_discard
		 * call above) and every a few entries.
		 */
		if ((i - db->d_lc_record.dlr_base - 1) % 64 == 0)
			ABT_thread_yield();
		rc = rdb_raft_load_entry(db, i);
		if (rc != 0)
			goto err_lc;
	}

	return 0;

err_lc:
	vos_cont_close(db->d_lc);
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
	vos_cont_close(db->d_lc);
}

static int
rdb_raft_get_election_timeout(void)
{
	char	       *name = "RDB_ELECTION_TIMEOUT";
	unsigned int	default_value = 7000;
	unsigned int	value = default_value;

	d_getenv_int(name, &value);
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

	d_getenv_int(name, &value);
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

	d_getenv_int(name, &value);
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

	d_getenv_int(name, &value);
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

/*
 * Load raft persistent state, if any. Our raft callbacks must be registered
 * already, because rdb_raft_cb_notify_membership_event is required. We use
 * db->d_raft_loaded to instruct some of our raft callbacks to avoid
 * unnecessary write I/Os.
 */
static int
rdb_raft_load(struct rdb *db)
{
	d_iov_t		value;
	uint64_t	term;
	int		vote;
	int		rc;

	D_DEBUG(DB_MD, DF_DB": load persistent state: begin\n", DP_DB(db));
	D_ASSERT(!db->d_raft_loaded);

	d_iov_set(&value, &term, sizeof(term));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_term, &value);
	if (rc == 0) {
		rc = raft_set_current_term(db->d_raft, term);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	} else if (rc != -DER_NONEXIST) {
		goto out;
	}

	d_iov_set(&value, &vote, sizeof(vote));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_vote, &value);
	if (rc == 0) {
		rc = raft_vote_for_nodeid(db->d_raft, vote);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	} else if (rc != -DER_NONEXIST) {
		goto out;
	}

	rc = rdb_raft_load_lc(db);
	if (rc != 0)
		goto out;

	db->d_raft_loaded = true;
out:
	D_DEBUG(DB_MD, DF_DB": load persistent state: end: "DF_RC"\n", DP_DB(db), DP_RC(rc));
	return rc;
}

int
rdb_raft_start(struct rdb *db)
{
	int	election_timeout;
	int	request_timeout;
	int	rc;

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

	db->d_raft = raft_new();
	if (db->d_raft == NULL) {
		D_ERROR(DF_DB": failed to create raft object\n", DP_DB(db));
		rc = -DER_NOMEM;
		goto err_compact_cv;
	}

	raft_set_nodeid(db->d_raft, dss_self_rank());
	raft_set_callbacks(db->d_raft, &rdb_raft_cbs, db);

	rc = rdb_raft_load(db);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to load raft persistent state\n", DP_DB(db));
		goto err_raft;
	}

	election_timeout = rdb_raft_get_election_timeout();
	request_timeout = rdb_raft_get_request_timeout();
	raft_set_election_timeout(db->d_raft, election_timeout);
	raft_set_request_timeout(db->d_raft, request_timeout);

	rc = dss_ult_create(rdb_recvd, db, DSS_XS_SELF, 0, 0, &db->d_recvd);
	if (rc != 0)
		goto err_lc;
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

	D_DEBUG(DB_MD,
		DF_DB": raft started: election_timeout=%dms request_timeout=%dms "
		"compact_thres="DF_U64" ae_max_entries=%u ae_max_size="DF_U64"\n", DP_DB(db),
		election_timeout, request_timeout, db->d_compact_thres, db->d_ae_max_entries,
		db->d_ae_max_size);
	return 0;

err_callbackd:
	db->d_stop = true;
	rc = ABT_thread_join(db->d_callbackd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_callbackd);
err_timerd:
	db->d_stop = true;
	rc = ABT_thread_join(db->d_timerd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_timerd);
err_recvd:
	db->d_stop = true;
	ABT_cond_broadcast(db->d_replies_cv);
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_recvd);
err_lc:
	rdb_raft_unload_lc(db);
err_raft:
	raft_free(db->d_raft);
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
rdb_raft_stop(struct rdb *db)
{
	int rc;

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
	rc = ABT_thread_join(db->d_compactd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_compactd);
	rc = ABT_thread_join(db->d_callbackd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_callbackd);
	rc = ABT_thread_join(db->d_timerd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_timerd);
	rc = ABT_thread_join(db->d_recvd);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&db->d_recvd);

	rdb_raft_unload_lc(db);
	raft_free(db->d_raft);
	ABT_cond_free(&db->d_compact_cv);
	ABT_cond_free(&db->d_replies_cv);
	ABT_cond_free(&db->d_events_cv);
	ABT_cond_free(&db->d_applied_cv);
	d_hash_table_destroy_inplace(&db->d_results, true /* force */);
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

	D_DEBUG(DB_MD, DF_DB": resigning from term "DF_U64"\n", DP_DB(db),
		term);
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
		rc = -DER_INVAL;
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

int
rdb_raft_get_ranks(struct rdb *db, d_rank_list_t **ranksp)
{
	d_rank_list_t  *ranks;
	int		n;
	int		i;
	int		rc;

	ABT_mutex_lock(db->d_raft_mutex);

	n = raft_get_num_nodes(db->d_raft);

	ranks = d_rank_list_alloc(n);
	if (ranks == NULL) {
		rc = -DER_NOMEM;
		goto mutex;
	}

	for (i = 0; i < n; i++) {
		raft_node_t	       *node = raft_get_node_from_idx(db->d_raft, i);
		struct rdb_raft_node   *rdb_node = raft_node_get_udata(node);

		ranks->rl_ranks[i] = rdb_node->dn_rank;
	}
	ranks->rl_nr = i;

	*ranksp = ranks;
	rc = 0;
mutex:
	ABT_mutex_unlock(db->d_raft_mutex);
	return rc;
}

void
rdb_requestvote_handler(crt_rpc_t *rpc)
{
	struct rdb_requestvote_in      *in = crt_req_get(rpc);
	struct rdb_requestvote_out     *out = crt_reply_get(rpc);
	struct rdb		       *db;
	char			       *s;
	struct rdb_raft_state		state;
	d_rank_t			srcrank;
	int				rc;

	s = in->rvi_msg.prevote ? " (prevote)" : "";
	rc = crt_req_src_rank_get(rpc, &srcrank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	db = rdb_lookup(in->rvi_op.ri_uuid);
	if (db == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);
	if (db->d_stop)
		D_GOTO(out_db, rc = -DER_CANCELED);

	D_DEBUG(DB_TRACE, DF_DB": handling raft rv%s from rank %u\n",
		DP_DB(db), s, srcrank);
	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_requestvote(db->d_raft,
				   raft_get_node(db->d_raft,
						 srcrank),
				   &in->rvi_msg, &out->rvo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to process REQUESTVOTE%s from rank %u: "
			"%d\n", DP_DB(db), s, srcrank, rc);
		/* raft_recv_requestvote() always generates a valid reply. */
		rc = 0;
	}

out_db:
	rdb_put(db);
out:
	out->rvo_op.ro_rc = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to send REQUESTVOTE%s reply to "
			"rank %u: %d\n", DP_UUID(in->rvi_op.ri_uuid), s,
			srcrank, rc);
}

void
rdb_appendentries_handler(crt_rpc_t *rpc)
{
	struct rdb_appendentries_in    *in = crt_req_get(rpc);
	struct rdb_appendentries_out   *out = crt_reply_get(rpc);
	struct rdb		       *db;
	struct rdb_raft_state		state;
	d_rank_t			srcrank;
	int				rc;

	rc = crt_req_src_rank_get(rpc, &srcrank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	db = rdb_lookup(in->aei_op.ri_uuid);
	if (db == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);
	if (db->d_stop)
		D_GOTO(out_db, rc = -DER_CANCELED);

	D_DEBUG(DB_TRACE, DF_DB": handling raft ae from rank %u\n", DP_DB(db),
		srcrank);
	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_appendentries(db->d_raft,
				     raft_get_node(db->d_raft, srcrank),
				     &in->aei_msg, &out->aeo_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to process APPENDENTRIES from rank %u: "
			"%d\n", DP_DB(db), srcrank, rc);
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
			srcrank, rc);
}

void
rdb_installsnapshot_handler(crt_rpc_t *rpc)
{
	struct rdb_installsnapshot_in  *in = crt_req_get(rpc);
	struct rdb_installsnapshot_out *out = crt_reply_get(rpc);
	struct rdb		       *db;
	struct rdb_raft_state		state;
	d_rank_t			srcrank;
	int				rc;

	rc = crt_req_src_rank_get(rpc, &srcrank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	db = rdb_lookup(in->isi_op.ri_uuid);
	if (db == NULL) {
		rc = -DER_NONEXIST;
		goto out;
	}
	if (db->d_stop) {
		rc = -DER_CANCELED;
		goto out_db;
	}

	D_DEBUG(DB_TRACE, DF_DB": handling raft is from rank %u\n", DP_DB(db),
		srcrank);

	/* Receive the bulk data buffers before entering raft. */
	rc = rdb_raft_recv_is(db, rpc, &in->isi_local.rl_kds_iov,
			      &in->isi_local.rl_data_iov);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to receive INSTALLSNAPSHOT chunk %ld"
			"/"DF_U64": %d\n", DP_DB(db), in->isi_msg.last_idx,
			in->isi_seq, rc);
		goto out_db;
	}

	ABT_mutex_lock(db->d_raft_mutex);
	rdb_raft_save_state(db, &state);
	rc = raft_recv_installsnapshot(db->d_raft,
				       raft_get_node(db->d_raft, srcrank),
				       &in->isi_msg, &out->iso_msg);
	rc = rdb_raft_check_state(db, &state, rc);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to process INSTALLSNAPSHOT from rank "
			"%u: %d\n", DP_DB(db), srcrank, rc);
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
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to send INSTALLSNAPSHOT reply to rank "
			"%u: %d\n", DP_UUID(in->isi_op.ri_uuid),
			srcrank, rc);
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
	d_rank_t			rank;
	raft_node_t		       *node;
	int				rc;

	/* Get the destination of the request - that is the source
	 * rank of this reply. This CaRT API is based on request hdr.
	 */
	rc = crt_req_dst_rank_get(rpc, &rank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	rc = ((struct rdb_op_out *)out)->ro_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_DB": opc %u failed: %d\n", DP_DB(db), opc,
			rc);
		return;
	}

	ABT_mutex_lock(db->d_raft_mutex);

	node = raft_get_node(db->d_raft, rank);
	if (node == NULL) {
		D_DEBUG(DB_MD, DF_DB": rank %u not in current membership\n", DP_DB(db), rank);
		goto out_mutex;
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
	case RDB_INSTALLSNAPSHOT:
		out_is = out;
		rc = raft_recv_installsnapshot_response(db->d_raft, node,
							&out_is->iso_msg);
		break;
	default:
		D_ASSERTF(0, DF_DB": unexpected opc: %u\n", DP_DB(db), opc);
	}
	rc = rdb_raft_check_state(db, &state, rc);
	if (rc != 0 && rc != -DER_NOTLEADER)
		D_ERROR(DF_DB": failed to process opc %u response: %d\n",
			DP_DB(db), opc, rc);

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
