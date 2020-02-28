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

#include <libpmem.h>
#include <daos_srv/vos.h>
#include "vos_layout.h"
#include "vos_internal.h"

/* Dummy offset for an aborted DTX address. */
#define DTX_UMOFF_ABORTED	1

/* 128 KB per SCM blob */
#define DTX_BLOB_SIZE		(1 << 17)

#define DTX_ACT_BLOB_MAGIC	0x14130a2b
#define DTX_CMT_BLOB_MAGIC	0x2502191c

static inline bool
umoff_is_null(umem_off_t umoff)
{
	return umoff == UMOFF_NULL;
}

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
dtx_inprogress(struct dtx_handle *dth, struct vos_dtx_act_ent *dae, int pos)
{
	if (dae != NULL) {
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX "DF_DTI" at %d\n",
			DP_DTI(&DAE_XID(dae)), pos);

		if (dth != NULL && dth->dth_conflict != NULL) {
			D_DEBUG(DB_TRACE, "Record conflict DTX "DF_DTI"\n",
				DP_DTI(&DAE_XID(dae)));
			daos_dti_copy(&dth->dth_conflict->dce_xid,
				      &DAE_XID(dae));
			dth->dth_conflict->dce_dkey = DAE_DKEY_HASH(dae);
		}
	} else {
		D_DEBUG(DB_TRACE, "Hit uncommitted (unknown) DTX at %d\n", pos);
	}

	return -DER_INPROGRESS;
}

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
dtx_act_ent_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		  d_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_dtx_act_ent	*dae = val_iov->iov_buf;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dae);

	return 0;
}

static int
dtx_act_ent_free(struct btr_instance *tins, struct btr_record *rec,
		 void *args)
{
	struct vos_dtx_act_ent	*dae;

	dae = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rec->rec_off = UMOFF_NULL;

	if (args != NULL) {
		/* Return the record addreass (offset in DRAM).
		 * The caller will release it after using.
		 */
		D_ASSERT(dae != NULL);
		*(struct vos_dtx_act_ent **)args = dae;
	} else if (dae != NULL) {
		if (dae->dae_records != NULL)
			D_FREE(dae->dae_records);
		D_FREE_PTR(dae);
	}

	return 0;
}

static int
dtx_act_ent_fetch(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_dtx_act_ent	*dae;

	D_ASSERT(val_iov != NULL);

	dae = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, dae, sizeof(*dae));

	return 0;
}

static int
dtx_act_ent_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t dtx_active_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_act_ent_alloc,
	.to_rec_free	= dtx_act_ent_free,
	.to_rec_fetch	= dtx_act_ent_fetch,
	.to_rec_update	= dtx_act_ent_update,
};

static int
dtx_cmt_ent_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		  d_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_container	*cont = tins->ti_priv;
	struct vos_dtx_cmt_ent	*dce = val_iov->iov_buf;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dce);
	if (!cont->vc_reindex_cmt_dtx || dce->dce_reindex) {
		d_list_add_tail(&dce->dce_committed_link,
				&cont->vc_dtx_committed_list);
		cont->vc_dtx_committed_count++;
	} else {
		d_list_add_tail(&dce->dce_committed_link,
				&cont->vc_dtx_committed_tmp_list);
		cont->vc_dtx_committed_tmp_count++;
	}

	return 0;
}

static int
dtx_cmt_ent_free(struct btr_instance *tins, struct btr_record *rec,
		 void *args)
{
	struct vos_container	*cont = tins->ti_priv;
	struct vos_dtx_cmt_ent	*dce;

	dce = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	D_ASSERT(dce != NULL);

	rec->rec_off = UMOFF_NULL;
	d_list_del(&dce->dce_committed_link);
	if (!cont->vc_reindex_cmt_dtx || dce->dce_reindex)
		cont->vc_dtx_committed_count--;
	else
		cont->vc_dtx_committed_tmp_count--;
	D_FREE_PTR(dce);

	return 0;
}

static int
dtx_cmt_ent_fetch(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key_iov, d_iov_t *val_iov)
{
	if (val_iov != NULL) {
		struct vos_dtx_cmt_ent	*dce;

		dce = umem_off2ptr(&tins->ti_umm, rec->rec_off);
		d_iov_set(val_iov, dce, sizeof(*dce));
	}

	return 0;
}

static int
dtx_cmt_ent_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val)
{
	struct vos_dtx_cmt_ent	*dce = val->iov_buf;

	dce->dce_exist = 1;

	return 0;
}

static btr_ops_t dtx_committed_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_cmt_ent_alloc,
	.to_rec_free	= dtx_cmt_ent_free,
	.to_rec_fetch	= dtx_cmt_ent_fetch,
	.to_rec_update	= dtx_cmt_ent_update,
};

int
vos_dtx_table_register(void)
{
	int	rc;

	rc = dbtree_class_register(VOS_BTR_DTX_ACT_TABLE, 0,
				   &dtx_active_btr_ops);
	if (rc != 0) {
		D_ERROR("Failed to register DTX active dbtree: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(VOS_BTR_DTX_CMT_TABLE, 0,
				   &dtx_committed_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX committed dbtree: %d\n", rc);

	return rc;
}

void
vos_dtx_table_destroy(struct umem_instance *umm, struct vos_cont_df *cont_df)
{
	struct vos_dtx_blob_df		*dbd;
	umem_off_t			 dbd_off;

	/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
	umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			sizeof(cont_df->cd_dtx_committed_head) +
			sizeof(cont_df->cd_dtx_committed_tail));

	while (!umoff_is_null(cont_df->cd_dtx_committed_head)) {
		dbd_off = cont_df->cd_dtx_committed_head;
		dbd = umem_off2ptr(umm, dbd_off);
		cont_df->cd_dtx_committed_head = dbd->dbd_next;
		umem_free(umm, dbd_off);
	}

	cont_df->cd_dtx_committed_head = UMOFF_NULL;
	cont_df->cd_dtx_committed_tail = UMOFF_NULL;

	/* cd_dtx_active_tail is next to cd_dtx_active_head */
	umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
			sizeof(cont_df->cd_dtx_active_head) +
			sizeof(cont_df->cd_dtx_active_tail));

	while (!umoff_is_null(cont_df->cd_dtx_active_head)) {
		dbd_off = cont_df->cd_dtx_active_head;
		dbd = umem_off2ptr(umm, dbd_off);
		cont_df->cd_dtx_active_head = dbd->dbd_next;
		umem_free(umm, dbd_off);
	}

	cont_df->cd_dtx_active_head = UMOFF_NULL;
	cont_df->cd_dtx_active_tail = UMOFF_NULL;
}

static int
dtx_ilog_rec_release(struct umem_instance *umm, struct vos_container *cont,
		     struct vos_dtx_record_df *rec, struct vos_dtx_act_ent *dae,
		     bool abort)
{
	struct ilog_df		*ilog;
	daos_handle_t		 loh;
	struct ilog_desc_cbs	 cbs;
	struct ilog_id		 id;
	int			 rc;

	ilog = umem_off2ptr(umm, rec->dr_record);

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
	rc = ilog_open(umm, ilog, &cbs, &loh);
	if (rc != 0)
		return rc;

	id.id_epoch = DAE_EPOCH(dae);
	id.id_tx_id = dae->dae_df_off;

	if (abort)
		rc = ilog_abort(loh, &id);
	else
		rc = ilog_persist(loh, &id);

	ilog_close(loh);
	return rc;
}

static void
do_dtx_rec_release(struct umem_instance *umm, struct vos_container *cont,
		   struct vos_dtx_act_ent *dae, struct vos_dtx_record_df *rec,
		   struct vos_dtx_record_df *rec_df, int index, bool abort)
{
	if (umoff_is_null(rec->dr_record))
		return;

	/* Has been deregistered. */
	if (rec_df != NULL && umoff_is_null(rec_df[index].dr_record))
		return;

	switch (rec->dr_type) {
	case DTX_RT_ILOG: {
		dtx_ilog_rec_release(umm, cont, rec, dae, abort);
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df	*svt;

		svt = umem_off2ptr(umm, rec->dr_record);
		if (abort) {
			if (DAE_INDEX(dae) != -1)
				umem_tx_add_ptr(umm, &svt->ir_dtx,
						sizeof(svt->ir_dtx));
			dtx_set_aborted(&svt->ir_dtx);
		} else {
			umem_tx_add_ptr(umm, &svt->ir_dtx, sizeof(svt->ir_dtx));
			svt->ir_dtx = UMOFF_NULL;
		}
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc		*evt;

		evt = umem_off2ptr(umm, rec->dr_record);
		if (abort) {
			if (DAE_INDEX(dae) != -1)
				umem_tx_add_ptr(umm, &evt->dc_dtx,
						sizeof(evt->dc_dtx));
			dtx_set_aborted(&evt->dc_dtx);
		} else {
			umem_tx_add_ptr(umm, &evt->dc_dtx, sizeof(evt->dc_dtx));
			evt->dc_dtx = UMOFF_NULL;
		}
		break;
	}
	default:
		D_ERROR(DF_UOID" unknown DTX "DF_DTI" type %u\n",
			DP_UOID(DAE_OID(dae)), DP_DTI(&DAE_XID(dae)),
			rec->dr_type);
		break;
	}
}

static void
dtx_rec_release(struct vos_container *cont, struct vos_dtx_act_ent *dae,
		bool abort, umem_off_t *p_offset)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_dtx_record_df	*rec_df = NULL;
	struct vos_dtx_blob_df		*dbd;
	int				 count;
	int				 i;

	D_ASSERT(DAE_INDEX(dae) >= 0);

	dbd = dae->dae_dbd;
	D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);

	dae_df = umem_off2ptr(umm, dae->dae_df_off);
	D_ASSERT(dae_df != NULL);

	if (dae->dae_records != NULL) {
		D_ASSERT(DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT);

		if (dae_df->dae_layout_gen != DAE_LAYOUT_GEN(dae))
			rec_df = umem_off2ptr(umm, dae_df->dae_rec_off);

		for (i = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT - 1; i >= 0; i--)
			do_dtx_rec_release(umm, cont, dae, &dae->dae_records[i],
					   rec_df, i, abort);

		D_FREE(dae->dae_records);
		dae->dae_records = NULL;
		dae->dae_rec_cap = 0;
	}

	if (dae_df->dae_layout_gen != DAE_LAYOUT_GEN(dae))
		rec_df = dae_df->dae_rec_inline;

	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = count - 1; i >= 0; i--)
		do_dtx_rec_release(umm, cont, dae, &DAE_REC_INLINE(dae)[i],
				   rec_df, i, abort);

	if (!umoff_is_null(dae_df->dae_rec_off))
		umem_free(umm, dae_df->dae_rec_off);

	if (dbd->dbd_count > 1 || dbd->dbd_index < dbd->dbd_cap) {
		umem_tx_add_ptr(umm, &dae_df->dae_flags,
				sizeof(dae_df->dae_flags));
		/* Mark the DTX entry as invalid in SCM. */
		dae_df->dae_flags = DTX_EF_INVALID;

		umem_tx_add_ptr(umm, &dbd->dbd_count, sizeof(dbd->dbd_count));
		dbd->dbd_count--;
	} else {
		struct vos_cont_df	*cont_df = cont->vc_cont_df;
		umem_off_t		 dbd_off;
		struct vos_dtx_blob_df	*tmp;

		dbd_off = umem_ptr2off(umm, dbd);
		tmp = umem_off2ptr(umm, dbd->dbd_prev);
		if (tmp != NULL) {
			umem_tx_add_ptr(umm, &tmp->dbd_next,
					sizeof(tmp->dbd_next));
			tmp->dbd_next = dbd->dbd_next;
		}

		tmp = umem_off2ptr(umm, dbd->dbd_next);
		if (tmp != NULL) {
			umem_tx_add_ptr(umm, &tmp->dbd_prev,
					sizeof(tmp->dbd_prev));
			tmp->dbd_prev = dbd->dbd_prev;
		}

		if (cont_df->cd_dtx_active_head == dbd_off) {
			umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
					sizeof(cont_df->cd_dtx_active_head));
			cont_df->cd_dtx_active_head = dbd->dbd_next;
		}

		if (cont_df->cd_dtx_active_tail == dbd_off) {
			umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_tail,
					sizeof(cont_df->cd_dtx_active_tail));
			cont_df->cd_dtx_active_tail = dbd->dbd_prev;
		}

		if (p_offset != NULL)
			*p_offset = dbd_off;
		else
			umem_free(umm, dbd_off);
	}

	if (abort)
		D_FREE_PTR(dae);
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct dtx_id *dti,
		   daos_epoch_t epoch, struct vos_dtx_cmt_ent **dce_p)
{
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_dtx_cmt_ent		*dce = NULL;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	umem_off_t			 offset = UMOFF_NULL;
	int				 rc = 0;

	d_iov_set(&kiov, dti, sizeof(*dti));
	/* For single replicated object, we trigger commit just after local
	 * modification done. Under such case, the caller exactly knows the
	 * @epoch and no need to lookup the active DTX table. On the other
	 * hand, for modifying single replicated object, there is no DTX
	 * entry in the active DTX table.
	 */
	if (epoch == 0) {
		d_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc == -DER_NONEXIST) {
			rc = dbtree_lookup(cont->vc_dtx_committed_hdl,
					   &kiov, NULL);
			goto out;
		}

		if (rc != 0)
			goto out;

		dae = (struct vos_dtx_act_ent *)riov.iov_buf;
	}

	D_ALLOC_PTR(dce);
	if (dce == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	DCE_XID(dce) = *dti;
	DCE_EPOCH(dce) = epoch != 0 ? epoch : DAE_EPOCH(dae);
	dce->dce_reindex = 0;

	d_iov_set(&riov, dce, sizeof(*dce));
	rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc != 0 || epoch != 0)
		goto out;

	dtx_rec_release(cont, dae, false, &offset);
	vos_dtx_del_cos(cont, &DAE_OID(dae), dti, DAE_DKEY_HASH(dae),
			DAE_INTENT(dae) == DAOS_INTENT_PUNCH ? true : false);

	/* If dbtree_delete() failed, the @dae will be left in the active DTX
	 * table until close the container. It is harmless but waste some DRAM.
	 */
	dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_BYPASS, &kiov, NULL);

	if (!umoff_is_null(offset))
		umem_free(vos_cont2umm(cont), offset);

out:
	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = "DF_RC"\n",
		DP_DTI(dti), DP_RC(rc));
	if (rc != 0) {
		if (dce != NULL)
			D_FREE_PTR(dce);
	} else {
		*dce_p = dce;
	}

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, daos_epoch_t epoch,
		  struct dtx_id *dti)
{
	struct vos_dtx_act_ent	*dae;
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;

	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc != 0)
		goto out;

	if (epoch != 0) {
		dae = (struct vos_dtx_act_ent *)riov.iov_buf;
		if (DAE_EPOCH(dae) > epoch)
			D_GOTO(out, rc = -DER_NONEXIST);
	}

	rc = dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_BYPASS,
			   &kiov, &dae);
	if (rc == 0)
		dtx_rec_release(cont, dae, true, NULL);

out:
	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = "DF_RC"\n", DP_DTI(dti),
		DP_RC(rc));

	return rc;
}

static bool
vos_dtx_is_normal_entry(struct umem_instance *umm, umem_off_t entry)
{
	if (umoff_is_null(entry) || dtx_is_aborted(entry))
		return false;

	return true;
}

static int
vos_dtx_extend_act_table(struct vos_container *cont)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_cont_df		*cont_df = cont->vc_cont_df;
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*tmp;
	umem_off_t			 dbd_off;

	dbd_off = umem_zalloc(umm, DTX_BLOB_SIZE);
	if (umoff_is_null(dbd_off)) {
		D_ERROR("No space when create actvie DTX table.\n");
		return -DER_NOSPACE;
	}

	dbd = umem_off2ptr(umm, dbd_off);
	dbd->dbd_magic = DTX_ACT_BLOB_MAGIC;
	dbd->dbd_cap = (DTX_BLOB_SIZE - sizeof(struct vos_dtx_blob_df)) /
			sizeof(struct vos_dtx_act_ent_df);

	tmp = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	if (tmp == NULL) {
		D_ASSERT(umoff_is_null(cont_df->cd_dtx_active_head));

		/* cd_dtx_active_tail is next to cd_dtx_active_head */
		umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
				sizeof(cont_df->cd_dtx_active_head) +
				sizeof(cont_df->cd_dtx_active_tail));
		cont_df->cd_dtx_active_head = dbd_off;
	} else {
		umem_tx_add_ptr(umm, &tmp->dbd_next, sizeof(tmp->dbd_next));
		tmp->dbd_next = dbd_off;

		dbd->dbd_prev = cont_df->cd_dtx_active_tail;
		umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_tail,
				sizeof(cont_df->cd_dtx_active_tail));
	}

	cont_df->cd_dtx_active_tail = dbd_off;

	return 0;
}

static int
vos_dtx_alloc(struct umem_instance *umm, struct dtx_handle *dth)
{
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_container		*cont;
	struct vos_cont_df		*cont_df;
	struct vos_dtx_blob_df		*dbd;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	cont_df = cont->vc_cont_df;
	dth->dth_gen = cont->vc_dtx_resync_gen;

	D_ALLOC_PTR(dae);
	if (dae == NULL)
		return -DER_NOMEM;

	dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	if (dbd == NULL || dbd->dbd_index >= dbd->dbd_cap) {
		rc = vos_dtx_extend_act_table(cont);
		if (rc != 0)
			goto out;

		dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	}

	DAE_XID(dae) = dth->dth_xid;
	DAE_OID(dae) = dth->dth_oid;
	DAE_DKEY_HASH(dae) = dth->dth_dkey_hash;
	DAE_EPOCH(dae) = dth->dth_epoch;
	DAE_FLAGS(dae) = dth->dth_leader ? DTX_EF_LEADER : 0;
	DAE_INTENT(dae) = dth->dth_intent;
	DAE_SRV_GEN(dae) = dth->dth_gen;
	DAE_LAYOUT_GEN(dae) = dth->dth_gen;

	/* Will be set as dbd::dbd_index via vos_dtx_prepared(). */
	DAE_INDEX(dae) = -1;

	dae->dae_df_off = cont_df->cd_dtx_active_tail +
			offsetof(struct vos_dtx_blob_df, dbd_active_data) +
			sizeof(struct vos_dtx_act_ent_df) * dbd->dbd_index;
	dae->dae_dbd = dbd;

	d_iov_set(&kiov, &DAE_XID(dae), sizeof(DAE_XID(dae)));
	d_iov_set(&riov, dae, sizeof(*dae));
	rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc == 0) {
		dth->dth_ent = dae;
		dth->dth_actived = 1;
	}

out:
	if (rc != 0)
		D_FREE_PTR(dae);

	return rc;
}

static int
vos_dtx_append(struct umem_instance *umm, struct dtx_handle *dth,
	       umem_off_t record, uint32_t type)
{
	struct vos_dtx_act_ent		*dae = dth->dth_ent;
	struct vos_dtx_record_df	*rec;

	D_ASSERT(dae != NULL);

	if (DAE_REC_CNT(dae) < DTX_INLINE_REC_CNT) {
		rec = &DAE_REC_INLINE(dae)[DAE_REC_CNT(dae)];
	} else {
		if (DAE_REC_CNT(dae) >= dae->dae_rec_cap + DTX_INLINE_REC_CNT) {
			int	count;

			if (dae->dae_rec_cap == 0)
				count = DTX_REC_CAP_DEFAULT;
			else
				count = dae->dae_rec_cap * 2;

			D_ALLOC(rec, sizeof(*rec) * count);
			if (rec == NULL)
				return -DER_NOMEM;

			if (dae->dae_records != NULL) {
				memcpy(rec, dae->dae_records,
				       sizeof(*rec) * dae->dae_rec_cap);
				D_FREE(dae->dae_records);
			}

			dae->dae_records = rec;
			dae->dae_rec_cap = count;
		}

		rec = &dae->dae_records[DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT];
	}

	rec->dr_type = type;
	rec->dr_record = record;

	/* The rec_cnt on-disk value will be refreshed via vos_dtx_prepared() */
	DAE_REC_CNT(dae)++;

	return 0;
}

int
vos_dtx_check_availability(struct umem_instance *umm, daos_handle_t coh,
			   umem_off_t entry, uint32_t intent, uint32_t type)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_container		*cont;
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_dtx_act_ent_df	*dae_df = NULL;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc;

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
	if (umoff_is_null(entry))
		return ALB_AVAILABLE_CLEAN;

	if (intent == DAOS_INTENT_PURGE)
		return ALB_AVAILABLE_DIRTY;

	/* Aborted */
	if (dtx_is_aborted(entry))
		return ALB_UNAVAILABLE;

	/* The DTX owner can always see the DTX. */
	if (dth != NULL && dth->dth_ent != NULL) {
		dae = dth->dth_ent;
		if (dae->dae_df_off == entry)
			return ALB_AVAILABLE_CLEAN;
	}

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	dae_df = umem_off2ptr(umm, entry);
	d_iov_set(&kiov, &dae_df->dae_xid, sizeof(dae_df->dae_xid));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			/* Handle it as aborted one. */
			return ALB_UNAVAILABLE;

		D_ERROR("Failed to find active DTX entry for "DF_DTI"\n",
			DP_DTI(&dae_df->dae_xid));
		return rc;
	}

	dae = (struct vos_dtx_act_ent *)riov.iov_buf;

	rc = vos_dtx_lookup_cos(coh, &DAE_OID(dae), &DAE_XID(dae),
			DAE_DKEY_HASH(dae),
			DAE_INTENT(dae) == DAOS_INTENT_PUNCH ? true : false);
	if (rc == 0)
		return ALB_AVAILABLE_CLEAN;

	if (rc != -DER_NONEXIST) {
		D_ERROR("Failed to check CoS for DTX entry "DF_DTI"\n",
			DP_DTI(&dae_df->dae_xid));
		return rc;
	}

	/* The followings are for non-committable cases. */

	if (intent == DAOS_INTENT_DEFAULT || intent == DAOS_INTENT_REBUILD) {
		if (!(DAE_FLAGS(dae) & DTX_EF_LEADER) ||
		    DAOS_FAIL_CHECK(DAOS_VOS_NON_LEADER)) {
			/* Inavailable for rebuild case. */
			if (intent == DAOS_INTENT_REBUILD)
				return ALB_UNAVAILABLE;

			/* Non-leader and non-rebuild case, return
			 * -DER_INPROGRESS, then the caller will retry
			 * the RPC with leader replica.
			 */
			return dtx_inprogress(NULL, dae, 1);
		}

		/* For leader, non-committed DTX is unavailable. */
		return ALB_UNAVAILABLE;
	}

	/* PUNCH DTX cannot be shared by others. */
	if (DAE_INTENT(dae) == DAOS_INTENT_PUNCH) {
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

		return dtx_inprogress(dth, dae, 2);
	}

	/* PUNCH cannot share with others. */
	if (intent == DAOS_INTENT_PUNCH) {
		/* XXX: One corner case: if some DTXs share the same
		 *	object/key, and the original DTX that create
		 *	the object/key is aborted, then when we come
		 *	here, we do not know which DTX conflict with
		 *	me, so we can NOT set dth::dth_conflict that
		 *	will be used by DTX conflict handling logic.
		 */
		return dtx_inprogress(dth, dae, 3);
	}

	if (DAE_INTENT(dae) != DAOS_INTENT_UPDATE) {
		D_ERROR("Unexpected DTX intent %u\n", DAE_INTENT(dae));
		return -DER_INVAL;
	}

	D_ASSERT(intent == DAOS_INTENT_UPDATE);

	/* XXX: This seems NOT suitable. Before new punch model introduced,
	 *	we allow multiple concurrent modifications to update targets
	 *	under the same object/dkey/akey. That is the DTX share mode.
	 *	It should not cause any conflict.
	 */
	return dtx_inprogress(dth, dae, 4);
}

umem_off_t
vos_dtx_get(void)
{
	struct dtx_handle	*dth = vos_dth_get();
	struct vos_dtx_act_ent	*dae;

	if (dth == NULL || dth->dth_ent == NULL)
		return UMOFF_NULL;

	dae = dth->dth_ent;

	return dae->dae_df_off;
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, umem_off_t *tx_id)
{
	struct dtx_handle	*dth = vos_dth_get();
	int			 rc = 0;

	/* For single participator case, we only need committed DTX
	 * entry for handling resend case, nothing for active table.
	 */
	if (dth == NULL || dth->dth_solo) {
		*tx_id = UMOFF_NULL;
		return 0;
	}

	if (dth->dth_ent == NULL) {
		rc = vos_dtx_alloc(umm, dth);
		if (rc != 0)
			return rc;
	}

	rc = vos_dtx_append(umm, dth, record, type);
	if (rc == 0) {
		struct vos_dtx_act_ent	*dae = dth->dth_ent;

		/* Incarnation log entry implies a share */
		*tx_id = dae->dae_df_off;
		if (type == DTX_RT_ILOG)
			dth->dth_has_ilog = 1;
	}

	D_DEBUG(DB_TRACE, "Register DTX record for "DF_DTI
		": entry %p, type %d, %s ilog entry, rc %d\n",
		DP_DTI(&dth->dth_xid), dth->dth_ent, type,
		dth->dth_has_ilog ? "has" : "has not", rc);

	return rc;
}

/* The caller has started PMDK transaction. */
void
vos_dtx_deregister_record(struct umem_instance *umm, daos_handle_t coh,
			  umem_off_t entry, umem_off_t record)
{
	struct vos_container		*cont;
	struct vos_dtx_act_ent		*dae;
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_dtx_record_df	*rec_df;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 count;
	int				 rc;
	int				 i;

	if (!vos_dtx_is_normal_entry(umm, entry))
		return;

	dae_df = umem_off2ptr(umm, entry);
	if (daos_is_zero_dti(&dae_df->dae_xid) ||
	    dae_df->dae_flags & DTX_EF_INVALID)
		return;

	cont = vos_hdl2cont(coh);
	if (cont == NULL)
		goto handle_df;

	d_iov_set(&kiov, &dae_df->dae_xid, sizeof(dae_df->dae_xid));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc != 0) {
		D_WARN("NOT find active DTX entry when deregister for "
		       DF_DTI"\n", DP_DTI(&dae_df->dae_xid));
		return;
	}

	dae = (struct vos_dtx_act_ent *)riov.iov_buf;
	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = 0; i < count; i++) {
		if (record == DAE_REC_INLINE(dae)[i].dr_record) {
			DAE_REC_INLINE(dae)[i].dr_record = UMOFF_NULL;
			goto handle_df;
		}
	}

	for (i = 0; i < DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT; i++) {
		if (record == dae->dae_records[i].dr_record) {
			dae->dae_records[i].dr_record = UMOFF_NULL;
			goto handle_df;
		}
	}

	/* Not found */
	return;

handle_df:
	if (dae_df->dae_rec_cnt > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = dae_df->dae_rec_cnt;

	rec_df = dae_df->dae_rec_inline;
	for (i = 0; i < count; i++) {
		if (rec_df[i].dr_record == record) {
			rec_df[i].dr_record = UMOFF_NULL;
			if (cont == NULL)
				dae_df->dae_layout_gen++;
			return;
		}
	}

	rec_df = umem_off2ptr(umm, dae_df->dae_rec_off);

	/* Not found */
	if (rec_df == NULL)
		return;

	for (i = 0; i < dae_df->dae_rec_cnt - DTX_INLINE_REC_CNT; i++) {
		if (rec_df[i].dr_record == record) {
			rec_df[i].dr_record = UMOFF_NULL;
			if (cont == NULL)
				dae_df->dae_layout_gen++;
			return;
		}
	}
}

int
vos_dtx_prepared(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent		*dae = dth->dth_ent;
	struct vos_container		*cont;
	struct umem_instance		*umm;
	struct vos_dtx_blob_df		*dbd;

	if (dae == NULL)
		return 0;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	if (dth->dth_solo) {
		int	rc;

		rc = vos_dtx_commit_internal(cont, &dth->dth_xid, 1,
					     dth->dth_epoch);
		if (rc == 0)
			dth->dth_sync = 1;

		return rc;
	}

	umm = vos_cont2umm(cont);
	dbd = dae->dae_dbd;

	/* If the DTX is for punch object that is quite possible affect
	 * subsequent operations, then synchronously commit the DTX when
	 * it becomes committable to avoid availability trouble.
	 */
	if (DAE_DKEY_HASH(dae) == 0)
		dth->dth_sync = 1;

	if (dae->dae_records != NULL) {
		struct vos_dtx_record_df	*rec_df;
		umem_off_t			 rec_off;
		int				 count;
		int				 size;

		count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
		D_ASSERTF(count > 0, "Invalid DTX rec count %d\n", count);

		size = sizeof(struct vos_dtx_record_df) * count;
		rec_off = umem_zalloc(umm, size);
		if (umoff_is_null(rec_off)) {
			D_ERROR("No space to store active DTX "DF_DTI"\n",
				DP_DTI(&DAE_XID(dae)));
			return -DER_NOSPACE;
		}

		rec_df = umem_off2ptr(umm, rec_off);
		memcpy(rec_df, dae->dae_records, size);
		DAE_REC_OFF(dae) = rec_off;
	}

	DAE_INDEX(dae) = dbd->dbd_index;
	if (DAE_INDEX(dae) > 0) {
		pmem_memcpy_nodrain(umem_off2ptr(umm, dae->dae_df_off),
				    &dae->dae_base,
				    sizeof(struct vos_dtx_act_ent_df));
		/* dbd_index is next to dbd_count */
		umem_tx_add_ptr(umm, &dbd->dbd_count,
				sizeof(dbd->dbd_count) +
				sizeof(dbd->dbd_index));
	} else {
		memcpy(umem_off2ptr(umm, dae->dae_df_off),
		       &dae->dae_base, sizeof(struct vos_dtx_act_ent_df));
	}

	dbd->dbd_count++;
	dbd->dbd_index++;

	return 0;
}

static int
do_vos_dtx_check(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t *epoch)
{
	struct vos_container	*cont;
	struct vos_dtx_act_ent	*dae;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == 0) {
		dae = (struct vos_dtx_act_ent *)riov.iov_buf;
		if (epoch != NULL) {
			if (*epoch == 0)
				*epoch = DAE_EPOCH(dae);
			else if (*epoch != DAE_EPOCH(dae))
				return -DER_MISMATCH;
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
	struct vos_container	*cont;
	int			 rc;

	rc = vos_dtx_lookup_cos(coh, oid, xid, dkey_hash, punch);
	if (rc == 0)
		return DTX_ST_COMMITTED;

	if (rc != -DER_NONEXIST)
		return rc;

	rc = do_vos_dtx_check(coh, xid, epoch);
	if (rc != -DER_NONEXIST)
		return rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (cont->vc_reindex_cmt_dtx)
		rc = -DER_AGAIN;

	return rc;
}

int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti)
{
	return do_vos_dtx_check(coh, dti, NULL);
}

int
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int count, daos_epoch_t epoch)
{
	struct vos_cont_df		*cont_df = cont->vc_cont_df;
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*dbd_prev;
	umem_off_t			 dbd_off;
	struct vos_dtx_cmt_ent_df	*dce_df;
	int				 committed = 0;
	int				 slots = 0;
	int				 cur = 0;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 i;
	int				 j;

	dbd = umem_off2ptr(umm, cont_df->cd_dtx_committed_tail);
	if (dbd != NULL)
		slots = dbd->dbd_cap - dbd->dbd_count;

	if (slots == 0)
		goto new_blob;

	umem_tx_add_ptr(umm, &dbd->dbd_count, sizeof(dbd->dbd_count));

again:
	if (slots > count)
		slots = count;

	count -= slots;

	if (slots > 1) {
		D_ALLOC(dce_df, sizeof(*dce_df) * slots);
		if (dce_df == NULL) {
			D_ERROR("Not enough DRAM to commit "DF_DTI"\n",
				DP_DTI(&dtis[cur]));

			/* For the DTXs that have been committed we will not
			 * re-insert them back into the active DTX table (in
			 * DRAM) even if we abort the PMDK transaction, then
			 * let's hide the error and commit former successful
			 * DTXs. The left non-committed DTXs will be handled
			 * next time.
			 */
			return committed > 0 ? 0 : -DER_NOMEM;
		}
	} else {
		dce_df = &dbd->dbd_commmitted_data[dbd->dbd_count];
	}

	for (i = 0, j = 0; i < slots; i++, cur++) {
		struct vos_dtx_cmt_ent	*dce = NULL;

		rc = vos_dtx_commit_one(cont, &dtis[cur], epoch, &dce);
		if (rc == 0 && dce != NULL)
			committed++;

		if (rc1 == 0)
			rc1 = rc;

		if (dce != NULL) {
			if (slots == 1)
				pmem_memcpy_nodrain(dce_df, &dce->dce_base,
						    sizeof(*dce_df));
			else
				memcpy(&dce_df[j], &dce->dce_base,
				       sizeof(dce_df[j]));
			j++;
		}
	}

	if (dce_df != &dbd->dbd_commmitted_data[dbd->dbd_count]) {
		if (j > 0)
			pmem_memcpy_nodrain(
				&dbd->dbd_commmitted_data[dbd->dbd_count],
				dce_df, sizeof(*dce_df) * j);
		D_FREE(dce_df);
	}

	if (j > 0)
		dbd->dbd_count += j;

	if (count == 0)
		return committed > 0 ? 0 : rc1;

	if (j < slots) {
		slots -= j;
		goto again;
	}

new_blob:
	dbd_prev = dbd;

	/* Need new @dbd */
	dbd_off = umem_zalloc(umm, DTX_BLOB_SIZE);
	if (umoff_is_null(dbd_off)) {
		D_ERROR("No space to store committed DTX %d "DF_DTI"\n",
			count, DP_DTI(&dtis[cur]));
		return committed > 0 ? 0 : -DER_NOSPACE;
	}

	dbd = umem_off2ptr(umm, dbd_off);
	dbd->dbd_magic = DTX_CMT_BLOB_MAGIC;
	dbd->dbd_cap = (DTX_BLOB_SIZE - sizeof(struct vos_dtx_blob_df)) /
		       sizeof(struct vos_dtx_cmt_ent_df);
	dbd->dbd_prev = umem_ptr2off(umm, dbd_prev);

	/* Not allow to commit too many DTX together. */
	D_ASSERTF(count < dbd->dbd_cap, "Too many DTX: %d/%d\n",
		  count, dbd->dbd_cap);

	if (count > 1) {
		D_ALLOC(dce_df, sizeof(*dce_df) * count);
		if (dce_df == NULL) {
			D_ERROR("Not enough DRAM to commit "DF_DTI"\n",
				DP_DTI(&dtis[cur]));
			return committed > 0 ? 0 : -DER_NOMEM;
		}
	} else {
		dce_df = &dbd->dbd_commmitted_data[0];
	}

	if (dbd_prev == NULL) {
		D_ASSERT(umoff_is_null(cont_df->cd_dtx_committed_head));
		D_ASSERT(umoff_is_null(cont_df->cd_dtx_committed_tail));

		/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
		umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
				sizeof(cont_df->cd_dtx_committed_head) +
				sizeof(cont_df->cd_dtx_committed_tail));
		cont_df->cd_dtx_committed_head = dbd_off;
	} else {
		umem_tx_add_ptr(umm, &dbd_prev->dbd_next,
				sizeof(dbd_prev->dbd_next));
		dbd_prev->dbd_next = dbd_off;

		umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				sizeof(cont_df->cd_dtx_committed_tail));
	}

	cont_df->cd_dtx_committed_tail = dbd_off;

	for (i = 0, j = 0; i < count; i++, cur++) {
		struct vos_dtx_cmt_ent	*dce = NULL;

		rc = vos_dtx_commit_one(cont, &dtis[cur], epoch, &dce);
		if (rc == 0 && dce != NULL)
			committed++;

		if (rc1 == 0)
			rc1 = rc;

		if (dce != NULL) {
			memcpy(&dce_df[j], &dce->dce_base, sizeof(dce_df[j]));
			j++;
		}
	}

	if (dce_df != &dbd->dbd_commmitted_data[0]) {
		if (j > 0)
			memcpy(&dbd->dbd_commmitted_data[0], dce_df,
			       sizeof(*dce_df) * j);
		D_FREE(dce_df);
	}

	dbd->dbd_count = j;

	return committed > 0 ? 0 : rc1;
}

int
vos_dtx_commit(daos_handle_t coh, struct dtx_id *dtis, int count)
{
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Commit multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		rc = vos_dtx_commit_internal(cont, dtis, count, 0);
		rc = umem_tx_end(vos_cont2umm(cont), rc);
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
	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		int	aborted = 0;

		for (i = 0; i < count; i++) {
			rc = vos_dtx_abort_one(cont, epoch, &dtis[i]);
			if (rc == 0)
				aborted++;
		}

		/* Some vos_dtx_abort_one may hit failure, for example, not
		 * found related DTX entry in the active DTX table, that is
		 * not important, go ahead. Because each DTX is independent
		 * from the others. For the DTXs that have been aborted, we
		 * cannot re-insert them back into the active DTX table (in
		 * DRAM) even if we abort this PMDK transaction, then let's
		 * commit the PMDK transaction anyway.
		 */
		rc = umem_tx_end(vos_cont2umm(cont), aborted > 0 ? 0 : rc);
	}

	return rc;
}

int
vos_dtx_aggregate(daos_handle_t coh)
{
	struct vos_container		*cont;
	struct vos_cont_df		*cont_df;
	struct umem_instance		*umm;
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*tmp;
	umem_off_t			 dbd_off;
	int				 rc;
	int				 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = vos_cont2umm(cont);
	cont_df = cont->vc_cont_df;

	dbd_off = cont_df->cd_dtx_committed_head;
	dbd = umem_off2ptr(umm, dbd_off);
	if (dbd == NULL || dbd->dbd_count == 0)
		return 0;

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		return rc;

	for (i = 0; i < dbd->dbd_count &&
	     !d_list_empty(&cont->vc_dtx_committed_list); i++) {
		struct vos_dtx_cmt_ent	*dce;
		d_iov_t			 kiov;

		dce = d_list_entry(cont->vc_dtx_committed_list.next,
				   struct vos_dtx_cmt_ent, dce_committed_link);
		d_iov_set(&kiov, &DCE_XID(dce), sizeof(DCE_XID(dce)));
		dbtree_delete(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			      &kiov, NULL);
	}

	tmp = umem_off2ptr(umm, dbd->dbd_next);
	if (tmp == NULL) {
		/* The last blob for committed DTX blob. */
		D_ASSERT(cont_df->cd_dtx_committed_tail ==
			 cont_df->cd_dtx_committed_head);

		umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				sizeof(cont_df->cd_dtx_committed_tail));
		cont_df->cd_dtx_committed_tail = UMOFF_NULL;
	} else {
		umem_tx_add_ptr(umm, &tmp->dbd_prev, sizeof(tmp->dbd_prev));
		tmp->dbd_prev = UMOFF_NULL;
	}

	umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			sizeof(cont_df->cd_dtx_committed_head));
	cont_df->cd_dtx_committed_head = dbd->dbd_next;

	umem_free(umm, dbd_off);

	return umem_tx_end(umm, 0);
}

void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat)
{
	struct vos_container	*cont;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	stat->dtx_committable_count = cont->vc_dtx_committable_count;
	stat->dtx_oldest_committable_time = vos_dtx_cos_oldest(cont);
	stat->dtx_committed_count = cont->vc_dtx_committed_count;
	if (d_list_empty(&cont->vc_dtx_committed_list)) {
		stat->dtx_oldest_committed_time = 0;
	} else {
		struct vos_dtx_cmt_ent	*dce;

		dce = d_list_entry(cont->vc_dtx_committed_list.next,
				   struct vos_dtx_cmt_ent, dce_committed_link);
		stat->dtx_oldest_committed_time = DCE_EPOCH(dce);
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
			  DAOS_INTENT_DEFAULT, true, &obj, 0);
	if (rc != 0) {
		D_ERROR(DF_UOID" fail to mark sync: rc = "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		return rc;
	}

	if (obj->obj_df != NULL && obj->obj_df->vo_sync < epoch) {
		D_INFO("Update sync epoch "DF_U64" => "DF_U64
		       " for the obj "DF_UOID"\n",
		       obj->obj_sync_epoch, epoch, DP_UOID(oid));

		obj->obj_sync_epoch = epoch;
		pmemobj_memcpy_persist(vos_cont2umm(cont)->umm_pool,
				       &obj->obj_df->vo_sync, &epoch,
				       sizeof(obj->obj_df->vo_sync));
	}

	vos_obj_release(occ, obj, false);
	return 0;
}

int
vos_dtx_act_reindex(struct vos_container *cont)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_cont_df		*cont_df = cont->vc_cont_df;
	struct vos_dtx_blob_df		*dbd;
	umem_off_t			 dbd_off = cont_df->cd_dtx_active_head;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;
	int				 i;

	while (1) {
		dbd = umem_off2ptr(umm, dbd_off);
		if (dbd == NULL)
			break;

		D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);

		for (i = 0; i < dbd->dbd_index; i++) {
			struct vos_dtx_act_ent_df	*dae_df;
			struct vos_dtx_act_ent		*dae;
			int				 count;

			dae_df = &dbd->dbd_active_data[i];
			D_ALLOC_PTR(dae);
			if (dae == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			memcpy(&dae->dae_base, dae_df, sizeof(dae->dae_base));
			dae->dae_df_off = umem_ptr2off(umm, dae_df);
			dae->dae_dbd = dbd;

			if (DAE_REC_CNT(dae) <= DTX_INLINE_REC_CNT)
				goto insert;

			count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
			D_ALLOC(dae->dae_records,
				sizeof(*dae->dae_records) * count);
			if (dae->dae_records == NULL) {
				D_FREE_PTR(dae);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			memcpy(dae->dae_records,
			       umem_off2ptr(umm, dae_df->dae_rec_off),
			       sizeof(*dae->dae_records) * count);
			dae->dae_rec_cap = count;

insert:
			d_iov_set(&kiov, &DAE_XID(dae), sizeof(DAE_XID(dae)));
			d_iov_set(&riov, dae, sizeof(*dae));
			rc = dbtree_upsert(cont->vc_dtx_active_hdl,
					   BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
					   &kiov, &riov);
			if (rc != 0) {
				if (dae->dae_records != NULL)
					D_FREE(dae->dae_records);
				D_FREE_PTR(dae);
				goto out;
			}
		}

		dbd_off = dbd->dbd_next;
	}

out:
	return rc > 0 ? 0 : rc;
}

int
vos_dtx_cmt_reindex(daos_handle_t coh, void *hint)
{
	struct umem_instance		*umm;
	struct vos_container		*cont;
	struct vos_cont_df		*cont_df;
	struct vos_dtx_cmt_ent		*dce;
	struct vos_dtx_blob_df		*dbd;
	umem_off_t			*dbd_off = hint;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;
	int				 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = vos_cont2umm(cont);
	cont_df = cont->vc_cont_df;

	if (umoff_is_null(*dbd_off))
		dbd = umem_off2ptr(umm, cont_df->cd_dtx_committed_head);
	else
		dbd = umem_off2ptr(umm, *dbd_off);

	if (dbd == NULL)
		D_GOTO(out, rc = 1);

	D_ASSERT(dbd->dbd_magic == DTX_CMT_BLOB_MAGIC);

	cont->vc_reindex_cmt_dtx = 1;

	for (i = 0; i < dbd->dbd_count; i++) {
		if (daos_is_zero_dti(&dbd->dbd_commmitted_data[i].dce_xid) ||
		    dbd->dbd_commmitted_data[i].dce_epoch == 0) {
			D_WARN("Skip invalid committed DTX entry\n");
			continue;
		}

		D_ALLOC_PTR(dce);
		if (dce == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(&dce->dce_base, &dbd->dbd_commmitted_data[i],
		       sizeof(dce->dce_base));
		dce->dce_reindex = 1;

		d_iov_set(&kiov, &DCE_XID(dce), sizeof(DCE_XID(dce)));
		d_iov_set(&riov, dce, sizeof(*dce));
		rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
				   DAOS_INTENT_UPDATE, &kiov, &riov);
		if (rc != 0)
			goto out;

		/* The committed DTX entry is already in the index.
		 * Related re-index logic can stop.
		 */
		if (dce->dce_exist)
			D_GOTO(out, rc = 1);
	}

	if (dbd->dbd_count < dbd->dbd_cap || umoff_is_null(dbd->dbd_next))
		D_GOTO(out, rc = 1);

	*dbd_off = dbd->dbd_next;

out:
	if (rc > 0) {
		d_list_splice_init(&cont->vc_dtx_committed_tmp_list,
				   &cont->vc_dtx_committed_list);
		cont->vc_dtx_committed_count +=
				cont->vc_dtx_committed_tmp_count;
		cont->vc_dtx_committed_tmp_count = 0;
		cont->vc_reindex_cmt_dtx = 0;
	}

	return rc;
}

void
vos_dtx_cleanup_dth(struct dtx_handle *dth)
{
	d_iov_t	kiov;
	int	rc;

	if (dth == NULL || !dth->dth_actived)
		return;

	d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
	rc = dbtree_delete(vos_hdl2cont(dth->dth_coh)->vc_dtx_active_hdl,
			   BTR_PROBE_EQ, &kiov, NULL);
	if (rc != 0)
		D_ERROR(DF_UOID" failed to remove DTX entry "DF_DTI": %d\n",
			DP_UOID(dth->dth_oid), DP_DTI(&dth->dth_xid), rc);
	else
		dth->dth_actived = 0;
}
