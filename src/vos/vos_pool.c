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

#include <daos_srv/vos.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/hash.h>
#include <sys/stat.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <vos_hhash.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

static pthread_mutex_t vos_pmemobj_lock = PTHREAD_MUTEX_INITIALIZER;

static PMEMobjpool *
vos_pmemobj_create(const char *path, const char *layout, size_t poolsize,
		   mode_t mode)
{
	PMEMobjpool *pop;

	pthread_mutex_lock(&vos_pmemobj_lock);
	pop = pmemobj_create(path, layout, poolsize, mode);
	pthread_mutex_unlock(&vos_pmemobj_lock);
	return pop;
}

static PMEMobjpool *
vos_pmemobj_open(const char *path, const char *layout)
{
	PMEMobjpool *pop;

	pthread_mutex_lock(&vos_pmemobj_lock);
	pop = pmemobj_open(path, layout);
	pthread_mutex_unlock(&vos_pmemobj_lock);
	return pop;
}

void
vos_pmemobj_close(PMEMobjpool *pop)
{
	pthread_mutex_lock(&vos_pmemobj_lock);
	pmemobj_close(pop);
	pthread_mutex_unlock(&vos_pmemobj_lock);
}

static inline struct vos_pool_root *
pmem_pool2root(PMEMobjpool *ph)
{
	TOID(struct vos_pool_root)  proot;

	proot = POBJ_ROOT(ph, struct vos_pool_root);
	return D_RW(proot);
}

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size,
		daos_event_t *ev)
{
	int			rc    = 0;
	PMEMobjpool		*ph;
	struct umem_attr	u_attr;

	if (NULL == path || uuid_is_null(uuid) || size < 0)
		return -DER_INVAL;

	D_DEBUG(DF_VOS2, "Pool Path: %s, size: "DF_U64",UUID: "DF_UUID"\n",
		path, size, DP_UUID(uuid));

	/**
	 * Path must be a file with a certain size when size
	 * argument is 0
	 */
	if (!size && (access(path, F_OK) == -1)) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return -DER_NONEXIST;
	}

	ph = vos_pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout), size,
				0666);
	if (!ph) {
		D_ERROR("Failed to create pool: %d\n", errno);
		return  -DER_NOSPACE;
	}

	u_attr.uma_id = UMEM_CLASS_PMEM;
	u_attr.uma_u.pmem_pool = ph;
	/**
	 * If the file is fallocated seperately
	 * we need the fallocated
	 * size for setting in the root object.
	 */
	if (!size) {
		struct stat lstat;

		stat(path, &lstat);
		size = lstat.st_size;
	}

	TX_BEGIN(ph) {

		struct vos_container_index	*co_idx;
		struct vos_pool_root		*root;
		vos_pool_info_t			*pinfo;

		root = pmem_pool2root(ph);
		pmemobj_tx_add_range_direct(root, sizeof(*root));

		memset(root, 0, sizeof(*root));
		root->vpr_ci_table = TX_ZNEW(struct vos_container_index);
		co_idx = D_RW(root->vpr_ci_table);

		/**
		 * Container table is empty create one
		 * Also caches the container index
		 * btr_handle
		 */

		rc = vos_ci_create(&u_attr, co_idx);
		if (rc != 0) {
			D_ERROR("Failed to create container index table: %d\n",
				rc);
			pmemobj_tx_abort(EFAULT);
		}

		uuid_copy(root->vpr_pool_id, uuid);
		pinfo = &root->vpr_pool_info;
		pinfo->pif_size	 = size;
		pinfo->pif_avail = size - pmemobj_root_size(ph);

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Initialize pool root error: %d\n", rc);
		/**
		 * The transaction can in reality be aborted
		 * only when there is no memory, either due
		 * to loss of power or no more memory in pool
		 */
	} TX_END

	if (rc != 0)
		D_GOTO(exit, rc);
exit:
	/* Close this local handle, opened using pool_open*/
	vos_pmemobj_close(ph);
	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP)
 * and revoke all its handles
 */
int
vos_pool_destroy(const char *path, uuid_t uuid, daos_event_t *ev)
{

	int			rc    = 0;
	struct vp_hdl		*vpool;
	struct daos_uuid	ukey;

	uuid_copy(ukey.uuid, uuid);
	D_DEBUG(DF_VOS2, "Destroy path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = vos_pool_lookup_handle(&ukey, &vpool);
	if (rc == 0 && vpool != NULL) {
		D_ERROR("Open reference exists, cannot destroy pool\n");
		vos_pool_putref_handle(vpool);
		D_GOTO(exit, rc = -DER_BUSY);
	}

	D_DEBUG(DF_VOS2, "No open handles. OK to destroy\n");
	/**
	 * NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	rc = remove(path);
	if (rc)
		D_ERROR("While deleting file from PMEM\n");

exit:
	return rc;
}

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh,
	      daos_event_t *ev)
{

	int				rc    = 0;
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct vos_container_index	*co_idx = NULL;
	struct daos_uuid		ukey;

	if (path == NULL) {
		D_ERROR("Invalid Pool Path\n");
		return -DER_INVAL;
	}

	uuid_copy(ukey.uuid, uuid);
	D_DEBUG(DF_VOS2, "pool %p, path: %s,Open/Copy:"DF_UUID"/"DF_UUID"\n",
		vpool, path, DP_UUID(uuid), DP_UUID(ukey.uuid));

	rc = vos_pool_lookup_handle(&ukey, &vpool);
	/* If found increments ref-count */
	if (rc == 0) {
		D_DEBUG(DF_VOS2, "Found open handle: %p\n", vpool);
		*poh = vos_pool2hdl(vpool);
		D_GOTO(exit, rc);
	}

	/* Create a new handle during open */
	D_ALLOC_PTR(vpool);
	if (vpool == NULL) {
		D_ERROR("Error allocating vpool handle\n");
		return -DER_NOMEM;
	}
	D_DEBUG(DF_VOS2, "Allocated vos pool :%p\n", vpool);

	vpool->vp_fpath = strdup(path);
	vpool->vp_ph = vos_pmemobj_open(path,
					POBJ_LAYOUT_NAME(vos_pool_layout));
	if (vpool->vp_ph == NULL) {
		D_ERROR("Error in opening the pool handle: %s\n",
			pmemobj_errormsg());
		D_GOTO(exit, rc = -DER_NO_HDL);
	}

	/**
	 * Setting Btree attributes for btree's used
	 * within this pool (both oi and kv object)
	 */
	vpool->vp_uma.uma_id = UMEM_CLASS_PMEM;
	vpool->vp_uma.uma_u.pmem_pool = vpool->vp_ph;
	rc = umem_class_init(&vpool->vp_uma, &vpool->vp_umm);
	if (rc != 0) {
		D_ERROR("Failed to instantiate umem: %d\n", rc);
		D_GOTO(exit, rc);
	}

	root = vos_pool2root(vpool);
	if (uuid_compare(uuid, root->vpr_pool_id)) {
		D_ERROR("UUID mismatch error");
		D_DEBUG(DF_VOS2, "User UUID:"DF_UUID"\n", DP_UUID(uuid));
		D_DEBUG(DF_VOS2, "Root Pool UUID:"DF_UUID"\n",
			DP_UUID(root->vpr_pool_id));
		D_GOTO(exit, rc = -DER_INVAL);
	}

	uuid_copy(vpool->vp_id, root->vpr_pool_id);
	co_idx = D_RW(root->vpr_ci_table);

	/* Insert and init pool handle */
	rc = vos_pool_insert_handle(vpool, &ukey, poh);
	if (rc) {
		D_ERROR("Error inserting into vos DRAM hash\n");
		D_GOTO(exit, rc);
	}

	/* Cache co-tree btree hdl */
	rc = dbtree_open_inplace(&co_idx->ci_btree, &vpool->vp_uma,
				 &vpool->vp_ct_hdl);

	if (rc) {
		D_ERROR("Container Tree open failed\n");
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

exit:
	if (rc != 0 && vpool != NULL)
		vos_pool_uhash_free(&vpool->vp_uhlink);

	return rc;
}

/**
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 */
int
vos_pool_close(daos_handle_t poh, daos_event_t *ev)
{

	int			 rc    = 0;
	struct vp_hdl		*vpool = NULL;

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Cannot close a NULL handle\n");
		return -DER_INVAL;
	}

	D_DEBUG(DF_VOS2, "Close handle :%p\n", vpool);
	rc = vos_pool_release_handle(vpool);
	if (rc)
		D_ERROR("Error in Deleting pool handle\n");
	return rc;
}

/**
 * Query attributes and statistics of the current pool
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo, daos_event_t *ev)
{

	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;

	vpool = vos_hdl2pool(poh);
	root = vos_pool2root(vpool);
	memcpy(pinfo, &root->vpr_pool_info, sizeof(root->vpr_pool_info));

	return 0;
}
