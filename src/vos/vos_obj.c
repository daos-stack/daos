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
 * This file is part of daos
 *
 * vos/vos_obj.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

/** Ensure the values of recx flags map to those exported by evtree */
D_CASSERT((uint32_t)VOS_VIS_FLAG_UNKNOWN == (uint32_t)EVT_UNKNOWN);
D_CASSERT((uint32_t)VOS_VIS_FLAG_COVERED == (uint32_t)EVT_COVERED);
D_CASSERT((uint32_t)VOS_VIS_FLAG_VISIBLE == (uint32_t)EVT_VISIBLE);
D_CASSERT((uint32_t)VOS_VIS_FLAG_PARTIAL == (uint32_t)EVT_PARTIAL);
D_CASSERT((uint32_t)VOS_VIS_FLAG_LAST == (uint32_t)EVT_LAST);

/** This callback is invoked only if the tree is not empty */
static int
empty_tree_check(daos_handle_t ih, vos_iter_entry_t *entry,
		 vos_iter_type_t type, vos_iter_param_t *param, void *cb_arg,
		 unsigned int *acts)
{
	bool	*empty = cb_arg;

	*empty = false;

	return 1; /* Return positive number to break iteration */
}

static int
tree_is_empty(struct vos_object *obj, daos_handle_t toh,
	      const daos_epoch_range_t *epr, vos_iter_type_t type)
{
	struct dtx_handle	*dth = vos_dth_get();
	bool			 empty = true;
	int			 rc;

	/** First ignore any uncommitted entries because they only matter
	 *  when there are no committed entries
	 */
	rc = vos_iterate_key(obj, toh, type, epr, true, empty_tree_check,
			     &empty, dth);

	if (rc < 0)
		return rc;

	if (!empty)
		return 0;

	/** Ok, there are no committed entries.  We need to run the iterator
	 *  on more time to ensure there are no uncommitted entries.  If there
	 *  are, this will return -DER_INPROGRESS.
	 */
	rc = vos_iterate_key(obj, toh, type, epr, false, empty_tree_check,
			     &empty, dth);

	if (rc < 0)
		return rc;

	/** The tree is empty */
	return 1;
}

static int
vos_propagate_check(struct vos_object *obj, daos_handle_t toh,
		    struct vos_ts_set *ts_set, const daos_epoch_range_t *epr,
		    vos_iter_type_t type)
{
	const char	*tree_name = NULL;
	uint64_t	 punch_flag = VOS_OF_PUNCH_PROPAGATE;
	int		 rc = 0;
	uint32_t	 read_flag = 0;
	uint32_t	 write_flag = 0;

	if (vos_ts_set_check_conflict(ts_set, epr->epr_hi)) {
		D_DEBUG(DB_IO, "Failed to punch key: "DF_RC"\n",
			DP_RC(-DER_TX_RESTART));
		return -DER_TX_RESTART;
	}

	switch (type) {
	case VOS_ITER_DKEY:
		read_flag = VOS_TS_READ_OBJ;
		write_flag = VOS_TS_WRITE_OBJ;
		tree_name = "DKEY";
		/** DAOS-4698.  Don't do emptiness check on dkey or propagate
		 *  to object until this rebuild bug is fixed.
		 */
		return 0;
	case VOS_ITER_AKEY:
		read_flag = VOS_TS_READ_DKEY;
		write_flag = VOS_TS_WRITE_DKEY;
		tree_name = "AKEY";
		break;
	default:
		D_ASSERT(0);
	}

	/** The check for propagation needs to update the read
	 *  time on the object as it's iterating the dkey tree
	 */
	vos_ts_set_append_cflags(ts_set, read_flag);

	rc = tree_is_empty(obj, toh, epr, type);
	if (rc > 0) {
		/** tree is now empty, set the flags so we can punch
		 *  the parent
		 */
		D_DEBUG(DB_TRACE, "%s tree empty, punching parent\n",
			tree_name);
		vos_ts_set_append_vflags(ts_set, punch_flag);
		vos_ts_set_append_cflags(ts_set, write_flag);

		return 1;
	}

	VOS_TX_LOG_FAIL(rc, "Could not check emptiness on punch: " DF_RC"\n",
			DP_RC(rc));

	return rc;
}

/**
 * @} vos_tree_helper
 */
static int
key_punch(struct vos_object *obj, daos_epoch_t epoch, daos_epoch_t bound,
	  uint32_t pm_ver, daos_key_t *dkey, unsigned int akey_nr,
	  daos_key_t *akeys, uint64_t flags, struct vos_ts_set *ts_set)
{
	struct vos_krec_df	*krec;
	struct vos_rec_bundle	 rbund;
	struct dcs_csum_info	 csum;
	struct vos_ilog_info	 obj_info = {0};
	struct vos_ilog_info	 dkey_info = {0};
	struct vos_ilog_info	 akey_info = {0};
	daos_epoch_range_t	 epr = {0, epoch};
	d_iov_t			 riov;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i;
	int			 rc;

	vos_ilog_fetch_init(&obj_info);
	vos_ilog_fetch_init(&dkey_info);
	vos_ilog_fetch_init(&akey_info);
	rc = obj_tree_init(obj);
	if (rc)
		D_GOTO(out, rc);

	rc = vos_ilog_punch(obj->obj_cont, &obj->obj_df->vo_ilog, &epr, bound,
			    NULL, &obj_info, ts_set, false, false);
	if (rc)
		D_GOTO(out, rc);

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_off	= UMOFF_NULL;
	rbund.rb_ver	= pm_ver;
	rbund.rb_csum	= &csum;
	ci_set_null(&csum);

	if (!akeys)
		goto punch_dkey;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY,
			      dkey, SUBTR_CREATE, DAOS_INTENT_PUNCH,
			      &krec, &toh, ts_set);
	if (rc) {
		D_ERROR("Error preparing dkey: rc="DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = vos_ilog_punch(obj->obj_cont, &krec->kr_ilog, &epr, bound,
			    &obj_info, &dkey_info, ts_set, false,
			    false);
	if (rc)
		D_GOTO(out, rc);

	/* We do not need to add an incarnation log entry in parent tree
	 * on punch.   If the subtree has nothing but punches, no need
	 * to track that.  If it has updates, the parent tree will have
	 * updates
	 */
	rbund.rb_tclass	= VOS_BTR_AKEY;
	for (i = 0; i < akey_nr; i++) {
		rbund.rb_iov = &akeys[i];
		rc = key_tree_punch(obj, toh, epoch, bound, &akeys[i], &riov,
				    flags, ts_set, &dkey_info,
				    &akey_info);
		if (rc != 0) {
			VOS_TX_LOG_FAIL(rc, "Failed to punch akey: rc="
					DF_RC"\n", DP_RC(rc));
			break;
		}
	}

	if (rc == 0 && (flags & VOS_OF_REPLAY_PC) == 0) {
		/** Check if we need to propagate the punch */
		rc = vos_propagate_check(obj, toh, ts_set, &epr,
					 VOS_ITER_AKEY);
	}

	if (rc != 1)
		goto out;
	/** else propagate the punch */

punch_dkey:
	rbund.rb_iov = dkey;
	rbund.rb_tclass	= VOS_BTR_DKEY;

	rc = key_tree_punch(obj, obj->obj_toh, epoch, bound, dkey, &riov,
			    flags, ts_set, &obj_info, &dkey_info);
	if (rc != 0)
		D_GOTO(out, rc);

	if (rc == 0 && (flags & VOS_OF_REPLAY_PC) == 0) {
		/** Check if we need to propagate to object */
		rc = vos_propagate_check(obj, obj->obj_toh, ts_set,
					 &epr, VOS_ITER_DKEY);
	}
 out:
	vos_ilog_fetch_finish(&obj_info);
	vos_ilog_fetch_finish(&dkey_info);
	vos_ilog_fetch_finish(&akey_info);

	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, 0);

	return rc;
}

static int
obj_punch(daos_handle_t coh, struct vos_object *obj, daos_epoch_t epoch,
	  daos_epoch_t bound, uint64_t flags, struct vos_ts_set *ts_set)
{
	struct daos_lru_cache	*occ  = vos_obj_cache_current();
	struct vos_container	*cont;
	struct vos_ilog_info	 info;
	int			 rc;

	vos_ilog_fetch_init(&info);
	cont = vos_hdl2cont(coh);
	rc = vos_oi_punch(cont, obj->obj_id, epoch, bound, flags, obj->obj_df,
			  &info, ts_set);
	if (rc)
		D_GOTO(failed, rc);

	/* evict it from cache, because future fetch should only see empty
	 * object (without obj_df)
	 */
	vos_obj_evict(occ, obj);
failed:
	vos_ilog_fetch_finish(&info);
	return rc;
}

/** If the object/key doesn't exist, we should augment the set with any missing
 *  entries
 */
static void
vos_punch_add_missing(struct vos_ts_set *ts_set, daos_key_t *dkey, int akey_nr,
		      daos_key_t *akeys)
{
	struct vos_akey_data	ad;

	ad.ad_is_iod = false;
	ad.ad_keys = akeys;

	vos_ts_add_missing(ts_set, dkey, akey_nr, &ad);
}

/**
 * Punch an object, or punch a dkey, or punch an array of akeys.
 */
int
vos_obj_punch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
	      unsigned int akey_nr, daos_key_t *akeys, struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_ts_set	*ts_set;
	struct vos_container	*cont;
	struct vos_object	*obj = NULL;
	bool			 punch_obj = false;
	uint64_t		 hold_flags;
	daos_epoch_range_t	 epr = { 0 };
	daos_epoch_t		 bound;
	int			 rc = 0;
	uint64_t		 cflags = 0;

	if (oid.id_shard % 3 == 1 && DAOS_FAIL_CHECK(DAOS_DTX_FAIL_IO))
		return -DER_IO;

	if (dtx_is_valid_handle(dth)) {
		epr.epr_hi = dth->dth_epoch;
		bound = MAX(dth->dth_epoch_bound, dth->dth_epoch);
	} else {
		epr.epr_hi = epoch;
		bound = epoch;
	}

	D_DEBUG(DB_IO, "Punch "DF_UOID", epoch "DF_U64"\n",
		DP_UOID(oid), epr.epr_hi);

	vos_dth_set(dth);
	cont = vos_hdl2cont(coh);

	if (dtx_is_valid_handle(dth)) {
		if (akey_nr) {
			cflags = VOS_TS_WRITE_AKEY;
			if (flags & VOS_OF_COND_PUNCH)
				cflags |= VOS_TS_READ_AKEY;
		} else if (dkey != NULL) {
			cflags = VOS_TS_WRITE_DKEY;
			if (flags & VOS_OF_COND_PUNCH)
				cflags |= VOS_TS_READ_DKEY;
		} else {
			cflags = VOS_TS_WRITE_OBJ;
			if (flags & VOS_OF_COND_PUNCH)
				cflags |= VOS_TS_READ_OBJ;
		}

	}

	rc = vos_ts_set_allocate(&ts_set, flags, cflags, akey_nr,
				 dtx_is_valid_handle(dth) ?
				 &dth->dth_xid : NULL);
	if (rc != 0)
		goto reset;

	rc = vos_ts_set_add(ts_set, cont->vc_ts_idx, NULL, 0);
	if (rc != 0)
		goto reset;

	rc = vos_tx_begin(dth, vos_cont2umm(cont));
	if (rc != 0)
		goto reset;

	/* Commit the CoS DTXs via the PUNCH PMDK transaction. */
	if (dtx_is_valid_handle(dth) && dth->dth_dti_cos_count > 0) {
		D_ALLOC_ARRAY(daes, dth->dth_dti_cos_count);
		if (daes == NULL)
			D_GOTO(abort, rc = -DER_NOMEM);

		rc = vos_dtx_commit_internal(cont, dth->dth_dti_cos,
					     dth->dth_dti_cos_count,
					     0, NULL, daes);
		if (rc <= 0)
			D_FREE(daes);
	}

	hold_flags = (flags & VOS_OF_COND_PUNCH) ? 0 : VOS_OBJ_CREATE;
	hold_flags |= VOS_OBJ_VISIBLE;
	/* NB: punch always generate a new incarnation of the object */
	rc = vos_obj_hold(vos_obj_cache_current(), vos_hdl2cont(coh), oid, &epr,
			  bound, hold_flags, DAOS_INTENT_PUNCH, &obj, ts_set);
	if (rc == 0) {
		if (dkey) { /* key punch */
			rc = key_punch(obj, epr.epr_hi, bound, pm_ver, dkey,
				       akey_nr, akeys, flags, ts_set);

			if (rc > 0)
				punch_obj = true;
		} else {
			punch_obj = true;
		}

		if (punch_obj)
			rc = obj_punch(coh, obj, epr.epr_hi, bound, flags,
				       ts_set);
	}
abort:
	rc = vos_tx_end(cont, dth, NULL, NULL, true, rc);

	if (obj != NULL)
		vos_obj_release(vos_obj_cache_current(), obj, rc != 0);
reset:
	if (rc != 0)
		D_DEBUG(DB_IO, "Failed to punch object "DF_UOID": rc = %d\n",
			DP_UOID(oid), rc);
	else if (daes != NULL)
		vos_dtx_post_handle(cont, daes, dth->dth_dti_cos_count, false);

	D_FREE(daes);

	vos_dth_set(NULL);

	if (rc == 0)
		vos_ts_set_upgrade(ts_set);

	if (rc == -DER_NONEXIST || rc == 0) {
		if (vos_ts_wcheck(ts_set, epr.epr_hi, bound)) {
			rc = -DER_TX_RESTART;
		} else {
			vos_punch_add_missing(ts_set, dkey, akey_nr, akeys);
			vos_ts_set_update(ts_set, epr.epr_hi);
			if (rc == 0)
				vos_ts_set_wupdate(ts_set, epr.epr_hi);
		}
	}

	vos_ts_set_free(ts_set);

	return rc;
}

int
vos_obj_delete(daos_handle_t coh, daos_unit_oid_t oid)
{
	struct daos_lru_cache	*occ  = vos_obj_cache_current();
	struct vos_container	*cont = vos_hdl2cont(coh);
	struct umem_instance	*umm = vos_cont2umm(cont);
	struct vos_object	*obj;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	int			 rc;

	rc = vos_obj_hold(occ, cont, oid, &epr, 0, VOS_OBJ_VISIBLE,
			  DAOS_INTENT_KILL, &obj, NULL);
	if (rc == -DER_NONEXIST)
		return 0;

	if (rc) {
		D_ERROR("Failed to hold object: %s\n", d_errstr(rc));
		return rc;
	}

	rc = umem_tx_begin(umm, NULL);
	if (rc)
		goto out;

	rc = vos_oi_delete(cont, obj->obj_id);
	if (rc)
		D_ERROR("Failed to delete object: %s\n", d_errstr(rc));

	rc = umem_tx_end(umm, rc);
	if (rc)
		goto out;

	/* NB: noop for full-stack mode */
	gc_wait();
out:
	vos_obj_release(occ, obj, true);
	return rc;
}

static int
key_iter_ilog_check(struct vos_krec_df *krec, struct vos_obj_iter *oiter,
		    vos_iter_type_t type, daos_epoch_range_t *epr,
		    bool check_existence, struct vos_ts_set *ts_set)
{
	struct umem_instance	*umm;
	int			 rc;

	umm = vos_obj2umm(oiter->it_obj);
	rc = vos_ilog_fetch(umm, vos_cont2hdl(oiter->it_obj->obj_cont),
			    vos_iter_intent(&oiter->it_iter), &krec->kr_ilog,
			    oiter->it_epr.epr_hi, oiter->it_iter.it_bound,
			    &oiter->it_punched, NULL, &oiter->it_ilog_info);

	if (rc != 0)
		goto out;

	if (vos_has_uncertainty(ts_set, &oiter->it_ilog_info,
				oiter->it_epr.epr_hi, oiter->it_iter.it_bound))
		D_GOTO(out, rc = -DER_TX_RESTART);

	rc = vos_ilog_check(&oiter->it_ilog_info, &oiter->it_epr, epr,
			    (oiter->it_flags & VOS_IT_PUNCHED) == 0);

out:
	D_ASSERTF(check_existence || rc != -DER_NONEXIST,
		  "Probe is required before fetch\n");
	return rc;
}

static int
key_ilog_prepare(struct vos_obj_iter *oiter, daos_handle_t toh, int key_type,
		 daos_key_t *key, int flags, daos_handle_t *sub_toh,
		 daos_epoch_range_t *epr, struct vos_punch_record *punched,
		 struct vos_ilog_info *info, struct vos_ts_set *ts_set)
{
	struct vos_krec_df	*krec = NULL;
	struct vos_object	*obj = oiter->it_obj;
	int			 rc;

	rc = key_tree_prepare(obj, toh, key_type, key, flags,
			      vos_iter_intent(&oiter->it_iter), &krec,
			      sub_toh, ts_set);
	if (rc == -DER_NONEXIST)
		return rc;

	if (rc != 0) {
		D_ERROR("Cannot load the prepare key tree: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	/* Update the lower bound for nested iterator */
	rc = key_iter_ilog_check(krec, oiter, key_type, epr, true, ts_set);
	if (rc != 0)
		goto fail;

	if (punched && vos_epc_punched(punched->pr_epc, punched->pr_minor_epc,
				       &info->ii_prior_punch))
		*punched = info->ii_prior_punch;

	return 0;
fail:
	if (sub_toh)
		key_tree_release(*sub_toh, flags & SUBTR_EVT);
	return rc;
}

/**
 * @defgroup vos_obj_iters VOS object iterators
 * @{
 *
 * - iterate d-key
 * - iterate a-key (array)
 * - iterate recx
 */
static int
key_iter_fetch_helper(struct vos_obj_iter *oiter, struct vos_rec_bundle *rbund,
		      d_iov_t *keybuf, daos_anchor_t *anchor)
{
	d_iov_t			 kiov;
	d_iov_t			 riov;
	struct dcs_csum_info	 csum;

	tree_rec_bundle2iov(rbund, &riov);

	rbund->rb_iov	= keybuf;
	rbund->rb_csum	= &csum;

	d_iov_set(rbund->rb_iov, NULL, 0); /* no copy */
	ci_set_null(rbund->rb_csum);

	return dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
}

static int
key_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *ent,
	       daos_anchor_t *anchor, bool check_existence)
{

	struct vos_krec_df	*krec;
	struct vos_rec_bundle	 rbund;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	int			 rc;

	rc = key_iter_fetch_helper(oiter, &rbund, &ent->ie_key, anchor);
	D_ASSERTF(check_existence || rc != -DER_NONEXIST,
		  "Iterator should probe before fetch\n");
	if (rc != 0)
		return rc;

	D_ASSERT(rbund.rb_krec);
	if (oiter->it_iter.it_type == VOS_ITER_AKEY) {
		if (rbund.rb_krec->kr_bmap & KREC_BF_EVT) {
			ent->ie_child_type = VOS_ITER_RECX;
		} else if (rbund.rb_krec->kr_bmap & KREC_BF_BTR) {
			ent->ie_child_type = VOS_ITER_SINGLE;
		} else {
			ent->ie_child_type = VOS_ITER_NONE;
		}
	} else {
		ent->ie_child_type = VOS_ITER_AKEY;
	}

	krec = rbund.rb_krec;

	rc = key_iter_ilog_check(krec, oiter, oiter->it_iter.it_type, &epr,
				 check_existence, NULL);
	if (rc == -DER_NONEXIST)
		return IT_OPC_NEXT;
	if (rc != 0)
		return rc;

	ent->ie_epoch = epr.epr_hi;
	ent->ie_punch = oiter->it_ilog_info.ii_next_punch;
	ent->ie_vis_flags = VOS_VIS_FLAG_VISIBLE;
	if (oiter->it_ilog_info.ii_create == 0) {
		/* The key has no visible subtrees so mark it covered */
		ent->ie_vis_flags = VOS_VIS_FLAG_COVERED;
	}

	return 0;
}

static int
key_iter_fetch_root(struct vos_obj_iter *oiter, vos_iter_type_t type,
		    struct vos_iter_info *info)
{
	struct vos_object	*obj = oiter->it_obj;
	struct vos_krec_df	*krec;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 keybuf;
	int			 rc;

	rc = key_iter_fetch_helper(oiter, &rbund, &keybuf, NULL);

	if (rc != 0) {
		D_ERROR("Could not fetch key: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	krec = rbund.rb_krec;
	info->ii_vea_info = obj->obj_cont->vc_pool->vp_vea_info;
	info->ii_uma = vos_obj2uma(obj);

	info->ii_epr = oiter->it_epr;
	info->ii_punched = oiter->it_punched;
	/* Update the lower bound for nested iterator */
	rc = key_iter_ilog_check(krec, oiter, type, &info->ii_epr, false, NULL);
	if (rc != 0)
		return rc;

	if (vos_epc_punched(info->ii_punched.pr_epc,
			    info->ii_punched.pr_minor_epc,
			    &oiter->it_ilog_info.ii_prior_punch))
		info->ii_punched = oiter->it_ilog_info.ii_prior_punch;

	if (type == VOS_ITER_RECX) {
		if ((krec->kr_bmap & KREC_BF_EVT) == 0)
			return -DER_NONEXIST;
		info->ii_evt = &krec->kr_evt;
	} else {
		if ((krec->kr_bmap & KREC_BF_BTR) == 0)
			return -DER_NONEXIST;
		info->ii_btr = &krec->kr_btr;
	}

	return 0;
}

static int
key_iter_copy(struct vos_obj_iter *oiter, vos_iter_entry_t *ent,
	      d_iov_t *iov_out)
{
	if (ent->ie_key.iov_len > iov_out->iov_buf_len)
		return -DER_OVERFLOW;

	D_ASSERT(ent->ie_key.iov_buf != NULL);
	D_ASSERT(iov_out->iov_buf != NULL);

	memcpy(iov_out->iov_buf, ent->ie_key.iov_buf, ent->ie_key.iov_len);
	iov_out->iov_len = ent->ie_key.iov_len;
	return 0;
}

/**
 * Check if the current entry can match the iterator condition, this function
 * returns IT_OPC_NOOP for true, returns IT_OPC_NEXT or IT_OPC_PROBE if further
 * operation is required. If IT_OPC_PROBE is returned, then the key to be
 * probed and its epoch range are also returned to @ent.
 */
static int
key_iter_match(struct vos_obj_iter *oiter, vos_iter_entry_t *ent)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_epoch_range_t	*epr = &oiter->it_epr;
	struct vos_ilog_info	 info;
	daos_handle_t		 toh;
	int			 rc;

	rc = key_iter_fetch(oiter, ent, NULL, true);
	if (rc != 0) {
		VOS_TX_TRACE_FAIL(rc, "Failed to fetch the entry: "DF_RC"\n",
				  DP_RC(rc));
		return rc;
	}

	if ((oiter->it_iter.it_type == VOS_ITER_AKEY) ||
	    (oiter->it_akey.iov_buf == NULL)) /* dkey w/o akey as condition */
		return IT_OPC_NOOP;

	/* else: has akey as condition */
	if (epr->epr_lo != epr->epr_hi || (oiter->it_flags & VOS_IT_PUNCHED)) {
		D_ERROR("Cannot support epoch range for conditional iteration "
			"because it is not clearly defined.\n");
		return -DER_INVAL; /* XXX simplify it for now */
	}

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY,
			      &ent->ie_key, 0, vos_iter_intent(&oiter->it_iter),
			      NULL, &toh, NULL);
	if (rc != 0) {
		D_DEBUG(DB_IO, "can't load the akey tree: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	vos_ilog_fetch_init(&info);
	rc = key_ilog_prepare(oiter, toh, VOS_BTR_AKEY, &oiter->it_akey, 0,
			      NULL, NULL, NULL, &info, NULL);
	if (rc == 0)
		rc = IT_OPC_NOOP;

	if (rc == -DER_NONEXIST)
		rc = IT_OPC_NEXT;

	vos_ilog_fetch_finish(&info);
	key_tree_release(toh, false);

	return rc;
}

/**
 * Check if the current item can match the provided condition (with the
 * giving a-key). If the item can't match the condition, this function
 * traverses the tree until a matched item is found.
 */
static int
key_iter_match_probe(struct vos_obj_iter *oiter)
{
	int	rc;

	while (1) {
		vos_iter_entry_t	entry;

		rc = key_iter_match(oiter, &entry);
		switch (rc) {
		default:
			D_ASSERT(rc < 0);
			VOS_TX_TRACE_FAIL(rc, "match failed, rc="DF_RC"\n",
					  DP_RC(rc));
			goto out;

		case IT_OPC_NOOP:
			/* already match the condition, no further operation */
			rc = 0;
			goto out;

		case IT_OPC_NEXT:
			/* move to the next tree record */
			rc = dbtree_iter_next(oiter->it_hdl);
			if (rc)
				goto out;
			break;
		}
	}
 out:
	return rc;
}

static int
key_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	int	rc;

	rc = dbtree_iter_probe(oiter->it_hdl,
			       anchor ? BTR_PROBE_GE : BTR_PROBE_FIRST,
			       vos_iter_intent(&oiter->it_iter),
			       NULL, anchor);
	if (rc)
		D_GOTO(out, rc);

	rc = key_iter_match_probe(oiter);
	if (rc)
		D_GOTO(out, rc);
 out:
	return rc;
}

static int
key_iter_next(struct vos_obj_iter *oiter)
{
	int	rc;

	rc = dbtree_iter_next(oiter->it_hdl);
	if (rc)
		D_GOTO(out, rc);

	rc = key_iter_match_probe(oiter);
	if (rc)
		D_GOTO(out, rc);
out:
	return rc;
}

/**
 * Iterator for the d-key tree.
 */
static int
dkey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *akey)
{
	int	rc;

	oiter->it_akey = *akey;

	rc = dbtree_iter_prepare(oiter->it_obj->obj_toh, 0, &oiter->it_hdl);

	return rc;
}

/**
 * Iterator for the akey tree.
 */
static int
akey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  struct vos_ts_set *ts_set)
{
	daos_handle_t		 toh;
	int			 rc;

	rc = key_ilog_prepare(oiter, oiter->it_obj->obj_toh, VOS_BTR_DKEY, dkey,
			      0, &toh, &oiter->it_epr, &oiter->it_punched,
			      &oiter->it_ilog_info, ts_set);
	if (rc != 0)
		goto failed;

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	key_tree_release(toh, false);

	if (rc == 0)
		return 0;

failed:
	VOS_TX_LOG_FAIL(rc, "Could not prepare akey iterator "DF_RC"\n",
			DP_RC(rc));
	return rc;
}

static int
prepare_key_from_toh(struct vos_obj_iter *oiter, daos_handle_t toh)
{
	return dbtree_iter_prepare(toh, 0, &oiter->it_hdl);
}

/**
 * Record extent (recx) iterator
 */

/**
 * Record extent (recx) iterator
 */
static int singv_iter_fetch(struct vos_obj_iter *oiter,
			   vos_iter_entry_t *it_entry,
			   daos_anchor_t *anchor);
/**
 * Prepare the iterator for the recx tree.
 */
static int
singv_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		   daos_key_t *akey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 ak_toh;
	daos_handle_t		 sv_toh;
	int			 rc;

	rc = key_ilog_prepare(oiter, obj->obj_toh, VOS_BTR_DKEY, dkey, 0,
			      &ak_toh, &oiter->it_epr, &oiter->it_punched,
			      &oiter->it_ilog_info, NULL);
	if (rc != 0)
		return rc;

	rc = key_ilog_prepare(oiter, ak_toh, VOS_BTR_AKEY, akey, 0, &sv_toh,
			      &oiter->it_epr, &oiter->it_punched,
			      &oiter->it_ilog_info, NULL);
	if (rc != 0)
		D_GOTO(failed_1, rc);

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(sv_toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0)
		D_DEBUG(DB_IO, "Cannot prepare singv iterator: "DF_RC"\n",
			DP_RC(rc));
	key_tree_release(sv_toh, false);
 failed_1:
	key_tree_release(ak_toh, false);
	return rc;
}

/**
 * Probe the single value based on @opc and conditions in @entry (epoch),
 * return the matched one to @entry.
 */
static int
singv_iter_probe_fetch(struct vos_obj_iter *oiter, dbtree_probe_opc_t opc,
		       vos_iter_entry_t *entry)
{
	struct vos_svt_key	key;
	d_iov_t			kiov;
	int			rc;

	d_iov_set(&kiov, &key, sizeof(key));
	key.sk_epoch = entry->ie_epoch;
	key.sk_minor_epc = entry->ie_minor_epc;

	rc = dbtree_iter_probe(oiter->it_hdl, opc,
			       vos_iter_intent(&oiter->it_iter), &kiov, NULL);
	if (rc != 0)
		return rc;

	memset(entry, 0, sizeof(*entry));
	rc = singv_iter_fetch(oiter, entry, NULL);
	return rc;
}

/**
 * Find the data that was written before/in the specified epoch of @oiter
 * for the recx in @entry. If this recx has no data for this epoch, then
 * this function will move on to the next recx and repeat this process.
 */
static int
singv_iter_probe_epr(struct vos_obj_iter *oiter, vos_iter_entry_t *entry)
{
	daos_epoch_range_t *epr = &oiter->it_epr;

	while (1) {
		int	opc;
		int	rc;

		switch (oiter->it_epc_expr) {
		default:
			return -DER_INVAL;

		case VOS_IT_EPC_EQ:
			if (entry->ie_epoch > epr->epr_hi)
				return -DER_NONEXIST;

			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_EQ;
				break;
			}
			return 0;

		case VOS_IT_EPC_RE:
			if (entry->ie_epoch > epr->epr_hi)
				return -DER_NONEXIST;

			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_GE;
				break;
			}
			return 0;

		case VOS_IT_EPC_RR:
			if (entry->ie_epoch < epr->epr_lo)
				return -DER_NONEXIST; /* end of story */

			if (entry->ie_epoch > epr->epr_hi) {
				entry->ie_epoch = epr->epr_hi;
				opc = BTR_PROBE_LE;
				break;
			}
			return 0;

		case VOS_IT_EPC_GE:
			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_GE;
				break;
			}
			return 0;

		case VOS_IT_EPC_LE:
			if (entry->ie_epoch > epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_LE;
				break;
			}
			return 0;
		}
		rc = singv_iter_probe_fetch(oiter, opc, entry);
		if (rc != 0)
			return rc;
	}
}

static int
singv_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	vos_iter_entry_t	entry;
	daos_anchor_t		tmp = {0};
	int			opc;
	int			rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		opc = anchor == NULL ? BTR_PROBE_LAST : BTR_PROBE_LE;
	else
		opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;

	rc = dbtree_iter_probe(oiter->it_hdl, opc,
			       vos_iter_intent(&oiter->it_iter), NULL, anchor);
	if (rc != 0)
		return rc;

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, &tmp);
	if (rc != 0)
		return rc;

	if (anchor != NULL) {
		if (memcmp(anchor, &tmp, sizeof(tmp)) == 0)
			return 0;

		D_DEBUG(DB_IO, "Can't find the provided anchor\n");
		/**
		 * the original recx has been merged/discarded, so we need to
		 * call singv_iter_probe_epr() and check if the current record
		 * can match the condition.
		 */
	}
	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

static int
singv_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		 daos_anchor_t *anchor)
{
	struct vos_svt_key	key;
	struct vos_rec_bundle	rbund;
	d_iov_t			kiov;
	d_iov_t			riov;
	int			rc;

	d_iov_set(&kiov, &key, sizeof(key));

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_biov	= &it_entry->ie_biov;
	rbund.rb_csum	= &it_entry->ie_csum;

	memset(&it_entry->ie_biov, 0, sizeof(it_entry->ie_biov));
	ci_set_null(rbund.rb_csum);

	rc = dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
	if (rc)
		D_GOTO(out, rc);

	it_entry->ie_vis_flags = VOS_VIS_FLAG_VISIBLE;
	it_entry->ie_epoch	 = key.sk_epoch;
	it_entry->ie_minor_epc	 = key.sk_minor_epc;
	if (vos_epc_punched(it_entry->ie_epoch, it_entry->ie_minor_epc,
			    &oiter->it_punched)) {
		/* entry is covered by a key/object punch */
		it_entry->ie_vis_flags = VOS_VIS_FLAG_COVERED;
	}
	it_entry->ie_rsize	 = rbund.rb_rsize;
	it_entry->ie_gsize	 = rbund.rb_gsize;
	it_entry->ie_ver	 = rbund.rb_ver;
	it_entry->ie_recx.rx_idx = 0;
	it_entry->ie_recx.rx_nr  = 1;
 out:
	return rc;
}

static int
singv_iter_next(struct vos_obj_iter *oiter)
{
	vos_iter_entry_t entry;
	int		 rc;
	int		 opc;

	/* Only one SV rec is visible for the given @epoch,
	 * so return -DER_NONEXIST directly for the next().
	 */
	if (oiter->it_flags & VOS_IT_RECX_VISIBLE &&
	    !(oiter->it_flags & VOS_IT_RECX_COVERED)) {
		D_ASSERT(oiter->it_epc_expr == VOS_IT_EPC_RR);
		return -DER_NONEXIST;
	}

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, NULL);
	if (rc != 0)
		return rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RE)
		entry.ie_epoch += 1;
	else if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		entry.ie_epoch -= 1;
	else
		entry.ie_epoch = DAOS_EPOCH_MAX;

	opc = (oiter->it_epc_expr == VOS_IT_EPC_RR) ?
		BTR_PROBE_LE : BTR_PROBE_GE;

	rc = singv_iter_probe_fetch(oiter, opc, &entry);
	if (rc != 0)
		return rc;

	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

#define recx_flags_set(flags, setting)	\
	(((flags) & (setting)) == (setting))

static uint32_t
recx_get_flags(struct vos_obj_iter *oiter)
{
	uint32_t options = EVT_ITER_EMBEDDED;

	if (recx_flags_set(oiter->it_flags,
			   VOS_IT_RECX_VISIBLE | VOS_IT_RECX_SKIP_HOLES)) {
		options |= EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES;
		D_ASSERT(!recx_flags_set(oiter->it_flags, VOS_IT_RECX_COVERED));
		goto done;
	}
	D_ASSERT(!recx_flags_set(oiter->it_flags, VOS_IT_RECX_SKIP_HOLES));
	if (oiter->it_flags & VOS_IT_RECX_VISIBLE)
		options |= EVT_ITER_VISIBLE;
	if (oiter->it_flags & VOS_IT_RECX_COVERED)
		options |= EVT_ITER_COVERED;

done:
	if (oiter->it_flags & VOS_IT_RECX_REVERSE)
		options |= EVT_ITER_REVERSE;
	if (oiter->it_flags & VOS_IT_FOR_PURGE)
		options |= EVT_ITER_FOR_PURGE;
	if (oiter->it_flags & VOS_IT_FOR_MIGRATION)
		options |= EVT_ITER_FOR_MIGRATION;
	return options;
}

/**
 * Sets the range filter.
 */
static inline void
recx2filter(struct evt_filter *filter, daos_recx_t *recx)
{
	if (recx->rx_nr == 0) {
		filter->fr_ex.ex_lo = 0ULL;
		filter->fr_ex.ex_hi = ~(0ULL);
	} else {
		filter->fr_ex.ex_lo = recx->rx_idx;
		filter->fr_ex.ex_hi = recx->rx_idx + recx->rx_nr - 1;
	}
}

/**
 * Prepare the iterator for the recx tree.
 */
static int
recx_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  daos_key_t *akey, struct vos_ts_set *ts_set)
{
	struct vos_object	*obj = oiter->it_obj;
	struct evt_filter	 filter = {0};
	daos_handle_t		 ak_toh;
	daos_handle_t		 rx_toh;
	int			 rc;
	uint32_t		 options;

	rc = key_ilog_prepare(oiter, obj->obj_toh, VOS_BTR_DKEY, dkey, 0,
			      &ak_toh, &oiter->it_epr, &oiter->it_punched,
			      &oiter->it_ilog_info, ts_set);
	if (rc != 0)
		return rc;

	rc = key_ilog_prepare(oiter, ak_toh, VOS_BTR_AKEY, akey, SUBTR_EVT,
			      &rx_toh, &oiter->it_epr, &oiter->it_punched,
			      &oiter->it_ilog_info, ts_set);
	if (rc != 0)
		D_GOTO(failed, rc);

	recx2filter(&filter, &oiter->it_recx);
	filter.fr_epr.epr_lo = oiter->it_epr.epr_lo;
	filter.fr_epr.epr_hi = oiter->it_iter.it_bound;
	filter.fr_epoch = oiter->it_epr.epr_hi;
	filter.fr_punch_epc = oiter->it_punched.pr_epc;
	filter.fr_punch_minor_epc = oiter->it_punched.pr_minor_epc;
	options = recx_get_flags(oiter);
	rc = evt_iter_prepare(rx_toh, options, &filter,
			      &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Cannot prepare recx iterator : "DF_RC"\n",
			DP_RC(rc));
	}
	key_tree_release(rx_toh, true);
 failed:
	key_tree_release(ak_toh, false);
	return rc;
}
static int
recx_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	int	opc;
	int	rc;

	opc = anchor ? EVT_ITER_FIND : EVT_ITER_FIRST;
	rc = evt_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	return rc;
}

static int
recx_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_anchor_t *anchor)
{
	struct evt_extent	*ext;
	struct evt_entry	 entry;
	int			 rc;
	unsigned int		 inob;

	rc = evt_iter_fetch(oiter->it_hdl, &inob, &entry, anchor);
	if (rc != 0)
		D_GOTO(out, rc);

	memset(it_entry, 0, sizeof(*it_entry));

	ext = &entry.en_sel_ext;
	it_entry->ie_epoch	 = entry.en_epoch;
	it_entry->ie_minor_epc	 = entry.en_minor_epc;
	it_entry->ie_recx.rx_idx = ext->ex_lo;
	it_entry->ie_recx.rx_nr	 = evt_extent_width(ext);
	ext = &entry.en_ext;
	/* Also export the original extent and the visibility flags */
	it_entry->ie_orig_recx.rx_idx = ext->ex_lo;
	it_entry->ie_orig_recx.rx_nr	 = evt_extent_width(ext);
	it_entry->ie_vis_flags = entry.en_visibility;
	it_entry->ie_rsize	= inob;
	it_entry->ie_ver	= entry.en_ver;
	it_entry->ie_csum	= entry.en_csum;
	bio_iov_set(&it_entry->ie_biov, entry.en_addr,
		    it_entry->ie_recx.rx_nr * it_entry->ie_rsize);
 out:
	return rc;
}

static int
recx_iter_copy(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
	       d_iov_t *iov_out)
{
	struct bio_io_context	*bioc;
	struct bio_iov		*biov = &it_entry->ie_biov;

	D_ASSERT(bio_iov2buf(biov) == NULL);
	D_ASSERT(iov_out->iov_buf != NULL);

	/* Skip copy and return success for a punched record */
	if (bio_addr_is_hole(&biov->bi_addr))
		return 0;
	else if (iov_out->iov_buf_len < bio_iov2len(biov))
		return -DER_OVERFLOW;

	/*
	 * Set 'iov_len' beforehand, cause it will be used as copy
	 * size in bio_read().
	 */
	iov_out->iov_len = bio_iov2len(biov);
	bioc = oiter->it_obj->obj_cont->vc_pool->vp_io_ctxt;
	D_ASSERT(bioc != NULL);

	return bio_read(bioc, biov->bi_addr, iov_out);
}

static int
recx_iter_next(struct vos_obj_iter *oiter)
{
	return evt_iter_next(oiter->it_hdl);
}

static int
recx_iter_fini(struct vos_obj_iter *oiter)
{
	return evt_iter_finish(oiter->it_hdl);
}

/**
 * common functions for iterator.
 */
static int vos_obj_iter_fini(struct vos_iterator *vitr);

/** prepare an object content iterator */
int
vos_obj_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		  struct vos_iterator **iter_pp,
		  struct vos_ts_set *ts_set)
{
	struct vos_obj_iter	*oiter;
	struct vos_container	*cont;
	struct dtx_handle	*dth = vos_dth_get();
	daos_epoch_t		 bound;
	int			 rc;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	bound = dtx_is_valid_handle(dth) ? dth->dth_epoch_bound :
		param->ip_epr.epr_hi;
	oiter->it_iter.it_bound = MAX(bound, param->ip_epr.epr_hi);
	vos_ilog_fetch_init(&oiter->it_ilog_info);
	oiter->it_iter.it_type = type;
	oiter->it_epr = param->ip_epr;
	oiter->it_epc_expr = param->ip_epc_expr;

	oiter->it_flags = param->ip_flags;
	oiter->it_recx = param->ip_recx;
	if (param->ip_flags & VOS_IT_FOR_PURGE)
		oiter->it_iter.it_for_purge = 1;
	if (param->ip_flags & VOS_IT_FOR_MIGRATION)
		oiter->it_iter.it_for_migration = 1;
	if (param->ip_flags == VOS_IT_KEY_TREE) {
		/** Prepare the iterator from an already open tree handle.   See
		 *  vos_iterate_key
		 */
		D_ASSERT(type == VOS_ITER_DKEY || type == VOS_ITER_AKEY);
		oiter->it_obj = param->ip_dkey.iov_buf;
		rc = prepare_key_from_toh(oiter, param->ip_hdl);
		goto done;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	rc = vos_ts_set_add(ts_set, cont->vc_ts_idx, NULL, 0);
	D_ASSERT(rc == 0);

	/* XXX the condition epoch ranges could cover multiple versions of
	 * the object/key if it's punched more than once. However, rebuild
	 * system should guarantee this will never happen.
	 */
	rc = vos_obj_hold(vos_obj_cache_current(), cont,
			  param->ip_oid, &oiter->it_epr,
			  oiter->it_iter.it_bound,
			  (oiter->it_flags & VOS_IT_PUNCHED) ? 0 :
			  VOS_OBJ_VISIBLE, vos_iter_intent(&oiter->it_iter),
			  &oiter->it_obj, ts_set);
	if (rc != 0) {
		VOS_TX_LOG_FAIL(rc, "Could not hold object to iterate: "DF_RC
				"\n", DP_RC(rc));
		D_GOTO(failed, rc);
	}

	oiter->it_punched = oiter->it_obj->obj_ilog_info.ii_prior_punch;

	rc = obj_tree_init(oiter->it_obj);
	if (rc != 0)
		goto failed;

	switch (type) {
	default:
		D_ERROR("unknown iterator type %d.\n", type);
		rc = -DER_INVAL;
		break;

	case VOS_ITER_DKEY:
		rc = dkey_iter_prepare(oiter, &param->ip_akey);
		break;

	case VOS_ITER_AKEY:
		rc = akey_iter_prepare(oiter, &param->ip_dkey, ts_set);
		break;

	case VOS_ITER_SINGLE:
		rc = singv_iter_prepare(oiter, &param->ip_dkey,
					&param->ip_akey);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_prepare(oiter, &param->ip_dkey, &param->ip_akey,
				       ts_set);
		break;
	}

done:
	if (rc != 0)
		D_GOTO(failed, rc);

	*iter_pp = &oiter->it_iter;
	return 0;
 failed:
	vos_obj_iter_fini(&oiter->it_iter);
	return rc;
}

int
vos_obj_iter_nested_tree_fetch(struct vos_iterator *iter, vos_iter_type_t type,
			       struct vos_iter_info *info)
{
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	int			 rc = 0;

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
	case VOS_ITER_RECX:
	case VOS_ITER_SINGLE:
		D_ERROR("Iterator type has no subtree\n");
		return -DER_INVAL;
	case VOS_ITER_DKEY:
		if (type != VOS_ITER_AKEY) {
			D_ERROR("Invalid nested iterator type for "
				"VOS_ITER_DKEY: %d\n", type);
			return -DER_INVAL;
		}
		break;
	case VOS_ITER_AKEY:
		if (type != VOS_ITER_RECX &&
		    type != VOS_ITER_SINGLE) {
			D_ERROR("Invalid nested iterator type for "
				"VOS_ITER_AKEY: %d\n", type);
			return -DER_INVAL;
		}
	};

	rc = key_iter_fetch_root(oiter, type, info);

	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to fetch and initialize cursor "
			"subtree: rc="DF_RC"\n", DP_RC(rc));
		return rc;
	}

	info->ii_obj = oiter->it_obj;

	return 0;
}

static int
nested_dkey_iter_init(struct vos_obj_iter *oiter, struct vos_iter_info *info)
{
	int		rc;

	/* XXX the condition epoch ranges could cover multiple versions of
	 * the object/key if it's punched more than once. However, rebuild
	 * system should guarantee this will never happen.
	 */
	rc = vos_obj_hold(vos_obj_cache_current(), vos_hdl2cont(info->ii_hdl),
			  info->ii_oid, &info->ii_epr, oiter->it_iter.it_bound,
			  (oiter->it_flags & VOS_IT_PUNCHED) ? 0 :
			  VOS_OBJ_VISIBLE, vos_iter_intent(&oiter->it_iter),
			  &oiter->it_obj, NULL);

	D_ASSERTF(rc != -DER_NONEXIST,
		  "Nested iterator called without setting probe");
	if (rc != 0) {
		/** -DER_NONEXIST and -DER_INPROGRESS should be caught earlier.
		 *  This function should only be called after a successful
		 *  probe.
		 */
		D_ERROR("Could not hold object: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = obj_tree_init(oiter->it_obj);

	if (rc != 0)
		goto failed;

	info->ii_punched = oiter->it_obj->obj_ilog_info.ii_prior_punch;

	rc = dkey_iter_prepare(oiter, info->ii_akey);

	if (rc != 0)
		goto failed;

	return 0;
failed:
	vos_obj_release(vos_obj_cache_current(), oiter->it_obj, false);

	return rc;
}

int
vos_obj_iter_nested_prep(vos_iter_type_t type, struct vos_iter_info *info,
			 struct vos_iterator **iter_pp)
{
	struct vos_object	*obj = info->ii_obj;
	struct vos_obj_iter	*oiter;
	struct dtx_handle	*dth = vos_dth_get();
	daos_epoch_t		 bound;
	struct evt_desc_cbs	 cbs;
	struct evt_filter	 filter = {0};
	daos_handle_t		 toh;
	int			 rc = 0;
	uint32_t		 options;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	vos_ilog_fetch_init(&oiter->it_ilog_info);
	bound = dtx_is_valid_handle(dth) ? dth->dth_epoch_bound :
		info->ii_epr.epr_hi;
	oiter->it_iter.it_bound = MAX(bound, info->ii_epr.epr_hi);
	oiter->it_epr = info->ii_epr;
	oiter->it_punched = info->ii_punched;
	oiter->it_epc_expr = info->ii_epc_expr;
	oiter->it_flags = info->ii_flags;
	if (type != VOS_ITER_DKEY)
		oiter->it_obj = obj;
	if (info->ii_flags & VOS_IT_FOR_PURGE)
		oiter->it_iter.it_for_purge = 1;
	if (info->ii_flags & VOS_IT_FOR_MIGRATION)
		oiter->it_iter.it_for_migration = 1;

	switch (type) {
	default:
		D_ERROR("unknown iterator type %d.\n", type);
		rc = -DER_INVAL;
		goto failed;

	case VOS_ITER_DKEY:
		rc = nested_dkey_iter_init(oiter, info);
		if (rc != 0)
			goto failed;
		goto success;
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		rc = dbtree_open_inplace_ex(info->ii_btr, info->ii_uma,
					vos_cont2hdl(obj->obj_cont),
					vos_obj2pool(obj), &toh);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open tree for iterator:"
				" rc = "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
		rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED,
					 &oiter->it_hdl);
		break;

	case VOS_ITER_RECX:
		vos_evt_desc_cbs_init(&cbs, vos_obj2pool(obj),
				      vos_cont2hdl(obj->obj_cont));
		rc = evt_open(info->ii_evt, info->ii_uma, &cbs, &toh);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open tree for iterator:"
				" rc = "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
		recx2filter(&filter, &info->ii_recx);
		filter.fr_epr.epr_lo = oiter->it_epr.epr_lo;
		filter.fr_epr.epr_hi = oiter->it_iter.it_bound;
		filter.fr_epoch = oiter->it_epr.epr_hi;
		filter.fr_punch_epc = oiter->it_punched.pr_epc;
		filter.fr_punch_minor_epc = oiter->it_punched.pr_minor_epc;
		options = recx_get_flags(oiter);
		rc = evt_iter_prepare(toh, options, &filter, &oiter->it_hdl);
		break;
	}
	key_tree_release(toh, type == VOS_ITER_RECX);

	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to prepare iterator: rc = "DF_RC"\n",
			DP_RC(rc));
		goto failed;
	}

success:
	*iter_pp = &oiter->it_iter;
	return 0;
failed:
	vos_ilog_fetch_finish(&oiter->it_ilog_info);
	D_FREE(oiter);
	return rc;
}

/** release the object iterator */
static int
vos_obj_iter_fini(struct vos_iterator *iter)
{
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	int			 rc;

	if (daos_handle_is_inval(oiter->it_hdl))
		D_GOTO(out, rc = -DER_NO_HDL);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		break;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		rc = dbtree_iter_finish(oiter->it_hdl);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_fini(oiter);
		break;
	}
 out:
	/* Release the object only if we didn't borrow it from the parent
	 * iterator.   The generic code reference counts the iterators
	 * to ensure that a parent never gets removed before all nested
	 * iterators are finalized
	 */
	if (oiter->it_flags != VOS_IT_KEY_TREE && oiter->it_obj != NULL &&
	    (iter->it_type == VOS_ITER_DKEY || !iter->it_from_parent))
		vos_obj_release(vos_obj_cache_current(), oiter->it_obj, false);

	vos_ilog_fetch_finish(&oiter->it_ilog_info);
	D_FREE(oiter);
	return 0;
}

int
vos_obj_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_probe(oiter, anchor);

	case VOS_ITER_SINGLE:
		return singv_iter_probe(oiter, anchor);

	case VOS_ITER_RECX:
		return recx_iter_probe(oiter, anchor);
	}
}

static int
vos_obj_iter_next(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_next(oiter);

	case VOS_ITER_SINGLE:
		return singv_iter_next(oiter);

	case VOS_ITER_RECX:
		return recx_iter_next(oiter);
	}
}

static int
vos_obj_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		   daos_anchor_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_fetch(oiter, it_entry, anchor, false);

	case VOS_ITER_SINGLE:
		return singv_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_RECX:
		return recx_iter_fetch(oiter, it_entry, anchor);
	}
}

static int
vos_obj_iter_copy(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		  d_iov_t *iov_out)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_copy(oiter, it_entry, iov_out);
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX:
		return recx_iter_copy(oiter, it_entry, iov_out);
	default:
		D_ASSERT(0);
		return -DER_INVAL;
	}
}

static int
obj_iter_delete(struct vos_obj_iter *oiter, void *args)
{
	struct umem_instance	*umm;
	int			 rc = 0;

	umm = vos_obj2umm(oiter->it_obj);

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		goto exit;

	rc = dbtree_iter_delete(oiter->it_hdl, args);

	rc = umem_tx_end(umm, rc);
exit:
	if (rc != 0)
		D_ERROR("Failed to delete iter entry: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
vos_obj_iter_aggregate(daos_handle_t ih, bool discard)
{
	struct vos_iterator	*iter = vos_hdl2iter(ih);
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	struct umem_instance	*umm;
	struct vos_krec_df	*krec;
	struct vos_object	*obj;
	daos_key_t		 key;
	struct vos_rec_bundle	 rbund;
	bool			 reprobe = false;
	int			 rc;

	D_ASSERTF(iter->it_type == VOS_ITER_AKEY ||
		  iter->it_type == VOS_ITER_DKEY,
		  "Aggregation only supported on keys\n");

	rc = key_iter_fetch_helper(oiter, &rbund, &key, NULL);
	D_ASSERTF(rc != -DER_NONEXIST,
		  "Iterator should probe before aggregation\n");
	if (rc != 0)
		return rc;

	obj = oiter->it_obj;
	krec = rbund.rb_krec;
	umm = vos_obj2umm(oiter->it_obj);

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		goto exit;

	rc = vos_ilog_aggregate(vos_cont2hdl(obj->obj_cont), &krec->kr_ilog,
				&oiter->it_epr, discard,
				oiter->it_punched.pr_epc, &oiter->it_ilog_info);

	if (rc == 1) {
		/* Incarnation log is empty so delete the key */
		reprobe = true;
		D_DEBUG(DB_IO, "Removing %s from tree\n",
			iter->it_type == VOS_ITER_DKEY ? "dkey" : "akey");
		if (krec->kr_bmap & KREC_BF_BTR &&
		    !dbtree_is_empty_inplace(&krec->kr_btr)) {
			/* This should be an assert eventually but we can't
			 * at present prevent underpunch
			 */
			D_ERROR("Removing orphaned single value tree\n");
		} else if (krec->kr_bmap & KREC_BF_EVT &&
			   !evt_is_empty(&krec->kr_evt)) {
			/* This should be an assert eventually but we can't
			 * at present prevent underpunch
			 */
			D_ERROR("Removing orphaned array value tree\n");
		}
		rc = dbtree_iter_delete(oiter->it_hdl, NULL);
		D_ASSERT(rc != -DER_NONEXIST);
	} else if (rc == -DER_NONEXIST) {
		/* Key no longer exists at epoch but isn't empty */
		reprobe = true;
		rc = 0;
	}

	rc = umem_tx_end(umm, rc);

exit:
	if (rc == 0 && reprobe)
		return 1;

	return rc;
}

static int
vos_obj_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return obj_iter_delete(oiter, args);

	case VOS_ITER_RECX:
		return evt_iter_delete(oiter->it_hdl, NULL);
	}
}

static int
vos_obj_iter_empty(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	if (daos_handle_is_inval(oiter->it_hdl))
		return -DER_NO_HDL;

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;
	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return dbtree_iter_empty(oiter->it_hdl);
	case VOS_ITER_RECX:
		return evt_iter_empty(oiter->it_hdl);
	}
}

struct vos_iter_ops	vos_obj_iter_ops = {
	.iop_prepare		= vos_obj_iter_prep,
	.iop_nested_tree_fetch	= vos_obj_iter_nested_tree_fetch,
	.iop_nested_prepare	= vos_obj_iter_nested_prep,
	.iop_finish		= vos_obj_iter_fini,
	.iop_probe		= vos_obj_iter_probe,
	.iop_next		= vos_obj_iter_next,
	.iop_fetch		= vos_obj_iter_fetch,
	.iop_copy		= vos_obj_iter_copy,
	.iop_delete		= vos_obj_iter_delete,
	.iop_empty		= vos_obj_iter_empty,
};
/**
 * @} vos_obj_iters
 */
