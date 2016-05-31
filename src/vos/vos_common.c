/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <daos/common.h>
#include <smmintrin.h>
#include <daos/rpc.h>
#include <daos_srv/daos_server.h>
#include <vos_internal.h>
#include <vos_hhash.h>

static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Object cache based on mode of instantiation
 */
struct vos_obj_cache*
vos_get_obj_cache(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_ocache;
#else
	return vos_tls_get()->vtl_imems_inst.vis_ocache;
#endif
}

/**
 * VOS in-memory structure creation.
 * Handle-hash:
 * -----------
 * This hash converts the DRAM handle to a uint64_t
 * cookie which is returned with a generic daos_handle_t.
 * Must be Done thread safe once across all handles for
 * stand-alone mode. TLS mode creates one of these structures
 * for each thread local storage.
 *
 * Object-cache:
 * ------------
 * In-memory object cache for object index in PMEM
 * Created once for standalone mode and once for every
 * TLS instance.
 */
static inline int
vos_imem_strts_create(struct vos_imem_strts *imem_inst)
{
	int rc = 0;

	rc = daos_hhash_create(DAOS_HHASH_BITS,
			       &imem_inst->vis_hhash);
	if (rc) {
		D_ERROR("VOS hhash creation error\n");
		return rc;
	}

	rc = vos_obj_cache_create(LRU_CACHE_MAX_SIZE,
				  &imem_inst->vis_ocache);
	if (rc) {
		D_ERROR("Error in createing object cache\n");
		return rc;
	}

	return rc;
}

static inline void
vos_imem_strts_destroy(struct vos_imem_strts *imem_inst)
{
	if (imem_inst->vis_ocache)
		vos_obj_cache_destroy(imem_inst->vis_ocache);

	if (imem_inst->vis_hhash)
		daos_hhash_destroy(imem_inst->vis_hhash);

}

static void *
vos_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct vos_tls *tls;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	if (vos_imem_strts_create(&tls->vtl_imems_inst)) {
		D_FREE_PTR(tls);
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
	D_FREE_PTR(tls);
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
	int rc = 0;

	/**
	 * Registering the class for OI btree
	 * and KV btree
	 */
	rc = vos_oi_init();
	if (rc) {
		D_ERROR("VOS OI btree initialization error\n");
		return rc;
	}

	rc = vos_obj_tree_register();
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
	.sm_ver		= 1,
	.sm_init	= vos_mod_init,
	.sm_fini	= vos_mod_fini,
	.sm_key		= &vos_module_key,
};

int
vos_init(void)
{
	int		rc = 0;
	static int	is_init = 0;

	if (is_init) {
		D_ERROR("Already initialized a VOS instance\n");
		return rc;
	}

	pthread_mutex_lock(&mutex);

	if (is_init && vsa_imems_inst)
		D_GOTO(exit, rc);

	D_ALLOC_PTR(vsa_imems_inst);
	if (vsa_imems_inst == NULL)
		D_GOTO(exit, rc);

	rc = vos_imem_strts_create(vsa_imems_inst);
	if (rc)
		D_GOTO(exit, rc);

	rc = vos_mod_init();
	if (!rc)
		is_init = 1;
exit:
	pthread_mutex_unlock(&mutex);
	if (rc && vsa_imems_inst)
		D_FREE_PTR(vsa_imems_inst);
	return rc;
}

void
vos_fini(void)
{
	pthread_mutex_lock(&mutex);
	if (vsa_imems_inst) {
		vos_imem_strts_destroy(vsa_imems_inst);
		D_FREE_PTR(vsa_imems_inst);
	}
	pthread_mutex_unlock(&mutex);
}

/**
 * Jump Consistent Hash from
 * A Fast, Minimal Memory, Consistent Hash Algorithm
 * http://arxiv.org/abs/1406.2294
 * takes as input a 64-bit unsigned int key and returns a
 * bucket number
*/
int32_t
vos_generate_jch(uint64_t key, uint32_t num_buckets)
{
	int64_t j = 0;
	int64_t b = 1;

	while (j < num_buckets) {
		b  = j;
		key = key * 2862933555777941757ULL + 1;
		j = (b + 1) * ((double)(1LL << 31)/(double)((key >> 33) + 1));
	}

	return b;
}

/*
 * Simple CRC64 hash with intrinsics
 * Should be eventually replaced with hash_function from ISA-L
*/
uint64_t
vos_generate_crc64(void *key, uint64_t size)
{
	uint64_t	*data = NULL, *new_key = NULL;
	uint64_t	 hash = 0xffffffffffffffff;
	int		 i, counter;

	if (size < 64) {
		new_key = (uint64_t *)malloc(64);
		/*Pad the rest of 64-bytes to 0*/
		memset(new_key, 0, 64);
		counter = 1;
	} else {
		new_key = (uint64_t *)malloc(size);
		memset(new_key, 0, size);
		counter = (int)(size/8);
	}
	memcpy(new_key, key, size);
	data = new_key;
	for (i = 0; i < counter ; i++) {
		hash = _mm_crc32_u64(hash, *data);
		data++;
	}

	D_DEBUG(DF_VOS3, "%"PRIu64"%"PRIu64" %"PRIu64"\n",
			*(uint64_t *)new_key, size, hash);
	free(new_key);

	return hash;
}
