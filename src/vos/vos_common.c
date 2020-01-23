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
#include <daos/lru.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_server.h>
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

struct vos_tls *
vos_tls_get(void)
{
#ifdef VOS_STANDALONE
	return self_mode.self_tls;
#else
	return dss_module_key_get(dss_tls_get(), &vos_module_key);
#endif
}

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

static void
vos_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct vos_tls *tls = data;

	D_ASSERT(d_list_empty(&tls->vtl_gc_pools));
	if (tls->vtl_ocache)
		vos_obj_cache_destroy(tls->vtl_ocache);

	if (tls->vtl_pool_hhash)
		d_uhash_destroy(tls->vtl_pool_hhash);

	if (tls->vtl_cont_hhash)
		d_uhash_destroy(tls->vtl_cont_hhash);

	umem_fini_txd(&tls->vtl_txd);
	D_FREE(tls);
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
	rc = vos_obj_cache_create(LRU_CACHE_BITS, &tls->vtl_ocache);
	if (rc) {
		D_ERROR("Error in createing object cache\n");
		goto failed;
	}

	rc = d_uhash_create(0, VOS_POOL_HHASH_BITS, &tls->vtl_pool_hhash);
	if (rc) {
		D_ERROR("Error in creating POOL ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	rc = d_uhash_create(D_HASH_FT_EPHEMERAL, VOS_CONT_HHASH_BITS,
			    &tls->vtl_cont_hhash);
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

	tls->vtl_dth = NULL;
	return tls;
failed:
	vos_tls_fini(dtls, key, tls);
	return NULL;
}

struct dss_module_key vos_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = vos_tls_init,
	.dmk_fini = vos_tls_fini,
};

static int
vos_mod_init(void)
{
	int	 rc = 0;

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

	rc = vos_dtx_cos_register();
	if (rc != 0) {
		D_ERROR("DTX CoS btree initialization error\n");
		return rc;
	}

	/* Registering the class for OI btree and KV btree */
	rc = vos_obj_tab_register();
	if (rc) {
		D_ERROR("VOS OI btree initialization error\n");
		return rc;
	}

	rc = obj_tree_register();
	if (rc)
		D_ERROR("Failed to register vos trees\n");

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

/* Storage path, NVMe config & shm_id used by standalone VOS */
#define VOS_STORAGE_PATH	"/mnt/daos"
#define VOS_NVME_CONF		"/etc/daos_nvme.conf"
#define VOS_NVME_SHM_ID		DAOS_NVME_SHMID_NONE
#define VOS_NVME_MEM_SIZE	DAOS_NVME_MEM_PRIMARY

static int
vos_self_nvme_init()
{
	int rc;

	/* IV tree used by VEA */
	rc = dbtree_class_register(DBTREE_CLASS_IV, BTR_FEAT_UINT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		return rc;

	rc = bio_nvme_init(VOS_NVME_CONF, VOS_NVME_SHM_ID,
			   VOS_NVME_MEM_SIZE, vos_db_get());
	if (rc)
		return rc;

	self_mode.self_nvme_init = true;
	rc = bio_xsctxt_alloc(&self_mode.self_xs_ctxt, -1 /* Self poll */);
	return rc;
}

static void
vos_self_fini_locked(void)
{
	vos_self_nvme_fini();
	vos_db_fini();

	if (self_mode.self_tls) {
		vos_tls_fini(NULL, NULL, self_mode.self_tls);
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

#if VOS_STANDALONE
	self_mode.self_tls = vos_tls_init(NULL, NULL);
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

	rc = vos_self_nvme_init();
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

	self_mode.self_ref = 1;
out:
	D_MUTEX_UNLOCK(&self_mode.self_lock);
	return 0;
failed:
	vos_self_fini_locked();
	D_MUTEX_UNLOCK(&self_mode.self_lock);
	return rc;
}
