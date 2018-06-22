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
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>

/** iterator for oid */
struct vos_oid_iter {
	struct vos_iterator	oit_iter;
	/* Handle of iterator */
	daos_handle_t		oit_hdl;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	oit_epr;
	/* Reference to the container */
	struct vos_container	*oit_cont;
};

#define OHKEY_LEN		16

/**
 * Hash key stored in btr_record (total 32 bytes)
 */
struct vos_obj_hkey {
	char			h_oid_hs[OHKEY_LEN];
	daos_epoch_t		h_epc_lo;
	daos_epoch_t		h_epc_hi;
};

/**
 * Key (btree operation parameter) for object table.
 */
struct vos_obj_key {
	daos_unit_oid_t		o_oid;
	daos_epoch_t		o_epc_lo;
	daos_epoch_t		o_epc_hi;
};

static int
obj_df_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct vos_obj_hkey);
}

static void
obj_df_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct vos_obj_key  *okey  = (struct vos_obj_key *)key_iov->iov_buf;
	struct vos_obj_hkey *ohkey = (struct vos_obj_hkey *)hkey;
	uint64_t	    *ohash = (uint64_t *)&ohkey->h_oid_hs[0];
	daos_unit_oid_t	     oid   = okey->o_oid;

	D_ASSERT(key_iov->iov_len == sizeof(struct vos_obj_key));
	ohkey->h_epc_lo = okey->o_epc_lo;
	ohkey->h_epc_hi = okey->o_epc_hi;

	ohash[0] = okey->o_oid.id_pub.lo;
	/* XXX hash collision? */
	ohash[1] = d_hash_murmur64((const unsigned char *)&oid.id_pub.hi,
				    sizeof(oid.id_pub.hi), oid.id_shard);
}

static int
obj_df_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct vos_obj_hkey *hkey1 = (struct vos_obj_hkey *)&rec->rec_hkey[0];
	struct vos_obj_hkey *hkey2 = (struct vos_obj_hkey *)hkey;
	int		     cmprc;

	cmprc = memcmp(hkey1->h_oid_hs, hkey2->h_oid_hs, OHKEY_LEN);
	if (cmprc)
		return dbtree_key_cmp_rc(cmprc);

	if (hkey1->h_epc_lo > hkey2->h_epc_hi)
		return BTR_CMP_GT;

	if (hkey1->h_epc_hi < hkey2->h_epc_lo)
		return BTR_CMP_LT;

	return BTR_CMP_EQ;
}

static int
obj_df_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		 daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_obj_df	 *obj_df;
	struct vos_obj_key	 *obj_key;
	TMMID(struct vos_obj_df)  obj_mmid;

	/* Allocate a PMEM value of type vos_obj_df */
	obj_mmid = umem_znew_typed(&tins->ti_umm, struct vos_obj_df);

	if (TMMID_IS_NULL(obj_mmid))
		return -DER_NOMEM;

	obj_df = umem_id2ptr_typed(&tins->ti_umm, obj_mmid);

	D_ASSERT(key_iov->iov_len == sizeof(struct vos_obj_key));
	obj_key = key_iov->iov_buf;

	obj_df->vo_id = obj_key->o_oid;
	obj_df->vo_epc_lo = obj_key->o_epc_lo;
	obj_df->vo_epc_hi = obj_key->o_epc_hi;

	daos_iov_set(val_iov, obj_df, sizeof(struct vos_obj_df));
	rec->rec_mmid = umem_id_t2u(obj_mmid);

	D_DEBUG(DB_TRACE, "alloc "DF_UOID" rec "UMMID_PF"\n",
		DP_UOID(obj_df->vo_id), UMMID_P(rec->rec_mmid));
	return 0;
}

static int
obj_df_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	 *umm = &tins->ti_umm;
	struct vos_obj_df	 *vobj;
	TMMID(struct vos_obj_df)  obj_mmid;
	int			  rc = 0;

	obj_mmid = umem_id_u2t(rec->rec_mmid, struct vos_obj_df);
	vobj = umem_id2ptr_typed(&tins->ti_umm, obj_mmid);

	/** Free the KV tree within this object */
	if (vobj->vo_tree.tr_class != 0) {
		struct umem_attr uma;
		daos_handle_t	 toh;

		umem_attr_get(&tins->ti_umm, &uma);
		rc = dbtree_open_inplace(&vobj->vo_tree, &uma, &toh);
		if (rc != 0)
			D_ERROR("Failed to open KV tree: %d\n", rc);
		else
			dbtree_destroy(toh);
	}
	umem_free_typed(umm, obj_mmid);
	return rc;
}

static int
obj_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_obj_df	*obj_df;

	D_ASSERT(val_iov != NULL);

	obj_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	daos_iov_set(val_iov, obj_df, sizeof(struct vos_obj_df));
	D_DEBUG(DB_TRACE, "fetch "DF_UOID" rec "UMMID_PF"\n",
		DP_UOID(obj_df->vo_id), UMMID_P(rec->rec_mmid));

	return 0;
}

static int
obj_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key, daos_iov_t *val)
{
	struct vos_obj_hkey *hkey = (struct vos_obj_hkey *)&rec->rec_hkey[0];
	struct vos_obj_key  *okey = (struct vos_obj_key *)key->iov_buf;
	struct vos_obj_df   *obj_df;

	/* NB: this is a tricky function because it actually updates the key
	 * not the value. It's ok because the record position in the btree
	 * is still the same.
	 */
	obj_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

	D_ASSERT(okey->o_epc_hi == okey->o_epc_lo);
	D_ASSERT(hkey->h_epc_hi >= okey->o_epc_lo &&
		 hkey->h_epc_lo <= okey->o_epc_lo);

	/* NB: it is possible obj_df::vo_epc_hi < obj_df::vo_epc_lo, it means
	 * the object has been punched within the same epoch it's created.
	 */
	hkey->h_epc_hi = obj_df->vo_epc_hi = okey->o_epc_lo - 1;
	return 0;
}

static btr_ops_t voi_ops = {
	.to_hkey_size	= obj_df_hkey_size,
	.to_hkey_gen	= obj_df_hkey_gen,
	.to_hkey_cmp	= obj_df_hkey_cmp,
	.to_rec_alloc	= obj_df_rec_alloc,
	.to_rec_free	= obj_df_rec_free,
	.to_rec_fetch	= obj_df_rec_fetch,
	.to_rec_update	= obj_df_rec_update,
};

/**
 * For testing obj index deletion
 */
int
vos_oi_find(struct vos_container *cont, daos_unit_oid_t oid,
	    daos_epoch_t epoch, struct vos_obj_df **obj)
{
	struct vos_obj_key	okey;
	daos_iov_t		key_iov;
	daos_iov_t		val_iov;
	int			rc;

	if (!cont->vc_otab_df) {
		D_ERROR("Object index cannot be empty\n");
		return -DER_NONEXIST;
	}

	okey.o_oid = oid;
	okey.o_epc_lo = okey.o_epc_hi = epoch;
	daos_iov_set(&key_iov, &okey, sizeof(okey));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_lookup(cont->vc_btr_hdl, &key_iov, &val_iov);
	if (rc == 0)
		*obj = val_iov.iov_buf;

	return rc;
}

/**
 * Find the object by OID and return it, or create an object for the oid.
 */
int
vos_oi_find_alloc(struct vos_container *cont, daos_unit_oid_t oid,
		  daos_epoch_t epoch, struct vos_obj_df **obj)
{
	struct vos_obj_key	okey;
	daos_iov_t		key_iov;
	daos_iov_t		val_iov;
	int			rc;

	D_DEBUG(DB_TRACE, "Lookup obj "DF_UOID" in the OI table.\n",
		DP_UOID(oid));

	rc = vos_oi_find(cont, oid, epoch, obj);
	if (rc == 0)
		return rc;

	/* Object ID not found insert it to the OI tree */
	D_DEBUG(DB_TRACE, "Object"DF_UOID" not found adding it..\n",
		DP_UOID(oid));

	okey.o_oid	= oid;
	okey.o_epc_lo	= epoch;
	okey.o_epc_hi	= DAOS_EPOCH_MAX;
	daos_iov_set(&key_iov, &okey, sizeof(okey));
	daos_iov_set(&val_iov, NULL, 0);

	rc = dbtree_update(cont->vc_btr_hdl, &key_iov, &val_iov);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}

	*obj = val_iov.iov_buf;
	return rc;
}

int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, struct vos_obj_df *obj)
{
	struct vos_obj_key	okey;
	daos_iov_t		kiov;
	int			rc;

	D_ASSERT(obj->vo_epc_lo <= epoch && obj->vo_epc_hi >= epoch);

	okey.o_oid	= oid;
	okey.o_epc_lo	= epoch;
	okey.o_epc_hi	= epoch;
	daos_iov_set(&kiov, &okey, sizeof(okey));

	rc = dbtree_update(cont->vc_btr_hdl, &kiov, NULL);
	if (rc != 0)
		D_GOTO(failed, rc);

	D_ASSERT(obj->vo_epc_hi == epoch - 1);
 failed:
	return rc;
}

static struct vos_oid_iter *
iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_oid_iter, oit_iter);
}

static int
oiter_fini(struct vos_iterator *iter)
{
	int			rc  = 0;
	struct vos_oid_iter	*oid_iter = NULL;

	/** iter type should be VOS_ITER_OBJ */
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	oid_iter = iter2oiter(iter);

	if (!daos_handle_is_inval(oid_iter->oit_hdl)) {
		rc = dbtree_iter_finish(oid_iter->oit_hdl);
		if (rc)
			D_ERROR("oid_iter_fini failed:%d\n", rc);
	}

	if (oid_iter->oit_cont != NULL)
		vos_cont_decref(oid_iter->oit_cont);

	D_FREE_PTR(oid_iter);
	return rc;
}

static int
oiter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	   struct vos_iterator **iter_pp)
{
	struct vos_oid_iter	*oid_iter = NULL;
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

	D_ALLOC_PTR(oid_iter);
	if (oid_iter == NULL)
		return -DER_NOMEM;

	oid_iter->oit_epr  = param->ip_epr;
	oid_iter->oit_cont = cont;
	vos_cont_addref(cont);

	rc = dbtree_iter_prepare(cont->vc_btr_hdl, 0, &oid_iter->oit_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &oid_iter->oit_iter;
	return 0;
exit:
	oiter_fini(&oid_iter->oit_iter);
	return rc;
}

/**
 * This function checks if the current object can match the condition, it
 * returns immediately on true, otherwise it will move the iterator cursor
 * to the object matching the condition.
 */
static int
oiter_probe_match(struct vos_iterator *iter)
{
	struct vos_oid_iter	*oiter = iter2oiter(iter);
	daos_epoch_range_t	*epr = &oiter->oit_epr;
	int			 rc;

	if (oiter->oit_epr.epr_lo == 0 &&
	    oiter->oit_epr.epr_hi == DAOS_EPOCH_MAX)
		D_GOTO(out, rc = 0); /* no condition */

	while (1) {
		struct vos_obj_df	*obj_df;
		struct vos_obj_key	 key;
		daos_iov_t		 iov;
		int			 iop;

		daos_iov_set(&iov, NULL, 0);
		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &iov, NULL);
		if (rc != 0)
			D_GOTO(out, rc);

		D_ASSERT(iov.iov_len == sizeof(struct vos_obj_df));
		obj_df = (struct vos_obj_df *)iov.iov_buf;

		iop = IT_OPC_NOOP;
		memset(&key, 0, sizeof(key));
		if (obj_df->vo_epc_hi < epr->epr_lo) {
			/* epoch range of the returned object is lower than the
			 * condition, we should call probe() for the epoch
			 * range condition.
			 */
			iop = IT_OPC_PROBE;
			key.o_epc_lo = epr->epr_lo;
			key.o_epc_hi = epr->epr_hi;

		} else if (obj_df->vo_epc_lo > epr->epr_hi) {
			/* move to the next object... */
			if (obj_df->vo_epc_hi < DAOS_EPOCH_MAX) {
				/* let's find the last version of this object,
				 * then we can use IT_OPC_NEXT to find the next
				 * object. See the "else" below.
				 */
				iop = IT_OPC_PROBE;
				key.o_epc_lo = key.o_epc_hi = DAOS_EPOCH_MAX;
			} else {
				/* This object is not within the epoch range
				 * condition, let's move to the next object.
				 */
				iop = IT_OPC_NEXT;
			}
		}

		switch (iop) {
		case IT_OPC_NOOP: /* match the condition */
			D_GOTO(out, rc = 0);

		case IT_OPC_PROBE:
			key.o_oid = obj_df->vo_id;
			daos_iov_set(&iov, &key, sizeof(key));
			rc = dbtree_iter_probe(oiter->oit_hdl, BTR_PROBE_GE,
					       &iov, NULL);
			break;

		case IT_OPC_NEXT:
			rc = dbtree_iter_next(oiter->oit_hdl);
			break;
		}

		if (rc)
			D_GOTO(out, rc);
	}
out:
	return rc;
}

static int
oiter_probe(struct vos_iterator *iter, daos_hash_out_t *anchor)
{
	struct vos_oid_iter	*oiter = iter2oiter(iter);
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
	rc = oiter_probe_match(iter);
 out:
	return rc;
}

static int
oiter_next(struct vos_iterator *iter)
{
	struct vos_oid_iter	*oid_iter = iter2oiter(iter);
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	rc = dbtree_iter_next(oid_iter->oit_hdl);
	if (rc)
		D_GOTO(out, rc);

	rc = oiter_probe_match(iter);
 out:
	return rc;
}

static int
oiter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	   daos_hash_out_t *anchor)
{
	struct vos_oid_iter	*oid_iter = iter2oiter(iter);
	struct vos_obj_df	*obj_df;
	daos_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	daos_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oid_iter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching oid info\n");
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj_df = (struct vos_obj_df *)rec_iov.iov_buf;

	it_entry->ie_oid = obj_df->vo_id;
	it_entry->ie_epr.epr_lo = obj_df->vo_epc_lo;
	it_entry->ie_epr.epr_hi = obj_df->vo_epc_hi;

	return 0;
}

static int
oiter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_oid_iter	*oiter = iter2oiter(iter);
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

struct vos_iter_ops vos_oid_iter_ops = {
	.iop_prepare =	oiter_prep,
	.iop_finish  =  oiter_fini,
	.iop_probe   =	oiter_probe,
	.iop_next    =  oiter_next,
	.iop_fetch   =  oiter_fetch,
	.iop_delete  =	oiter_delete,
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

	rc = dbtree_class_register(VOS_BTR_OBJ_TABLE, 0, &voi_ops);
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

	rc = dbtree_open_inplace(&otab_df->obt_btr, &pool->vp_uma, &btr_hdl);
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
