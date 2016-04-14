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
 * Implementation for pool specific functions in VOS
 *
 * vos/src/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#include <daos_srv/vos.h>
#include <errno.h>
#include <daos/daos_errno.h>
#include <daos/daos_common.h>
#include <daos/daos_hash.h>
#include <vos_layout.h>
#include <vos_internal.h>


static void
daos_vpool_hhash_free(struct daos_hlink *hlink)
{
	struct vos_pool *vpool;
	vpool = container_of(hlink, struct vos_pool, vpool_hlink);

	if (NULL != vpool) {
		if (NULL != vpool->path)
			free(vpool->path);
		free(vpool);
	}
}

struct daos_hlink_ops	vpool_hh_ops = {
	.hop_free	= daos_vpool_hhash_free,
};


/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 *
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size,
		daos_handle_t *poh, daos_event_t *ev)
{
	int				rc    = 0;
	struct vos_pool			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	size_t				root_size;
	TOID(struct vos_pool_root)	proot;

	if (NULL == path || uuid_is_null(uuid) || size < 0)
		return -DER_INVAL;

	D_ALLOC_PTR(vpool);
	if (NULL == vpool)
		return -DER_NOMEM;

	vpool->path = strdup(path);
	vpool->ph = pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout),
							  size, 0666);
	if (NULL == vpool->ph) {
		D_ERROR("Failed to create pool: %d\n", errno);
		rc = -DER_NOSPACE;
		goto exit;
	}

	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	root_size = pmemobj_root_size(vpool->ph);

	TX_BEGIN(vpool->ph) {
		root->vpr_magic = 0;
		uuid_copy(root->vpr_pool_id, uuid);
		/* TODO: Yet to identify compatibility and
		   incompatibility flags */
		root->vpr_compat_flags = 0;
		root->vpr_incompat_flags = 0;
		/* This will eventually call the
		   constructor of container table */
		root->vpr_ci_table =
			TX_NEW(struct vos_container_table);
		D_RW(root->vpr_ci_table)->chtable =
			TOID_NULL(struct vos_chash_table);
		root->vpr_pool_info.pif_ncos = 0;
		root->vpr_pool_info.pif_nobjs = 0;
		root->vpr_pool_info.pif_size = size;
		root->vpr_pool_info.pif_avail = size - root_size;
	} TX_END

	/* creating and initializing a handle hash
	 * to maintain all "DRAM" pool handles
	 * This hash converts the DRAM pool handle to a uint64_t
	 * cookie. This cookies is returned with a generic
	 * daos_handle_t */

	/* Thread safe vos_hhash creation
	 * and link initialization
	 * hash-table created once across all handles in VOS*/
	rc = vos_create_hhash();
	if (rc) {
		D_ERROR("Creating hhash failure\n");
		goto exit;
	}
	daos_hhash_hlink_init(&vpool->vpool_hlink, &vpool_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &vpool->vpool_hlink,
			       DAOS_HTYPE_VOS_POOL);
	daos_hhash_link_key(&vpool->vpool_hlink, &poh->cookie);
exit:
	if (rc && NULL != vpool) {
		if (NULL != vpool->path)
			free(vpool->path);
		D_FREE_PTR(vpool);
	}
	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP)
 * and revoke all its handles
 *
 */
int
vos_pool_destroy(daos_handle_t poh, daos_event_t *ev)
{

	int			 rc    = 0;
	struct vos_pool		*vpool = NULL;
	struct daos_hlink	*hlink;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (NULL == hlink) {
		D_ERROR("VOS pool handle lookup error\n");
		return -DER_INVAL;
	}

	vpool = container_of(hlink, struct vos_pool, vpool_hlink);
	rc = remove(vpool->path);
	if (rc) {
		D_ERROR("While deleting file from PMEM\n");
		return rc;
	}
	pmemobj_close(vpool->ph);
	daos_hhash_link_delete(daos_vos_hhash, &vpool->vpool_hlink);
	return rc;
}

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 *
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh,
	      daos_event_t *ev)
{

	int				rc    = 0;
	char				pool_uuid_str[37], uuid_str[37];
	struct vos_pool			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	TOID(struct vos_pool_root)	proot;

	if (NULL == path) {
		D_ERROR("Invalid Pool Path\n");
		return -DER_INVAL;
	}

	/* Create a new handle during open */
	D_ALLOC_PTR(vpool);
	if (NULL == vpool) {
		D_ERROR("Error allocating vpool handle");
		return -DER_NOMEM;
	}

	vpool->path = strdup(path);
	vpool->ph = pmemobj_open(path, POBJ_LAYOUT_NAME(vos_pool_layout));
	if (NULL == vpool->ph) {
		D_ERROR("Error in opening the pool handle");
		if (NULL != vpool)
			free(vpool);
		return	-DER_NO_HDL;
	}

	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	if (uuid_compare(uuid, root->vpr_pool_id)) {
		uuid_unparse(uuid, pool_uuid_str);
		uuid_unparse(uuid, uuid_str);
		D_ERROR("UUID mismatch error (uuid: %s, vpool_id: %s",
			uuid_str, pool_uuid_str);
		if (NULL != vpool)
			free(vpool);
		return -DER_INVAL;
	}
	/*
	 * Pool can be created and opened with different VOS instances
	 * So during open if the handle hash does not exist
	 * it must be created and initialized
	 *
	 * */
	rc =  vos_create_hhash();
	if (rc) {
		D_ERROR("Creating handle hash failed\n");
		goto exit;
	}
	daos_hhash_hlink_init(&vpool->vpool_hlink, &vpool_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &vpool->vpool_hlink,
			       DAOS_HTYPE_VOS_POOL);
	daos_hhash_link_key(&vpool->vpool_hlink, &poh->cookie);

exit:
	if (rc && NULL != vpool) {
		if (NULL != vpool->path)
			free(vpool->path);
		D_FREE_PTR(vpool);
	}

	return rc;
}

/**
 *
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 *
 */
int
vos_pool_close(daos_handle_t poh, daos_event_t *ev)
{

	int			 rc    = 0;
	struct vos_pool		*vpool = NULL;
	struct daos_hlink	*hlink;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (NULL == hlink) {
		D_ERROR("VOS pool handle lookup error");
		return -DER_INVAL;
	}

	vpool = container_of(hlink, struct vos_pool, vpool_hlink);
	pmemobj_close(vpool->ph);
	daos_hhash_link_delete(daos_vos_hhash, &vpool->vpool_hlink);
	return rc;
}

/**
 * Query attributes and statistics of the current pool
 *
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo, daos_event_t *ev)
{

	int				rc    = 0;
	struct vos_pool			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct daos_hlink		*hlink;
	TOID(struct vos_pool_root)	proot;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (NULL == hlink)
		return -DER_INVAL;

	vpool = container_of(hlink, struct vos_pool, vpool_hlink);
	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	pinfo->pif_ncos = root->vpr_pool_info.pif_ncos;
	pinfo->pif_nobjs = root->vpr_pool_info.pif_nobjs;
	pinfo->pif_size = root->vpr_pool_info.pif_size;
	pinfo->pif_avail = root->vpr_pool_info.pif_avail;

	return rc;
}
