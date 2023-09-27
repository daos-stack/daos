/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "vos_internal.h"
#include "vos_ilog.h"
#include "vos_obj.h"
#include <daos_srv/vos.h>

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
	     d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct vos_container	*cont = vos_hdl2cont(tins->ti_coh);
	struct dtx_handle	*dth = vos_dth_get(cont->vc_pool->vp_sysdb);
	struct vos_obj_df	*obj;
	daos_unit_oid_t		*key;
	umem_off_t		 obj_off;
	int			 rc;

	/* Allocate a PMEM value of type vos_obj_df */
	obj_off = umem_zalloc(&tins->ti_umm, sizeof(struct vos_obj_df));
	if (UMOFF_IS_NULL(obj_off))
		return -DER_NOSPACE;

	obj = umem_off2ptr(&tins->ti_umm, obj_off);

	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));
	key = key_iov->iov_buf;

	if (val_out == NULL) {
		obj->vo_id	= *key;
		obj->vo_sync	= 0;
		rc = ilog_create(&tins->ti_umm, &obj->vo_ilog);
		if (rc != 0) {
			D_ERROR("Failure to create incarnation log: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	} else {
		struct vos_obj_df *new_obj = val_out->iov_buf;

		memcpy(obj, new_obj, sizeof(*obj));
		obj->vo_id = *key;
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

struct oi_delete_arg {
	void		*cont;
	uint32_t	only_delete_entry:1;
};

static int
oi_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct vos_obj_df	*obj;
	struct ilog_desc_cbs	 cbs;
	struct oi_delete_arg	*del_arg = args;
	daos_handle_t		 coh = { 0 };
	int			 rc;
	struct vos_pool		*pool;

	obj = umem_off2ptr(umm, rec->rec_off);

	D_ASSERT(tins->ti_priv);
	pool = (struct vos_pool *)tins->ti_priv;
	/* Normally it should delete both ilog and vo_tree, but during upgrade
	 * the new OID (with new layout version) will share the same ilog and
	 * vos_tree with the old OID (with old layout version), so it will only
	 * delete the entry in this case.
	 */
	if (del_arg != NULL && del_arg->only_delete_entry) {
		memset(&obj->vo_ilog, 0, sizeof(obj->vo_ilog));
		memset(&obj->vo_tree, 0, sizeof(obj->vo_tree));
	} else {
		vos_ilog_desc_cbs_init(&cbs, tins->ti_coh);
		rc = ilog_destroy(umm, &cbs, &obj->vo_ilog);
		if (rc != 0) {
			D_ERROR("Failed to destroy incarnation log: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		vos_ilog_ts_evict(&obj->vo_ilog, VOS_TS_TYPE_OBJ, pool->vp_sysdb);
	}


	if (del_arg != NULL)
		coh = vos_cont2hdl((struct vos_container *)del_arg->cont);
	return gc_add_item(tins->ti_priv, coh, GC_OBJ, rec->rec_off, 0);
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
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static umem_off_t
oi_node_alloc(struct btr_instance *tins, int size)
{
	return umem_zalloc(&tins->ti_umm, size);
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
	struct dtx_handle	*dth = vos_dth_get(cont->vc_pool->vp_sysdb);
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
			   &key_iov, &val_iov, NULL);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}
	obj = val_iov.iov_buf;
	/** Since we just allocated it, we can save a tx_add later to set this */
	obj->vo_max_write = epoch;

	vos_ilog_ts_ignore(vos_cont2umm(cont), &obj->vo_ilog);
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
vos_oi_delete(struct vos_container *cont, daos_unit_oid_t oid, bool only_delete_entry)
{
	struct oi_delete_arg arg;
	d_iov_t		key_iov;
	int		rc = 0;

	D_DEBUG(DB_TRACE, "Delete obj "DF_UOID"\n", DP_UOID(oid));

	arg.cont = cont;
	if (only_delete_entry)
		arg.only_delete_entry = 1;
	else
		arg.only_delete_entry = 0;
	d_iov_set(&key_iov, &oid, sizeof(oid));
	rc = dbtree_delete(cont->vc_btr_hdl, BTR_PROBE_EQ, &key_iov, &arg);
	if (rc == -DER_NONEXIST)
		return 0;

	if (rc != 0) {
		D_ERROR("Failed to delete object, "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	return 0;
}

int
vos_oi_upgrade_layout_ver(struct vos_container *cont, daos_unit_oid_t oid,
			  uint32_t layout_ver)
{
	d_iov_t		  key_iov;
	d_iov_t		  val_iov;
	int		  rc;

	if (oid.id_layout_ver == layout_ver)
		return 0;

	/* NB: This is only used by object layout version upgrade. During upgrade,
	 * it does not need to recreate the ilog or vos_tree for the new OI entry,
	 * i.e. the new layout OI entry and old OI entry will share the same ilog
	 * and vos_tree.
	 *
	 * So this function will fetch the OI entry, then use @val_out by dbtree_upsert(),
	 * so oi_rec_alloc() will not allocate new ilog and vos_tree.
	 *
	 * The old OI_entry will be deleted by oi_rec_free(), which will not delete ilog
	 * and vos_tree in this case.
	 **/
	d_iov_set(&key_iov, &oid, sizeof(oid));
	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_fetch(cont->vc_btr_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key_iov, NULL, &val_iov);
	if (rc == -DER_NONEXIST)
		return 0;
	if (rc) {
		D_ERROR("dbtree fetch "DF_UOID": %d\n", DP_UOID(oid), rc);
		return rc;
	}

	d_iov_set(&key_iov, &oid, sizeof(oid));
	oid.id_layout_ver = layout_ver;
	rc = dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			   &key_iov, &val_iov, &val_iov);
	if (rc)
		D_ERROR("dbtree upsert "DF_UOID": %d\n", DP_UOID(oid), rc);

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

	if (daos_handle_is_valid(oiter->oit_hdl)) {
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
			    oiter->oit_epr.epr_hi, oiter->oit_iter.it_bound, false,
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
	info->ii_filter_cb = iter->it_filter_cb;
	info->ii_filter_arg = iter->it_filter_arg;

	return 0;
}

static int
oi_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	     struct vos_iterator **iter_pp, struct vos_ts_set *ts_set)
{
	struct vos_oi_iter	*oiter = NULL;
	struct vos_container	*cont = NULL;
	struct dtx_handle	*dth;
	int			rc = 0;

	if (type != VOS_ITER_OBJ) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_OBJ, type);
		return -DER_INVAL;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont == NULL)
		return -DER_INVAL;

	dth = vos_dth_get(cont->vc_pool->vp_sysdb);

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

	oiter->oit_iter.it_filter_cb = param->ip_filter_cb;
	oiter->oit_iter.it_filter_arg = param->ip_filter_arg;
	oiter->oit_flags = param->ip_flags;
	if (param->ip_flags & VOS_IT_FOR_PURGE)
		oiter->oit_iter.it_for_purge = 1;
	if (param->ip_flags & VOS_IT_FOR_DISCARD)
		oiter->oit_iter.it_for_discard = 1;
	if (param->ip_flags & VOS_IT_FOR_MIGRATION)
		oiter->oit_iter.it_for_migration = 1;
	if (cont->vc_pool->vp_sysdb)
		oiter->oit_iter.it_for_sysdb = 1;

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
oi_iter_match_probe(struct vos_iterator *iter, daos_anchor_t *anchor, uint32_t flags)
{
	uint64_t		 start_seq;
	struct vos_oi_iter	*oiter	= iter2oiter(iter);
	struct dtx_handle	*dth;
	char			*str	= NULL;
	vos_iter_desc_t		 desc;
	uint64_t		 feats;
	unsigned int		 acts;
	int			 rc;
	bool			 is_sysdb = !!iter->it_for_sysdb;

	while (1) {
		struct vos_obj_df *obj;
		d_iov_t	   iov;

		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &iov, anchor);
		if (rc != 0) {
			str = "fetch";
			goto failed;
		}

		D_ASSERT(iov.iov_len == sizeof(struct vos_obj_df));
		obj = (struct vos_obj_df *)iov.iov_buf;

		if (iter->it_filter_cb != NULL && (flags & VOS_ITER_PROBE_AGAIN) == 0) {
			desc.id_type = VOS_ITER_OBJ;
			desc.id_oid = obj->vo_id;
			desc.id_parent_punch = 0;

			feats = dbtree_feats_get(&obj->vo_tree);

			if (!vos_feats_agg_time_get(feats, &desc.id_agg_write)) {
				/* Upgrading case, set it to latest known epoch */
				if (obj->vo_max_write == 0)
					vos_ilog_last_update(&obj->vo_ilog, VOS_TS_TYPE_OBJ,
							     &desc.id_agg_write,
							     !!iter->it_for_sysdb);
				else
					desc.id_agg_write = obj->vo_max_write;
			}
			acts = 0;
			start_seq = vos_sched_seq(is_sysdb);
			dth = vos_dth_get(is_sysdb);
			vos_dth_set(NULL, is_sysdb);
			rc = iter->it_filter_cb(vos_iter2hdl(iter), &desc, iter->it_filter_arg,
						&acts);
			vos_dth_set(dth, is_sysdb);
			if (rc != 0)
				goto failed;
			if (start_seq != vos_sched_seq(is_sysdb))
				acts |= VOS_ITER_CB_YIELD;
			if (acts & (VOS_ITER_CB_EXIT | VOS_ITER_CB_ABORT | VOS_ITER_CB_RESTART |
				    VOS_ITER_CB_DELETE | VOS_ITER_CB_YIELD))
				return acts;
			if (acts & VOS_ITER_CB_SKIP)
				goto next;
		}

		rc = oi_iter_ilog_check(obj, oiter, NULL, true);
		if (rc == 0)
			break;
		if (rc != -DER_NONEXIST) {
			str = "ilog check";
			goto failed;
		}
next:
		flags = 0;
		rc = dbtree_iter_next(oiter->oit_hdl);
		if (rc != 0) {
			str = "next";
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
oi_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor, uint32_t flags)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	dbtree_probe_opc_t	 next_opc;
	dbtree_probe_opc_t	 opc;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	next_opc = (flags & VOS_ITER_PROBE_NEXT) ? BTR_PROBE_GT : BTR_PROBE_GE;
	opc = vos_anchor_is_zero(anchor) ? BTR_PROBE_FIRST : next_opc;
	rc = dbtree_iter_probe(oiter->oit_hdl, opc, vos_iter_intent(iter), NULL,
			       anchor);
	if (rc)
		D_GOTO(out, rc);

	/* NB: these probe cannot guarantee the returned entry is within
	 * the condition epoch range.
	 */
	rc = oi_iter_match_probe(iter, anchor, flags);
 out:
	return rc;
}

static int
oi_iter_next(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	rc = dbtree_iter_next(oiter->oit_hdl);
	if (rc)
		D_GOTO(out, rc);

	rc = oi_iter_match_probe(iter, anchor, 0);
 out:
	return rc;
}

static int
oi_iter_fill(struct vos_obj_df *obj, struct vos_oi_iter *oiter, bool check_existence,
	     vos_iter_entry_t *ent)
{
	daos_epoch_range_t	epr = {0, DAOS_EPOCH_MAX};
	int			rc;

	rc = oi_iter_ilog_check(obj, oiter, &epr, check_existence);
	if (rc != 0)
		return rc;

	ent->ie_oid = obj->vo_id;
	ent->ie_punch = oiter->oit_ilog_info.ii_next_punch;
	ent->ie_obj_punch = ent->ie_punch;
	ent->ie_epoch = epr.epr_hi;
	ent->ie_vis_flags = VOS_VIS_FLAG_VISIBLE;
	if (oiter->oit_ilog_info.ii_create == 0) {
		/** Object isn't visible so mark covered */
		ent->ie_vis_flags = VOS_VIS_FLAG_COVERED;
	}
	ent->ie_child_type = VOS_ITER_DKEY;

	/* Upgrading case, set it to latest known epoch */
	if (obj->vo_max_write == 0)
		vos_ilog_last_update(&obj->vo_ilog, VOS_TS_TYPE_OBJ,
				     &ent->ie_last_update, oiter->oit_iter.it_for_sysdb);
	else
		ent->ie_last_update = obj->vo_max_write;

	return 0;
}

static int
oi_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	      daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	d_iov_t			 rec_iov;
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

	return oi_iter_fill(rec_iov.iov_buf, oiter, false, it_entry);
}

static int
oi_iter_process(struct vos_iterator *iter, vos_iter_proc_op_t op, void *args)
{
	struct oi_delete_arg	del_arg;
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	if (op != VOS_ITER_PROC_OP_DELETE)
		return -DER_NOSYS;

	del_arg.cont = args;
	del_arg.only_delete_entry = 0;
	rc = umem_tx_begin(vos_cont2umm(oiter->oit_cont), NULL);
	if (rc != 0)
		goto exit;

	rc = dbtree_iter_delete(oiter->oit_hdl, &del_arg);

	rc = umem_tx_end(vos_cont2umm(oiter->oit_cont), rc);

	if (rc != 0)
		D_ERROR("Failed to delete oid entry: "DF_RC"\n", DP_RC(rc));
exit:
	return rc;
}

int
oi_iter_check_punch(daos_handle_t ih)
{
	struct vos_iterator	*iter = vos_hdl2iter(ih);
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	struct oi_delete_arg	del_arg;
	daos_unit_oid_t		 oid;
	d_iov_t			 rec_iov;
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

	if (!vos_ilog_is_punched(vos_cont2hdl(oiter->oit_cont), &obj->vo_ilog, &oiter->oit_epr,
				 NULL, &oiter->oit_ilog_info))
		return 0;

	/** Ok, ilog is fully punched, so we can move it to gc heap */
	rc = umem_tx_begin(vos_cont2umm(oiter->oit_cont), NULL);
	if (rc != 0)
		goto exit;

	/* Incarnation log is empty, delete the object */
	D_DEBUG(DB_IO, "Moving object "DF_UOID" to gc heap\n",
		DP_UOID(oid));
	/* Evict the object from cache */
	rc = vos_obj_evict_by_oid(vos_obj_cache_current(oiter->oit_cont->vc_pool->vp_sysdb),
				  oiter->oit_cont, oid);
	if (rc != 0)
		D_ERROR("Could not evict object "DF_UOID" "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
	del_arg.cont = oiter->oit_cont;
	del_arg.only_delete_entry = 0;
	rc = dbtree_iter_delete(oiter->oit_hdl, &del_arg);
	D_ASSERT(rc != -DER_NONEXIST);

	rc = umem_tx_end(vos_cont2umm(oiter->oit_cont), rc);
exit:
	if (rc == 0)
		return 1;

	return rc;
}

int
oi_iter_aggregate(daos_handle_t ih, bool range_discard)
{
	struct vos_iterator	*iter = vos_hdl2iter(ih);
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	daos_unit_oid_t		 oid;
	d_iov_t			 rec_iov;
	bool			 delete = false, invisible = false;
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
				&oiter->oit_epr, iter->it_for_discard, false, NULL,
				&oiter->oit_ilog_info);
	if (rc == 1) {
		/* Incarnation log is empty, delete the object */
		D_DEBUG(DB_IO, "Removing object "DF_UOID" from tree\n",
			DP_UOID(oid));
		delete = true;

		/* XXX: The dkey tree may be not empty because related prepared transaction can
		 *	be aborted. Then it will be added and handled via GC when oi_rec_free().
		 */

		/* Evict the object from cache */
		rc = vos_obj_evict_by_oid(vos_obj_cache_current(oiter->oit_cont->vc_pool->vp_sysdb),
					  oiter->oit_cont, oid);
		if (rc != 0)
			D_ERROR("Could not evict object "DF_UOID" "DF_RC"\n",
				DP_UOID(oid), DP_RC(rc));
		rc = dbtree_iter_delete(oiter->oit_hdl, NULL);
		D_ASSERT(rc != -DER_NONEXIST);
	} else if (rc == -DER_NONEXIST) {
		/** ilog isn't visible in range but still has some entries */
		invisible = true;
		rc = 0;
	}

	rc = umem_tx_end(vos_cont2umm(oiter->oit_cont), rc);
exit:
	if (rc == 0 && (delete || invisible))
		return delete ? 1 : 2;

	return rc;
}

struct vos_iter_ops vos_oi_iter_ops = {
	.iop_prepare		= oi_iter_prep,
	.iop_nested_tree_fetch	= oi_iter_nested_tree_fetch,
	.iop_finish		= oi_iter_fini,
	.iop_probe		= oi_iter_probe,
	.iop_next		= oi_iter_next,
	.iop_fetch		= oi_iter_fetch,
	.iop_process		= oi_iter_process,
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
