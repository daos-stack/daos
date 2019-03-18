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

#define DTX_UMMID_ABORTED	((umem_id_t){ .pool_uuid_lo = -1, .off = -1, })
#define DTX_UMMID_UNKNOWN	((umem_id_t){ .pool_uuid_lo = -1, .off = -2, })

struct dtx_rec_bundle {
	umem_id_t	trb_ummid;
};

static int
dtx_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct daos_tx_id);
}

static void
dtx_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct daos_tx_id));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtx_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct daos_tx_id	*hkey1 = (struct daos_tx_id *)&rec->rec_hkey[0];
	struct daos_tx_id	*hkey2 = (struct daos_tx_id *)hkey;
	int			 rc;

	rc = memcmp(hkey1, hkey2, sizeof(struct daos_tx_id));

	return dbtree_key_cmp_rc(rc);
}

static int
dtx_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_rec_bundle	*rbund;

	rbund = (struct dtx_rec_bundle *)val_iov->iov_buf;
	D_ASSERT(!UMMID_IS_NULL(rbund->trb_ummid));

	/* Directly reference the input addreass (in SCM). */
	rec->rec_mmid = rbund->trb_ummid;
	return 0;
}

static int
dtx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_id_t	*ummid = (umem_id_t *)args;

	D_ASSERT(args != NULL);
	D_ASSERT(!UMMID_IS_NULL(rec->rec_mmid));

	/* Return the record addreass (in SCM). The caller will release it
	 * after using.
	 */
	*ummid = rec->rec_mmid;
	rec->rec_mmid = UMMID_NULL;
	return 0;
}

static int
dtx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_dtx_entry_df	*dtx;

	D_ASSERT(val_iov != NULL);

	dtx = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
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

	dtab_df->tt_time_last_shrink = time(NULL);
	dtab_df->tt_count = 0;
	dtab_df->tt_entry_head = UMMID_NULL;
	dtab_df->tt_entry_tail = UMMID_NULL;

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

	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC)
		return;

	if (!(obj->vo_oi_attr & VOS_OI_REMOVED)) {
		D_ERROR(DF_UOID" with OBJ DTX ("DF_DTI") missed REMOVED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	if (abort) { /* abort case */
		/* Recover the visibility of the exchange source. */
		obj->vo_oi_attr &= ~VOS_OI_REMOVED;
		obj->vo_dtx = UMMID_NULL;
		return;
	}

	/* XXX: If the exchange target still exist, it will be the next
	 *	record. If it does not exist, then either it is crashed
	 *	or it has already degistered from the DTX records list.
	 *	We cannot commit the DTX under any the two cases. Fail
	 *	the DTX commit is meaningless, then some warnings.
	 */
	if (UMMID_IS_NULL(rec->tr_next)) {
		D_ERROR(DF_UOID" miss OBJ DTX ("DF_DTI") exchange pairs (1)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	tgt_rec = umem_id2ptr(umm, rec->tr_next);
	if (tgt_rec->tr_flags != DTX_RF_EXCHANGE_TGT) {
		D_ERROR(DF_UOID" miss OBJ DTX ("DF_DTI") exchange pairs (2)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	/* Exchange the sub-tree between max epoch record and the given
	 * epoch record. The record with max epoch will be removed when
	 * aggregation or some special cleanup.
	 */
	tgt_obj = umem_id2ptr(umm, tgt_rec->tr_record);
	umem_tx_add_ptr(umm, tgt_obj, sizeof(*tgt_obj));

	/* The @tgt_obj which epoch is current DTX's epoch will be
	 * visibile to outside of VOS. Set its vo_earliest as @obj
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
		    struct vos_dtx_record_df *rec, umem_id_t ummid, bool abort)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_id2ptr(umm, ummid);
	if (dtx->te_intent == DAOS_INTENT_PUNCH) {
		/* Because PUNCH cannot share with others, the vo_dtx should
		 * reference current DTX.
		 */
		if (!umem_id_equal(umm, obj->vo_dtx, ummid))
			D_ERROR("The OBJ "DF_UOID" should referece DTX "
				DF_DTI", but referenced %s\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				UMMID_IS_NULL(obj->vo_dtx) ? "UNLL" : "other");
		dtx_obj_rec_exchange(umm, obj, dtx, rec, abort);
		return;
	}

	/* Because PUNCH and UPDATE cannot share, both current DTX
	 * and the obj former referenced DTX should be for UPDATE.
	 */
	obj->vo_dtx_shares--;
	if (abort) {
		/* Other DTX that shares the obj has been committed firstly.
		 * It must be for sharing of update. Subsequent modification
		 * may have seen the modification before current DTX commit
		 * or abort.
		 */
		if (UMMID_IS_NULL(obj->vo_dtx))
			return;

		if (obj->vo_dtx_shares == 0)
			/* The last shared UPDATE DTX is aborted. */
			obj->vo_dtx = DTX_UMMID_ABORTED;
		else if (umem_id_equal(umm, obj->vo_dtx, ummid))
			/* I am the original DTX that create the object that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			obj->vo_dtx = DTX_UMMID_UNKNOWN;
		return;
	}

	if (UMMID_IS_NULL(obj->vo_dtx)) {
		/* "vo_latest == 0" is for punched case. */
		if (obj->vo_latest < dtx->te_epoch && obj->vo_latest != 0)
			obj->vo_latest = dtx->te_epoch;

		/* "vo_earliest == DAOS_EPOCH_MAX" means that new created
		 * object has been punched before current DTX committed.
		 */
		if (obj->vo_earliest > dtx->te_epoch &&
		    obj->vo_earliest != DAOS_EPOCH_MAX)
			obj->vo_earliest = dtx->te_epoch;
	} else {
		/* I am the DTX that is committed firstly. The current DTX
		 * may be not the one that create the object. The DTX that
		 * create the object may be not committed or has been aborted.
		 * So vo_latest and vo_earliest may be different from current
		 * DTX. Set them as current DTX's epoch.
		 */
		obj->vo_dtx = UMMID_NULL;
		obj->vo_latest = dtx->te_epoch;
		obj->vo_earliest = dtx->te_epoch;
	}
}

static void
dtx_key_rec_exchange(struct umem_instance *umm, struct vos_krec_df *key,
		     struct vos_dtx_entry_df *dtx,
		     struct vos_dtx_record_df *rec, bool abort)
{
	struct vos_dtx_record_df	*tgt_rec;
	struct vos_krec_df		*tgt_key;

	if (rec->tr_flags != DTX_RF_EXCHANGE_SRC)
		return;

	if (!(key->kr_bmap & KREC_BF_REMOVED)) {
		D_ERROR(DF_UOID" with KEY DTX ("DF_DTI") missed REMOVED flag\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	if (abort) { /* abort case */
		/* Recover the visibility of the exchange source. */
		key->kr_bmap &= ~KREC_BF_REMOVED;
		key->kr_dtx = UMMID_NULL;
		return;
	}

	/* XXX: If the exchange target still exist, it will be the next
	 *	record. If it does not exist, then either it is crashed
	 *	or it has already degistered from the DTX records list.
	 *	We cannot commit the DTX under any the two cases. Fail
	 *	the DTX commit is meaningless, then some warnings.
	 */
	if (UMMID_IS_NULL(rec->tr_next)) {
		D_ERROR(DF_UOID" miss KEY DTX ("DF_DTI") exchange pairs (1)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	tgt_rec = umem_id2ptr(umm, rec->tr_next);
	if (tgt_rec->tr_flags != DTX_RF_EXCHANGE_TGT) {
		D_ERROR(DF_UOID" miss KEY DTX ("DF_DTI") exchange pairs (2)\n",
			DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid));
		return;
	}

	/* Exchange the sub-tree between max epoch record and the given
	 * epoch record. The record with max epoch will be removed when
	 * aggregation or some special cleanup.
	 */
	tgt_key = umem_id2ptr(umm, tgt_rec->tr_record);
	umem_tx_add_ptr(umm, tgt_key, sizeof(*tgt_key));

	if (key->kr_bmap & KREC_BF_EVT) {
		tgt_key->kr_evt = key->kr_evt;
		D_ASSERT(tgt_key->kr_bmap & KREC_BF_EVT);
		/* The @key which epoch is MAX will be removed later. */
		memset(&key->kr_evt, 0, sizeof(key->kr_evt));
	} else {
		D_ASSERT(tgt_key->kr_bmap & KREC_BF_BTR);
		tgt_key->kr_btr = key->kr_btr;
		/* The @key which epoch is MAX will be removed later. */
		memset(&key->kr_btr, 0, sizeof(key->kr_btr));
	}

	/* The @tgt_key which epoch is current DTX's epoch will be
	 * visibile to outside of VOS. Set its kr_earliest as @key
	 * kr_earliest.
	 */
	tgt_key->kr_earliest = key->kr_earliest;
	tgt_key->kr_latest = dtx->te_epoch;

	key->kr_latest = 0;
	key->kr_earliest = DAOS_EPOCH_MAX;

	D_DEBUG(DB_TRACE, "Exchanged KEY DTX records for "DF_DTI"\n",
		DP_DTI(&dtx->te_xid));
}

static void
dtx_key_rec_release(struct umem_instance *umm, struct vos_krec_df *key,
		    struct vos_dtx_record_df *rec, umem_id_t ummid, bool abort)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_id2ptr(umm, ummid);
	if (dtx->te_intent == DAOS_INTENT_PUNCH) {
		/* Because PUNCH cannot share with others, the kr_dtx should
		 * reference current DTX.
		 */
		if (!umem_id_equal(umm, key->kr_dtx, ummid))
			D_ERROR("The KEY "DF_UOID" should referece DTX "
				DF_DTI", but referenced %s\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				UMMID_IS_NULL(key->kr_dtx) ? "UNLL" : "other");
		dtx_key_rec_exchange(umm, key, dtx, rec, abort);
		return;
	}

	/* Because PUNCH and UPDATE cannot share, both current DTX
	 * and the key former referenced DTX should be for UPDATE.
	 */
	key->kr_dtx_shares--;
	if (abort) {
		/* Other DTX that shares the key has been committed firstly.
		 * It must be for sharing of update. Subsequent modification
		 * may have seen the modification before current DTX commit
		 * or abort.
		 */
		if (UMMID_IS_NULL(key->kr_dtx))
			return;

		if (key->kr_dtx_shares == 0)
			/* The last shared UPDATE DTX is aborted. */
			key->kr_dtx = DTX_UMMID_ABORTED;
		else if (umem_id_equal(umm, key->kr_dtx, ummid))
			/* I am the original DTX that create the key that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			key->kr_dtx = DTX_UMMID_UNKNOWN;
		return;
	}

	if (UMMID_IS_NULL(key->kr_dtx)) {
		/* "kr_latest == 0" is for punched case. */
		if (key->kr_latest < dtx->te_epoch && key->kr_latest != 0)
			key->kr_latest = dtx->te_epoch;

		/* "kr_earliest == DAOS_EPOCH_MAX" means that new created
		 * key has been punched before current DTX committed.
		 */
		if (key->kr_earliest > dtx->te_epoch &&
		    key->kr_earliest != DAOS_EPOCH_MAX)
			key->kr_earliest = dtx->te_epoch;
	} else {
		/* I am the DTX that is committed firstly. The current DTX
		 * may be not the one that create the key. The DTX that create
		 * the key may be not committed or has been aborted. So the
		 * kr_latest and kr_earliest may be different from current DTX.
		 * Set them as current DTX's epoch.
		 */
		key->kr_dtx = UMMID_NULL;
		key->kr_latest = dtx->te_epoch;
		key->kr_earliest = dtx->te_epoch;
	}
}

static int
dtx_rec_release(struct umem_instance *umm, umem_id_t ummid,
		bool abort, bool destroy)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_id2ptr(umm, ummid);
	umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	while (!UMMID_IS_NULL(dtx->te_records)) {
		umem_id_t			 rec_mmid = dtx->te_records;
		struct vos_dtx_record_df	*rec;

		rec = umem_id2ptr(umm, rec_mmid);
		switch (rec->tr_type) {
		case DTX_RT_OBJ: {
			struct vos_obj_df	*obj;

			obj = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, obj, sizeof(*obj));
			dtx_obj_rec_release(umm, obj, rec, ummid, abort);
			break;
		}
		case DTX_RT_KEY: {
			struct vos_krec_df	*key;

			key = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, key, sizeof(*key));
			dtx_key_rec_release(umm, key, rec, ummid, abort);
			break;
		}
		case DTX_RT_SVT: {
			struct vos_irec_df	*svt;

			svt = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &svt->ir_dtx, sizeof(svt->ir_dtx));
			if (abort)
				svt->ir_dtx = DTX_UMMID_ABORTED;
			else
				svt->ir_dtx = UMMID_NULL;
			break;
		}
		case DTX_RT_EVT: {
			struct evt_desc		*evt;

			evt = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, &evt->dc_dtx, sizeof(evt->dc_dtx));
			if (abort)
				evt->dc_dtx = DTX_UMMID_ABORTED;
			else
				evt->dc_dtx = UMMID_NULL;
			break;
		}
		default:
			D_ERROR(DF_UOID" unknown DTX "DF_DTI" type %u\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				rec->tr_type);
			break;
		}

		dtx->te_records = rec->tr_next;
		umem_tx_add_ptr(umm, rec, sizeof(*rec));
		umem_free(umm, rec_mmid);
	}

	D_DEBUG(DB_TRACE, "dtx_rec_release: %s/%s the DTX "DF_DTI"\n",
		abort ? "abort" : "commit", destroy ? "destroy" : "keep",
		DP_DTI(&dtx->te_xid));

	if (destroy)
		umem_free(umm, ummid);
	else
		dtx->te_flags &= ~(DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES);

	return 0;
}

static void
vos_dtx_unlink_entry(struct umem_instance *umm, struct vos_dtx_table_df *tab,
		     struct vos_dtx_entry_df *dtx)
{
	struct vos_dtx_entry_df	*ent;

	if (UMMID_IS_NULL(dtx->te_next)) { /* The tail of the DTXs list. */
		if (UMMID_IS_NULL(dtx->te_prev)) { /* The unique one on list. */
			tab->tt_entry_head = UMMID_NULL;
			tab->tt_entry_tail = UMMID_NULL;
		} else {
			ent = umem_id2ptr(umm, dtx->te_prev);
			umem_tx_add_ptr(umm, &ent->te_next,
					sizeof(ent->te_next));
			ent->te_next = UMMID_NULL;
			tab->tt_entry_tail = dtx->te_prev;
			dtx->te_prev = UMMID_NULL;
		}
	} else if (UMMID_IS_NULL(dtx->te_prev)) { /* The head of DTXs list */
		ent = umem_id2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, &ent->te_prev, sizeof(ent->te_prev));
		ent->te_prev = UMMID_NULL;
		tab->tt_entry_head = dtx->te_next;
		dtx->te_next = UMMID_NULL;
	} else {
		ent = umem_id2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, &ent->te_prev, sizeof(ent->te_prev));
		ent->te_prev = dtx->te_prev;

		ent = umem_id2ptr(umm, dtx->te_prev);
		umem_tx_add_ptr(umm, &ent->te_next, sizeof(ent->te_next));
		ent->te_next = dtx->te_next;

		dtx->te_prev = UMMID_NULL;
		dtx->te_next = UMMID_NULL;
	}
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct daos_tx_id *dti)
{
	struct umem_instance		*umm = &cont->vc_pool->vp_umm;
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_entry_df		*ent;
	struct vos_dtx_table_df		*tab;
	struct dtx_rec_bundle		 rbund;
	daos_iov_t			 kiov;
	daos_iov_t			 riov;
	umem_id_t			 ummid;
	int				 rc = 0;

	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &ummid);
	if (rc == -DER_NONEXIST) {
		daos_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		goto out;
	}

	if (rc != 0)
		goto out;

	dtx = umem_id2ptr(umm, ummid);
	umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	dtx->te_state = DTX_ST_COMMITTED;
	if (dtx->te_dkey_hash[0] != 0)
		vos_dtx_del_cos(cont, &dtx->te_oid, dti, dtx->te_dkey_hash[0],
				dtx->te_intent == DAOS_INTENT_PUNCH ?
				true : false);

	rbund.trb_ummid = ummid;
	daos_iov_set(&riov, &rbund, sizeof(rbund));
	rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc != 0)
		goto out;

	tab = &cont->vc_cont_df->cd_dtx_table_df;
	umem_tx_add_ptr(umm, tab, sizeof(*tab));

	tab->tt_count++;
	if (UMMID_IS_NULL(tab->tt_entry_tail)) {
		D_ASSERT(UMMID_IS_NULL(tab->tt_entry_head));

		tab->tt_entry_head = ummid;
		tab->tt_entry_tail = tab->tt_entry_head;
	} else {
		ent = umem_id2ptr(umm, tab->tt_entry_tail);
		umem_tx_add_ptr(umm, &ent->te_next, sizeof(ent->te_next));

		ent->te_next = ummid;
		dtx->te_prev = tab->tt_entry_tail;
		tab->tt_entry_tail = ent->te_next;
	}

	/* If there are pending exchange of records, then we need some
	 * additional work when commit, such as exchange the subtree
	 * under the source and target records, then related subtree
	 * can be exported correctly. That will be done when release
	 * related vos_dth_record_df(s) attached to the DTX.
	 */
	if (dtx->te_flags & (DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES))
		rc = dtx_rec_release(umm, ummid, false, false);

out:
	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = %d\n",
		DP_DTI(dti), rc);

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, struct daos_tx_id *dti,
		  bool force)
{
	daos_iov_t	 kiov;
	umem_id_t	 dtx;
	int		 rc;

	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &dtx);
	if (rc == 0)
		rc = dtx_rec_release(&cont->vc_pool->vp_umm, dtx, true, true);

	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = %d\n", DP_DTI(dti), rc);

	if (rc != 0 && force)
		rc = 0;

	return rc;
}

int
vos_dtx_begin(struct daos_tx_id *dti, daos_unit_oid_t *oid, daos_key_t *dkey,
	      daos_handle_t coh, daos_epoch_t epoch, uint32_t pm_ver,
	      uint32_t intent, uint32_t flags, struct daos_tx_handle **dtx)
{
	struct daos_tx_handle	*th;

	D_ALLOC_PTR(th);
	if (th == NULL)
		return -DER_NOMEM;

	th->dth_xid = *dti;
	th->dth_oid = *oid;
	th->dth_dkey = dkey;
	th->dth_coh = coh;
	th->dth_epoch = epoch;
	D_INIT_LIST_HEAD(&th->dth_shares);
	th->dth_ver = pm_ver;
	th->dth_intent = intent;
	th->dth_flags = flags;
	th->dth_sync = 0;
	th->dth_non_rep = 0;
	th->dth_ent = UMMID_NULL;

	*dtx = th;
	D_DEBUG(DB_TRACE, "Start the DTX "DF_DTI"\n", DP_DTI(dti));

	return 0;
}

int
vos_dtx_end(struct daos_tx_handle *dth, int result, bool leader)
{
	struct vos_container	*cont;
	struct vos_dtx_table_df	*tab;
	uint64_t		 now = time(NULL);

	if (result < 0)
		goto out;

	result = 0;
	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	if (leader) {
		if (dth->dth_sync)
			D_GOTO(out, result = DTX_ACT_COMMIT_SYNC);

		if (cont->vc_dtx_committable_count > DTX_THRESHOLD_COUNT)
			D_GOTO(out, result = DTX_ACT_COMMIT_ASYNC);

		if (now - cont->vc_dtx_time_last_commit >
		    DTX_COMMIT_THRESHOLD_TIME)
			D_GOTO(out, result = DTX_ACT_COMMIT_ASYNC);
	}

	tab = &cont->vc_cont_df->cd_dtx_table_df;
	if (tab->tt_count > DTX_AGGREGATION_THRESHOLD_COUNT)
		D_GOTO(out, result = DTX_ACT_AGGREGATE);

	if (now - tab->tt_time_last_shrink > DTX_AGGREGATION_THRESHOLD_TIME)
		D_GOTO(out, result = DTX_ACT_AGGREGATE);

out:
	D_DEBUG(DB_TRACE,
		"Stop the DTX "DF_DTI" ver = %u, %s, %s, %s: rc = %d\n",
		DP_DTI(&dth->dth_xid), dth->dth_ver,
		dth->dth_sync ? "sync" : "async",
		dth->dth_non_rep ? "non-replicated" : "replicated",
		dth->dth_leader ? "leader" : "non-leader", result);
	D_FREE_PTR(dth);
	return result;
}

int
vos_dtx_check_committable(daos_handle_t coh, daos_unit_oid_t *oid,
			  struct daos_tx_id *dti, uint64_t dkey_hash,
			  bool punch)
{
	struct vos_container	*cont;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (dkey_hash != 0) {
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

int
vos_dtx_commit(daos_handle_t coh, struct daos_tx_id *dtis, int count)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	/* Commit multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc == 0) {
		int	i;

		for (i = 0; i < count; i++)
			vos_dtx_commit_one(cont, &dtis[i]);

		cont->vc_dtx_time_last_commit = time(NULL);
		umem_tx_commit(umm);
	}

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, struct daos_tx_id *dtis, int count, bool force)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 rc;
	int			 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	/* Abort multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(umm, vos_txd_get());
	for (i = 0; rc == 0 && i < count; i++)
		rc = vos_dtx_abort_one(cont, &dtis[i], force);

	if (rc == 0)
		umem_tx_commit(umm);
	else
		umem_tx_abort(umm, rc);

	return rc;
}

int
vos_dtx_aggregate(daos_handle_t coh)
{
	struct vos_container		*cont;
	struct umem_instance		*umm;
	struct vos_dtx_table_df		*tab;
	umem_id_t			 dtx_mmid;
	uint64_t			 now = time(NULL);
	int				 count;
	int				 rc = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	tab = &cont->vc_cont_df->cd_dtx_table_df;

	if (tab->tt_count <= DTX_AGGREGATION_THRESHOLD_COUNT)
		return 0;

	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc != 0)
		return rc;

	umem_tx_add_ptr(umm, tab, sizeof(*tab));
	for (count = 0, dtx_mmid = tab->tt_entry_head;
	     count < DTX_AGGREGATION_YIELD_INTERVAL && !UMMID_IS_NULL(dtx_mmid);
	     count++) {
		struct vos_dtx_entry_df	*dtx;
		daos_iov_t		 kiov;
		umem_id_t		 ummid;

		dtx = umem_id2ptr(umm, dtx_mmid);
		if (tab->tt_count <= DTX_AGGREGATION_THRESHOLD_COUNT &&
		    now - dtx->te_sec <= DTX_AGGREGATION_THRESHOLD_TIME)
			break;

		umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
		daos_iov_set(&kiov, &dtx->te_xid, sizeof(dtx->te_xid));
		rc = dbtree_delete(cont->vc_dtx_committed_hdl, &kiov, &ummid);
		D_ASSERT(rc == 0);

		tab->tt_count--;
		dtx_mmid = dtx->te_next;
		vos_dtx_unlink_entry(umm, tab, dtx);
		dtx_rec_release(umm, ummid, false, true);
	}

	tab->tt_time_last_shrink = now;
	umem_tx_commit(umm);

	if (tab->tt_count > DTX_AGGREGATION_THRESHOLD_COUNT)
		return DTX_ACT_AGGREGATE;

	return 0;
}
