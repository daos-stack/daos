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

extern vos_chash_ops_t vos_co_idx_hop;

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size,
		daos_handle_t *poh, daos_event_t *ev)
{
	int		 rc    = 0;
	struct vp_hdl	*vpool = NULL;

	if (NULL == path || uuid_is_null(uuid) || size < 0)
		return -DER_INVAL;

	/* Path must be a file with a certain size when size
	 * argument is 0
	 */
	if (!size && (access(path, F_OK) == -1)) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return -DER_NONEXIST;
	}

	D_ALLOC_PTR(vpool);
	if (vpool == NULL)
		return -DER_NOMEM;

	vos_pool_hhash_init(vpool);
	vpool->vp_fpath = strdup(path);
	vpool->vp_ph = pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout),
				      size, 0666);
	if (!vpool->vp_ph) {
		D_ERROR("Failed to create pool: %d\n", errno);
		D_GOTO(exit, rc = -DER_NOSPACE);
	}
	/**
	 * Setting Btree attributes for btree's used
	 * within this pool (both oi and kv object)
	 */
	vpool->vp_uma.uma_id = UMEM_CLASS_PMEM;
	vpool->vp_uma.uma_u.pmem_pool = vpool->vp_ph;

	rc = umem_class_init(&vpool->vp_uma, &vpool->vp_umm);
	D_ASSERT(rc == 0);

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

	TX_BEGIN(vpool->vp_ph) {
		struct vos_container_index *co_idx;
		struct vos_pool_root	   *root;
		vos_pool_info_t		   *pinfo;

		root = vos_pool2root(vpool);
		pmemobj_tx_add_range_direct(root, sizeof(*root));

		memset(root, 0, sizeof(*root));
		root->vpr_ci_table = TX_ZNEW(struct vos_container_index);
		co_idx = D_RW(root->vpr_ci_table);

		/* Container table is empty create one */
		rc = vos_chash_create(vpool->vp_ph, VCH_MIN_BUCKET_SIZE,
				      VCH_MAX_BUCKET_SIZE, CRC64, true,
				      &co_idx->chtable, &vos_co_idx_hop);
		if (rc != 0) {
			D_ERROR("Failed to create container index table: %d\n",
				rc);
			pmemobj_tx_abort(EFAULT);
		}

		uuid_copy(root->vpr_pool_id, uuid);
		pinfo = &root->vpr_pool_info;

		pinfo->pif_size	 = size;
		pinfo->pif_avail = size - pmemobj_root_size(vpool->vp_ph);

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Initialize pool root error: %d\n", rc);
		/* The transaction can in reality be aborted
		 * only when there is no memory, either due
		 * to loss of power or no more memory in pool
		 */
	} TX_END

	if (rc != 0)
		D_GOTO(exit, rc);

	vos_pool_insert_handle(vpool, poh);
exit:
	vos_pool_putref_handle(vpool);
	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP)
 * and revoke all its handles
 */
int
vos_pool_destroy(daos_handle_t poh, daos_event_t *ev)
{

	int			rc    = 0;
	struct vp_hdl		*vpool = NULL;

	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("VOS pool handle lookup error\n");
		return -DER_INVAL;
	}
	/* NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	rc = remove(vpool->vp_fpath);
	if (rc) {
		D_ERROR("While deleting file from PMEM\n");
		D_GOTO(exit, rc);
	}

	vos_pool_delete_handle(vpool);
exit:
	vos_pool_putref_handle(vpool);
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
	char				pool_uuid_str[37], uuid_str[37];
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct vos_container_index	*co_idx;
	if (path == NULL) {
		D_ERROR("Invalid Pool Path\n");
		return -DER_INVAL;
	}

	/* Create a new handle during open */
	D_ALLOC_PTR(vpool);
	if (vpool == NULL) {
		D_ERROR("Error allocating vpool handle");
		return -DER_NOMEM;
	}

	vos_pool_hhash_init(vpool);
	vpool->vp_fpath = strdup(path);
	vpool->vp_ph = pmemobj_open(path, POBJ_LAYOUT_NAME(vos_pool_layout));
	if (vpool->vp_ph == NULL) {
		D_ERROR("Error in opening the pool handle");
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
		goto exit;
	}

	D_DEBUG(DF_MISC, "vpool open %p\n", vpool);

	root = vos_pool2root(vpool);
	if (uuid_compare(uuid, root->vpr_pool_id)) {
		uuid_unparse(uuid, pool_uuid_str);
		uuid_unparse(uuid, uuid_str);
		D_ERROR("UUID mismatch error (uuid: %s, vpool_id: %s",
			uuid_str, pool_uuid_str);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	co_idx = D_RW(root->vpr_ci_table);
	rc = vos_chash_set_ops(vpool->vp_ph, co_idx->chtable,
			       &vos_co_idx_hop);
	if (rc) {
		D_ERROR("Setting container table hash-value failed: %d",
			rc);
		D_GOTO(exit, rc = -DER_NONEXIST);
	}
	vos_pool_insert_handle(vpool, poh);

exit:
	vos_pool_putref_handle(vpool);
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

	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("VOS pool handle lookup error");
		return -DER_INVAL;
	}

	/**
	 * daos_hhash_link_delete eventually calls the call-back
	 * daos_vpool_free which also closes the pmemobj pool
	 */
	vos_pool_delete_handle(vpool);
	vos_pool_putref_handle(vpool);

	return rc;
}

/**
 * Query attributes and statistics of the current pool
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo, daos_event_t *ev)
{

	int				rc    = 0;
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;

	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL)
		return -DER_INVAL;

	root = vos_pool2root(vpool);

	memcpy(pinfo, &root->vpr_pool_info, sizeof(root->vpr_pool_info));
	vos_pool_putref_handle(vpool);

	return rc;
}
