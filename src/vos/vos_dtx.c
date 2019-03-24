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

static inline int
dtx_inprogress(struct vos_dtx_entry_df *dtx, int pos)
{
	if (dtx != NULL)
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX "DF_DTI
			" with state %u, time %lu at %d\n",
			DP_DTI(&dtx->te_xid), dtx->te_state, dtx->te_sec, pos);
	else
		D_DEBUG(DB_TRACE, "Hit uncommitted (unknown) DTX at %d\n", pos);

	return -DER_INPROGRESS;
}

struct dtx_rec_bundle {
	umem_id_t	trb_ummid;
};

static int
dtx_hkey_size(void)
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
		umem_tx_add_ptr(umm, &tgt_key->kr_evt[0],
				sizeof(tgt_key->kr_evt[0]));
		tgt_key->kr_evt[0] = key->kr_evt[0];

		umem_tx_add_ptr(umm, &key->kr_evt[0],
				sizeof(key->kr_evt[0]));
		memset(&key->kr_evt[0], 0, sizeof(key->kr_evt[0]));
	}

	/* The @tgt_key which epoch is current DTX's epoch will be
	 * visibile to outside of VOS. Set its kr_earliest as @key
	 * kr_earliest.
	 */
	tgt_key->kr_btr = key->kr_btr;
	tgt_key->kr_earliest = key->kr_earliest;
	tgt_key->kr_latest = dtx->te_epoch;

	/* The @key which epoch is MAX will be removed later. */
	memset(&key->kr_btr, 0, sizeof(key->kr_btr));
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
	if (!destroy)
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

struct vos_dtx_share {
	/** Link into the daos_tx_handle::dth_shares */
	d_list_t		vds_link;
	/** The DTX record type, see enum vos_dtx_record_types. */
	uint32_t		vds_type;
	/** The record in the related tree in SCM. */
	umem_id_t		vds_record;
};

static bool
vos_dtx_is_normal_entry(struct umem_instance *umm, umem_id_t entry)
{
	if (UMMID_IS_NULL(entry) ||
	    umem_id_equal(umm, entry, DTX_UMMID_ABORTED) ||
	    umem_id_equal(umm, entry, DTX_UMMID_UNKNOWN))
		return false;

	return true;
}

static int
vos_dtx_alloc(struct umem_instance *umm, struct daos_tx_handle *dth,
	      umem_id_t rec_mmid, struct vos_dtx_entry_df **dtxp)
{
	daos_key_t		*dkey = dth->dth_dkey;
	struct vos_dtx_entry_df	*dtx;
	struct vos_container	*cont;
	umem_id_t		 dtx_mmid;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	struct dtx_rec_bundle	 rbund;
	int			 rc;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dtx_mmid = umem_zalloc(umm, sizeof(struct vos_dtx_entry_df));
	if (UMMID_IS_NULL(dtx_mmid))
		return -DER_NOMEM;

	dtx = umem_id2ptr(umm, dtx_mmid);
	dtx->te_xid = dth->dth_xid;
	dtx->te_oid = dth->dth_oid;
	if (dkey != NULL) {
		dtx->te_dkey_hash[0] = d_hash_murmur64(dkey->iov_buf,
						       dkey->iov_len,
						       VOS_BTR_MUR_SEED);
		dtx->te_dkey_hash[1] = d_hash_string_u32(dkey->iov_buf,
							 dkey->iov_len);
	} else {
		dtx->te_dkey_hash[0] = 0;
		dtx->te_dkey_hash[1] = 0;
	}
	dtx->te_epoch = dth->dth_epoch;
	dtx->te_ver = dth->dth_ver;
	dtx->te_state = DTX_ST_INIT;
	dtx->te_flags = 0;
	dtx->te_intent = dth->dth_intent;
	dtx->te_sec = time(NULL);
	dtx->te_records = rec_mmid;
	dtx->te_next = UMMID_NULL;
	dtx->te_prev = UMMID_NULL;

	rbund.trb_ummid = dtx_mmid;
	daos_iov_set(&riov, &rbund, sizeof(rbund));
	daos_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
	rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc == 0) {
		dth->dth_ent = dtx_mmid;
		*dtxp = dtx;
	}

	return rc;
}

static void
vos_dtx_append(struct umem_instance *umm, struct daos_tx_handle *dth,
	       umem_id_t rec_mmid, umem_id_t record, uint32_t type,
	       uint32_t flags, struct vos_dtx_entry_df **dtxp)
{
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_record_df	*tgt;

	/* The @dtx must be new created via former
	 * vos_dtx_register_record(), no need umem_tx_add_ptr().
	 */
	dtx = umem_id2ptr(umm, dth->dth_ent);
	dtx->te_sec = time(NULL);
	rec = umem_id2ptr(umm, rec_mmid);
	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_mmid;
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
	tgt = umem_id2ptr(umm, rec->tr_next);
	D_ASSERT(tgt->tr_flags == 0);

	tgt->tr_flags = DTX_RF_EXCHANGE_TGT;
	dtx->te_flags |= DTX_EF_EXCHANGE_PENDING;

	if (type == DTX_RT_OBJ) {
		struct vos_obj_df	*obj;

		obj = umem_id2ptr(umm, record);
		obj->vo_oi_attr |= VOS_OI_REMOVED;
	} else {
		struct vos_krec_df	*key;

		key = umem_id2ptr(umm, record);
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
	umem_id_t			 rec_mmid;

	rec_mmid = umem_zalloc(umm, sizeof(struct vos_dtx_record_df));
	if (UMMID_IS_NULL(rec_mmid))
		return -DER_NOMEM;

	rec = umem_id2ptr(umm, rec_mmid);
	rec->tr_type = vds->vds_type;
	rec->tr_flags = 0;
	rec->tr_record = vds->vds_record;

	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_mmid;

	return 0;
}

static int
vos_dtx_share_obj(struct umem_instance *umm, struct daos_tx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct vos_dtx_share *vds,
		  bool *shared)
{
	struct vos_obj_df	*obj;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	obj = umem_id2ptr(umm, vds->vds_record);
	/* The to be shared obj has been committed. */
	if (UMMID_IS_NULL(obj->vo_dtx))
		return 0;

	rc = vos_dtx_append_share(umm, dtx, vds);
	if (rc != 0)
		goto out;

	umem_tx_add_ptr(umm, obj, sizeof(*obj));

	/* The to be shared obj has been aborted, reuse it. */
	if (umem_id_equal(umm, obj->vo_dtx, DTX_UMMID_ABORTED)) {
		D_ASSERTF(obj->vo_dtx_shares == 0,
			  "Invalid shared obj DTX shares %d\n",
			  obj->vo_dtx_shares);
		obj->vo_dtx_shares = 1;
		goto out;
	}

	obj->vo_dtx_shares++;
	*shared = true;

	/* The original obj DTX has been aborted, but some others
	 * still share the obj. Then set the vo_dtx to current DTX.
	 */
	if (umem_id_equal(umm, obj->vo_dtx, DTX_UMMID_UNKNOWN)) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" shares obj "
			"with unknown DTXs, shares count %u.\n",
			DP_DTI(&dth->dth_xid),
			obj->vo_dtx_shares);
		obj->vo_dtx = dth->dth_ent;
		goto out;
	}

	sh_dtx = umem_id2ptr(umm, obj->vo_dtx);
	D_ASSERT(dtx != sh_dtx);
	D_ASSERTF(sh_dtx->te_state == DTX_ST_PREPARED ||
		  sh_dtx->te_state == DTX_ST_INIT,
		  "Invalid shared obj DTX state: %u\n",
		  sh_dtx->te_state);

	umem_tx_add_ptr(umm, sh_dtx, sizeof(*sh_dtx));
	sh_dtx->te_flags |= DTX_EF_SHARES;

out:
	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares obj with others, "
		"the shares count %u: rc = %d\n",
		DP_DTI(&dth->dth_xid), obj->vo_dtx_shares, rc);

	return rc;
}

static int
vos_dtx_share_key(struct umem_instance *umm, struct daos_tx_handle *dth,
		  struct vos_dtx_entry_df *dtx, struct vos_dtx_share *vds,
		  bool *shared)
{
	struct vos_krec_df	*key;
	struct vos_dtx_entry_df	*sh_dtx;
	int			 rc;

	key = umem_id2ptr(umm, vds->vds_record);
	/* The to be shared key has been committed. */
	if (UMMID_IS_NULL(key->kr_dtx))
		return 0;

	rc = vos_dtx_append_share(umm, dtx, vds);
	if (rc != 0)
		goto out;

	umem_tx_add_ptr(umm, key, sizeof(*key));

	/* The to be shared key has been aborted, reuse it. */
	if (umem_id_equal(umm, key->kr_dtx, DTX_UMMID_ABORTED)) {
		D_ASSERTF(key->kr_dtx_shares == 0,
			  "Invalid shared key DTX shares %d\n",
			  key->kr_dtx_shares);
		key->kr_dtx_shares = 1;
		goto out;
	}

	key->kr_dtx_shares++;
	*shared = true;

	/* The original key DTX has been aborted, but some others
	 * still share the key. Then set the kr_dtx to current DTX.
	 */
	if (umem_id_equal(umm, key->kr_dtx, DTX_UMMID_UNKNOWN)) {
		D_DEBUG(DB_TRACE, "The DTX "DF_DTI" shares key "
			"with unknown DTXs, shares count %u.\n",
			DP_DTI(&dth->dth_xid), key->kr_dtx_shares);
		key->kr_dtx = dth->dth_ent;
		goto out;
	}

	sh_dtx = umem_id2ptr(umm, key->kr_dtx);
	D_ASSERT(dtx != sh_dtx);
	D_ASSERTF(sh_dtx->te_state == DTX_ST_PREPARED ||
		  sh_dtx->te_state == DTX_ST_INIT,
		  "Invalid shared key DTX state: %u\n",
		  sh_dtx->te_state);

	umem_tx_add_ptr(umm, sh_dtx, sizeof(*sh_dtx));
	sh_dtx->te_flags |= DTX_EF_SHARES;

out:
	D_DEBUG(DB_TRACE, "The DTX "DF_DTI" try to shares key with others, "
		"the shares count %u: rc = %d\n",
		DP_DTI(&dth->dth_xid), key->kr_dtx_shares, rc);

	return rc;
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_id_t record,
			uint32_t type, uint32_t flags)
{
	struct daos_tx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_share		*vds;
	struct vos_dtx_share		*next;
	umem_id_t			 rec_mmid = UMMID_NULL;
	umem_id_t			*entry = NULL;
	uint32_t			*shares = NULL;
	int				 rc = 0;
	bool				 shared = false;

	switch (type) {
	case DTX_RT_OBJ: {
		struct vos_obj_df	*obj;

		obj = umem_id2ptr(umm, record);
		entry = &obj->vo_dtx;
		if (dth == NULL || dth->dth_intent == DAOS_INTENT_UPDATE)
			shares = &obj->vo_dtx_shares;

		/* "flags == 0" means new created object. It is unnecessary
		 * to umem_tx_add_ptr() for new created object.
		 */
		if (flags != 0)
			umem_tx_add_ptr(umm, obj, sizeof(*obj));
		break;
	}
	case DTX_RT_KEY: {
		struct vos_krec_df	*key;

		key = umem_id2ptr(umm, record);
		entry = &key->kr_dtx;
		if (dth == NULL || dth->dth_intent == DAOS_INTENT_UPDATE)
			shares = &key->kr_dtx_shares;

		if (flags != 0)
			umem_tx_add_ptr(umm, key, sizeof(*key));
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df	*svt;

		svt = umem_id2ptr(umm, record);
		entry = &svt->ir_dtx;
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc		*evt;

		evt = umem_id2ptr(umm, record);
		entry = &evt->dc_dtx;
		break;
	}
	default:
		D_ERROR("Unknown DTX type %u\n", type);
		return -DER_INVAL;
	}

	if (dth == NULL) {
		*entry = UMMID_NULL;
		if (shares != NULL)
			*shares = 0;

		return 0;
	}

	rec_mmid = umem_zalloc(umm, sizeof(struct vos_dtx_record_df));
	if (UMMID_IS_NULL(rec_mmid))
		return -DER_NOMEM;

	rec = umem_id2ptr(umm, rec_mmid);
	rec->tr_type = type;
	rec->tr_flags = flags;
	rec->tr_record = record;
	rec->tr_next = UMMID_NULL;

	if (UMMID_IS_NULL(dth->dth_ent)) {
		D_ASSERT(flags == 0);

		rc = vos_dtx_alloc(umm, dth, rec_mmid, &dtx);
		if (rc != 0)
			return rc;
	} else {
		vos_dtx_append(umm, dth, rec_mmid, record, type, flags, &dtx);
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
vos_dtx_degister_record(struct umem_instance *umm,
			umem_id_t entry, umem_id_t record, uint32_t type)
{
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	struct vos_dtx_record_df	*prev = NULL;
	umem_id_t			 rec_mmid;

	if (!vos_dtx_is_normal_entry(umm, entry))
		return;

	dtx = umem_id2ptr(umm, entry);
	rec_mmid = dtx->te_records;
	while (!UMMID_IS_NULL(rec_mmid)) {
		rec = umem_id2ptr(umm, rec_mmid);
		if (!umem_id_equal(umm, record, rec->tr_record)) {
			prev = rec;
			rec_mmid = rec->tr_next;
			continue;
		}

		if (prev == NULL) {
			umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
			dtx->te_records = rec->tr_next;
		} else {
			umem_tx_add_ptr(umm, prev, sizeof(*prev));
			prev->tr_next = rec->tr_next;
		}

		umem_free(umm, rec_mmid);
		break;
	};

	/* The caller will destroy related OBJ/KEY/SVT/EVT record after
	 * degistered the DTX record. So not reset DTX reference inside
	 * related OBJ/KEY/SVT/EVT record unless necessary.
	 */
	if (type == DTX_RT_OBJ) {
		struct vos_obj_df	*obj;

		obj = umem_id2ptr(umm, record);
		D_ASSERT(umem_id_equal(umm, obj->vo_dtx, entry));

		umem_tx_add_ptr(umm, obj, sizeof(*obj));
		if (dtx->te_intent == DAOS_INTENT_UPDATE) {
			obj->vo_dtx_shares--;
			if (obj->vo_dtx_shares > 0)
				obj->vo_dtx = DTX_UMMID_UNKNOWN;
		}
	} else if (type == DTX_RT_KEY) {
		struct vos_krec_df	*key;

		key = umem_id2ptr(umm, record);
		D_ASSERT(umem_id_equal(umm, key->kr_dtx, entry));

		if (dtx->te_intent == DAOS_INTENT_UPDATE) {
			umem_tx_add_ptr(umm, key, sizeof(*key));
			key->kr_dtx_shares--;
			if (key->kr_dtx_shares > 0)
				key->kr_dtx = DTX_UMMID_UNKNOWN;
		}
	}
}

int
vos_dtx_prepared(struct daos_tx_handle *dth)
{
	struct vos_container	*cont;
	struct vos_dtx_entry_df	*dtx;
	int			 rc = 0;

	D_ASSERT(!UMMID_IS_NULL(dth->dth_ent));

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dtx = umem_id2ptr(&cont->vc_pool->vp_umm, dth->dth_ent);
	if (dth->dth_intent == DAOS_INTENT_UPDATE) {
		daos_iov_t	kiov;
		daos_iov_t	riov;

		/* There is CPU yield during the bulk transfer, then it is
		 * possible that some others (rebuild) abort this DTX by race.
		 * So we need to locate (or verify) DTX via its ID instead of
		 * directly using the dth_ent.
		 */
		daos_iov_set(&kiov, &dth->dth_xid, sizeof(struct daos_tx_id));
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
			dth->dth_ent = UMMID_NULL;
		else
			D_ERROR(DF_UOID" fail to commit for "DF_DTI" rc = %d\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid), rc);
	}

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
		if (!UMMID_IS_NULL(dth->dth_ent)) {
			struct vos_dtx_entry_df *dtx;
			int			 rc;

			dtx = umem_id2ptr(&cont->vc_pool->vp_umm, dth->dth_ent);
			if (dtx->te_flags & DTX_EF_SHARES ||
			    dtx->te_dkey_hash[0] == 0) {
				/* If some DTXs share something (object/key),
				 * then synchronously commit the DTX that
				 * becomes committable to avoid visibility
				 * trouble.
				 */
				dth->dth_sync = 1;
				D_GOTO(out, result = DTX_ACT_COMMIT_SYNC);
			}

			rc = vos_dtx_add_cos(vos_cont2hdl(cont), &dth->dth_oid,
					&dth->dth_xid, dtx->te_dkey_hash[0],
					dtx->te_intent == DAOS_INTENT_PUNCH ?
					true : false);
			if (rc != 0)
				D_GOTO(out, result = rc);
		}

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

void
vos_dtx_commit_internal(struct vos_container *cont, struct daos_tx_id *dtis,
			int count)
{
	int	i;

	for (i = 0; i < count; i++)
		vos_dtx_commit_one(cont, &dtis[i]);

	cont->vc_dtx_time_last_commit = time(NULL);
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
		vos_dtx_commit_internal(cont, dtis, count);
		umem_tx_commit(umm);
	}

	return rc;
}

int
vos_dtx_abort_internal(struct vos_container *cont, struct daos_tx_id *dtis,
		       int count, bool force)
{
	int	rc = 0;
	int	i;

	for (i = 0; rc == 0 && i < count; i++)
		rc = vos_dtx_abort_one(cont, &dtis[i], force);

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, struct daos_tx_id *dtis, int count, bool force)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	/* Abort multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc != 0)
		return rc;

	rc = vos_dtx_abort_internal(cont, dtis, count, force);
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
