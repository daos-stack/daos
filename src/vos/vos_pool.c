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
 * vos/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#include <daos_srv/vos.h>
#include <daos/daos_errno.h>
#include <daos/daos_common.h>
#include <daos/daos_hash.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

static void
daos_vpool_free(struct daos_hlink *hlink)
{
	struct vp_hdl *vpool;

	vpool = container_of(hlink, struct vp_hdl, vp_hlink);

	if (vpool != NULL) {
		if (vpool->vp_ph)
			pmemobj_close(vpool->vp_ph);
		if (vpool->vp_fpath != NULL)
			free(vpool->vp_fpath);
		D_FREE_PTR(vpool);
	}
}

struct daos_hlink_ops	vpool_hh_ops = {
	.hop_free	= daos_vpool_free,
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
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	size_t				root_size;
	TOID(struct vos_pool_root)	proot;

	if (NULL == path || uuid_is_null(uuid) || size < 0)
		return -DER_INVAL;

	/* Path must be a file with a certain size when size
	 * argument is 0
	 */
	if (!size && (access(path, F_OK) == -1)) {
		D_ERROR("File not present when size is 0");
		return -DER_NONEXIST;
	}

	/* creating and initializing a handle hash
	 * to maintain all "DRAM" pool handles
	 * This hash converts the DRAM pool handle to a uint64_t
	 * cookie. This cookies is returned with a generic
	 * daos_handle_t */

	/* Thread safe vos_hhash creation
	 * and link initialization
	 * hash-table created once across all handles in VOS
	 */
	rc = vos_create_hhash();
	if (rc) {
		D_ERROR("Creating hhash failure\n");
		return rc;
	}

	D_ALLOC_PTR(vpool);
	if (vpool == NULL)
		return -DER_NOMEM;

	vpool->vp_fpath = strdup(path);
	vpool->vp_ph = pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout),
				      size, 0666);
	if (!vpool->vp_ph) {
		D_ERROR("Failed to create pool: %d\n", errno);
		rc = -DER_NOSPACE;
		goto exit;
	}

	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);
	root_size = pmemobj_root_size(vpool->vp_ph);

	TX_BEGIN(vpool->vp_ph) {
		TX_ADD(proot);
		memset(root, 0, sizeof(*root));
		root->vpr_ci_table =
			TX_ZNEW(struct vos_container_index);
		uuid_copy(root->vpr_pool_id, uuid);
		root->vpr_pool_info.pif_size = size;
		root->vpr_pool_info.pif_avail = size - root_size;
	} TX_ONABORT {
		D_ERROR("Initialize pool root error: %s\n",
			pmemobj_errormsg());
		/* The transaction can in reality be aborted
		 * only when there is no memory, either due
		 * to loss of power or no more memory in pool
		 */
		rc = -DER_NOMEM;
		goto exit;
	} TX_END

	daos_hhash_hlink_init(&vpool->vp_hlink, &vpool_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &vpool->vp_hlink,
			       DAOS_HTYPE_VOS_POOL);
	daos_hhash_link_key(&vpool->vp_hlink, &poh->cookie);
	daos_hhash_link_putref(daos_vos_hhash, &vpool->vp_hlink);
exit:
	if (rc)
		daos_vpool_free(&vpool->vp_hlink);

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
	struct vp_hdl		*vpool = NULL;
	struct daos_hlink	*hlink;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (hlink == NULL) {
		D_ERROR("VOS pool handle lookup error\n");
		return -DER_INVAL;
	}

	vpool = container_of(hlink, struct vp_hdl, vp_hlink);
	rc = remove(vpool->vp_fpath);
	if (rc) {
		D_ERROR("While deleting file from PMEM\n");
		goto exit;
	}

	daos_hhash_link_delete(daos_vos_hhash, &vpool->vp_hlink);
exit:
	daos_hhash_link_putref(daos_vos_hhash, &vpool->vp_hlink);

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
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	TOID(struct vos_pool_root)	proot;

	if (path == NULL) {
		D_ERROR("Invalid Pool Path\n");
		return -DER_INVAL;
	}

	/*
	 * Pool can be created and opened with different VOS instances
	 * So during open if the handle hash does not exist
	 * it must be created and initialized.
	 */
	rc = vos_create_hhash();
	if (rc) {
		D_ERROR("Creating handle hash failed\n");
		return rc;
	}

	/* Create a new handle during open */
	D_ALLOC_PTR(vpool);
	if (vpool == NULL) {
		D_ERROR("Error allocating vpool handle");
		return -DER_NOMEM;
	}

	vpool->vp_fpath = strdup(path);
	vpool->vp_ph = pmemobj_open(path, POBJ_LAYOUT_NAME(vos_pool_layout));
	if (vpool->vp_ph == NULL) {
		D_ERROR("Error in opening the pool handle");
		rc = -DER_NO_HDL;
		goto exit;
	}

	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);
	if (uuid_compare(uuid, root->vpr_pool_id)) {
		uuid_unparse(uuid, pool_uuid_str);
		uuid_unparse(uuid, uuid_str);
		D_ERROR("UUID mismatch error (uuid: %s, vpool_id: %s",
			uuid_str, pool_uuid_str);
		rc = -DER_INVAL;
		goto exit;
	}

	daos_hhash_hlink_init(&vpool->vp_hlink, &vpool_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &vpool->vp_hlink,
			       DAOS_HTYPE_VOS_POOL);
	daos_hhash_link_key(&vpool->vp_hlink, &poh->cookie);
	daos_hhash_link_putref(daos_vos_hhash, &vpool->vp_hlink);
exit:
	if (rc)
		daos_vpool_free(&vpool->vp_hlink);

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
	struct vp_hdl		*vpool = NULL;
	struct daos_hlink	*hlink;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (hlink == NULL) {
		D_ERROR("VOS pool handle lookup error");
		return -DER_INVAL;
	}

	vpool = container_of(hlink, struct vp_hdl, vp_hlink);

	/* daos_hhash_link_delete eventually calls the call-back
	 * daos_vpool_free which also closes the pmemobj pool
	 */
	daos_hhash_link_delete(daos_vos_hhash, &vpool->vp_hlink);
	daos_hhash_link_putref(daos_vos_hhash, &vpool->vp_hlink);

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
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct daos_hlink		*hlink;
	TOID(struct vos_pool_root)	proot;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (hlink == NULL)
		return -DER_INVAL;

	vpool = container_of(hlink, struct vp_hdl, vp_hlink);
	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);

	memcpy(pinfo, &root->vpr_pool_info,
	       sizeof(root->vpr_pool_info));

	daos_hhash_link_putref(daos_vos_hhash, &vpool->vp_hlink);
	return rc;
}
