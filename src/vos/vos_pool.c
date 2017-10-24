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
 * Implementation for pool specific functions in VOS
 *
 * vos/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define DDSUBSYS	DDFAC(vos)

#include <daos_srv/vos.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/hash.h>
#include <sys/stat.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

pthread_mutex_t vos_pmemobj_lock = PTHREAD_MUTEX_INITIALIZER;
/**
 * Memory class is PMEM by default, user can set it to VMEM (volatile memory)
 * for testing.
 */
umem_class_id_t	vos_mem_class	 = UMEM_CLASS_PMEM;

static struct vos_pool *
pool_hlink2ptr(struct daos_ulink *hlink)
{
	D__ASSERT(hlink != NULL);
	return container_of(hlink, struct vos_pool, vp_hlink);
}

static void
pool_hop_free(struct daos_ulink *hlink)
{
	struct vos_pool	*pool = pool_hlink2ptr(hlink);

	D__ASSERT(pool->vp_opened == 0);

	if (!daos_handle_is_inval(pool->vp_cookie_th))
		vos_cookie_tab_destroy(pool->vp_cookie_th);

	if (!daos_handle_is_inval(pool->vp_cont_th))
		dbtree_close(pool->vp_cont_th);

	if (pool->vp_uma.uma_u.pmem_pool)
		vos_pmemobj_close(pool->vp_uma.uma_u.pmem_pool);

	D__FREE_PTR(pool);
}

static struct daos_ulink_ops   pool_uuid_hops = {
	.uop_free       = pool_hop_free,
};

/** allocate DRAM instance of vos pool */
static int
pool_alloc(uuid_t uuid, struct vos_pool **pool_p)
{
	struct vos_pool		*pool;
	struct umem_attr	 uma;
	int			 rc;

	D__ALLOC_PTR(pool);
	if (pool == NULL)
		return -DER_NOMEM;

	daos_uhash_ulink_init(&pool->vp_hlink, &pool_uuid_hops);
	uuid_copy(pool->vp_id, uuid);

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	/* Create a cookie index table in DRAM */
	rc = vos_cookie_tab_create(&uma, &pool->vp_cookie_tab,
				    &pool->vp_cookie_th);
	if (rc != 0) {
		D__ERROR("Cookie tree create failed: %d\n", rc);
		D__GOTO(failed, rc);
	}
	*pool_p = pool;
	return 0;
failed:
	pool_hop_free(&pool->vp_hlink);
	return rc;
}

static int
pool_link(struct vos_pool *pool, struct daos_uuid *ukey, daos_handle_t *poh)
{
	int	rc;

	rc = daos_uhash_link_insert(vos_pool_hhash_get(), ukey,
				    &pool->vp_hlink);
	if (rc) {
		D__ERROR("uuid hash table insert failed: %d\n", rc);
		D__GOTO(failed, rc);
	}
	*poh = vos_pool2hdl(pool);
	return 0;
failed:
	return rc;
}

static void
pool_unlink(struct vos_pool *pool)
{
	daos_uhash_link_delete(vos_pool_hhash_get(), &pool->vp_hlink);
}

static int
pool_lookup(struct daos_uuid *ukey, struct vos_pool **pool)
{
	struct daos_ulink *hlink;

	hlink = daos_uhash_link_lookup(vos_pool_hhash_get(), ukey);
	if (hlink == NULL) {
		D__DEBUG(DB_MGMT, "can't find "DF_UUID"\n", DP_UUID(ukey->uuid));
		return -DER_NONEXIST;
	}

	*pool = pool_hlink2ptr(hlink);
	return 0;
}

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size)
{
	PMEMobjpool	*ph;
	int		 rc = 0;

	if (!path || uuid_is_null(uuid))
		return -DER_INVAL;

	D__DEBUG(DB_MGMT, "Pool Path: %s, size: "DF_U64",UUID: "DF_UUID"\n",
		path, size, DP_UUID(uuid));

	/* Path must be a file with a certain size when size argument is 0 */
	if (!size && access(path, F_OK) == -1) {
		D__ERROR("File not accessible (%d) when size is 0\n", errno);
		return -DER_NONEXIST;
	}

	ph = vos_pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout), size,
				0666);
	if (!ph) {
		D__ERROR("Failed to create pool, size="DF_U64", errno=%d\n",
			size, errno);
		return  -DER_NOSPACE;
	}

	/* If the file is fallocated seperately we need the fallocated size
	 * for setting in the root object.
	 */
	if (!size) {
		struct stat lstat;

		stat(path, &lstat);
		size = lstat.st_size;
	}

	TX_BEGIN(ph) {
		struct vos_pool_df	*pool_df;
		struct umem_attr	 uma;

		pool_df = vos_pool_pop2df(ph);
		pmemobj_tx_add_range_direct(pool_df, sizeof(*pool_df));
		memset(pool_df, 0, sizeof(*pool_df));

		memset(&uma, 0, sizeof(uma));
		uma.uma_id = vos_mem_class;
		uma.uma_u.pmem_pool = ph;

		rc = vos_cont_tab_create(&uma, &pool_df->pd_ctab_df);
		if (rc != 0)
			pmemobj_tx_abort(EFAULT);

		uuid_copy(pool_df->pd_id, uuid);
		pool_df->pd_pool_info.pif_size  = size;
		/* XXX we don't really maintain the available size */
		pool_df->pd_pool_info.pif_avail = size - pmemobj_root_size(ph);

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D__ERROR("Initialize pool root error: %d\n", rc);
		/**
		 * The transaction can in reality be aborted
		 * only when there is no memory, either due
		 * to loss of power or no more memory in pool
		 */
	} TX_END

	if (rc != 0)
		D__GOTO(exit, rc);
exit:
	/* Close this local handle, opened using pool_open */
	vos_pmemobj_close(ph);
	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP) and revoke all its handles
 */
int
vos_pool_destroy(const char *path, uuid_t uuid)
{

	struct vos_pool		*pool;
	struct daos_uuid	 ukey;
	int			 rc;

	uuid_copy(ukey.uuid, uuid);
	D__DEBUG(DB_MGMT, "Destroy path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D__ERROR("Open reference exists, cannot destroy pool\n");
		vos_pool_decref(pool);
		D__GOTO(exit, rc = -DER_BUSY);
	}

	D__DEBUG(DB_MGMT, "No open handles. OK to destroy\n");
	/**
	 * NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	rc = remove(path);
	if (rc)
		D__ERROR("While deleting file from PMEM\n");
exit:
	return rc;
}

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh)
{

	struct vos_pool_df	*pool_df;
	struct vos_pool		*pool;
	struct umem_attr	*uma;
	struct daos_uuid	 ukey;
	int			 rc;

	if (path == NULL || poh == NULL) {
		D__ERROR("Invalid parameters.\n");
		return -DER_INVAL;
	}

	uuid_copy(ukey.uuid, uuid);
	D__DEBUG(DB_MGMT, "open pool %s, uuid "DF_UUID"\n", path, DP_UUID(uuid));

	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D__DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
			pool->vp_opened, pool);
		pool->vp_opened++;
		*poh = vos_pool2hdl(pool);
		return 0;
	}

	/* Create a new handle during open */
	rc = pool_alloc(uuid, &pool); /* returned with refcount=1 */
	if (rc != 0) {
		D__ERROR("Error allocating pool handle\n");
		return rc;
	}

	uma = &pool->vp_uma;
	uma->uma_id = vos_mem_class;
	uma->uma_u.pmem_pool = vos_pmemobj_open(path,
				   POBJ_LAYOUT_NAME(vos_pool_layout));
	if (uma->uma_u.pmem_pool == NULL) {
		D__ERROR("Error in opening the pool: %s\n", pmemobj_errormsg());
		D__GOTO(failed, rc = -DER_NO_HDL);
	}

	/* initialize a umem instance for later btree operations */
	rc = umem_class_init(uma, &pool->vp_umm);
	if (rc != 0) {
		D__ERROR("Failed to instantiate umem: %d\n", rc);
		D__GOTO(failed, rc);
	}

	pool_df = vos_pool_ptr2df(pool);
	if (uuid_compare(uuid, pool_df->pd_id)) {
		D__ERROR("Mismatch uuid, user="DF_UUID", pool="DF_UUID"\n",
			DP_UUID(uuid), DP_UUID(pool_df->pd_id));
		D__GOTO(failed, rc = -DER_IO);
	}

	/* Cache container table btree hdl */
	rc = dbtree_open_inplace(&pool_df->pd_ctab_df.ctb_btree,
				 &pool->vp_uma, &pool->vp_cont_th);
	if (rc) {
		D__ERROR("Container Tree open failed\n");
		D__GOTO(failed, rc);
	}

	/* Insert the opened pool to the uuid hash table */
	rc = pool_link(pool, &ukey, poh);
	if (rc) {
		D__ERROR("Error inserting into vos DRAM hash\n");
		D__GOTO(failed, rc);
	}

	pool->vp_opened = 1;
	D__DEBUG(DB_MGMT, "Opened pool %p\n", pool);
failed:
	vos_pool_decref(pool); /* -1 for myself */
	return rc;
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
		D__ERROR("Cannot close a NULL handle\n");
		return -DER_NO_HDL;
	}
	D__DEBUG(DB_MGMT, "Close opened(%d) pool "DF_UUID" (%p).\n",
		pool->vp_opened, DP_UUID(pool->vp_id), pool);

	D__ASSERT(pool->vp_opened > 0);
	pool->vp_opened--;
	if (pool->vp_opened == 0)
		pool_unlink(pool);

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

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NONEXIST;

	pool_df = vos_pool_ptr2df(pool);
	memcpy(pinfo, &pool_df->pd_pool_info, sizeof(pool_df->pd_pool_info));
	return 0;
}
