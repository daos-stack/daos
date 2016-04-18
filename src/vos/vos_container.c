/*
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
 * Implementation for container specific functions in VOS
 *
 * vos/src/vos_container.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <daos_srv/vos.h>
#include <daos/daos_errno.h>
#include <daos/daos_common.h>
#include <daos/daos_hash.h>
#include <vos_layout.h>
#include <vos_internal.h>

/**
 * Callback free methods for VOS Container and
 * VOS Pool
 */
static void
daos_co_hhash_free(struct daos_hlink *hlink)
{
	struct vc_hdl *co_hdl;

	co_hdl = container_of(hlink, struct vc_hdl,
			      vc_hlink);
	if (NULL != co_hdl)
		D_FREE_PTR(co_hdl);
}

struct daos_hlink_ops	co_hdl_hh_ops = {
	.hop_free	= daos_co_hhash_free,
};

/**
 * VOS_CHASH_TABLE Callback routines
 */
int
co_compare_key(const void *a, const void *b)
{
	int  rc = 0;

	rc = uuid_compare(*(uuid_t *)a, *(uuid_t *)b);
	if (!rc)
		return 0;
	else
		return -1;
}

void
co_print_key(const void *a)
{
	char uuid_str[37];

	uuid_unparse(*(uuid_t *)a, uuid_str);
	D_DEBUG(DF_VOS3, "Key: %s\n", uuid_str);
}
void
co_print_value(void *a)
{
	PMEMoid obj;

	obj = *(PMEMoid *)a;
	if (NULL != a)
		D_DEBUG(DF_VOS3, "Obj-table address: %p\n",
			pmemobj_direct(obj));
}

/**
 * Create a container within a VOSP
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				ret    = 0;
	uuid_t				tmp_uuid;
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct vos_container_index	*ci_table = NULL;
	TOID(struct vos_container)	cvalue;
	TOID(struct vos_pool_root)	proot;
	PMEMoid				*object_address;

	vpool = vos_pool_lookup_handle(poh);
	if (NULL == vpool) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		return -DER_INVAL;
	}

	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);
	ci_table = (struct vos_container_index *)
		   D_RW(root->vpr_ci_table);
	uuid_copy(tmp_uuid, co_uuid);

	/* Create a new container entry */
	/* Create a PMEM hashtable if one does not exist */
	/* If one exists then container exists */

	if (TOID_IS_NULL(ci_table->chtable)) {
		/* Container table is empty create one */
		ret = vos_chash_create(vpool->vp_ph, VCH_MIN_BUCKET_SIZE,
				       VCH_MAX_BUCKET_SIZE, CRC64, true,
				       &ci_table->chtable,
				       co_compare_key,
				       co_print_key, co_print_value);
		if (ret) {
			D_ERROR("creating container table :%d\n", ret);
			goto exit;
		}
	} else {
		ret = vos_chash_lookup(vpool->vp_ph, ci_table->chtable,
				       (void *)&tmp_uuid,
				       sizeof(uuid_t),
				       (void *)&object_address);
		if (!ret)
			goto exit;
	}

	/* PMEM Transaction to allocate and add new container entry to
	 * PMEM-hash table
	 * PMEM Allocations would be released on transaction failure
	 * inserting into PMEM-hashtable is nested which is eventually
	 * flattened */

	TX_BEGIN(vpool->vp_ph) {
		cvalue = TX_NEW(struct vos_container);
		uuid_copy(D_RW(cvalue)->vc_id, tmp_uuid);
		D_RW(cvalue)->vc_obtable = TX_NEW(struct vos_object_index);
		D_RW(cvalue)->vc_ehtable = TX_NEW(struct vos_epoch_index);
		D_RW(cvalue)->vc_info.pci_nobjs = 0;
		D_RW(cvalue)->vc_info.pci_used = 0;

		ret =
		vos_chash_insert(vpool->vp_ph, ci_table->chtable,
				 (void *)&tmp_uuid, sizeof(uuid_t),
				 &cvalue, sizeof(struct vos_container));
		if (ret) {
			D_ERROR("Container table insert failed with error : %d",
				ret);
			pmemobj_tx_abort(0);
		}
	} TX_ONABORT {
		D_ERROR("Creating a container entry: %s\n",
			pmemobj_errormsg());
		ret = -DER_NOMEM;
	} TX_END;

exit:
	vos_pool_putref_handle(vpool);
	return ret;
}

/**
 * Open a container within a VOSP
 */
int
vos_co_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh,
	    daos_event_t *ev)
{

	int				ret    = 0;
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct vos_container_index	*ci_table = NULL;
	TOID(struct vos_pool_root)	proot;
	TOID(struct vos_container)	*object_address;
	struct vc_hdl			*co_hdl = NULL;
	uuid_t				tmp_uuid;

	/* Lookup container handle of hash link */
	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		ret = -DER_INVAL;
		goto exit;
	}

	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);
	ci_table = (struct vos_container_index *)
		   D_RW(root->vpr_ci_table);

	if (TOID_IS_NULL(ci_table->chtable)) {
		D_ERROR("Empty Container table\n");
		ret = -DER_NONEXIST;
		goto exit;
	}

	uuid_copy(tmp_uuid, co_uuid);
	ret = vos_chash_lookup(vpool->vp_ph, ci_table->chtable,
			       (void *)&tmp_uuid, sizeof(uuid_t),
			       (void **)&object_address);
	if (ret) {
		D_ERROR("Container does not exist\n");
		goto exit;
	}

	D_ALLOC_PTR(co_hdl);
	if (NULL == co_hdl) {
		D_ERROR("Error in allocating container handle\n");
		ret = -DER_NOSPACE;
		goto exit;
	}

	co_hdl->vc_ph = vpool->vp_ph;
	uuid_copy(co_hdl->vc_id, tmp_uuid);
	co_hdl->vc_obj_table = (struct vos_object_index *)
				D_RW(D_RW(*object_address)->vc_obtable);
	co_hdl->vc_epoch_table = (struct vos_epoch_index *)
				  D_RW(D_RW(*object_address)->vc_ehtable);

	daos_hhash_hlink_init(&co_hdl->vc_hlink, &co_hdl_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &co_hdl->vc_hlink,
			       DAOS_HTYPE_VOS_CO);
	daos_hhash_link_key(&co_hdl->vc_hlink, &coh->cookie);
	daos_hhash_link_putref(daos_vos_hhash, &co_hdl->vc_hlink);

exit:
	vos_pool_putref_handle(vpool);
	if (ret && co_hdl)
		daos_co_hhash_free(&co_hdl->vc_hlink);
	return ret;
}

/**
 * Release container open handle
 */
int
vos_co_close(daos_handle_t coh, daos_event_t *ev)
{

	struct daos_hlink	*hlink;

	/* Lookup container handle of hash link */
	hlink  = daos_hhash_link_lookup(daos_vos_hhash, coh.cookie);
	if (NULL == hlink) {
		D_ERROR("Invalid handle for container");
		return -DER_INVAL;
	}
	daos_hhash_link_delete(daos_vos_hhash, hlink);
	daos_hhash_link_putref(daos_vos_hhash, hlink);

	return 0;
}

/**
 * Destroy a container
 *
 */
int
vos_co_destroy(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				ret    = 0;
	struct vp_hdl			*vpool = NULL;
	struct vos_pool_root		*root  = NULL;
	struct vos_container_index	*ci_table = NULL;
	TOID(struct vos_pool_root)	proot;

	vpool = vos_pool_lookup_handle(poh);
	if (!vpool) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		ret = -DER_INVAL;
		goto exit;
	}

	proot = POBJ_ROOT(vpool->vp_ph, struct vos_pool_root);
	root = D_RW(proot);
	ci_table = (struct vos_container_index *)
			D_RW(root->vpr_ci_table);
	/**
	 * vos_chash_remove hash its own transactions
	 * since chash_table stores key and value in PMEM
	 * Its just enough to remove the entry
	 *
	 */
	ret = vos_chash_remove(vpool->vp_ph, ci_table->chtable,
			       co_uuid, sizeof(uuid_t));
	if (ret)
		D_ERROR("Failed to remove container\n");
exit:
	vos_pool_putref_handle(vpool);
	return ret;
}

/**
 * Query container information.
 *
 */
int
vos_co_query(daos_handle_t coh, vos_co_info_t *vc_info, daos_event_t *ev)
{

	int				ret    = 0;
	struct vos_pool_root		*root  = NULL;
	struct vc_hdl			*co_hdl = NULL;
	struct vos_container_index	*ci_table = NULL;
	TOID(struct vos_container)	*container_value;
	TOID(struct vos_pool_root)	proot;
	struct daos_hlink		*hlink = NULL;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, coh.cookie);
	if (NULL == hlink) {
		D_ERROR("Invalid handle for container\n");
		ret = -DER_INVAL;
		goto exit;
	}

	co_hdl = container_of(hlink, struct vc_hdl, vc_hlink);
	proot = POBJ_ROOT(co_hdl->vc_ph, struct vos_pool_root);
	root = D_RW(proot);

	ci_table = (struct vos_container_index *)
		   D_RW(root->vpr_ci_table);

	ret = vos_chash_lookup(co_hdl->vc_ph, ci_table->chtable,
			       co_hdl->vc_id, sizeof(uuid_t),
			       (void **)&container_value);
	if (ret) {
		D_ERROR("Container does not exist\n");
		goto exit;
	}

	vc_info->pci_nobjs = D_RW(*container_value)->vc_info.pci_nobjs;
	vc_info->pci_used  = D_RW(*container_value)->vc_info.pci_used;
exit:
	daos_hhash_link_putref(daos_vos_hhash, hlink);
	return ret;
}
