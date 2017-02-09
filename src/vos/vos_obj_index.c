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
 * VOS object table definition
 * vos/vos_obj_index.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define DD_SUBSYS	DD_FAC(vos)

#include <daos_errno.h>
#include <daos/mem.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>
#include <vos_hhash.h>

/** iterator for oid */
struct vos_oid_iter {
	struct vos_iterator	oit_iter;
	/* Handle of iterator */
	daos_handle_t		oit_hdl;
	/* Container handle */
	struct vc_hdl		*oit_chdl;
};

static int
vo_hkey_size(struct btr_instance *tins)
{
	return sizeof(daos_unit_oid_t);
}

static void
vo_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
vo_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	     daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct vos_obj)	vo_rec_mmid;
	struct vos_obj		*vo_rec;

	/* Allocate a PMEM value of type vos_obj */
	vo_rec_mmid = umem_znew_typed(&tins->ti_umm, struct vos_obj);

	if (TMMID_IS_NULL(vo_rec_mmid))
		return -DER_NOMEM;

	vo_rec = umem_id2ptr_typed(&tins->ti_umm, vo_rec_mmid);
	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));
	vo_rec->vo_oid = *(daos_unit_oid_t *)(key_iov->iov_buf);
	daos_iov_set(val_iov, vo_rec, sizeof(struct vos_obj));
	rec->rec_mmid = umem_id_t2u(vo_rec_mmid);
	return 0;
}

static int
vo_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	*umm = &tins->ti_umm;
	TMMID(struct vos_obj)	vo_rec_mmid;

	vo_rec_mmid = umem_id_u2t(rec->rec_mmid, struct vos_obj);
	umem_free_typed(umm, vo_rec_mmid);

	return 0;
}

static int
vo_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_obj		*vo_rec = NULL;

	D_ASSERT(val_iov != NULL);

	vo_rec = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	daos_iov_set(val_iov, vo_rec, sizeof(struct vos_obj));

	return 0;
}

static int
vo_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	/**
	 * TODO : Implement update when object metadata is introduced
	 */
	return 0;
}

static btr_ops_t voi_ops = {
	.to_hkey_size	= vo_hkey_size,
	.to_hkey_gen	= vo_hkey_gen,
	.to_rec_alloc	= vo_rec_alloc,
	.to_rec_free	= vo_rec_free,
	.to_rec_fetch	= vo_rec_fetch,
	.to_rec_update	= vo_rec_update,
};

/**
 * For testing obj index deletion
 */
int
vos_oi_find(struct vc_hdl *co_hdl, daos_unit_oid_t oid,
	    struct vos_obj **obj)
{
	int				rc = 0;
	daos_iov_t			key_iov, val_iov;
	struct vos_object_index		*obj_index = NULL;

	obj_index = co_hdl->vc_obj_table;
	if (!obj_index) {
		D_ERROR("Object index cannot be empty\n");
		return -DER_NONEXIST;
	}

	daos_iov_set(&key_iov, &oid, sizeof(daos_unit_oid_t));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_lookup(co_hdl->vc_btr_hdl, &key_iov, &val_iov);
	if (rc == 0)
		*obj = val_iov.iov_buf;

	return rc;
}


/**
 * Find the object by OID and return it, or create an object for the oid.
 */
int
vos_oi_find_alloc(struct vc_hdl *co_hdl, daos_unit_oid_t oid,
		  struct vos_obj **obj)
{
	int				rc = 0;
	daos_iov_t			key_iov, val_iov;

	D_DEBUG(DF_VOS2, "Lookup obj "DF_UOID" in the OI table.\n",
		DP_UOID(oid));

	daos_iov_set(&key_iov, &oid, sizeof(daos_unit_oid_t));
	daos_iov_set(&val_iov, NULL, 0);

	rc = vos_oi_find(co_hdl, oid, obj);
	if (rc == 0)
		return rc;

	/* Object ID not found insert it to the OI tree */
	D_DEBUG(DF_VOS1, "Object"DF_UOID" not found adding it..\n",
		DP_UOID(oid));

	rc = dbtree_update(co_hdl->vc_btr_hdl, &key_iov, &val_iov);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}

	*obj = val_iov.iov_buf;
	return rc;
}

/*TODO: Implement Remove once we have dbtree delete
 *	Implement Update with SR metadata added
 */


/**
 * Internal usage APIs
 * For use from container APIs and init APIs
 */
int
vos_oi_init()
{
	int	rc;

	D_DEBUG(DF_VOS2, "Registering class for OI table Class: %d\n",
		VOS_BTR_OIT);

	rc = dbtree_class_register(VOS_BTR_OIT, 0, &voi_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_oi_create(struct vos_pool *pool, struct vos_object_index *obj_index)
{

	int				rc = 0;
	daos_handle_t			btr_hdl;
	struct btr_root			*oi_root = NULL;

	if (!pool || !obj_index) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	/**
	 * Inplace btr_root
	 */
	oi_root = (struct btr_root *) &(obj_index->obtable);

	if (!oi_root->tr_class) {
		D_DEBUG(DF_VOS2, "create OI Tree in-place: %d\n",
			VOS_BTR_OIT);

		rc = dbtree_create_inplace(VOS_BTR_OIT, 0, OT_BTREE_ORDER,
					   &pool->vp_uma, &obj_index->obtable,
					   &btr_hdl);
		if (rc)
			D_ERROR("dbtree create failed\n");
	}

	return rc;
}

int
vos_oi_destroy(struct vos_pool *pool, struct vos_object_index *obj_index)
{
	int				rc = 0;
	daos_handle_t			btr_hdl;


	if (!pool || !obj_index) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	/**
	 * TODO: Check for KVobject oih->or_obj
	 * if not empty. Destroy it too.
	 */
	rc = dbtree_open_inplace(&obj_index->obtable, &pool->vp_uma, &btr_hdl);
	if (rc) {
		D_ERROR("No Object handle, Tree open failed\n");
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

	/**
	 * TODO: Check for KVobject oih->or_obj
	 * if not empty. Destroy it too.
	 */
	rc = dbtree_destroy(btr_hdl);
	if (rc)
		D_ERROR("OI BTREE destroy failed\n");
exit:
	return rc;
}


static struct vos_oid_iter*
vos_iter2oid_iter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_oid_iter, oit_iter);
}

struct vos_oid_iter*
vos_hdl2oid_iter(daos_handle_t hdl)
{
	return vos_iter2oid_iter(vos_hdl2iter(hdl));
}

static int
vos_oid_iter_fini(struct vos_iterator *iter)
{
	int			rc  = 0;
	struct vos_oid_iter	*oid_iter = NULL;

	/** iter type should be VOS_ITER_OBJ */
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	oid_iter = vos_iter2oid_iter(iter);

	if (!daos_handle_is_inval(oid_iter->oit_hdl)) {
		rc = dbtree_iter_finish(oid_iter->oit_hdl);
		if (rc)
			D_ERROR("oid_iter_fini failed:%d\n", rc);
	}

	if (oid_iter->oit_chdl != NULL)
		vos_co_putref_handle(oid_iter->oit_chdl);

	D_FREE_PTR(oid_iter);
	return rc;
}

int
vos_oid_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		  struct vos_iterator **iter_pp)
{
	struct vos_oid_iter	*oid_iter = NULL;
	struct vc_hdl		*co_hdl = NULL;
	int			rc = 0;

	if (type != VOS_ITER_OBJ) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_OBJ, type);
		return -DER_INVAL;
	}

	co_hdl = vos_hdl2co(param->ip_hdl);
	if (co_hdl == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oid_iter);
	if (oid_iter == NULL)
		return -DER_NOMEM;

	oid_iter->oit_chdl = co_hdl;
	vos_co_addref_handle(co_hdl);

	rc = dbtree_iter_prepare(co_hdl->vc_btr_hdl, 0, &oid_iter->oit_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &oid_iter->oit_iter;
	return 0;
exit:
	vos_oid_iter_fini(&oid_iter->oit_iter);
	return rc;
}

int
vos_oid_iter_probe(struct vos_iterator *iter, daos_hash_out_t *anchor)
{
	struct vos_oid_iter	*oid_iter = vos_iter2oid_iter(iter);
	dbtree_probe_opc_t	opc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	return dbtree_iter_probe(oid_iter->oit_hdl, opc, NULL, anchor);
}

static int
vos_oid_iter_next(struct vos_iterator *iter)
{
	struct vos_oid_iter	*oid_iter = vos_iter2oid_iter(iter);

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	return dbtree_iter_next(oid_iter->oit_hdl);
}

static int
vos_oid_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		   daos_hash_out_t *anchor)
{
	struct vos_oid_iter	*oid_iter = vos_iter2oid_iter(iter);
	daos_iov_t		rec_iov;
	struct vos_obj		*vo_rec;
	int			rc;

	D_DEBUG(DF_VOS2, "obj-iter oid fetch callback\n");
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	daos_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oid_iter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching oid info\n");
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj));
	vo_rec = (struct vos_obj *)rec_iov.iov_buf;
	it_entry->ie_oid = vo_rec->vo_oid;

	return 0;
}

static int
vos_oid_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_oid_iter	*oiter = vos_iter2oid_iter(iter);
	PMEMobjpool		*pop;
	int			rc = 0;


	D_DEBUG(DF_VOS2, "oid-iter delete callback\n");
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	pop = vos_co2pop(oiter->oit_chdl);

	TX_BEGIN(pop) {
		rc = dbtree_iter_delete(oiter->oit_hdl, args);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DF_VOS1, "Failed to delete oid entry: %d\n", rc);
	} TX_END

	return rc;
}

struct vos_iter_ops vos_oid_iter_ops = {
	.iop_prepare =	vos_oid_iter_prep,
	.iop_finish  =  vos_oid_iter_fini,
	.iop_probe   =	vos_oid_iter_probe,
	.iop_next    =  vos_oid_iter_next,
	.iop_fetch   =  vos_oid_iter_fetch,
	.iop_delete  =	vos_oid_iter_delete,
};
