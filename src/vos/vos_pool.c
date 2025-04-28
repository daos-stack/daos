/**
 * (C) Copyright 2016-2025 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Implementation for pool specific functions in VOS
 *
 * vos/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_srv/ras.h>
#include <daos_errno.h>
#include <gurt/hash.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "vos_layout.h"
#include "vos_internal.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <daos_pool.h>

static void
vos_iod2bsgl(struct umem_store *store, struct umem_store_iod *iod, struct bio_sglist *bsgl)
{
	struct bio_iov	*biov;
	bio_addr_t	 addr = { 0 };
	uint32_t	 off_bytes;
	int		 i;

	off_bytes = store->stor_hdr_blks * store->stor_blk_size;
	for (i = 0; i < iod->io_nr; i++) {
		biov = &bsgl->bs_iovs[i];

		bio_addr_set(&addr, DAOS_MEDIA_NVME, iod->io_regions[i].sr_addr + off_bytes);
		bio_iov_set(biov, addr, iod->io_regions[i].sr_size);
	}
	bsgl->bs_nr_out = bsgl->bs_nr;
}

static int
vos_meta_rwv(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl, bool update)
{
	struct bio_sglist	bsgl;
	struct bio_iov		local_biov;
	int			rc;

	D_ASSERT(store && store->stor_priv != NULL);
	D_ASSERT(iod->io_nr > 0);
	D_ASSERT(sgl->sg_nr > 0);

	if (update) {
		rc = bio_meta_clear_empty(store->stor_priv);
		if (rc)
			return rc;
	} else if (bio_meta_is_empty(store->stor_priv)) {
		return 0;
	}

	if (iod->io_nr == 1) {
		bsgl.bs_iovs = &local_biov;
		bsgl.bs_nr = 1;
	} else {
		rc = bio_sgl_init(&bsgl, iod->io_nr);
		if (rc)
			return rc;
	}
	vos_iod2bsgl(store, iod, &bsgl);

	if (update)
		rc = bio_writev(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META), &bsgl, sgl);
	else
		rc = bio_readv(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META), &bsgl, sgl);

	if (iod->io_nr > 1)
		bio_sgl_fini(&bsgl);

	return rc;
}

static inline int
vos_meta_readv(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	return vos_meta_rwv(store, iod, sgl, false);
}


#define META_READ_BATCH_SIZE (1024 * 1024)

struct meta_load_control {
	ABT_mutex		mlc_lock;
	ABT_cond		mlc_cond;
	int			mlc_rc;
	int			mlc_inflights;
	int			mlc_wait_finished;
};

struct meta_load_arg {
	daos_off_t		  mla_off;
	uint64_t		  mla_read_size;
	char		         *mla_start;
	struct umem_store        *mla_store;
	struct meta_load_control *mla_control;
};

#define META_READ_QD_NR	4

static inline void
vos_meta_load_fn(void *arg)
{
	struct meta_load_arg	  *mla = arg;
	struct umem_store_iod	  iod;
	d_sg_list_t		  sgl;
	d_iov_t			  iov;
	int			  rc;
	struct meta_load_control *mlc = mla->mla_control;

	D_ASSERT(mla != NULL);
	iod.io_nr             = 1;
	iod.io_regions        = &iod.io_region;
	iod.io_region.sr_addr = mla->mla_off;
	iod.io_region.sr_size = mla->mla_read_size;

	sgl.sg_iovs   = &iov;
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, mla->mla_start, mla->mla_read_size);

	rc = vos_meta_rwv(mla->mla_store, &iod, &sgl, false);
	if (!mlc->mlc_rc && rc)
		mlc->mlc_rc = rc;

	D_FREE(mla);
	mlc->mlc_inflights--;
	if (mlc->mlc_wait_finished && mlc->mlc_inflights == 0)
		ABT_cond_signal(mlc->mlc_cond);
	else if (!mlc->mlc_wait_finished && mlc->mlc_inflights == META_READ_QD_NR)
		ABT_cond_signal(mlc->mlc_cond);
}

static int
vos_meta_load(struct umem_store *store, char *start, daos_off_t offset, daos_size_t len)
{
	uint64_t		 read_size;
	uint64_t		 remain_size = len;
	daos_off_t		 off = offset;
	int			 rc = 0;
	struct meta_load_arg	*mla;
	struct meta_load_control mlc;

	mlc.mlc_inflights = 0;
	mlc.mlc_rc = 0;
	mlc.mlc_wait_finished = 0;
	rc = ABT_mutex_create(&mlc.mlc_lock);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_ERROR("Failed to create ABT mutex: %d\n", rc);
		return rc;
	}

	rc = ABT_cond_create(&mlc.mlc_cond);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_ERROR("Failed to create ABT cond: %d\n", rc);
		goto destroy_lock;
	}

	while (remain_size) {
		read_size =
		    (remain_size > META_READ_BATCH_SIZE) ? META_READ_BATCH_SIZE : remain_size;

		D_ALLOC_PTR(mla);
		if (mla == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		mla->mla_off = off;
		mla->mla_read_size = read_size;
		mla->mla_start = start;
		mla->mla_store = store;
		mla->mla_control = &mlc;

		mlc.mlc_inflights++;
		rc = vos_exec(vos_meta_load_fn, (void *)mla);
		if (rc || mlc.mlc_rc) {
			if (rc) {
				D_FREE(mla);
				mlc.mlc_inflights--;
			}
			break;
		}

		if (mlc.mlc_inflights > META_READ_QD_NR) {
			ABT_cond_wait(mlc.mlc_cond, mlc.mlc_lock);
			D_ASSERT(mlc.mlc_inflights <= META_READ_QD_NR);
		}

		off += read_size;
		start += read_size;
		remain_size -= read_size;
	}

	mlc.mlc_wait_finished = 1;
	if (mlc.mlc_inflights > 0) {
		ABT_cond_wait(mlc.mlc_cond, mlc.mlc_lock);
		D_ASSERT(mlc.mlc_inflights == 0);
	}
	ABT_cond_free(&mlc.mlc_cond);

destroy_lock:
	ABT_mutex_free(&mlc.mlc_lock);
	return rc ? rc : mlc.mlc_rc;
}

struct vos_waitqueue {
	ABT_cond	vw_cond;
	ABT_mutex	vw_mutex;
};

static int
vos_waitqueue_create(void **ret_wq)
{
	struct vos_waitqueue	*wq;
	int			 rc;

	D_ALLOC_PTR(wq);
	if (wq == NULL)
		return -DER_NOMEM;

	rc = ABT_mutex_create(&wq->vw_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE(wq);
		return dss_abterr2der(rc);
	}
	rc = ABT_cond_create(&wq->vw_cond);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&wq->vw_mutex);
		D_FREE(wq);
		return dss_abterr2der(rc);
	}

	*ret_wq = wq;
	return 0;
}

static void
vos_waitqueue_destroy(void *arg)
{
	struct vos_waitqueue	*wq = arg;

	ABT_cond_free(&wq->vw_cond);
	ABT_mutex_free(&wq->vw_mutex);
	D_FREE(wq);
}

static void
vos_waitqueue_wait(void *arg, bool yield_only)
{
	struct vos_waitqueue	*wq = arg;

	if (yield_only) {
		ABT_thread_yield();
		return;
	}
	ABT_mutex_lock(wq->vw_mutex);
	ABT_cond_wait(wq->vw_cond, wq->vw_mutex);
	ABT_mutex_unlock(wq->vw_mutex);
}

static void
vos_waitqueue_wakeup(void *arg, bool wakeup_all)
{
	struct vos_waitqueue	*wq = arg;

	ABT_mutex_lock(wq->vw_mutex);
	if (wakeup_all)
		ABT_cond_broadcast(wq->vw_cond);
	else
		ABT_cond_signal(wq->vw_cond);
	ABT_mutex_unlock(wq->vw_mutex);
}

static inline int
vos_meta_writev(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	return vos_meta_rwv(store, iod, sgl, true);
}

static int
vos_meta_flush_prep(struct umem_store *store, struct umem_store_iod *iod, daos_handle_t *fh)
{
	struct bio_desc		*biod;
	struct bio_sglist	*bsgl;
	int			 rc;

	D_ASSERT(store && store->stor_priv != NULL);
	D_ASSERT(iod->io_nr > 0);
	D_ASSERT(fh != NULL);

	biod = bio_iod_alloc(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META),
			     NULL, 1, BIO_IOD_TYPE_UPDATE);
	if (biod == NULL)
		return -DER_NOMEM;

	bsgl = bio_iod_sgl(biod, 0);
	rc = bio_sgl_init(bsgl, iod->io_nr);
	if (rc)
		goto free;

	vos_iod2bsgl(store, iod, bsgl);

	rc = bio_iod_try_prep(biod, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc) {
		DL_CDEBUG(rc == -DER_AGAIN, DB_TRACE, DLOG_ERR, rc, "Failed to prepare DMA buffer");
		goto free;
	}

	fh->cookie = (uint64_t)biod;
	return 0;
free:
	bio_iod_free(biod);
	return rc;
}

static int
vos_meta_flush_copy(daos_handle_t fh, d_sg_list_t *sgl)
{
	struct bio_desc	*biod = (struct bio_desc *)fh.cookie;

	D_ASSERT(sgl->sg_nr > 0);
	return bio_iod_copy(biod, sgl, 1);
}

static int
vos_meta_flush_post(daos_handle_t fh, int err)
{
	struct bio_desc	*biod = (struct bio_desc *)fh.cookie;

	err = bio_iod_post(biod, err);
	bio_iod_free(biod);
	if (err) {
		DL_ERROR(err, "Checkpointing flush failed.");
		/* See the comment in vos_wal_commit() */
		if (err != -DER_NVME_IO) {
			D_ERROR("Checkpointing flush hit fatal error, kill engine...\n");
			err = kill(getpid(), SIGKILL);
			if (err != 0)
				D_ERROR("Failed to raise SIGKILL: %d\n", errno);
		}
	}

	return err;
}

#define VOS_WAL_DIR	"vos_wal"

void
vos_wal_metrics_init(struct vos_wal_metrics *vw_metrics, const char *path, int tgt_id)
{
	int	rc;

	/* Initialize metrics for WAL stats */
	rc = d_tm_add_metric(&vw_metrics->vwm_wal_sz, D_TM_STATS_GAUGE, "WAL tx size",
			     "bytes", "%s/%s/wal_sz/tgt_%d", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create WAL size telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_wal_qd, D_TM_STATS_GAUGE, "WAL tx QD",
			     "commits", "%s/%s/wal_qd/tgt_%d", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create WAL QD telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_wal_waiters, D_TM_STATS_GAUGE, "WAL waiters",
			     "transactions", "%s/%s/wal_waiters/tgt_%d", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create WAL waiters telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_wal_dur, D_TM_DURATION, "WAL commit duration", NULL,
			     "%s/%s/wal_dur/tgt_%d", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create WAL commit duration telemetry: " DF_RC "\n", DP_RC(rc));

	/* Initialize metrics for WAL replay */
	rc = d_tm_add_metric(&vw_metrics->vwm_replay_count, D_TM_COUNTER, "Number of WAL replays",
			     NULL, "%s/%s/replay_count/tgt_%u", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'replay_count' telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_replay_size, D_TM_GAUGE, "WAL replay size", "bytes",
			     "%s/%s/replay_size/tgt_%u", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'replay_size' telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_replay_time, D_TM_GAUGE, "WAL replay time", "us",
			     "%s/%s/replay_time/tgt_%u", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'replay_time' telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vw_metrics->vwm_replay_tx, D_TM_COUNTER,
			     "Number of replayed transactions", NULL,
			     "%s/%s/replay_transactions/tgt_%u", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'replay_transactions' telemetry: "DF_RC"\n", DP_RC(rc));


	rc = d_tm_add_metric(&vw_metrics->vwm_replay_ent, D_TM_COUNTER,
			     "Number of replayed log entries", NULL,
			     "%s/%s/replay_entries/tgt_%u", path, VOS_WAL_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'replay_entries' telemetry: "DF_RC"\n", DP_RC(rc));
}

#define VOS_CACHE_DIR	"vos_cache"

void
vos_cache_metrics_init(struct vos_cache_metrics *vc_metrics, const char *path, int tgt_id)
{
	int	rc;

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_ne, D_TM_GAUGE, "Non-evictable pages",
			     "pages", "%s/%s/page_ne/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create non-evictable pages telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_pinned, D_TM_GAUGE, "Pinned pages",
			     "pages", "%s/%s/page_pinned/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create pinned pages telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_free, D_TM_GAUGE, "Free pages",
			     "pages", "%s/%s/page_free/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create free pages telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_hit, D_TM_COUNTER, "Page cache hit",
			     "hits", "%s/%s/page_hit/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create page hit telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_miss, D_TM_COUNTER, "Page cache miss",
			     "misses", "%s/%s/page_miss/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create page miss telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_evict, D_TM_COUNTER, "Page cache evict",
			     "pages", "%s/%s/page_evict/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create page evict telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_flush, D_TM_COUNTER, "Page cache flush",
			     "pages", "%s/%s/page_flush/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create page flush telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_pg_load, D_TM_COUNTER, "Page cache load",
			     "pages", "%s/%s/page_load/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create page load telemetry.");

	rc = d_tm_add_metric(&vc_metrics->vcm_obj_hit, D_TM_COUNTER, "Object cache hit",
			     "hits", "%s/%s/obj_hit/tgt_%d", path, VOS_CACHE_DIR, tgt_id);
	if (rc)
		DL_WARN(rc, "Failed to create object hit telemetry.");

}

static inline struct vos_wal_metrics *
store2wal_metrics(struct umem_store *store)
{
	struct vos_pool_metrics	*vpm = (struct vos_pool_metrics *)store->stor_stats;

	return vpm != NULL ? &vpm->vp_wal_metrics : NULL;
}

static inline int
vos_wal_reserve(struct umem_store *store, uint64_t *tx_id)
{
	struct bio_wal_info	wal_info;
	struct vos_pool		*pool;
	struct bio_wal_stats	ws = { 0 };
	struct vos_wal_metrics	*vwm = store2wal_metrics(store);
	int			rc;

	pool = store->vos_priv;

	if (unlikely(pool == NULL))
		goto reserve; /** In case there is any race for checkpoint init. */

	/** Update checkpoint state before reserve to ensure we activate checkpointing if there
	 *  is any space pressure in the WAL.
	 */
	bio_wal_query(store->stor_priv, &wal_info);

	pool->vp_update_cb(pool->vp_chkpt_arg, wal_info.wi_commit_id, wal_info.wi_used_blks,
			   wal_info.wi_tot_blks);

reserve:
	D_ASSERT(store && store->stor_priv != NULL);
	rc = bio_wal_reserve(store->stor_priv, tx_id, (vwm != NULL) ? &ws : NULL);
	if (rc == 0 && vwm != NULL)
		d_tm_set_gauge(vwm->vwm_wal_waiters, ws.ws_waiters);

	return rc;
}

static inline int
vos_wal_commit(struct umem_store *store, struct umem_wal_tx *wal_tx, void *data_iod)
{
	struct bio_wal_info     wal_info;
	struct vos_pool        *pool;
	struct bio_wal_stats    ws = {0};
	struct vos_wal_metrics *vwm = store2wal_metrics(store);
	int                     rc;

	D_ASSERT(store && store->stor_priv != NULL);
	if (vwm != NULL)
		d_tm_mark_duration_start(vwm->vwm_wal_dur, D_TM_CLOCK_REALTIME);
	rc = bio_wal_commit(store->stor_priv, wal_tx, data_iod, (vwm != NULL) ? &ws : NULL);
	if (vwm != NULL)
		d_tm_mark_duration_end(vwm->vwm_wal_dur);
	if (rc) {
		DL_ERROR(rc, "WAL commit failed.");
		/*
		 * WAL commit could fail due to faulty NVMe or other fatal errors like ENOMEM
		 * or software bug.
		 *
		 * On NVMe I/O error, the NVMe device should have been marked as faulty (in
		 * the BIO module), and a series actions will be automatically triggered to
		 * take DOWN all impacted pool targets. We just suppress the error here since
		 * the caller (DAV) can't cope with commit error.
		 *
		 * On other fatal error, the best we can do is killing the engine...
		 */
		if (rc != -DER_NVME_IO) {
			D_ERROR("WAL commit hit fatal error, kill engine...\n");
			rc = kill(getpid(), SIGKILL);
			if (rc != 0)
				D_ERROR("Failed to raise SIGKILL: %d\n", errno);
		}
		store->store_faulty = true;
	} else if (vwm != NULL) {
		d_tm_set_gauge(vwm->vwm_wal_sz, ws.ws_size);
		d_tm_set_gauge(vwm->vwm_wal_qd, ws.ws_qd);
	}

	bio_wal_query(store->stor_priv, &wal_info);
	umem_cache_commit(store, wal_info.wi_commit_id);

	pool = store->vos_priv;
	if (unlikely(pool == NULL))
		return 0; /** In case there is any race for checkpoint init. */

	/** Update checkpoint state after commit in case there is an active checkpoint waiting
	 *  for this commit to finish.
	 */
	pool->vp_update_cb(pool->vp_chkpt_arg, wal_info.wi_commit_id, wal_info.wi_used_blks,
			   wal_info.wi_tot_blks);

	return 0;
}

static inline int
vos_wal_replay(struct umem_store *store,
	       int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *arg),
	       void *arg)
{
	struct bio_wal_rp_stats	 wrs;
	struct vos_wal_metrics	*vwm = store2wal_metrics(store);
	int			 rc;

	D_ASSERT(store && store->stor_priv != NULL);
	rc = bio_wal_replay(store->stor_priv, (vwm != NULL) ? &wrs : NULL, replay_cb, arg);

	/* VOS file rehydration metrics */
	if (vwm != NULL && rc >= 0) {
		d_tm_inc_counter(vwm->vwm_replay_count, 1);
		d_tm_set_gauge(vwm->vwm_replay_size, wrs.wrs_sz);
		d_tm_set_gauge(vwm->vwm_replay_time, wrs.wrs_tm);
		d_tm_inc_counter(vwm->vwm_replay_tx, wrs.wrs_tx_cnt);
		d_tm_inc_counter(vwm->vwm_replay_ent, wrs.wrs_entries);
	}
	return rc;
}

static inline int
vos_wal_id_cmp(struct umem_store *store, uint64_t id1, uint64_t id2)
{
	D_ASSERT(store && store->stor_priv != NULL);
	return bio_wal_id_cmp(store->stor_priv, id1, id2);
}

struct umem_store_ops vos_store_ops = {
	.so_waitqueue_create	= vos_waitqueue_create,
	.so_waitqueue_destroy	= vos_waitqueue_destroy,
	.so_waitqueue_wait	= vos_waitqueue_wait,
	.so_waitqueue_wakeup	= vos_waitqueue_wakeup,
	.so_load	= vos_meta_load,
	.so_read	= vos_meta_readv,
	.so_write	= vos_meta_writev,
	.so_flush_prep	= vos_meta_flush_prep,
	.so_flush_copy	= vos_meta_flush_copy,
	.so_flush_post	= vos_meta_flush_post,
	.so_wal_reserv	= vos_wal_reserve,
	.so_wal_submit	= vos_wal_commit,
	.so_wal_replay	= vos_wal_replay,
	.so_wal_id_cmp	= vos_wal_id_cmp,
};

#define	CHKPT_TELEMETRY_DIR	"checkpoint"

void
vos_chkpt_metrics_init(struct vos_chkpt_metrics *vc_metrics, const char *path, int tgt_id)
{
	int rc;

	rc = d_tm_add_metric(&vc_metrics->vcm_duration, D_TM_DURATION, "Checkpoint duration", NULL,
			     "%s/%s/duration/tgt_%d", path, CHKPT_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("failed to create checkpoint_duration metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vc_metrics->vcm_dirty_pages, D_TM_STATS_GAUGE,
			     "Number of dirty page blocks checkpointed", "16MiB",
			     "%s/%s/dirty_pages/tgt_%d", path, CHKPT_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("failed to create checkpoint_dirty_pages metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vc_metrics->vcm_dirty_chunks, D_TM_STATS_GAUGE,
			     "Number of umem chunks checkpointed", "4KiB",
			     "%s/%s/dirty_chunks/tgt_%d", path, CHKPT_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("failed to create checkpoint_dirty_chunks metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vc_metrics->vcm_iovs_copied, D_TM_STATS_GAUGE,
			     "Number of sgl iovs used to copy dirty chunks", NULL,
			     "%s/%s/iovs_copied/tgt_%d", path, CHKPT_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("failed to create checkpoint_iovs_copied metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&vc_metrics->vcm_wal_purged, D_TM_STATS_GAUGE,
			     "Size of WAL purged by the checkpoint", "4KiB",
			     "%s/%s/wal_purged/tgt_%d", path, CHKPT_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("failed to create checkpoint_wal_purged metric: "DF_RC"\n", DP_RC(rc));

}

void
vos_pool_checkpoint_init(daos_handle_t poh, vos_chkpt_update_cb_t update_cb,
			 vos_chkpt_wait_cb_t wait_cb, void *arg, struct umem_store **storep)
{
	struct vos_pool      *pool;
	struct umem_instance *umm;
	struct umem_store    *store;
	struct bio_wal_info   wal_info;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	umm   = vos_pool2umm(pool);
	store = &umm->umm_pool->up_store;

	pool->vp_update_cb = update_cb;
	pool->vp_wait_cb   = wait_cb;
	pool->vp_chkpt_arg = arg;
	store->vos_priv    = pool;

	*storep = store;

	bio_wal_query(store->stor_priv, &wal_info);

	/** Set the initial values */
	update_cb(arg, wal_info.wi_commit_id, wal_info.wi_used_blks, wal_info.wi_tot_blks);
}

void
vos_pool_checkpoint_fini(daos_handle_t poh)
{
	struct vos_pool      *pool;
	struct umem_instance *umm;
	struct umem_store    *store;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	umm   = vos_pool2umm(pool);
	store = &umm->umm_pool->up_store;

	pool->vp_update_cb = NULL;
	pool->vp_wait_cb   = NULL;
	pool->vp_chkpt_arg = NULL;
	store->vos_priv    = NULL;
}

bool
vos_pool_needs_checkpoint(daos_handle_t poh)
{
	struct vos_pool *pool;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	/** TODO: Revisit. */
	return bio_nvme_configured(SMD_DEV_TYPE_META);
}

int
vos_pool_checkpoint(daos_handle_t poh)
{
	struct vos_pool               *pool;
	uint64_t                       tx_id;
	struct umem_instance          *umm;
	struct umem_store             *store;
	struct bio_wal_info            wal_info;
	int                            rc;
	uint64_t                       purge_size = 0;
	struct umem_cache_chkpt_stats  stats = { 0 };
	struct vos_chkpt_metrics      *chkpt_metrics = NULL;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	umm   = vos_pool2umm(pool);
	store = &umm->umm_pool->up_store;

	if (pool->vp_metrics != NULL)
		chkpt_metrics = &pool->vp_metrics->vp_chkpt_metrics;

	if (chkpt_metrics != NULL)
		d_tm_mark_duration_start(chkpt_metrics->vcm_duration, D_TM_CLOCK_REALTIME);

	bio_wal_query(store->stor_priv, &wal_info);
	tx_id = wal_info.wi_commit_id;
	if (tx_id == wal_info.wi_ckp_id) {
		D_DEBUG(DB_TRACE, "No checkpoint needed for "DF_UUID"\n", DP_UUID(pool->vp_id));
		return 0;
	}

	D_DEBUG(DB_MD, "Checkpoint started pool=" DF_UUID ", committed_id=" DF_X64 "\n",
		DP_UUID(pool->vp_id), tx_id);

	rc = bio_meta_clear_empty(store->stor_priv);
	if (rc)
		return rc;

	rc = umem_cache_checkpoint(store, pool->vp_wait_cb, pool->vp_chkpt_arg, &tx_id, &stats);

	if (rc == 0)
		rc = bio_wal_checkpoint(store->stor_priv, tx_id, &purge_size);

	bio_wal_query(store->stor_priv, &wal_info);

	/* Update the used block info post checkpoint */
	pool->vp_update_cb(pool->vp_chkpt_arg, wal_info.wi_commit_id, wal_info.wi_used_blks,
			   wal_info.wi_tot_blks);

	D_DEBUG(DB_MD,
		"Checkpoint finished pool=" DF_UUID ", committed_id=" DF_X64 ", rc=" DF_RC "\n",
		DP_UUID(pool->vp_id), tx_id, DP_RC(rc));

	if (chkpt_metrics != NULL) {
		d_tm_mark_duration_end(chkpt_metrics->vcm_duration);
		if (!rc) {
			d_tm_set_gauge(chkpt_metrics->vcm_dirty_pages, stats.uccs_nr_pages);
			d_tm_set_gauge(chkpt_metrics->vcm_dirty_chunks, stats.uccs_nr_dchunks);
			d_tm_set_gauge(chkpt_metrics->vcm_iovs_copied, stats.uccs_nr_iovs);
			d_tm_set_gauge(chkpt_metrics->vcm_wal_purged, purge_size);
		}
	}
	return rc;
}

int
vos_pool_settings_init(bool md_on_ssd)
{
	return umempobj_settings_init(md_on_ssd);
}

static inline enum bio_mc_flags
vos2mc_flags(unsigned int vos_flags)
{
	enum bio_mc_flags mc_flags = 0;

	if (vos_flags & VOS_POF_RDB)
		mc_flags |= BIO_MC_FL_RDB;

	if (vos_flags & VOS_POF_FOR_RECREATE)
		mc_flags |= BIO_MC_FL_RECREATE;

	return mc_flags;
}

static inline void
init_umem_store(struct umem_store *store, struct bio_meta_context *mc)
{
	bio_meta_get_attr(mc, &store->stor_size, &store->stor_blk_size, &store->stor_hdr_blks,
			  (uint8_t *)&store->store_type, &store->store_evictable);
	store->stor_priv = mc;
	store->stor_ops = &vos_store_ops;

	/* Legacy BMEM V1 pool without backend type stored */
	if (bio_nvme_configured(SMD_DEV_TYPE_META) && store->store_type == DAOS_MD_PMEM)
		store->store_type = DAOS_MD_BMEM;
}

static int
vos_pool_store_type(daos_size_t scm_sz, daos_size_t meta_sz)
{
	int backend;

	backend = umempobj_get_backend_type();
	D_ASSERT((meta_sz != 0) && (scm_sz != 0));

	if (scm_sz > meta_sz) {
		D_ERROR("memsize %lu is greater than metasize %lu", scm_sz, meta_sz);
		return -DER_INVAL;
	}

	if (scm_sz < meta_sz) {
		if (backend != DAOS_MD_BMEM_V2) {
			D_ERROR("scm_sz %lu is less than meta_sz %lu", scm_sz, meta_sz);
			return -DER_INVAL;
		}
	}

	return backend;
}

int
vos_pool_roundup_size(daos_size_t *scm_sz, daos_size_t *meta_sz)
{
	size_t alignsz;
	int    rc;

	D_ASSERT(*scm_sz != 0);
	rc = vos_pool_store_type(*scm_sz, *meta_sz ? *meta_sz : *scm_sz);
	if (rc < 0)
		return rc;

	/* Round up the size such that it is compatible with backend */
	alignsz  = umempobj_pgsz(rc);
	*scm_sz  = max(D_ALIGNUP(*scm_sz, alignsz), 1 << 24);
	if (*meta_sz)
		*meta_sz = max(D_ALIGNUP(*meta_sz, alignsz), 1 << 24);

	return 0;
}

static int
vos_pmemobj_create(const char *path, uuid_t pool_id, const char *layout,
		   size_t scm_sz, size_t nvme_sz, size_t wal_sz, size_t meta_sz,
		   unsigned int flags, struct umem_pool **ph)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct umem_store	 store = { 0 };
	struct bio_meta_context	*mc;
	struct umem_pool	*pop = NULL;
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	int			 rc, ret;
	size_t                   scm_sz_actual;

	*ph = NULL;
	/* always use PMEM mode for SMD */
	if (flags & VOS_POF_SYSDB) {
		store.store_type = DAOS_MD_PMEM;
		store.store_standalone = true;
		goto umem_create;
	}

	/* No NVMe is configured or current xstream doesn't have NVMe context */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL) {
		store.store_type = DAOS_MD_PMEM;
		goto umem_create;
	}

	if (!scm_sz) {
		struct stat lstat;

		rc = stat(path, &lstat);
		if (rc != 0)
			return daos_errno2der(errno);
		scm_sz_actual = lstat.st_size;
	} else
		scm_sz_actual = scm_sz;

	/* Is meta_sz is set then use it, otherwise derive from VOS file size or scm_sz */
	if (!meta_sz)
		meta_sz = scm_sz_actual;

	rc = vos_pool_store_type(scm_sz_actual, meta_sz);
	if (rc < 0) {
		D_ERROR("Failed to determine the store type for xs:%p pool:"DF_UUID". "DF_RC,
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}
	store.store_type = rc;

	D_DEBUG(DB_MGMT, "Create BIO meta context for xs:%p pool:"DF_UUID" "
		"scm_sz: %zu meta_sz: %zu, nvme_sz: %zu wal_sz:%zu backend:%d\n",
		xs_ctxt, DP_UUID(pool_id), scm_sz, meta_sz, nvme_sz, wal_sz, store.store_type);

	rc = bio_mc_create(xs_ctxt, pool_id, scm_sz_actual, meta_sz, wal_sz, nvme_sz, mc_flags,
			   store.store_type);
	if (rc != 0) {
		D_ERROR("Failed to create BIO meta context for xs:%p pool:"DF_UUID". "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	rc = bio_mc_open(xs_ctxt, pool_id, mc_flags, &mc);
	if (rc != 0) {
		D_ERROR("Failed to open BIO meta context for xs:%p pool:"DF_UUID". "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));

		ret = bio_mc_destroy(xs_ctxt, pool_id, mc_flags);
		if (ret)
			D_ERROR("Failed to destroy BIO meta context. "DF_RC"\n", DP_RC(ret));

		return rc;
	}

	init_umem_store(&store, mc);

umem_create:
	D_DEBUG(DB_MGMT, "umempobj_create sz: " DF_U64 " store_sz: " DF_U64, scm_sz,
		store.stor_size);
	pop = umempobj_create(path, layout, UMEMPOBJ_ENABLE_STATS, scm_sz, 0600, &store);
	if (pop != NULL) {
		*ph = pop;
		return 0;
	}
	rc = daos_errno2der(errno);
	D_ASSERT(rc != 0);

	if (store.stor_priv != NULL) {
		ret = bio_mc_close(store.stor_priv);
		if (ret) {
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(ret));
			return rc;
		}
		ret = bio_mc_destroy(xs_ctxt, pool_id, mc_flags);
		if (ret)
			D_ERROR("Failed to destroy BIO meta context. "DF_RC"\n", DP_RC(ret));
	}

	return rc;
}

static int
vos_pmemobj_open(const char *path, uuid_t pool_id, const char *layout, unsigned int flags,
		 void *metrics, struct umem_pool **ph)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct umem_store	 store = { 0 };
	struct bio_meta_context	*mc;
	struct umem_pool	*pop;
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	int			 rc, ret;

	*ph = NULL;
	/* always use PMEM mode for SMD */
	if (flags & VOS_POF_SYSDB) {
		store.store_type = DAOS_MD_PMEM;
		store.store_standalone = true;
		goto umem_open;
	}

	/* No NVMe is configured or current xstream doesn't have NVMe context */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL) {
		store.store_type = DAOS_MD_PMEM;
		goto umem_open;
	}

	D_DEBUG(DB_MGMT, "Open BIO meta context for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(pool_id));

	rc = bio_mc_open(xs_ctxt, pool_id, mc_flags, &mc);
	if (rc) {
		D_ERROR("Failed to open BIO meta context for xs:%p pool:"DF_UUID", "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	init_umem_store(&store, mc);
	store.stor_stats = metrics;

umem_open:
	pop = umempobj_open(path, layout, UMEMPOBJ_ENABLE_STATS, &store);
	if (pop != NULL) {
		*ph = pop;
		return 0;
	}
	rc = daos_errno2der(errno);
	D_ASSERT(rc != 0);

	if (store.stor_priv != NULL) {
		ret = bio_mc_close(store.stor_priv);
		if (ret)
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(ret));
	}

	return rc;
}

static inline void
vos_pmemobj_close(struct umem_pool *pop)
{
	struct umem_store	store;
	int			rc;

	store = pop->up_store;

	umempobj_close(pop);

	if (store.stor_priv != NULL) {
		rc = bio_mc_close(store.stor_priv);
		if (rc)
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(rc));
	}
}

static inline struct vos_pool_df *
vos_pool_pop2df(struct umem_pool *pop)
{
	return (struct vos_pool_df *)
		umempobj_get_rootptr(pop, sizeof(struct vos_pool_df));
}

static struct vos_pool *
pool_hlink2ptr(struct d_ulink *hlink)
{
	D_ASSERT(hlink != NULL);
	return container_of(hlink, struct vos_pool, vp_hlink);
}

static void
vos_delete_blob(uuid_t pool_uuid, unsigned int flags)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	int			 rc;

	/* NVMe device isn't configured */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL)
		return;

	D_DEBUG(DB_MGMT, "Deleting blob for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(pool_uuid));

	rc = bio_mc_destroy(xs_ctxt, pool_uuid, mc_flags);
	if (rc)
		D_ERROR("Destroying meta context blob for xs:%p pool="DF_UUID" failed: "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_uuid), DP_RC(rc));

	return;
}

static inline void
pool_free(struct vos_pool *pool)
{
	if (pool->vp_cond != ABT_COND_NULL)
		ABT_cond_free(&pool->vp_cond);

	if (pool->vp_mutex != ABT_MUTEX_NULL)
		ABT_mutex_free(&pool->vp_mutex);

	D_FREE(pool);
}

static void
pool_hop_free(struct d_ulink *hlink)
{
	struct vos_pool		*pool = pool_hlink2ptr(hlink);
	int			 rc;

	D_ASSERT(pool->vp_opened == 0);
	D_ASSERT(!gc_have_pool(pool));

	if (pool->vp_vea_info != NULL)
		vea_unload(pool->vp_vea_info);

	if (daos_handle_is_valid(pool->vp_cont_th))
		dbtree_close(pool->vp_cont_th);

	if (pool->vp_size != 0) {
		rc = munlock((void *)pool->vp_umm.umm_base, pool->vp_size);
		if (rc != 0)
			D_WARN("Failed to unlock pool memory at "DF_X64": errno=%d (%s)\n",
			       pool->vp_umm.umm_base, errno, strerror(errno));
		else
			D_DEBUG(DB_MGMT, "Unlocked VOS pool memory: "DF_U64" bytes at "DF_X64"\n",
				pool->vp_size, pool->vp_umm.umm_base);
	}

	if (pool->vp_uma.uma_pool)
		vos_pmemobj_close(pool->vp_uma.uma_pool);

	vos_dedup_fini(pool);

	if (pool->vp_dummy_ioctxt) {
		rc = bio_ioctxt_close(pool->vp_dummy_ioctxt);
		if (rc != 0)
			D_WARN("Failed to close dummy ioctxt: rc=%d\n", rc);
	}

	if (pool->vp_dying)
		vos_delete_blob(pool->vp_id, pool->vp_rdb ? VOS_POF_RDB : 0);

	pool_free(pool);
}

static bool
pool_hop_cmp(struct d_ulink *ulink, void *cmp_args)
{
	struct vos_pool	*pool;
	bool		 show_all;

	D_ASSERT(cmp_args != NULL);
	show_all = *(bool *)cmp_args;
	pool = container_of(ulink, struct vos_pool, vp_hlink);
	if (!pool->vp_opening || show_all)
		return true;

	return false;
}

static struct d_ulink_ops   pool_uuid_hops = {
	.uop_free       = pool_hop_free,
	.uop_cmp	= pool_hop_cmp,
};

/** allocate DRAM instance of vos pool */
static int
pool_alloc(uuid_t uuid, struct vos_pool **pool_p)
{
	struct vos_pool		*pool;
	int			 rc;

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		return -DER_NOMEM;

	rc = ABT_mutex_create(&pool->vp_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE(pool);
		return dss_abterr2der(rc);
	}

	rc = ABT_cond_create(&pool->vp_cond);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&pool->vp_mutex);
		D_FREE(pool);
		return dss_abterr2der(rc);
	}

	d_uhash_ulink_init(&pool->vp_hlink, &pool_uuid_hops);
	D_INIT_LIST_HEAD(&pool->vp_gc_link);
	D_INIT_LIST_HEAD(&pool->vp_gc_cont);
	uuid_copy(pool->vp_id, uuid);

	*pool_p = pool;
	return 0;
}

static int
pool_lookup(struct d_uuid *ukey, struct vos_pool **pool_p, bool show_all)
{
	struct vos_pool		*pool;
	struct d_ulink		*hlink;
	struct d_hash_table	*htable;

	htable = vos_pool_hhash_get(uuid_compare(ukey->uuid, *vos_db_pool_uuid()) == 0);

again:
	hlink = d_uhash_link_lookup(htable, ukey, &show_all);
	if (hlink == NULL) {
		D_DEBUG(DB_MGMT, "can't find "DF_UUID"\n", DP_UUID(ukey->uuid));
		return -DER_NONEXIST;
	}

	pool = pool_hlink2ptr(hlink);
	if (unlikely(pool->vp_opening)) {
		D_ASSERT(show_all);

		ABT_mutex_lock(pool->vp_mutex);
		if (likely(pool->vp_opening))
			ABT_cond_wait(pool->vp_cond, pool->vp_mutex);
		ABT_mutex_unlock(pool->vp_mutex);
		vos_pool_decref(pool);
		goto again;
	}

	*pool_p = pool;
	return 0;
}

static int
vos_blob_format_cb(void *cb_data)
{
	struct bio_blob_hdr	*blob_hdr = cb_data;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct bio_io_context	*ioctxt;
	int			 rc;

	/* Create a bio_io_context to get the blob */
	rc = bio_ioctxt_open(&ioctxt, xs_ctxt, blob_hdr->bbh_pool, false);
	if (rc) {
		D_ERROR("Failed to create an I/O context for writing blob "
			"header: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Write the blob header info to blob offset 0 */
	rc = bio_write_blob_hdr(ioctxt, blob_hdr);
	if (rc)
		D_ERROR("Failed to write header for blob:"DF_U64" : "DF_RC"\n",
			blob_hdr->bbh_blob_id, DP_RC(rc));

	rc = bio_ioctxt_close(ioctxt);
	if (rc)
		D_ERROR("Failed to free I/O context: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Unmap (TRIM) the extent being freed
 */
static int
vos_blob_unmap_cb(d_sg_list_t *unmap_sgl, uint32_t blk_sz, void *data)
{
	struct bio_io_context	*ioctxt = data;
	int			 rc;

	/* unmap unused pages for NVMe media to perform more efficiently */
	rc = bio_blob_unmap_sgl(ioctxt, unmap_sgl, blk_sz);
	if (rc)
		D_ERROR("Blob unmap SGL failed. "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
pool_open_prep(uuid_t uuid, unsigned int flags, struct vos_pool **p_pool);

static int
pool_open_post(struct umem_pool **p_ph, struct vos_pool_df *pool_df, unsigned int flags,
	       void *metrics, struct vos_pool *pool, int ret);

int
vos_pool_create_ex(const char *path, uuid_t uuid, daos_size_t scm_sz, daos_size_t nvme_sz,
		   daos_size_t wal_sz, daos_size_t meta_sz, unsigned int flags, uint32_t version,
		   daos_handle_t *poh)
{
	struct umem_pool	*ph;
	struct umem_attr	 uma = {0};
	struct umem_instance	 umem = {0};
	struct vos_pool_df	*pool_df = NULL;
	struct bio_blob_hdr	 blob_hdr;
	uint32_t		 vea_compat = 0;
	daos_handle_t		 hdl;
	struct d_uuid		 ukey;
	struct vos_pool		*pool = NULL;
	struct vos_pool_ext_df  *pd_ext_df;
	int			 rc = 0;

	if (!path || uuid_is_null(uuid) || daos_file_is_dax(path))
		return -DER_INVAL;

	if (version == 0)
		version = POOL_DF_VERSION;
	else if (version < POOL_DF_VER_1 || version > POOL_DF_VERSION)
		return -DER_INVAL;

	D_DEBUG(DB_MGMT,
		"Pool Path: %s, size: " DF_U64 ":" DF_U64 ":" DF_U64 ", "
		"UUID: " DF_UUID ", version: %u\n",
		path, scm_sz, nvme_sz, meta_sz, DP_UUID(uuid), version);

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	uuid_copy(ukey.uuid, uuid);
	rc = pool_lookup(&ukey, &pool, true);
	if (rc == 0) {
		D_ASSERT(pool != NULL);
		D_ERROR("Found already opened(%d) pool:%p dying(%d)\n",
			pool->vp_opened, pool, pool->vp_dying);
		vos_pool_decref(pool);
		return -DER_EXIST;
	}

	/* Path must be a file with a certain size when size argument is 0 */
	if (!scm_sz && access(path, F_OK | R_OK | W_OK) == -1) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return daos_errno2der(errno);
	}

	/*
	 * The caller may not want to open the pool, but we still need to insert the pool
	 * handle into the hash table to handle concurrent pool_create or pool_open.
	 */
	rc = pool_open_prep(uuid, flags, &pool);
	if (rc != 0)
		return rc;

	rc = vos_pmemobj_create(path, uuid, VOS_POOL_LAYOUT, scm_sz, nvme_sz, wal_sz, meta_sz,
				flags, &ph);
	if (rc) {
		D_ERROR("Failed to create pool %s, scm_sz="DF_U64", nvme_sz="DF_U64", meta_sz="
			DF_U64". "DF_RC"\n", path, scm_sz, nvme_sz, meta_sz, DP_RC(rc));
		goto post;
	}

	pool_df = vos_pool_pop2df(ph);

	/* If the file is fallocated separately we need the fallocated size
	 * for setting in the root object.
	 */
	if (!scm_sz) {
		struct stat lstat;

		rc = stat(path, &lstat);
		if (rc != 0)
			D_GOTO(post, rc = daos_errno2der(errno));
		scm_sz = lstat.st_size;
	}

	uma.uma_id = umempobj_backend_type2class_id(ph->up_store.store_type);
	uma.uma_pool = ph;

	rc = umem_class_init(&uma, &umem);
	if (rc != 0)
		goto post;

	rc = umem_tx_begin(&umem, NULL);
	if (rc != 0)
		goto post;

	rc = umem_tx_add_ptr(&umem, pool_df, sizeof(*pool_df));
	if (rc != 0)
		goto end;

	memset(pool_df, 0, sizeof(*pool_df));

	pool_df->pd_ext = umem_zalloc(&umem, sizeof(struct vos_pool_ext_df));
	if (UMOFF_IS_NULL(pool_df->pd_ext)) {
		D_ERROR("Failed to allocate pool df extension.\n");
		rc = -DER_NOSPACE;
		goto end;
	}

	rc = gc_init_pool(&umem, pool_df);
	if (rc)
		goto end;

	rc = dbtree_create_inplace(VOS_BTR_CONT_TABLE, 0, VOS_CONT_ORDER,
				   &uma, &pool_df->pd_cont_root, &hdl);
	if (rc != 0)
		goto end;

	dbtree_close(hdl);

	uuid_copy(pool_df->pd_id, uuid);
	/* Use meta-blob size as scm if present */
	pool_df->pd_scm_sz      = (meta_sz) ? meta_sz : scm_sz;
	pool_df->pd_nvme_sz	= nvme_sz;
	pool_df->pd_magic	= POOL_DF_MAGIC;
	if (DAOS_FAIL_CHECK(FLC_POOL_DF_VER))
		pool_df->pd_version = 0;
	else
		pool_df->pd_version = version;

	/* pd_ext is newly allocated, no need to call tx_add_ptr() */
	pd_ext_df             = umem_off2ptr(&umem, pool_df->pd_ext);
	pd_ext_df->ped_mem_sz = scm_sz;
end:
	/**
	 * The transaction can in reality be aborted
	 * only when there is no memory, either due
	 * to loss of power or no more memory in pool
	 */
	if (rc == 0)
		rc = umem_tx_commit(&umem);
	else
		rc = umem_tx_abort(&umem, rc);

	if (rc != 0) {
		D_ERROR("Initialize pool root error: "DF_RC"\n", DP_RC(rc));
		goto post;
	}

	/* SCM only pool or data blob isn't configured */
	if (nvme_sz == 0 || !bio_nvme_configured(SMD_DEV_TYPE_DATA))
		goto post;

	/* Format SPDK blob header */
	blob_hdr.bbh_blk_sz = VOS_BLK_SZ;
	blob_hdr.bbh_hdr_sz = VOS_BLOB_HDR_BLKS;
	uuid_copy(blob_hdr.bbh_pool, uuid);

	/* Determine VEA compatibility bits */
	/* TODO: only enable bitmap for large pool size */
	if (version >= VOS_POOL_DF_2_6)
		vea_compat |= VEA_COMPAT_FEATURE_BITMAP;

	/* Format SPDK blob*/
	rc = vea_format(&umem, vos_txd_get(flags & VOS_POF_SYSDB), &pool_df->pd_vea_df,
			VOS_BLK_SZ, VOS_BLOB_HDR_BLKS, nvme_sz, vos_blob_format_cb,
			&blob_hdr, false, vea_compat);
	if (rc) {
		D_ERROR("Format blob error for pool:"DF_UUID". "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto post;
	}

post:
	if (rc == 0 && poh != NULL) {
		rc = pool_open_post(&ph, pool_df, flags, NULL, pool, rc);
		if (rc == 0)
			*poh = vos_pool2hdl(pool);
	} else {
		pool->vp_opening = 0;
		ABT_cond_broadcast(pool->vp_cond);
		vos_pool_hash_del(pool);
		vos_pool_decref(pool);
	}

	/* Close this local handle, if it hasn't been consumed nor already
	 * been closed by pool_open upon error.
	 */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz, daos_size_t data_sz,
		daos_size_t meta_sz, unsigned int flags, uint32_t version, daos_handle_t *poh)
{
	/* create vos pool with default WAL size */
	return vos_pool_create_ex(path, uuid, scm_sz, data_sz, 0, meta_sz, flags, version, poh);
}

/**
 * kill the pool before destroy:
 * - detach from GC, delete SPDK blob
 */
int
vos_pool_kill(uuid_t uuid, unsigned int flags)
{
	struct d_uuid	ukey;
	int		rc;

	uuid_copy(ukey.uuid, uuid);
	while (1) {
		struct vos_pool	*pool = NULL;

		rc = pool_lookup(&ukey, &pool, true);
		if (rc) {
			D_ASSERT(rc == -DER_NONEXIST);
			rc = 0;
			break;
		}
		D_ASSERT(pool->vp_sysdb == false);

		D_ASSERT(pool != NULL);
		if (gc_have_pool(pool)) {
			/* still pinned by GC, un-pin it because there is no
			 * need to run GC for this pool anymore.
			 */
			gc_del_pool(pool);
			vos_pool_decref(pool); /* -1 for lookup */
			continue;	/* try again */
		}
		pool->vp_dying = 1;
		vos_pool_decref(pool); /* -1 for lookup */

		ras_notify_eventf(RAS_POOL_DEFER_DESTROY, RAS_TYPE_INFO, RAS_SEV_WARNING,
				  NULL, NULL, NULL, NULL, &ukey.uuid, NULL, NULL, NULL, NULL,
				  "pool:"DF_UUID" destroy is deferred", DP_UUID(uuid));
		/* Blob destroy will be deferred to last vos_pool ref drop */
		return -DER_BUSY;
	}
	D_DEBUG(DB_MGMT, DF_UUID": No open handles, OK to delete: flags=%x\n", DP_UUID(uuid),
		flags);

	vos_delete_blob(uuid, flags);
	return 0;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP) and revoke all its handles
 */
int
vos_pool_destroy_ex(const char *path, uuid_t uuid, unsigned int flags)
{
	int	rc;

	D_DEBUG(DB_MGMT, "delete path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = vos_pool_kill(uuid, flags);
	if (rc)
		return rc;

	if (daos_file_is_dax(path))
		return -DER_INVAL;

	/**
	 * NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	rc = remove(path);
	if (rc) {
		if (errno == ENOENT)
			D_GOTO(exit, rc = 0);
		D_ERROR("Failure deleting file from PMEM: %s\n",
			strerror(errno));
	}
exit:
	return rc;
}

int
vos_pool_destroy(const char *path, uuid_t uuid)
{
	return vos_pool_destroy_ex(path, uuid, 0);
}

enum {
	/** Memory locking flag not initialized */
	LM_FLAG_UNINIT,
	/** Memory locking disabled */
	LM_FLAG_DISABLED,
	/** Memory locking enabled */
	LM_FLAG_ENABLED
};

static void
lock_pool_memory(struct vos_pool *pool)
{
	static int	lock_mem = LM_FLAG_UNINIT;
	struct rlimit	rlim;
	size_t		lock_bytes;
	int		rc;

	if (lock_mem == LM_FLAG_UNINIT) {
		rc = getrlimit(RLIMIT_MEMLOCK, &rlim);
		if (rc != 0) {
			D_WARN("getrlimit() failed; errno=%d (%s)\n", errno, strerror(errno));
			lock_mem = LM_FLAG_DISABLED;
			return;
		}

		if (rlim.rlim_cur != RLIM_INFINITY || rlim.rlim_max != RLIM_INFINITY) {
			D_WARN("Infinite rlimit not detected, not locking VOS pool memory\n");
			lock_mem = LM_FLAG_DISABLED;
			return;
		}

		lock_mem = LM_FLAG_ENABLED;
	}

	if (lock_mem == LM_FLAG_DISABLED)
		return;

	/*
	 * Mlock may take several tens of seconds to complete when memory
	 * is tight, so mlock is skipped in current MD-on-SSD scenario.
	 */
	if (bio_nvme_configured(SMD_DEV_TYPE_META))
		return;

	lock_bytes = pool->vp_pool_df->pd_scm_sz;
	rc = mlock((void *)pool->vp_umm.umm_base, lock_bytes);
	if (rc != 0) {
		D_WARN("Could not lock memory for VOS pool "DF_U64" bytes at "DF_X64
		       "; errno=%d (%s)\n", lock_bytes, pool->vp_umm.umm_base,
		       errno, strerror(errno));
		return;
	}

	/* Only save the size if the locking was successful */
	pool->vp_size = lock_bytes;
	D_DEBUG(DB_MGMT, "Locking VOS pool in memory "DF_U64" bytes at "DF_X64"\n", pool->vp_size,
		pool->vp_umm.umm_base);
}

static int
pool_open_prep(uuid_t uuid, unsigned int flags, struct vos_pool **p_pool)
{
	struct vos_pool		*pool = NULL;
	struct d_uuid		 ukey;
	int			 rc;
	bool			 show_all = true;

	/* Create a new handle during open */
	rc = pool_alloc(uuid, &pool); /* returned with refcount=1 */
	if (rc != 0) {
		D_ERROR("Error allocating pool handle\n");
		return rc;
	}

	pool->vp_opening = 1;
	pool->vp_sysdb = !!(flags & VOS_POF_SYSDB);
	pool->vp_excl = !!(flags & VOS_POF_EXCL);
	pool->vp_small = !!(flags & VOS_POF_SMALL);
	pool->vp_rdb = !!(flags & VOS_POF_RDB);

	/*
	 * Insert the pool into the uuid hash table before full opened, because subsequent
	 * pool_open process maybe yield and other pool_lookup for the same UUID will find
	 * the in-processing pool_open, then wait there and avoid concurrent pool_open.
	 */
	uuid_copy(ukey.uuid, uuid);
	rc = d_uhash_link_insert(vos_pool_hhash_get(pool->vp_sysdb), &ukey, &show_all,
				 &pool->vp_hlink);
	if (rc != 0) {
		D_ERROR("uuid hash table insert for pool " DF_UUID " failed: " DF_RC "\n",
			DP_UUID(uuid), DP_RC(rc));
		pool_free(pool);
	} else {
		/* Here, two references on the pool: one is for myself, the other for the hash. */
		*p_pool = pool;
	}

	return rc;
}

static int
pool_open_post(struct umem_pool **p_ph, struct vos_pool_df *pool_df, unsigned int flags,
	       void *metrics, struct vos_pool *pool, int ret)
{
	struct umem_attr	*uma;
	int			 rc;

	if (ret != 0)
		D_GOTO(out, rc = ret);

	D_ASSERT(pool_df != NULL);

	uma = &pool->vp_uma;
	uma->uma_pool = *p_ph;
	uma->uma_id = umempobj_backend_type2class_id(uma->uma_pool->up_store.store_type);

	/* initialize a umem instance for later btree operations */
	rc = umem_class_init(uma, &pool->vp_umm);
	if (rc != 0)
		goto out;

	/* It has been taken by uma->uma_pool and will be closed when pool_hop_free(). */
	*p_ph = NULL;

	if (pool_df->pd_version >= VOS_POOL_DF_2_4)
		pool->vp_feats |= VOS_POOL_FEAT_2_4;
	if (pool_df->pd_version >= VOS_POOL_DF_2_6)
		pool->vp_feats |= VOS_POOL_FEAT_2_6;
	if (pool_df->pd_version >= VOS_POOL_DF_2_8)
		pool->vp_feats |= VOS_POOL_FEAT_2_8;
	pool->vp_pool_df = pool_df;

	/* Initialize dummy data I/O context */
	rc = bio_ioctxt_open(&pool->vp_dummy_ioctxt, vos_xsctxt_get(), pool->vp_id, true);
	if (rc) {
		D_ERROR("Failed to open dummy I/O context. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/* Cache container table btree hdl */
	rc = dbtree_open_inplace_ex(&pool_df->pd_cont_root, &pool->vp_uma,
				    DAOS_HDL_INVAL, pool, &pool->vp_cont_th);
	if (rc) {
		D_ERROR("Container Tree open failed\n");
		goto out;
	}

	pool->vp_metrics = metrics;
	if (!(flags & VOS_POF_FOR_FEATURE_FLAG) && bio_nvme_configured(SMD_DEV_TYPE_DATA) &&
	    pool_df->pd_nvme_sz != 0) {
		struct vea_unmap_context	 unmap_ctxt;
		struct vos_pool_metrics		*vp_metrics = metrics;
		void				*vea_metrics = NULL;

		if (vp_metrics)
			vea_metrics = vp_metrics->vp_vea_metrics;
		/* set unmap callback fp */
		unmap_ctxt.vnc_unmap = vos_blob_unmap_cb;
		unmap_ctxt.vnc_data = vos_data_ioctxt(pool);
		unmap_ctxt.vnc_ext_flush = flags & VOS_POF_EXTERNAL_FLUSH;
		rc = vea_load(&pool->vp_umm, vos_txd_get(flags & VOS_POF_SYSDB),
			      &pool_df->pd_vea_df, &unmap_ctxt, vea_metrics, &pool->vp_vea_info);
		if (rc) {
			D_ERROR("Failed to load block space info: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		if (pool->vp_vea_info == NULL)
			/** always store on SCM if no bdev */
			pool->vp_data_thresh = 0;
		else
			pool->vp_data_thresh = DAOS_PROP_PO_DATA_THRESH_DEFAULT;
	}

	rc = vos_dedup_init(pool);
	if (rc)
		goto out;

	rc = gc_open_pool(pool);
	if (rc)
		goto out;

	pool->vp_opened = 1;
	vos_space_sys_init(pool);
	/* Ensure GC is triggered after server restart */
	gc_add_pool(pool);
	lock_pool_memory(pool);

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DB_MGMT, rc,
		  "Open pool " DF_UUID "(%p) with df version %d",
		  DP_UUID(pool->vp_id), pool, pool_df != NULL ? pool_df->pd_version : -1);
	pool->vp_opening = 0;
	ABT_cond_broadcast(pool->vp_cond);
	if (rc != 0) {
		vos_pool_hash_del(pool);
		vos_pool_decref(pool);
	}
	return rc;
}

int
vos_pool_open_metrics(const char *path, uuid_t uuid, unsigned int flags, void *metrics,
		      daos_handle_t *poh)
{
	struct vos_pool_df	*pool_df = NULL;
	struct vos_pool		*pool = NULL;
	struct umem_pool	*ph = NULL;
	struct d_uuid		 ukey;
	int			 rc;

	if (path == NULL || poh == NULL) {
		D_ERROR("Invalid parameters.\n");
		return -DER_INVAL;
	}

	if (unlikely(flags & VOS_POF_SKIP_UUID_CHECK)) {
		D_ERROR("Do not support SKIP_UUID_CHECK flags (%x) via regular pool open API\n",
			VOS_POF_SKIP_UUID_CHECK);
		return -DER_NOTSUPPORTED;
	}

	D_DEBUG(DB_MGMT, "Pool Path: %s, UUID: "DF_UUID"\n", path,
		DP_UUID(uuid));

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	uuid_copy(ukey.uuid, uuid);

	rc = pool_lookup(&ukey, &pool, true);
	if (rc == 0) {
		D_ASSERT(pool != NULL);
		D_DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
			pool->vp_opened, pool);
		if (pool->vp_dying) {
			D_ERROR("Found dying pool : %p\n", pool);
			vos_pool_decref(pool);
			return -DER_BUSY;
		}
		if (!(flags & VOS_POF_FOR_CHECK_QUERY) &&
		    ((flags & VOS_POF_EXCL) || pool->vp_excl)) {
			vos_pool_decref(pool);
			return -DER_BUSY;
		}
		pool->vp_opened++;
		*poh = vos_pool2hdl(pool);
		return 0;
	}

	rc = pool_open_prep(uuid, flags, &pool);
	if (rc != 0)
		return rc;

	rc = bio_xsctxt_health_check(vos_xsctxt_get(), false, false);
	if (rc) {
		DL_WARN(rc, DF_UUID": Skip pool open due to faulty NVMe.", DP_UUID(uuid));
		goto out;
	}

	rc = vos_pmemobj_open(path, uuid, VOS_POOL_LAYOUT, flags, metrics, &ph);
	if (rc) {
		D_ERROR("Error in opening the pool "DF_UUID". "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out;
	}

	pool_df = vos_pool_pop2df(ph);
	if (pool_df->pd_magic != POOL_DF_MAGIC) {
		D_CRIT("Unknown DF magic %x\n", pool_df->pd_magic);
		rc = -DER_DF_INVAL;
		goto out;
	}

	if (pool_df->pd_version > POOL_DF_VERSION ||
	    pool_df->pd_version < POOL_DF_VER_1) {
		D_ERROR("Unsupported DF version %x\n", pool_df->pd_version);
		/** Send a RAS notification */
		vos_report_layout_incompat("VOS pool", pool_df->pd_version,
					   POOL_DF_VER_1, POOL_DF_VERSION,
					   &ukey.uuid);
		rc = -DER_DF_INCOMPT;
		goto out;
	}

	if (uuid_compare(uuid, pool_df->pd_id)) {
		D_ERROR("Mismatch uuid, user="DF_UUIDF", pool="DF_UUIDF"\n",
			DP_UUID(uuid), DP_UUID(pool_df->pd_id));
		rc = -DER_ID_MISMATCH;
		goto out;
	}

out:
	rc = pool_open_post(&ph, pool_df, flags, metrics, pool, rc);
	if (rc == 0)
		*poh = vos_pool2hdl(pool);

	/* Close this local handle, if it hasn't been consumed nor already
	 * been closed by pool_open upon error.
	 */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

int
vos_pool_open(const char *path, uuid_t uuid, unsigned int flags, daos_handle_t *poh)
{
	return vos_pool_open_metrics(path, uuid, flags, NULL, poh);
}

int
vos_pool_upgrade(daos_handle_t poh, uint32_t version)
{
	struct vos_pool    *pool;
	struct vos_pool_df *pool_df;
	int                 rc = 0;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	pool_df = pool->vp_pool_df;

	if (version <= pool_df->pd_version) {
		D_INFO(DF_UUID ": Ignore pool durable format upgrade from version %u to %u\n",
		       DP_UUID(pool->vp_id), pool_df->pd_version, version);
		return 0;
	}

	D_INFO(DF_UUID ": Attempting pool durable format upgrade from %d to %d\n",
	       DP_UUID(pool->vp_id), pool_df->pd_version, version);
	D_ASSERTF(version > pool_df->pd_version && version <= POOL_DF_VERSION,
		  "Invalid pool upgrade version %d, current version is %d\n", version,
		  pool_df->pd_version);

	if (version >= VOS_POOL_DF_2_6 && pool_df->pd_version < VOS_POOL_DF_2_6 &&
	    pool->vp_vea_info)
		rc = vea_upgrade(pool->vp_vea_info, &pool->vp_umm, &pool_df->pd_vea_df, version);
	if (rc)
		return rc;

	rc = umem_tx_begin(&pool->vp_umm, NULL);
	if (rc != 0)
		return rc;

	rc = umem_tx_add_ptr(&pool->vp_umm, &pool_df->pd_version, sizeof(pool_df->pd_version));
	if (rc != 0)
		goto end;

	pool_df->pd_version = version;

end:
	rc = umem_tx_end(&pool->vp_umm, rc);

	if (rc != 0)
		return rc;

	if (version >= VOS_POOL_DF_2_2)
		pool->vp_feats |= VOS_POOL_FEAT_2_2;
	if (version >= VOS_POOL_DF_2_4)
		pool->vp_feats |= VOS_POOL_FEAT_2_4;
	if (version >= VOS_POOL_DF_2_6)
		pool->vp_feats |= VOS_POOL_FEAT_2_6;
	if (version >= VOS_POOL_DF_2_8)
		pool->vp_feats |= VOS_POOL_FEAT_2_8;

	return 0;
}

/**
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 */
int
vos_pool_close(daos_handle_t poh)
{
	struct vos_pool	*pool;

	pool = vos_hdl2pool(poh);
	if (pool == NULL) {
		D_ERROR("Cannot close a NULL handle\n");
		return -DER_NO_HDL;
	}
	D_DEBUG(DB_MGMT, "Close opened(%d) pool "DF_UUID" (%p).\n",
		pool->vp_opened, DP_UUID(pool->vp_id), pool);

	D_ASSERT(pool->vp_opened > 0);
	pool->vp_opened--;

	/* If the last reference is holding by GC */
	if (pool->vp_opened == 1 && gc_have_pool(pool)) {
		gc_del_pool(pool);
	} else if (pool->vp_opened == 0) {
		vos_pool_hash_del(pool);
		gc_close_pool(pool);
	}

	vos_pool_decref(pool); /* -1 for myself */
	return 0;
}

/**
 * Query attributes and statistics of the current pool
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo)
{
	struct vos_pool		*pool;
	struct vos_pool_df	*pool_df;
	int			 rc;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	pool_df = pool->vp_pool_df;

	D_ASSERT(pinfo != NULL);
	pinfo->pif_cont_nr = pool_df->pd_cont_nr;
	pinfo->pif_gc_stat = pool->vp_gc_stat_global;

	/*
	 * NOTE: The chk_pool_info::cpi_statistics contains the inconsistency statistics during
	 *	 phase range [CSP_DTX_RESYNC, CSP_AGGREGATION] for the pool shard on the target.
	 *	 Related information will be filled in subsequent CR project milestone.
	 */
	memset(&pinfo->pif_chk, 0, sizeof(pinfo->pif_chk));

	rc = vos_space_query(pool, &pinfo->pif_space, true);
	if (rc)
		D_ERROR("Query pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
	return rc;
}

int
vos_pool_query_space(uuid_t pool_id, struct vos_pool_space *vps)
{
	struct vos_pool	*pool = NULL;
	struct d_uuid	 ukey;
	int		 rc;

	uuid_copy(ukey.uuid, pool_id);
	rc = pool_lookup(&ukey, &pool, false);
	if (rc) {
		D_ASSERT(rc == -DER_NONEXIST);
		return rc;
	}

	D_ASSERT(pool != NULL);
	D_ASSERT(pool->vp_sysdb == false);
	rc = vos_space_query(pool, vps, false);
	vos_pool_decref(pool);
	return rc;
}

int
vos_pool_space_sys_set(daos_handle_t poh, daos_size_t *space_sys)
{
	struct vos_pool	*pool = vos_hdl2pool(poh);

	if (pool == NULL)
		return -DER_NO_HDL;
	if (space_sys == NULL)
		return -DER_INVAL;

	return vos_space_sys_set(pool, space_sys);
}

int
vos_pool_ctl(daos_handle_t poh, enum vos_pool_opc opc, void *param)
{
	struct vos_pool		*pool;
	int			i;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	switch (opc) {
	default:
		return -DER_NOSYS;
	case VOS_PO_CTL_RESET_GC:
		memset(&pool->vp_gc_stat_global, 0, sizeof(pool->vp_gc_stat_global));
		break;
	case VOS_PO_CTL_SET_DATA_THRESH:
		if (param == NULL)
			return -DER_INVAL;

		if (pool->vp_vea_info == NULL)
			/** no bdev, discard request */
			break;

		pool->vp_data_thresh = *((uint32_t *)param);
		break;
	case VOS_PO_CTL_SET_SPACE_RB:
		if (param == NULL)
			return -DER_INVAL;

		i = *((unsigned int *)param);
		if (i >= 100 || i < 0) {
			D_ERROR("Invalid space reserve ratio for rebuild. %d\n", i);
			return -DER_INVAL;
		}
		pool->vp_space_rb = i;
		break;
	}

	return 0;
}

/** Convenience function to return address of a bio_addr in pmem.  If it's a hole or NVMe address,
 *  it returns NULL.
 */
const void *
vos_pool_biov2addr(daos_handle_t poh, struct bio_iov *biov)
{
	struct vos_pool *pool;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	if (bio_addr_is_hole(&biov->bi_addr))
		return NULL;

	if (bio_iov2media(biov) == DAOS_MEDIA_NVME)
		return NULL;

	return umem_off2ptr(vos_pool2umm(pool), bio_iov2raw_off(biov));
}

bool
vos_pool_feature_skip_start(daos_handle_t poh)
{
	struct vos_pool *vos_pool;

	vos_pool = vos_hdl2pool(poh);
	D_ASSERT(vos_pool != NULL);

	return vos_pool->vp_pool_df->pd_compat_flags & VOS_POOL_COMPAT_FLAG_SKIP_START;
}

bool
vos_pool_feature_immutable(daos_handle_t poh)
{
	struct vos_pool *vos_pool;

	vos_pool = vos_hdl2pool(poh);
	D_ASSERT(vos_pool != NULL);

	return vos_pool->vp_pool_df->pd_compat_flags & VOS_POOL_COMPAT_FLAG_IMMUTABLE;
}

bool
vos_pool_feature_skip_rebuild(daos_handle_t poh)
{
	struct vos_pool *vos_pool;

	vos_pool = vos_hdl2pool(poh);
	D_ASSERT(vos_pool != NULL);

	return vos_pool->vp_pool_df->pd_compat_flags & VOS_POOL_COMPAT_FLAG_SKIP_REBUILD;
}

bool
vos_pool_feature_skip_dtx_resync(daos_handle_t poh)
{
	struct vos_pool *vos_pool;

	vos_pool = vos_hdl2pool(poh);
	D_ASSERT(vos_pool != NULL);

	return vos_pool->vp_pool_df->pd_compat_flags & VOS_POOL_COMPAT_FLAG_SKIP_DTX_RESYNC;
}
