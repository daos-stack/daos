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

static void
dtx_set_aborted(umem_off_t *umoff)
{
	umem_off_set_null_flags(umoff, DTX_UMOFF_ABORTED);
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
dtx_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
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
dtx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
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
dtx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
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
			rc = dbtree_destroy(hdl, NULL);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX active dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	if (dtab_df->tt_committed_btr.tr_class != 0) {
		rc = dbtree_open_inplace(&dtab_df->tt_committed_btr,
					 &pool->vp_uma, &hdl);
		if (rc == 0)
			rc = dbtree_destroy(hdl, NULL);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX committed dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	return rc;
}

static int
dtx_ilog_rec_release(struct umem_instance *umm,
		     struct vos_dtx_entry_df *dtx,
		     struct vos_dtx_record_df *rec, umem_off_t umoff,
		     bool abort)
{
	struct ilog_df		*ilog;
	daos_handle_t		 loh;
	struct ilog_desc_cbs	 cbs;
	struct ilog_id		 id;
	int			 rc;

	ilog = umem_off2ptr(umm, rec->tr_record);

	vos_ilog_desc_cbs_init(&cbs, DAOS_HDL_INVAL);
	rc = ilog_open(umm, ilog, &cbs, &loh);
	if (rc != 0)
		return rc;

	id.id_epoch = dtx->te_epoch;
	id.id_tx_id = umoff;

	if (abort)
		rc = ilog_abort(loh, &id);
	else
		rc = ilog_persist(loh, &id);

	ilog_close(loh);
	return rc;
}

static void
dtx_rec_release(struct umem_instance *umm, umem_off_t umoff,
		bool abort, bool destroy, bool logged)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_off2ptr(umm, umoff);
	if (!logged)
		umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	while (!dtx_is_null(dtx->te_records)) {
		umem_off_t			 rec_umoff = dtx->te_records;
		struct vos_dtx_record_df	*rec;

		rec = umem_off2ptr(umm, rec_umoff);
		switch (rec->tr_type) {
		case DTX_RT_ILOG: {
			dtx_ilog_rec_release(umm, dtx, rec, umoff, abort);
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

	D_DEBUG(DB_TRACE, "dtx_rec_release: %s/%s the DTX "DF_DTI"\n",
		abort ? "abort" : "commit", destroy ? "destroy" : "keep",
		DP_DTI(&dtx->te_xid));

	if (destroy)
		umem_free(umm, umoff);
	else
		dtx->te_flags &= ~DTX_EF_SHARES;
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
	d_iov_t			 kiov;
	d_iov_t			 riov;
	umem_off_t			 umoff;
	int				 rc = 0;

	d_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   &kiov, &umoff);
	if (rc == -DER_NONEXIST) {
		d_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		goto out;
	}

	if (rc != 0)
		goto out;

	dtx = umem_off2ptr(umm, umoff);
	umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	dtx->te_state = DTX_ST_COMMITTED;
	rbund.trb_umoff = umoff;
	d_iov_set(&riov, &rbund, sizeof(rbund));
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
	dtx_rec_release(umm, umoff, false, false, true);
	vos_dtx_del_cos(cont, &dtx->te_oid, dti, dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);

out:
	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = %d\n",
		DP_DTI(dti), rc);

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, daos_epoch_t epoch,
		  struct dtx_id *dti, bool force)
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
		dtx_rec_release(vos_cont2umm(cont), off, true, true, false);

out:
	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = %d\n", DP_DTI(dti), rc);

	if (rc != 0 && force)
		rc = 0;

	return rc;
}

static bool
vos_dtx_is_normal_entry(struct umem_instance *umm, umem_off_t entry)
{
	if (dtx_is_null(entry) || dtx_is_aborted(entry))
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
	d_iov_t		 kiov;
	d_iov_t		 riov;
	struct dtx_rec_bundle	 rbund;
	int			 rc;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	dtx_umoff = umem_zalloc(umm, sizeof(struct vos_dtx_entry_df));
	if (dtx_is_null(dtx_umoff))
		return -DER_NOSPACE;

	dtx = umem_off2ptr(umm, dtx_umoff);
	dtx->te_xid = dth->dth_xid;
	dtx->te_oid = dth->dth_oid;
	dtx->te_dkey_hash = dth->dth_dkey_hash;
	dtx->te_epoch = dth->dth_epoch;
	dtx->te_ver = dth->dth_ver;
	dtx->te_state = DTX_ST_PREPARED;
	dtx->te_flags = dth->dth_leader ? DTX_EF_LEADER : 0;
	dtx->te_intent = dth->dth_intent;
	dtx->te_time = crt_hlc_get();
	dtx->te_records = rec_umoff;
	dtx->te_next = UMOFF_NULL;
	dtx->te_prev = UMOFF_NULL;

	rbund.trb_umoff = dtx_umoff;
	d_iov_set(&riov, &rbund, sizeof(rbund));
	d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
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
	       struct vos_dtx_entry_df **dtxp)
{
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;

	/* The @dtx must be new created via former
	 * vos_dtx_register_record(), no need umem_tx_add_ptr().
	 */
	dtx = umem_off2ptr(umm, dth->dth_ent);
	dtx->te_time = crt_hlc_get();
	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_next = dtx->te_records;
	dtx->te_records = rec_umoff;
	*dtxp = dtx;
}

static int
vos_dtx_check_shares(struct umem_instance *umm, daos_handle_t coh,
		     struct dtx_handle *dth, struct vos_dtx_entry_df *dtx,
		     umem_off_t record, uint32_t intent, uint32_t type,
		     umem_off_t *addr)
{
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

	dtx_record_conflict(dth, dtx);

	return dtx_inprogress(dtx, 5);
}

int
vos_dtx_check_availability(struct umem_instance *umm, daos_handle_t coh,
			   umem_off_t entry, umem_off_t record, uint32_t intent,
			   uint32_t type)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx = NULL;
	umem_off_t			*addr = NULL;

	switch (type) {
	case DTX_RT_SVT:
	case DTX_RT_EVT:
	case DTX_RT_ILOG:
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

		return ALB_AVAILABLE_CLEAN;
	}

	/* Committed */
	if (dtx_is_null(entry))
		return ALB_AVAILABLE_CLEAN;

	if (intent == DAOS_INTENT_PURGE)
		return ALB_AVAILABLE_DIRTY;

	/* Aborted */
	if (dtx_is_aborted(entry))
		return ALB_UNAVAILABLE;

	/* The DTX owner can always see the DTX. */
	if (dth != NULL && entry == dth->dth_ent)
		return ALB_AVAILABLE_CLEAN;

	dtx = umem_off2ptr(umm, entry);
	switch (dtx->te_state) {
	case DTX_ST_COMMITTED:
		return ALB_AVAILABLE_CLEAN;
	case DTX_ST_PREPARED: {
		struct vos_container	*cont = vos_hdl2cont(coh);
		int			 rc;

		if (cont == NULL)
			goto skip_cos;

		rc = vos_dtx_lookup_cos(coh, &dtx->te_oid, &dtx->te_xid,
			dtx->te_dkey_hash,
			dtx->te_intent == DAOS_INTENT_PUNCH ? true : false);
		if (rc == 0)
			return ALB_AVAILABLE_CLEAN;

		if (rc != -DER_NONEXIST)
			return rc;

		/* The followings are for non-committable cases. */

skip_cos:
		if (intent == DAOS_INTENT_DEFAULT ||
		    intent == DAOS_INTENT_REBUILD) {
			if (!(dtx->te_flags & DTX_EF_LEADER) ||
			    DAOS_FAIL_CHECK(DAOS_VOS_NON_LEADER)) {
				/* Inavailable for rebuild case. */
				if (intent == DAOS_INTENT_REBUILD)
					return ALB_UNAVAILABLE;

				D_DEBUG(DB_TRACE, "Let's ask leader "DF_DTI
					"\n", DP_DTI(&dtx->te_xid));
				/* Non-leader and non-rebuild case,
				 * return -DER_INPROGRESS, then the
				 * caller will retry the RPC with
				 * leader replica.
				 */
				return dtx_inprogress(dtx, 2);
			}

			/* For leader, non-committed DTX is unavailable. */
			return ALB_UNAVAILABLE;
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
	default:
		D_ERROR("Unexpected DTX state %u\n", dtx->te_state);
		return -DER_INVAL;
	}
}

umem_off_t
vos_dtx_get(void)
{
	struct dtx_handle		*dth = vos_dth_get();


	if (dth == NULL)
		return UMOFF_NULL;

	return dth->dth_ent;
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, umem_off_t *tx_id)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_dtx_entry_df		*dtx;
	struct vos_dtx_record_df	*rec;
	umem_off_t			 rec_umoff = UMOFF_NULL;
	int				 rc = 0;

	if (dth == NULL) {
		*tx_id = UMOFF_NULL;
		return 0;
	}

	rec_umoff = umem_zalloc(umm, sizeof(struct vos_dtx_record_df));
	if (dtx_is_null(rec_umoff))
		return -DER_NOSPACE;

	rec = umem_off2ptr(umm, rec_umoff);
	rec->tr_type = type;
	rec->tr_record = record;
	rec->tr_next = UMOFF_NULL;

	if (dtx_is_null(dth->dth_ent)) {
		rc = vos_dtx_alloc(umm, dth, rec_umoff, &dtx);
		if (rc != 0)
			return rc;
	} else {
		vos_dtx_append(umm, dth, rec_umoff, record, type, &dtx);
	}

	/* Incarnation log entry implies a share */
	*tx_id = dth->dth_ent;

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
				umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
				dtx->te_records = rec->tr_next;
			} else {
				umem_tx_add_ptr(umm, prev, sizeof(*prev));
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

	/* The caller will destroy related ILOG/SVT/EVT record after
	 * deregistered the DTX record. So not reset DTX reference inside
	 * related ILOG/SVT/EVT record unless necessary.
	 */
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
	rc = vos_tx_begin(vos_cont2umm(cont));
	if (rc == 0) {
		vos_dtx_commit_internal(cont, dtis, count);
		rc = vos_tx_end(vos_cont2umm(cont), rc);
	}

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, daos_epoch_t epoch, struct dtx_id *dtis,
	      int count, bool force)
{
	struct vos_container	*cont;
	int			 rc;
	int			 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Abort multiple DTXs via single PMDK transaction. */
	rc = vos_tx_begin(vos_cont2umm(cont));
	if (rc != 0)
		return rc;

	for (i = 0; rc == 0 && i < count; i++)
		rc = vos_dtx_abort_one(cont, epoch, &dtis[i], force);

	return vos_tx_end(vos_cont2umm(cont), rc);
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

	rc = vos_tx_begin(vos_cont2umm(cont));
	if (rc != 0)
		return rc;

	umem_tx_add_ptr(umm, tab, sizeof(*tab));
	for (count = 0, dtx_umoff = tab->tt_entry_head;
	     count < max && !dtx_is_null(dtx_umoff); count++) {
		struct vos_dtx_entry_df	*dtx;
		d_iov_t		 kiov;
		umem_off_t		 umoff;

		dtx = umem_off2ptr(umm, dtx_umoff);
		if (dtx_hlc_age2sec(dtx->te_time) < age)
			break;

		d_iov_set(&kiov, &dtx->te_xid, sizeof(dtx->te_xid));
		rc = dbtree_delete(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
				   &kiov, &umoff);
		D_ASSERT(rc == 0);

		tab->tt_count--;
		dtx_umoff = dtx->te_next;
		umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
		vos_dtx_unlink_entry(umm, tab, dtx);
		dtx_rec_release(umm, umoff, false, true, true);
	}

	/* This function needs to handle failures.  tx_add_ptr can fail as
	 * can dbtree_delete
	 */
	vos_tx_end(vos_cont2umm(cont), 0);
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
		stat->dtx_oldest_committed_time = dtx->te_time;
	} else {
		stat->dtx_oldest_committed_time = 0;
	}
}

int
vos_dtx_mark_sync(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch)
{
	struct vos_container	*cont;
	struct daos_lru_cache	*occ;
	struct vos_object	*obj;
	daos_epoch_range_t	 epr = {0, epoch};
	int	rc;

	cont = vos_hdl2cont(coh);
	occ = vos_obj_cache_current();
	rc = vos_obj_hold(occ, cont, oid, &epr, true,
			  DAOS_INTENT_DEFAULT, true, &obj);
	if (rc != 0) {
		D_ERROR(DF_UOID" fail to mark sync(1): rc = %d\n",
			DP_UOID(oid), rc);
		return rc;
	}

	if (obj->obj_df != NULL && obj->obj_df->vo_sync < epoch) {
		rc = vos_tx_begin(vos_cont2umm(cont));
		if (rc == 0) {
			umem_tx_add_ptr(&cont->vc_pool->vp_umm,
					&obj->obj_df->vo_sync,
					sizeof(obj->obj_df->vo_sync));
			obj->obj_df->vo_sync = epoch;
			rc = vos_tx_end(vos_cont2umm(cont), rc);
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

	vos_obj_release(occ, obj, false);
	return rc;
}
