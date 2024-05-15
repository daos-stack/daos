/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos/debug.h>
#include <gurt/list.h>
#include <gurt/debug.h>
#include <cart/iv.h>
#include <daos_srv/iv.h>
#include <daos_srv/daos_engine.h>

#include "chk_internal.h"

static int
chk_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc = 0;

	rc = d_sgl_init(sgl, 1);
	if (rc != 0)
		goto out;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct chk_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL) {
		d_sgl_fini(sgl, true);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	sgl->sg_iovs[0].iov_buf_len = sizeof(struct chk_iv);
	sgl->sg_iovs[0].iov_len = sizeof(struct chk_iv);

out:
	return rc;
}

static int
chk_iv_ent_init(struct ds_iv_key *iv_key, void *data, struct ds_iv_entry *entry)
{
	int	rc;

	rc = chk_iv_alloc_internal(&entry->iv_value);
	if (rc == 0) {
		entry->iv_key.class_id = iv_key->class_id;
		entry->iv_key.rank = iv_key->rank;
	}

	return rc;
}

static int
chk_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static void
chk_iv_ent_put(struct ds_iv_entry *entry, void *priv)
{
}

static int
chk_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);

	return 0;
}

static int
chk_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key, d_sg_list_t *dst, void **priv)
{
	D_ASSERT(0);

	return 0;
}

/* Update the chk pool svc lists and status from engine to leader. */
static int
chk_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		  d_sg_list_t *src, void **priv)
{
	struct chk_iv	*dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct chk_iv	*src_iv = src->sg_iovs[0].iov_buf;
	int		 rc;

	if (src_iv->ci_rank == dss_self_rank()) {
		if (src_iv->ci_to_leader) {
			/*
			 * The case of the check engine sending IV message to the check leader
			 * on the same rank has already been handled via chk_iv_update(). Then
			 * only need to handle the case that the check leader resides on other
			 * rank (trigger RPC to the check leader - the IV parent via returning
			 * -DER_IVCB_FORWARD.
			 */
			D_ASSERTF(!chk_is_on_leader(src_iv->ci_gen, CHK_LEADER_RANK, false),
				  "Invalid IV forward path for gen "DF_X64"/"DF_X64", rank %u, "
				  "phase %u, status %d/%d, from_psl %s\n",
				  src_iv->ci_gen, src_iv->ci_seq, src_iv->ci_rank, src_iv->ci_phase,
				  src_iv->ci_ins_status, src_iv->ci_pool_status,
				  src_iv->ci_from_psl ? "yes" : "no");
			rc = -DER_IVCB_FORWARD;
		} else {
			/*
			 * If it is message to engine, then it may be triggered by check leader,
			 * but it also may be from the pool service leader to other pool shards.
			 * Return zero that will trigger IV_SYNC to other check engines.
			 *
			 * NOTE: Currently, IV refresh from root node is always direct to leaves,
			 *	 it does not need some internal nodes to forward. So here, if it
			 *	 is not for PS leader notification, then it must be triggered by
			 *	 the check leader.
			 */
			if (!src_iv->ci_from_psl)
				D_ASSERTF(chk_is_on_leader(src_iv->ci_gen, CHK_LEADER_RANK, false),
					  "Invalid IV forward path for gen "DF_X64"/"DF_X64
					  ", rank %u, phase %u, status %d/%d\n", src_iv->ci_gen,
					  src_iv->ci_seq, src_iv->ci_rank, src_iv->ci_phase,
					  src_iv->ci_ins_status, src_iv->ci_pool_status);
			rc = 0;
		}
	} else {
		/*
		 * We got an IV SYNC (refresh) RPC from some engine. But because the engine
		 * always set CRT_IV_SHORTCUT_TO_ROOT for sync, then this should not happen.
		 */
		D_ASSERTF(src_iv->ci_to_leader,
			  "Got invalid IV SYNC with gen "DF_X64"/"DF_X64", rank %u, phase %u, "
			  "status %d/%d, to_leader no, from_psl %s\n",
			  src_iv->ci_gen, src_iv->ci_seq, src_iv->ci_rank, src_iv->ci_phase,
			  src_iv->ci_ins_status, src_iv->ci_pool_status,
			  src_iv->ci_from_psl ? "yes" : "no");
		*dst_iv = *src_iv;
		rc = chk_leader_notify(dst_iv);
	}

	return rc;
}

/* Refresh the chk status from leader to engines. */
static int
chk_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		   d_sg_list_t *src, int ref_rc, void **priv)
{
	struct chk_iv	*dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct chk_iv	*src_iv = src->sg_iovs[0].iov_buf;
	int		 rc = 0;

	/*
	 * For the notification from pool service leader, skip the local pool shard that will
	 * be handled by the pool service leader (including the @cpr status and pool service).
	 *
	 * For the notification from the check leader to check engines, do not skip the local
	 * check engine.
	 */
	if (!src_iv->ci_to_leader && (src_iv->ci_rank != dss_self_rank() || !src_iv->ci_from_psl)) {
		*dst_iv = *src_iv;
		rc = chk_engine_notify(dst_iv);
	}

	return rc;
}

static int
chk_iv_value_alloc(struct ds_iv_entry *entry, struct ds_iv_key *key, d_sg_list_t *sgl)
{
	return chk_iv_alloc_internal(sgl);
}

struct ds_iv_class_ops chk_iv_ops = {
	.ivc_ent_init		= chk_iv_ent_init,
	.ivc_ent_get		= chk_iv_ent_get,
	.ivc_ent_put		= chk_iv_ent_put,
	.ivc_ent_destroy	= chk_iv_ent_destroy,
	.ivc_ent_fetch		= chk_iv_ent_fetch,
	.ivc_ent_update		= chk_iv_ent_update,
	.ivc_ent_refresh	= chk_iv_ent_refresh,
	.ivc_value_alloc	= chk_iv_value_alloc,
};

int
chk_iv_update(void *ns, struct chk_iv *iv, uint32_t shortcut, uint32_t sync_mode, bool retry)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iv->ci_rank = dss_self_rank();
	iv->ci_seq = d_hlc_get();

	if (chk_is_on_leader(iv->ci_gen, CHK_LEADER_RANK, false) && iv->ci_to_leader) {
		/*
		 * It is the check engine sends IV message to the check leader on
		 * the same rank. Then directly notify the check leader without RPC.
		 */
		rc = chk_leader_notify(iv);
	} else {
		iov.iov_buf = iv;
		iov.iov_len = sizeof(*iv);
		iov.iov_buf_len = sizeof(*iv);
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs = &iov;

		memset(&key, 0, sizeof(key));
		key.class_id = IV_CHK;
		rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "CHK iv "DF_X64"/"DF_X64" on rank %u, phase %u, ins_status %u, "
		 "pool_status %u, to_leader %s, from_psl %s: rc = %d\n",
		 iv->ci_gen, iv->ci_seq, iv->ci_rank, iv->ci_phase, iv->ci_ins_status,
		 iv->ci_pool_status, iv->ci_to_leader ? "yes" : "no",
		 iv->ci_from_psl ? "yes" : "no", rc);

	return rc;
}

int
chk_iv_init(void)
{
	return ds_iv_class_register(IV_CHK, &iv_cache_ops, &chk_iv_ops);
}

int
chk_iv_fini(void)
{
	return ds_iv_class_unregister(IV_CHK);
}
