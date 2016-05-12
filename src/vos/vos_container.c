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
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/hash.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <vos_obj.h>

/* NB: hide the dark secret that uuid_t is an array not a structure */
struct uuid_key {
	uuid_t			uuid;
};

/**
 * Callback free methods for VOS Container and
 * VOS Pool
 */
static void
daos_co_hhash_free(struct daos_hlink *hlink)
{
	struct vc_hdl *co_hdl;

	D_ASSERT(hlink);
	co_hdl = container_of(hlink, struct vc_hdl, vc_hlink);
	D_FREE_PTR(co_hdl);
}

struct daos_hlink_ops	co_hdl_hh_ops = {
	.hop_free	= daos_co_hhash_free,
};

/**
 * VOS_CHASH_TABLE Callback routines
 */
static int
co_compare_key(const void *a, const void *b)
{
	return uuid_compare(((const struct uuid_key *)a)->uuid,
			    ((const struct uuid_key *)b)->uuid);
}

static void
co_print_key(const void *a)
{
	char uuid_str[37];

	uuid_unparse(((const struct uuid_key *)a)->uuid, uuid_str);
	D_DEBUG(DF_VOS3, "Key: %s\n", uuid_str);
}

static void
co_print_value(const void *a)
{
	PMEMoid obj;

	D_ASSERT(a != NULL);
	obj = *(PMEMoid *)a;
	D_DEBUG(DF_VOS3, "Obj-table address: %p\n", pmemobj_direct(obj));
}

vos_chash_ops_t	vos_co_idx_hop = {
	.hop_key_cmp	= co_compare_key,
	.hop_key_print	= co_print_key,
	.hop_val_print	= co_print_value,
};

/**
 * Create a container within a VOSP
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				ret;
	struct vp_hdl			*vpool = NULL;
	PMEMoid				*object_address;
	struct uuid_key			 ukey;
	TOID(struct vos_chash_table)	 coi_table;

	vpool = vos_pool_lookup_handle(poh);
	if (NULL == vpool) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		return -DER_INVAL;
	}

	D_DEBUG(DF_VOS3, "looking up co_id in container index\n");
	coi_table = vos_pool2coi_table(vpool);
	uuid_copy(ukey.uuid, co_uuid);
	ret = vos_chash_lookup(vpool->vp_ph, coi_table,
			       (void *)&ukey, sizeof(ukey),
			       (void **)&object_address);
	if (!ret) {
		D_DEBUG(DF_VOS3, "Container existed\n");
		ret = -DER_EXIST;
		goto exit;
	}

	/**
	 * PMEM Transaction to allocate and add new container entry to
	 * PMEM-hash table
	 * PMEM Allocations would be released on transaction failure
	 * inserting into PMEM-hashtable is nested which is eventually
	 * flattened
	 */

	TX_BEGIN(vpool->vp_ph) {
		TOID(struct vos_container)  vc_oid;
		struct vos_container	   *vc;

		vc_oid = TX_ZNEW(struct vos_container);
		vc = D_RW(vc_oid);

		uuid_copy(vc->vc_id, co_uuid);
		vc->vc_obtable	= TX_NEW(struct vos_object_index);
		vc->vc_ehtable	= TX_NEW(struct vos_epoch_index);

		D_DEBUG(DF_VOS3, "Inserting into container index\n");
		ret = vos_chash_insert(vpool->vp_ph, coi_table,
				       (void *)&ukey, sizeof(ukey),
				       &vc_oid, sizeof(struct vos_container));
		if (ret) {
			D_ERROR("Container table insert failed with error : %d",
				ret);
			pmemobj_tx_abort(ENOMEM);
		}

		ret = vos_oi_create(vpool, D_RW(vc->vc_obtable));
		if (ret) {
			D_ERROR("VOS object index create failure\n");
			pmemobj_tx_abort(ENOMEM);
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
	TOID(struct vos_container)	*object_address;
	struct vc_hdl			*co_hdl = NULL;
	struct uuid_key			 ukey;

	/* Lookup container handle of hash link */
	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		ret = -DER_INVAL;
		goto exit;
	}

	uuid_copy(ukey.uuid, co_uuid);
	ret = vos_chash_lookup(vpool->vp_ph, vos_pool2coi_table(vpool),
			       (void *)&ukey, sizeof(ukey),
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

	uuid_copy(co_hdl->vc_id, co_uuid);
	co_hdl->vc_phdl		= vpool;
	co_hdl->vc_co		= D_RW(*object_address);
	co_hdl->vc_obj_table	= D_RW(co_hdl->vc_co->vc_obtable);
	co_hdl->vc_epoch_table	= D_RW(co_hdl->vc_co->vc_ehtable);

	/* Cache this btr object ID in container handle */
	ret = dbtree_open_inplace(&co_hdl->vc_obj_table->obtable,
				 &co_hdl->vc_phdl->vp_uma,
				 &co_hdl->vc_btr_hdl);
	if (ret) {
		D_ERROR("No Object handle, Tree open failed\n");
		ret = -DER_NONEXIST;
		goto exit;
	}

	daos_hhash_hlink_init(&co_hdl->vc_hlink, &co_hdl_hh_ops);
	daos_hhash_link_insert(daos_vos_hhash, &co_hdl->vc_hlink,
			       DAOS_HTYPE_VOS_CO);
	daos_hhash_link_key(&co_hdl->vc_hlink, &coh->cookie);
	vos_co_putref_handle(co_hdl);
exit:
	/* if success ref-count released during close/delete */
	if (ret) {
		/**
		 * TODO: move vos_pool_putref_handle to
		 * daos_co_hhash_free once deadlock on
		 * daos handle hash is removed.
		 */
		vos_pool_putref_handle(vpool);
		if (co_hdl)
			daos_co_hhash_free(&co_hdl->vc_hlink);
	}
	return ret;
}

/**
 * Release container open handle
 */
int
vos_co_close(daos_handle_t coh, daos_event_t *ev)
{

	struct vc_hdl		*co_hdl = NULL;

	co_hdl = vos_co_lookup_handle(coh);
	if (!co_hdl) {
		D_ERROR("Invalid handle for container\n");
		return -DER_INVAL;
	}

	dbtree_close(co_hdl->vc_btr_hdl);
	vos_pool_putref_handle(co_hdl->vc_phdl);
	daos_hhash_link_delete(daos_vos_hhash,
			       &co_hdl->vc_hlink);
	vos_co_putref_handle(co_hdl);

	return 0;
}

/**
 * Destroy a container
 */
int
vos_co_destroy(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				ret    = 0;
	struct vp_hdl			*vpool = NULL;
	TOID(struct vos_container)	*object_address;
	struct uuid_key			 ukey;
	TOID(struct vos_chash_table)	 coi_table;

	vpool = vos_pool_lookup_handle(poh);
	if (!vpool) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		ret = -DER_INVAL;
		goto exit;
	}

	coi_table = vos_pool2coi_table(vpool);
	uuid_copy(ukey.uuid, co_uuid);
	ret = vos_chash_lookup(vpool->vp_ph, coi_table,
			       (void *)&ukey, sizeof(ukey),
			       (void **)&object_address);
	if (ret) {
		D_ERROR("Container does not exist\n");
		goto exit;
	}

	/**
	 * Need to destroy object index before removing
	 * container entry
	 * Outer transaction for all destroy operations
	 * both oi_destroy and chash_remove have internal
	 * transaction which will be nested.
	 */
	TX_BEGIN(vpool->vp_ph) {
		struct vos_object_index	*obj_index;

		obj_index = D_RW(D_RW(*object_address)->vc_obtable);
		ret = vos_oi_destroy(vpool, obj_index);
		if (ret) {
			D_ERROR("OI destroy failed with error : %d",
				ret);
			pmemobj_tx_abort(EFAULT);
		}
		/**
		 * vos_chash_remove hash its own transactions
		 * since chash_table stores key and value in PMEM
		 * Its just enough to remove the entry
		 *
		*/
		ret = vos_chash_remove(vpool->vp_ph, coi_table,
				       (void *)&ukey, sizeof(ukey));
		if (ret) {
			D_ERROR("Chash remove failed with error : %d", ret);
			pmemobj_tx_abort(EFAULT);
		}

	}  TX_ONABORT {
		D_ERROR("Destroying container transaction failed %s\n",
			pmemobj_errormsg());
	} TX_END;

exit:
	vos_pool_putref_handle(vpool);
	return ret;
}

/**
 * Query container information.
 */
int
vos_co_query(daos_handle_t coh, vos_co_info_t *vc_info, daos_event_t *ev)
{

	int				ret    = 0;
	struct vc_hdl			*co_hdl = NULL;

	co_hdl = vos_co_lookup_handle(coh);
	if (!co_hdl) {
		D_ERROR("Invalid handle for container\n");
		return -DER_INVAL;
	}

	memcpy(vc_info, &co_hdl->vc_co->vc_info, sizeof(*vc_info));
	vos_co_putref_handle(co_hdl);
	return ret;
}
