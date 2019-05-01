/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos two-phase commit transaction.
 *
 * vos/vos_dtx.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_srv/vos.h>
#include "vos_layout.h"
#include "vos_internal.h"

/* Dummy offset for an aborted DTX address. */
#define DTX_UMOFF_ABORTED		1

/* Dummy offset for some unknown DTX address. */
#define DTX_UMOFF_UNKNOWN		2

static inline bool
dtx_is_aborted(umem_off_t umoff)
{
	return umem_off_get_invalid_flags(umoff) == DTX_UMOFF_ABORTED;
}

static inline bool
dtx_is_unknown(umem_off_t umoff)
{
	return umem_off_get_invalid_flags(umoff) == DTX_UMOFF_UNKNOWN;
}

static void
dtx_set_aborted(umem_off_t *umoff)
{
	umem_off_set_invalid(umoff, DTX_UMOFF_ABORTED);
}

static void
dtx_set_unknown(umem_off_t *umoff)
{
	umem_off_set_invalid(umoff, DTX_UMOFF_UNKNOWN);
}

static inline int
dtx_inprogress(struct vos_dtx_entry_df *dtx, int pos)
{
	if (dtx != NULL)
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX "DF_DTI
			" with state %u at %d\n",
			DP_DTI(&dtx->te_xid), dtx->te_state, pos);
	else
		D_DEBUG(DB_TRACE, "Hit uncommitted (unknown) DTX at %d\n", pos);

	return -DER_INPROGRESS;
}

static inline void
dtx_record_conflict(struct dtx_handle *dth, struct vos_dtx_entry_df *dtx)
{
	if (dth != NULL && dtx != NULL) {
		daos_dti_copy(&dth->dth_conflict->dce_xid, &dtx->te_xid);
		dth->dth_conflict->dce_dkey = dtx->te_dkey_hash;
	}
}

struct dtx_rec_bundle {
	umem_off_t	trb_umoff;
};

static int
dtx_hkey_size(void)
{
	return sizeof(struct dtx_id);
}

static void
dtx_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct dtx_id));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtx_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct dtx_id	*hkey1 = (struct dtx_id *)&rec->rec_hkey[0];
	struct dtx_id	*hkey2 = (struct dtx_id *)hkey;
	int		 rc;

	rc = memcmp(hkey1, hkey2, sizeof(struct dtx_id));

	return dbtree_key_cmp_rc(rc);
}

static int
dtx_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_rec_bundle	*rbund;

	rbund = (struct dtx_rec_bundle *)val_iov->iov_buf;
	D_ASSERT(!dtx_is_null(rbund->trb_umoff));

	/* Directly reference the input addreass (in SCM). */
	rec->rec_off = rbund->trb_umoff;

	return 0;
}

static int
dtx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_off_t	*umoff;

	D_ASSERT(args != NULL);
	D_ASSERT(!UMOFF_IS_NULL(rec->rec_off));

	/* Return the record addreass (offset in SCM). The caller will
	 * release it after using.
	 */
	umoff = (umem_off_t *)args;
	*umoff = rec->rec_off;
	rec->rec_off = UMOFF_NULL;

	return 0;
}

static int
dtx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_dtx_entry_df	*dtx;

	D_ASSERT(val_iov != NULL);

	dtx = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	daos_iov_set(val_iov, dtx, sizeof(*dtx));
	return 0;
}

static int
dtx_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key, daos_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t dtx_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_rec_alloc,
	.to_rec_free	= dtx_rec_free,
	.to_rec_fetch	= dtx_rec_fetch,
	.to_rec_update	= dtx_rec_update,
};

#define DTX_BTREE_ORDER		20

int
vos_dtx_table_register(void)
{
	int	rc;

	D_DEBUG(DB_DF, "Registering DTX table class: %d\n", VOS_BTR_DTX_TABLE);

	rc = dbtree_class_register(VOS_BTR_DTX_TABLE, 0, &dtx_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX dbtree: rc = %d\n", rc);

	return rc;
}

int
vos_dtx_table_create(struct vos_pool *pool, struct vos_dtx_table_df *dtab_df)
{
	daos_handle_t	hdl;
	int		rc;

	if (pool == NULL || dtab_df == NULL) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	D_ASSERT(dtab_df->tt_active_btr.tr_class == 0);
	D_ASSERT(dtab_df->tt_committed_btr.tr_class == 0);

	D_DEBUG(DB_DF, "create DTX dbtree in-place for pool "DF_UUID": %d\n",
		DP_UUID(pool->vp_id), VOS_BTR_DTX_TABLE);

	rc = dbtree_create_inplace(VOS_BTR_DTX_TABLE, 0,
				   DTX_BTREE_ORDER, &pool->vp_uma,
				   &dtab_df->tt_active_btr, &hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX active dbtree for pool "
			DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
		return rc;
	}

	dbtree_close(hdl);

	rc = dbtree_create_inplace(VOS_BTR_DTX_TABLE, 0,
				   DTX_BTREE_ORDER, &pool->vp_uma,
				   &dtab_df->tt_committed_btr, &hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX committed dbtree for pool "
			DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
		return rc;
	}

	dtab_df->tt_count = 0;
	dtab_df->tt_entry_head = UMOFF_NULL;
	dtab_df->tt_entry_tail = UMOFF_NULL;

	dbtree_close(hdl);
	return 0;
}

int
vos_dtx_table_destroy(struct vos_pool *pool, struct vos_dtx_table_df *dtab_df)
{
	daos_handle_t	hdl;
	int		rc = 0;

	if (pool == NULL || dtab_df == NULL) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	if (dtab_df->tt_active_btr.tr_class != 0) {
		rc = dbtree_open_inplace(&dtab_df->tt_active_btr,
					 &pool->vp_uma, &hdl);
		if (rc == 0)
			rc = dbtree_destroy(hdl);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX active dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	if (dtab_df->tt_committed_btr.tr_class != 0) {
		rc = dbtree_open_inplace(&dtab_df->tt_committed_btr,
					 &pool->vp_uma, &hdl);
		if (rc == 0)
			rc = dbtree_destroy(hdl);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX committed dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	return rc;
}

static void
dtx_obj_rec_exchange(struct umem_instance *umm, struct vos_obj_df *obj,
		     struct vos_dtx_entry_df *dtx,
		     struct vos_dtx_record_df *rec, bool abort)
{
	struct vos_dtx_record_df	*tgt_rec;
	struct vos_obj_df		*tgt_obj;

	if (rec->tr_flags == DTX_RF_EXCHANGE_TGT) {
		/* For commit case, should already have been handled
		 * during handling the soruce.
		 *
		 * For abort case, set its vo_dtx as aborted. The tgt
		 * record will be removed via VOS aggregation or other
		 * tools some time later.
		 */
		if (abort)
			dtx_set_aborted(&obj->vo_dtx);
		return;
	}

	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC)
		return;

	if (!(obj->vo_oi_attr & VOS_OI_REMOVED)) {
		D_ERROR(DF_UOID" with OBJ DTX ("DF_DTI") missed REMOVED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	if (abort) { /* abort case */
		/* Recover the availability of the exchange source. */
		obj->vo_oi_attr &= ~VOS_OI_REMOVED;
		obj->vo_dtx = UMOFF_NULL;
		return;
	}

	/* XXX: If the exchange target still exist, it will be the next
	 *	record. If it does not exist, then either it is crashed
	 *	or it has already deregistered from the DTX records list.
	 *	We cannot commit the DTX under any the two cases. Fail
	 *	the DTX commit is meaningless, then some warnings.
	 */
	if (dtx_is_null(rec->tr_next)) {
		D_ERROR(DF_UOID" miss OBJ DTX ("DF_DTI") exchange pairs (1)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	tgt_rec = umem_off2ptr(umm, rec->tr_next);
	if (tgt_rec->tr_flags != DTX_RF_EXCHANGE_TGT) {
		D_ERROR(DF_UOID" miss OBJ DTX ("DF_DTI") exchange pairs (2)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	/* Exchange the sub-tree between max epoch record and the given
	 * epoch record. The record with max epoch will be removed when
	 * aggregation or some special cleanup.
	 */
	tgt_obj = umem_off2ptr(umm, tgt_rec->tr_record);
	umem_tx_add_ptr(umm, tgt_obj, sizeof(*tgt_obj));

	/* The @tgt_obj which epoch is current DTX's epoch will be
	 * available to outside of VOS. Set its vo_earliest as @obj
	 * vo_earliest.
	 */
	tgt_obj->vo_tree = obj->vo_tree;
	tgt_obj->vo_earliest = obj->vo_earliest;
	tgt_obj->vo_latest = dtx->te_epoch;
	tgt_obj->vo_incarnation = obj->vo_incarnation;

	/* The @obj which epoch is MAX will be removed later. */
	memset(&obj->vo_tree, 0, sizeof(obj->vo_tree));
	obj->vo_latest = 0;
	obj->vo_earliest = DAOS_EPOCH_MAX;
	obj->vo_incarnation++; /* cache should be revalidated */

	D_DEBUG(DB_TRACE, "Exchanged OBJ DTX records for "DF_DTI"\n",
		DP_DTI(&dtx->te_xid));
}

static void
dtx_obj_rec_release(struct umem_instance *umm, struct vos_obj_df *obj,
		    struct vos_dtx_record_df *rec, umem_off_t umoff, bool abort)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_off2ptr(umm, umoff);
	if (dtx->te_intent == DAOS_INTENT_PUNCH) {
		/* Because PUNCH cannot share with others, the vo_dtx should
		 * reference current DTX.
		 */
		if (obj->vo_dtx != umoff)
			D_ERROR("The OBJ "DF_UOID" should referece DTX "
				DF_DTI", but referenced %s\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				dtx_is_null(obj->vo_dtx) ? "UNLL" : "other");
		else
			dtx_obj_rec_exchange(umm, obj, dtx, rec, abort);
		return;
	}

	/* Because PUNCH and UPDATE cannot share, both current DTX
	 * and the obj former referenced DTX should be for UPDATE.
	 */

	obj->vo_dtx_shares--;

	/* If current DTX references the object with VOS_OI_REMOVED,
	 * then means that at least one of the former shared update
	 * DTXs has been committed before current update DTX commit
	 * or abort. Then in spite of whether the punch DTX will be
	 * or has been committed or not, current update DTX (in sipte
	 * of commit or abort) needs to do nothing.
	 *
	 * If the punch DTX is aborted, then the VOS_OI_REMOVED will
	 * be removed, and the obj::vo_dtx will be set as NULL.
	 */
	if (obj->vo_oi_attr & VOS_OI_REMOVED)
		return;

	/* Other DTX that shares the obj has been committed firstly.
	 * It must be for sharing of update. Subsequent modification
	 * may have seen the modification before current DTX commit
	 * or abort.
	 *
	 * Please NOTE that the obj::vo_latest and obj::vo_earliest
	 * have already been handled during vos_update_end().
	 */
	if (dtx_is_null(obj->vo_dtx))
		return;

	if (abort) {
		if (obj->vo_dtx_shares == 0)
			/* The last shared UPDATE DTX is aborted. */
			dtx_set_aborted(&obj->vo_dtx);
		else if (obj->vo_dtx == umoff)
			/* I am the original DTX that create the object that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			dtx_set_unknown(&obj->vo_dtx);
	} else {
		obj->vo_dtx = UMOFF_NULL;
	}
}

static void
dtx_key_rec_exchange(struct umem_instance *umm, struct vos_krec_df *key,
		     struct vos_dtx_entry_df *dtx,
		     struct vos_dtx_record_df *rec, bool abort)
{
	struct vos_dtx_record_df	*tgt_rec;
	struct vos_krec_df		*tgt_key;

	if (rec->tr_flags == DTX_RF_EXCHANGE_TGT) {
		/* For commit case, should already have been handled
		 * during handling the soruce.
		 *
		 * For abort case, set its kr_dtx as aborted. The tgt
		 * record will be removed via VOS aggregation or other
		 * tools some time later.
		 */
		if (abort)
			dtx_set_aborted(&key->kr_dtx);
		return;
	}

	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC)
		return;

	if (!(key->kr_bmap & KREC_BF_REMOVED)) {
		D_ERROR(DF_UOID" with KEY DTX ("DF_DTI") missed REMOVED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	if (abort) { /* abort case */
		/* Recover the availability of the exchange source. */
		key->kr_bmap &= ~KREC_BF_REMOVED;
		key->kr_dtx = UMOFF_NULL;
		return;
	}

	/* XXX: If the exchange target still exist, it will be the next
	 *	record. If it does not exist, then either it is crashed
	 *	or it has already deregistered from the DTX records list.
	 *	We cannot commit the DTX under any the two cases. Fail
	 *	the DTX commit is meaningless, then some warnings.
	 */
	if (dtx_is_null(rec->tr_next)) {
		D_ERROR(DF_UOID" miss KEY DTX ("DF_DTI") exchange pairs (1)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	tgt_rec = umem_off2ptr(umm, rec->tr_next);
	if (tgt_rec->tr_flags != DTX_RF_EXCHANGE_TGT) {
		D_ERROR(DF_UOID" miss KEY DTX ("DF_DTI") exchange pairs (2)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	/* Exchange the sub-tree between max epoch record and the given
	 * epoch record. The record with max epoch will be removed when
	 * aggregation or some special cleanup.
	 */
	tgt_key = umem_off2ptr(umm, tgt_rec->tr_record);
	umem_tx_add_ptr(umm, tgt_key, sizeof(*tgt_key));

	if (key->kr_bmap & KREC_BF_EVT) {
		tgt_key->kr_evt = key->kr_evt;
		/* The @key which epoch is MAX will be removed later. */
		memset(&key->kr_evt, 0, sizeof(key->kr_evt));
	} else {
		D_ASSERT(key->kr_bmap & KREC_BF_BTR);

		tgt_key->kr_btr = key->kr_btr;
		/* The @key which epoch is MAX will be removed later. */
		memset(&key->kr_btr, 0, sizeof(key->kr_btr));
	}

	/* The @tgt_key which epoch is current DTX's epoch will be
	 * available to outside of VOS. Set its kr_earliest as @key
	 * kr_earliest.
	 */
	tgt_key->kr_earliest = key->kr_earliest;
	tgt_key->kr_latest = dtx->te_epoch;
	tgt_key->kr_bmap = (key->kr_bmap | KREC_BF_PUNCHED) & ~KREC_BF_REMOVED;

	key->kr_latest = 0;
	key->kr_earliest = DAOS_EPOCH_MAX;

	D_DEBUG(DB_TRACE, "Exchanged KEY DTX records for "DF_DTI"\n",
		DP_DTI(&dtx->te_xid));
}

static void
dtx_key_rec_release(struct umem_instance *umm, struct vos_krec_df *key,
		    struct vos_dtx_record_df *rec, umem_off_t umoff, bool abort)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_off2ptr(umm, umoff);
	if (dtx->te_intent == DAOS_INTENT_PUNCH) {
		/* Because PUNCH cannot share with others, the kr_dtx should
		 * reference current DTX.
		 */
		if (key->kr_dtx != umoff)
			D_ERROR("The KEY "DF_UOID" should referece DTX "
				DF_DTI", but referenced %s\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				dtx_is_null(key->kr_dtx) ? "UNLL" : "other");
		else
			dtx_key_rec_exchange(umm, key, dtx, rec, abort);
		return;
	}

	/* Because PUNCH and UPDATE cannot share, both current DTX
	 * and the key former referenced DTX should be for UPDATE.
	 */

	key->kr_dtx_shares--;

	/* If current DTX references the key with KREC_BF_REMOVED,
	 * then means that at least one of the former shared update
	 * DTXs has been committed before current update DTX commit
	 * or abort. Then in spite of whether the punch DTX will be
	 * or has been committed or not, current update DTX (in sipte
	 * of commit or abort) needs to do nothing.
	 *
	 * If the punch DTX is aborted, then the VOS_OI_REMOVED will
	 * be removed, and the key::kr_dtx will be set as NULL.
	 */
	if (key->kr_bmap & KREC_BF_REMOVED)
		return;

	/* Other DTX that shares the key has been committed firstly.
	 * It must be for sharing of update. Subsequent modification
	 * may have seen the modification before current DTX commit
	 * or abort.
	 *
	 * Please NOTE that the key::kr_latest and key::kr_earliest
	 * have already been handled during vos_update_end().
	 */
	if (dtx_is_null(key->kr_dtx))
		return;

	if (abort) {
		if (key->kr_dtx_shares == 0)
			/* The last shared UPDATE DTX is aborted. */
			dtx_set_aborted(&key->kr_dtx);
		else if (key->kr_dtx == umoff)
			/* I am the original DTX that create the key that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			dtx_set_unknown(&key->kr_dtx);
	} else {
		key->kr_dtx = UMOFF_NULL;
	}
}

static void
dtx_rec_release(struct umem_instance *umm, umem_off_t umoff,
		bool abort, bool destroy)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_off2ptr(umm, umoff);
	if (!destroy &&
	    (!dtx_is_null(dtx->te_records) ||
	     dtx->te_flags & (DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES)))
		umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	while (!dtx_is_null(dtx->te_records)) {
		umem_off_t			 rec_umoff = dtx->te_records;
		struct vos_dtx_record_df	*rec;

		rec = umem_off2ptr(umm, rec_umoff);
		switch (rec->tr_type) {
		case DTX_RT_OBJ: {
			struct vos_obj_df	*obj;

			obj = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, obj, sizeof(*obj));
			dtx_obj_rec_release(umm, obj, rec, umoff, abort);
			break;
		}
		case DTX_RT_KEY: {
			struct vos_krec_df	*key;

			key = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, key, sizeof(*key));
			dtx_key_rec_release(umm, key, rec, umoff, abort);
			break;
		}
		case DTX_RT_SVT: {
			struct vos_irec_df	*svt;

			svt = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &svt->ir_dtx, sizeof(svt->ir_dtx));
			if (abort)
				dtx_set_aborted(&svt->ir_dtx);
			else
				svt->ir_dtx = UMOFF_NULL;
			break;
		}
		case DTX_RT_EVT: {
			struct evt_desc		*evt;

			evt = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &evt->dc_dtx, sizeof(evt->dc_dtx));
			if (abort)
				dtx_set_aborted(&evt->dc_dtx);
			else
				evt->dc_dtx = UMOFF_NULL;
			break;
		}
		default:
			D_ERROR(DF_UOID" unknown DTX "DF_DTI" type %u\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				rec->tr_type);
			break;
		}

		dtx->te_records = rec->tr_next;
		umem_free_off(umm, rec_umoff);
	}

	D_DEBUG(DB_TRACE, "dtx_rec_release: %s/%s the DTX "DF_DTI"\n",
		abort ? "abort" : "commit", destroy ? "destroy" : "keep",
		DP_DTI(&dtx->te_xid));

	if (destroy)
		umem_free_off(umm, umoff);
	else
		dtx->te_flags &= ~(DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES);
}

static void
vos_dtx_unlink_entry(struct umem_instance *umm, struct vos_dtx_table_df *tab,
		     struct vos_dtx_entry_df *dtx)
{
	struct vos_dtx_entry_df	*ent;

	if (dtx_is_null(dtx->te_next)) { /* The tail of the DTXs list. */
		if (dtx_is_null(dtx->te_prev)) { /* The unique one on list. */
			tab->tt_entry_head = UMOFF_NULL;
			tab->tt_entry_tail = UMOFF_NULL;
		} else {
			ent = umem_off2ptr(umm, dtx->te_prev);
			umem_tx_add_ptr(umm, &ent->te_next,
					sizeof(ent->te_next));
			ent->te_next = UMOFF_NULL;
			tab->tt_entry_tail = dtx->te_prev;
			dtx->te_prev = UMOFF_NULL;
		}
	} else if (dtx_is_null(dtx->te_prev)) { /* The head of DTXs list */
		ent = umem_off2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, &ent->te_prev, sizeof(ent->te_prev));
		ent->te_prev = UMOFF_NULL;
		tab->tt_entry_head = dtx->te_next;
		dtx->te_next = UMOFF_NULL;
	} else {
		ent = umem_off2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, &ent->te_prev, sizeof(ent->te_prev));
		ent->te_prev = dtx->te_prev;

		ent = umem_off2ptr(umm, dtx->te_prev);
		umem_tx_add_ptr(umm, &ent->te_next, sizeof(ent->te_next));
		ent->te_next = dtx->te_next;

		dtx->te_prev = UMOFF_NULL;
		dtx->te_next = UMOFF_NULL;
	}
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct dtx_id *dti)
{
	struct umem_instance		*umm = &cont->vc_pool->vp_umm;
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_entry_df		*ent;
	struct vos_dtx_table_df		*tab;
	struct dtx_rec_bundle		 rbund;
	daos_iov_t			 kiov;
	daos_iov_t			 riov;
	umem_off_t			 umoff;
	int				 rc = 0;

	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &umoff);
	if (rc == -DER_NONEXIST) {
		daos_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		goto out;
	}

	if (rc != 0)
		goto out;

	dtx = umem_off2ptr(umm, umoff);
	umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	dtx->te_state = DTX_ST_COMMITTED;
	rbund.trb_umoff = umoff;
	daos_iov_set(&riov, &rbund, sizeof(rbund));
	rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc != 0)
		goto out;

	tab = &cont->vc_cont_df->cd_dtx_table_df;
	umem_tx_add_ptr(umm, tab, sizeof(*tab));

	tab->tt_count++;
	if (dtx_is_null(tab->tt_entry_tail)) {
		D_ASSERT(dtx_is_null(tab->tt_entry_head));

		tab->tt_entry_head = umoff;
		tab->tt_entry_tail = tab->tt_entry_head;
	} else {
		ent = umem_off2ptr(umm, tab->tt_entry_tail);
		umem_tx_add_ptr(umm, &ent->te_next, sizeof(ent->te_next));

		ent->te_next = umoff;
		dtx->te_prev = tab->tt_entry_tail;
		tab->tt_entry_tail = ent->te_next;
	}

	/* XXX: Only mark the DTX as DTX_ST_COMMITTED (when commit) is not
	 *	enough. Otherwise, some subsequent modification may change
	 *	related data record's DTX reference or remove related data
	 *	record as to the current DTX will have invalid reference(s)
	 *	via its DTX record(s).
	 */
	dtx_rec_release(umm, umoff, false, false);
	vos_dtx_del_cos(cont, &dtx->te_oid, dti, dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);

out:
	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = %d\n",
		DP_DTI(dti), rc);

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, struct dtx_id *dti,
		  bool force)
{
	daos_iov_t	 kiov;
	umem_off_t	 dtx;
	int		 rc;

	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &dtx);
	if (rc == 0)
		dtx_rec_release(&cont->vc_pool->vp_umm, dtx, true, true);

	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = %d\n", DP_DTI(dti), rc);

	if (rc != 0 && force)
		rc = 0;

	return rc;
}

struct vos_dtx_share {
	/** Link into the dtx_handle::dth_shares */
	d_list_t		vds_link;
	/** The DTX record type, see enum vos_dtx_record_types. */
	uint32_t		vds_type;
	/** The record in the related tree in SCM. */
	umem_off_t		vds_record;
};

static int (*vos_dtx_check_leader)(uuid_t, daos_unit_oid_t *,
				   uint32_t, struct pl_obj_layout **) = NULL;

static bool
vos_dtx_is_normal_entry(struct umem_instance *umm, umem_off_t entry)
{
	if (dtx_is_null(entry) || dtx_is_aborted(entry) ||
	    dtx_is_unknown(entry))
		return false;

	return true;
}

static int
vos_dtx_alloc(struct umem_instance *umm, struct dtx_handle *dth,
	      umem_off_t rec_umoff, struct vos_dtx_entry_df **dtxp)
{
	struct vos_dtx_entry_df	*dtx;
	struct vos_container	*cont;
	umem_off_t		 dtx_umoff;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	struct dtx_rec_bundle	 rbund;
	int			 rc;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dtx_umoff = umem_zalloc_off(umm, sizeof(struct vos_dtx_entry_df));
	if (dtx_is_null(dtx_umoff))
		return -DER_NOMEM;

	dtx = umem_off2ptr(umm, dtx_umoff);
	dtx->te_xid = dth->dth_xid;
	dtx->te_oid = dth->dth_oid;
	dtx->te_dkey_hash = dth->dth_dkey_hash;
	dtx->te_epoch = dth->dth_epoch;
	dtx->te_ver = dth->dth_ver;
	dtx->te_state = DTX_ST_INIT;
	dtx->te_flags = 0;
	dtx->te_intent = dth->dth_intent;
	dtx->te_sec = crt_hlc_get();
	dtx->te_records = rec_umoff;
	dtx->te_next = UMOFF_NULL;
	dtx->te_prev = UMOFF_NULL;

	rbund.trb_umoff = dtx_umoff;
	daos_iov_set(&riov, &rbund, sizeof(rbund));
	daos_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
	rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc == 0) {
		dth->dth_ent = dtx_umoff;
		*dtxp = dtx;
	}

	return rc;
}

static void
vos_dtx_append(struct umem_instance *umm, struct dtx_handle *dth,
	       umem_off_t rec_umoff, umem_off_t record, uint32_t type,
	       uint32_t flags, struct vos_dtx_entry_df **dtxp)
{
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_record_df	*tgt;

	/* The @dtx must be new created via former
	 * vos_dtx_register_record(), no need umem_tx_add_ptr().
	 */
	dtx = umem_off2ptr(umm, dth->dth_ent);
	dtx->te_sec = crt_hlc_get();
	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_umoff;
	*dtxp = dtx;

	if (flags == 0)
		return;

	/*
	 * XXX: Currently, we only support DTX_RF_EXCHANGE_SRC when register
	 *	the target for punch {a,d}key. It is implemented as exchange
	 *	related {a,d}key. The exchange target has registered its record
	 *	to the DTX just before the exchange source.
	 */
	D_ASSERT(flags == DTX_RF_EXCHANGE_SRC);
	D_ASSERT(type == DTX_RT_OBJ || type == DTX_RT_KEY);

	/* The @tgt must be new created via former
	 * vos_dtx_register_record(), no need umem_tx_add_ptr().
	 */
	tgt = umem_off2ptr(umm, rec->tr_next);
	D_ASSERT(tgt->tr_flags == 0);

	tgt->tr_flags = DTX_RF_EXCHANGE_TGT;
	dtx->te_flags |= DTX_EF_EXCHANGE_PENDING;

	if (type == DTX_RT_OBJ) {
		struct vos_obj_df	*obj;

		obj = umem_off2ptr(umm, record);
		obj->vo_oi_attr |= VOS_OI_REMOVED;
	} else {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		key->kr_bmap |= KREC_BF_REMOVED;
	}

	D_DEBUG(DB_TRACE, "Register exchange source for %s DTX "DF_DTI"\n",
		type == DTX_RT_OBJ ? "OBJ" : "KEY", DP_DTI(&dtx->te_xid));
}

static int
vos_dtx_append_share(struct umem_instance *umm, struct vos_dtx_entry_df *dtx,
		     struct vos_dtx_share *vds)
{
	struct vos_dtx_record_df	*rec;
	umem_off_t			 rec_umoff;

	rec_umoff = umem_zalloc_off(umm, sizeof(struct vos_dtx_record_df));
	if (dtx_is_null(rec_umoff))
		return -DER_NOMEM;

	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_type = vds->vds_type;
	rec->tr_flags = 0;
	rec->tr_record = vds->vds_record;

	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_umoff;

	return 0;
}

static int
vos_dtx_share_obj(struct umem_instance *umm, struct dtx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct vos_dtx_share *vds,
		  bool *shared)
{
	struct vos_obj_df	*obj;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	obj = umem_off2ptr(umm, vds->vds_record);
	dth->dth_obj = vds->vds_record;
	/* The to be shared obj has been committed. */
	if (dtx_is_null(obj->vo_dtx))
		return 0;

	rc = vos_dtx_append_share(umm, dtx, vds);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" failed to shares obj "
			"with others:: rc = %d\n",
			DP_DTI(&dth->dth_xid), rc);
		return rc;
	}

	umem_tx_add_ptr(umm, obj, sizeof(*obj));

	/* The to be shared obj has been aborted, reuse it. */
	if (dtx_is_aborted(obj->vo_dtx)) {
		D_ASSERTF(obj->vo_dtx_shares == 0,
			  "Invalid shared obj DTX shares %d\n",
			  obj->vo_dtx_shares);
		obj->vo_dtx_shares = 1;
		return 0;
	}

	obj->vo_dtx_shares++;
	*shared = true;

	/* The original obj DTX has been aborted, but some others
	 * still share the obj. Then set the vo_dtx to current DTX.
	 */
	if (dtx_is_unknown(obj->vo_dtx)) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" shares obj "
			"with unknown DTXs, shares count %u.\n",
			DP_DTI(&dth->dth_xid), obj->vo_dtx_shares);
		obj->vo_dtx = dth->dth_ent;
		return 0;
	}

	D_ASSERT(vos_dtx_is_normal_entry(umm, obj->vo_dtx));

	sh_dtx = umem_off2ptr(umm, obj->vo_dtx);
	D_ASSERT(dtx != sh_dtx);
	D_ASSERTF(sh_dtx->te_state == DTX_ST_PREPARED ||
		  sh_dtx->te_state == DTX_ST_INIT,
		  "Invalid shared obj DTX state: %u\n",
		  sh_dtx->te_state);

	umem_tx_add_ptr(umm, sh_dtx, sizeof(*sh_dtx));
	sh_dtx->te_flags |= DTX_EF_SHARES;

	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares obj "DF_X64
		" with other DTX "DF_DTI", the shares count %u\n",
		DP_DTI(&dth->dth_xid), vds->vds_record,
		DP_DTI(&sh_dtx->te_xid), obj->vo_dtx_shares);

	return 0;
}

static int
vos_dtx_share_key(struct umem_instance *umm, struct dtx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct vos_dtx_share *vds,
		  bool *shared)
{
	struct vos_krec_df	*key;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	rc = vos_dtx_append_share(umm, dtx, vds);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" failed to shares key "
			"with others:: rc = %d\n",
			DP_DTI(&dth->dth_xid), rc);
		return rc;
	}

	key = umem_off2ptr(umm, vds->vds_record);
	umem_tx_add_ptr(umm, key, sizeof(*key));
	key->kr_dtx_shares++;
	*shared = true;

	/* The original key DTX has been aborted, but some others
	 * still share the key. Then set the kr_dtx to current DTX.
	 */
	if (dtx_is_unknown(key->kr_dtx)) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" shares key "
			"with unknown DTXs, shares count %u.\n",
			DP_DTI(&dth->dth_xid), key->kr_dtx_shares);
		key->kr_dtx = dth->dth_ent;
		return 0;
	}

	D_ASSERT(vos_dtx_is_normal_entry(umm, key->kr_dtx));

	sh_dtx = umem_off2ptr(umm, key->kr_dtx);
	D_ASSERT(dtx != sh_dtx);
	D_ASSERTF(sh_dtx->te_state == DTX_ST_PREPARED ||
		  sh_dtx->te_state == DTX_ST_INIT,
		  "Invalid shared key DTX state: %u\n",
		  sh_dtx->te_state);

	umem_tx_add_ptr(umm, sh_dtx, sizeof(*sh_dtx));
	sh_dtx->te_flags |= DTX_EF_SHARES;

	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares key "DF_X64
		" with other DTX "DF_DTI", the shares count %u\n",
		DP_DTI(&dth->dth_xid), vds->vds_record,
		DP_DTI(&sh_dtx->te_xid), key->kr_dtx_shares);

	return 0;
}

static int
vos_dtx_check_shares(struct dtx_handle *dth, struct vos_dtx_entry_df *dtx,
		     umem_off_t record, uint32_t intent, uint32_t type)
{
	struct vos_dtx_share	*vds;

	if (dtx != NULL)
		D_ASSERT(dtx->te_intent == DAOS_INTENT_UPDATE);

	/* PUNCH cannot share with others. */
	if (intent == DAOS_INTENT_PUNCH) {
		/* XXX: One corner case: if some DTXs share the same
		 *	object/key, and the original DTX that create
		 *	the object/key is aborted, then when we come
		 *	here, we do not know which DTX conflict with
		 *	me, so we can NOT set dth::dth_conflict that
		 *	will be used by DTX conflict handling logic.
		 */
		dtx_record_conflict(dth, dtx);

		return dtx_inprogress(dtx, 4);
	}

	D_ASSERT(intent == DAOS_INTENT_UPDATE);

	/* Only OBJ/KEY record can be shared by new update. */
	if (type != DTX_RT_OBJ && type != DTX_RT_KEY) {
		dtx_record_conflict(dth, dtx);

		return dtx_inprogress(dtx, 5);
	}

	if (dth == NULL)
		return dtx_inprogress(dtx, 6);

	D_ALLOC_PTR(vds);
	if (vds == NULL)
		return -DER_NOMEM;

	vds->vds_type = type;
	vds->vds_record = record;
	d_list_add_tail(&vds->vds_link, &dth->dth_shares);

	return ALB_AVAILABLE_CLEAN;
}

int
vos_dtx_check_availability(struct umem_instance *umm, daos_handle_t coh,
			   umem_off_t entry, umem_off_t record, uint32_t intent,
			   uint32_t type)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx = NULL;
	bool				 hidden = false;

	switch (type) {
	case DTX_RT_OBJ: {
		struct vos_obj_df	*obj;

		obj = umem_off2ptr(umm, record);

		/* I just created (or shared) the object,
		 * so should be available unless aborted.
		 */
		if (dth != NULL && dth->dth_obj == record) {
			if (!dtx_is_aborted(obj->vo_dtx))
				return ALB_AVAILABLE_CLEAN;

			if (intent == DAOS_INTENT_PURGE)
				return ALB_AVAILABLE_DIRTY;

			return ALB_UNAVAILABLE;
		}

		if (obj->vo_oi_attr & VOS_OI_REMOVED)
			hidden = true;
		break;
	}
	case DTX_RT_KEY: {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		if (key->kr_bmap & KREC_BF_REMOVED)
			hidden = true;
		break;
	}
	case DTX_RT_SVT:
	case DTX_RT_EVT:
		break;
	default:
		D_ERROR("Unexpected DTX type %u\n", type);
		/* Everything is available to PURGE, even if it belongs to some
		 * uncommitted DTX that may be garbage because of corruption.
		 */
		if (intent == DAOS_INTENT_PURGE)
			return ALB_AVAILABLE_DIRTY;

		return -DER_INVAL;
	}

	if (intent == DAOS_INTENT_CHECK) {
		/* Check whether the DTX is aborted or not. */
		if (dtx_is_aborted(entry))
			return ALB_UNAVAILABLE;
		else
			return ALB_AVAILABLE_CLEAN;
	}

	/* Committed */
	if (dtx_is_null(entry)) {
		if (!hidden)
			return ALB_AVAILABLE_CLEAN;

		if (intent == DAOS_INTENT_PURGE)
			return ALB_AVAILABLE_DIRTY;

		return ALB_UNAVAILABLE;
	}

	if (intent == DAOS_INTENT_PURGE)
		return hidden ? ALB_AVAILABLE_CLEAN : ALB_AVAILABLE_DIRTY;

	/* Aborted */
	if (dtx_is_aborted(entry))
		return hidden ? ALB_AVAILABLE_CLEAN : ALB_UNAVAILABLE;

	if (dtx_is_unknown(entry)) {
		/* The original DTX must be with DAOS_INTENT_UPDATE, then
		 * it has been shared by other UPDATE DTXs. And then the
		 * original DTX was aborted, but other DTXs still are not
		 * 'committable' yet.
		 */

		if (intent == DAOS_INTENT_DEFAULT ||
		    intent == DAOS_INTENT_REBUILD)
			return hidden ? ALB_AVAILABLE_CLEAN : ALB_UNAVAILABLE;

		return vos_dtx_check_shares(dth, NULL, record, intent, type);
	}

	/* The DTX owner can always see the DTX. */
	if (dth != NULL && entry == dth->dth_ent)
		return ALB_AVAILABLE_CLEAN;

	dtx = umem_off2ptr(umm, entry);
	switch (dtx->te_state) {
	case DTX_ST_COMMITTED:
		return hidden ? ALB_UNAVAILABLE : ALB_AVAILABLE_CLEAN;
	case DTX_ST_PREPARED: {
		struct vos_container	*cont = vos_hdl2cont(coh);
		int			 rc;

		rc = vos_dtx_lookup_cos(coh, &dtx->te_oid, &dtx->te_xid,
			dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);
		if (rc == 0) {
			/* XXX: For the committable punch DTX, if there is
			 *	pending exchange (of sub-trees) operation,
			 *	then do it, otherwise the subsequent fetch
			 *	cannot get the proper sub-trees.
			 */
			if (dtx->te_flags & DTX_EF_EXCHANGE_PENDING) {
				rc = vos_tx_begin(cont->vc_pool);
				if (rc == 0) {
					dtx_rec_release(umm, entry,
							false, false);
					rc = vos_tx_end(cont->vc_pool, 0);
				}

				if (rc != 0)
					return rc;
			}

			return hidden ? ALB_UNAVAILABLE : ALB_AVAILABLE_CLEAN;
		}

		if (rc != -DER_NONEXIST)
			return rc;

		/* The followings are for non-committable cases. */

		if (intent == DAOS_INTENT_DEFAULT ||
		    intent == DAOS_INTENT_REBUILD) {
			rc = vos_dtx_check_leader(cont->vc_pool->vp_id,
						  &dtx->te_oid,
						  dtx->te_ver, NULL);
			if (rc < 0)
				return rc;

			if (rc == 0) {
				/* Inavailable for rebuild case. */
				if (intent == DAOS_INTENT_REBUILD)
					return hidden ? ALB_AVAILABLE_CLEAN :
						ALB_UNAVAILABLE;

				/* Non-leader and non-rebuild case,
				 * return -DER_INPROGRESS, then the
				 * caller will retry the RPC with
				 * leader replica.
				 */
				return dtx_inprogress(dtx, 2);
			}

			/* For leader, non-committed DTX is unavailable. */
			return hidden ? ALB_AVAILABLE_CLEAN : ALB_UNAVAILABLE;
		}

		/* PUNCH DTX cannot be shared by others. */
		if (dtx->te_intent == DAOS_INTENT_PUNCH) {
			dtx_record_conflict(dth, dtx);

			return dtx_inprogress(dtx, 3);
		}

		/* Fall through. */
	}
	case DTX_ST_INIT:
		if (dtx->te_intent != DAOS_INTENT_UPDATE) {
			D_ERROR("Unexpected DTX intent %u\n", dtx->te_intent);
			return -DER_INVAL;
		}

		if ((intent == DAOS_INTENT_DEFAULT ||
		     intent == DAOS_INTENT_REBUILD) &&
		    dtx->te_state == DTX_ST_INIT)
			return hidden ? ALB_AVAILABLE_CLEAN : ALB_UNAVAILABLE;

		return vos_dtx_check_shares(dth, dtx, record, intent, type);
	default:
		D_ERROR("Unexpected DTX state %u\n", dtx->te_state);
		return -DER_INVAL;
	}
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, uint32_t flags)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_share		*vds;
	struct vos_dtx_share		*next;
	umem_off_t			 rec_umoff = UMOFF_NULL;
	umem_off_t			*entry = NULL;
	uint32_t			*shares = NULL;
	int				 rc = 0;
	bool				 shared = false;

	switch (type) {
	case DTX_RT_OBJ: {
		struct vos_obj_df	*obj;

		obj = umem_off2ptr(umm, record);
		entry = &obj->vo_dtx;
		if (dth == NULL || dth->dth_intent == DAOS_INTENT_UPDATE)
			shares = &obj->vo_dtx_shares;

		/* "flags == 0" means new created object. It is unnecessary
		 * to umem_tx_add_ptr() for new created object.
		 */
		if (flags != 0)
			umem_tx_add_ptr(umm, obj, sizeof(*obj));
		else if (dth != NULL)
			dth->dth_obj = record;
		break;
	}
	case DTX_RT_KEY: {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		entry = &key->kr_dtx;
		if (dth == NULL || dth->dth_intent == DAOS_INTENT_UPDATE)
			shares = &key->kr_dtx_shares;

		if (flags != 0)
			umem_tx_add_ptr(umm, key, sizeof(*key));
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df	*svt;

		svt = umem_off2ptr(umm, record);
		entry = &svt->ir_dtx;
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc		*evt;

		evt = umem_off2ptr(umm, record);
		entry = &evt->dc_dtx;
		break;
	}
	default:
		D_ERROR("Unknown DTX type %u\n", type);
		return -DER_INVAL;
	}

	if (dth == NULL) {
		*entry = UMOFF_NULL;
		if (shares != NULL)
			*shares = 0;

		return 0;
	}

	rec_umoff = umem_zalloc_off(umm, sizeof(struct vos_dtx_record_df));
	if (dtx_is_null(rec_umoff))
		return -DER_NOMEM;

	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_type = type;
	rec->tr_flags = flags;
	rec->tr_record = record;
	rec->tr_next = UMOFF_NULL;

	if (dtx_is_null(dth->dth_ent)) {
		D_ASSERT(flags == 0);

		rc = vos_dtx_alloc(umm, dth, rec_umoff, &dtx);
		if (rc != 0)
			return rc;
	} else {
		vos_dtx_append(umm, dth, rec_umoff, record, type, flags, &dtx);
	}

	*entry = dth->dth_ent;
	if (shares != NULL)
		*shares = 1;

	if (d_list_empty(&dth->dth_shares))
		return 0;

	d_list_for_each_entry_safe(vds, next, &dth->dth_shares, vds_link) {
		if (vds->vds_type == DTX_RT_OBJ)
			rc = vos_dtx_share_obj(umm, dth, dtx, vds, &shared);
		else
			rc = vos_dtx_share_key(umm, dth, dtx, vds, &shared);
		if (rc != 0)
			return rc;

		d_list_del(&vds->vds_link);
		D_FREE_PTR(vds);
	}

	if (rc == 0 && shared)
		dtx->te_flags |= DTX_EF_SHARES;

	return rc;
}

/* The caller has started PMDK transaction. */
void
vos_dtx_deregister_record(struct umem_instance *umm,
			  umem_off_t entry, umem_off_t record, uint32_t type)
{
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_record_df	*prev = NULL;
	umem_off_t			 rec_umoff;

	if (!vos_dtx_is_normal_entry(umm, entry))
		return;

	dtx = umem_off2ptr(umm, entry);
	rec_umoff = dtx->te_records;
	while (!dtx_is_null(rec_umoff)) {
		rec = umem_off2ptr(umm, rec_umoff);
		if (record != rec->tr_record) {
			prev = rec;
			rec_umoff = rec->tr_next;
			continue;
		}

		if (prev == NULL) {
			umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
			dtx->te_records = rec->tr_next;
		} else {
			umem_tx_add_ptr(umm, prev, sizeof(*prev));
			prev->tr_next = rec->tr_next;
		}

		umem_free_off(umm, rec_umoff);
		break;
	};

	/* The caller will destroy related OBJ/KEY/SVT/EVT record after
	 * deregistered the DTX record. So not reset DTX reference inside
	 * related OBJ/KEY/SVT/EVT record unless necessary.
	 */
	if (type == DTX_RT_OBJ) {
		struct vos_obj_df	*obj;

		obj = umem_off2ptr(umm, record);
		D_ASSERT(obj->vo_dtx == entry);

		umem_tx_add_ptr(umm, obj, sizeof(*obj));
		if (dtx->te_intent == DAOS_INTENT_UPDATE) {
			obj->vo_dtx_shares--;
			if (obj->vo_dtx_shares > 0)
				dtx_set_unknown(&obj->vo_dtx);
		}
	} else if (type == DTX_RT_KEY) {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		D_ASSERT(key->kr_dtx == entry);

		if (dtx->te_intent == DAOS_INTENT_UPDATE) {
			umem_tx_add_ptr(umm, key, sizeof(*key));
			key->kr_dtx_shares--;
			if (key->kr_dtx_shares > 0)
				dtx_set_unknown(&key->kr_dtx);
		}
	}
}

int
vos_dtx_prepared(struct dtx_handle *dth)
{
	struct vos_container	*cont;
	struct vos_dtx_entry_df	*dtx;
	int			 rc = 0;

	D_ASSERT(!dtx_is_null(dth->dth_ent));

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dtx = umem_off2ptr(&cont->vc_pool->vp_umm, dth->dth_ent);
	if (dth->dth_intent == DAOS_INTENT_UPDATE) {
		daos_iov_t	kiov;
		daos_iov_t	riov;

		/* There is CPU yield during the bulk transfer, then it is
		 * possible that some others (rebuild) abort this DTX by race.
		 * So we need to locate (or verify) DTX via its ID instead of
		 * directly using the dth_ent.
		 */
		daos_iov_set(&kiov, &dth->dth_xid, sizeof(struct dtx_id));
		daos_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc == -DER_NONEXIST)
			/* The DTX has been aborted by race, notify the RPC
			 * sponsor to retry via returning -DER_INPROGRESS.
			 */
			return dtx_inprogress(dtx, 1);

		if (rc != 0)
			return rc;

		D_ASSERT(dtx == (struct vos_dtx_entry_df *)riov.iov_buf);
	}

	/* The caller has already started the PMDK transaction
	 * and add the DTX into the PMDK transaction.
	 */
	dtx->te_state = DTX_ST_PREPARED;

	if (dth->dth_non_rep) {
		dth->dth_sync = 0;
		rc = vos_dtx_commit_one(cont, &dth->dth_xid);
		if (rc == 0)
			dth->dth_ent = UMOFF_NULL;
		else
			D_ERROR(DF_UOID" fail to commit for "DF_DTI" rc = %d\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid), rc);
	} else if (dtx->te_flags & DTX_EF_SHARES || dtx->te_dkey_hash == 0) {
		/* If some DTXs share something (object/key) with others,
		 * or punch object that is quite possible affect subsequent
		 * operations, then synchronously commit the DTX when it
		 * becomes committable to avoid availability trouble.
		 */
		dth->dth_sync = 1;
	}

	return rc;
}

void
vos_dtx_register_check_leader(int (*checker)(uuid_t, daos_unit_oid_t *,
			      uint32_t, struct pl_obj_layout **))
{
	vos_dtx_check_leader = checker;
}

int
vos_dtx_check_committable(daos_handle_t coh, daos_unit_oid_t *oid,
			  struct dtx_id *dti, uint64_t dkey_hash,
			  bool punch)
{
	struct vos_container	*cont;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (oid != NULL) {
		rc = vos_dtx_lookup_cos(coh, oid, dti, dkey_hash, punch);
		if (rc == 0)
			return DTX_ST_COMMITTED;
	}

	daos_iov_set(&kiov, dti, sizeof(*dti));
	daos_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == 0) {
		struct vos_dtx_entry_df	*dtx;

		dtx = (struct vos_dtx_entry_df *)riov.iov_buf;
		return dtx->te_state;
	}

	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0)
			return DTX_ST_COMMITTED;
	}

	return rc;
}

void
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int count)
{
	int	i;

	for (i = 0; i < count; i++)
		vos_dtx_commit_one(cont, &dtis[i]);
}

int
vos_dtx_commit(daos_handle_t coh, struct dtx_id *dtis, int count)
{
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Commit multiple DTXs via single PMDK transaction. */
	rc = vos_tx_begin(cont->vc_pool);
	if (rc == 0) {
		vos_dtx_commit_internal(cont, dtis, count);
		rc = vos_tx_end(cont->vc_pool, rc);
	}

	return rc;
}

int
vos_dtx_abort_internal(struct vos_container *cont, struct dtx_id *dtis,
		       int count, bool force)
{
	int	rc = 0;
	int	i;

	for (i = 0; rc == 0 && i < count; i++)
		rc = vos_dtx_abort_one(cont, &dtis[i], force);

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, struct dtx_id *dtis, int count, bool force)
{
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Abort multiple DTXs via single PMDK transaction. */
	rc = vos_tx_begin(cont->vc_pool);
	if (rc != 0)
		return rc;

	rc = vos_dtx_abort_internal(cont, dtis, count, force);

	return vos_tx_end(cont->vc_pool, rc);
}

int
vos_dtx_aggregate(daos_handle_t coh, uint64_t max, uint64_t age)
{
	struct vos_container		*cont;
	struct umem_instance		*umm;
	struct vos_dtx_table_df		*tab;
	umem_off_t			 dtx_umoff;
	uint64_t			 count;
	int				 rc = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	tab = &cont->vc_cont_df->cd_dtx_table_df;

	rc = vos_tx_begin(cont->vc_pool);
	if (rc != 0)
		return rc;

	umem_tx_add_ptr(umm, tab, sizeof(*tab));
	for (count = 0, dtx_umoff = tab->tt_entry_head;
	     count < max && !dtx_is_null(dtx_umoff); count++) {
		struct vos_dtx_entry_df	*dtx;
		daos_iov_t		 kiov;
		umem_off_t		 umoff;

		dtx = umem_off2ptr(umm, dtx_umoff);
		if (dtx_hlc_age2sec(dtx->te_sec) <= age)
			break;

		daos_iov_set(&kiov, &dtx->te_xid, sizeof(dtx->te_xid));
		rc = dbtree_delete(cont->vc_dtx_committed_hdl, &kiov, &umoff);
		D_ASSERT(rc == 0);

		tab->tt_count--;
		dtx_umoff = dtx->te_next;
		vos_dtx_unlink_entry(umm, tab, dtx);
		dtx_rec_release(umm, umoff, false, true);
	}

	vos_tx_end(cont->vc_pool, 0);
	return count < max ? 1 : 0;
}

void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat)
{
	struct vos_container		*cont;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	stat->dtx_committable_count = cont->vc_dtx_committable_count;
	stat->dtx_oldest_committable_time = vos_dtx_cos_oldest(cont);
	stat->dtx_committed_count = cont->vc_cont_df->cd_dtx_table_df.tt_count;
	if (!dtx_is_null(cont->vc_cont_df->cd_dtx_table_df.tt_entry_head)) {
		struct vos_dtx_entry_df	*dtx;

		dtx = umem_off2ptr(&cont->vc_pool->vp_umm,
			cont->vc_cont_df->cd_dtx_table_df.tt_entry_head);
		stat->dtx_oldest_committed_time = dtx->te_sec;
	} else {
		stat->dtx_oldest_committed_time = 0;
	}
}
