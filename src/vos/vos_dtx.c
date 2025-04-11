/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos two-phase commit transaction.
 *
 * vos/vos_dtx.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_srv/vos.h>
#include <daos/common.h>
#include "vos_layout.h"
#include "vos_internal.h"

/* 16 KB blob for each active DTX blob */
#define DTX_ACT_BLOB_SIZE	(1 << 14)
/* 4 KB for committed DTX blob */
#define DTX_CMT_BLOB_SIZE	(1 << 12)

#define DTX_ACT_BLOB_MAGIC	0x14130a2b
#define DTX_CMT_BLOB_MAGIC	0x2502191c

#define DTX_UMOFF_TYPES		(DTX_UMOFF_ILOG | DTX_UMOFF_SVT | DTX_UMOFF_EVT)
#define DTX_INDEX_INVAL		(int32_t)(-1)

#define dtx_evict_lid(cont, dae)							\
	do {										\
		if (dae->dae_dth != NULL && dae->dae_dth->dth_ent != NULL) {		\
			D_ASSERT(dae->dae_dth->dth_ent == dae);				\
			dae->dae_dth->dth_ent = NULL;					\
		}									\
		D_DEBUG(DB_IO, "Evicting DTX "DF_DTI": lid=%x\n",			\
			DP_DTI(&DAE_XID(dae)), DAE_LID(dae));				\
		d_list_del_init(&dae->dae_link);					\
		lrua_evictx(cont->vc_dtx_array,						\
			    (DAE_LID(dae) & DTX_LID_SOLO_MASK) - DTX_LID_RESERVED,	\
			    DAE_EPOCH(dae));						\
	} while (0)

bool vos_skip_old_partial_dtx;

static inline uint32_t
dtx_umoff_flag2type(umem_off_t umoff)
{
	switch (umem_off2flags(umoff) & DTX_UMOFF_TYPES) {
	case DTX_UMOFF_ILOG:
		return DTX_RT_ILOG;
	case DTX_UMOFF_SVT:
		return DTX_RT_SVT;
	case DTX_UMOFF_EVT:
		return DTX_RT_EVT;
	default:
		D_ASSERT(0);
	}

	return 0;
}

static int
dtx_inprogress(struct vos_dtx_act_ent *dae, struct dtx_handle *dth,
	       bool hit_again, bool retry, int pos)
{
	struct dtx_share_peer	*dsp;
	struct dtx_memberships	*mbs;
	bool			 s_try = false;

	if (!retry)
		return -DER_INPROGRESS;

	if (dth == NULL)
		goto out;

	if (DAE_FLAGS(dae) & DTE_LEADER)
		goto out;

	/* If hit the same non-committed DTX again after once dtx_refresh,
	 * then ask client to retry, the leader may be waiting non-leader
	 * to reply at that time.
	 */
	if (hit_again) {
		while ((dsp = d_list_pop_entry(&dth->dth_share_tbd_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);
		dth->dth_share_tbd_count = 0;
		goto out;
	}

	s_try = true;

	d_list_for_each_entry(dsp, &dth->dth_share_tbd_list, dsp_link) {
		if (memcmp(&dsp->dsp_xid, &DAE_XID(dae),
			   sizeof(struct dtx_id)) == 0)
			goto out;
	}

	if (dth->dth_share_tbd_count >= DTX_REFRESH_MAX)
		goto out;

	D_ALLOC(dsp, sizeof(*dsp) + sizeof(*mbs) + DAE_MBS_DSIZE(dae));
	if (dsp == NULL) {
		D_ERROR("Hit uncommitted DTX "DF_DTI" at %d: lid=%d, "
			"but fail to alloc DRAM.\n",
			DP_DTI(&DAE_XID(dae)), pos, DAE_LID(dae));
		return -DER_NOMEM;
	}

	dsp->dsp_xid = DAE_XID(dae);
	dsp->dsp_oid = DAE_OID(dae);
	dsp->dsp_epoch = DAE_EPOCH(dae);
	dsp->dsp_version = DAE_VER(dae);
	dsp->dsp_dkey_hash = DAE_DKEY_HASH(dae);

	mbs = (struct dtx_memberships *)(dsp + 1);
	mbs->dm_tgt_cnt = DAE_TGT_CNT(dae);
	mbs->dm_grp_cnt = DAE_GRP_CNT(dae);
	mbs->dm_data_size = DAE_MBS_DSIZE(dae);
	mbs->dm_flags = DAE_MBS_FLAGS(dae);
	mbs->dm_dte_flags = DAE_FLAGS(dae);
	if (DAE_MBS_DSIZE(dae) <= sizeof(DAE_MBS_INLINE(dae))) {
		memcpy(mbs->dm_data, DAE_MBS_INLINE(dae), DAE_MBS_DSIZE(dae));
	} else {
		struct umem_instance	*umm;

		umm = vos_cont2umm(vos_hdl2cont(dth->dth_coh));
		memcpy(mbs->dm_data, umem_off2ptr(umm, DAE_MBS_OFF(dae)),
		       DAE_MBS_DSIZE(dae));
	}

	dsp->dsp_inline_mbs = 1;
	dsp->dsp_mbs = mbs;

	d_list_add_tail(&dsp->dsp_link, &dth->dth_share_tbd_list);
	dth->dth_share_tbd_count++;

out:
	D_DEBUG(DB_IO,
		"%s hit uncommitted DTX "DF_DTI" at %d: dth %p (dist %s), lid=%d, flags %x/%x, "
		"ver %u/%u, may need %s retry.\n", hit_again ? "Repeat" : "First",
		DP_DTI(&DAE_XID(dae)), pos, dth, dth != NULL && dth->dth_dist ? "yes" : "no",
		DAE_LID(dae), DAE_FLAGS(dae), DAE_MBS_FLAGS(dae), DAE_VER(dae),
		dth != NULL ? dth->dth_ver : 0, s_try ? "server" : "client");

	return -DER_INPROGRESS;
}

static void
dtx_act_ent_cleanup(struct vos_container *cont, struct vos_dtx_act_ent *dae,
		    struct dtx_handle *dth, bool evict, bool keep_df)
{
	if (evict) {
		daos_unit_oid_t	*oids;
		int		 count;
		int		 i;

		if (dth != NULL) {
			if (dth->dth_oid_array != NULL) {
				D_ASSERT(dth->dth_oid_cnt > 0);

				count = dth->dth_oid_cnt;
				oids = dth->dth_oid_array;
			} else {
				count = 1;
				oids = &dth->dth_leader_oid;
			}
		} else {
			count = dae->dae_oid_cnt;
			oids = dae->dae_oids;
		}

		for (i = 0; i < count; i++)
			vos_obj_evict_by_oid(cont, oids[i]);
	}

	if (dae->dae_oids != NULL && dae->dae_oids != &dae->dae_oid_inline &&
	    dae->dae_oids != &DAE_OID(dae)) {
		D_FREE(dae->dae_oids);
		dae->dae_oid_cnt = 0;
	}

	DAE_REC_OFF(dae) = UMOFF_NULL;
	D_FREE(dae->dae_records);
	dae->dae_rec_cap = 0;
	DAE_REC_CNT(dae) = 0;
	dae->dae_need_release = 0;

	if (!keep_df) {
		dae->dae_df_off = UMOFF_NULL;
		dae->dae_dbd = NULL;
	}
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
		  d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
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

	if (dae != NULL) {
		d_list_del_init(&dae->dae_order_link);
		d_list_del_init(&dae->dae_link);
	}

	if (args != NULL) {
		/* Return the record addreass (offset in DRAM).
		* The caller will release it after using.
		*/
		D_ASSERT(dae != NULL);
		*(struct vos_dtx_act_ent **)args = dae;
	} else if (dae != NULL) {
		dtx_act_ent_cleanup(tins->ti_priv, dae, NULL, true, false);
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
		   d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct vos_container	*cont = tins->ti_priv;
	struct vos_dtx_act_ent	*dae_new = val->iov_buf;
	struct vos_dtx_act_ent	*dae_old;

	dae_old = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	D_ASSERT(dae_old != dae_new);

	if (unlikely(dae_old->dae_aborting)) {
		D_ERROR("Hit former in-aborting DTX entry %p "DF_DTI"\n",
			dae_old, DP_DTI(&DAE_XID(dae_old)));
		return -DER_INPROGRESS;
	}

	if (unlikely(!dae_old->dae_aborted)) {
		/*
		 * If the new entry and the old entry are for the same transaction, then the RPC
		 * for the new one will take 'RESEND' flag, that will cause the old one has been
		 * aborted before arriving at here. So it is quite possible that the new one and
		 * the old one are for different transactions.
		 */
		if (DAE_EPOCH(dae_old) < DAE_EPOCH(dae_new)) {
			D_ERROR("The TX ID "DF_DTI" may be reused for epoch "DF_X64" vs "DF_X64"\n",
				DP_DTI(&DAE_XID(dae_old)), DAE_EPOCH(dae_old), DAE_EPOCH(dae_new));
			return -DER_TX_ID_REUSED;
		}

		/*
		 * If the old entry has higher epoch, it is quite possible that the resent RPC
		 * was handled before the original RPC (corresponding to 'dae_new'). Returning
		 * -DER_INPROGRESS to make the RPC sponsor to retry the RPC with 'RESEND' flag,
		 *  then related RPC handler logic will handle such case.
		 */
		if (DAE_EPOCH(dae_old) > DAE_EPOCH(dae_new)) {
			D_ERROR("Resent RPC may be handled before original one for DTX "DF_DTI
				" with epoch "DF_X64" vs "DF_X64"\n",
				DP_DTI(&DAE_XID(dae_old)), DAE_EPOCH(dae_old), DAE_EPOCH(dae_new));
			return -DER_INPROGRESS;
		}

		/*
		 * The two entries uses the same epoch, then it may be caused by repeated RPCs
		 * from different sources, such as multiple relay engines forward the same RPC
		 * to current target. We need to notify related caller for such buggy case.
		 */
		D_ERROR("Receive repeated DTX "DF_DTI" with epoch "DF_X64"\n",
			DP_DTI(&DAE_XID(dae_old)), DAE_EPOCH(dae_old));
		return -DER_MISC;
	}

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dae_new);
	dtx_evict_lid(cont, dae_old);

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
		  d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct vos_dtx_cmt_ent	*dce = val_iov->iov_buf;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dce);

	return 0;
}

static int
dtx_cmt_ent_free(struct btr_instance *tins, struct btr_record *rec,
		 void *args)
{
	struct vos_dtx_cmt_ent	*dce;

	dce = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	D_ASSERT(dce != NULL);

	rec->rec_off = UMOFF_NULL;
	D_FREE(dce);

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
		   d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct vos_dtx_cmt_ent	*dce_new = val->iov_buf;
	struct vos_dtx_cmt_ent	*dce_old;

	dce_old = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	/* Two possible cases for that:
	 *
	 * Case one:
	 * It is possible that when commit the DTX for the first time,
	 * it failed at removing the DTX entry from active table, but
	 * at that time the DTX entry has already been added into the
	 * committed table that is in DRAM. Currently, we do not have
	 * efficient way to recover such DRAM based btree structure,
	 * so just keep it there with 'dce_invalid' flags. Then when
	 * we re-commit such DTX, we may come here.
	 *
	 * Case two:
	 * As the vos_dtx_cmt_reindex() logic going, some RPC handler
	 * ULT may add more entries into the committed table. Then it
	 * is possible that vos_dtx_cmt_reindex() logic hit the entry
	 * in the committed blob that has already been added into the
	 * indexed table.
	 */

	if (dce_old->dce_invalid) {
		rec->rec_off = umem_ptr2off(&tins->ti_umm, dce_new);
		D_FREE(dce_old);
	} else if (!dce_old->dce_reindex) {
		D_ASSERTF(dce_new->dce_reindex, "Repeatedly commit DTX "DF_DTI"\n",
			  DP_DTI(&DCE_XID(dce_new)));
		dce_new->dce_exist = 1;
	}

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

	rc = dbtree_class_register(VOS_BTR_DTX_CMT_TABLE,
				   BTR_FEAT_SKIP_LEAF_REBAL,
				   &dtx_committed_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX committed dbtree: %d\n", rc);

	return rc;
}

int
vos_dtx_table_destroy(struct umem_instance *umm, struct vos_cont_df *cont_df)
{
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_act_ent_df	*dae_df;
	umem_off_t			 dbd_off;
	int				 i;
	int				 rc;

	/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
	rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			     sizeof(cont_df->cd_dtx_committed_head) +
			     sizeof(cont_df->cd_dtx_committed_tail));
	if (rc != 0)
		return rc;

	while (!UMOFF_IS_NULL(cont_df->cd_dtx_committed_head)) {
		dbd_off = cont_df->cd_dtx_committed_head;
		dbd = umem_off2ptr(umm, dbd_off);
		cont_df->cd_dtx_committed_head = dbd->dbd_next;
		rc = umem_free(umm, dbd_off);
		if (rc != 0)
			return rc;
	}

	cont_df->cd_dtx_committed_head = UMOFF_NULL;
	cont_df->cd_dtx_committed_tail = UMOFF_NULL;

	/* cd_dtx_active_tail is next to cd_dtx_active_head */
	rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
			     sizeof(cont_df->cd_dtx_active_head) +
			     sizeof(cont_df->cd_dtx_active_tail));
	if (rc != 0)
		return rc;

	while (!UMOFF_IS_NULL(cont_df->cd_dtx_active_head)) {
		dbd_off = cont_df->cd_dtx_active_head;
		dbd = umem_off2ptr(umm, dbd_off);

		for (i = 0; i < dbd->dbd_index; i++) {
			dae_df = &dbd->dbd_active_data[i];
			if (!(dae_df->dae_flags & DTE_INVALID)) {
				if (!UMOFF_IS_NULL(dae_df->dae_rec_off)) {
					rc = umem_free(umm, dae_df->dae_rec_off);
					if (rc != 0)
						return rc;
				}
				if (!UMOFF_IS_NULL(dae_df->dae_mbs_off)) {
					rc = umem_free(umm, dae_df->dae_mbs_off);
					if (rc != 0)
						return rc;
				}
			}
		}

		cont_df->cd_dtx_active_head = dbd->dbd_next;
		rc = umem_free(umm, dbd_off);
		if (rc != 0)
			return rc;
	}

	cont_df->cd_dtx_active_head = UMOFF_NULL;
	cont_df->cd_dtx_active_tail = UMOFF_NULL;

	return 0;
}

static int
dtx_ilog_rec_release(struct umem_instance *umm, struct vos_container *cont,
		     umem_off_t rec, struct vos_dtx_act_ent *dae, bool abort)
{
	struct ilog_df		*ilog;
	daos_handle_t		 loh;
	struct ilog_desc_cbs	 cbs;
	struct ilog_id		 id;
	int			 rc;

	ilog = umem_off2ptr(umm, umem_off2offset(rec));

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
	rc = ilog_open(umm, ilog, &cbs, false, &loh);
	if (rc != 0)
		return rc;

	id.id_epoch = DAE_EPOCH(dae);
	id.id_tx_id = DAE_LID(dae);

	if (abort)
		rc = ilog_abort(loh, &id);
	else
		rc = ilog_persist(loh, &id);

	ilog_close(loh);

	if (rc != 0 && rc != -DER_NONEXIST)
		D_ERROR("Failed to release ilog rec for "DF_DTI", abort %s: "DF_RC"\n",
			DP_DTI(&DAE_XID(dae)), abort ? "yes" : "no", DP_RC(rc));

	return rc;
}

static int
do_dtx_rec_release(struct umem_instance *umm, struct vos_container *cont,
		   struct vos_dtx_act_ent *dae, umem_off_t rec, bool abort)
{
	int	rc = 0;

	if (UMOFF_IS_NULL(rec))
		return 0;

	switch (dtx_umoff_flag2type(rec)) {
	case DTX_RT_ILOG: {
		rc = dtx_ilog_rec_release(umm, cont, rec, dae, abort);
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df	*svt;

		svt = umem_off2ptr(umm, umem_off2offset(rec));

		if (!vos_irec_is_valid(svt, DAE_LID(dae))) {
			rc = -DER_NONEXIST;
			break;
		}
		if (abort) {
			if (DAE_INDEX(dae) != DTX_INDEX_INVAL) {
				rc = umem_tx_add_ptr(umm, &svt->ir_dtx,
						     sizeof(svt->ir_dtx));
				if (rc != 0)
					return rc;
			}

			dtx_set_aborted(&svt->ir_dtx);
		} else {
			rc = umem_tx_add_ptr(umm, &svt->ir_dtx,
					     sizeof(svt->ir_dtx));
			if (rc != 0)
				return rc;

			dtx_set_committed(&svt->ir_dtx);
		}
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc		*evt;

		evt = umem_off2ptr(umm, umem_off2offset(rec));

		if (!evt_desc_is_valid(evt, DAE_LID(dae))) {
			rc = -DER_NONEXIST;
			break;
		}

		if (abort) {
			if (DAE_INDEX(dae) != DTX_INDEX_INVAL) {
				rc = umem_tx_add_ptr(umm, &evt->dc_dtx,
						     sizeof(evt->dc_dtx));
				if (rc != 0)
					return rc;
			}

			dtx_set_aborted(&evt->dc_dtx);
		} else {
			rc = umem_tx_add_ptr(umm, &evt->dc_dtx,
					     sizeof(evt->dc_dtx));
			if (rc != 0)
				return rc;

			dtx_set_committed(&evt->dc_dtx);
		}
		break;
	}
	default:
		/* On-disk data corruption case. */
		rc = -DER_IO;
		D_ERROR("Unknown DTX "DF_DTI" type %u\n",
			DP_DTI(&DAE_XID(dae)), dtx_umoff_flag2type(rec));
		break;
	}

	if (rc == -DER_NONEXIST) {
		D_WARN("DTX record no longer exists, may indicate some corruption: "
		       DF_DTI " type %u, discard\n",
		       DP_DTI(&DAE_XID(dae)), dtx_umoff_flag2type(rec));
		d_tm_inc_counter(vos_tls_get(false)->vtl_invalid_dtx, 1);
	}

	return rc;
}

static int
dtx_rec_release(struct vos_container *cont, struct vos_dtx_act_ent *dae, bool abort, bool keep_act)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_dtx_blob_df		*dbd;
	bool				 invalid = false;
	int				 count;
	int				 i;
	int				 rc = 0;

	if (unlikely(dae->dae_need_release == 0))
		return 0;

	/* In spite of for commit or abort, the DTX must be local preparing/prepared. */
	D_ASSERTF(vos_dae_is_prepare(dae),
		  "Unexpected DTX "DF_DTI" status for %s in pool "DF_UUID" cont "DF_UUID"\n",
		  DP_DTI(&DAE_XID(dae)), abort ? "abort" : "commit",
		  DP_UUID(cont->vc_pool->vp_id), DP_UUID(cont->vc_id));

	dbd = dae->dae_dbd;
	dae_df = umem_off2ptr(umm, dae->dae_df_off);

	D_ASSERTF(dae_df != NULL, "Hit invalid DTX entry "DF_DTI" when release for %s in pool "
		  DF_UUID" cont "DF_UUID"\n", DP_DTI(&DAE_XID(dae)), abort ? "abort" : "commit",
		  DP_UUID(cont->vc_pool->vp_id), DP_UUID(cont->vc_id));
	D_ASSERTF(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC,
		  "Bad blob %p magic %x for "DF_DTI" (lid %x) in pool "DF_UUID" cont "DF_UUID"\n",
		  dbd, dbd->dbd_magic, DP_DTI(&DAE_XID(dae)), DAE_LID(dae),
		  DP_UUID(cont->vc_pool->vp_id), DP_UUID(cont->vc_id));
	D_ASSERTF(dbd->dbd_index > 0,
		  "%s DTX "DF_DTI" against new DTX blob %p in pool "DF_UUID" cont "DF_UUID"\n",
		   abort ? "abort" : "commit", DP_DTI(&DAE_XID(dae)), dbd,
		   DP_UUID(cont->vc_pool->vp_id), DP_UUID(cont->vc_id));

	/* Handle DTX records as FIFO order to find out potential invalid DTX earlier. */

	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = 0; i < count; i++) {
		rc = do_dtx_rec_release(umm, cont, dae, DAE_REC_INLINE(dae)[i], abort);
		if (unlikely(rc == -DER_NONEXIST)) {
			invalid = true;
			break;
		}
		if (rc != 0)
			return rc;
	}

	if (!invalid && dae->dae_records != NULL) {
		D_ASSERT(DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT);
		D_ASSERT(!UMOFF_IS_NULL(dae_df->dae_rec_off));

		for (i = 0; i < DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT; i++) {
			rc = do_dtx_rec_release(umm, cont, dae, dae->dae_records[i], abort);
			if (unlikely(rc == -DER_NONEXIST)) {
				invalid = true;
				break;
			}
			if (rc != 0)
				return rc;
		}
	}

	if (!UMOFF_IS_NULL(dae_df->dae_rec_off)) {
		rc = umem_free(umm, dae_df->dae_rec_off);
		if (rc != 0)
			return rc;

		if (!invalid && keep_act) {
			rc = umem_tx_add_ptr(umm, &dae_df->dae_rec_off, sizeof(dae_df->dae_rec_off));
			if (rc != 0)
				return rc;
			dae_df->dae_rec_off = UMOFF_NULL;
		}
	}

	if (!invalid && keep_act) {
		/* When re-commit partial committed DTX, the count can be zero. */
		if (dae_df->dae_rec_cnt > 0) {
			rc = umem_tx_add_ptr(umm, &dae_df->dae_rec_cnt,
					     sizeof(dae_df->dae_rec_cnt));
			if (rc != 0)
				return rc;

			dae_df->dae_rec_cnt = 0;
		}

		/*
		 * If it is required to keep the active DTX entry, then it must be for partial
		 * commit. Let's mark it as DTE_PARTIAL_COMMITTED.
		 */
		if (DAE_FLAGS(dae) & DTE_PARTIAL_COMMITTED)
			return 0;

		rc = umem_tx_add_ptr(umm, &dae_df->dae_flags, sizeof(dae_df->dae_flags));
		if (rc != 0)
			return rc;

		dae_df->dae_flags |= DTE_PARTIAL_COMMITTED;

		return 0;
	}

	if (invalid)
		rc = 0;

	if (!UMOFF_IS_NULL(dae_df->dae_mbs_off)) {
		/* dae_mbs_off will be invalid via flag DTE_INVALID. */
		rc = umem_free(umm, dae_df->dae_mbs_off);
		if (rc != 0)
			return rc;
	}

	if (dbd->dbd_count > 1 || dbd->dbd_index < dbd->dbd_cap) {
		rc = umem_tx_add_ptr(umm, &dae_df->dae_flags, sizeof(dae_df->dae_flags));
		if (rc != 0)
			return rc;

		rc = umem_tx_add_ptr(umm, &dbd->dbd_count, sizeof(dbd->dbd_count));
		if (rc != 0)
			return rc;

		/* Mark the DTX entry as invalid persistently. */
		dae_df->dae_flags = DTE_INVALID;
		dbd->dbd_count--;
	} else {
		struct vos_cont_df	*cont_df = cont->vc_cont_df;
		umem_off_t		 dbd_off;
		struct vos_dtx_blob_df	*tmp;

		dbd_off = umem_ptr2off(umm, dbd);
		tmp = umem_off2ptr(umm, dbd->dbd_prev);
		if (tmp != NULL) {
			rc = umem_tx_add_ptr(umm, &tmp->dbd_next,
					     sizeof(tmp->dbd_next));
			if (rc != 0)
				return rc;

			tmp->dbd_next = dbd->dbd_next;
		}

		tmp = umem_off2ptr(umm, dbd->dbd_next);
		if (tmp != NULL) {
			rc = umem_tx_add_ptr(umm, &tmp->dbd_prev,
					     sizeof(tmp->dbd_prev));
			if (rc != 0)
				return rc;

			tmp->dbd_prev = dbd->dbd_prev;
		}

		if (cont_df->cd_dtx_active_head == dbd_off) {
			rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
					sizeof(cont_df->cd_dtx_active_head));
			if (rc != 0)
				return rc;

			cont_df->cd_dtx_active_head = dbd->dbd_next;
		}

		if (cont_df->cd_dtx_active_tail == dbd_off) {
			rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_tail,
					sizeof(cont_df->cd_dtx_active_tail));
			if (rc != 0)
				return rc;

			cont_df->cd_dtx_active_tail = dbd->dbd_prev;
		}

		rc = umem_free(umm, dbd_off);

		DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc,
			  "Release DTX active blob %p (" UMOFF_PF ") for cont " DF_UUID, dbd,
			  UMOFF_P(dbd_off), DP_UUID(cont->vc_id));
	}

	return rc;
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct dtx_id *dti, daos_epoch_t epoch,
		   daos_epoch_t cmt_time, bool keep_act, struct vos_dtx_cmt_ent **dce_p,
		   struct vos_dtx_act_ent **dae_p, bool *rm_cos)
{
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_dtx_cmt_ent		*dce = NULL;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;

	d_iov_set(&kiov, dti, sizeof(*dti));
	/* For single replicated object, we trigger commit just after local
	 * modification done. Under such case, the caller exactly knows the
	 * @epoch and no need to lookup the active DTX table.
	 */
	if (epoch == 0) {
		d_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc == -DER_NONEXIST) {
			rc = dbtree_lookup(cont->vc_dtx_committed_hdl,
					   &kiov, &riov);
			if (rc == 0) {
				dce = (struct vos_dtx_cmt_ent *)riov.iov_buf;
				if (dce->dce_invalid)
					rc = -DER_NONEXIST;
				else
					rc = -DER_ALREADY;
				dce = NULL;
			}
		}

		if (rc != 0)
			goto out;

		dae = riov.iov_buf;
		D_ASSERT(dae->dae_preparing == 0);

		if (vos_dae_is_abort(dae)) {
			D_ERROR("NOT allow to commit an aborted DTX "DF_DTI"\n",
				DP_DTI(dti));
			D_GOTO(out, rc = -DER_NONEXIST);
		}

		/* It has been committed before, but failed to be removed
		 * from the active table, just remove it again.
		 */
		if (unlikely(dae->dae_committed)) {
			rc = dbtree_delete(cont->vc_dtx_active_hdl,
					   BTR_PROBE_BYPASS, &kiov, &dae);
			if (rc == 0) {
				dtx_act_ent_cleanup(cont, dae, NULL, false, false);
				dtx_evict_lid(cont, dae);
			}

			goto out;
		}

		/* Another ULT is committing the DTX, but yield, then regard it as 'committed'.
		 * The former committing ULT will guarantee the DTX to be committed successfully.
		 */
		if (dae->dae_committing)
			D_GOTO(out, rc = -DER_ALREADY);
	} else {
		struct dtx_handle	*dth = vos_dth_get(cont->vc_pool->vp_sysdb);

		D_ASSERT(dtx_is_valid_handle(dth));
		D_ASSERT(dth->dth_ent != NULL);
		D_ASSERT(dth->dth_solo);

		dae = dth->dth_ent;
	}

	/* Generate committed DTX entry when it is not required to keep the active DTX entry. */
	if (!keep_act) {
		D_ALLOC_PTR(dce);
		if (dce == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		DCE_CMT_TIME(dce) = cmt_time;
		DCE_XID(dce) = DAE_XID(dae);
		DCE_EPOCH(dce) = DAE_EPOCH(dae);

		d_iov_set(&riov, dce, sizeof(*dce));
		rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
				   DAOS_INTENT_UPDATE, &kiov, &riov, NULL);
		if (rc != 0)
			goto out;

		*dce_p = dce;
		dce = NULL;
	} else {
		D_ASSERT(rm_cos == NULL);
	}

	dae->dae_committing = 1;

	if (epoch != 0)
		goto out;

	rc = dtx_rec_release(cont, dae, false, keep_act);
	if (rc != 0)
		goto out;

	D_ASSERT(dae_p != NULL);
	*dae_p = dae;

out:
	if (rc != -DER_ALREADY && rc != -DER_NONEXIST)
		DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc, "Commit the DTX " DF_DTI, DP_DTI(dti));

	if (rc != 0)
		D_FREE(dce);

	if (rm_cos != NULL &&
	    ((rc == 0 && !keep_act) || rc == -DER_NONEXIST || (rc == -DER_ALREADY && dae == NULL)))
		*rm_cos = true;

	return rc;
}

static inline const char *
vos_dtx_flags2name(uint32_t flags)
{
	switch (flags) {
	case DTE_CORRUPTED:
		return "corrupted";
	case DTE_ORPHAN:
		return "orphan";
	case DTE_PARTIAL_COMMITTED:
		return "partial_committed";
	default:
		return "unknown";
	}

	return NULL;
}

static int
vos_dtx_extend_act_table(struct vos_container *cont)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_cont_df		*cont_df = cont->vc_cont_df;
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*tmp;
	umem_off_t			 dbd_off;
	int				 rc;

	dbd_off = umem_zalloc(umm, DTX_ACT_BLOB_SIZE);
	if (UMOFF_IS_NULL(dbd_off)) {
		D_ERROR("No space when create active DTX table.\n");
		return -DER_NOSPACE;
	}

	dbd = umem_off2ptr(umm, dbd_off);
	dbd->dbd_magic = DTX_ACT_BLOB_MAGIC;
	dbd->dbd_cap = (DTX_ACT_BLOB_SIZE - sizeof(struct vos_dtx_blob_df)) /
			sizeof(struct vos_dtx_act_ent_df);
	dbd->dbd_count = 0;
	dbd->dbd_index = 0;

	tmp = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	if (tmp == NULL) {
		D_ASSERT(UMOFF_IS_NULL(cont_df->cd_dtx_active_head));

		/* cd_dtx_active_tail is next to cd_dtx_active_head */
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
				     sizeof(cont_df->cd_dtx_active_head) +
				     sizeof(cont_df->cd_dtx_active_tail));
		if (rc != 0)
			goto out;

		cont_df->cd_dtx_active_head = dbd_off;
	} else {
		rc = umem_tx_add_ptr(umm, &tmp->dbd_next,
				     sizeof(tmp->dbd_next));
		if (rc != 0)
			goto out;

		tmp->dbd_next = dbd_off;

		dbd->dbd_prev = cont_df->cd_dtx_active_tail;
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_tail,
				     sizeof(cont_df->cd_dtx_active_tail));
		if (rc != 0)
			goto out;
	}

	cont_df->cd_dtx_active_tail = dbd_off;

out:
	DL_CDEBUG(rc == 0, DB_IO, DLOG_ERR, rc,
		  "Allocated DTX active blob %p ("UMOFF_PF") for cont "DF_UUID,
		  dbd, UMOFF_P(dbd_off), DP_UUID(cont->vc_id));
	return rc;
}

static int
vos_dtx_alloc(struct umem_instance *umm, struct dtx_handle *dth)
{
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_container		*cont;
	uint32_t			 idx;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	uint64_t			 now;
	int				 rc = 0;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	/* Do not allow the modification with too old epoch. */
	if (dth->dth_epoch <= cont->vc_mod_epoch_bound) {
		now = daos_gettime_coarse();
		if (now - cont->vc_dtx_reject_ts > 10) {
			D_WARN("Reject DTX (1) " DF_DTI " with epoch " DF_X64
			       " vs bound " DF_X64 "\n", DP_DTI(&dth->dth_xid),
			       dth->dth_epoch, cont->vc_mod_epoch_bound);
			cont->vc_dtx_reject_ts = now;
		}
		return -DER_TX_RESTART;
	}

	/*
	 * NOTE: For the purpose of efficient calculating container based local stable epoch,
	 *	 we will maintain some kind of sorted list for active DTX entries with epoch
	 *	 order. But consider related overhead, it is not easy to maintain a strictly
	 *	 sorted list for all active DTX entries. For the DTX which leader resides on
	 *	 current target, its epoch is already sorted when generate on current engine.
	 *	 So the main difficulty is for those DTX entries which leaders are on remote
	 *	 targets.
	 *
	 *	 On the other hand, the local stable epoch is mainly used to generate global
	 *	 stable epoch that is for incremental reintegration. In fact, we do not need
	 *	 a very accurate global stable epoch for incremental reintegration. It means
	 *	 that it is no matter (or non-fatal) if the calculated stable epoch is a bit
	 *	 smaller than the real case. For example, seconds error for the stable epoch
	 *	 almost can be ignored if we compare such overhead with rebuilding the whole
	 *	 target from scratch. So for the DTX entry which leader is on remote target,
	 *	 we will maintain it in the list with relative incremental trend based on the
	 *	 epoch instead of strict sorting the epoch. We introduce an O(1) algorithm to
	 *	 handle such unsorted DTX entries list.
	 *
	 *	 For distributed transaction, its epoch may be generated on non-leader.
	 */

	if (!dth->dth_epoch_owner && !d_list_empty(&cont->vc_dtx_unsorted_list)) {
		dae = d_list_entry(cont->vc_dtx_unsorted_list.prev, struct vos_dtx_act_ent,
				   dae_order_link);
		if (dth->dth_epoch < DAE_EPOCH(dae) &&
		    cont->vc_mod_epoch_bound < DAE_EPOCH(dae) - d_sec2hlc(vos_agg_gap)) {
			/*
			 * It guarantees that even if there was some older DTX to be added,
			 * the epoch difference between it and all former added ones cannot
			 * exceed vos_agg_gap. So we can easily calculate the local stable
			 * epoch. Please reference vos_cont_get_local_stable_epoch().
			 */
			D_DEBUG(DB_TRACE, "Increase acceptable modification boundary from "
				DF_X64 " to " DF_X64 " for container " DF_UUID "\n",
				cont->vc_mod_epoch_bound,
				DAE_EPOCH(dae) - d_sec2hlc(vos_agg_gap), DP_UUID(cont->vc_id));
			cont->vc_mod_epoch_bound = DAE_EPOCH(dae) - d_sec2hlc(vos_agg_gap);
			if (dth->dth_epoch <= cont->vc_mod_epoch_bound) {
				now = daos_gettime_coarse();
				if (now - cont->vc_dtx_reject_ts > 10) {
					D_WARN("Reject DTX (2) " DF_DTI " with epoch " DF_X64
					       " vs bound " DF_X64 "\n", DP_DTI(&dth->dth_xid),
					       dth->dth_epoch, cont->vc_mod_epoch_bound);
					cont->vc_dtx_reject_ts = now;
				}
				return -DER_TX_RESTART;
			}
		}
	}

	rc = lrua_allocx(cont->vc_dtx_array, &idx, dth->dth_epoch, &dae, &dth->dth_local_stub);
	if (rc != 0) {
		/* The array is full, need to commit some transactions first */
		if (rc == -DER_BUSY)
			rc = -DER_INPROGRESS;

		D_ERROR("Failed to allocate active DTX entry for "DF_DTI"\n",
			DP_DTI(&dth->dth_xid));
		return rc;
	}

	D_INIT_LIST_HEAD(&dae->dae_link);
	D_INIT_LIST_HEAD(&dae->dae_order_link);
	DAE_LID(dae) = idx + DTX_LID_RESERVED;
	if (dth->dth_solo)
		DAE_LID(dae) |= DTX_LID_SOLO_FLAG;
	DAE_XID(dae) = dth->dth_xid;
	DAE_OID(dae) = dth->dth_leader_oid;
	DAE_DKEY_HASH(dae) = dth->dth_dkey_hash;
	DAE_EPOCH(dae) = dth->dth_epoch;
	DAE_FLAGS(dae) = dth->dth_flags;
	if (dth->dth_epoch_owner)
		DAE_FLAGS(dae) |= DTE_EPOCH_SORTED;
	DAE_VER(dae) = dth->dth_ver;

	if (dth->dth_mbs != NULL) {
		DAE_TGT_CNT(dae) = dth->dth_mbs->dm_tgt_cnt;
		DAE_GRP_CNT(dae) = dth->dth_mbs->dm_grp_cnt;
		DAE_MBS_DSIZE(dae) = dth->dth_mbs->dm_data_size;
		DAE_MBS_FLAGS(dae) = dth->dth_mbs->dm_flags;
	} else {
		DAE_TGT_CNT(dae) = 1;
		DAE_GRP_CNT(dae) = 1;
		DAE_MBS_DSIZE(dae) = 0;
		DAE_MBS_FLAGS(dae) = 0;
	}

	/* Will be set as dbd::dbd_index via vos_dtx_prepared(). */
	DAE_INDEX(dae) = DTX_INDEX_INVAL;
	dae->dae_dth = dth;

	D_DEBUG(DB_IO, "Allocated new DTX " DF_DTI ", lid=%x, epoch " DF_U64 ", dae=%p\n",
		DP_DTI(&dth->dth_xid), DAE_LID(dae), DAE_EPOCH(dae), dae);

	d_iov_set(&kiov, &DAE_XID(dae), sizeof(DAE_XID(dae)));
	d_iov_set(&riov, dae, sizeof(*dae));
	rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov, NULL);
	if (rc == 0) {
		dae->dae_start_time = daos_wallclock_secs();
		d_list_add_tail(&dae->dae_link, &cont->vc_dtx_act_list);
		if (dth->dth_epoch_owner)
			d_list_add_tail(&dae->dae_order_link, &cont->vc_dtx_sorted_list);
		else
			/*
			 * Add all the others, including non-leader(s), into unsorted list.
			 * Then even though the leader was evicted for some reason, related
			 * DTX still can be considered via the new leader on another target.
			 */
			d_list_add_tail(&dae->dae_order_link, &cont->vc_dtx_unsorted_list);
		dth->dth_ent = dae;
	} else {
		dtx_evict_lid(cont, dae);
	}

	return rc;
}

static int
vos_dtx_append(struct dtx_handle *dth, umem_off_t record, uint32_t type)
{
	struct vos_dtx_act_ent		*dae = dth->dth_ent;
	umem_off_t			*rec;

	D_ASSERT(dae != NULL);

	if (DAE_REC_CNT(dae) < DTX_INLINE_REC_CNT) {
		rec = &DAE_REC_INLINE(dae)[DAE_REC_CNT(dae)];
	} else {
		if (DAE_REC_CNT(dae) >= dae->dae_rec_cap + DTX_INLINE_REC_CNT) {
			int	count;

			if (dae->dae_rec_cap == 0)
				count = DTX_INLINE_REC_CNT;
			else
				count = dae->dae_rec_cap * 2;

			D_ALLOC_ARRAY(rec, count);
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

	*rec = record;
	dtx_type2umoff_flag(rec, type);

	/* The rec_cnt on-disk value will be refreshed via vos_dtx_prepared() */
	DAE_REC_CNT(dae)++;

	return 0;
}

static inline uint32_t
vos_dtx_status(struct vos_dtx_act_ent *dae)
{
	if (DAE_FLAGS(dae) & DTE_CORRUPTED)
		return DTX_ST_CORRUPTED;

	if (unlikely(dae->dae_committed))
		return DTX_ST_COMMITTED;

	if (dae->dae_committing)
		return DTX_ST_COMMITTING;

	if (dae->dae_committable)
		return DTX_ST_COMMITTABLE;

	if (dae->dae_prepared)
		return DTX_ST_PREPARED;

	if (dae->dae_preparing)
		return DTX_ST_PREPARING;

	if (unlikely(dae->dae_aborted))
		return DTX_ST_ABORTED;

	if (dae->dae_aborting)
		return DTX_ST_ABORTING;

	return DTX_ST_INITED;
}

/*
 * Since no entries should be hidden from 'purge' (aggregation, discard,
 * remove) operations, ALB_UNAVAILABLE should never be returned for the
 * DAOS_INTENT_PURGE.
 */
int
vos_dtx_check_availability(daos_handle_t coh, uint32_t entry,
			   daos_epoch_t epoch, uint32_t intent, uint32_t type, bool retry)
{
	struct dtx_handle		*dth;
	struct vos_container		*cont;
	struct vos_dtx_act_ent		*dae = NULL;
	bool				 found;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	dth = vos_dth_get(cont->vc_pool->vp_sysdb);
	if (dth != NULL && dth->dth_for_migration)
		intent = DAOS_INTENT_MIGRATION;

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
		return intent == DAOS_INTENT_PURGE ? ALB_AVAILABLE_DIRTY : -DER_INVAL;
	}

	if (intent == DAOS_INTENT_CHECK)
		return dtx_is_aborted(entry) ? ALB_UNAVAILABLE : ALB_AVAILABLE_CLEAN;

	if (dtx_is_committed(entry, cont, epoch))
		return ALB_AVAILABLE_CLEAN;

	if (dtx_is_aborted(entry)) {
		if (intent == DAOS_INTENT_DISCARD)
			return ALB_AVAILABLE_CLEAN;

		if (intent == DAOS_INTENT_PURGE)
			return ALB_AVAILABLE_ABORTED;

		return ALB_UNAVAILABLE;
	}

	D_ASSERTF(epoch != 0, "Invalid epoch for DTX (lid: %x) availability check\n", entry);

	found = lrua_lookupx(cont->vc_dtx_array, (entry & DTX_LID_SOLO_MASK) - DTX_LID_RESERVED,
			     epoch, &dae);
	D_ASSERTF(found, "Non-committed DTX must be in active table: lid=%x, epoch="
		  DF_U64 ", boundary " DF_U64 "\n", entry, epoch, cont->vc_solo_dtx_epoch);

	if (intent == DAOS_INTENT_PURGE) {
		uint32_t	age = d_hlc_age2sec(DAE_XID(dae).dti_hlc);
		uint64_t	now;

		/*
		 * Don't print warning message for fresh record, since INTENT_PURGE could
		 * be used for fresh record in normal cases. (see vos_ilog_is_punched()).
		 */
		if (age <= 10)
			return ALB_AVAILABLE_DIRTY;

		/* Rate limit on such warning messages */
		now = daos_gettime_coarse();
		if (now <= cont->vc_agg_busy_ts + 10)
			return ALB_AVAILABLE_DIRTY;

		/* VOS aggregation is trying to reclaim data record being referenced by DTX */
		D_WARN("DTX "DF_DTI" (state:%u, flags:%x, age:%u, type:%u) is still inuse!\n",
		       DP_DTI(&DAE_XID(dae)), vos_dtx_status(dae), DAE_FLAGS(dae), age, type);

		cont->vc_agg_busy_ts = now;
		return ALB_AVAILABLE_DIRTY;
	}

	if (intent == DAOS_INTENT_DISCARD)
		return vos_dae_in_process(dae) ? ALB_UNAVAILABLE : ALB_AVAILABLE_CLEAN;

	/* The DTX owner can always see the DTX. */
	if (dtx_is_valid_handle(dth) && dae == dth->dth_ent)
		return ALB_AVAILABLE_CLEAN;

	if (vos_dae_is_commit(dae) || DAE_FLAGS(dae) & DTE_PARTIAL_COMMITTED) {
		if (entry & DTX_LID_SOLO_FLAG && dae->dae_committing) {
			D_DEBUG(DB_IO, "Hit in-committing solo DTX "DF_DTI", "DF_X64":"DF_X64"\n",
				DP_DTI(&DAE_XID(dae)), DAE_EPOCH(dae), cont->vc_solo_dtx_epoch);

			/* For solo 'committing' DTX, do not return ALB_AVAILABLE_CLEAN. Otherwise,
			 * it will misguide the caller as fake 'committed', but related data may
			 * be invisible to the subsequent fetch until become real 'committed'.
			 */
			return dtx_inprogress(dae, dth, false, false, 7);
		}

		return ALB_AVAILABLE_CLEAN;
	}

	if (vos_dae_is_abort(dae))
		return ALB_UNAVAILABLE;

	/* Access corrupted DTX entry. */
	if (DAE_FLAGS(dae) & DTE_CORRUPTED) {
		/* Allow update/punch against corrupted SV or EV with new
		 * epoch. But if the obj/key is corrupted, or it is fetch
		 * operation, then return -DER_DATA_LOSS.
		 */
		if ((intent == DAOS_INTENT_UPDATE ||
		     intent == DAOS_INTENT_PUNCH) &&
		    (type == DTX_RT_SVT || type == DTX_RT_EVT))
			return ALB_AVAILABLE_CLEAN;

		/* Corrupted DTX is invisible to data migration. */
		if (intent == DAOS_INTENT_MIGRATION)
			return ALB_UNAVAILABLE;

		return -DER_DATA_LOSS;
	}

	/* Access orphan DTX entry. */
	if (DAE_FLAGS(dae) & DTE_ORPHAN) {
		/* Allow update/punch against orphan SV or EV with new
		 * epoch. But if the obj/key is orphan, or it is fetch
		 * operation, then return -DER_TX_UNCERTAIN.
		 */
		if ((intent == DAOS_INTENT_UPDATE ||
		     intent == DAOS_INTENT_PUNCH) &&
		    (type == DTX_RT_SVT || type == DTX_RT_EVT))
			return ALB_AVAILABLE_CLEAN;

		/* Orphan DTX is invisible to data migration. */
		if (intent == DAOS_INTENT_MIGRATION)
			return ALB_UNAVAILABLE;

		return -DER_TX_UNCERTAIN;
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_COMMIT) ||
	    DAOS_FAIL_CHECK(DAOS_DTX_MISS_ABORT))
		return ALB_UNAVAILABLE;

	if (dth != NULL && !(DAE_FLAGS(dae) & DTE_LEADER)) {
		struct dtx_share_peer	*dsp;

		d_list_for_each_entry(dsp, &dth->dth_share_cmt_list, dsp_link) {
			if (memcmp(&dsp->dsp_xid, &DAE_XID(dae),
				   sizeof(struct dtx_id)) == 0)
				return ALB_AVAILABLE_CLEAN;
		}

		d_list_for_each_entry(dsp, &dth->dth_share_abt_list, dsp_link) {
			if (memcmp(&dsp->dsp_xid, &DAE_XID(dae),
				   sizeof(struct dtx_id)) == 0)
				return ALB_UNAVAILABLE;
		}

		d_list_for_each_entry(dsp, &dth->dth_share_act_list, dsp_link) {
			if (memcmp(&dsp->dsp_xid, &DAE_XID(dae),
				   sizeof(struct dtx_id)) == 0) {
				if (dsp->dsp_status == -DER_INPROGRESS)
					return dtx_inprogress(dae, dth, true, true, 9);

				if (unlikely(dsp->dsp_status != 0))
					return dsp->dsp_status;

				if (!dtx_is_valid_handle(dth) ||
				    intent == DAOS_INTENT_IGNORE_NONCOMMITTED)
					return ALB_UNAVAILABLE;

				return dtx_inprogress(dae, dth, true, true, 4);
			}
		}
	}

	/* The following are for non-committable cases.
	 *
	 * Current CoS mechanism only works for single RDG based replicated
	 * object modification. For other cases, such as transaction across
	 * multiple RDGs, or modifying EC object, we may have to query with
	 * related leader.
	 */

	if (intent == DAOS_INTENT_IGNORE_NONCOMMITTED) {
		/* For transactional read, has to wait the non-committed
		 * modification to guarantee the transaction semantics.
		 */
		if (dtx_is_valid_handle(dth))
			return dtx_inprogress(dae, dth, false, true, 5);

		return ALB_UNAVAILABLE;
	}

	/*
	 * Up layer rebuild logic guarantees that the rebuild scan will not be
	 * triggered until DTX resync has been done on all related targets. So
	 * here, if rebuild logic hits non-committed DTX entry, it must be for
	 * new IO that version is not older than rebuild, then it is invisible
	 * to rebuild. Related new IO corresponding to such non-committed DTX
	 * has already been sent to the in-rebuilding target.
	 */
	if (intent == DAOS_INTENT_MIGRATION)
		return ALB_UNAVAILABLE;

	if (intent == DAOS_INTENT_DEFAULT) {
		if (DAOS_FAIL_CHECK(DAOS_VOS_NON_LEADER))
			return dtx_inprogress(dae, dth, false, true, 6);

		/*
		 * For transactional read, has to wait the non-committed
		 * modification to guarantee the transaction semantics.
		 */
		if (dtx_is_valid_handle(dth))
			return dtx_inprogress(dae, dth, false, true, 2);

		/* Ignore non-prepared DTX in spite of on leader or not. */
		if (dae->dae_dbd == NULL || dae->dae_dth != NULL || dae->dae_preparing)
			return ALB_UNAVAILABLE;

		if (!(DAE_FLAGS(dae) & DTE_LEADER))
			/*
			 * Read on non-leader, return -DER_INPROGRESS, then the caller
			 * will refresh the DTX or retry related RPC with the leader.
			 */
			return dtx_inprogress(dae, dth, false, true, 1);

		/* For stand-alone read on leader, ignore non-committed DTX. */
		return ALB_UNAVAILABLE;
	}

	D_ASSERTF(intent == DAOS_INTENT_UPDATE || intent == DAOS_INTENT_PUNCH,
		  "Unexpected intent (1) %u\n", intent);

	if (type == DTX_RT_ILOG) {
		if (!dtx_is_valid_handle(dth))
			/* XXX: For rebuild case, if some normal IO has
			 *	generated related record by race before
			 *	rebuild logic handling it. Then rebuild
			 *	logic should ignore such record because
			 *	its epoch is higher than rebuild epoch.
			 *	The rebuild logic needs to create the
			 *	original target record that exists on
			 *	other healthy replicas before punch.
			 */
			return ALB_UNAVAILABLE;

		return dtx_inprogress(dae, dth, false, retry, 3);
	}

	D_ASSERTF(intent == DAOS_INTENT_UPDATE,
		  "Unexpected intent (2) %u\n", intent);

	/*
	 * We do not share modification between two DTXs for SV/EV value.
	 * Because the value is epoch based, different DTXs use different
	 * epochs, then make them to be visible to each other.
	 */
	return ALB_AVAILABLE_CLEAN;
}

uint32_t
vos_dtx_get(bool standalone)
{
	struct dtx_handle	*dth = vos_dth_get(standalone);

	if (!dtx_is_valid_handle(dth))
		return DTX_LID_COMMITTED;

	if (unlikely(dth->dth_aborted))
		return DTX_LID_ABORTED;

	if (dth->dth_ent == NULL)
		return DTX_LID_COMMITTED;

	return DAE_LID((struct vos_dtx_act_ent *)dth->dth_ent);
}

int
vos_dtx_validation(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	*dae;
	struct vos_container	*cont;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc = 0;

	D_ASSERT(dtx_is_valid_handle(dth));

	dae = dth->dth_ent;

	/* During current ULT waiting for some event, such as bulk data
	 * transfer, some other has aborted the pre-allocated DTX entry.
	 * Under such case, return DTX_ST_ABORTED for retry by client.
	 *
	 * It is also possible that the DTX has been committed by other
	 * (for resend), then return DTX_ST_COMMITTED.
	 *
	 * More cases, the DTX entry has been is aborted by other during
	 * current ULT yield, and then the DTX was recreated on the same
	 * (or different) DTX LRU array slot.
	 */

	if (unlikely(dth->dth_aborted)) {
		D_ASSERT(dae == NULL);
		cont = vos_hdl2cont(dth->dth_coh);
		D_ASSERT(cont != NULL);

		d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
		d_iov_set(&riov, NULL, 0);

		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
				if (rc == 0)
					D_GOTO(out, rc = DTX_ST_COMMITTED);
			}

			/* Failed to lookup DTX entry, in spite of whether it is DER_NONEXIST
			 * or not, then handle it as aborted that will cause client to retry.
			 */
			D_GOTO(out, rc = DTX_ST_ABORTED);
		}

		dae = riov.iov_buf;
	} else if (unlikely(dae == NULL)) {
		D_GOTO(out, rc = DTX_ST_COMMITTED);
	}

	rc = vos_dtx_status(dae);

out:
	dth->dth_need_validation = 0;
	return rc;
}

/* The caller has started local transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, uint32_t *tx_id)
{
	struct dtx_handle	*dth = vos_dth_get(umm->umm_pool->up_store.store_standalone);
	struct vos_dtx_act_ent	*dae;
	int			 rc = 0;

	if (!dtx_is_real_handle(dth)) {
		dtx_set_committed(tx_id);
		return 0;
	}

	/*
	 * Check whether someone touched the DTX before we registering modification
	 * for the first time (during the prepare, such as bulk data transferring).
	 */
	if (unlikely(dth->dth_need_validation && !dth->dth_active)) {
		rc = vos_dtx_validation(dth);
		switch (rc) {
		case DTX_ST_INITED:
			if (!dth->dth_aborted)
				break;
			/* Fall through */
		case DTX_ST_PREPARED:
		case DTX_ST_PREPARING:
			/* The DTX has been ever aborted and related resent RPC
			 * is in processing. Return -DER_AGAIN to make this ULT
			 * to retry sometime later without dtx_abort().
			 */
			D_GOTO(out, rc = -DER_AGAIN);
		case DTX_ST_COMMITTED:
		case DTX_ST_COMMITTING:
		case DTX_ST_COMMITTABLE:
			/* Aborted then prepared/committed by race.
			 * Return -DER_ALREADY to avoid repeated modification.
			 */
			dth->dth_already = 1;
			D_GOTO(out, rc = -DER_ALREADY);
		case DTX_ST_ABORTED:
			D_ASSERT(dth->dth_ent == NULL);
			/* Aborted, return -DER_INPROGRESS for client retry.
			 *
			 * Fall through.
			 */
		case DTX_ST_ABORTING:
			D_GOTO(out, rc = -DER_INPROGRESS);
		default:
			D_ASSERTF(0, "Unexpected DTX "DF_DTI" status %d\n",
				  DP_DTI(&dth->dth_xid), rc);
		}
	}

	dae = dth->dth_ent;
	/* There must has been vos_dtx_attach() before vos_dtx_register_record(). */
	D_ASSERT(dae != NULL);

	/* For single participator case, we only hold DTX entry
	 * for handling resend case, not trace modified target.
	 */
	if (dth->dth_solo) {
		dth->dth_active = 1;
		*tx_id = DAE_LID(dae);
		return 0;
	}

	rc = vos_dtx_append(dth, record, type);
	if (rc == 0) {
		/* Incarnation log entry implies a share */
		dth->dth_active = 1;
		*tx_id = DAE_LID(dae);
		if (type == DTX_RT_ILOG)
			dth->dth_modify_shared = 1;
	}

out:
	DL_CDEBUG(rc == 0 || rc == -DER_ALREADY, DB_TRACE, DLOG_ERR, rc,
		  "Register DTX record for "DF_DTI": lid=%d entry %p, type %d, %s ilog entry",
		  DP_DTI(&dth->dth_xid),
		  dth->dth_ent == NULL ? 0 : DAE_LID((struct vos_dtx_act_ent *)dth->dth_ent),
		  dth->dth_ent, type, dth->dth_modify_shared ? "has" : "has not");

	return rc;
}

/* The caller has started local transaction. */
int
vos_dtx_deregister_record(struct umem_instance *umm, daos_handle_t coh,
			  uint32_t entry, daos_epoch_t epoch, umem_off_t record)
{
	struct dtx_handle		*dth = vos_dth_get(false);
	struct vos_dtx_act_ent		*dae;
	struct vos_dtx_act_ent_df	*dae_df;
	umem_off_t			*rec_df;
	bool				 found;
	int				 count;
	int				 rc;
	int				 i;

	/*
	 * If @coh is empty handle, then we are destroying the container. Under such case,
	 * both the in-DRAM and on-dish DTX entry have already been released or destroyed.
	 */
	if (daos_handle_is_inval(coh))
		return 0;

	if (!vos_dtx_is_normal_entry(entry))
		return 0;

	found = lrua_lookupx(vos_hdl2cont(coh)->vc_dtx_array,
			     (entry & DTX_LID_SOLO_MASK) - DTX_LID_RESERVED, epoch, &dae);
	D_ASSERTF(found, "Could not find active DTX record for lid=%x, epoch=" DF_U64 "\n",
		  entry, epoch);

	/*
	 * NOTE: If the record to be deregistered (for free or overwrite, and so on) is referenced
	 *	 by another prepared (but non-committed) DTX, then do not allow current transaction
	 *	 to modify it. Because if current transaction is aborted or failed for some reason,
	 *	 there is no efficient way to recover such former non-committed DTX.
	 *
	 *	 If dth is NULL, then it is for GC. Under such case, deregister the record anyway.
	 */
	if (dae->dae_dbd != NULL) {
		if (dth != NULL)
			return dtx_inprogress(dae, dth, false, false, 8);

		dae_df = umem_off2ptr(umm, dae->dae_df_off);
		D_ASSERT(!(dae_df->dae_flags & DTE_INVALID));

		if (dae_df->dae_rec_cnt > DTX_INLINE_REC_CNT)
			count = DTX_INLINE_REC_CNT;
		else
			count = dae_df->dae_rec_cnt;

		rec_df = dae_df->dae_rec_inline;
		for (i = 0; i < count; i++) {
			if (record == umem_off2offset(rec_df[i])) {
				rc = umem_tx_add_ptr(umm, &rec_df[i], sizeof(rec_df[i]));
				if (rc != 0)
					return rc;

				rec_df[i] = UMOFF_NULL;
				goto cache;
			}
		}

		rec_df = umem_off2ptr(umm, dae_df->dae_rec_off);
		if (rec_df == NULL)
			/* If non-exist on disk, then must be non-exist in cache. */
			return 0;

		for (i = 0; i < dae_df->dae_rec_cnt - DTX_INLINE_REC_CNT; i++) {
			if (record == umem_off2offset(rec_df[i])) {
				rc = umem_tx_add_ptr(umm, &rec_df[i], sizeof(rec_df[i]));
				if (rc != 0)
					return rc;

				rec_df[i] = UMOFF_NULL;
				goto cache;
			}
		}

		/* If non-exist on disk, then must be non-exist in cache. */
		return 0;
	}

cache:
	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = 0; i < count; i++) {
		if (record == umem_off2offset(DAE_REC_INLINE(dae)[i])) {
			DAE_REC_INLINE(dae)[i] = UMOFF_NULL;
			return 0;
		}
	}

	for (i = 0; i < DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT; i++) {
		if (record == umem_off2offset(dae->dae_records[i])) {
			dae->dae_records[i] = UMOFF_NULL;
			return 0;
		}
	}

	return 0;
}

int
vos_dtx_prepared(struct dtx_handle *dth, struct vos_dtx_cmt_ent **dce_p)
{
	struct vos_dtx_act_ent		*dae = dth->dth_ent;
	struct vos_container		*cont = vos_hdl2cont(dth->dth_coh);
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_cont_df		*cont_df;
	struct umem_instance		*umm;
	struct vos_dtx_blob_df		*dbd;
	umem_off_t			 rec_off;
	size_t				 size;
	int				 count;
	int				 rc = 0;

	if (!dth->dth_active)
		return 0;

	/* There must be vos_dtx_attach() before prepared. */
	D_ASSERT(dae != NULL);
	D_ASSERT(cont != NULL);
	D_ASSERT(!dae->dae_aborting);
	D_ASSERT(!dae->dae_aborted);

	if (dth->dth_solo) {
		if (dth->dth_drop_cmt)
			/* The active DTX entry will be removed via vos_dtx_post_handle()
			 * after related local TX being committed successfully.
			 */
			dae->dae_committing = 1;
		else
			rc = vos_dtx_commit_internal(cont, &dth->dth_xid, 1,
						     dth->dth_epoch, false, NULL, NULL, dce_p);
		dth->dth_active = 0;
		dth->dth_pinned = 0;
		if (rc >= 0) {
			dth->dth_sync = 1;
			rc = 0;
		}

		return rc;
	}

	D_ASSERT(dae->dae_dbd == NULL);

	cont_df = cont->vc_cont_df;
	umm = vos_cont2umm(cont);
	dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	if (dbd == NULL || dbd->dbd_index >= dbd->dbd_cap) {
		rc = vos_dtx_extend_act_table(cont);
		if (rc != 0)
			return rc;

		dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	}

	D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);

	dae->dae_dbd = dbd;
	dae->dae_df_off = umem_ptr2off(umm, dbd) +
			  offsetof(struct vos_dtx_blob_df, dbd_active_data) +
			  sizeof(struct vos_dtx_act_ent_df) * dbd->dbd_index;
	dae_df = umem_off2ptr(umm, dae->dae_df_off);

	/* Use the dkey_hash for the last modification as the dkey_hash
	 * for the whole transaction. It will used as the index for DTX
	 * committable/CoS cache.
	 */
	DAE_DKEY_HASH(dae) = dth->dth_dkey_hash;

	/* If the DTX is for punch object that is quite possible affect
	 * subsequent operations, then synchronously commit the DTX when
	 * it becomes committable to avoid availability trouble.
	 *
	 * If someone (from non-leader) tried to refresh the DTX status
	 * before its 'prepared', then let's commit it synchronously.
	 */
	if ((DAE_DKEY_HASH(dae) == 0 || dae->dae_maybe_shared) &&
	    (dth->dth_modification_cnt > 0))
		dth->dth_sync = 1;

	if (dth->dth_oid_array != NULL) {
		D_ASSERT(dth->dth_oid_cnt > 0);

		dae->dae_oid_cnt = dth->dth_oid_cnt;
		if (dth->dth_oid_cnt == 1) {
			dae->dae_oid_inline = dth->dth_oid_array[0];
			dae->dae_oids = &dae->dae_oid_inline;
		} else {
			size = sizeof(daos_unit_oid_t) * dth->dth_oid_cnt;
			D_ALLOC_NZ(dae->dae_oids, size);
			if (dae->dae_oids == NULL) {
				/* Not fatal. */
				D_WARN("No DRAM to store ACT DTX OIDs "
				       DF_DTI"\n", DP_DTI(&DAE_XID(dae)));
				dae->dae_oid_cnt = 0;
			} else {
				memcpy(dae->dae_oids, dth->dth_oid_array, size);
			}
		}
	} else {
		dae->dae_oids = &DAE_OID(dae);
		dae->dae_oid_cnt = 1;
	}

	if (DAE_MBS_DSIZE(dae) <= sizeof(DAE_MBS_INLINE(dae))) {
		memcpy(DAE_MBS_INLINE(dae), dth->dth_mbs->dm_data,
		       DAE_MBS_DSIZE(dae));
	} else {
		rec_off = umem_zalloc(umm, DAE_MBS_DSIZE(dae));
		if (UMOFF_IS_NULL(rec_off)) {
			D_ERROR("No space to store DTX mbs "
				DF_DTI"\n", DP_DTI(&DAE_XID(dae)));
			return -DER_NOSPACE;
		}

		memcpy(umem_off2ptr(umm, rec_off),
		       dth->dth_mbs->dm_data, DAE_MBS_DSIZE(dae));
		DAE_MBS_OFF(dae) = rec_off;
	}

	if (dae->dae_records != NULL) {
		count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
		D_ASSERTF(count > 0, "Invalid DTX rec count %d\n", count);

		size = sizeof(umem_off_t) * count;
		rec_off = umem_zalloc(umm, size);
		if (UMOFF_IS_NULL(rec_off)) {
			D_ERROR("No space to store active DTX "DF_DTI"\n",
				DP_DTI(&DAE_XID(dae)));
			return -DER_NOSPACE;
		}

		memcpy(umem_off2ptr(umm, rec_off), dae->dae_records, size);
		DAE_REC_OFF(dae) = rec_off;
	}

	DAE_INDEX(dae) = dbd->dbd_index;
	if (DAE_INDEX(dae) > 0) {
		rc = umem_tx_xadd_ptr(umm, dae_df, sizeof(*dae_df), UMEM_XADD_NO_SNAPSHOT);
		if (rc != 0)
			goto out;

		/* dbd_index is next to dbd_count */
		rc = umem_tx_add_ptr(umm, &dbd->dbd_count,
				     sizeof(dbd->dbd_count) + sizeof(dbd->dbd_index));
		if (rc != 0)
			goto out;
	}

	memcpy(dae_df, &dae->dae_base, sizeof(*dae_df));
	dbd->dbd_count++;
	dbd->dbd_index++;

	dae->dae_preparing = 1;
	dae->dae_need_release = 1;

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc,
		  "Preparing DTX "DF_DTI" in dbd "UMOFF_PF" at index %u, count %u, cap %u",
		  DP_DTI(&DAE_XID(dae)), UMOFF_P(cont_df->cd_dtx_active_tail),
		  dbd->dbd_index, dbd->dbd_count, dbd->dbd_cap);
	return rc;
}

static struct dtx_memberships *
vos_dtx_pack_mbs(struct umem_instance *umm, struct vos_dtx_act_ent *dae)
{
	struct dtx_handle	*dth = dae->dae_dth;
	struct dtx_memberships	*tmp;
	size_t			 size;

	size = sizeof(*tmp) + DAE_MBS_DSIZE(dae);
	D_ALLOC(tmp, size);
	if (tmp == NULL)
		return NULL;

	tmp->dm_tgt_cnt = DAE_TGT_CNT(dae);
	tmp->dm_grp_cnt = DAE_GRP_CNT(dae);
	tmp->dm_data_size = DAE_MBS_DSIZE(dae);
	tmp->dm_flags = DAE_MBS_FLAGS(dae);
	tmp->dm_dte_flags = DAE_FLAGS(dae);

	/* The DTX is not prepared yet, copy the MBS from DTX handle. */
	if (dth != NULL)
		memcpy(tmp->dm_data, dth->dth_mbs->dm_data, tmp->dm_data_size);
	else if (tmp->dm_data_size <= sizeof(DAE_MBS_INLINE(dae)))
		memcpy(tmp->dm_data, DAE_MBS_INLINE(dae), tmp->dm_data_size);
	else
		memcpy(tmp->dm_data, umem_off2ptr(umm, DAE_MBS_OFF(dae)),
		       tmp->dm_data_size);

	return tmp;
}

int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t *epoch,
	      uint32_t *pm_ver, struct dtx_cos_key *dck, bool for_refresh)
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
		dae = riov.iov_buf;

		if (DAE_FLAGS(dae) & DTE_CORRUPTED)
			return DTX_ST_CORRUPTED;

		if (DAE_FLAGS(dae) & DTE_ORPHAN)
			return -DER_TX_UNCERTAIN;

		if (dck != NULL) {
			dck->oid = DAE_OID(dae);
			dck->dkey_hash = DAE_DKEY_HASH(dae);
		}

		if (dae->dae_committed || dae->dae_committing) {
			/* For solo 'committing' DTX, do not return DTX_ST_COMMITTED. Otherwise,
			 * it will misguide the caller as fake 'committed', but related data may
			 * be invisible to the subsequent fetch until become real 'committed'.
			 */
			if (dae->dae_committing && DAE_LID(dae) & DTX_LID_SOLO_FLAG)
				return -DER_INPROGRESS;

			if (epoch != NULL)
				*epoch = DAE_EPOCH(dae);

			return DTX_ST_COMMITTED;
		}

		if (dae->dae_committable || DAE_FLAGS(dae) & DTE_PARTIAL_COMMITTED) {
			if (epoch != NULL)
				*epoch = DAE_EPOCH(dae);

			return DTX_ST_COMMITTABLE;
		}

		if (vos_dae_is_abort(dae))
			return -DER_NONEXIST;

		if (for_refresh) {
			dae->dae_maybe_shared = 1;
			/*
			 * If DTX_REFRESH happened on current DTX entry but it was not marked
			 * as leader, then there must be leader switch and the DTX resync has
			 * not completed yet. Under such case, returning "-DER_INPROGRESS" to
			 * make related DTX_REFRESH sponsor (client) to retry sometime later.
			 */
			if (!(DAE_FLAGS(dae) & DTE_LEADER))
				return -DER_INPROGRESS;
		} else {
			/* Not committable yet, related RPC handler ULT is still running. */
			if (dae->dae_dth != NULL)
				return -DER_INPROGRESS;

			if (epoch != NULL) {
				daos_epoch_t e = *epoch;

				*epoch = DAE_EPOCH(dae);
				if (e != 0) {
					if (e > DAE_EPOCH(dae))
						return -DER_MISMATCH;

					if (e < DAE_EPOCH(dae))
						return -DER_TX_RESTART;
				}
			}
		}

		if (!vos_dae_is_prepare(dae))
			return DTX_ST_INITED;

		if (pm_ver == NULL)
			return DTX_ST_PREPARED;

		if (*pm_ver <= cont->vc_dtx_resync_ver) {
			if (!for_refresh)
				*pm_ver = DAE_VER(dae);
			return DTX_ST_PREPARED;
		}

		/*
		 * Before DTX resync completed, it is not sure whether related DTX is
		 * committable or not, then have to ask DTX refresh sponsor to retry.
		 */
		return -DER_INPROGRESS;
	}

	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0) {
			struct vos_dtx_cmt_ent	*dce;

			dce = (struct vos_dtx_cmt_ent *)riov.iov_buf;
			if (dce->dce_invalid)
				return -DER_NONEXIST;

			if (epoch != NULL)
				*epoch = DCE_EPOCH(dce);

			return DTX_ST_COMMITTED;
		}
	}

	if (rc == -DER_NONEXIST && !cont->vc_cmt_dtx_indexed)
		rc = -DER_INPROGRESS;

	return rc;
}

int
vos_dtx_load_mbs(daos_handle_t coh, struct dtx_id *dti, daos_unit_oid_t *oid,
		 struct dtx_memberships **mbs)
{
	struct vos_container	*cont;
	struct dtx_memberships	*tmp;
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
		dae = riov.iov_buf;
		tmp = vos_dtx_pack_mbs(vos_cont2umm(cont), dae);
		if (tmp == NULL) {
			rc = -DER_NOMEM;
		} else {
			if (oid != NULL)
				*oid = DAE_OID(dae);
			*mbs = tmp;
		}
	} else if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0)
			rc = 1;
		else if (rc == -DER_NONEXIST && !cont->vc_cmt_dtx_indexed)
			rc = -DER_INPROGRESS;
	}

	return rc;
}

int
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id dtis[],
			int count, daos_epoch_t epoch, bool keep_act, bool rm_cos[],
			struct vos_dtx_act_ent **daes, struct vos_dtx_cmt_ent **dces)
{
	struct vos_cont_df		*cont_df = cont->vc_cont_df;
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*dbd_prev;
	umem_off_t			 dbd_off;
	uint64_t			 cmt_time = daos_wallclock_secs();
	int				 committed = 0;
	int				 rc = 0;
	int				 p = 0;
	int				 i = 0;
	int				 j;
	int				 k;
	bool				 allocated = false;

	dbd = umem_off2ptr(umm, cont_df->cd_dtx_committed_tail);
	if (dbd == NULL)
		goto new_blob;

	D_ASSERTF(dbd->dbd_magic == DTX_CMT_BLOB_MAGIC,
		  "Corrupted committed DTX blob %x\n", dbd->dbd_magic);

	D_ASSERTF(dbd->dbd_cap >= dbd->dbd_count,
		  "Invalid committed DTX blob slots %d/%d\n",
		  dbd->dbd_cap, dbd->dbd_count);

	if (dbd->dbd_cap == dbd->dbd_count)
		goto new_blob;

again:
	for (j = dbd->dbd_count; j < dbd->dbd_cap && i < count; i++) {
		rc = vos_dtx_commit_one(cont, &dtis[i], epoch, cmt_time, keep_act, &dces[i],
					daes != NULL ? &daes[i] : NULL,
					rm_cos != NULL ? &rm_cos[i] : NULL);
		if (rc == 0 && (daes == NULL || daes[i] != NULL))
			committed++;

		if (rc == -DER_ALREADY || rc == -DER_NONEXIST)
			rc = 0;

		if (rc != 0)
			goto out;

		if (dces[i] != NULL)
			j++;
	}

	if (j > dbd->dbd_count) {
		if (!allocated) {
			rc = umem_tx_xadd_ptr(umm, &dbd->dbd_committed_data[dbd->dbd_count],
					      sizeof(struct vos_dtx_cmt_ent_df) *
					      (j - dbd->dbd_count), UMEM_XADD_NO_SNAPSHOT);
			if (rc != 0)
				goto out;

			/* Only need to add range for the first partial blob. */
			rc = umem_tx_add_ptr(umm, &dbd->dbd_count, sizeof(dbd->dbd_count));
			if (rc != 0)
				goto out;
		}

		for (k = dbd->dbd_count; k < j; k++, p++) {
			while (dces[p] == NULL)
				p++;

			memcpy(&dbd->dbd_committed_data[k], &dces[p]->dce_base,
			       sizeof(struct vos_dtx_cmt_ent_df));
		}

		dbd->dbd_count = j;
	}

	if (i == count)
		goto out;

new_blob:
	dbd_prev = dbd;
	/* Need new @dbd */
	dbd_off = umem_zalloc(umm, DTX_CMT_BLOB_SIZE);
	if (UMOFF_IS_NULL(dbd_off)) {
		D_ERROR("No space to store committed DTX %d "DF_DTI"\n",
			count, DP_DTI(&dtis[i]));
		D_GOTO(out, rc = -DER_NOSPACE);
	}

	dbd = umem_off2ptr(umm, dbd_off);
	dbd->dbd_magic = DTX_CMT_BLOB_MAGIC;
	dbd->dbd_cap = (DTX_CMT_BLOB_SIZE - sizeof(struct vos_dtx_blob_df)) /
		       sizeof(struct vos_dtx_cmt_ent_df);
	dbd->dbd_prev = umem_ptr2off(umm, dbd_prev);

	if (dbd_prev == NULL) {
		D_ASSERT(UMOFF_IS_NULL(cont_df->cd_dtx_committed_head));
		D_ASSERT(UMOFF_IS_NULL(cont_df->cd_dtx_committed_tail));

		/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
				     sizeof(cont_df->cd_dtx_committed_head) +
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0)
			goto out;

		cont_df->cd_dtx_committed_head = dbd_off;
	} else {
		rc = umem_tx_add_ptr(umm, &dbd_prev->dbd_next,
				     sizeof(dbd_prev->dbd_next));
		if (rc != 0)
			goto out;

		dbd_prev->dbd_next = dbd_off;

		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0)
			goto out;
	}

	D_DEBUG(DB_IO, "Allocated DTX committed blob %p ("UMOFF_PF") for cont "DF_UUID"\n",
		dbd, UMOFF_P(dbd_off), DP_UUID(cont->vc_id));

	cont_df->cd_dtx_committed_tail = dbd_off;
	allocated = true;
	goto again;

out:
	return rc < 0 ? rc : committed;
}

void
vos_dtx_post_handle(struct vos_container *cont,
		    struct vos_dtx_act_ent **daes,
		    struct vos_dtx_cmt_ent **dces,
		    int count, bool abort, bool rollback, bool keep_act)
{
	d_iov_t		kiov;
	int		rc;
	int		i;

	D_ASSERT(daes != NULL);

	if (rollback) {
		D_ASSERT(!abort);

		for (i = 0; i < count; i++) {
			if (daes[i] != NULL)
				daes[i]->dae_committing = 0;
		}

		if (dces == NULL)
			return;

		for (i = 0; i < count; i++) {
			if (dces[i] == NULL)
				continue;

			d_iov_set(&kiov, &DCE_XID(dces[i]),
				  sizeof(DCE_XID(dces[i])));
			rc = dbtree_delete(cont->vc_dtx_committed_hdl,
					   BTR_PROBE_EQ, &kiov, NULL);
			if (rc != 0 && rc != -DER_NONEXIST) {
				D_WARN("Failed to rollback cmt DTX entry "
				       DF_DTI": "DF_RC"\n",
				       DP_DTI(&DCE_XID(dces[i])), DP_RC(rc));
				dces[i]->dce_invalid = 1;
			}
		}

		return;
	}

	if (!abort && dces != NULL) {
		struct vos_tls		*tls = vos_tls_get(false);
		int			 j = 0;

		D_ASSERT(cont->vc_pool->vp_sysdb == false);
		for (i = 0; i < count; i++) {
			if (dces[i] != NULL)
				j++;
		}

		if (j > 0) {
			cont->vc_dtx_committed_count += j;
			cont->vc_pool->vp_dtx_committed_count += j;
			d_tm_inc_gauge(tls->vtl_committed, j);
		}
	}

	for (i = 0; i < count; i++) {
		if (daes[i] == NULL)
			continue;

		/*
		 * If it is required to keep the active DTX entry, then it must be for partial
		 * commit. Let's mark it as DTE_PARTIAL_COMMITTED.
		 */
		if (!abort && keep_act) {
			DAE_FLAGS(daes[i]) |= DTE_PARTIAL_COMMITTED;

			daes[i]->dae_committing = 0;
			dtx_act_ent_cleanup(cont, daes[i], NULL, false, true);
			continue;
		}

		d_iov_set(&kiov, &DAE_XID(daes[i]), sizeof(DAE_XID(daes[i])));
		rc = dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
				   &kiov, NULL);
		if (rc == 0 || rc == -DER_NONEXIST) {
			dtx_evict_lid(cont, daes[i]);
		} else {
			/* The DTX entry has been committed or aborted, but we
			 * cannot remove it from the active table, can mark it
			 * as 'committed' or 'aborted'. That will consume some
			 * DRAM until server restart.
			 */
			D_WARN("Cannot remove DTX "DF_DTI" from active table: "
			       DF_RC"\n", DP_DTI(&DAE_XID(daes[i])), DP_RC(rc));

			D_ASSERT(daes[i]->dae_preparing == 0);

			daes[i]->dae_prepared = 0;
			if (abort) {
				D_ASSERT(daes[i]->dae_committing == 0);

				daes[i]->dae_aborted = 1;
				daes[i]->dae_aborting = 0;
				dtx_act_ent_cleanup(cont, daes[i], NULL, true, false);
			} else {
				D_ASSERT(daes[i]->dae_aborting == 0);

				daes[i]->dae_committed = 1;
				daes[i]->dae_committing = 0;
				dtx_act_ent_cleanup(cont, daes[i], NULL, false, false);
			}
			DAE_FLAGS(daes[i]) &= ~(DTE_CORRUPTED | DTE_ORPHAN | DTE_PARTIAL_COMMITTED);
		}
	}
}

static inline void
dtx_unpin(struct vos_container *cont, struct umem_pin_handle *pin_hdl)
{
	struct vos_pool	*pool = vos_cont2pool(cont);

	if (pin_hdl != NULL)
		umem_cache_unpin(vos_pool2store(pool), pin_hdl);
}

static inline int
bkts_add_rec(struct vos_pool *pool, struct vos_bkt_array *bkts, umem_off_t rec_off)
{
	uint32_t	bkt_id;
	int		rc;

	if (UMOFF_IS_NULL(rec_off))
		return 0;

	bkt_id = umem_get_mb_from_offset(vos_pool2umm(pool), rec_off);
	if (bkt_id == UMEM_DEFAULT_MBKT_ID)
		return 0;

	rc = vos_bkt_array_add(bkts, bkt_id);
	if (rc)
		DL_ERROR(rc, "Failed to add %u into bucket array.", bkt_id);

	return rc;
}

static int
bkts_add_dae(struct vos_pool *pool, struct vos_bkt_array *bkts_in, struct vos_dtx_act_ent *dae)
{
	struct vos_bkt_array	local_bkts, *bkts;
	umem_off_t		rec_off;
	int			i, count, rc = 0;

	vos_bkt_array_init(&local_bkts);
	bkts = bkts_in->vba_cnt == 0 ? bkts_in : &local_bkts;

	if (dae->dae_records != NULL) {
		D_ASSERT(DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT);

		for (i = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT - 1; i >= 0; i--) {
			rec_off = umem_off2offset(dae->dae_records[i]);
			rc = bkts_add_rec(pool, bkts, rec_off);
			if (rc)
				goto out;
		}
		count = DTX_INLINE_REC_CNT;
	} else {
		count = DAE_REC_CNT(dae);
	}

	for (i = count - 1; i >= 0; i--) {
		rec_off = umem_off2offset(DAE_REC_INLINE(dae)[i]);
		rc = bkts_add_rec(pool, bkts, rec_off);
		if (rc)
			goto out;
	}

	/* Stop adding the dae when current dae not located in the subset of @bkts_in */
	if (local_bkts.vba_cnt != 0 && !vos_bkt_array_subset(bkts_in, &local_bkts))
		rc = 1;
out:
	vos_bkt_array_fini(&local_bkts);
	return rc;
}

static int
dtx_commit_pin(struct vos_container *cont, struct dtx_id dtis[], int count, int *pinned,
	       struct umem_pin_handle **pin_hdl)
{
	struct vos_dtx_act_ent	*dae;
	struct vos_bkt_array	 bkts;
	d_iov_t			 kiov, riov;
	int			 i, rc;

	*pinned = count;
	*pin_hdl = NULL;

	if (!vos_pool_is_evictable(vos_cont2pool(cont)))
		return 0;

	vos_bkt_array_init(&bkts);

	for (i = 0; i < count; i++) {
		d_iov_set(&kiov, &dtis[i], sizeof(struct dtx_id));
		d_iov_set(&riov, NULL, 0);

		rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			continue;
		} else if (rc) {
			DL_ERROR(rc, "Failed to lookup DTX active table.");
			goto out;
		}

		dae = riov.iov_buf;
		D_ASSERT(dae->dae_preparing == 0);

		if (vos_dae_is_abort(dae) || dae->dae_committed || dae->dae_committing ||
		    dae->dae_need_release == 0)
			continue;

		rc = bkts_add_dae(vos_cont2pool(cont), &bkts, dae);
		if (rc) {
			if (rc < 0) {
				DL_ERROR(rc, "Failed to add DTX to bucket array.");
				goto out;
			}
			*pinned = i;
			break;
		}
	}

	rc = vos_bkt_array_pin(vos_cont2pool(cont), &bkts, pin_hdl);
	if (rc)
		DL_ERROR(rc, "Failed to pin buckets.");
out:
	vos_bkt_array_fini(&bkts);
	return rc;
}

int
vos_dtx_commit(daos_handle_t coh, struct dtx_id dtis[], int count, bool keep_act, bool rm_cos[])
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_dtx_cmt_ent	**dces = NULL;
	struct vos_container	 *cont;
	struct umem_pin_handle	 *pin_hdl;
	int			  tot_committed = 0, committed, pinned;
	int			  idx = 0, rc = 0;

	D_ASSERT(count > 0);

	D_ALLOC_ARRAY(daes, count);
	if (daes == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(dces, count);
	if (dces == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

pin_objects:
	rc = dtx_commit_pin(cont, &dtis[idx], count, &pinned, &pin_hdl);
	if (rc) {
		DL_ERROR(rc, "Pin objects failed before DTX commit.");
		goto out;
	}

	D_ASSERT(pinned > 0 && pinned <= count);
	count -= pinned;

	/* Commit multiple DTXs via single local transaction. */
	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		committed = vos_dtx_commit_internal(cont, &dtis[idx], pinned, 0, keep_act,
						    rm_cos != NULL ? &rm_cos[idx] : NULL,
						    &daes[idx], &dces[idx]);
		if (committed >= 0) {
			rc = umem_tx_commit(vos_cont2umm(cont));
			D_ASSERT(rc == 0);
			tot_committed += committed;
		} else {
			rc = umem_tx_abort(vos_cont2umm(cont), committed);
		}
		vos_dtx_post_handle(cont, &daes[idx], &dces[idx], pinned, false, rc != 0, keep_act);
	}

	dtx_unpin(cont, pin_hdl);

	if (count > 0) {
		idx += pinned;
		goto pin_objects;
	}
out:
	D_FREE(daes);
	D_FREE(dces);

	return rc < 0 ? rc : tot_committed;
}

static int
dtx_abort_pin(struct vos_container *cont, struct vos_dtx_act_ent *dae,
	      struct umem_pin_handle **pin_hdl)
{
	struct vos_bkt_array	bkts;
	int			rc;

	if (!vos_pool_is_evictable(vos_cont2pool(cont)))
		return 0;

	if (dae->dae_need_release == 0)
		return 0;

	vos_bkt_array_init(&bkts);
	rc = bkts_add_dae(vos_cont2pool(cont), &bkts, dae);
	if (rc) {
		D_ASSERT(rc < 0);
		DL_ERROR(rc, "Failed to add DTX to buckets.");
		goto out;
	}

	rc = vos_bkt_array_pin(vos_cont2pool(cont), &bkts, pin_hdl);
	if (rc)
		DL_ERROR(rc, "Failed to pin buckets.");
out:
	vos_bkt_array_fini(&bkts);
	return rc;

}

int
vos_dtx_abort_internal(struct vos_container *cont, struct vos_dtx_act_ent *dae, bool force)
{
	struct dtx_handle	*dth = dae->dae_dth;
	struct umem_instance	*umm;
	struct umem_pin_handle	*pin_hdl = NULL;
	int			 rc;

	rc = dtx_abort_pin(cont, dae, &pin_hdl);
	if (rc)
		goto out;

	umm = vos_cont2umm(cont);
	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		goto out;

	if (dth != NULL) {
		D_ASSERT(dth->dth_ent == dae || dth->dth_ent == NULL);
		/* Not allow dtx_abort against solo DTX. */
		D_ASSERT(!dth->dth_solo);
		/* Set dth->dth_need_validation to notify the dth owner. */
		dth->dth_need_validation = 1;
	}

	rc = dtx_rec_release(cont, dae, true, false);
	dae->dae_preparing = 0;
	if (rc == 0) {
		dae->dae_aborting = 1;
		rc = umem_tx_commit(umm);
		D_ASSERTF(rc == 0, "local TX commit failure %d\n", rc);
	} else {
		rc = umem_tx_abort(umm, rc);
	}

	if (rc == 0 && dth != NULL) {
		dae->dae_dth = NULL;
		dth->dth_aborted = 1;
		dth->dth_ent = NULL;
		dth->dth_pinned = 0;
		/*
		 * dtx_act_ent_cleanup() will be triggered via vos_dtx_post_handle()
		 * when remove the DTX entry from active DTX table.
		 */
	}

	/*
	 * NOTE: Do not reset dth_need_validation for "else" case,
	 *	 because it may be also co-set (shared) by others.
	 */

out:
	dtx_unpin(cont, pin_hdl);

	if (rc == 0 || force)
		vos_dtx_post_handle(cont, &dae, NULL, 1, true, false, false);
	else if (rc != 0)
		dae->dae_aborting = 0;

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t epoch)
{
	struct vos_container	*cont;
	struct vos_dtx_act_ent	*dae = NULL;
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);
	D_ASSERT(epoch != 0);

	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0) {
			D_ERROR("NOT allow to abort a committed DTX (1) "DF_DTI"\n", DP_DTI(dti));
			D_GOTO(out, rc = -DER_NO_PERM);
		}
	}

	if (rc != 0)
		goto out;

	dae = riov.iov_buf;
	if (vos_dae_is_commit(dae)) {
		D_ERROR("NOT allow to abort a committed DTX (2) "DF_DTI"\n", DP_DTI(dti));
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (vos_dae_is_abort(dae)) {
		/*
		 * The DTX has been aborted before, but failed to be removed from the active
		 * table at that time, then need to be removed again via vos_dtx_post_handle.
		 */
		if (dae->dae_aborted)
			vos_dtx_post_handle(cont, &dae, NULL, 1, true, false, false);

		D_GOTO(out, rc = -DER_ALREADY);
	}

	if (epoch != DAOS_EPOCH_MAX && epoch != DAE_EPOCH(dae))
		D_GOTO(out, rc = -DER_NONEXIST);

	if (unlikely(dae->dae_preparing)) {
		/*
		 * NOTE: Abort in-preparing DTX entry. It may because the non-leader is some slow,
		 *	 as to leader got timeout and then abort the DTX by race. Under such case,
		 *	 directly set dae->dae_aborting to notify the prepare sponsor that the DTX
		 *	 should be aborted after its prepare done.
		 */
		D_WARN("Trying to abort in preparing DTX "DF_DTI" by race\n", DP_DTI(dti));
		dae->dae_aborting = 1;
	} else {
		rc = vos_dtx_abort_internal(cont, dae, false);
	}

out:
	if (rc == -DER_ALREADY)
		rc = 0;
	else if (rc != -DER_NONEXIST)
		DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc, "Abort the DTX " DF_DTI, DP_DTI(dti));

	return rc;
}

static void
do_dtx_rec_discard_invalid(struct umem_instance *umm, struct vos_dtx_act_ent *dae, umem_off_t *rec,
			   int *discarded)
{
	bool valid;

	if (UMOFF_IS_NULL(*rec))
		return;

	switch (dtx_umoff_flag2type(*rec)) {
	case DTX_RT_ILOG: {
		valid = ilog_is_valid(umm, *rec, DAE_LID(dae), DAE_EPOCH(dae));
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df *svt = umem_off2ptr(umm, *rec);
		valid                   = vos_irec_is_valid(svt, DAE_LID(dae));
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc *evt = umem_off2ptr(umm, *rec);
		valid                = evt_desc_is_valid(evt, DAE_LID(dae));
		break;
	}
	default:
		/* On-disk data corruption case. */
		valid = false;
		break;
	}

	if (!valid) {
		*rec = UMOFF_NULL;
		*discarded += 1;
	}
}

static int
vos_dtx_discard_invalid_internal(struct vos_container *cont, struct vos_dtx_act_ent *dae,
				 int *discarded)
{
	struct umem_instance *umm                 = vos_cont2umm(cont);
	int                   discarded_noninline = 0;
	int                   discarded_inline    = 0;
	int                   count               = min(DAE_REC_CNT(dae), DTX_INLINE_REC_CNT);
	int                   i;

	/* go through the inlined records */
	for (i = 0; i < count; i++) {
		do_dtx_rec_discard_invalid(umm, dae, &DAE_REC_INLINE(dae)[i], &discarded_inline);
	}

	if (discarded_inline > 0) {
		/* copy the whole array to durable format */
		struct vos_dtx_act_ent_df *dae_df = umem_off2ptr(umm, dae->dae_df_off);
		size_t                     size   = sizeof(umem_off_t) * count;
		int                        rc = umem_tx_add_ptr(umm, &dae_df->dae_rec_inline, size);
		if (rc != 0) {
			return rc;
		}
		memcpy(&dae_df->dae_rec_inline, &DAE_REC_INLINE(dae), size);
	}

	/* go through the non-inlined records if present */
	if (dae->dae_records != NULL) {
		D_ASSERT(DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT);

		count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
		for (i = 0; i < count; i++) {
			do_dtx_rec_discard_invalid(umm, dae, &dae->dae_records[i],
						   &discarded_noninline);
		}

		if (discarded_noninline > 0) {
			/* copy the whole array to the durable format */
			size_t size   = sizeof(umem_off_t) * count;
			void  *rec_df = umem_off2ptr(umm, DAE_REC_OFF(dae));
			int    rc     = umem_tx_add_ptr(umm, rec_df, size);
			if (rc != 0) {
				return rc;
			}
			memcpy(rec_df, dae->dae_records, size);
		}
	}

	*discarded = discarded_inline + discarded_noninline;

	return 0;
}

int
vos_dtx_discard_invalid(daos_handle_t coh, struct dtx_id *dti, int *discarded)
{
	struct vos_container   *cont;
	struct vos_dtx_act_ent *dae = NULL;
	d_iov_t                 riov;
	d_iov_t                 kiov;
	int                     rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	D_ASSERT(dti != NULL);
	D_ASSERT(discarded != NULL);

	/* lookup the DTX entry */
	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc != 0) {
		return rc;
	}
	dae = riov.iov_buf;

	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		rc = vos_dtx_discard_invalid_internal(cont, dae, discarded);
		if (rc == 0 && *discarded > 0) {
			rc = umem_tx_commit(vos_cont2umm(cont));
		} else {
			rc = umem_tx_abort(vos_cont2umm(cont), rc);
			if (rc == -DER_CANCELED) {
				rc = 0;
			}
		}
	}

	return rc;
}

static int
vos_dtx_set_flags_one(struct vos_container *cont, struct dtx_id *dti, uint32_t flags)
{
	struct umem_instance		*umm;
	struct vos_dtx_act_ent		*dae;
	struct vos_dtx_act_ent_df	*dae_df;
	d_iov_t				 riov;
	d_iov_t				 kiov;
	int				 rc;

	d_iov_set(&kiov, dti, sizeof(*dti));
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0) {
			D_ERROR("Not allow to set flag %s on committed (1) DTX entry "DF_DTI"\n",
				vos_dtx_flags2name(flags), DP_DTI(dti));
			D_GOTO(out, rc = -DER_NO_PERM);
		}
	}

	if (rc != 0)
		goto out;

	dae = (struct vos_dtx_act_ent *)riov.iov_buf;
	if (DAE_FLAGS(dae) & flags)
		goto out;

	if ((dae->dae_committable && (flags & (DTE_CORRUPTED | DTE_ORPHAN))) ||
	    dae->dae_committing || dae->dae_committed || vos_dae_is_abort(dae)) {
		D_ERROR("Not allow to set flag %s on the %s DTX entry "DF_DTI"\n",
			vos_dtx_flags2name(flags),
			vos_dae_is_abort(dae) ? "abort" : "commit", DP_DTI(dti));
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	umm = vos_cont2umm(cont);
	dae_df = umem_off2ptr(umm, dae->dae_df_off);
	D_ASSERT(dae_df != NULL);

	rc = umem_tx_add_ptr(umm, &dae_df->dae_flags, sizeof(dae_df->dae_flags));
	if (rc == 0) {
		dae_df->dae_flags |= flags;
		DAE_FLAGS(dae) |= flags;
	}

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DLOG_WARN, rc, "Mark the DTX entry " DF_DTI " as %s",
		  DP_DTI(dti), vos_dtx_flags2name(flags));

	return rc;
}

int
vos_dtx_set_flags(daos_handle_t coh, struct dtx_id dtis[], int count, uint32_t flags)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 rc = 0;
	int			 i;

	if (unlikely(count == 0))
		goto out;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Only allow set single flags. */
	if (flags != DTE_CORRUPTED && flags != DTE_ORPHAN) {
		D_ERROR("Try to set unrecognized flags %x on DTX "DF_DTI", count %u\n",
			flags, DP_DTI(&dtis[0]), count);
		D_GOTO(out, rc = -DER_INVAL);
	}

	umm = vos_cont2umm(cont);
	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		goto out;

	for (i = 0; i < count; i++) {
		rc = vos_dtx_set_flags_one(cont, &dtis[i], flags);
		if (rc != 0)
			break;
	}

	rc = umem_tx_end(umm, rc);

out:
	return rc;
}

int
vos_dtx_aggregate(daos_handle_t coh)
{
	struct vos_tls			*tls = vos_tls_get(false);
	struct vos_container		*cont;
	struct vos_cont_df		*cont_df;
	struct umem_instance		*umm;
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_blob_df		*tmp;
	uint64_t			 epoch;
	umem_off_t			 dbd_off;
	umem_off_t			 next = UMOFF_NULL;
	int				 count = 0;
	int				 rc;
	int				 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	cont_df = cont->vc_cont_df;
	dbd_off = cont_df->cd_dtx_committed_head;
	umm = vos_cont2umm(cont);
	epoch = cont_df->cd_newest_aggregated;

	dbd = umem_off2ptr(umm, dbd_off);
	if (dbd == NULL || dbd->dbd_count == 0)
		return 0;

	D_ASSERT(cont->vc_pool->vp_sysdb == false);
	/* Take the opportunity to free some memory if we can */
	lrua_array_aggregate(cont->vc_dtx_array);

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0) {
		D_ERROR("Failed to TX begin for DTX aggregation "UMOFF_PF": "
			DF_RC"\n", UMOFF_P(dbd_off), DP_RC(rc));
		return rc;
	}

	for (i = 0; i < dbd->dbd_count; i++) {
		struct vos_dtx_cmt_ent_df	*dce_df;
		d_iov_t				 kiov;

		dce_df = &dbd->dbd_committed_data[i];
		if (epoch < dce_df->dce_epoch)
			epoch = dce_df->dce_epoch;
		d_iov_set(&kiov, &dce_df->dce_xid, sizeof(dce_df->dce_xid));
		rc = dbtree_delete(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
				   &kiov, NULL);
		if (rc != 0 && rc != -DER_NONEXIST) {
			D_ERROR("Failed to remove entry for DTX aggregation "
				UMOFF_PF": "DF_RC"\n",
				UMOFF_P(dbd_off), DP_RC(rc));
			goto out;
		}
	}

	count = dbd->dbd_count;

	if (epoch != cont_df->cd_newest_aggregated) {
		rc = umem_tx_add_ptr(umm, &cont_df->cd_newest_aggregated,
				     sizeof(cont_df->cd_newest_aggregated));
		if (rc != 0) {
			D_ERROR("Failed to refresh epoch for DTX aggregation "UMOFF_PF": "DF_RC"\n",
				UMOFF_P(dbd_off), DP_RC(rc));
			goto out;
		}

		cont_df->cd_newest_aggregated = epoch;
	}

	next = dbd->dbd_next;
	tmp = umem_off2ptr(umm, next);
	if (tmp == NULL) {
		/* The last blob for committed DTX blob. */
		D_ASSERT(cont_df->cd_dtx_committed_tail ==
			 cont_df->cd_dtx_committed_head);

		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0) {
			D_ERROR("Failed to update tail for DTX aggregation "
				UMOFF_PF": "DF_RC"\n",
				UMOFF_P(dbd_off), DP_RC(rc));
			goto out;
		}

		cont_df->cd_dtx_committed_tail = UMOFF_NULL;
	} else {
		rc = umem_tx_add_ptr(umm, &tmp->dbd_prev,
				     sizeof(tmp->dbd_prev));
		if (rc != 0) {
			D_ERROR("Failed to update prev for DTX aggregation "
				UMOFF_PF": "DF_RC"\n",
				UMOFF_P(dbd_off), DP_RC(rc));
			goto out;
		}

		tmp->dbd_prev = UMOFF_NULL;
	}

	rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			     sizeof(cont_df->cd_dtx_committed_head));
	if (rc != 0) {
		D_ERROR("Failed to update head for DTX aggregation "
			UMOFF_PF": "DF_RC"\n",
			UMOFF_P(dbd_off), DP_RC(rc));
		goto out;
	}

	cont_df->cd_dtx_committed_head = next;

	rc = umem_free(umm, dbd_off);

out:
	rc = umem_tx_end(umm, rc);
	if (rc == 0) {
		if (cont->vc_cmt_dtx_reindex_pos == dbd_off)
			cont->vc_cmt_dtx_reindex_pos = next;

		cont->vc_dtx_committed_count -= count;
		cont->vc_pool->vp_dtx_committed_count -= count;
		d_tm_dec_gauge(tls->vtl_committed, count);
	}

	DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc,
		  "Release DTX committed blob %p (" UMOFF_PF ") for cont " DF_UUID, dbd,
		  UMOFF_P(dbd_off), DP_UUID(cont->vc_id));

	return rc;
}

void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat, uint32_t flags)
{
	struct vos_container	*cont;
	struct vos_cont_df	*cont_df;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (d_list_empty(&cont->vc_dtx_act_list)) {
		stat->dtx_oldest_active_time = 0;
	} else {
		struct vos_dtx_act_ent	*dae;

		dae = d_list_entry(cont->vc_dtx_act_list.next,
				   struct vos_dtx_act_ent, dae_link);
		if (flags & DSF_SKIP_BAD) {
			while (DAE_FLAGS(dae) & (DTE_CORRUPTED | DTE_ORPHAN)) {
				if (dae->dae_link.next ==
				    &cont->vc_dtx_act_list) {
					stat->dtx_oldest_active_time = 0;
					goto cmt;
				}

				dae = d_list_entry(dae->dae_link.next,
						   struct vos_dtx_act_ent,
						   dae_link);
			}
		}

		stat->dtx_oldest_active_time = dae->dae_start_time;
	}

cmt:
	stat->dtx_cont_cmt_count = cont->vc_dtx_committed_count;
	stat->dtx_pool_cmt_count = cont->vc_pool->vp_dtx_committed_count;

	stat->dtx_first_cmt_blob_time_up = 0;
	stat->dtx_first_cmt_blob_time_lo = 0;
	cont_df = cont->vc_cont_df;
	stat->dtx_newest_aggregated = cont_df->cd_newest_aggregated;

	if (!UMOFF_IS_NULL(cont_df->cd_dtx_committed_head)) {
		struct umem_instance		*umm = vos_cont2umm(cont);
		struct vos_dtx_blob_df		*dbd;
		struct vos_dtx_cmt_ent_df	*dce;
		int				 i;

		dbd = umem_off2ptr(umm, cont_df->cd_dtx_committed_head);

		for (i = 0; i < dbd->dbd_count; i++) {
			dce = &dbd->dbd_committed_data[i];

			if (!daos_is_zero_dti(&dce->dce_xid) &&
			    dce->dce_cmt_time != 0) {
				stat->dtx_first_cmt_blob_time_up = dce->dce_cmt_time;
				break;
			}
		}

		for (i = dbd->dbd_count - 1; i > 0; i--) {
			dce = &dbd->dbd_committed_data[i];

			if (!daos_is_zero_dti(&dce->dce_xid) &&
			    dce->dce_cmt_time != 0) {
				stat->dtx_first_cmt_blob_time_lo = dce->dce_cmt_time;
				break;
			}
		}
	}
}

void
vos_set_dtx_resync_version(daos_handle_t coh, uint32_t ver)
{
	struct vos_container *cont = vos_hdl2cont(coh);

	D_ASSERT(cont != NULL);

	if (likely(cont->vc_dtx_resync_ver < ver)) {
		D_INFO("Update resync version %u => %u for container " DF_UUID "\n",
		       cont->vc_dtx_resync_ver, ver, DP_UUID(cont->vc_id));
		cont->vc_dtx_resync_ver = ver;
	}
}

void
vos_dtx_mark_committable(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	*dae = dth->dth_ent;

	D_ASSERT(dae != NULL);

	D_ASSERTF(dae->dae_prepared == 1,
		  "DTX " DF_DTI " should be prepared locally before committable\n",
		  DP_DTI(&dth->dth_xid));

	dae->dae_committable = 1;
	DAE_FLAGS(dae) &= ~(DTE_CORRUPTED | DTE_ORPHAN);
}

int
vos_dtx_mark_sync(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch)
{
	struct vos_container	*cont;
	struct vos_object	*obj;
	daos_epoch_range_t	 epr = {0, epoch};
	int	rc;

	cont = vos_hdl2cont(coh);
	rc = vos_obj_hold(cont, oid, &epr, 0, VOS_OBJ_VISIBLE,
			  DAOS_INTENT_DEFAULT, &obj, 0);
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
		umem_atomic_copy(vos_cont2umm(cont),
				       &obj->obj_df->vo_sync, &epoch,
				       sizeof(obj->obj_df->vo_sync), UMEM_COMMIT_IMMEDIATE);
	}

	vos_obj_release(obj, 0, false);
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
	struct vos_dtx_act_ent		*prev = NULL;
	/* The max epoch for all unsorted DTX entries to be re-indexed. */
	uint64_t			 max_eph = 0;
	/* The min epoch which DTX entry is after the max_eph DTX. */
	uint64_t			 min_eph = 0;
	/* The largest diff for above pairs 'max_eph - min_eph'. */
	uint64_t			 diff = 0;
	uint64_t			 start_time = daos_wallclock_secs();
	int				 rc = 0;
	int				 i;

	while (!UMOFF_IS_NULL(dbd_off)) {
		int	dbd_count = 0;

		dbd = umem_off2ptr(umm, dbd_off);
		D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);

		for (i = 0; i < dbd->dbd_index; i++) {
			struct vos_dtx_act_ent_df	*dae_df;
			struct vos_dtx_act_ent		*dae;

			dae_df = &dbd->dbd_active_data[i];
			if (dae_df->dae_flags & DTE_INVALID)
				continue;

			if (daos_is_zero_dti(&dae_df->dae_xid)) {
				D_WARN("Hit zero active DTX entry.\n");
				continue;
			}

			if (dae_df->dae_lid < DTX_LID_RESERVED) {
				D_ERROR("Corruption in DTX table found, lid=%d"
					" is invalid\n", dae_df->dae_lid);
				D_GOTO(out, rc = -DER_IO);
			}
			rc = lrua_allocx_inplace(cont->vc_dtx_array,
					 dae_df->dae_lid - DTX_LID_RESERVED,
					 dae_df->dae_epoch, &dae);
			if (rc != 0) {
				if (rc == -DER_NOMEM) {
					D_ERROR("Not enough memory for DTX "
						"table\n");
				} else {
					D_ERROR("Corruption in DTX table found,"
						" lid=%d is invalid rc="DF_RC
						"\n", dae_df->dae_lid,
						DP_RC(rc));
					rc = -DER_IO;
				}
				D_GOTO(out, rc);
			}
			D_ASSERT(dae != NULL);

			D_DEBUG(DB_TRACE, "Re-indexed lid DTX: "DF_DTI
				" lid=%d\n", DP_DTI(&DAE_XID(dae)),
				DAE_LID(dae));

			memcpy(&dae->dae_base, dae_df, sizeof(dae->dae_base));
			dae->dae_df_off = umem_ptr2off(umm, dae_df);
			dae->dae_dbd = dbd;
			dae->dae_prepared = 1;
			dae->dae_need_release = 1;
			D_INIT_LIST_HEAD(&dae->dae_link);

			if (vos_skip_old_partial_dtx && DAE_FLAGS(dae) & DTE_PARTIAL_COMMITTED)
				DAE_REC_CNT(dae) = 0;

			if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT) {
				size_t	size;
				int	count;

				count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
				size = sizeof(*dae->dae_records) * count;

				D_ALLOC_NZ(dae->dae_records, size);
				if (dae->dae_records == NULL) {
					dtx_evict_lid(cont, dae);
					D_GOTO(out, rc = -DER_NOMEM);
				}

				memcpy(dae->dae_records,
				       umem_off2ptr(umm, dae_df->dae_rec_off),
				       size);
				dae->dae_rec_cap = count;
			}

			d_iov_set(&kiov, &DAE_XID(dae), sizeof(DAE_XID(dae)));
			d_iov_set(&riov, dae, sizeof(*dae));
			rc = dbtree_upsert(cont->vc_dtx_active_hdl,
					   BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
					   &kiov, &riov, NULL);
			if (rc != 0) {
				D_FREE(dae->dae_records);
				dtx_evict_lid(cont, dae);
				goto out;
			}

			dae->dae_start_time = start_time;
			d_list_add_tail(&dae->dae_link, &cont->vc_dtx_act_list);
			if (DAE_FLAGS(dae) & DTE_EPOCH_SORTED) {
				d_list_add_tail(&dae->dae_order_link, &cont->vc_dtx_sorted_list);
			} else {
				/*
				 * The DXT entries in the active blob may be generated against
				 * different VOS AGG GAP configurations, or even upgraded from
				 * old system that did not support VOS AGG GAP logic yet. Link
				 * them into a reindex list. During the reindex scanning, we
				 * will find out the pairs with the largest epoch difference.
				 * Using such difference to estimate the local stable epoch.
				 *
				 * NOTE: The min_eph may be not the smallest one in all the DTX
				 *	 entries to be re-indexed, instead, it is after current
				 *	 known max_eph, and if max_eph is changed, min_eph will
				 *	 be reset. So there may be multiple max/min pairs. Each
				 *	 pairs has own epoch difference (max_eph - min_eph). We
				 *	 use the largest diff.
				 *
				 * This is an O(N) algorithm. N is the count of DTX entries to be
				 * re-indexed. Please reference vos_cont_get_local_stable_epoch().
				 */
				if (prev == NULL || DAE_EPOCH(dae) > DAE_EPOCH(prev)) {
					if (max_eph < DAE_EPOCH(dae)) {
						max_eph = DAE_EPOCH(dae);
						min_eph = 0;
					}
				} else {
					if (min_eph == 0 || min_eph > DAE_EPOCH(dae)) {
						min_eph = DAE_EPOCH(dae);
						if (diff < max_eph - min_eph)
							diff = max_eph - min_eph;
					}
				}

				d_list_add_tail(&dae->dae_order_link, &cont->vc_dtx_reindex_list);
			}
			prev = dae;
			dbd_count++;
		}

		if (unlikely(dbd_count != dbd->dbd_count)) {
			D_ERROR("Unmatched active DTX count %d/%d, cap %d, idx %d for blob %p ("
				UMOFF_PF"), head "UMOFF_PF", tail "UMOFF_PF" in pool "
				DF_UUID" cont "DF_UUID"\n", dbd_count, dbd->dbd_count, dbd->dbd_cap,
				dbd->dbd_index, dbd, UMOFF_P(dbd_off),
				UMOFF_P(cont_df->cd_dtx_active_head),
				UMOFF_P(cont_df->cd_dtx_active_tail), DP_UUID(cont->vc_pool->vp_id),
				DP_UUID(cont->vc_id));
			D_GOTO(out, rc = -DER_IO);
		}

		dbd_off = dbd->dbd_next;
	}

	cont->vc_dtx_reindex_eph_diff = diff;

out:
	return rc > 0 ? 0 : rc;
}

int
vos_dtx_cmt_reindex(daos_handle_t coh)
{
	struct umem_instance		*umm;
	struct vos_container		*cont;
	struct vos_dtx_cmt_ent		*dce;
	struct vos_dtx_blob_df		*dbd;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;
	int				 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (cont->vc_cmt_dtx_indexed)
		return 1;

	umm = vos_cont2umm(cont);
	dbd = umem_off2ptr(umm, cont->vc_cmt_dtx_reindex_pos);
	if (dbd == NULL)
		D_GOTO(out, rc = 1);

	D_ASSERTF(dbd->dbd_magic == DTX_CMT_BLOB_MAGIC,
		  "Corrupted committed DTX blob (2) %x\n", dbd->dbd_magic);

	for (i = 0; i < dbd->dbd_count; i++) {
		struct vos_dtx_cmt_ent_df *dce_df = &dbd->dbd_committed_data[i];

		if (daos_is_zero_dti(&dce_df->dce_xid) || dce_df->dce_epoch == 0) {
			D_WARN("Skip invalid committed DTX entry\n");
			continue;
		}

		D_ALLOC_PTR(dce);
		if (dce == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(&dce->dce_base, dce_df, sizeof(dce->dce_base));
		dce->dce_reindex = 1;

		d_iov_set(&kiov, &DCE_XID(dce), sizeof(DCE_XID(dce)));
		d_iov_set(&riov, dce, sizeof(*dce));
		rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
				   DAOS_INTENT_UPDATE, &kiov, &riov, NULL);
		if (rc != 0) {
			D_FREE(dce);
			goto out;
		}

		/* The committed DTX entry is already in the index.
		 * Related re-index logic can stop.
		 */
		if (dce->dce_exist) {
			D_FREE(dce);
			D_GOTO(out, rc = 1);
		}
	}

	if (dbd->dbd_count < dbd->dbd_cap || UMOFF_IS_NULL(dbd->dbd_next))
		D_GOTO(out, rc = 1);

	cont->vc_cmt_dtx_reindex_pos = dbd->dbd_next;

out:
	if (rc > 0) {
		cont->vc_cmt_dtx_reindex_pos = UMOFF_NULL;
		cont->vc_cmt_dtx_indexed = 1;
	}

	return rc;
}

void
vos_dtx_cleanup_internal(struct dtx_handle *dth)
{
	struct vos_container	*cont;
	struct vos_dtx_act_ent	*dae = NULL;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	if (!dtx_is_valid_handle(dth) || (!dth->dth_active && dth->dth_ent == NULL) ||
	    dth->dth_local)
		return;

	dth->dth_active = 0;
	if (unlikely(dth->dth_already))
		return;

	cont = vos_hdl2cont(dth->dth_coh);
	dae = dth->dth_ent;

	if (dth->dth_pinned) {
		/* Only keep the DTX entry (header) for handling resend RPC,
		 * remove DTX records, purge related VOS objects from cache.
		 */
		if (dae != NULL) {
			D_ASSERT(!vos_dae_is_prepare(dae));
			dtx_act_ent_cleanup(cont, dae, dth, true, false);
		}
	} else {
		d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
		d_iov_set(&riov, NULL, 0);

		if (dae == NULL) {
			rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
			/* Need not search committed table, since cannot cleanup 'committed' one. */
			if (rc != 0)
				return;

			dae = (struct vos_dtx_act_ent *)riov.iov_buf;
		}

		/* Cannot cleanup 'prepare'/'commit' DTX entry. */
		if (vos_dae_is_prepare(dae) || vos_dae_is_commit(dae))
			goto out;

		/* Skip the @dae if it belong to another instance for resent request. */
		if (DAE_EPOCH(dae) != dth->dth_epoch)
			goto out;

		dtx_act_ent_cleanup(cont, dae, dth, true, false);

		rc = dbtree_delete(cont->vc_dtx_active_hdl,
				   riov.iov_buf != NULL ? BTR_PROBE_BYPASS : BTR_PROBE_EQ,
				   &kiov, &dae);
		if (rc == 0 || rc == -DER_NONEXIST)
			dtx_evict_lid(cont, dae);
		else
			dae->dae_aborted = 1;

out:
		dth->dth_ent = NULL;
	}
}

void
vos_dtx_cleanup(struct dtx_handle *dth, bool unpin)
{
	struct vos_dtx_act_ent	*dae;
	struct vos_container	*cont;

	if (!dtx_is_valid_handle(dth) || unlikely(dth->dth_already))
		return;

	D_ASSERT(!dth->dth_local);

	dae = dth->dth_ent;
	if (dae == NULL) {
		if (!dth->dth_active && !unpin)
			return;
	} else {
		/* 'prepared'/'preparing' DTX can be either committed or aborted, not cleanup. */
		if (vos_dae_is_prepare(dae) || vos_dae_is_commit(dae))
			return;
	}

	if (unpin)
		dth->dth_pinned = 0;

	cont = vos_hdl2cont(dth->dth_coh);
	/* This will abort the transaction and callback to vos_dtx_cleanup_internal(). */
	vos_tx_end(cont, dth, NULL, NULL, true /* don't care */, NULL, -DER_CANCELED);
}

int
vos_dtx_attach(struct dtx_handle *dth, bool persistent, bool exist)
{
	struct vos_container	*cont;
	struct umem_instance	*umm = NULL;
	struct vos_dtx_cmt_ent	*dce = NULL;
	struct vos_dtx_act_ent	*dae;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc = 0;
	bool			 tx = false;

	if (!dtx_is_valid_handle(dth))
		return 0;

	cont = vos_hdl2cont(dth->dth_coh);
	umm = vos_cont2umm(cont);

	if (dth->dth_ent != NULL) {
		D_ASSERT(persistent);
		D_ASSERT(dth->dth_active == 0);
	} else {
		D_ASSERT(dth->dth_pinned == 0);

		if (exist) {
			d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
			d_iov_set(&riov, NULL, 0);
			rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
			if (rc != 0)
				goto out;

			dae = riov.iov_buf;
			if (dae->dae_dth == NULL)
				dae->dae_dth = dth;
			else
				D_ASSERT(dae->dae_dth == dth);

			dth->dth_ent = dae;
			dth->dth_need_validation = dae->dae_need_validation;
		}
	}

	/*
	 * Doesn't need to pin the object before starting tx, since the DTX commit from
	 * following vos_dtx_prepared() is for read-only DTX transaction, no object data
	 * will be accessed during DTX commit.
	 */
	if (persistent) {
		rc = umem_tx_begin(umm, NULL);
		if (rc != 0)
			goto out;

		tx = true;
	}

	if (dth->dth_ent == NULL)
		rc = vos_dtx_alloc(umm, dth);

out:
	if (rc == 0) {
		if (persistent) {
			dth->dth_active = 1;
			rc = vos_dtx_prepared(dth, &dce);
			if (!dth->dth_solo)
				D_ASSERT(dce == NULL);
		} else {
			dth->dth_pinned = 1;
		}
	}

	if (persistent) {
		if (tx) {
			if (rc == 0) {
				rc = umem_tx_commit(umm);
				D_ASSERTF(rc == 0, "local TX commit failure %d\n", rc);
			} else {
				rc = umem_tx_abort(umm, rc);
			}
		}

		dae = dth->dth_ent;
		if (dae != NULL) {
			if (unlikely(dae->dae_preparing && dae->dae_aborting)) {
				dae->dae_preparing = 0;
				rc = vos_dtx_abort_internal(cont, dae, true);
				DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc,
					  "Delay abort DTX " DF_DTI " (2)", DP_DTI(&dth->dth_xid));

				/* Aborted by race, return -DER_INPROGRESS for client retry. */
				return -DER_INPROGRESS;
			}

			dae->dae_preparing = 0;
			if (dth->dth_solo)
				vos_dtx_post_handle(cont, &dae, &dce, 1, false, rc != 0, false);
			else if (rc == 0)
				dae->dae_prepared = 1;
		}
	}

	if (rc != 0) {
		if (dth->dth_ent != NULL) {
			dth->dth_pinned = 0;
			vos_dtx_cleanup_internal(dth);
		}

		if (rc != 0)
			DL_CDEBUG(rc != -DER_TX_RESTART, DLOG_ERR, DB_TRACE, rc,
				  "Failed to pin DTX entry for "DF_DTI": "DF_RC,
				  DP_DTI(&dth->dth_xid), DP_RC(rc));
	}

	return rc;
}

void
vos_dtx_detach(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	*dae = dth->dth_ent;

	D_ASSERTF(dth->dth_local_tx_started == 0,
		  "DTX " DF_DTI " should end locally before detach\n", DP_DTI(&dth->dth_xid));

	if (dae != NULL) {
		D_ASSERT(dae->dae_dth == dth);

		dae->dae_dth = NULL;
		dth->dth_ent = NULL;
	}
	dth->dth_pinned = 0;
}

/** Allocate space for saving the vos reservations and deferred actions */
int
vos_dtx_rsrvd_init(struct dtx_handle *dth)
{
	dth->dth_rsrvd_cnt = 0;
	dth->dth_deferred_cnt = 0;
	dth->dth_deferred_used_cnt = 0;
	D_INIT_LIST_HEAD(&dth->dth_deferred_nvme);

	if (dth->dth_modification_cnt <= 1) {
		dth->dth_rsrvds = &dth->dth_rsrvd_inline;
		return 0;
	}

	D_ALLOC_ARRAY(dth->dth_rsrvds, dth->dth_modification_cnt);
	if (dth->dth_rsrvds == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(dth->dth_deferred, dth->dth_modification_cnt);
	if (dth->dth_deferred == NULL) {
		D_FREE(dth->dth_rsrvds);
		return -DER_NOMEM;
	}

	return 0;
}

void
vos_dtx_rsrvd_fini(struct dtx_handle *dth)
{
	if (dth->dth_rsrvds != NULL) {
		D_ASSERT(d_list_empty(&dth->dth_deferred_nvme));
		D_FREE(dth->dth_deferred);
		if (dth->dth_rsrvds != &dth->dth_rsrvd_inline)
			D_FREE(dth->dth_rsrvds);
	}
}

static const struct lru_callbacks lru_dtx_cache_cbs = {
	.lru_on_alloc = vos_lru_alloc_track,
	.lru_on_free = vos_lru_free_track,
};

int
vos_dtx_cache_reset(daos_handle_t coh, bool force)
{
	struct vos_container	*cont;
	struct umem_attr	 uma;
	int			 rc = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;

	if (!force) {
		if (cont->vc_dtx_array)
			lrua_array_aggregate(cont->vc_dtx_array);
		goto cmt;
	}

	if (daos_handle_is_valid(cont->vc_dtx_active_hdl)) {
		rc = dbtree_destroy(cont->vc_dtx_active_hdl, NULL);
		if (rc != 0) {
			D_ERROR("Failed to destroy active DTX tree for "DF_UUID": "DF_RC"\n",
				DP_UUID(cont->vc_id), DP_RC(rc));
			return rc;
		}

		cont->vc_dtx_active_hdl = DAOS_HDL_INVAL;
	}

	if (cont->vc_dtx_array)
		lrua_array_free(cont->vc_dtx_array);

	rc = lrua_array_alloc(&cont->vc_dtx_array, DTX_ARRAY_LEN, DTX_ARRAY_NR,
			      sizeof(struct vos_dtx_act_ent), LRU_FLAG_REUSE_UNIQUE,
			      &lru_dtx_cache_cbs, vos_tls_get(false));
	if (rc != 0) {
		D_ERROR("Failed to re-create DTX active array for "DF_UUID": "DF_RC"\n",
			DP_UUID(cont->vc_id), DP_RC(rc));
		return rc;
	}

	rc = dbtree_create_inplace_ex(VOS_BTR_DTX_ACT_TABLE, 0, DTX_BTREE_ORDER, &uma,
				      &cont->vc_dtx_active_btr, DAOS_HDL_INVAL, cont,
				      &cont->vc_dtx_active_hdl);
	if (rc != 0) {
		D_ERROR("Failed to re-create DTX active tree for "DF_UUID": "DF_RC"\n",
			DP_UUID(cont->vc_id), DP_RC(rc));
		return rc;
	}

	rc = vos_dtx_act_reindex(cont);
	if (rc != 0) {
		D_ERROR("Fail to reindex active DTX table for "DF_UUID": "DF_RC"\n",
			DP_UUID(cont->vc_id), DP_RC(rc));
		return rc;
	}

cmt:
	if (daos_handle_is_valid(cont->vc_dtx_committed_hdl)) {
		rc = dbtree_destroy(cont->vc_dtx_committed_hdl, NULL);
		if (rc != 0) {
			D_ERROR("Failed to destroy committed DTX tree for "DF_UUID": "DF_RC"\n",
				DP_UUID(cont->vc_id), DP_RC(rc));
			return rc;
		}

		cont->vc_pool->vp_dtx_committed_count -= cont->vc_dtx_committed_count;
		D_ASSERT(cont->vc_pool->vp_sysdb == false);
		d_tm_dec_gauge(vos_tls_get(false)->vtl_committed, cont->vc_dtx_committed_count);

		cont->vc_dtx_committed_hdl = DAOS_HDL_INVAL;
		cont->vc_dtx_committed_count = 0;
		cont->vc_cmt_dtx_indexed = 0;
		cont->vc_cmt_dtx_reindex_pos = cont->vc_cont_df->cd_dtx_committed_head;
	}

	rc = dbtree_create_inplace_ex(VOS_BTR_DTX_CMT_TABLE, 0, DTX_BTREE_ORDER, &uma,
				      &cont->vc_dtx_committed_btr, DAOS_HDL_INVAL, cont,
				      &cont->vc_dtx_committed_hdl);
	if (rc != 0) {
		D_ERROR("Failed to re-create DTX committed tree for "DF_UUID": "DF_RC"\n",
			DP_UUID(cont->vc_id), DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_TRACE, "Reset DTX cache for "DF_UUID"\n", DP_UUID(cont->vc_id));

	return 0;
}

void
vos_dtx_renew_epoch(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	*dae = dth->dth_ent;

	if (dae != NULL)
		DAE_EPOCH(dae) = dth->dth_epoch;

	if (dth->dth_local_stub != NULL)
		lrua_refresh_key(dth->dth_local_stub, dth->dth_epoch);
}

int
vos_dtx_local_begin(struct dtx_handle *dth, daos_handle_t poh)
{
	struct vos_pool      *pool;
	struct umem_instance *umm;
	int                   rc;

	if (dth == NULL || !dth->dth_local) {
		return 0;
	}

	/**
	 * RDB is intended as the main user of local transactions. RDB is known
	 * to engage over 32 OIDs per transaction. Hence, the initial value is
	 * hoped to accommodate all of them.
	 */
	dth->dth_local_oid_cap = (1 << 6);
	dth->dth_local_oid_cnt = 0;
	D_ALLOC_ARRAY(dth->dth_local_oid_array, dth->dth_local_oid_cap);
	if (dth->dth_local_oid_array == NULL)
		return -DER_NOMEM;

	pool = vos_hdl2pool(poh);
	umm  = vos_pool2umm(pool);

	if (vos_pool_is_evictable(pool)) {
		D_ERROR("VOS local tx doesn't support evictable pool:"DF_UUID"\n",
			DP_UUID(pool->vp_id));
		rc = -DER_NOTSUPPORTED;
		goto error;
	}

	rc = vos_tx_begin(dth, umm, pool->vp_sysdb);
	if (rc != 0) {
		D_ERROR("Failed to start transaction: rc=" DF_RC "\n", DP_RC(rc));
		goto error;
	}

	return 0;

error:
	D_FREE(dth->dth_local_oid_array);
	return rc;
}

int
vos_dtx_local_end(struct dtx_handle *dth, int result)
{
	dth->dth_local_complete = 1;
	result                  = vos_tx_end(NULL, dth, NULL, NULL, true, NULL, result);

	for (int i = 0; i < dth->dth_local_oid_cnt; ++i) {
		vos_cont_decref(dth->dth_local_oid_array[i].dor_cont);
	}

	dth->dth_local_oid_cnt = 0;
	D_FREE(dth->dth_local_oid_array);
	dth->dth_local_oid_cap = 0;

	return result;
}
