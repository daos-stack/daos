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
	return umem_off2flags(umoff) == DTX_UMOFF_ABORTED;
}

static inline bool
dtx_is_unknown(umem_off_t umoff)
{
	return umem_off2flags(umoff) == DTX_UMOFF_UNKNOWN;
}

static void
dtx_set_aborted(umem_off_t *umoff)
{
	umem_off_set_null_flags(umoff, DTX_UMOFF_ABORTED);
}

static void
dtx_set_unknown(umem_off_t *umoff)
{
	umem_off_set_null_flags(umoff, DTX_UMOFF_UNKNOWN);
}

static inline int
dtx_inprogress(struct vos_dtx_entry_df *dtx, int pos)
{
	if (dtx != NULL)
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX "DF_DTI" at %d\n",
			DP_DTI(&dtx->te_xid), pos);
	else
		D_DEBUG(DB_TRACE, "Hit uncommitted (unknown) DTX at %d\n", pos);

	return -DER_INPROGRESS;
}

static inline void
dtx_record_conflict(struct dtx_handle *dth, struct vos_dtx_entry_df *dtx)
{
	if (dth != NULL && dth->dth_conflict != NULL && dtx != NULL) {
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
dtx_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
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
dtx_active_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		     d_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_rec_bundle	*rbund;

	rbund = (struct dtx_rec_bundle *)val_iov->iov_buf;
	D_ASSERT(!dtx_is_null(rbund->trb_umoff));

	/* Directly reference the input addreass (in SCM). */
	rec->rec_off = rbund->trb_umoff;

	return 0;
}

static int
dtx_active_rec_free(struct btr_instance *tins, struct btr_record *rec,
		    void *args)
{
	D_ASSERT(!UMOFF_IS_NULL(rec->rec_off));

	if (args != NULL) {
		umem_off_t	*umoff;

		/* Return the record addreass (offset in SCM).
		 * The caller will release it after using.
		 */
		umoff = (umem_off_t *)args;
		*umoff = rec->rec_off;
		umem_tx_add_ptr(&tins->ti_umm, &rec->rec_off,
				sizeof(rec->rec_off));
		rec->rec_off = UMOFF_NULL;
	} else {
		/* This only can happen when the dtx entry is allocated but
		 * fail to be inserted into DTX table. Under such case, the
		 * new allocated dtx entry will be automatically freed when
		 * related PMDK transaction is aborted.
		 */
	}

	return 0;
}

static int
dtx_active_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		     d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_dtx_entry_df	*dtx;

	D_ASSERT(val_iov != NULL);

	dtx = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, dtx, sizeof(*dtx));
	return 0;
}

static int
dtx_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t dtx_active_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_active_rec_alloc,
	.to_rec_free	= dtx_active_rec_free,
	.to_rec_fetch	= dtx_active_rec_fetch,
	.to_rec_update	= dtx_rec_update,
};

struct dtx_committed_rec {
	struct dtx_id	dcr_xid;
	daos_epoch_t	dcr_epoch;
	d_list_t	dcr_link;
};

static int
dtx_committed_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
			d_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_container		*cont = tins->ti_priv;
	struct dtx_committed_rec	*dcr = val_iov->iov_buf;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dcr);
	d_list_add_tail(&dcr->dcr_link, &cont->vc_dtx_committed_list);
	cont->vc_dtx_committed_count++;

	return 0;
}

static int
dtx_committed_rec_free(struct btr_instance *tins, struct btr_record *rec,
		       void *args)
{
	struct vos_container		*cont = tins->ti_priv;
	struct dtx_committed_rec	*dcr;

	D_ASSERT(!UMOFF_IS_NULL(rec->rec_off));

	dcr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_list_del(&dcr->dcr_link);
	D_FREE(dcr);
	cont->vc_dtx_committed_count--;

	return 0;
}

static int
dtx_committed_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
			d_iov_t *key_iov, d_iov_t *val_iov)
{
	return 0;
}

static btr_ops_t dtx_committed_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_committed_rec_alloc,
	.to_rec_free	= dtx_committed_rec_free,
	.to_rec_fetch	= dtx_committed_rec_fetch,
	.to_rec_update	= dtx_rec_update,
};

int
vos_dtx_table_register(void)
{
	int	rc;

	rc = dbtree_class_register(VOS_BTR_DTX_ACTIVE_TABLE, 0,
				   &dtx_active_btr_ops);
	if (rc != 0) {
		D_ERROR("Failed to register DTX active dbtree: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(VOS_BTR_DTX_COMMITTED_TABLE, 0,
				   &dtx_committed_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX committed dbtree: %d\n", rc);

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
		 * during handling the source.
		 *
		 * For abort case, set its vo_dtx as aborted. The tgt
		 * record will be removed via VOS aggregation or other
		 * tools some time later.
		 */
		D_ASSERT(abort);

		dtx_set_aborted(&obj->vo_dtx);
		return;
	}

	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC) {
		D_ERROR(DF_UOID" with OBJ DTX ("DF_DTI") missed SRC flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

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
	if (!(tgt_obj->vo_oi_attr & VOS_OI_PUNCHED)) {
		D_ERROR(DF_UOID" with OBJ DTX ("DF_DTI") missed PUNCHED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	umem_tx_add_ptr(umm, tgt_obj, sizeof(*tgt_obj));

	/* The @tgt_obj which epoch is current DTX's epoch will be
	 * available to outside of VOS. Set its vo_earliest as @obj
	 * vo_earliest.
	 */
	tgt_obj->vo_tree = obj->vo_tree;
	tgt_obj->vo_earliest = obj->vo_earliest;
	tgt_obj->vo_latest = dtx->te_epoch;
	tgt_obj->vo_incarnation = obj->vo_incarnation;
	tgt_obj->vo_dtx = UMOFF_NULL;

	/* The @obj which epoch is MAX will be removed later. */
	memset(&obj->vo_tree, 0, sizeof(obj->vo_tree));
	obj->vo_latest = 0;
	obj->vo_earliest = DAOS_EPOCH_MAX;
	obj->vo_incarnation++; /* cache should be revalidated */
	obj->vo_dtx = UMOFF_NULL;

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
		if (dtx_is_null(obj->vo_dtx)) {
			/* Two possible cases:
			 *
			 * 1. It is the punch exchange target (with the
			 *    flag DTX_RF_EXCHANGE_TGT) that should has
			 *    been processed when handling the exchange
			 *    source.
			 *
			 * 2. It is the DTX record for creating the obj
			 *    that will be punched in the modification.
			 *    The flag is zero under such case.
			 */
			if (rec->tr_flags == 0 && abort)
				dtx_set_aborted(&obj->vo_dtx);
		} else if (obj->vo_dtx != umoff) {
			/* Because PUNCH cannot share with others, the vo_dtx
			 * should reference current DTX.
			 */
			D_ERROR("The OBJ "DF_UOID" should referece DTX "
				DF_DTI", but referenced "UMOFF_PF" by wrong.\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				UMOFF_P(obj->vo_dtx));
		} else {
			dtx_obj_rec_exchange(umm, obj, dtx, rec, abort);
		}

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
		 * during handling the source.
		 *
		 * For abort case, set its kr_dtx as aborted. The tgt
		 * record will be removed via VOS aggregation or other
		 * tools some time later.
		 */
		D_ASSERT(abort);

		dtx_set_aborted(&key->kr_dtx);
		return;
	}

	/* For the case of DAOS_EPOCH_MAX does not exist when punch. */
	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC) {
		if (abort)
			dtx_set_aborted(&key->kr_dtx);
		else
			key->kr_dtx = UMOFF_NULL;
		return;
	}

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
	if (!(tgt_key->kr_bmap & KREC_BF_PUNCHED)) {
		D_ERROR(DF_UOID" with KEY DTX ("DF_DTI") missed PUNCHED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	umem_tx_add_ptr(umm, tgt_key, sizeof(*tgt_key));

	tgt_key->kr_bmap |= (key->kr_bmap & KREC_BF_DKEY);
	if (key->kr_bmap & KREC_BF_EVT) {
		tgt_key->kr_bmap |= KREC_BF_EVT;
		tgt_key->kr_evt = key->kr_evt;
		/* The @key which epoch is MAX will be removed later. */
		memset(&key->kr_evt, 0, sizeof(key->kr_evt));
	} else {
		D_ASSERT(key->kr_bmap & KREC_BF_BTR);

		tgt_key->kr_bmap |= KREC_BF_BTR;
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
	tgt_key->kr_dtx = UMOFF_NULL;

	key->kr_latest = 0;
	key->kr_earliest = DAOS_EPOCH_MAX;
	key->kr_dtx = UMOFF_NULL;

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
		if (dtx_is_null(key->kr_dtx)) {
			if (rec->tr_flags != DTX_RF_EXCHANGE_TGT)
				D_ERROR("The KEY ("UMOFF_PF") "DF_UOID
					" should referece DTX "DF_DTI
					", but lost punch flag (%x).\n",
					UMOFF_P(umem_ptr2off(umm, rec)),
					DP_UOID(dtx->te_oid),
					DP_DTI(&dtx->te_xid), rec->tr_flags);

			D_ASSERT(!abort);
		} else if (key->kr_dtx != umoff) {
			/* Because PUNCH cannot share with others, the kr_dtx
			 * should reference current DTX.
			 */
			D_ERROR("The KEY "DF_UOID" should referece DTX "
				DF_DTI", but referenced "UMOFF_PF" by wrong.\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				UMOFF_P(key->kr_dtx));
		} else {
			dtx_key_rec_exchange(umm, key, dtx, rec, abort);
		}

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
	while (!dtx_is_null(dtx->te_records)) {
		umem_off_t			 rec_umoff = dtx->te_records;
		struct vos_dtx_record_df	*rec;

		rec = umem_off2ptr(umm, rec_umoff);
		switch (rec->tr_type) {
		case DTX_RT_OBJ: {
			struct vos_obj_df	*obj;

			obj = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &obj->vo_dtx,
					VOS_OBJ_SIZE_PARTIAL);
			dtx_obj_rec_release(umm, obj, rec, umoff, abort);
			break;
		}
		case DTX_RT_KEY: {
			struct vos_krec_df	*key;

			key = umem_off2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &key->kr_dtx,
					VOS_KEY_SIZE_PARTIAL);
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
		umem_free(umm, rec_umoff);
	}

	if (destroy)
		umem_free(umm, umoff);
	else
		dtx->te_flags &= ~(DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES);
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct dtx_id *dti,
		   umem_off_t umoff)
{
	struct umem_instance		*umm = &cont->vc_pool->vp_umm;
	struct dtx_committed_rec	*dcr = NULL;
	struct vos_dtx_entry_df		*dtx;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;
	bool				 drop_cos = false;

	d_iov_set(&kiov, dti, sizeof(*dti));
	if (dtx_is_null(umoff)) {
		rc = dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
				   &kiov, &umoff);
		if (rc == -DER_NONEXIST) {
			d_iov_set(&riov, NULL, 0);
			rc = dbtree_lookup(cont->vc_dtx_committed_hdl,
					   &kiov, NULL);
			goto out;
		}

		if (rc != 0)
			goto out;

		drop_cos = true;
	}

	dtx = umem_off2ptr(umm, umoff);

	D_ALLOC_PTR(dcr);
	if (dcr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dcr->dcr_xid = *dti;
	dcr->dcr_epoch = dtx->te_epoch;

	d_iov_set(&riov, dcr, sizeof(*dcr));
	rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc != 0)
		goto out;

	/* XXX: Only mark the DTX as DTX_ST_COMMITTED (when commit) is not
	 *	enough. Otherwise, some subsequent modification may change
	 *	related data record's DTX reference or remove related data
	 *	record as to the current DTX will have invalid reference(s)
	 *	via its DTX record(s).
	 */
	dtx_rec_release(umm, umoff, false, true);
	if (drop_cos)
		/* For single participator case, the DTX is not in COS cache. */
		vos_dtx_del_cos(cont, &dtx->te_oid, dti, dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);

out:
	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = %d\n",
		DP_DTI(dti), rc);
	if (rc != 0)
		D_FREE(dcr);

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, daos_epoch_t epoch,
		  struct dtx_id *dti)
{
	d_iov_t			kiov;
	umem_off_t		off;
	dbtree_probe_opc_t	opc = BTR_PROBE_EQ;
	int			rc;

	d_iov_set(&kiov, dti, sizeof(*dti));
	if (epoch != 0) {
		struct vos_dtx_entry_df	*dtx;
		d_iov_t			 riov;

		d_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc != 0)
			goto out;

		dtx = (struct vos_dtx_entry_df *)riov.iov_buf;
		if (dtx->te_epoch > epoch)
			D_GOTO(out, rc = -DER_NONEXIST);

		opc = BTR_PROBE_BYPASS;
	}

	rc = dbtree_delete(cont->vc_dtx_active_hdl, opc, &kiov, &off);
	if (rc == 0)
		dtx_rec_release(&cont->vc_pool->vp_umm, off, true, true);

out:
	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = %d\n", DP_DTI(dti), rc);

	return rc;
}

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
	      struct vos_dtx_entry_df **dtxp)
{
	struct vos_dtx_entry_df	*dtx;
	struct vos_container	*cont;
	umem_off_t		 dtx_umoff;
	struct dtx_rec_bundle	 rbund;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dth->dth_gen = cont->vc_dtx_resync_gen;

	dtx_umoff = umem_zalloc(umm, sizeof(struct vos_dtx_entry_df));
	if (dtx_is_null(dtx_umoff))
		return -DER_NOSPACE;

	dtx = umem_off2ptr(umm, dtx_umoff);
	dtx->te_xid = dth->dth_xid;
	dtx->te_oid = dth->dth_oid;
	dtx->te_dkey_hash = dth->dth_dkey_hash;
	dtx->te_epoch = dth->dth_epoch;
	dtx->te_ver = dth->dth_ver;
	dtx->te_flags = dth->dth_leader ? DTX_EF_LEADER : 0;
	dtx->te_intent = dth->dth_intent;
	dtx->te_gen = dth->dth_gen;
	dtx->te_records = UMOFF_NULL;

	/* For single participator case, the DTX will be committed immediately
	 * just after the local modification done. So it is unnecessary to add
	 * the DTX into the active DTX table and be remove very soon, instead,
	 * it will be directly inserted into committed DTX table when commit.
	 */
	if (!dth->dth_single_participator) {
		d_iov_t		kiov;
		d_iov_t		riov;
		int		rc;

		rbund.trb_umoff = dtx_umoff;
		d_iov_set(&riov, &rbund, sizeof(rbund));
		d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
		rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
				   DAOS_INTENT_UPDATE, &kiov, &riov);
		if (rc != 0)
			return rc;
	}

	dth->dth_ent = dtx_umoff;
	*dtxp = dtx;

	return 0;
}

static int
vos_dtx_append(struct umem_instance *umm, struct dtx_handle *dth,
	       umem_off_t record, uint32_t type, uint32_t flags,
	       struct vos_dtx_entry_df **dtxp)
{
	umem_off_t			 rec_umoff;
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_record_df	*tgt;

	/* The @dtx must be new created via former
	 * vos_dtx_register_record(), no need umem_tx_add_ptr().
	 */
	dtx = umem_off2ptr(umm, dth->dth_ent);

	rec_umoff = umem_zalloc(umm, sizeof(struct vos_dtx_record_df));
	if (dtx_is_null(rec_umoff))
		return -DER_NOSPACE;

	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_type = type;
	rec->tr_flags = flags;
	rec->tr_record = record;
	rec->tr_next = dtx->te_records;

	dtx->te_records = rec_umoff;
	*dtxp = dtx;

	if (flags == 0)
		return 0;

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
		umem_tx_add_ptr(umm, &obj->vo_oi_attr, sizeof(obj->vo_oi_attr));
		obj->vo_oi_attr |= VOS_OI_REMOVED;
	} else {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		umem_tx_add_ptr(umm, &key->kr_bmap, sizeof(key->kr_bmap));
		key->kr_bmap |= KREC_BF_REMOVED;
	}

	D_DEBUG(DB_TRACE, "Register exchange source for %s DTX "DF_DTI"\n",
		type == DTX_RT_OBJ ? "OBJ" : "KEY", DP_DTI(&dtx->te_xid));

	return 0;
}

static int
vos_dtx_append_share(struct umem_instance *umm, struct vos_dtx_entry_df *dtx,
		     struct dtx_share *dts)
{
	struct vos_dtx_record_df	*rec;
	umem_off_t			 rec_umoff;

	rec_umoff = umem_zalloc(umm, sizeof(struct vos_dtx_record_df));
	if (dtx_is_null(rec_umoff))
		return -DER_NOSPACE;

	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_type = dts->dts_type;
	rec->tr_flags = 0;
	rec->tr_record = dts->dts_record;

	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_umoff;

	return 0;
}

static int
vos_dtx_share_obj(struct umem_instance *umm, struct dtx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct dtx_share *dts,
		  bool *shared)
{
	struct vos_obj_df	*obj;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	obj = umem_off2ptr(umm, dts->dts_record);
	dth->dth_obj = dts->dts_record;
	/* The to be shared obj has been committed. */
	if (dtx_is_null(obj->vo_dtx))
		return 0;

	rc = vos_dtx_append_share(umm, dtx, dts);
	if (rc != 0) {
		D_ERROR("The DTX "DF_DTI" failed to shares obj "
			"with others:: rc = %d\n",
			DP_DTI(&dth->dth_xid), rc);
		return rc;
	}

	umem_tx_add_ptr(umm, &obj->vo_dtx_shares, sizeof(obj->vo_dtx_shares));

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
		umem_tx_add_ptr(umm, &obj->vo_dtx, sizeof(obj->vo_dtx));
		obj->vo_dtx = dth->dth_ent;
		return 0;
	}

	D_ASSERT(vos_dtx_is_normal_entry(umm, obj->vo_dtx));

	sh_dtx = umem_off2ptr(umm, obj->vo_dtx);
	D_ASSERT(dtx != sh_dtx);

	umem_tx_add_ptr(umm, &sh_dtx->te_flags, sizeof(sh_dtx->te_flags));
	sh_dtx->te_flags |= DTX_EF_SHARES;

	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares obj "DF_X64
		" with other DTX "DF_DTI", the shares count %u\n",
		DP_DTI(&dth->dth_xid), dts->dts_record,
		DP_DTI(&sh_dtx->te_xid), obj->vo_dtx_shares);

	return 0;
}

static int
vos_dtx_share_key(struct umem_instance *umm, struct dtx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct dtx_share *dts,
		  bool *shared)
{
	struct vos_krec_df	*key;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	key = umem_off2ptr(umm, dts->dts_record);
	/* The to be shared key has been committed. */
	if (dtx_is_null(key->kr_dtx))
		return 0;

	rc = vos_dtx_append_share(umm, dtx, dts);
	if (rc != 0) {
		D_ERROR("The DTX "DF_DTI" failed to shares key "
			"with others:: rc = %d\n",
			DP_DTI(&dth->dth_xid), rc);
		return rc;
	}

	umem_tx_add_ptr(umm, &key->kr_dtx_shares, sizeof(key->kr_dtx_shares));

	/* The to be shared key has been aborted, reuse it. */
	if (dtx_is_aborted(key->kr_dtx)) {
		D_ASSERTF(key->kr_dtx_shares == 0,
			  "Invalid shared key DTX shares %d\n",
			  key->kr_dtx_shares);
		key->kr_dtx_shares = 1;
		return 0;
	}

	key->kr_dtx_shares++;
	*shared = true;

	/* The original key DTX has been aborted, but some others
	 * still share the key. Then set the kr_dtx to current DTX.
	 */
	if (dtx_is_unknown(key->kr_dtx)) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" shares key "
			"with unknown DTXs, shares count %u.\n",
			DP_DTI(&dth->dth_xid), key->kr_dtx_shares);
		umem_tx_add_ptr(umm, &key->kr_dtx, sizeof(key->kr_dtx));
		key->kr_dtx = dth->dth_ent;
		return 0;
	}

	D_ASSERT(vos_dtx_is_normal_entry(umm, key->kr_dtx));

	sh_dtx = umem_off2ptr(umm, key->kr_dtx);
	D_ASSERT(dtx != sh_dtx);

	umem_tx_add_ptr(umm, &sh_dtx->te_flags, sizeof(sh_dtx->te_flags));
	sh_dtx->te_flags |= DTX_EF_SHARES;

	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares key "DF_X64
		" with other DTX "DF_DTI", the shares count %u\n",
		DP_DTI(&dth->dth_xid), dts->dts_record,
		DP_DTI(&sh_dtx->te_xid), key->kr_dtx_shares);

	return 0;
}

static int
vos_dtx_check_shares(struct umem_instance *umm, daos_handle_t coh,
		     struct dtx_handle *dth, struct vos_dtx_entry_df *dtx,
		     umem_off_t record, uint32_t intent, uint32_t type,
		     umem_off_t *addr)
{
	struct dtx_share	*dts;

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

	/* Here, if the obj/key has 'prepared' DTX, but current @dth is NULL,
	 * then it is the race case between normal IO and rebuild: the normal
	 * IO creates the obj/key before the rebuild request being handled.
	 *
	 * Under such case, we should (partially) commit the normal DTX with
	 * the shared target (obj/key) to guarantee the rebuild can go ahead.
	 */
	if (dth == NULL) {
		struct vos_container	*cont = vos_hdl2cont(coh);
		int			 rc;

		D_ASSERT(addr != NULL);

		rc = vos_tx_begin(cont->vc_pool);
		if (rc == 0) {
			umem_tx_add_ptr(umm, addr, sizeof(*addr));
			*addr = UMOFF_NULL;
			rc = vos_tx_end(cont->vc_pool, 0);
		}
		if (rc != 0)
			return rc;

		return ALB_AVAILABLE_CLEAN;
	}

	D_ALLOC_PTR(dts);
	if (dts == NULL)
		return -DER_NOMEM;

	dts->dts_type = type;
	dts->dts_record = record;
	d_list_add_tail(&dts->dts_link, &dth->dth_shares);

	return ALB_AVAILABLE_CLEAN;
}

int
vos_dtx_check_availability(struct umem_instance *umm, daos_handle_t coh,
			   umem_off_t entry, umem_off_t record, uint32_t intent,
			   uint32_t type)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx = NULL;
	umem_off_t			*addr = NULL;
	int				 rc;
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

		addr = &obj->vo_dtx;
		if (obj->vo_oi_attr & VOS_OI_REMOVED)
			hidden = true;
		break;
	}
	case DTX_RT_KEY: {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		addr = &key->kr_dtx;
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

	if (intent == DAOS_INTENT_CHECK || intent == DAOS_INTENT_COS) {
		if (dtx_is_aborted(entry))
			return ALB_UNAVAILABLE;

		if (dtx_is_null(entry) && hidden)
			return ALB_UNAVAILABLE;

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

		return vos_dtx_check_shares(umm, coh, dth, NULL, record, intent,
					    type, addr);
	}

	/* The DTX owner can always see the DTX. */
	if (dth != NULL && entry == dth->dth_ent)
		return ALB_AVAILABLE_CLEAN;

	dtx = umem_off2ptr(umm, entry);
	rc = vos_dtx_lookup_cos(coh, &dtx->te_oid, &dtx->te_xid,
			dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);
	if (rc == 0) {
		/* XXX: For the committable punch DTX, if there is pending
		 *	exchange (of sub-trees) operation, then do it, otherwise
		 *	the subsequent fetch cannot get the proper sub-trees.
		 */
		if (dtx->te_flags & DTX_EF_EXCHANGE_PENDING) {
			struct vos_container	*cont = vos_hdl2cont(coh);

			rc = vos_tx_begin(cont->vc_pool);
			if (rc == 0) {
				umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
				dtx_rec_release(umm, entry, false, false);
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

	if (intent == DAOS_INTENT_DEFAULT || intent == DAOS_INTENT_REBUILD) {
		if (!(dtx->te_flags & DTX_EF_LEADER) ||
		    DAOS_FAIL_CHECK(DAOS_VOS_NON_LEADER)) {
			/* Inavailable for rebuild case. */
			if (intent == DAOS_INTENT_REBUILD)
				return hidden ? ALB_AVAILABLE_CLEAN :
					ALB_UNAVAILABLE;

			/* Non-leader and non-rebuild case, return
			 * -DER_INPROGRESS, then the caller will retry
			 *  the RPC with leader replica.
			 */
			return dtx_inprogress(dtx, 2);
		}

		/* For leader, non-committed DTX is unavailable. */
		return hidden ? ALB_AVAILABLE_CLEAN : ALB_UNAVAILABLE;
	}

	/* PUNCH DTX cannot be shared by others. */
	if (dtx->te_intent == DAOS_INTENT_PUNCH) {
		if (dth == NULL)
			/* XXX: For rebuild case, if some normal IO
			 *	has generated punch-record (by race)
			 *	before rebuild logic handling that,
			 *	then rebuild logic should ignore such
			 *	punch-record, because the punch epoch
			 *	will be higher than the rebuild epoch.
			 *	The rebuild logic needs to create the
			 *	original target record that exists on
			 *	other healthy replicas before punch.
			 */
			return ALB_UNAVAILABLE;

		dtx_record_conflict(dth, dtx);

		return dtx_inprogress(dtx, 3);
	}

	if (dtx->te_intent != DAOS_INTENT_UPDATE) {
		D_ERROR("Unexpected DTX intent %u\n", dtx->te_intent);
		return -DER_INVAL;
	}

	return vos_dtx_check_shares(umm, coh, dth, dtx, record, intent,
				    type, addr);
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, uint32_t flags)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx;
	struct dtx_share		*dts;
	struct dtx_share		*next;
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
		if (flags == 0 && dth != NULL)
			dth->dth_obj = record;
		break;
	}
	case DTX_RT_KEY: {
		struct vos_krec_df	*key;

		key = umem_off2ptr(umm, record);
		entry = &key->kr_dtx;
		if (dth == NULL || dth->dth_intent == DAOS_INTENT_UPDATE)
			shares = &key->kr_dtx_shares;
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

	if (dtx_is_null(dth->dth_ent)) {
		D_ASSERT(flags == 0);

		rc = vos_dtx_alloc(umm, dth, &dtx);
		if (rc != 0)
			return rc;
	}

	/* For single participator case, we only need the DTX entry
	 * without DTX records for related targets to be modified.
	 */
	if (dth->dth_single_participator) {
		*entry = UMOFF_NULL;
		if (shares != NULL)
			*shares = 0;

		return 0;
	}

	rc = vos_dtx_append(umm, dth, record, type, flags, &dtx);
	if (rc != 0)
		return rc;

	*entry = dth->dth_ent;
	if (shares != NULL)
		*shares = 1;

	if (d_list_empty(&dth->dth_shares))
		return 0;

	d_list_for_each_entry_safe(dts, next, &dth->dth_shares, dts_link) {
		if (dts->dts_type == DTX_RT_OBJ)
			rc = vos_dtx_share_obj(umm, dth, dtx, dts, &shared);
		else
			rc = vos_dtx_share_key(umm, dth, dtx, dts, &shared);
		if (rc != 0)
			return rc;

		d_list_del(&dts->dts_link);
		D_FREE_PTR(dts);
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
		if (record == rec->tr_record) {
			if (prev == NULL) {
				umem_tx_add_ptr(umm, &dtx->te_records,
						sizeof(dtx->te_records));
				dtx->te_records = rec->tr_next;
			} else {
				umem_tx_add_ptr(umm, &prev->tr_next,
						sizeof(prev->tr_next));
				prev->tr_next = rec->tr_next;
			}

			umem_free(umm, rec_umoff);
			break;
		}

		prev = rec;
		rec_umoff = rec->tr_next;
	}

	if (dtx_is_null(rec_umoff))
		return;

	/* The caller will destroy related OBJ/KEY/SVT/EVT record after
	 * deregistered the DTX record. So not reset DTX reference inside
	 * related OBJ/KEY/SVT/EVT record unless necessary.
	 */
	if (type == DTX_RT_OBJ) {
		struct vos_obj_df	*obj;

		obj = umem_off2ptr(umm, record);
		D_ASSERT(obj->vo_dtx == entry);

		umem_tx_add_ptr(umm, &obj->vo_dtx, VOS_OBJ_SIZE_PARTIAL);
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
			umem_tx_add_ptr(umm, &key->kr_dtx,
					VOS_KEY_SIZE_PARTIAL);
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
	/* The caller has already started the PMDK transaction
	 * and add the DTX into the PMDK transaction.
	 */

	if (dth->dth_single_participator) {
		dth->dth_sync = 0;
		rc = vos_dtx_commit_one(cont, &dth->dth_xid, dth->dth_ent);
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

static int
do_vos_dtx_check(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t *epoch)
{
	struct vos_container	*cont;
	struct vos_dtx_entry_df	*dtx;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == 0) {
		dtx = (struct vos_dtx_entry_df *)riov.iov_buf;
		if (epoch != NULL) {
			if (*epoch != 0 && *epoch != dtx->te_epoch)
				return -DER_MISMATCH;

			*epoch = dtx->te_epoch;
		}

		return DTX_ST_PREPARED;
	}

	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, NULL);
		if (rc == 0)
			return DTX_ST_COMMITTED;
	}

	return rc;
}

int
vos_dtx_check_resend(daos_handle_t coh, daos_unit_oid_t *oid,
		     struct dtx_id *xid, uint64_t dkey_hash,
		     bool punch, daos_epoch_t *epoch)
{
	int	rc;

	rc = vos_dtx_lookup_cos(coh, oid, xid, dkey_hash, punch);
	if (rc == 0)
		return DTX_ST_COMMITTED;

	if (rc != -DER_NONEXIST)
		return rc;

	return do_vos_dtx_check(coh, xid, epoch);
}

int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti)
{
	return do_vos_dtx_check(coh, dti, NULL);
}

void
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int count)
{
	int	i;

	for (i = 0; i < count; i++)
		vos_dtx_commit_one(cont, &dtis[i], UMOFF_NULL);
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
		vos_tx_end(cont->vc_pool, 0);
	}

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, daos_epoch_t epoch, struct dtx_id *dtis,
	      int count)
{
	struct vos_container	*cont;
	int			 rc;
	int			 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Abort multiple DTXs via single PMDK transaction. */
	rc = vos_tx_begin(cont->vc_pool);
	if (rc == 0) {
		for (i = 0; rc == 0 && i < count; i++)
			vos_dtx_abort_one(cont, epoch, &dtis[i]);

		vos_tx_end(cont->vc_pool, 0);
	}

	return rc;
}

int
vos_dtx_aggregate(daos_handle_t coh, uint64_t max, uint64_t age)
{
	struct vos_container	*cont;
	int			 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	for (i = 0;
	     i < max && !d_list_empty(&cont->vc_dtx_committed_list); i++) {
		struct dtx_committed_rec	*dcr;
		d_iov_t				 kiov;

		dcr = d_list_entry(cont->vc_dtx_committed_list.next,
				   struct dtx_committed_rec, dcr_link);
		if (dtx_hlc_age2sec(dcr->dcr_epoch) < age)
			break;

		d_iov_set(&kiov, &dcr->dcr_xid, sizeof(dcr->dcr_xid));
		dbtree_delete(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			      &kiov, NULL);
	}

	return i < max ? 1 : 0;
}

void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat)
{
	struct vos_container		*cont;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	stat->dtx_committable_count = cont->vc_dtx_committable_count;
	stat->dtx_oldest_committable_time = vos_dtx_cos_oldest(cont);
	stat->dtx_committed_count = cont->vc_dtx_committed_count;
	if (d_list_empty(&cont->vc_dtx_committed_list)) {
		stat->dtx_oldest_committed_time = 0;
	} else {
		struct dtx_committed_rec	*dcr;

		dcr = d_list_entry(cont->vc_dtx_committed_list.next,
				   struct dtx_committed_rec, dcr_link);
		stat->dtx_oldest_committed_time = dcr->dcr_epoch;
	}
}

int
vos_dtx_mark_sync(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch)
{
	struct vos_container	*cont;
	struct daos_lru_cache	*occ;
	struct vos_object	*obj;
	int	rc;

	cont = vos_hdl2cont(coh);
	occ = vos_obj_cache_current();
	rc = vos_obj_hold(occ, cont, oid, epoch, true,
			  DAOS_INTENT_DEFAULT, &obj);
	if (rc != 0) {
		D_ERROR(DF_UOID" fail to mark sync(1): rc = %d\n",
			DP_UOID(oid), rc);
		return rc;
	}

	if (obj->obj_df != NULL && obj->obj_df->vo_sync < epoch) {
		rc = vos_tx_begin(cont->vc_pool);
		if (rc == 0) {
			umem_tx_add_ptr(&cont->vc_pool->vp_umm,
					&obj->obj_df->vo_sync,
					sizeof(obj->obj_df->vo_sync));
			obj->obj_df->vo_sync = epoch;
			rc = vos_tx_end(cont->vc_pool, rc);
		}

		if (rc == 0) {
			D_INFO("Update sync epoch "DF_U64" => "DF_U64
			       " for the obj "DF_UOID"\n",
			       obj->obj_sync_epoch, epoch, DP_UOID(oid));
			obj->obj_sync_epoch = epoch;
		} else {
			D_ERROR(DF_UOID" fail to mark sync(2): rc = %d\n",
				DP_UOID(oid), rc);
		}
	}

	vos_obj_release(occ, obj);
	return rc;
}
