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

#define CT_BTREE_ORDER 20

/**
 * Wrapper buffer to fetch
 * direct pointers
 */
struct vc_val_buf {
	struct vos_container		*vc_co;
	struct vp_hdl			*vc_vpool;
};

/** iterator for co_uuid */
struct vos_co_iter {
	struct vos_iterator	cot_iter;
	/* Handle of iterator */
	daos_handle_t		cot_hdl;
	/* Pool handle */
	struct vp_hdl		*cot_phdl;
};


static int
vc_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct daos_uuid);
}

static void
vc_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct daos_uuid));
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
	struct daos_uuid		*u_key = NULL;
	int				rc = 0;

	D_DEBUG(DF_VOS3, "Allocating entry for container table\n");
	u_key = (struct daos_uuid *)key_iov->iov_buf;
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

static inline int
vos_co_tree_lookup(struct vp_hdl *vpool, struct daos_uuid *ukey,
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
	daos_iov_set(&key, ukey, sizeof(struct daos_uuid));
	daos_iov_set(&value, sbuf, sizeof(struct vc_val_buf));
	return dbtree_lookup(btr_hdl, &key, &value);
}


/**
 * Create a container within a VOSP
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid)
{

	int				rc = 0;
	struct vp_hdl			*vpool = NULL;
	struct daos_uuid		ukey;
	struct vc_val_buf		s_buf;

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle?\n");
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

		daos_iov_set(&key, &ukey, sizeof(struct daos_uuid));
		daos_iov_set(&value, &s_buf, sizeof(struct vc_val_buf));

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
	return rc;
}

/**
 * Open a container within a VOSP
 */
int
vos_co_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh)
{

	int				rc = 0;
	struct vp_hdl			*vpool = NULL;
	struct daos_uuid		ukey;
	struct vc_val_buf		s_buf;
	struct vc_hdl			*co_hdl = NULL;

	D_DEBUG(DF_VOS2, "Open container "DF_UUID"\n", DP_UUID(co_uuid));
	D_DEBUG(DF_VOS2, "Checking if container handle exists for "DF_UUID"\n",
		DP_UUID(co_uuid));
	D_DEBUG(DF_VOS3, "looking up co_id in container index\n");

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle?\n");
		return -DER_INVAL;
	}
	uuid_copy(ukey.uuid, co_uuid);

	/**
	 * Check if handle exists
	 * then return the handle immediately
	 */
	rc = vos_co_lookup_handle(&ukey, &co_hdl);
	if (rc == 0) {
		D_DEBUG(DF_VOS2, "Found handle in DRAM UUID hash\n");
		*coh = vos_co2hdl(co_hdl);
		D_GOTO(exit, rc);
	}

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

	rc = vos_co_insert_handle(co_hdl, &ukey, coh);
	if (rc) {
		D_ERROR("Error inserting vos container handle to uuid hash\n");
		D_GOTO(exit, rc);
	}

exit:
	if (rc != 0 && co_hdl)
		vos_co_uhash_free(&co_hdl->vc_uhlink);

	return rc;
}

/**
 * Release container open handle
 */
int
vos_co_close(daos_handle_t coh)
{
	struct vc_hdl	*co_hdl;
	int		 rc;

	co_hdl = vos_hdl2co(coh);
	if (co_hdl == NULL) {
		D_ERROR("Cannot close a NULL handle\n");
		return -DER_INVAL;
	}

	vos_obj_cache_evict(vos_obj_cache_current(), co_hdl);

	rc = vos_co_release_handle(co_hdl);
	if (rc) {
		D_ERROR("Error in deleting container handle\n");
		return rc;
	}

	return 0;
}

/**
 * Query container information
 */
int
vos_co_query(daos_handle_t coh, vos_co_info_t *vc_info)
{

	struct vc_hdl			*co_hdl;

	co_hdl = vos_hdl2co(coh);
	if (co_hdl == NULL) {
		D_ERROR("Empty container handle for querying?\n");
		return -DER_INVAL;
	}

	memcpy(vc_info, &co_hdl->vc_co->vc_info, sizeof(*vc_info));
	return 0;
}


/**
 * Destroy a container
 */
int
vos_co_destroy(daos_handle_t poh, uuid_t co_uuid)
{

	int				rc = 0;
	struct vp_hdl			*vpool;
	struct daos_uuid		ukey;
	struct vc_val_buf		s_buf;
	struct vos_object_index		*vc_oi = NULL;
	struct vc_hdl			*co_hdl = NULL;

	uuid_copy(ukey.uuid, co_uuid);
	D_DEBUG(DF_VOS3, "Destroying CO ID in container index "DF_UUID"\n",
		DP_UUID(ukey.uuid));

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle for destroying container?\n");
		return -DER_INVAL;
	}

	rc = vos_co_lookup_handle(&ukey, &co_hdl);
	if (rc != -DER_NONEXIST) {
		D_ERROR("Open reference exists, cannot destroy\n");
		vos_co_putref_handle(co_hdl);
		D_GOTO(exit, rc = -DER_NO_PERM);
	}

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
			D_ERROR("OI destroy failed with error : %d\n",
				rc);
			pmemobj_tx_abort(EFAULT);
		}
		/**
		 * TODO: Add dbtree_remove when available
		 * Currently this is a leak in the table
		 * i.e removing vc_cid.
		 **/
		/**
		 * Temporarily removing PMEM allocations
		 * XXXX: Remove this once dbtree_delete is added.
		 */
		umem_free_typed(&vpool->vp_umm, s_buf.vc_co->vc_obtable);
		umem_free_typed(&vpool->vp_umm, s_buf.vc_co->vc_ehtable);
		/**
		 * Required to eliminate empty records while enumeration
		 * XXX Remove once dbtree_delete is used.
		 **/
		s_buf.vc_co->vc_obtable.oid = UMMID_NULL;
		s_buf.vc_co->vc_ehtable.oid = UMMID_NULL;

	}  TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Destroying container transaction failed %d\n", rc);
	} TX_END;
exit:
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
vos_ci_create(struct umem_attr *p_umem_attr,
	      struct vos_container_index *co_index)
{

	int			rc = 0;
	struct btr_root		*ci_root = NULL;
	daos_handle_t		btr_hdl;

	if (!co_index) {
		D_ERROR("Container_index create failed\n");
		return -DER_INVAL;
	}

	ci_root = (struct btr_root *) &(co_index->ci_btree);

	D_ASSERT(ci_root->tr_class == 0);
	D_DEBUG(DF_VOS2, "Create CI Tree in-place: %d\n",
		VOS_BTR_CIT);
	rc = dbtree_create_inplace(VOS_BTR_CIT, 0, CT_BTREE_ORDER,
				   p_umem_attr,
				   &co_index->ci_btree,
				   &btr_hdl);
	if (rc) {
		D_ERROR("DBtree create failed\n");
		D_GOTO(exit, rc);
	}

	rc = dbtree_close(btr_hdl);
	if (rc)
		D_ERROR("Error in closing btree handle\n");

exit:
	return rc;
}

static struct vos_co_iter*
vos_iter2co_iter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_co_iter, cot_iter);
}

static int
vos_co_iter_fini(struct vos_iterator *iter)
{
	int			rc = 0;
	struct vos_co_iter	*co_iter;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	co_iter = vos_iter2co_iter(iter);

	if (!daos_handle_is_inval(co_iter->cot_hdl)) {
		rc = dbtree_iter_finish(co_iter->cot_hdl);
		if (rc)
			D_ERROR("co_iter_fini failed: %d\n", rc);
	}

	if (co_iter->cot_phdl != NULL)
		vos_pool_putref_handle(co_iter->cot_phdl);

	D_FREE_PTR(co_iter);
	return rc;
}

int
vos_co_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		 struct vos_iterator **iter_pp)
{
	struct vos_co_iter	*co_iter = NULL;
	struct vp_hdl		*vpool = NULL;
	int			rc = 0;

	if (type != VOS_ITER_COUUID) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_COUUID, type);
		return -DER_INVAL;
	}

	vpool = vos_hdl2pool(param->ip_hdl);
	if (vpool == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(co_iter);
	if (co_iter == NULL)
		return -DER_NOMEM;

	co_iter->cot_phdl = vpool;
	vos_pool_addref_handle(vpool);

	rc = dbtree_iter_prepare(vpool->vp_ct_hdl, 0, &co_iter->cot_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &co_iter->cot_iter;
	return 0;
exit:
	vos_co_iter_fini(&co_iter->cot_iter);
	return rc;
}

static int
vos_co_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		  daos_hash_out_t *anchor)
{
	struct vos_co_iter	*co_iter = vos_iter2co_iter(iter);
	daos_iov_t		key, value;
	struct daos_uuid	ukey;
	struct vc_val_buf	vc_val_buf;
	int			rc;

	D_DEBUG(DF_VOS2, "Container iter co uuid fetch callback\n");
	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	daos_iov_set(&key, &ukey, sizeof(struct daos_uuid));
	daos_iov_set(&value, &vc_val_buf, sizeof(struct vc_val_buf));

	uuid_clear(it_entry->ie_couuid);

	rc = dbtree_iter_fetch(co_iter->cot_hdl, &key, &value, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching co info: %d\n", rc);
		return rc;
	}
	D_ASSERT(value.iov_len == sizeof(struct vc_val_buf));
	uuid_copy(it_entry->ie_couuid, vc_val_buf.vc_co->vc_id);

	return rc;
}

/**
 * XXX: Remove when dbtree_delete is available
 * Temporary function to check if record is NULL
 * needed until dbtree_delete is implemented
 * to skip zombie records.
 */
static int
vos_co_iter_check(struct vos_co_iter *co_iter, daos_hash_out_t *anchor)
{

	daos_iov_t		key, value;
	struct daos_uuid	ukey;
	struct vc_val_buf	vc_val_buf;
	int			rc = 0;

	daos_iov_set(&key, &ukey, sizeof(struct daos_uuid));
	daos_iov_set(&value, &vc_val_buf, sizeof(struct vc_val_buf));
	rc = dbtree_iter_fetch(co_iter->cot_hdl, &key, &value, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching co info: %d\n", rc);
		D_GOTO(exit, rc);
	}

	if (TMMID_IS_NULL(vc_val_buf.vc_co->vc_obtable))
		rc = -DER_NONEXIST;
exit:
	return rc;
}


static int
vos_co_iter_next(struct vos_iterator *iter)
{
	struct vos_co_iter	*co_iter = vos_iter2co_iter(iter);
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);
	/**
	 * TODO:
	 * This section of code currently skips
	 * zombie records from container tree
	 * Currently does fetch(es) to verify the presence
	 * of a valid record, stops when there is a
	 * valid record or at last node.
	 * Not needed when we delete the dbtree node
	 *
	 * This section of while loop will be replaced
	 * by
	 * return  dbtree_iter_probe(...);
	 */
	while (1) {
		rc = dbtree_iter_next(co_iter->cot_hdl);
		if (rc)
			break;
		rc = vos_co_iter_check(co_iter, NULL);
		if (!rc)
			break;
	}
	return rc;
}

static int
vos_co_iter_probe(struct vos_iterator *iter, daos_hash_out_t *anchor)
{
	struct vos_co_iter	*co_iter = vos_iter2co_iter(iter);
	dbtree_probe_opc_t	opc;
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	rc = dbtree_iter_probe(co_iter->cot_hdl, opc, NULL, anchor);
	/**
	 * TODO:
	 * This section of code currently skips
	 * zombie records from container tree
	 * temp solution until dbtree_delete is
	 * implemented.
	 */

	if (rc == 0) {
		rc = vos_co_iter_check(co_iter, NULL);
		if (rc != 0)
			rc = vos_co_iter_next(iter);
	}
	return rc;
}



struct vos_iter_ops vos_co_iter_ops = {
	.iop_prepare = vos_co_iter_prep,
	.iop_finish  = vos_co_iter_fini,
	.iop_probe   = vos_co_iter_probe,
	.iop_next    = vos_co_iter_next,
	.iop_fetch   = vos_co_iter_fetch,
};
