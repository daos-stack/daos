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
 * VOS Container API implementation
 * vos/vos_container.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <daos_srv/vos.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/mem.h>
#include <daos/hash.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>
#include <vos_hhash.h>

/**
 * NB: hide the dark secret that
 * uuid_t is an array not a structure
 */
struct uuid_key {
	uuid_t			uuid;
};

/**
 * Wrapper buffer to fetch
 * direct pointers
 */
struct vc_val_buf {
	struct vos_container		*vc_co;
	struct vp_hdl			*vc_vpool;
};

static int
vc_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct uuid_key);
}

static void
vc_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct uuid_key));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
vc_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	struct umem_instance		*umm = &tins->ti_umm;
	struct vos_container		*vc_rec = NULL;

	TMMID(struct vos_container) vc_cid = umem_id_u2t(rec->rec_mmid,
							 struct vos_container);
	if (TMMID_IS_NULL(vc_cid))
		return -DER_NONEXIST;

	vc_rec = umem_id2ptr_typed(&tins->ti_umm, vc_cid);

	if (!TMMID_IS_NULL(vc_rec->vc_obtable))
		umem_free_typed(&tins->ti_umm,
				vc_rec->vc_obtable);
	if (!TMMID_IS_NULL(vc_rec->vc_ehtable))
		umem_free_typed(&tins->ti_umm,
				vc_rec->vc_ehtable);

	umem_free_typed(umm, vc_cid);
	return 0;
}

static int
vc_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	     daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct vos_container)	vc_cid;
	struct vos_container		*vc_rec = NULL;
	struct vos_object_index		*vc_oi = NULL;
	struct vc_val_buf		*vc_val_buf = NULL;
	struct uuid_key			*u_key = NULL;
	int				rc = 0;

	D_DEBUG(DF_VOS3, "Allocating entry for container table\n");
	u_key = (struct uuid_key *)key_iov->iov_buf;
	D_DEBUG(DF_VOS3, DF_UUID" Allocating record for container\n",
		DP_UUID(u_key->uuid));

	vc_val_buf = (struct vc_val_buf *)(val_iov->iov_buf);
	vc_cid = umem_znew_typed(&tins->ti_umm, struct vos_container);
	if (TMMID_IS_NULL(vc_cid))
		return -DER_NOMEM;

	rec->rec_mmid = umem_id_t2u(vc_cid);
	vc_rec = umem_id2ptr_typed(&tins->ti_umm, vc_cid);
	uuid_copy(vc_rec->vc_id, u_key->uuid);
	vc_val_buf->vc_co = vc_rec;

	vc_rec->vc_obtable = umem_znew_typed(&tins->ti_umm,
					     struct vos_object_index);
	if (TMMID_IS_NULL(vc_rec->vc_obtable))
		D_GOTO(exit, rc = -DER_NOMEM);

	vc_rec->vc_ehtable = umem_znew_typed(&tins->ti_umm,
					     struct vos_epoch_index);
	if (TMMID_IS_NULL(vc_rec->vc_ehtable))
		D_GOTO(exit, rc = -DER_NOMEM);

	vc_oi = umem_id2ptr_typed(&tins->ti_umm, vc_rec->vc_obtable);
	rc = vos_oi_create(vc_val_buf->vc_vpool, vc_oi);
	if (rc) {
		D_ERROR("VOS object index create failure\n");
		D_GOTO(exit, rc);
	}
exit:
	if (rc != 0)
		vc_rec_free(tins, rec);

	return rc;
}


static int
vc_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_container		*vc_rec = NULL;
	struct vc_val_buf		*vc_val_buf = NULL;

	vc_rec = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	vc_val_buf = (struct vc_val_buf *)val_iov->iov_buf;
	vc_val_buf->vc_co = vc_rec;
	val_iov->iov_len = sizeof(struct vc_val_buf);

	return 0;
}

static int
vc_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	D_DEBUG(DF_VOS3, "At VOS container rec update\n");
	D_DEBUG(DF_VOS3, "Record exists already. Nothing to do\n");
	return 0;
}

static btr_ops_t vct_ops = {
	.to_hkey_size	= vc_hkey_size,
	.to_hkey_gen	= vc_hkey_gen,
	.to_rec_alloc	= vc_rec_alloc,
	.to_rec_free	= vc_rec_free,
	.to_rec_fetch	= vc_rec_fetch,
	.to_rec_update  = vc_rec_update,
};

static inline void
vos_co_set_kv(daos_iov_t *key, daos_iov_t *val, void *kbuf,
	      size_t ksize, void *vbuf, size_t vsize)
{
	daos_iov_set(key, kbuf, ksize);
	daos_iov_set(val, vbuf, vsize);
}

static inline int
vos_co_tree_lookup(struct vp_hdl *vpool, struct uuid_key *ukey,
		   struct vc_val_buf *sbuf)
{
	struct vos_container_index	*coi;
	daos_handle_t			btr_hdl;
	daos_iov_t			key, value;
	int				rc;

	coi = vos_pool2coi_table(vpool);
	rc = dbtree_open_inplace(&coi->ci_btree, &vpool->vp_uma,
				 &btr_hdl);
	D_ASSERT(rc == 0);
	vos_co_set_kv(&key, &value, ukey,
		      sizeof(struct uuid_key), sbuf,
		      sizeof(struct vc_val_buf));

	return dbtree_lookup(btr_hdl, &key, &value);
}


/**
 * Create a container within a VOSP
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				rc = 0;
	struct vp_hdl			*vpool = NULL;
	struct uuid_key			ukey;
	struct vc_val_buf		s_buf;

	vpool = vos_pool_lookup_handle(poh);
	if (NULL == vpool) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		return -DER_INVAL;
	}

	D_DEBUG(DF_VOS3, "looking up co_id in container index\n");
	uuid_copy(ukey.uuid, co_uuid);
	s_buf.vc_vpool = vpool;

	rc = vos_co_tree_lookup(vpool, &ukey, &s_buf);
	if (!rc) {
		/* Check if attemt to reuse the same container uuid */
		D_ERROR("Container already exists\n");
		D_GOTO(exit, rc = -DER_EXIST);
	}

	TX_BEGIN(vpool->vp_ph) {
		daos_iov_t key, value;

		vos_co_set_kv(&key, &value,
			      &ukey, sizeof(struct uuid_key),
			      &s_buf, sizeof(struct vc_val_buf));

		rc = dbtree_update(vpool->vp_ct_hdl, &key, &value);
		if (rc) {
			D_ERROR("Creating a container entry: %d\n", rc);
			pmemobj_tx_abort(ENOMEM);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Creating a container entry: %d\n", rc);
	} TX_END;

exit:
	vos_pool_putref_handle(vpool);
	return rc;
}

/**
 * Open a container within a VOSP
 */
int
vos_co_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh,
	    daos_event_t *ev)
{

	int				rc = 0;
	struct vp_hdl			*vpool = NULL;
	struct uuid_key			ukey;
	struct vc_val_buf		s_buf;
	struct vc_hdl			*co_hdl = NULL;

	/* Lookup container handle of hash link */
	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		return -DER_INVAL;
	}

	D_DEBUG(DF_VOS2, "Open container "DF_UUID"\n", DP_UUID(co_uuid));
	D_DEBUG(DF_VOS3, "looking up co_id in container index\n");

	uuid_copy(ukey.uuid, co_uuid);
	rc = vos_co_tree_lookup(vpool, &ukey, &s_buf);
	if (rc) {
		D_DEBUG(DF_VOS3, DF_UUID" container does not exist\n",
			DP_UUID(co_uuid));
		D_GOTO(exit, rc);
	}

	D_ALLOC_PTR(co_hdl);
	if (NULL == co_hdl) {
		D_ERROR("Error in allocating container handle\n");
		D_GOTO(exit, rc = -DER_NOSPACE);
	}

	uuid_copy(co_hdl->vc_id, co_uuid);
	co_hdl->vc_phdl		= vpool;
	co_hdl->vc_co		= s_buf.vc_co;
	co_hdl->vc_obj_table	= umem_id2ptr_typed(&vpool->vp_umm,
						    s_buf.vc_co->vc_obtable);
	co_hdl->vc_epoch_table	= umem_id2ptr_typed(&vpool->vp_umm,
						    s_buf.vc_co->vc_ehtable);

	/* Cache this btr object ID in container handle */
	rc = dbtree_open_inplace(&co_hdl->vc_obj_table->obtable,
				 &co_hdl->vc_phdl->vp_uma,
				 &co_hdl->vc_btr_hdl);
	if (rc) {
		D_ERROR("No Object handle, Tree open failed\n");
		D_GOTO(exit, rc);
	}

	vos_co_hhash_init(co_hdl);
	vos_co_insert_handle(co_hdl, coh);
	vos_co_putref_handle(co_hdl);
exit:
	/* if success ref-count released during close/delete */
	if (rc) {
		vos_pool_putref_handle(vpool);
		if (co_hdl)
			vos_co_hhash_free(&co_hdl->vc_hlink);
	}
	return rc;
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
	vos_co_delete_handle(co_hdl);
	vos_co_putref_handle(co_hdl);

	return 0;
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


/**
 * Destroy a container
 */
int
vos_co_destroy(daos_handle_t poh, uuid_t co_uuid, daos_event_t *ev)
{

	int				rc = 0;
	struct vp_hdl			*vpool = NULL;
	struct uuid_key			ukey;
	struct vc_val_buf		s_buf;
	struct vos_object_index		*vc_oi = NULL;

	vpool = vos_pool_lookup_handle(poh);
	if (vpool == NULL) {
		D_ERROR("Error in looking up VOS pool handle from hhash\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	D_DEBUG(DF_VOS3, "Destroying CO ID in container index\n");
	uuid_copy(ukey.uuid, co_uuid);

	rc = vos_co_tree_lookup(vpool, &ukey, &s_buf);
	if (rc) {
		D_DEBUG(DF_VOS3, DF_UUID" container does not exist\n",
			DP_UUID(co_uuid));
		D_GOTO(exit, rc);
	}

	/**
	 * Need to destroy object index before removing
	 * container entry
	 * Outer transaction for all destroy operations
	 * both oi_destroy and chash_remove have internal
	 * transaction which will be nested.
	 */
	TX_BEGIN(vpool->vp_ph) {
		vc_oi = umem_id2ptr_typed(&vpool->vp_umm,
					  s_buf.vc_co->vc_obtable);
		rc = vos_oi_destroy(vpool, vc_oi);
		if (rc) {
			D_ERROR("OI destroy failed with error : %d",
				rc);
			pmemobj_tx_abort(EFAULT);
		}
		/**
		 * TODO: Add dbtree_remove when available
		 * Currently this is a leak in the table
		 * i.e removing vc_cid.
		 **/
		/** Temporarily removing PMEM allocations **/
		if (!TMMID_IS_NULL(s_buf.vc_co->vc_obtable))
			umem_free_typed(&vpool->vp_umm,
					s_buf.vc_co->vc_obtable);
		if (!TMMID_IS_NULL(s_buf.vc_co->vc_ehtable))
			umem_free_typed(&vpool->vp_umm,
					s_buf.vc_co->vc_ehtable);
	}  TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Destroying container transaction failed %d\n", rc);
	} TX_END;

exit:
	vos_pool_putref_handle(vpool);
	return rc;
}

/**
 * Internal Usage API
 * For use from container APIs and int APIs
 */

int
vos_ci_init()
{
	int	rc;

	D_DEBUG(DF_VOS2, "Registering Container table class: %d\n",
		VOS_BTR_CIT);

	rc = dbtree_class_register(VOS_BTR_CIT, 0, &vct_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_ci_create(struct vp_hdl *po_hdl,
	      struct vos_container_index *co_index)
{

	int			rc = 0;
	struct btr_root		*ci_root = NULL;

	if (!po_hdl || !co_index) {
		D_ERROR("Invalid handle\nContainer_index create failed\n");
		return -DER_INVAL;
	}

	ci_root = (struct btr_root *) &(co_index->ci_btree);

	D_ASSERT(ci_root->tr_class == 0);
	D_DEBUG(DF_VOS2, "Create CI Tree in-place: %d\n",
		VOS_BTR_CIT);
	rc = dbtree_create_inplace(VOS_BTR_CIT, 0, OT_BTREE_ORDER,
				   &po_hdl->vp_uma,
				   &co_index->ci_btree,
				   &po_hdl->vp_ct_hdl);
	if (rc)
		D_ERROR("DBtree create failed\n");

	return rc;
}
