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

static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
/**
 * Object cache based on mode of instantiation
 */
struct daos_lru_cache*
vos_get_obj_cache(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_ocache;
#else
	return vos_tls_get()->vtl_imems_inst.vis_ocache;
#endif
}

int
vos_csum_enabled(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_enable_checksum;
#else
	return vos_tls_get()->vtl_imems_inst.vis_enable_checksum;
#endif
}

int
vos_csum_compute(daos_sg_list_t *sgl, daos_csum_buf_t *csum)
{
	int	rc;
#ifdef VOS_STANDALONE
	daos_csum_t *checksum = &vsa_imems_inst->vis_checksum;
#else
	daos_csum_t *checksum =
		&vos_tls_get()->vtl_imems_inst.vis_checksum;
#endif
	rc = daos_csum_compute(checksum, sgl);
	if (rc != 0) {
		D_ERROR("Checksum compute error from VOS: %d\n", rc);
		return rc;
	}

	csum->cs_len = csum->cs_buf_len = daos_csum_get_size(checksum);
	rc = daos_csum_get(checksum, csum);
	if (rc != 0)
		D_ERROR("Error while obtaining checksum :%d\n", rc);
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
	char		*env;
	int		rc;

	imem_inst->vis_enable_checksum = 0;
	rc = vos_obj_cache_create(LRU_CACHE_BITS,
				  &imem_inst->vis_ocache);
	if (rc) {
		D_ERROR("Error in createing object cache\n");
		return rc;
	}

	rc = d_uhash_create(0 /* no locking */, VOS_POOL_HHASH_BITS,
			    &imem_inst->vis_pool_hhash);
	if (rc) {
		D_ERROR("Error in creating POOL ref hash: %d\n", rc);
		goto failed;
	}

	rc = d_uhash_create(0 /* no locking */, VOS_CONT_HHASH_BITS,
			    &imem_inst->vis_cont_hhash);
	if (rc) {
		D_ERROR("Error in creating CONT ref hash: %d\n", rc);
		goto failed;
	}

	env = getenv("VOS_CHECKSUM");
	if (env != NULL) {
		rc = daos_csum_init(env, &imem_inst->vis_checksum);
		if (rc != 0) {
			D_ERROR("Error in initializing checksum\n");
			goto failed;
		}
		D_DEBUG(DB_IO, "Enable VOS checksum=%s\n", env);
		imem_inst->vis_enable_checksum = 1;
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

	return tls;
}

static void
vos_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct vos_tls *tls = data;

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

static int
vos_nvme_init(void)
{
	int rc;

	/* IV tree used by VEA */
	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		return rc;

	rc = bio_nvme_init(VOS_STORAGE_PATH, VOS_NVME_CONF, VOS_NVME_SHM_ID);
	if (rc)
		return rc;
	vsa_nvme_init = true;

	rc = bio_xsctxt_alloc(&vsa_xsctxt_inst, -1 /* Self poll */);
	return rc;
}

void
vos_fini(void)
{
	D_MUTEX_LOCK(&mutex);
	if (vsa_imems_inst) {
		vos_imem_strts_destroy(vsa_imems_inst);
		D_FREE(vsa_imems_inst);
	}
	umem_fini_txd(&vsa_txd_inst);
	vos_nvme_fini();
	ABT_finalize();
	D_MUTEX_UNLOCK(&mutex);
}

int
vos_init(void)
{
	int		rc = 0;
	static int	is_init = 0;

	if (is_init) {
		D_ERROR("Already initialized a VOS instance\n");
		return rc;
	}

	D_MUTEX_LOCK(&mutex);

	if (is_init && vsa_imems_inst)
		D_GOTO(exit, rc);

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}

	rc = umem_init_txd(&vsa_txd_inst);
	if (rc) {
		ABT_finalize();
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}

	vsa_xsctxt_inst = NULL;
	vsa_nvme_init = false;

	D_ALLOC_PTR(vsa_imems_inst);
	if (vsa_imems_inst == NULL)
		D_GOTO(exit, rc);

	rc = vos_imem_strts_create(vsa_imems_inst);
	if (rc)
		D_GOTO(exit, rc);

	rc = vos_mod_init();
	if (rc)
		D_GOTO(exit, rc);

	rc = vos_nvme_init();
	if (rc)
		D_GOTO(exit, rc);

	is_init = 1;
exit:
	D_MUTEX_UNLOCK(&mutex);
	if (rc)
		vos_fini();
	return rc;
}
