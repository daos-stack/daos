/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * Common internal functions for VOS
 * vos/vos_common.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/rpc.h>
#include <daos_srv/daos_server.h>
#include <vos_internal.h>
#include <daos/lru.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>

struct bio_xs_context	*vsa_xsctxt_inst;
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;

static bool vsa_nvme_init;
struct vos_tls	*standalone_tls;

struct vos_tls *
vos_tls_get(void)
{
#ifdef VOS_STANDALONE
	return standalone_tls;
#else
	struct vos_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct vos_tls *)dss_module_key_get(dtc, &vos_module_key);
	return tls;
#endif /* VOS_STANDALONE */
}

#ifdef VOS_STANDALONE
int
vos_profile_start(char *path, int avg)
{
	struct daos_profile *dp;
	int rc;

	if (standalone_tls == NULL)
		return 0;

	rc = daos_profile_init(&dp, path, avg, 0, 0);
	if (rc)
		return rc;

	standalone_tls->vtl_dp = dp;
	return 0;
}

void
vos_profile_stop()
{
	if (standalone_tls == NULL || standalone_tls->vtl_dp == NULL)
		return;

	daos_profile_dump(standalone_tls->vtl_dp);
	daos_profile_destroy(standalone_tls->vtl_dp);
	standalone_tls->vtl_dp = NULL;
}

#endif

/**
 * Object cache based on mode of instantiation
 */
struct daos_lru_cache*
vos_get_obj_cache(void)
{
	return vos_tls_get()->vtl_imems_inst.vis_ocache;
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
	if (!publish)
		vos_publish_blocks(cont, &dth->dth_deferred_nvme,
				   false, VOS_IOS_GENERIC);

	return 0;
}

int
vos_tx_begin(struct dtx_handle *dth, struct umem_instance *umm)
{
	int	rc;

	if (dth == NULL)
		return umem_tx_begin(umm, vos_txd_get());

	if (dth->dth_local_tx_started)
		return 0;

	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc == 0)
		dth->dth_local_tx_started = 1;

	return rc;
}

int
vos_tx_end(struct vos_container *cont, struct dtx_handle *dth_in,
	   struct vos_rsrvd_scm **rsrvd_scmp, d_list_t *nvme_exts, bool started,
	   int err)
{
	struct dtx_handle	*dth = dth_in;
	struct dtx_rsrvd_uint	*dru;
	struct dtx_handle	 tmp = {0};
	int			 rc = err;

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
	if (err == 0 && dth->dth_modification_cnt > dth->dth_op_seq)
		return 0;

	dth->dth_local_tx_started = 0;

	if (dtx_is_valid_handle(dth_in) && err == 0)
		err = vos_dtx_prepared(dth);

	if (err == 0)
		rc = vos_tx_publish(dth, true);

	rc = umem_tx_end(vos_cont2umm(cont), rc);

cancel:
	if (rc != 0) {
		/* The transaction aborted or failed to commit. */
		vos_tx_publish(dth, false);
		if (dtx_is_valid_handle(dth_in))
			vos_dtx_cleanup_internal(dth);
	}

	if (err != 0)
		return err;

	return rc;
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

static inline void
vos_imem_strts_destroy(struct vos_imem_strts *imem_inst)
{
	if (imem_inst->vis_ocache)
		vos_obj_cache_destroy(imem_inst->vis_ocache);

	if (imem_inst->vis_pool_hhash)
		d_uhash_destroy(imem_inst->vis_pool_hhash);

	if (imem_inst->vis_cont_hhash)
		d_uhash_destroy(imem_inst->vis_cont_hhash);
}

static inline int
vos_imem_strts_create(struct vos_imem_strts *imem_inst)
{
	int		rc;

	rc = vos_obj_cache_create(LRU_CACHE_BITS,
				  &imem_inst->vis_ocache);
	if (rc) {
		D_ERROR("Error in creating object cache\n");
		return rc;
	}

	rc = d_uhash_create(D_HASH_FT_NOLOCK, VOS_POOL_HHASH_BITS,
			    &imem_inst->vis_pool_hhash);
	if (rc) {
		D_ERROR("Error in creating POOL ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	rc = d_uhash_create(D_HASH_FT_NOLOCK | D_HASH_FT_EPHEMERAL,
			    VOS_CONT_HHASH_BITS, &imem_inst->vis_cont_hhash);
	if (rc) {
		D_ERROR("Error in creating CONT ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	return 0;

failed:
	vos_imem_strts_destroy(imem_inst);
	return rc;
}

static void *
vos_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct vos_tls *tls;
	int rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&tls->vtl_gc_pools);
	if (vos_imem_strts_create(&tls->vtl_imems_inst)) {
		D_FREE(tls);
		return NULL;
	}

	rc = umem_init_txd(&tls->vtl_txd);
	if (rc) {
		vos_imem_strts_destroy(&tls->vtl_imems_inst);
		D_FREE(tls);
		return NULL;
	}

	tls->vtl_dth = NULL;

	rc = vos_ts_table_alloc(&tls->vtl_ts_table);
	if (rc) {
		umem_fini_txd(&tls->vtl_txd);
		vos_imem_strts_destroy(&tls->vtl_imems_inst);
		D_FREE(tls);
		return NULL;
	}

	return tls;
}

static void
vos_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct vos_tls *tls = data;

	vos_imem_strts_destroy(&tls->vtl_imems_inst);
	umem_fini_txd(&tls->vtl_txd);
	vos_ts_table_free(&tls->vtl_ts_table);

	D_FREE(tls);
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


	return rc;
}

static int
vos_mod_fini(void)
{
	return 0;
}

struct dss_module vos_srv_module =  {
	.sm_name	= "vos_srv",
	.sm_mod_id	= DAOS_VOS_MODULE,
	.sm_ver		= DAOS_VOS_VERSION,
	.sm_init	= vos_mod_init,
	.sm_fini	= vos_mod_fini,
	.sm_key		= &vos_module_key,
};

static void
vos_nvme_fini(void)
{
	if (vsa_xsctxt_inst != NULL) {
		bio_xsctxt_free(vsa_xsctxt_inst);
		vsa_xsctxt_inst = NULL;
	}
	if (vsa_nvme_init) {
		bio_nvme_fini();
		vsa_nvme_init = false;
	}
}

/* Storage path, NVMe config & shm_id used by standalone VOS */
#define VOS_STORAGE_PATH	"/mnt/daos"
#define VOS_NVME_CONF		"/etc/daos_nvme.conf"
#define VOS_NVME_SHM_ID		DAOS_NVME_SHMID_NONE
#define VOS_NVME_MEM_SIZE	DAOS_NVME_MEM_PRIMARY

static int
vos_nvme_init(void)
{
	int rc;

	/* IV tree used by VEA */
	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		return rc;

	rc = bio_nvme_init(VOS_STORAGE_PATH, VOS_NVME_CONF, VOS_NVME_SHM_ID,
		VOS_NVME_MEM_SIZE);
	if (rc)
		return rc;
	vsa_nvme_init = true;

	rc = bio_xsctxt_alloc(&vsa_xsctxt_inst, -1 /* Self poll */);
	return rc;
}

static int	vos_inited;

static void
vos_fini_locked(void)
{
	if (standalone_tls) {
		vos_tls_fini(NULL, NULL, standalone_tls);
		standalone_tls = NULL;
	}
	vos_nvme_fini();
	ABT_finalize();
}

void
vos_fini(void)
{
	/* Clean up things left behind in standalone mode.
	 * NB: this function is only defined for standalone mode.
	 */
	gc_wait();

	D_MUTEX_LOCK(&mutex);

	D_ASSERT(vos_inited > 0);
	vos_inited--;
	if (vos_inited == 0)
		vos_fini_locked();

	D_MUTEX_UNLOCK(&mutex);
}

int
vos_init(void)
{
	char		*evt_mode;
	int		 rc = 0;

	D_MUTEX_LOCK(&mutex);
	if (vos_inited) {
		vos_inited++;
		D_GOTO(out, rc);
	}

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}

	vos_start_epoch = 0;

#if VOS_STANDALONE
	standalone_tls = vos_tls_init(NULL, NULL);
	if (!standalone_tls) {
		ABT_finalize();
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}
#endif
	rc = vos_mod_init();
	if (rc)
		D_GOTO(failed, rc);

	rc = vos_nvme_init();
	if (rc)
		D_GOTO(failed, rc);

	evt_mode = getenv("DAOS_EVTREE_MODE");
	if (evt_mode) {
		if (strcasecmp("soff", evt_mode) == 0)
			vos_evt_feats = EVT_FEAT_SORT_SOFF;
		else if (strcasecmp("dist_even", evt_mode) == 0)
			vos_evt_feats = EVT_FEAT_SORT_DIST_EVEN;
	}
	switch (vos_evt_feats) {
	case EVT_FEAT_SORT_SOFF:
		D_INFO("Using start offset sort for evtree\n");
		break;
	case EVT_FEAT_SORT_DIST_EVEN:
		D_INFO("Using distance sort sort for evtree with even split\n");
		break;
	default:
		D_INFO("Using distance with closest side split for evtree "
		       "(default)\n");
	}
	vos_inited = 1;
out:
	D_MUTEX_UNLOCK(&mutex);
	return 0;
failed:
	vos_fini_locked();
	D_MUTEX_UNLOCK(&mutex);
	return rc;
}
