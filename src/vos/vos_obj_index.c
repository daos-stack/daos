/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos/object.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>

/** iterator for oid */
struct vos_oi_iter {
	/** embedded VOS common iterator */
	struct vos_iterator	oit_iter;
	/** Handle of iterator */
	daos_handle_t		oit_hdl;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	oit_epr;
	/** previous iterated OID */
	daos_unit_oid_t		oit_id_prev;
	/** Reference to the container */
	struct vos_container	*oit_cont;
};

/**
 * hashed key for object index table.
 */
struct oi_hkey {
	daos_unit_oid_t		oi_oid;
	daos_epoch_t		oi_epc;
};

static int
oi_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct oi_hkey);
}

static void
oi_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct oi_hkey));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
oi_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct oi_hkey	*hkey1 = (struct oi_hkey *)&rec->rec_hkey[0];
	struct oi_hkey	*hkey2 = (struct oi_hkey *)hkey;
	int		 cmprc;

	cmprc = memcmp(&hkey1->oi_oid, &hkey2->oi_oid, sizeof(hkey1->oi_oid));
	if (cmprc)
		return dbtree_key_cmp_rc(cmprc);

	if (hkey1->oi_epc > hkey2->oi_epc)
		return BTR_CMP_MATCHED | BTR_CMP_GT;

	if (hkey1->oi_epc < hkey2->oi_epc)
		return BTR_CMP_MATCHED | BTR_CMP_LT;

	return BTR_CMP_EQ;
}

static int
oi_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	     daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_obj_df	 *obj;
	struct oi_hkey		 *hkey;
	TMMID(struct vos_obj_df)  obj_mmid;

	/* Allocate a PMEM value of type vos_obj_df */
	obj_mmid = umem_znew_typed(&tins->ti_umm, struct vos_obj_df);
	if (TMMID_IS_NULL(obj_mmid))
		return -DER_NOMEM;

	obj = umem_id2ptr_typed(&tins->ti_umm, obj_mmid);

	D_ASSERT(key_iov->iov_len == sizeof(struct oi_hkey));
	hkey = key_iov->iov_buf;

	obj->vo_id	= hkey->oi_oid;
	obj->vo_punched	= hkey->oi_epc;

	daos_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	rec->rec_mmid = umem_id_t2u(obj_mmid);

	D_DEBUG(DB_TRACE, "alloc "DF_UOID" rec "UMMID_PF"\n",
		DP_UOID(obj->vo_id), UMMID_P(rec->rec_mmid));
	return 0;
}

static int
oi_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	 *umm = &tins->ti_umm;
	struct vos_obj_df	 *obj;
	TMMID(struct vos_obj_df)  obj_mmid;
	int			  rc = 0;

	obj_mmid = umem_id_u2t(rec->rec_mmid, struct vos_obj_df);
	obj = umem_id2ptr_typed(&tins->ti_umm, obj_mmid);

	/** Free the KV tree within this object */
	if (obj->vo_tree.tr_class != 0) {
		struct umem_attr uma;
		daos_handle_t	 toh;

		umem_attr_get(&tins->ti_umm, &uma);
		rc = dbtree_open_inplace_ex(&obj->vo_tree, &uma,
					    tins->ti_blks_info, &toh);
		if (rc != 0)
			D_ERROR("Failed to open OI tree: %d\n", rc);
		else
			dbtree_destroy(toh);
	}
	umem_free_typed(umm, obj_mmid);
	return rc;
}

static int
oi_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_obj_df	*obj;

	obj = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	D_DEBUG(DB_TRACE, "fetch "DF_UOID" rec "UMMID_PF"\n",
		DP_UOID(obj->vo_id), UMMID_P(rec->rec_mmid));

	D_ASSERT(val_iov != NULL);
	daos_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	return 0;
}

static int
oi_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key, daos_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t oi_btr_ops = {
	.to_hkey_size	= oi_hkey_size,
	.to_hkey_gen	= oi_hkey_gen,
	.to_hkey_cmp	= oi_hkey_cmp,
	.to_rec_alloc	= oi_rec_alloc,
	.to_rec_free	= oi_rec_free,
	.to_rec_fetch	= oi_rec_fetch,
	.to_rec_update	= oi_rec_update,
};

/**
 * Locate a durable object in OI table.
 */
int
vos_oi_find(struct vos_container *cont, daos_unit_oid_t oid,
	    daos_epoch_t epoch, struct vos_obj_df **obj_p)
{
	struct oi_hkey	hkey;
	daos_iov_t	key_iov;
	daos_iov_t	val_iov;
	int		rc;

	if (!cont->vc_otab_df) {
		D_ERROR("Object index cannot be empty\n");
		return -DER_NONEXIST;
	}

	hkey.oi_oid = oid;
	hkey.oi_epc = epoch;
	daos_iov_set(&key_iov, &hkey, sizeof(hkey));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_fetch(cont->vc_btr_hdl, BTR_PROBE_GE | BTR_PROBE_MATCHED,
			  &key_iov, NULL, &val_iov);
	if (rc == 0) {
		struct vos_obj_df *obj = val_iov.iov_buf;

		D_ASSERT(daos_unit_obj_id_equal(obj->vo_id, oid));
		*obj_p = obj;
	}
	return rc;
}

/**
 * Locate a durable object in OI table, or create it if it's unfound
 */
int
vos_oi_find_alloc(struct vos_container *cont, daos_unit_oid_t oid,
		  daos_epoch_t epoch, struct vos_obj_df **obj_p)
{
	struct oi_hkey	hkey;
	daos_iov_t	key_iov;
	daos_iov_t	val_iov;
	int		rc;

	D_DEBUG(DB_TRACE, "Lookup obj "DF_UOID" in the OI table.\n",
		DP_UOID(oid));

	rc = vos_oi_find(cont, oid, epoch, obj_p);
	if (rc == 0 || rc != -DER_NONEXIST)
		return rc;

	/* Object ID not found insert it to the OI tree */
	D_DEBUG(DB_TRACE, "Object"DF_UOID" not found adding it.. eph "
		DF_U64"\n", DP_UOID(oid), epoch);

	hkey.oi_oid = oid;
	hkey.oi_epc = DAOS_EPOCH_MAX; /* max as incarnation */
	daos_iov_set(&key_iov, &hkey, sizeof(hkey));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, &key_iov, &val_iov);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}

	*obj_p = val_iov.iov_buf;
	return rc;
}

/**
 * Punch a durable object, it will generate a new incarnation with the same
 * ID in OI table.
 */
int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, uint32_t flags, struct vos_obj_df *obj)
{
	struct oi_hkey	hkey;
	daos_iov_t	key_iov;
	daos_iov_t	val_iov;
	int		rc;
	bool		replay = (flags & VOS_OF_REPLAY_PC);

	D_DEBUG(DB_TRACE, "Punch obj "DF_UOID", epoch="DF_U64".\n",
		DP_UOID(oid), epoch);

	if (obj->vo_punched == epoch) {
		D_DEBUG(DB_TRACE, "Punch the same epoch.\n");
		goto out;
	}

	if (obj->vo_punched != DAOS_EPOCH_MAX && !replay) {
		D_ERROR("Underwrite is allowed only for replaying punch\n");
		rc = -DER_NO_PERM;
		goto failed;
	}

	/* create a new incarnation for the punch */
	hkey.oi_oid	= oid;
	hkey.oi_epc	= epoch;
	daos_iov_set(&key_iov, &hkey, sizeof(hkey));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, &key_iov, &val_iov);
	if (rc != 0)
		goto out;

	if (!replay) {
		struct vos_obj_df *tmp;

		D_ASSERT(obj->vo_punched == DAOS_EPOCH_MAX);

		tmp = (struct vos_obj_df *)val_iov.iov_buf;
		D_ASSERT(tmp != obj);
		/* the new incarnation should take over the subtree from
		 * the originally highest incarnation.
		 */
		tmp->vo_tree = obj->vo_tree;
		tmp->vo_incarnation = obj->vo_incarnation;

		/* NB: this changed vos_object_df, which means cache might
		 * be stale, so other callers should invalidate cache.
		 */
		umem_tx_add_ptr(&cont->vc_pool->vp_umm, obj, sizeof(*obj));
		memset(&obj->vo_tree, 0, sizeof(obj->vo_tree));
		obj->vo_incarnation++; /* cache should be revalidated */
	}
 out:
	return 0;
 failed:
	D_ERROR("Failed to punch object, rc=%d\n", rc);
	return rc;
}

static struct vos_oi_iter *
iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_oi_iter, oit_iter);
}

static int
oi_iter_fini(struct vos_iterator *iter)
{
	int			rc  = 0;
	struct vos_oi_iter	*oiter = NULL;

	/** iter type should be VOS_ITER_OBJ */
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	oiter = iter2oiter(iter);

	if (!daos_handle_is_inval(oiter->oit_hdl)) {
		rc = dbtree_iter_finish(oiter->oit_hdl);
		if (rc)
			D_ERROR("oid_iter_fini failed:%d\n", rc);
	}

	if (oiter->oit_cont != NULL)
		vos_cont_decref(oiter->oit_cont);

	D_FREE_PTR(oiter);
	return rc;
}

static int
oi_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	   struct vos_iterator **iter_pp)
{
	struct vos_oi_iter	*oiter = NULL;
	struct vos_container	*cont = NULL;
	int			rc = 0;

	if (type != VOS_ITER_OBJ) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_OBJ, type);
		return -DER_INVAL;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->oit_epr  = param->ip_epr;
	oiter->oit_cont = cont;
	vos_cont_addref(cont);

	rc = dbtree_iter_prepare(cont->vc_btr_hdl, 0, &oiter->oit_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &oiter->oit_iter;
	return 0;
exit:
	oi_iter_fini(&oiter->oit_iter);
	return rc;
}

/**
 * This function checks if the current object can match the condition, it
 * returns immediately on true, otherwise it will move the iterator cursor
 * to the object matching the condition.
 */
static int
oi_iter_match_probe(struct vos_iterator *iter)
{
	struct vos_oi_iter	*oiter	= iter2oiter(iter);
	daos_epoch_range_t	*epr	= &oiter->oit_epr;
	char			*str	= NULL;
	int			 rc;

	if (oiter->oit_epr.epr_lo == 0 &&
	    oiter->oit_epr.epr_hi == DAOS_EPOCH_MAX)
		goto out; /* no condition so it's a match */

	while (1) {
		struct vos_obj_df *obj;
		struct oi_hkey	   hkey;
		int		   probe;
		daos_iov_t	   iov;

		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &iov, NULL);
		if (rc != 0) {
			str = "fetch";
			goto failed;
		}

		D_ASSERT(iov.iov_len == sizeof(struct vos_obj_df));
		obj = (struct vos_obj_df *)iov.iov_buf;

		probe = 0;
		if (obj->vo_punched < epr->epr_lo) {
			/* NB: object should be invisible in punched epoch */
			/* epoch range of the returned object is lower than the
			 * condition, we should call probe() for the epoch
			 * range condition.
			 */
			probe = BTR_PROBE_GE;
			hkey.oi_epc = epr->epr_lo;

		} else if (obj->vo_punched > epr->epr_hi) {
			/* punched epoch is higher than the upper boundary,
			 * it might or might not match the condition.
			 */
			if (!daos_unit_obj_id_equal(obj->vo_id,
						    oiter->oit_id_prev)) {
				/* the first object incarnation which is
				 * higher than the upper boundary, it should
				 * be returned.
				 */
				oiter->oit_id_prev = obj->vo_id;
			} else {
				/* move to the next object...
				 *
				 * combination of DAOS_EPOCH_MAX and
				 * BTR_PROBE_GT will allow us to find the
				 * next object.
				 */
				probe = BTR_PROBE_GT;
				hkey.oi_epc = DAOS_EPOCH_MAX;
			}
		}

		if (!probe) /* matches the condition */
			break;

		hkey.oi_oid = obj->vo_id;
		daos_iov_set(&iov, &hkey, sizeof(hkey));
		rc = dbtree_iter_probe(oiter->oit_hdl, probe, &iov, NULL);
		if (rc != 0) {
			str = "probe";
			goto failed;
		}
	}
 out:
	return 0;
 failed:
	if (rc == -DER_NONEXIST)
		return 0; /* end of iteration, not a failure */

	D_ERROR("iterator %s failed, rc=%d\n", str, rc);
	return rc;
}

static int
oi_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	dbtree_probe_opc_t	 opc;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	rc = dbtree_iter_probe(oiter->oit_hdl, opc, NULL, anchor);
	if (rc)
		D_GOTO(out, rc);

	/* NB: these probe cannot guarantee the returned entry is within
	 * the condition epoch range.
	 */
	rc = oi_iter_match_probe(iter);
 out:
	return rc;
}

static int
oi_iter_next(struct vos_iterator *iter)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	rc = dbtree_iter_next(oiter->oit_hdl);
	if (rc)
		D_GOTO(out, rc);

	rc = oi_iter_match_probe(iter);
 out:
	return rc;
}

static int
oi_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	      daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	daos_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	daos_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching oid info\n");
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;

	it_entry->ie_oid = obj->vo_id;
	it_entry->ie_epoch = obj->vo_punched;
	return 0;
}

static int
oi_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	PMEMobjpool		*pop;
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	pop = vos_cont2pop(oiter->oit_cont);

	TX_BEGIN(pop) {
		rc = dbtree_iter_delete(oiter->oit_hdl, args);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Failed to delete oid entry: %d\n", rc);
	} TX_END

	return rc;
}

struct vos_iter_ops vos_oi_iter_ops = {
	.iop_prepare =	oi_iter_prep,
	.iop_finish  =  oi_iter_fini,
	.iop_probe   =	oi_iter_probe,
	.iop_next    =  oi_iter_next,
	.iop_fetch   =  oi_iter_fetch,
	.iop_delete  =	oi_iter_delete,
};

/**
 * Internal usage APIs
 * For use from container APIs and init APIs
 */
int
vos_obj_tab_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Registering class for OI table Class: %d\n",
		VOS_BTR_OBJ_TABLE);

	rc = dbtree_class_register(VOS_BTR_OBJ_TABLE, 0, &oi_btr_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_obj_tab_create(struct vos_pool *pool, struct vos_obj_table_df *otab_df)
{

	int				rc = 0;
	daos_handle_t			btr_hdl;
	struct btr_root			*oi_root = NULL;

	if (!pool || !otab_df) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	/** Inplace btr_root */
	oi_root = (struct btr_root *) &(otab_df->obt_btr);
	if (!oi_root->tr_class) {
		D_DEBUG(DB_DF, "create OI Tree in-place: %d\n",
			VOS_BTR_OBJ_TABLE);

		rc = dbtree_create_inplace(VOS_BTR_OBJ_TABLE, 0,
					   OT_BTREE_ORDER, &pool->vp_uma,
					   &otab_df->obt_btr, &btr_hdl);
		if (rc)
			D_ERROR("dbtree create failed\n");
		dbtree_close(btr_hdl);
	}
	return rc;
}

int
vos_obj_tab_destroy(struct vos_pool *pool, struct vos_obj_table_df *otab_df)
{
	int				rc = 0;
	daos_handle_t			btr_hdl;

	if (!pool || !otab_df) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	rc = dbtree_open_inplace_ex(&otab_df->obt_btr, &pool->vp_uma,
				    pool->vp_vea_info, &btr_hdl);
	if (rc) {
		D_ERROR("No Object handle, Tree open failed\n");
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

	rc = dbtree_destroy(btr_hdl);
	if (rc)
		D_ERROR("OI BTREE destroy failed\n");
exit:
	return rc;
}
