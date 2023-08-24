/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: rebuild initiator
 *
 * This file contains the server API methods and the RPC handlers for rebuild
 * initiator.
 */
#define D_LOGFAC	DD_FAC(rebuild)

#include <daos_srv/pool.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/iv.h>
#include <cart/iv.h>
#include "rpc.h"
#include "rebuild_internal.h"
#include "rpc.h"
#include "rebuild_internal.h"

static int
rebuild_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct rebuild_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct rebuild_iv);
free:
	if (rc)
		d_sgl_fini(sgl, true);
	return rc;
}

static int
rebuild_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		    struct ds_iv_entry *entry)
{
	int rc;

	rc = rebuild_iv_alloc_internal(&entry->iv_value);
	if (rc)
		return rc;

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;
	return 0;
}

static int
rebuild_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static void
rebuild_iv_ent_put(struct ds_iv_entry *entry, void *priv)
{
	return;
}

static int
rebuild_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);
	return 0;
}

static int
rebuild_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		     d_sg_list_t *dst, void **priv)
{
	D_ASSERT(0);
	return 0;
}

/* Update the rebuild status from leaves to the master */
static int
rebuild_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		      d_sg_list_t *src, void **priv)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct rebuild_global_pool_tracker *rgt;
	d_rank_t	  rank;
	int		  rc;

	D_DEBUG(DB_REBUILD, "rank %d master rank %d\n", src_iv->riv_rank,
		src_iv->riv_master_rank);

	if (src_iv->riv_master_rank == -1)
		return -DER_NOTLEADER;

	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return rc;

	if (rank != src_iv->riv_master_rank)
		return -DER_IVCB_FORWARD;

	if (src_iv->riv_sync)
		return 0;

	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);

	/* Gathering the rebuild status here */
	rgt = rebuild_global_pool_tracker_lookup(src_iv->riv_pool_uuid,
						 src_iv->riv_ver, src_iv->riv_rebuild_gen);
	if (rgt == NULL)
		D_GOTO(out, rc);

	if (rgt->rgt_leader_term == src_iv->riv_leader_term) {
		/* update the rebuild global status */
		if (!src_iv->riv_global_done) {
			rgt->rgt_status.rs_toberb_obj_nr +=
				src_iv->riv_toberb_obj_count;
			rgt->rgt_status.rs_obj_nr += src_iv->riv_obj_count;
			rgt->rgt_status.rs_rec_nr += src_iv->riv_rec_count;
			rgt->rgt_status.rs_size += src_iv->riv_size;
		}

		rebuild_global_status_update(rgt, src_iv);
		if (rgt->rgt_status.rs_errno == 0) {
			rgt->rgt_status.rs_errno = src_iv->riv_status;
			if (src_iv->riv_status != 0) {
				rgt->rgt_status.rs_fail_rank = src_iv->riv_rank;
				rgt->rgt_abort = 1;
			}
		}
		D_DEBUG(DB_REBUILD, "update rebuild "DF_UUID" ver %d gen %u"
			" toberb_obj/rb_obj/rec/global state/status/rank/abort "
			DF_U64"/"DF_U64"/"DF_U64"/%d/%d/%d/%d\n",
			DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver,
			rgt->rgt_rebuild_gen, rgt->rgt_status.rs_toberb_obj_nr,
			rgt->rgt_status.rs_obj_nr, rgt->rgt_status.rs_rec_nr,
			rgt->rgt_status.rs_state, rgt->rgt_status.rs_errno,
			src_iv->riv_rank, rgt->rgt_abort);
	}
	rgt_put(rgt);
out:
	D_DEBUG(DB_TRACE, "pool "DF_UUID" master_rank %d\n",
		DP_UUID(dst_iv->riv_pool_uuid), dst_iv->riv_master_rank);

	return 0;
}

/* Distribute the rebuild uuid/master rank from master to leaves */
static int
rebuild_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		       d_sg_list_t *src, int ref_rc, void **priv)
{
	struct rebuild_tgt_pool_tracker *rpt;
	struct rebuild_iv *dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	int rc = 0;

	rpt = rpt_lookup(src_iv->riv_pool_uuid, src_iv->riv_ver,
			 src_iv->riv_rebuild_gen);
	if (rpt == NULL)
		return 0;

	if (rpt->rt_leader_term != src_iv->riv_leader_term) {
		rpt_put(rpt);
		return 0;
	}

	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);
	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	dst_iv->riv_global_done = src_iv->riv_global_done;
	dst_iv->riv_global_scan_done = src_iv->riv_global_scan_done;
	dst_iv->riv_stable_epoch = src_iv->riv_stable_epoch;
	dst_iv->riv_global_dtx_resyc_version = src_iv->riv_global_dtx_resyc_version;

	if (dst_iv->riv_global_done || dst_iv->riv_global_scan_done ||
	    dst_iv->riv_stable_epoch || dst_iv->riv_dtx_resyc_version) {
		D_DEBUG(DB_REBUILD, DF_UUID"/%u/%u/"DF_U64" gsd/gd/stable/ver %d/%d/"DF_X64"/%u\n",
			DP_UUID(src_iv->riv_pool_uuid), src_iv->riv_ver,
			src_iv->riv_rebuild_gen, src_iv->riv_leader_term,
			dst_iv->riv_global_scan_done, dst_iv->riv_global_done,
			dst_iv->riv_stable_epoch, dst_iv->riv_global_dtx_resyc_version);

		if (rpt->rt_stable_epoch == 0)
			rpt->rt_stable_epoch = dst_iv->riv_stable_epoch;
		else if (rpt->rt_stable_epoch != dst_iv->riv_stable_epoch)
			D_WARN("leader change stable epoch from "DF_U64" to "
			       DF_U64 "\n", rpt->rt_stable_epoch,
			       dst_iv->riv_stable_epoch);
		rpt->rt_global_done = dst_iv->riv_global_done;
		rpt->rt_global_scan_done = dst_iv->riv_global_scan_done;
		if (rpt->rt_global_dtx_resync_version < rpt->rt_rebuild_ver &&
		    dst_iv->riv_global_dtx_resyc_version >= rpt->rt_rebuild_ver) {
			D_INFO(DF_UUID" global/iv/rebuild_ver %u/%u/%u signal wait cond\n",
			       DP_UUID(src_iv->riv_pool_uuid), rpt->rt_global_dtx_resync_version,
			       dst_iv->riv_global_dtx_resyc_version, rpt->rt_rebuild_ver);
			ABT_mutex_lock(rpt->rt_lock);
			ABT_cond_signal(rpt->rt_global_dtx_wait_cond);
			ABT_mutex_unlock(rpt->rt_lock);
		}
		rpt->rt_global_dtx_resync_version = dst_iv->riv_global_dtx_resyc_version;

		rpt_put(rpt);
	}

	return rc;
}

static int
rebuild_iv_alloc(struct ds_iv_entry *entry, struct ds_iv_key *key,
		 d_sg_list_t *sgl)
{
	return rebuild_iv_alloc_internal(sgl);
}

struct ds_iv_class_ops rebuild_iv_ops = {
	.ivc_ent_init		= rebuild_iv_ent_init,
	.ivc_ent_get		= rebuild_iv_ent_get,
	.ivc_ent_put		= rebuild_iv_ent_put,
	.ivc_ent_destroy	= rebuild_iv_ent_destroy,
	.ivc_ent_fetch		= rebuild_iv_ent_fetch,
	.ivc_ent_update		= rebuild_iv_ent_update,
	.ivc_ent_refresh	= rebuild_iv_ent_refresh,
	.ivc_value_alloc	= rebuild_iv_alloc,
};

int
rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iov.iov_buf = rebuild_iv;
	iov.iov_len = sizeof(*rebuild_iv);
	iov.iov_buf_len = sizeof(*rebuild_iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_REBUILD;
	rc = ds_iv_fetch(ns, &key, &sgl, true /* retry */);
	if (rc)
		D_ERROR("iv fetch failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
rebuild_iv_update(void *ns, struct rebuild_iv *iv, unsigned int shortcut,
		  unsigned int sync_mode, bool retry)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iov.iov_buf = iv;
	iov.iov_len = sizeof(*iv);
	iov.iov_buf_len = sizeof(*iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_REBUILD;
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	if (rc)
		D_CDEBUG(daos_quiet_error(rc), DB_REBUILD, DLOG_ERR, "iv update failed "DF_RC"\n",
			 DP_RC(rc));

	return rc;
}

int
rebuild_iv_init(void)
{
	return ds_iv_class_register(IV_REBUILD, &iv_cache_ops, &rebuild_iv_ops);
}

int
rebuild_iv_fini(void)
{
	return ds_iv_class_unregister(IV_REBUILD);
}
