/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include <vos_ilog.h>
#include <vos_obj.h>

/** iterator for oid */
struct vos_oi_iter {
	/** embedded VOS common iterator */
	struct vos_iterator	oit_iter;
	/** Handle of iterator */
	daos_handle_t		oit_hdl;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	oit_epr;
	/** Reference to the container */
	struct vos_container	*oit_cont;
	/** Incarnation log entries for current entry */
	struct vos_ilog_info	 oit_ilog_info;
	/** punched epoch for current entry */
	daos_epoch_t		 oit_punched;
	/** cached iterator flags */
	uint32_t		 oit_flags;
};

static int
oi_hkey_size(void)
{
	return sizeof(daos_unit_oid_t);
}

static int
oi_rec_msize(int alloc_overhead)
{
	return alloc_overhead + sizeof(struct vos_obj_df);
}

static void
oi_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));

	memcpy(hkey, key_iov->iov_buf, sizeof(daos_unit_oid_t));
}

static int
oi_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	daos_unit_oid_t	*oid1 = (daos_unit_oid_t *)&rec->rec_hkey[0];
	daos_unit_oid_t	*oid2 = (daos_unit_oid_t *)hkey;

	return dbtree_key_cmp_rc(memcmp(oid1, oid2, sizeof(*oid1)));
}

static int
oi_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	     d_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_handle	*dth = vos_dth_get();
	struct vos_obj_df	*obj;
	daos_unit_oid_t		*key;
	umem_off_t		 obj_off;
	int			 rc;

	/* Allocate a PMEM value of type vos_obj_df */
	obj_off = vos_slab_alloc(&tins->ti_umm, sizeof(struct vos_obj_df),
				 VOS_SLAB_OBJ_DF);
	if (UMOFF_IS_NULL(obj_off))
		return -DER_NOSPACE;

	obj = umem_off2ptr(&tins->ti_umm, obj_off);

	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));
	key = key_iov->iov_buf;

	obj->vo_sync	= 0;
	obj->vo_id	= *key;
	rc = ilog_create(&tins->ti_umm, &obj->vo_ilog);
	if (rc != 0) {
		D_ERROR("Failure to create incarnation log: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	d_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	rec->rec_off = obj_off;

	/* For new created object, commit it synchronously to reduce
	 * potential conflict with subsequent modifications against
	 * the same object.
	 */
	if (dtx_is_valid_handle(dth))
		dth->dth_sync = 1;

	D_DEBUG(DB_TRACE, "alloc "DF_UOID" rec "DF_X64"\n",
		DP_UOID(obj->vo_id), rec->rec_off);
	return 0;
}

static int
oi_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct vos_obj_df	*obj;
	struct ilog_desc_cbs	 cbs;
	int			 rc;

	obj = umem_off2ptr(umm, rec->rec_off);

	vos_ilog_desc_cbs_init(&cbs, tins->ti_coh);
	rc = ilog_destroy(umm, &cbs, &obj->vo_ilog);
	if (rc != 0) {
		D_ERROR("Failed to destroy incarnation log: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	vos_ilog_ts_evict(&obj->vo_ilog, VOS_TS_TYPE_OBJ);

	D_ASSERT(tins->ti_priv);
	return gc_add_item((struct vos_pool *)tins->ti_priv, GC_OBJ,
			   rec->rec_off, 0);
}

static int
oi_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_obj_df	*obj;

	obj = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	D_DEBUG(DB_TRACE, "fetch "DF_UOID" rec "DF_X64"\n",
		DP_UOID(obj->vo_id), rec->rec_off);

	D_ASSERT(val_iov != NULL);
	d_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	return 0;
}

static int
oi_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static umem_off_t
oi_node_alloc(struct btr_instance *tins, int size)
{
	return vos_slab_alloc(&tins->ti_umm, size, VOS_SLAB_OBJ_NODE);
}

static btr_ops_t oi_btr_ops = {
	.to_rec_msize		= oi_rec_msize,
	.to_hkey_size		= oi_hkey_size,
	.to_hkey_gen		= oi_hkey_gen,
	.to_hkey_cmp		= oi_hkey_cmp,
	.to_rec_alloc		= oi_rec_alloc,
	.to_rec_free		= oi_rec_free,
	.to_rec_fetch		= oi_rec_fetch,
	.to_rec_update		= oi_rec_update,
	.to_node_alloc		= oi_node_alloc,
};

/**
 * Locate a durable object in OI table.
 */
int
vos_oi_find(struct vos_container *cont, daos_unit_oid_t oid,
	    struct vos_obj_df **obj_p, struct vos_ts_set *ts_set)
{
	struct ilog_df		*ilog = NULL;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	int			 rc;
	int			 tmprc;

	*obj_p = NULL;
	d_iov_set(&key_iov, &oid, sizeof(oid));
	d_iov_set(&val_iov, NULL, 0);

	rc = dbtree_fetch(cont->vc_btr_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key_iov, NULL, &val_iov);
	if (rc == 0) {
		struct vos_obj_df *obj = val_iov.iov_buf;

		D_ASSERT(daos_unit_obj_id_equal(obj->vo_id, oid));
		*obj_p = obj;
		ilog = &obj->vo_ilog;
	}

	tmprc = vos_ilog_ts_add(ts_set, ilog, &oid, sizeof(oid));

	D_ASSERT(tmprc == 0); /* Non-zero return for akey only */

	return rc;
}

/**
 * Locate a durable object in OI table, or create it if it's not found
 */
int
vos_oi_find_alloc(struct vos_container *cont, daos_unit_oid_t oid,
		  daos_epoch_t epoch, bool log, struct vos_obj_df **obj_p,
		  struct vos_ts_set *ts_set)
{
	struct dtx_handle	*dth = vos_dth_get();
	struct vos_obj_df	*obj = NULL;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	daos_handle_t		 loh;
	struct ilog_desc_cbs	 cbs;
	int			 rc;

	D_DEBUG(DB_TRACE, "Lookup obj "DF_UOID" in the OI table.\n",
		DP_UOID(oid));

	rc = vos_oi_find(cont, oid, &obj, ts_set);
	if (rc == 0)
		goto do_log;
	if (rc != -DER_NONEXIST)
		return rc;

	/* Object ID not found insert it to the OI tree */
	D_DEBUG(DB_TRACE, "Object "DF_UOID" not found adding it..\n",
		DP_UOID(oid));

	d_iov_set(&val_iov, NULL, 0);
	d_iov_set(&key_iov, &oid, sizeof(oid));

	rc = dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			   &key_iov, &val_iov);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}
	obj = val_iov.iov_buf;

	vos_ilog_ts_mark(ts_set, &obj->vo_ilog);
do_log:
	if (!log)
		goto skip_log;
	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
	rc = ilog_open(vos_cont2umm(cont), &obj->vo_ilog, &cbs, &loh);
	if (rc != 0)
		return rc;

	rc = ilog_update(loh, NULL, epoch,
			 dtx_is_valid_handle(dth) ? dth->dth_op_seq : 1, false);

	ilog_close(loh);
skip_log:
	if (rc == 0)
		*obj_p = obj;

	return rc;
}

/**
 * Punch a durable object, it will generate a new incarnation with the same
 * ID in OI table.
 */
int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, daos_epoch_t bound, uint64_t flags,
	     struct vos_obj_df *obj, struct vos_ilog_info *info,
	     struct vos_ts_set *ts_set)
{
	daos_epoch_range_t	 epr = {0, epoch};
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "Punch obj "DF_UOID", epoch="DF_U64".\n",
		DP_UOID(oid), epoch);

	rc = vos_ilog_punch(cont, &obj->vo_ilog, &epr, bound, NULL,
			    info, ts_set, true,
			    (flags & VOS_OF_REPLAY_PC) != 0);

	if (rc == 0 && vos_ts_set_check_conflict(ts_set, epoch))
		rc = -DER_TX_RESTART;

	VOS_TX_LOG_FAIL(rc, "Failed to update incarnation log entry: "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

/**
 * Delete a durable object
 *
 * NB: this operation is not going to be part of distributed transaction,
 * it is only used by rebalance and reintegration.
 *
 * XXX: this only deletes the latest incarnation of the object, but the
 * ongoing work (incarnation log) will change it and there will be only
 * one incarnation for each object.
 */
int
vos_oi_delete(struct vos_container *cont, daos_unit_oid_t oid)
{
	d_iov_t		key_iov;
	int		rc = 0;

	D_DEBUG(DB_TRACE, "Delete obj "DF_UOID"\n", DP_UOID(oid));

	d_iov_set(&key_iov, &oid, sizeof(oid));

	rc = dbtree_delete(cont->vc_btr_hdl, BTR_PROBE_EQ, &key_iov,
			   cont->vc_pool);
	if (rc == -DER_NONEXIST)
		return 0;

	if (rc != 0) {
		D_ERROR("Failed to delete object, "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	return 0;
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
			D_ERROR("oid_iter_fini failed:"DF_RC"\n", DP_RC(rc));
	}

	if (oiter->oit_cont != NULL)
		vos_cont_decref(oiter->oit_cont);

	vos_ilog_fetch_finish(&oiter->oit_ilog_info);
	D_FREE(oiter);
	return rc;
}

static int
oi_iter_ilog_check(struct vos_obj_df *obj, struct vos_oi_iter *oiter,
		   daos_epoch_range_t *epr, bool check_existence)
{
	struct umem_instance	*umm;
	int			 rc;

	umm = vos_cont2umm(oiter->oit_cont);
	rc = vos_ilog_fetch(umm, vos_cont2hdl(oiter->oit_cont),
			    vos_iter_intent(&oiter->oit_iter), &obj->vo_ilog,
			    oiter->oit_epr.epr_hi, oiter->oit_iter.it_bound,
			    NULL, NULL, &oiter->oit_ilog_info);
	if (rc != 0)
		goto out;

	if (oiter->oit_ilog_info.ii_uncertain_create)
		D_GOTO(out, rc = -DER_TX_RESTART);

	rc = vos_ilog_check(&oiter->oit_ilog_info, &oiter->oit_epr, epr,
			    (oiter->oit_flags & VOS_IT_PUNCHED) == 0);
out:
	D_ASSERTF(check_existence || rc != -DER_NONEXIST,
		  "Probe is required before fetch\n");
	return rc;
}


static int
oi_iter_nested_tree_fetch(struct vos_iterator *iter, vos_iter_type_t type,
			  struct vos_iter_info *info)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	d_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	if (type != VOS_ITER_DKEY) {
		D_DEBUG(DB_TRACE, "Expected VOS_ITER_DKEY nested iterator type,"
			" got %d\n", type);
		return -DER_INVAL;
	}

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, NULL);
	if (rc != 0) {
		D_ERROR("Error while fetching oid info: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;

	rc = oi_iter_ilog_check(obj, oiter, &info->ii_epr, false);
	if (rc != 0)
		return rc;

	info->ii_oid = obj->vo_id;
	info->ii_punched = oiter->oit_ilog_info.ii_prior_punch;
	info->ii_hdl = vos_cont2hdl(oiter->oit_cont);

	return 0;
}

static int
oi_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	     struct vos_iterator **iter_pp, struct vos_ts_set *ts_set)
{
	struct vos_oi_iter	*oiter = NULL;
	struct vos_container	*cont = NULL;
	struct dtx_handle	*dth = vos_dth_get();
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

	rc = vos_ts_set_add(ts_set, cont->vc_ts_idx, NULL, 0);
	D_ASSERT(rc == 0);

	vos_ilog_fetch_init(&oiter->oit_ilog_info);
	oiter->oit_iter.it_type = type;
	oiter->oit_epr  = param->ip_epr;
	oiter->oit_cont = cont;
	if (dtx_is_valid_handle(dth))
		oiter->oit_iter.it_bound = MAX(dth->dth_epoch,
					       dth->dth_epoch_bound);
	else
		oiter->oit_iter.it_bound = param->ip_epr.epr_hi;
	vos_cont_addref(cont);

	oiter->oit_flags = param->ip_flags;
	if (param->ip_flags & VOS_IT_FOR_PURGE)
		oiter->oit_iter.it_for_purge = 1;
	if (param->ip_flags & VOS_IT_FOR_MIGRATION)
		oiter->oit_iter.it_for_migration = 1;

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
	char			*str	= NULL;

	int			 rc;

	while (1) {
		struct vos_obj_df *obj;
		int		   probe;
		d_iov_t	   iov;

		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &iov, NULL);
		if (rc != 0) {
			str = "fetch";
			goto failed;
		}

		D_ASSERT(iov.iov_len == sizeof(struct vos_obj_df));
		obj = (struct vos_obj_df *)iov.iov_buf;

		rc = oi_iter_ilog_check(obj, oiter, NULL, true);
		if (rc == 0)
			break;
		if (rc != -DER_NONEXIST) {
			str = "ilog check";
			goto failed;
		}
		probe = BTR_PROBE_GT;

		d_iov_set(&iov, &obj->vo_id, sizeof(obj->vo_id));
		rc = dbtree_iter_probe(oiter->oit_hdl, probe,
				       vos_iter_intent(iter), &iov, NULL);
		if (rc != 0) {
			str = "probe";
			goto failed;
		}
	}
	return 0;
 failed:
	if (rc == -DER_NONEXIST) /* Non-existence isn't a failure */
		return rc;

	VOS_TX_LOG_FAIL(rc, "iterator %s failed, rc="DF_RC"\n", str, DP_RC(rc));

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
	rc = dbtree_iter_probe(oiter->oit_hdl, opc, vos_iter_intent(iter), NULL,
			       anchor);
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
	daos_epoch_range_t	 epr;
	d_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		if (rc == -DER_INPROGRESS)
			D_DEBUG(DB_TRACE, "Cannot fetch oid info because of "
				"conflict modification: "DF_RC"\n", DP_RC(rc));
		else
			D_ERROR("Error while fetching oid info: "DF_RC"\n",
				DP_RC(rc));
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;

	rc = oi_iter_ilog_check(obj, oiter, &epr, false);
	if (rc != 0)
		return rc;

	it_entry->ie_oid = obj->vo_id;
	it_entry->ie_punch = oiter->oit_ilog_info.ii_next_punch;
	it_entry->ie_epoch = epr.epr_hi;
	it_entry->ie_vis_flags = VOS_VIS_FLAG_VISIBLE;
	if (oiter->oit_ilog_info.ii_create == 0) {
		/** Object isn't visible so mark covered */
		it_entry->ie_vis_flags = VOS_VIS_FLAG_COVERED;
	}
	it_entry->ie_child_type = VOS_ITER_DKEY;
	return 0;
}

static int
oi_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	rc = umem_tx_begin(vos_cont2umm(oiter->oit_cont), NULL);
	if (rc != 0)
		goto exit;

	rc = dbtree_iter_delete(oiter->oit_hdl, args);

	rc = umem_tx_end(vos_cont2umm(oiter->oit_cont), rc);

	if (rc != 0)
		D_ERROR("Failed to delete oid entry: "DF_RC"\n", DP_RC(rc));
exit:
	return rc;
}

int
oi_iter_aggregate(daos_handle_t ih, bool discard)
{
	struct vos_iterator	*iter = vos_hdl2iter(ih);
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	daos_unit_oid_t		 oid;
	d_iov_t			 rec_iov;
	bool			 reprobe = false;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, NULL);
	D_ASSERTF(rc != -DER_NONEXIST,
		  "Probe should be done before aggregation\n");
	if (rc != 0)
		return rc;
	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;
	oid = obj->vo_id;

	rc = umem_tx_begin(vos_cont2umm(oiter->oit_cont), NULL);
	if (rc != 0)
		goto exit;

	rc = vos_ilog_aggregate(vos_cont2hdl(oiter->oit_cont), &obj->vo_ilog,
				&oiter->oit_epr, discard, 0,
				&oiter->oit_ilog_info);
	if (rc == 1) {
		/* Incarnation log is empty, delete the object */
		D_DEBUG(DB_IO, "Removing object "DF_UOID" from tree\n",
			DP_UOID(oid));
		reprobe = true;
		if (!dbtree_is_empty_inplace(&obj->vo_tree)) {
			/* This can be an assert once we have sane under punch
			 * detection.
			 */
			D_ERROR("Removing orphaned dkey tree\n");
		}
		/* Evict the object from cache */
		rc = vos_obj_evict_by_oid(vos_obj_cache_current(),
					  oiter->oit_cont, oid);
		if (rc != 0)
			D_ERROR("Could not evict object "DF_UOID" "DF_RC"\n",
				DP_UOID(oid), DP_RC(rc));
		rc = dbtree_iter_delete(oiter->oit_hdl, NULL);
		D_ASSERT(rc != -DER_NONEXIST);
	} else if (rc == -DER_NONEXIST) {
		/** ilog isn't visible in range but still has some enrtries */
		reprobe = true;
		rc = 0;
	}

	rc = umem_tx_end(vos_cont2umm(oiter->oit_cont), rc);
exit:
	if (rc == 0 && reprobe)
		return 1;

	return rc;
}

struct vos_iter_ops vos_oi_iter_ops = {
	.iop_prepare		= oi_iter_prep,
	.iop_nested_tree_fetch	= oi_iter_nested_tree_fetch,
	.iop_finish		= oi_iter_fini,
	.iop_probe		= oi_iter_probe,
	.iop_next		= oi_iter_next,
	.iop_fetch		= oi_iter_fetch,
	.iop_delete		= oi_iter_delete,
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
