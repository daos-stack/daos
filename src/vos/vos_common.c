/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
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
#include "vos_internal.h"

static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;

int
vos_init(void)
{
	int				rc = 0;
	static int			is_init = 0;

	if (!is_init) {

		pthread_mutex_lock(&mutex);
		/**
		 * creating and initializing a handle hash
		 * to maintain all "DRAM" pool handles
		 * This hash converts the DRAM pool handle to a uint64_t
		 * cookie. This cookies is returned with a generic
		 * daos_handle_t
		 * Thread safe vos_hhash creation
		 * and link initialization
		 * hash-table created once across all handles in VOS
		 */
		if (!is_init && !daos_vos_hhash) {
			rc = daos_hhash_create(DAOS_HHASH_BITS,
					       &daos_vos_hhash);
			if (rc) {
				D_ERROR("VOS hhash creation error\n");
				goto exit;
			}
			/**
			 * Registering the class for OI btree
			 */
			rc = vos_oi_init();
			if (rc)
				D_ERROR("VOS OI btree initialization error\n");
			else
				is_init = 1;
		}
exit:
		pthread_mutex_unlock(&mutex);

	} else
		D_ERROR("Already initialized a VOS instance\n");

	return rc;
}

void
vos_fini(void)
{
	pthread_mutex_lock(&mutex);

	if (daos_vos_hhash) {
		daos_hhash_destroy(daos_vos_hhash);
		daos_vos_hhash = NULL;
	} else
		D_ERROR("Nothing to destroy!\n");

	pthread_mutex_unlock(&mutex);
}


struct vc_hdl*
vos_co_lookup_handle(daos_handle_t coh)
{
	struct vc_hdl		*co_hdl = NULL;
	struct daos_hlink	*hlink = NULL;

	hlink = daos_hhash_link_lookup(daos_vos_hhash,
				       coh.cookie);
	if (!hlink)
		D_ERROR("vos container handle lookup error\n");
	else
		co_hdl = container_of(hlink, struct vc_hdl,
				      vc_hlink);
	return co_hdl;
}



struct vp_hdl*
vos_pool_lookup_handle(daos_handle_t poh)
{
	struct vp_hdl		*vpool = NULL;
	struct daos_hlink	*hlink = NULL;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (!hlink)
		D_ERROR("VOS pool handle lookup error\n");
	else
		vpool = container_of(hlink, struct vp_hdl,
				     vp_hlink);
	return vpool;
}


inline void
vos_pool_putref_handle(struct vp_hdl *vpool)
{
	if (!vpool) {
		D_ERROR("Empty Pool handle\n");
		return;
	}
	daos_hhash_link_putref(daos_vos_hhash,
			       &vpool->vp_hlink);

}

inline void
vos_co_putref_handle(struct vc_hdl *co_hdl)
{
	if (!co_hdl) {
		D_ERROR("Empty container handle\n");
		return;
	}
	daos_hhash_link_putref(daos_vos_hhash,
			       &co_hdl->vc_hlink);
}



static void *
vos_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct vos_tls *tls;

	D_ALLOC_PTR(tls);

	return tls;
}

static void
vos_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct vos_tls *tls = data;

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
	return 0;
}

static int
vos_mod_fini(void)
{
	return 0;
}

struct dss_module vos_module =  {
	.sm_name	= "vos",
	.sm_mod_id	= DAOS_VOS_MODULE,
	.sm_ver		= 1,
	.sm_init	= vos_mod_init,
	.sm_fini	= vos_mod_fini,
	.sm_key		= &vos_module_key,
};

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
