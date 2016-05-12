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
 * VOS object table definition
 * vos/vos_object_table.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#include <daos_errno.h>
#include <daos/mem.h>
#include <daos/btree.h>
#include <daos_types.h>
#include "vos_internal.h"
#include "vos_obj.h"


/**
 * Wrapper buffer to fetch
 * direct pointer address to
 * the vos_obj struct.
 */
struct oi_val_buf {
	daos_unit_oid_t *oid;
	struct vos_obj *ptr;
};


static int
vo_hkey_size(struct btr_instance *tins)
{
	return sizeof(daos_unit_oid_t);
}

static void
vo_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	*(uint64_t *)hkey = vos_generate_crc64(key_iov->iov_buf,
					       key_iov->iov_len);
}

static int
vo_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	     daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct vos_obj)	vo_rec_mmid;
	struct vos_obj		*vo_rec;
	struct oi_val_buf	*oi_val_buf = NULL;

	/* Allocate a PMEM value of type vos_obj */
	vo_rec_mmid = umem_znew_typed(&tins->ti_umm, struct vos_obj);

	if (TMMID_IS_NULL(vo_rec_mmid))
		return -DER_NOMEM;
	vo_rec = umem_id2ptr_typed(&tins->ti_umm, vo_rec_mmid);
	oi_val_buf = (struct oi_val_buf *)(val_iov->iov_buf);

	vo_rec->vo_oid = *(oi_val_buf->oid);
	oi_val_buf->ptr = vo_rec;
	rec->rec_mmid = umem_id_t2u(vo_rec_mmid);
	return 0;
}

static int
vo_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	struct umem_instance	*umm = &tins->ti_umm;
	/* TMMID can be in both persistent
	 * and virtual memory
	 */
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
	struct oi_val_buf	*oi_val_buf = NULL;

	if (val_iov != NULL) {
		val_iov->iov_len = sizeof(struct oi_val_buf);
		vo_rec = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
		oi_val_buf = (struct oi_val_buf *)val_iov->iov_buf;
		oi_val_buf->ptr = vo_rec;
	}
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

static btr_ops_t vot_ops = {
	.to_hkey_size	= vo_hkey_size,
	.to_hkey_gen	= vo_hkey_gen,
	.to_rec_alloc	= vo_rec_alloc,
	.to_rec_free	= vo_rec_free,
	.to_rec_fetch	= vo_rec_fetch,
	.to_rec_update	= vo_rec_update,
};


/**
 * Object index API
 */
int
vos_oi_lookup(daos_handle_t coh, daos_unit_oid_t oid,
	      struct vos_obj **obj)
{

	int				rc = 0;
	daos_iov_t			key_iov, val_iov;
	struct vc_hdl			*co_hdl = NULL;
	struct vos_object_index		*obj_index = NULL;
	struct oi_val_buf		s_buf;

	co_hdl = vos_co_lookup_handle(coh);
	if (!co_hdl) {
		D_ERROR("Empty DRAM container handle\n");
		return -DER_INVAL;
	}

	obj_index = co_hdl->vc_obj_table;
	if (!obj_index) {
		D_ERROR("Object index cannot be empty\n");
		rc = -DER_NONEXIST;
		goto exit;
	}

	key_iov.iov_buf = &oid;
	key_iov.iov_len = key_iov.iov_buf_len = sizeof(daos_unit_oid_t);
	daos_iov_set(&val_iov, NULL, 0);

	s_buf.oid = &oid;
	val_iov.iov_buf = &s_buf;
	val_iov.iov_len = sizeof(s_buf);

	rc = dbtree_lookup(co_hdl->vc_btr_hdl, &key_iov, &val_iov);
	if (!rc) {
		D_DEBUG(DF_VOS1, "Object found in obj_index");
		*obj = s_buf.ptr;
	} else {
		/* Object ID not found insert it to the OI tree */
		/**
		 * TODO: Would be useful to have a dbtree_update_fetch API
		 * Currently we wrap the vos_obj*  in a struct to obtain
		 * PMEM address
		 */
		rc = dbtree_update(co_hdl->vc_btr_hdl, &key_iov, &val_iov);
		if (rc)
			D_ERROR("Failed to update Key for Object index\n");
		*obj = s_buf.ptr;
	}
exit:
	vos_co_putref_handle(co_hdl);
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

	rc = dbtree_class_register(OT_BTREE_CLASS, 0, &vot_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_oi_create(struct vp_hdl *po_hdl,
	      struct vos_object_index *obj_index)
{

	int				rc = 0;
	daos_handle_t			btr_hdl;
	struct btr_root			*oi_root = NULL;

	if (!po_hdl || !obj_index) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	/**
	 * Inplace btr_root
	 */
	oi_root = (struct btr_root *) &(obj_index->obtable);

	if (!oi_root->tr_class) {
		rc = dbtree_create_inplace(OT_BTREE_CLASS, 0, OT_BTREE_ORDER,
					  &po_hdl->vp_uma,
					  &obj_index->obtable,
					  &btr_hdl);
		if (rc)
			D_ERROR("dbtree create failed\n");
	}

	return rc;
}

int
vos_oi_destroy(struct vp_hdl *po_hdl,
	       struct vos_object_index *obj_index)
{
	int				rc = 0;
	daos_handle_t			btr_hdl;


	if (!po_hdl || !obj_index) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	/* TODO: Check for KVobject oih->or_obj
	 * if not empty. Destroy it too.
	 */
	rc = dbtree_open_inplace(&obj_index->obtable, &po_hdl->vp_uma,
				 &btr_hdl);
	if (rc) {
		D_ERROR("No Object handle, Tree open failed\n");
		rc = -DER_NONEXIST;
		goto exit;
	}

	/* TODO: Check for KVobject oih->or_obj
	 * if not empty. Destroy it too.
	 */
	rc = dbtree_destroy(btr_hdl);
	if (rc)
		D_ERROR("OI BTREE destroy failed\n");

exit:
	return rc;
}

