/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include <math.h>

static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;

static bool vsa_nvme_init;
static struct vos_tls	*standalone_tls;

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
		D_ERROR("Error in createing object cache\n");
		return rc;
	}

	rc = d_uhash_create(0 /* no locking */, VOS_POOL_HHASH_BITS,
			    &imem_inst->vis_pool_hhash);
	if (rc) {
		D_ERROR("Error in creating POOL ref hash: "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

	rc = d_uhash_create(D_HASH_FT_EPHEMERAL, VOS_CONT_HHASH_BITS,
			    &imem_inst->vis_cont_hhash);
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
	return tls;
}

static void
vos_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct vos_tls *tls = data;

	D_ASSERT(d_list_empty(&tls->vtl_gc_pools));
	vos_imem_strts_destroy(&tls->vtl_imems_inst);
	umem_fini_txd(&tls->vtl_txd);

	D_FREE(tls);
}

struct dss_module_key vos_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = vos_tls_init,
	.dmk_fini = vos_tls_fini,
};

unsigned int VOS_BLK_SZ;
unsigned int VOS_BLK_SHIFT;
unsigned int VOS_BLOB_HRD_BLKS;

static int
vos_mod_init(void)
{
	int		rc = 0;
	char		*media_limit;
	unsigned	user_limit = 4096;
	char		*mdata_align = NULL;

	media_limit = getenv("DAOS_MEDIA_THRESHOLD");
	if (media_limit != NULL) {
		/** Check if user media selection limit is legal power of 2 */
		user_limit =
		((atoi(media_limit) & ((atoi(media_limit)) - 1)) == 0) ?
			atoi(media_limit) : 4096;
	}

	mdata_align = getenv("METADATA_ALIGN");
        if (mdata_align != NULL)
		VOS_BLOB_HRD_BLKS = 16;
	else
		VOS_BLOB_HRD_BLKS = 1;

	D_PRINT("VOS BLOB header blocks: %u\n",
		VOS_BLOB_HRD_BLKS);

	VOS_BLK_SZ    = user_limit;
	VOS_BLK_SHIFT = log10(VOS_BLK_SZ)/log10(2);
	D_PRINT("Using VOS Media Selection threshold: %u, 2^%u\n",
		VOS_BLK_SZ, VOS_BLK_SHIFT);

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

static int
abt_thread_stacksize(void)
{
	char   *env;

	env = getenv("ABT_THREAD_STACKSIZE");
	if (env == NULL)
		env = getenv("ABT_ENV_THREAD_STACKSIZE");
	if (env != NULL)
		return atoi(env);
	return 0;
}

static int
set_abt_thread_stacksize(int n)
{
	char   *name = "ABT_THREAD_STACKSIZE";
	char   *value;
	int	rc;

	D_ASSERTF(n > 0, "%d\n", n);
	D_ASPRINTF(value, "%d", n);
	if (value == NULL)
		return -DER_NOMEM;
	D_INFO("Setting %s to %s\n", name, value);
	rc = setenv(name, value, 1 /* overwrite */);
	D_FREE(value);
	if (rc != 0)
		return daos_errno2der(errno);
	return 0;
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

	/* Some cases (DAOS-4310) have been found where the default ABT's ULTs
	 * stack-size (16K) is not enough and this can lead to stack overrun
	 * and corruption, so double it just in case
	 */
	if (abt_thread_stacksize() < 32768) {
		rc = set_abt_thread_stacksize(32768);
		if (rc != 0) {
			D_ERROR("failed to set ABT_THREAD_STACKSIZE: %d\n", rc);
			D_MUTEX_UNLOCK(&mutex);
			return rc;
		}
	}

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}


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
