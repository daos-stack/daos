/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Common internal functions for VOS
 * vos/vos_common.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <fcntl.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/lru.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>
#include <daos_srv/ras.h>
#include <daos_srv/daos_engine.h>
#include <vos_internal.h>

struct vos_self_mode {
	struct vos_tls		*self_tls;
	struct bio_xs_context	*self_xs_ctxt;
	pthread_mutex_t		 self_lock;
	bool			 self_nvme_init;
	int			 self_ref;
};

struct vos_self_mode		 self_mode = {
	.self_lock	= PTHREAD_MUTEX_INITIALIZER,
};

#define DF_MAX_BUF 128
void
vos_report_layout_incompat(const char *type, int version, int min_version,
			   int max_version, uuid_t *uuid)
{
	char buf[DF_MAX_BUF];

	snprintf(buf, DF_MAX_BUF, "Incompatible %s may not be opened. Version"
		 " %d is outside acceptable range %d-%d", type, version,
		 min_version, max_version);
	buf[DF_MAX_BUF - 1] = 0; /* Shut up any static analyzers */

	if (ds_notify_ras_event == NULL) {
		D_CRIT("%s\n", buf);
		return;
	}

	ds_notify_ras_event(RAS_POOL_DF_INCOMPAT, buf, RAS_TYPE_INFO,
			    RAS_SEV_ERROR, NULL, NULL, NULL, NULL, uuid,
			    NULL, NULL, NULL, NULL);
}

struct vos_tls *
vos_tls_get(void)
{
#ifdef VOS_STANDALONE
	return self_mode.self_tls;
#else
	return dss_module_key_get(dss_tls_get(), &vos_module_key);
#endif
}

/** Add missing timestamp cache entries.  This should be called
 *  when execution may have been short circuited by a non-existent
 *  entity so we can fill in the negative timestamps before doing
 *  timestamp updates.
 */
void
vos_ts_add_missing(struct vos_ts_set *ts_set, daos_key_t *dkey, int akey_nr,
		   struct vos_akey_data *ad)
{
	daos_key_t	*akey;
	int		 i, rc, remaining;

	if (!vos_ts_in_tx(ts_set) || dkey == NULL)
		return;

	if (ts_set->ts_etype == VOS_TS_TYPE_DKEY) {
		/** Add the negative dkey entry */
		rc = vos_ts_set_add(ts_set, 0 /* don't care */,
				    dkey->iov_buf,
				    (int)dkey->iov_len);
		D_ASSERT(rc == 0);
	}

	remaining = (VOS_TS_TYPE_AKEY + akey_nr) - ts_set->ts_init_count;

	/** Add negative akey entries */
	for (i = akey_nr - remaining; i < akey_nr; i++) {
		akey = ad->ad_is_iod ?
			&ad->ad_iods[i].iod_name : &ad->ad_keys[i];
		rc = vos_ts_set_add(ts_set, 0 /* don't care */,
				    akey->iov_buf,
				    (int)akey->iov_len);
		D_ASSERT(rc == 0);
	}
}

#ifdef VOS_STANDALONE
int
vos_profile_start(char *path, int avg)
{
	struct vos_tls *tls = vos_tls_get();
	struct daos_profile *dp;
	int rc;

	if (tls == NULL)
		return 0;

	rc = daos_profile_init(&dp, path, avg, 0, 0);
	if (rc)
		return rc;

	tls->vtl_dp = dp;
	return 0;
}

void
vos_profile_stop()
{
	struct vos_tls *tls = vos_tls_get();

	if (tls == NULL || tls->vtl_dp == NULL)
		return;

	daos_profile_dump(tls->vtl_dp);
	daos_profile_destroy(tls->vtl_dp);
	tls->vtl_dp = NULL;
}

#endif

struct bio_xs_context *
vos_xsctxt_get(void)
{
#ifdef VOS_STANDALONE
	return self_mode.self_xs_ctxt;
#else
	return dss_get_module_info()->dmi_nvme_ctxt;
#endif
}

int
vos_bio_addr_free(struct vos_pool *pool, bio_addr_t *addr, daos_size_t nob)
{
	int	rc;

	if (bio_addr_is_hole(addr))
		return 0;

	if (addr->ba_type == DAOS_MEDIA_SCM) {
		rc = umem_free(&pool->vp_umm, addr->ba_off);
	} else {
		uint64_t blk_off;
		uint32_t blk_cnt;

		D_ASSERT(addr->ba_type == DAOS_MEDIA_NVME);
		blk_off = vos_byte2blkoff(addr->ba_off);
		blk_cnt = vos_byte2blkcnt(nob);

		rc = vea_free(pool->vp_vea_info, blk_off, blk_cnt);
		if (rc)
			D_ERROR("Error on block ["DF_U64", %u] free. "DF_RC"\n",
				blk_off, blk_cnt, DP_RC(rc));
	}
	return rc;
}

static int
vos_tx_publish(struct dtx_handle *dth, bool publish)
{
	struct vos_container	*cont = vos_hdl2cont(dth->dth_coh);
	struct dtx_rsrvd_uint	*dru;
	struct vos_rsrvd_scm	*scm;
	int			 rc;
	int			 i;

	if (dth->dth_rsrvds == NULL)
		return 0;

	for (i = 0; i < dth->dth_rsrvd_cnt; i++) {
		dru = &dth->dth_rsrvds[i];
		rc = vos_publish_scm(cont, dru->dru_scm, publish);
		D_FREE(dru->dru_scm);

		/* FIXME: Currently, vos_publish_blocks() will release
		 *	  reserved information in 'dru_nvme_list' from
		 *	  DRAM. So if vos_publish_blocks() failed, the
		 *	  reserve information in DRAM for those former
		 *	  published ones cannot be rollback. That will
		 *	  cause space leaking before in-memory reserve
		 *	  information synced with persistent allocation
		 *	  heap until the server restart.
		 *
		 *	  It is not fatal, will be handled later.
		 */
		if (rc && publish)
			return rc;

		/** Function checks if list is empty */
		rc = vos_publish_blocks(cont, &dru->dru_nvme,
					publish, VOS_IOS_GENERIC);
		if (rc && publish)
			return rc;
	}

	for (i = 0; i < dth->dth_deferred_cnt; i++) {
		scm = dth->dth_deferred[i];
		rc = vos_publish_scm(cont, scm, publish);
		D_FREE(dth->dth_deferred[i]);

		if (rc && publish)
			return rc;
	}

	/** Handle the deferred NVMe cancellations */
	vos_publish_blocks(cont, &dth->dth_deferred_nvme, false, VOS_IOS_GENERIC);

	return 0;
}

int
vos_tx_begin(struct dtx_handle *dth, struct umem_instance *umm)
{
	int	rc;

	if (dth == NULL)
		return umem_tx_begin(umm, vos_txd_get());

	/** Note: On successful return, dth tls gets set and will be cleared by the corresponding
	 *        call to vos_tx_end.  This is to avoid ever keeping that set after a call to
	 *        umem_tx_end, which may yield for bio operations.
	 */

	if (dth->dth_local_tx_started) {
		vos_dth_set(dth);
		return 0;
	}

	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc == 0) {
		dth->dth_local_tx_started = 1;
		vos_dth_set(dth);
	}

	return rc;
}

int
vos_tx_end(struct vos_container *cont, struct dtx_handle *dth_in,
	   struct vos_rsrvd_scm **rsrvd_scmp, d_list_t *nvme_exts,
	   bool started, int err)
{
	struct dtx_handle	*dth = dth_in;
	struct dtx_rsrvd_uint	*dru;
	struct vos_dtx_cmt_ent	*dce = NULL;
	struct dtx_handle	 tmp = {0};

	if (!dtx_is_valid_handle(dth)) {
		/** Created a dummy dth handle for publishing extents */
		dth = &tmp;
		tmp.dth_modification_cnt = dth->dth_op_seq = 1;
		tmp.dth_local_tx_started = started ? 1 : 0;
		tmp.dth_rsrvds = &dth->dth_rsrvd_inline;
		tmp.dth_coh = vos_cont2hdl(cont);
		D_INIT_LIST_HEAD(&tmp.dth_deferred_nvme);
	}

	if (rsrvd_scmp != NULL) {
		D_ASSERT(nvme_exts != NULL);
		dru = &dth->dth_rsrvds[dth->dth_rsrvd_cnt++];
		dru->dru_scm = *rsrvd_scmp;
		*rsrvd_scmp = NULL;

		D_INIT_LIST_HEAD(&dru->dru_nvme);
		d_list_splice_init(nvme_exts, &dru->dru_nvme);
	}

	if (!dth->dth_local_tx_started)
		goto cancel;

	/* Not the last modification. */
	if (err == 0 && dth->dth_modification_cnt > dth->dth_op_seq) {
		vos_dth_set(NULL);
		return 0;
	}

	dth->dth_local_tx_started = 0;

	if (dtx_is_valid_handle(dth_in) && err == 0)
		err = vos_dtx_prepared(dth, &dce);

	if (err == 0)
		err = vos_tx_publish(dth, true);

	vos_dth_set(NULL);

	err = umem_tx_end(vos_cont2umm(cont), err);

cancel:
	if (err != 0) {
		/* The transaction aborted or failed to commit. */
		vos_tx_publish(dth, false);
		if (dtx_is_valid_handle(dth_in))
			vos_dtx_cleanup_internal(dth);
	}

	if (dce != NULL) {
		struct vos_dtx_act_ent	*dae = dth_in->dth_ent;

		vos_dtx_post_handle(cont, &dae, &dce, 1, false,
				    err != 0 ? true : false);
		dth_in->dth_ent = NULL;
	}

	return err;
}

/**
 * VOS in-memory structure creation.
 * Handle-hash:
 * -----------
 * Uses in-memory daos_uuid hash to maintain one
 * reference per thread in heap for each pool/container.
 * Calls to pool/container open/close track references
 * through internal refcounting.
 *
 * Object-cache:
 * ------------
 * In-memory object cache for object index in PMEM
 * Created once for standalone mode and once for every
 * TLS instance.
 */

static void
vos_tls_fini(void *data)
{
	struct vos_tls *tls = data;

	/* All GC callers should have exited, but they can still leave
	 * uncleaned pools behind. It is OK to free these pool handles with
	 * leftover, because GC can clean up leftover when it starts again.
	 */
	D_ASSERTF(tls->vtl_gc_running == 0, "GC running = %d\n",
		  tls->vtl_gc_running);

	while (!d_list_empty(&tls->vtl_gc_pools)) {
		struct vos_pool *pool;

		pool = d_list_entry(tls->vtl_gc_pools.next,
				    struct vos_pool, vp_gc_link);
		gc_del_pool(pool);
	}

	if (tls->vtl_ocache)
		vos_obj_cache_destroy(tls->vtl_ocache);

	if (tls->vtl_pool_hhash)
		d_uhash_destroy(tls->vtl_pool_hhash);

	if (tls->vtl_cont_hhash)
		d_uhash_destroy(tls->vtl_cont_hhash);

	umem_fini_txd(&tls->vtl_txd);
	if (tls->vtl_ts_table)
		vos_ts_table_free(&tls->vtl_ts_table);
	D_FREE(tls);
}

static void *
vos_tls_init(int xs_id, int tgt_id)
{
	struct vos_tls *tls;
	int		rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&tls->vtl_gc_pools);
	rc = vos_obj_cache_create(LRU_CACHE_BITS, &tls->vtl_ocache);
	if (rc) {
		D_ERROR("Error in creating object cache\n");
		goto failed;
	}

	rc = d_uhash_create(D_HASH_FT_NOLOCK, VOS_POOL_HHASH_BITS,
			    &tls->vtl_pool_hhash);
	if (rc) {
		D_ERROR("Error in creating POOL ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	rc = d_uhash_create(D_HASH_FT_NOLOCK | D_HASH_FT_EPHEMERAL,
			    VOS_CONT_HHASH_BITS, &tls->vtl_cont_hhash);
	if (rc) {
		D_ERROR("Error in creating CONT ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	rc = umem_init_txd(&tls->vtl_txd);
	if (rc) {
		D_ERROR("Error in creating txd: %d\n", rc);
		goto failed;
	}

	rc = vos_ts_table_alloc(&tls->vtl_ts_table);
	if (rc) {
		D_ERROR("Error in creating timestamp table: %d\n", rc);
		goto failed;
	}

	if (tgt_id < 0)
		/** skip sensor setup on standalone vos & sys xstream */
		return tls;

	rc = d_tm_add_metric(&tls->vtl_committed, D_TM_STATS_GAUGE,
			     "Number of committed entries kept around for reply"
			     " reconstruction", "entries",
			     "io/dtx/committed/tgt_%u", tgt_id);
	if (rc)
		D_WARN("Failed to create committed cnt sensor: "DF_RC"\n",
		       DP_RC(rc));

	return tls;
failed:
	vos_tls_fini(tls);
	return NULL;
}

struct dss_module_key vos_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = vos_tls_init,
	.dmk_fini = vos_tls_fini,
};

daos_epoch_t	vos_start_epoch = DAOS_EPOCH_MAX;

static int
vos_mod_init(void)
{
	int	 rc = 0;

	if (vos_start_epoch == DAOS_EPOCH_MAX)
		vos_start_epoch = crt_hlc_get();

	rc = vos_pool_settings_init();
	if (rc != 0) {
		D_ERROR("VOS pool setting initialization error\n");
		return rc;
	}

	rc = vos_cont_tab_register();
	if (rc) {
		D_ERROR("VOS CI btree initialization error\n");
		return rc;
	}

	rc = vos_dtx_table_register();
	if (rc) {
		D_ERROR("DTX btree initialization error\n");
		return rc;
	}

	/**
	 * Registering the class for OI btree
	 * and KV btree
	 */
	rc = vos_obj_tab_register();
	if (rc) {
		D_ERROR("VOS OI btree initialization error\n");
		return rc;
	}

	rc = obj_tree_register();
	if (rc) {
		D_ERROR("Failed to register vos trees\n");
		return rc;
	}

	rc = vos_ilog_init();
	if (rc)
		D_ERROR("Failed to initialize incarnation log capability\n");

	d_getenv_int("DAOS_VOS_AGG_THRESH", &vos_agg_nvme_thresh);
	if (vos_agg_nvme_thresh == 0 || vos_agg_nvme_thresh > 256)
		vos_agg_nvme_thresh = VOS_MW_NVME_THRESH;
	/* Round down to 2^n blocks */
	if (vos_agg_nvme_thresh > 1)
		vos_agg_nvme_thresh = (vos_agg_nvme_thresh / 2) * 2;

	D_INFO("Set aggregate NVMe record threshold to %u blocks (blk_sz:%lu).\n",
	       vos_agg_nvme_thresh, VOS_BLK_SZ);

	return rc;
}

static int
vos_mod_fini(void)
{
	return 0;
}

static inline int
vos_metrics_count(void)
{
	return vea_metrics_count();
}

static void
vos_metrics_free(void *data)
{
	struct vos_pool_metrics *vp_metrics = data;

	if (vp_metrics->vp_vea_metrics != NULL)
		vea_metrics_free(vp_metrics->vp_vea_metrics);
	D_FREE(data);
}

#define VOS_AGG_DIR	"vos_aggregation"

static inline char *
agg_op2str(unsigned int agg_op)
{
	switch (agg_op) {
	case AGG_OP_SCAN:
		return "scanned";
	case AGG_OP_SKIP:
		return "skipped";
	case AGG_OP_DEL:
		return "deleted";
	default:
		return "unknown";
	}
}

static void *
vos_metrics_alloc(const char *path, int tgt_id)
{
	struct vos_pool_metrics	*vp_metrics;
	struct vos_agg_metrics	*vam;
	char			 desc[40];
	int			 i, rc;

	D_ASSERT(tgt_id >= 0);

	D_ALLOC_PTR(vp_metrics);
	if (vp_metrics == NULL)
		return NULL;

	vp_metrics->vp_vea_metrics = vea_metrics_alloc(path, tgt_id);
	if (vp_metrics->vp_vea_metrics == NULL) {
		vos_metrics_free(vp_metrics);
		return NULL;
	}

	vam = &vp_metrics->vp_agg_metrics;

	/* VOS aggregation EPR scan duration */
	rc = d_tm_add_metric(&vam->vam_epr_dur, D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME,
			     "EPR scan duration", NULL, "%s/%s/epr_duration/tgt_%u",
			     path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'epr_duration' telemetry: "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation scanned/skipped/deleted objs/dkeys/akeys */
	for (i = 0; i < AGG_OP_MERGE; i++) {
		snprintf(desc, sizeof(desc), "%s objs", agg_op2str(i));
		rc = d_tm_add_metric(&vam->vam_obj[i], D_TM_COUNTER, desc, NULL,
				     "%s/%s/obj_%s/tgt_%u", path, VOS_AGG_DIR,
				     agg_op2str(i), tgt_id);
		if (rc)
			D_WARN("Failed to create 'obj_%s' telemetry : "DF_RC"\n",
			       agg_op2str(i), DP_RC(rc));

		snprintf(desc, sizeof(desc), "%s dkeys", agg_op2str(i));
		rc = d_tm_add_metric(&vam->vam_dkey[i], D_TM_COUNTER, desc, NULL,
				     "%s/%s/dkey_%s/tgt_%u", path, VOS_AGG_DIR,
				     agg_op2str(i), tgt_id);
		if (rc)
			D_WARN("Failed to create 'dkey_%s' telemetry : "DF_RC"\n",
			       agg_op2str(i), DP_RC(rc));

		snprintf(desc, sizeof(desc), "%s akeys", agg_op2str(i));
		rc = d_tm_add_metric(&vam->vam_akey[i], D_TM_COUNTER, desc, NULL,
				     "%s/%s/akey_%s/tgt_%u", path, VOS_AGG_DIR,
				     agg_op2str(i), tgt_id);
		if (rc)
			D_WARN("Failed to create 'akey_%s' telemetry : "DF_RC"\n",
			       agg_op2str(i), DP_RC(rc));
	}

	/* VOS aggregation hit uncommitted entries */
	rc = d_tm_add_metric(&vam->vam_uncommitted, D_TM_COUNTER, "uncommitted entries", NULL,
			     "%s/%s/uncommitted/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'uncommitted' telemetry : "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation hit CSUM errors */
	rc = d_tm_add_metric(&vam->vam_csum_errs, D_TM_COUNTER, "CSUM errors", NULL,
			     "%s/%s/csum_errors/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'csum_errors' telemetry : "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation SV deletions */
	rc = d_tm_add_metric(&vam->vam_del_sv, D_TM_COUNTER, "deleted single values", NULL,
			     "%s/%s/deleted_sv/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'deleted_sv' telemetry : "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation EV deletions */
	rc = d_tm_add_metric(&vam->vam_del_ev, D_TM_COUNTER, "deleted array values", NULL,
			     "%s/%s/deleted_ev/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'deleted_ev' telemetry : "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation total merged recx */
	rc = d_tm_add_metric(&vam->vam_merge_recs, D_TM_COUNTER, "total merged recs", NULL,
			     "%s/%s/merged_recs/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'merged_recs' telemetry : "DF_RC"\n", DP_RC(rc));

	/* VOS aggregation total merged size */
	rc = d_tm_add_metric(&vam->vam_merge_size, D_TM_COUNTER, "total merged size", "bytes",
			     "%s/%s/merged_size/tgt_%u", path, VOS_AGG_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create 'merged_size' telemetry : "DF_RC"\n", DP_RC(rc));

	return vp_metrics;
}

struct dss_module_metrics vos_metrics = {
	.dmm_tags = DAOS_TGT_TAG,
	.dmm_init = vos_metrics_alloc,
	.dmm_fini = vos_metrics_free,
	.dmm_nr_metrics = vos_metrics_count,
};

struct dss_module vos_srv_module =  {
	.sm_name	= "vos_srv",
	.sm_mod_id	= DAOS_VOS_MODULE,
	.sm_ver		= 1,
	.sm_proto_count	= 1,
	.sm_init	= vos_mod_init,
	.sm_fini	= vos_mod_fini,
	.sm_key		= &vos_module_key,
	.sm_metrics	= &vos_metrics,
};

static void
vos_self_nvme_fini(void)
{
	if (self_mode.self_xs_ctxt != NULL) {
		bio_xsctxt_free(self_mode.self_xs_ctxt);
		self_mode.self_xs_ctxt = NULL;
	}
	if (self_mode.self_nvme_init) {
		bio_nvme_fini();
		self_mode.self_nvme_init = false;
	}
}

/* Storage path, NVMe config & numa node used by standalone VOS */
#define VOS_NVME_CONF		"daos_nvme.conf"
#define VOS_NVME_NUMA_NODE	DAOS_NVME_NUMANODE_NONE
#define VOS_NVME_MEM_SIZE	1024
#define VOS_NVME_HUGEPAGE_SIZE	2	/* 2MB */
#define VOS_NVME_NR_TARGET	1

static int
vos_self_nvme_init(const char *vos_path)
{
	char	*nvme_conf;
	int	 rc, fd;

	D_ASSERT(vos_path != NULL);
	D_ASPRINTF(nvme_conf, "%s/%s", vos_path, VOS_NVME_CONF);
	if (nvme_conf == NULL)
		return -DER_NOMEM;

	/* IV tree used by VEA */
	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		goto out;

	/* Only use hugepages if NVME SSD configuration existed. */
	fd = open(nvme_conf, O_RDONLY, 0600);
	if (fd < 0) {
		rc = bio_nvme_init(NULL, VOS_NVME_NUMA_NODE, 0, 0,
				   VOS_NVME_NR_TARGET, vos_db_get(), true);
	} else {
		rc = bio_nvme_init(nvme_conf, VOS_NVME_NUMA_NODE,
				   VOS_NVME_MEM_SIZE, VOS_NVME_HUGEPAGE_SIZE,
				   VOS_NVME_NR_TARGET, vos_db_get(), true);
		close(fd);
	}

	if (rc)
		goto out;

	self_mode.self_nvme_init = true;
	rc = bio_xsctxt_alloc(&self_mode.self_xs_ctxt, -1 /* Self poll */);
out:
	D_FREE(nvme_conf);
	return rc;
}

static void
vos_self_fini_locked(void)
{
	vos_self_nvme_fini();
	vos_db_fini();

	if (self_mode.self_tls) {
		vos_tls_fini(self_mode.self_tls);
		self_mode.self_tls = NULL;
	}
	ABT_finalize();
}

void
vos_self_fini(void)
{
	/* Clean up things left behind in standalone mode.
	 * NB: this function is only defined for standalone mode.
	 */
	gc_wait();

	D_MUTEX_LOCK(&self_mode.self_lock);

	D_ASSERT(self_mode.self_ref > 0);
	self_mode.self_ref--;
	if (self_mode.self_ref == 0)
		vos_self_fini_locked();

	D_MUTEX_UNLOCK(&self_mode.self_lock);
}

int
vos_self_init(const char *db_path)
{
	char	*evt_mode;
	int	 rc = 0;

	D_MUTEX_LOCK(&self_mode.self_lock);
	if (self_mode.self_ref) {
		self_mode.self_ref++;
		D_GOTO(out, rc);
	}

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_MUTEX_UNLOCK(&self_mode.self_lock);
		return rc;
	}

	vos_start_epoch = 0;

#if VOS_STANDALONE
	self_mode.self_tls = vos_tls_init(0, -1);
	if (!self_mode.self_tls) {
		ABT_finalize();
		D_MUTEX_UNLOCK(&self_mode.self_lock);
		return rc;
	}
#endif
	rc = vos_mod_init();
	if (rc)
		D_GOTO(failed, rc);

	rc = vos_db_init(db_path, "self_db", true);
	if (rc)
		D_GOTO(failed, rc);

	rc = vos_self_nvme_init(db_path);
	if (rc)
		D_GOTO(failed, rc);

	evt_mode = getenv("DAOS_EVTREE_MODE");
	if (evt_mode) {
		if (strcasecmp("soff", evt_mode) == 0) {
			vos_evt_feats &= ~EVT_FEATS_SUPPORTED;
			vos_evt_feats |= EVT_FEAT_SORT_SOFF;
		} else if (strcasecmp("dist_even", evt_mode) == 0) {
			vos_evt_feats &= ~EVT_FEATS_SUPPORTED;
			vos_evt_feats |= EVT_FEAT_SORT_DIST_EVEN;
		}
	}
	switch (vos_evt_feats & EVT_FEATS_SUPPORTED) {
	case EVT_FEAT_SORT_SOFF:
		D_INFO("Using start offset sort for evtree\n");
		break;
	case EVT_FEAT_SORT_DIST_EVEN:
		D_INFO("Using distance sort for evtree with even split\n");
		break;
	default:
		D_INFO("Using distance with closest side split for evtree "
		       "(default)\n");
	}

	self_mode.self_ref = 1;
out:
	D_MUTEX_UNLOCK(&self_mode.self_lock);
	return 0;
failed:
	vos_self_fini_locked();
	D_MUTEX_UNLOCK(&self_mode.self_lock);
	return rc;
}
