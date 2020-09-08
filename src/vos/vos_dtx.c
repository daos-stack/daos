/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

/* 128 KB per SCM blob */
#define DTX_BLOB_SIZE		(1 << 17)
/** Ensure 16-bit signed int is sufficient to store record index */
D_CASSERT((DTX_BLOB_SIZE / sizeof(struct vos_dtx_act_ent_df)) <  (1 << 15));
D_CASSERT((DTX_BLOB_SIZE / sizeof(struct vos_dtx_cmt_ent_df)) <  (1 << 15));

#define DTX_ACT_BLOB_MAGIC	0x14130a2b
#define DTX_CMT_BLOB_MAGIC	0x2502191c

enum {
	DTX_UMOFF_ILOG		= (1 << 0),
	DTX_UMOFF_SVT		= (1 << 1),
	DTX_UMOFF_EVT		= (1 << 2),
};

#define DTX_UMOFF_TYPES		(DTX_UMOFF_ILOG | DTX_UMOFF_SVT | DTX_UMOFF_EVT)
#define DTX_INDEX_INVAL		(int16_t)(-1)

static inline void
dtx_type2umoff_flag(umem_off_t *rec, uint32_t type)
{
	uint8_t		flag = 0;

	switch (type) {
	case DTX_RT_ILOG:
		flag = DTX_UMOFF_ILOG;
		break;
	case DTX_RT_SVT:
		flag = DTX_UMOFF_SVT;
		break;
	case DTX_RT_EVT:
		flag = DTX_UMOFF_EVT;
		break;
	default:
		D_ASSERT(0);
	}

	umem_off_set_flags(rec, flag);
}

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

static inline bool
umoff_is_null(umem_off_t umoff)
{
	return umoff == UMOFF_NULL;
}

static inline bool
dtx_is_aborted(uint32_t tx_lid)
{
	return tx_lid == DTX_LID_ABORTED;
}

static void
dtx_set_aborted(uint32_t *tx_lid)
{
	*tx_lid = DTX_LID_ABORTED;
}

static inline int
dtx_inprogress(struct vos_dtx_act_ent *dae, struct dtx_handle *dth,
	       bool for_read)
{
	/* If the modifications crosses multiple redundancy groups, then it
	 * is possible that the sub modifications on the DTX leader are not
	 * the same as the ones on non-leaders. Under such case, if someone
	 * wants to read the data on some non-leader but hits non-committed
	 * DTX, then asking the client to retry with leader maybe not help.
	 * Instead, we can ask make the client to retry the read again (and
	 * again) sometime later. We do sync commit the DTX with 'DTE_BLOCK'
	 * flag. So it will not cause the client to retry read for too many
	 * times unless such DTX hit some trouble (such as client or server
	 * failure) that may cause current readers to be blocked until such
	 * DTX has been handled by the new leader via DTX recovery.
	 */
	if (DAE_FLAGS(dae) & DTE_BLOCK && for_read && dth != NULL &&
	    dth->dth_modification_cnt == 0)
		dth->dth_local_retry = 1;
	else if (dth != NULL)
		dth->dth_local_retry = 0;

	D_DEBUG(DB_IO,
		"Hit uncommitted DTX "DF_DTI" lid=%d, need %s retry\n",
		DP_DTI(&DAE_XID(dae)), DAE_LID(dae),
		(dth != NULL && dth->dth_local_retry) ? "local" : "remote");

	return -DER_INPROGRESS;
}

static void
dtx_act_ent_cleanup(struct vos_container *cont, struct vos_dtx_act_ent *dae,
		    bool evict)
{
	daos_unit_oid_t		*oids;
	int			 max;
	int			 i;

	D_FREE(dae->dae_records);

	if (!evict)
		return;

	if (DAE_OID_CNT(dae) == 0) {
		oids = &DAE_OID(dae);
		max = 1;
	} else if (DAE_OID_CNT(dae) == 1) {
		oids = &DAE_OID_INLINE(dae);
		max = 1;
	} else {
		oids = umem_off2ptr(vos_cont2umm(cont), DAE_OID_OFF(dae));
		max = DAE_OID_CNT(dae);
	}

	for (i = 0; i < max; i++)
		vos_obj_evict_by_oid(vos_obj_cache_current(), cont, oids[i]);
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
		dtx_act_ent_cleanup(tins->ti_priv, dae, true);
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
	struct vos_dtx_act_ent	*dae_new = val->iov_buf;
	struct vos_dtx_act_ent	*dae_old;

	dae_old = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	D_ASSERTF(0, "NOT allow to update act DTX entry for "DF_DTI
		  " from epoch "DF_X64" to "DF_X64"\n",
		  DP_DTI(&DAE_XID(dae_old)),
		  DAE_EPOCH(dae_old), DAE_EPOCH(dae_new));

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

	/* Two possible cases for that:
	 *
	 * Case one:
	 * It is possible that when commit the DTX for the first time,
	 * it failed at removing the DTX entry from active table, but
	 * at that time the DTX entry has already been added into the
	 * committed table that is in DRAM. Currently, we do not have
	 * efficient way to recover such DRAM based btree structure,
	 * so just keep it there. Then when we re-commit such DTX, we
	 * may come here.
	 *
	 * Case two:
	 * As the vos_dtx_cmt_reindex() logic going, some RPC handler
	 * ULT may add more entries into the committed table. Then it
	 * is possible that vos_dtx_cmt_reindex() logic hit the entry
	 * in the committed blob that has already been added into the
	 * indexed table.
	 */

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

int
vos_dtx_table_destroy(struct umem_instance *umm, struct vos_cont_df *cont_df)
{
	struct vos_dtx_blob_df		*dbd;
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_dtx_cmt_ent_df	*dce_df;
	umem_off_t			 dbd_off;
	int				 i;
	int				 rc;

	/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
	rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			     sizeof(cont_df->cd_dtx_committed_head) +
			     sizeof(cont_df->cd_dtx_committed_tail));
	if (rc != 0)
		return rc;

	while (!umoff_is_null(cont_df->cd_dtx_committed_head)) {
		dbd_off = cont_df->cd_dtx_committed_head;
		dbd = umem_off2ptr(umm, dbd_off);

		for (i = 0; i < dbd->dbd_count; i++) {
			dce_df = &dbd->dbd_committed_data[i];
			if (!umoff_is_null(dce_df->dce_oid_off)) {
				rc = umem_free(umm, dce_df->dce_oid_off);
				if (rc != 0)
					return rc;
			}
		}

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

	while (!umoff_is_null(cont_df->cd_dtx_active_head)) {
		dbd_off = cont_df->cd_dtx_active_head;
		dbd = umem_off2ptr(umm, dbd_off);

		for (i = 0; i < dbd->dbd_index; i++) {
			dae_df = &dbd->dbd_active_data[i];
			if (!(dae_df->dae_flags & DTE_INVALID)) {
				if (!umoff_is_null(dae_df->dae_rec_off)) {
					rc = umem_free(umm,
						       dae_df->dae_rec_off);
					if (rc != 0)
						return rc;
				}
				if (!umoff_is_null(dae_df->dae_mbs_off)) {
					rc = umem_free(umm,
						       dae_df->dae_mbs_off);
					if (rc != 0)
						return rc;
				}
				if (dae_df->dae_oid_cnt > 1) {
					rc = umem_free(umm,
						       dae_df->dae_oid_off);
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
	rc = ilog_open(umm, ilog, &cbs, &loh);
	if (rc != 0)
		return rc;

	id.id_epoch = DAE_EPOCH(dae);
	id.id_tx_id = DAE_LID(dae);

	if (abort)
		rc = ilog_abort(loh, &id);
	else
		rc = ilog_persist(loh, &id);

	ilog_close(loh);
	return rc;
}

static int
do_dtx_rec_release(struct umem_instance *umm, struct vos_container *cont,
		   struct vos_dtx_act_ent *dae, umem_off_t rec, bool abort)
{
	int	rc = 0;

	if (umoff_is_null(rec))
		return 0;

	switch (dtx_umoff_flag2type(rec)) {
	case DTX_RT_ILOG: {
		rc = dtx_ilog_rec_release(umm, cont, rec, dae, abort);
		break;
	}
	case DTX_RT_SVT: {
		struct vos_irec_df	*svt;

		svt = umem_off2ptr(umm, umem_off2offset(rec));
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

			svt->ir_dtx = DTX_LID_COMMITTED;
		}
		break;
	}
	case DTX_RT_EVT: {
		struct evt_desc		*evt;

		evt = umem_off2ptr(umm, umem_off2offset(rec));
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

			evt->dc_dtx = DTX_LID_COMMITTED;
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

	return rc;
}

#define dtx_evict_lid(cont, dae)					\
	do {								\
		D_DEBUG(DB_TRACE, "Evicting lid "DF_DTI": lid=%d\n",	\
			DP_DTI(&DAE_XID(dae)), DAE_LID(dae));		\
		lrua_evictx(cont->vc_dtx_array,				\
			    DAE_LID(dae) - DTX_LID_RESERVED,		\
			    DAE_EPOCH(dae));				\
	} while (0)

static int
dtx_rec_release(struct vos_container *cont, struct vos_dtx_act_ent *dae,
		bool abort)
{
	struct umem_instance		*umm = vos_cont2umm(cont);
	struct vos_dtx_act_ent_df	*dae_df;
	struct vos_dtx_blob_df		*dbd;
	int				 count;
	int				 i;
	int				 rc = 0;

	D_ASSERT(DAE_INDEX(dae) >= 0);

	dbd = dae->dae_dbd;
	D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);

	dae_df = umem_off2ptr(umm, dae->dae_df_off);
	D_ASSERT(dae_df != NULL);

	if (DAE_OID_CNT(dae) > 1 && abort) {
		rc = umem_free(umm, dae_df->dae_oid_off);
		if (rc != 0)
			return rc;
	}

	if (!umoff_is_null(dae_df->dae_mbs_off)) {
		/* dae_mbs_off will be invalid via flag DTE_INVALID. */
		rc = umem_free(umm, dae_df->dae_mbs_off);
		if (rc != 0)
			return rc;
	}

	if (dae->dae_records != NULL) {
		D_ASSERT(DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT);

		for (i = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT - 1;
		     i >= 0; i--) {
			rc = do_dtx_rec_release(umm, cont, dae,
						dae->dae_records[i], abort);
			if (rc != 0)
				return rc;
		}
	}

	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = count - 1; i >= 0; i--) {
		rc = do_dtx_rec_release(umm, cont, dae, DAE_REC_INLINE(dae)[i],
					abort);
		if (rc != 0)
			return rc;
	}

	if (!umoff_is_null(dae_df->dae_rec_off)) {
		rc = umem_free(umm, dae_df->dae_rec_off);
		if (rc != 0)
			return rc;
	}

	if (dbd->dbd_count > 1 || dbd->dbd_index < dbd->dbd_cap) {
		rc = umem_tx_add_ptr(umm, &dae_df->dae_flags,
				sizeof(dae_df->dae_flags));
		if (rc != 0)
			return rc;

		/* Mark the DTX entry as invalid in SCM. */
		dae_df->dae_flags = DTE_INVALID;

		rc = umem_tx_add_ptr(umm, &dbd->dbd_count,
				     sizeof(dbd->dbd_count));
		if (rc != 0)
			return rc;

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
	}

	return rc;
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct dtx_id *dti,
		   daos_epoch_t epoch, struct vos_dtx_cmt_ent **dce_p,
		   struct dtx_cos_key *dck, struct vos_dtx_act_ent **dae_p,
		   bool *fatal)
{
	struct vos_dtx_act_ent		*dae = NULL;
	struct vos_dtx_cmt_ent		*dce = NULL;
	d_iov_t				 kiov;
	d_iov_t				 riov;
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
					   &kiov, &riov);
			if (rc == 0 && dck != NULL) {
				dce = (struct vos_dtx_cmt_ent *)riov.iov_buf;
				dck->oid = DCE_OID(dce);
				dck->dkey_hash = DCE_DKEY_HASH(dce);
				dce = NULL;
			}

			goto out;
		}

		if (rc != 0)
			goto out;

		dae = (struct vos_dtx_act_ent *)riov.iov_buf;

		if (dae->dae_aborted) {
			D_ERROR("NOT allow to commit an aborted DTX "DF_DTI"\n",
				DP_DTI(dti));
			D_GOTO(out, rc = -DER_NONEXIST);
		}

		/* It has been committed before, but failed to be removed
		 * from the active table, just remove it again.
		 */
		if (dae->dae_committed) {
			if (dck != NULL) {
				dck->oid = DAE_OID(dae);
				dck->dkey_hash = DAE_DKEY_HASH(dae);
			}

			rc = dbtree_delete(cont->vc_dtx_active_hdl,
					   BTR_PROBE_BYPASS, &kiov, &dae);
			if (rc == 0) {
				dtx_act_ent_cleanup(cont, dae, false);
				dtx_evict_lid(cont, dae);
			}

			goto out;
		}
	}

	D_ALLOC_PTR(dce);
	if (dce == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (dae != NULL) {
		memcpy(&dce->dce_base.dce_common, &dae->dae_base.dae_common,
		       sizeof(dce->dce_base.dce_common));
		if (DAE_OID_CNT(dae) == 1) {
			/* Overwrite DCE_OID if modify single object. */
			DCE_OID(dce) = DAE_OID_INLINE(dae);
			DCE_OID_CNT(dce) = 1;
		} else if (DAE_OID_CNT(dae) > 1) {
			/* Take over the OID_OFF from active entry. */
			DCE_OID_OFF(dce) = DAE_OID_OFF(dae);
			DCE_OID_CNT(dce) = DAE_OID_CNT(dae);
		} else {
			/* Only the leader_oid is modified by the DTX.*/
			DCE_OID_CNT(dce) = 1;
		}
	} else {
		struct dtx_handle	*dth = vos_dth_get();

		D_ASSERT(dtx_is_valid_handle(dth));

		DCE_XID(dce) = *dti;
		DCE_EPOCH(dce) = epoch;
		if (dth->dth_oid_array != NULL) {
			if (dth->dth_oid_cnt == 1) {
				DCE_OID(dce) = dth->dth_oid_array[0];
				DCE_OID_CNT(dce) = 1;
			} else {
				struct umem_instance	*umm;
				size_t			 size;
				umem_off_t		 rec_off;

				umm = vos_cont2umm(cont);
				size = sizeof(daos_unit_oid_t) *
					dth->dth_oid_cnt;
				rec_off = umem_zalloc(umm, size);

				if (umoff_is_null(rec_off)) {
					D_ERROR("No space to store CMT DTX OID "
						DF_DTI"\n", DP_DTI(dti));
					D_GOTO(out, rc = -DER_NOSPACE);
				}

				memcpy(umem_off2ptr(umm, rec_off),
				       dth->dth_oid_array, size);
				DCE_OID_OFF(dce) = rec_off;
				DCE_OID_CNT(dce) = dth->dth_oid_cnt;
			}
		} else {
			D_ASSERT(dth->dth_oid_cnt == 0);

			DCE_OID(dce) = dth->dth_leader_oid;
			DCE_OID_CNT(dce) = 1;
		}
	}

	dce->dce_reindex = 0;

	d_iov_set(&riov, dce, sizeof(*dce));
	rc = dbtree_upsert(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc != 0 || epoch != 0)
		goto out;

	rc = dtx_rec_release(cont, dae, false);
	if (rc != 0) {
		*fatal = true;
		goto out;
	}

	if (dck != NULL) {
		dck->oid = DAE_OID(dae);
		dck->dkey_hash = DAE_DKEY_HASH(dae);
	}

	D_ASSERT(dae_p != NULL);
	*dae_p = dae;

out:
	D_CDEBUG(rc != 0 && rc != -DER_NONEXIST, DLOG_ERR, DB_IO,
		 "Commit the DTX "DF_DTI": rc = "DF_RC"\n",
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
		  struct dtx_id *dti, struct vos_dtx_act_ent **dae_p,
		  bool *fatal)
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

	dae = (struct vos_dtx_act_ent *)riov.iov_buf;

	if (dae->dae_committable || dae->dae_committed) {
		D_ERROR("NOT allow to abort a committed DTX "DF_DTI"\n",
			DP_DTI(dti));
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	/* It has been committed before, but failed to be removed
	 * from the active table, just remove it again.
	 */
	if (dae->dae_aborted) {
		rc = dbtree_delete(cont->vc_dtx_active_hdl,
				   BTR_PROBE_BYPASS, &kiov, &dae);
		if (rc == 0) {
			dtx_act_ent_cleanup(cont, dae, false);
			dtx_evict_lid(cont, dae);
		}

		goto out;
	}

	if (epoch != 0 && DAE_EPOCH(dae) > epoch)
		D_GOTO(out, rc = -DER_NONEXIST);

	rc = dtx_rec_release(cont, dae, true);
	if (rc != 0) {
		*fatal = true;
		goto out;
	}

	D_ASSERT(dae_p != NULL);
	*dae_p = dae;

out:
	D_DEBUG(DB_IO, "Abort the DTX "DF_DTI": rc = "DF_RC"\n", DP_DTI(dti),
		DP_RC(rc));

	return rc;
}

static bool
vos_dtx_is_normal_entry(uint32_t entry)
{
	if (entry < DTX_LID_RESERVED)
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
	int				 rc;

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
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_head,
				     sizeof(cont_df->cd_dtx_active_head) +
				     sizeof(cont_df->cd_dtx_active_tail));
		if (rc != 0)
			return rc;

		cont_df->cd_dtx_active_head = dbd_off;
	} else {
		rc = umem_tx_add_ptr(umm, &tmp->dbd_next,
				     sizeof(tmp->dbd_next));
		if (rc != 0)
			return rc;

		tmp->dbd_next = dbd_off;

		dbd->dbd_prev = cont_df->cd_dtx_active_tail;
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_active_tail,
				     sizeof(cont_df->cd_dtx_active_tail));
		if (rc != 0)
			return rc;
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
	uint32_t			 idx;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	cont_df = cont->vc_cont_df;

	rc = lrua_allocx(cont->vc_dtx_array, &idx, dth->dth_epoch, &dae);
	if (rc != 0) {
		/** The array is full, need to commit some transactions first */
		if (rc == -DER_BUSY)
			return -DER_INPROGRESS;
		return rc;
	}

	dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	if (dbd == NULL || dbd->dbd_index >= dbd->dbd_cap) {
		rc = vos_dtx_extend_act_table(cont);
		if (rc != 0)
			goto out;

		dbd = umem_off2ptr(umm, cont_df->cd_dtx_active_tail);
	}

	DAE_LID(dae) = idx + DTX_LID_RESERVED;
	DAE_XID(dae) = dth->dth_xid;
	DAE_OID(dae) = dth->dth_leader_oid;
	DAE_DKEY_HASH(dae) = dth->dth_dkey_hash;
	DAE_EPOCH(dae) = dth->dth_epoch;
	DAE_FLAGS(dae) = dth->dth_flags;
	DAE_VER(dae) = dth->dth_ver;

	D_ASSERT(dth->dth_mbs != NULL);

	DAE_TGT_CNT(dae) = dth->dth_mbs->dm_tgt_cnt;
	DAE_GRP_CNT(dae) = dth->dth_mbs->dm_grp_cnt;
	DAE_MBS_DSIZE(dae) = dth->dth_mbs->dm_data_size;

	/* Will be set as dbd::dbd_index via vos_dtx_prepared(). */
	DAE_INDEX(dae) = DTX_INDEX_INVAL;

	dae->dae_df_off = cont_df->cd_dtx_active_tail +
			offsetof(struct vos_dtx_blob_df, dbd_active_data) +
			sizeof(struct vos_dtx_act_ent_df) * dbd->dbd_index;
	dae->dae_dbd = dbd;
	D_ASSERT(dbd->dbd_magic == DTX_ACT_BLOB_MAGIC);
	D_DEBUG(DB_IO, "Allocated new lid DTX: "DF_DTI" lid=%d dae=%p"
		" dae_dbd=%p\n", DP_DTI(&dth->dth_xid), DAE_LID(dae), dae, dbd);

	d_iov_set(&kiov, &DAE_XID(dae), sizeof(DAE_XID(dae)));
	d_iov_set(&riov, dae, sizeof(*dae));
	rc = dbtree_upsert(cont->vc_dtx_active_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);
	if (rc == 0) {
		dth->dth_ent = dae;
		dth->dth_active = 1;
	}

out:
	if (rc != 0)
		dtx_evict_lid(cont, dae);

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

int
vos_dtx_check_availability(daos_handle_t coh, uint32_t entry,
			   daos_epoch_t epoch, uint32_t intent, uint32_t type)
{
	struct dtx_handle		*dth = vos_dth_get();
	struct vos_container		*cont;
	struct vos_dtx_act_ent		*dae = NULL;
	bool				 found;

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
	if (entry == DTX_LID_COMMITTED)
		return ALB_AVAILABLE_CLEAN;

	if (intent == DAOS_INTENT_PURGE)
		return ALB_AVAILABLE_DIRTY;

	/* Aborted */
	if (dtx_is_aborted(entry))
		return ALB_UNAVAILABLE;

	/* The DTX owner can always see the DTX. */
	if (dtx_is_valid_handle(dth) && dth->dth_ent != NULL) {
		dae = dth->dth_ent;
		if (DAE_LID(dae) == entry && DAE_EPOCH(dae) == epoch)
			return ALB_AVAILABLE_CLEAN;
	}

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	found = lrua_lookupx(cont->vc_dtx_array, entry - DTX_LID_RESERVED,
			     epoch, &dae);

	if (!found) {
		/** If we move to not marking entries explicitly, this
		 *  state will mean it is committed
		 */
		D_DEBUG(DB_TRACE,
			"Entry %d "DF_U64" not in lru array, it must be"
			" committed\n", entry, epoch);
		return ALB_AVAILABLE_CLEAN;
	}

	if (dae->dae_committable || dae->dae_committed)
		return ALB_AVAILABLE_CLEAN;

	if (dae->dae_aborted)
		return ALB_UNAVAILABLE;

	/* The following are for non-committable cases. */

	if (intent == DAOS_INTENT_DEFAULT || intent == DAOS_INTENT_REBUILD) {
		if (!(DAE_FLAGS(dae) & DTE_LEADER) ||
		    DAOS_FAIL_CHECK(DAOS_VOS_NON_LEADER)) {
			/* Inavailable for rebuild case. */
			if (intent == DAOS_INTENT_REBUILD)
				return ALB_UNAVAILABLE;

			/* Non-leader and non-rebuild case, return
			 * -DER_INPROGRESS, then the caller will retry
			 * the RPC with leader replica.
			 */
			return dtx_inprogress(dae, dth, true);
		}

		/* For leader, non-committed DTX is unavailable. */
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

		return dtx_inprogress(dae, dth, false);
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
vos_dtx_get(void)
{
	struct dtx_handle	*dth = vos_dth_get();
	struct vos_dtx_act_ent	*dae;

	if (!dtx_is_valid_handle(dth) || dth->dth_ent == NULL)
		return DTX_LID_COMMITTED;

	dae = dth->dth_ent;

	return DAE_LID(dae);
}

/* The caller has started PMDK transaction. */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, uint32_t *tx_id)
{
	struct dtx_handle	*dth = vos_dth_get();
	int			 rc = 0;

	if (!dtx_is_valid_handle(dth)) {
		*tx_id = DTX_LID_COMMITTED;
		return 0;
	}

	/* For single participator case, we only need committed DTX
	 * entry for handling resend case, nothing for active table.
	 */
	if (dth->dth_solo) {
		dth->dth_active = 1;
		*tx_id = DTX_LID_COMMITTED;
		return 0;
	}

	if (dth->dth_ent == NULL) {
		rc = vos_dtx_alloc(umm, dth);
		if (rc != 0)
			return rc;
	}

	rc = vos_dtx_append(dth, record, type);
	if (rc == 0) {
		struct vos_dtx_act_ent	*dae = dth->dth_ent;

		/* Incarnation log entry implies a share */
		*tx_id = DAE_LID(dae);
		if (type == DTX_RT_ILOG)
			dth->dth_modify_shared = 1;
	}

	D_DEBUG(DB_TRACE, "Register DTX record for "DF_DTI
		": lid=%d entry %p, type %d, %s ilog entry, rc %d\n",
		DP_DTI(&dth->dth_xid),
		DAE_LID((struct vos_dtx_act_ent *)dth->dth_ent), dth->dth_ent,
		type, dth->dth_modify_shared ? "has" : "has not", rc);

	return rc;
}

/* The caller has started PMDK transaction. */
void
vos_dtx_deregister_record(struct umem_instance *umm, daos_handle_t coh,
			  uint32_t entry, daos_epoch_t epoch, umem_off_t record)
{
	struct vos_container		*cont;
	struct vos_dtx_act_ent		*dae;
	struct vos_dtx_act_ent_df	*dae_df;
	umem_off_t			*rec_df;
	bool				 found;
	int				 count;
	int				 i;

	if (!vos_dtx_is_normal_entry(entry))
		return;

	D_ASSERT(entry >= DTX_LID_RESERVED);

	cont = vos_hdl2cont(coh);
	/* If "cont" is NULL, then we are destroying the container.
	 * Under such case, the DTX entry in DRAM has been removed,
	 * The on-disk entry will be destroyed soon.
	 */
	if (cont == NULL)
		return;

	found = lrua_lookupx(cont->vc_dtx_array, entry - DTX_LID_RESERVED,
			     epoch, &dae);
	if (!found) {
		D_WARN("Could not find active DTX record for lid=%d, epoch="
		       DF_U64"\n", entry, epoch);
		return;
	}

	dae_df = umem_off2ptr(umm, dae->dae_df_off);
	if (daos_is_zero_dti(&dae_df->dae_xid) ||
	    dae_df->dae_flags & DTE_INVALID)
		return;

	if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT)
		count = DTX_INLINE_REC_CNT;
	else
		count = DAE_REC_CNT(dae);

	for (i = 0; i < count; i++) {
		if (record == umem_off2offset(DAE_REC_INLINE(dae)[i])) {
			DAE_REC_INLINE(dae)[i] = UMOFF_NULL;
			goto handle_df;
		}
	}

	for (i = 0; i < DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT; i++) {
		if (record == umem_off2offset(dae->dae_records[i])) {
			dae->dae_records[i] = UMOFF_NULL;
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
		if (umem_off2offset(rec_df[i]) == record) {
			rec_df[i] = UMOFF_NULL;
			return;
		}
	}

	rec_df = umem_off2ptr(umm, dae_df->dae_rec_off);

	/* Not found */
	if (rec_df == NULL)
		return;

	for (i = 0; i < dae_df->dae_rec_cnt - DTX_INLINE_REC_CNT; i++) {
		if (umem_off2offset(rec_df[i]) == record) {
			rec_df[i] = UMOFF_NULL;
			return;
		}
	}
}

int
vos_dtx_prepared(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent		*dae;
	struct vos_container		*cont;
	struct umem_instance		*umm;
	struct vos_dtx_blob_df		*dbd;
	umem_off_t			 rec_off;
	size_t				 size;
	int				 count;
	int				 rc;

	if (!dth->dth_active)
		return 0;

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	if (dth->dth_solo) {
		rc = vos_dtx_commit_internal(cont, &dth->dth_xid, 1,
					     dth->dth_epoch, NULL, NULL);
		dth->dth_active = 0;
		if (rc >= 0)
			dth->dth_sync = 1;

		return rc > 0 ? 0 : rc;
	}

	dae = dth->dth_ent;
	D_ASSERT(dae != NULL);

	umm = vos_cont2umm(cont);
	dbd = dae->dae_dbd;

	/* If the DTX is for punch object that is quite possible affect
	 * subsequent operations, then synchronously commit the DTX when
	 * it becomes committable to avoid availability trouble.
	 */
	if (DAE_DKEY_HASH(dae) == 0)
		dth->dth_sync = 1;

	DAE_OID_CNT(dae) = dth->dth_oid_cnt;
	if (dth->dth_oid_array != NULL) {
		D_ASSERT(dth->dth_oid_cnt != 0);

		if (dth->dth_oid_cnt == 1) {
			DAE_OID_INLINE(dae) = dth->dth_oid_array[0];
		} else {
			size = sizeof(daos_unit_oid_t) * dth->dth_oid_cnt;

			rec_off = umem_zalloc(umm, size);
			if (umoff_is_null(rec_off)) {
				D_ERROR("No space to store DTX OIDs "DF_DTI"\n",
					DP_DTI(&DAE_XID(dae)));
				return -DER_NOSPACE;
			}

			memcpy(umem_off2ptr(umm, rec_off),
			       dth->dth_oid_array, size);
			DAE_OID_OFF(dae) = rec_off;
		}
	}

	if (DAE_MBS_DSIZE(dae) <= sizeof(DAE_MBS_INLINE(dae))) {
		memcpy(DAE_MBS_INLINE(dae), dth->dth_mbs->dm_data,
		       DAE_MBS_DSIZE(dae));
	} else {
		rec_off = umem_zalloc(umm, DAE_MBS_DSIZE(dae));
		if (umoff_is_null(rec_off)) {
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
		if (umoff_is_null(rec_off)) {
			D_ERROR("No space to store active DTX "DF_DTI"\n",
				DP_DTI(&DAE_XID(dae)));
			return -DER_NOSPACE;
		}

		memcpy(umem_off2ptr(umm, rec_off), dae->dae_records, size);
		DAE_REC_OFF(dae) = rec_off;
	}

	DAE_INDEX(dae) = dbd->dbd_index;
	if (DAE_INDEX(dae) > 0) {
		pmem_memcpy_nodrain(umem_off2ptr(umm, dae->dae_df_off),
				    &dae->dae_base,
				    sizeof(struct vos_dtx_act_ent_df));
		/* dbd_index is next to dbd_count */
		rc = umem_tx_add_ptr(umm, &dbd->dbd_count,
				     sizeof(dbd->dbd_count) +
				     sizeof(dbd->dbd_index));
		if (rc != 0)
			return rc;
	} else {
		memcpy(umem_off2ptr(umm, dae->dae_df_off),
		       &dae->dae_base, sizeof(struct vos_dtx_act_ent_df));
	}

	dbd->dbd_count++;
	dbd->dbd_index++;

	return 0;
}

int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t *epoch,
	      uint32_t *pm_ver, bool for_resent)
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
		if (dae->dae_committable || dae->dae_committed)
			return DTX_ST_COMMITTED;

		if (dae->dae_aborted)
			return -DER_NONEXIST;

		if (epoch != NULL) {
			if (*epoch == 0)
				*epoch = DAE_EPOCH(dae);
			else if (*epoch != DAE_EPOCH(dae))
				return -DER_MISMATCH;
		}

		if (pm_ver != NULL)
			*pm_ver = DAE_VER(dae);

		return DTX_ST_PREPARED;
	}

	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, NULL);
		if (rc == 0)
			return DTX_ST_COMMITTED;
	}

	if (rc == -DER_NONEXIST && for_resent && cont->vc_reindex_cmt_dtx)
		rc = -DER_AGAIN;

	return rc;
}

int
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int count, daos_epoch_t epoch, struct dtx_cos_key *dcks,
			struct vos_dtx_act_ent **daes)
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
	bool				 fatal = false;

	dbd = umem_off2ptr(umm, cont_df->cd_dtx_committed_tail);
	if (dbd != NULL)
		slots = dbd->dbd_cap - dbd->dbd_count;

	if (slots == 0)
		goto new_blob;

	rc = umem_tx_add_ptr(umm, &dbd->dbd_count, sizeof(dbd->dbd_count));
	if (rc != 0)
		return rc;

again:
	if (slots > count)
		slots = count;

	count -= slots;

	if (slots > 1) {
		D_ALLOC(dce_df, sizeof(*dce_df) * slots);
		if (dce_df == NULL) {
			D_ERROR("Not enough DRAM to commit "DF_DTI"\n",
				DP_DTI(&dtis[cur]));

			/* non-fatal, former handled ones can be committed. */
			return committed > 0 ? committed : -DER_NOMEM;
		}
	} else {
		dce_df = &dbd->dbd_committed_data[dbd->dbd_count];
	}

	for (i = 0, j = 0; i < slots && rc1 == 0; i++, cur++) {
		struct vos_dtx_cmt_ent	*dce = NULL;

		rc = vos_dtx_commit_one(cont, &dtis[cur], epoch, &dce,
					dcks != NULL ? &dcks[cur] : NULL,
					daes != NULL ? &daes[cur] : NULL,
					&fatal);
		if (fatal)
			return rc;

		if (rc == 0 && (daes == NULL || daes[cur] != NULL))
			committed++;

		if (rc == -DER_NONEXIST)
			rc = 0;

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

	if (dce_df != &dbd->dbd_committed_data[dbd->dbd_count]) {
		if (j > 0)
			pmem_memcpy_nodrain(
				&dbd->dbd_committed_data[dbd->dbd_count],
				dce_df, sizeof(*dce_df) * j);
		D_FREE(dce_df);
	}

	if (j > 0)
		dbd->dbd_count += j;

	if (count == 0 || rc1 != 0)
		return committed > 0 ? committed : rc1;

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
		return committed > 0 ? committed : -DER_NOSPACE;
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
			return committed > 0 ? committed : -DER_NOMEM;
		}
	} else {
		dce_df = &dbd->dbd_committed_data[0];
	}

	if (dbd_prev == NULL) {
		D_ASSERT(umoff_is_null(cont_df->cd_dtx_committed_head));
		D_ASSERT(umoff_is_null(cont_df->cd_dtx_committed_tail));

		/* cd_dtx_committed_tail is next to cd_dtx_committed_head */
		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
				     sizeof(cont_df->cd_dtx_committed_head) +
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0)
			return rc;

		cont_df->cd_dtx_committed_head = dbd_off;
	} else {
		rc = umem_tx_add_ptr(umm, &dbd_prev->dbd_next,
				     sizeof(dbd_prev->dbd_next));
		if (rc != 0)
			return rc;

		dbd_prev->dbd_next = dbd_off;

		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0)
			return rc;
	}

	cont_df->cd_dtx_committed_tail = dbd_off;

	for (i = 0, j = 0; i < count && rc1 == 0; i++, cur++) {
		struct vos_dtx_cmt_ent	*dce = NULL;

		rc = vos_dtx_commit_one(cont, &dtis[cur], epoch, &dce,
					dcks != NULL ? &dcks[cur] : NULL,
					daes != NULL ? &daes[cur] : NULL,
					&fatal);
		if (fatal)
			return rc;

		if (rc == 0 && (daes == NULL || daes[cur] != NULL))
			committed++;

		if (rc == -DER_NONEXIST)
			rc = 0;

		if (rc1 == 0)
			rc1 = rc;

		if (dce != NULL) {
			memcpy(&dce_df[j], &dce->dce_base, sizeof(dce_df[j]));
			j++;
		}
	}

	if (dce_df != &dbd->dbd_committed_data[0]) {
		if (j > 0)
			memcpy(&dbd->dbd_committed_data[0], dce_df,
			       sizeof(*dce_df) * j);
		D_FREE(dce_df);
	}

	dbd->dbd_count = j;

	return committed > 0 ? committed : rc1;
}

void
vos_dtx_post_handle(struct vos_container *cont, struct vos_dtx_act_ent **daes,
		    int count, bool abort)
{
	int	rc;
	int	i;

	for (i = 0; i < count; i++) {
		d_iov_t		kiov;

		if (daes[i] == NULL)
			continue;

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
			if (abort)
				daes[i]->dae_aborted = 1;
			else
				daes[i]->dae_committed = 1;
		}
	}
}

int
vos_dtx_commit(daos_handle_t coh, struct dtx_id *dtis, int count,
	       struct dtx_cos_key *dcks)
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_container	 *cont;
	int			  committed = 0;
	int			  rc;

	D_ASSERT(count > 0);

	D_ALLOC_ARRAY(daes, count);
	if (daes == NULL)
		return -DER_NOMEM;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Commit multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		committed = vos_dtx_commit_internal(cont, dtis, count,
						    0, dcks, daes);
		rc = umem_tx_end(vos_cont2umm(cont),
				 committed > 0 ? 0 : committed);
		if (rc == 0)
			vos_dtx_post_handle(cont, daes, count, false);
	}

	D_FREE(daes);

	return rc < 0 ? rc : committed;
}

int
vos_dtx_abort(daos_handle_t coh, daos_epoch_t epoch, struct dtx_id *dtis,
	      int count)
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_container	 *cont;
	int			  aborted = 0;
	int			  rc;
	int			  i;
	bool			  fatal = false;

	D_ASSERT(count > 0);

	D_ALLOC_ARRAY(daes, count);
	if (daes == NULL)
		return -DER_NOMEM;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* Abort multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(vos_cont2umm(cont), NULL);
	if (rc == 0) {
		for (i = 0; i < count; i++) {
			rc = vos_dtx_abort_one(cont, epoch, &dtis[i], &daes[i],
					       &fatal);
			if (fatal) {
				aborted = rc;
				break;
			}

			if (rc == 0 && daes[i] != NULL)
				aborted++;
		}

		rc = umem_tx_end(vos_cont2umm(cont), aborted > 0 ? 0 : rc);
		if (rc == 0)
			vos_dtx_post_handle(cont, daes, count, true);
	}

	D_FREE(daes);

	return rc < 0 ? rc : aborted;
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

	/** Take the opportunity to free some memory if we can */
	lrua_array_aggregate(cont->vc_dtx_array);

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		return rc;

	for (i = 0; i < dbd->dbd_count &&
	     !d_list_empty(&cont->vc_dtx_committed_list); i++) {
		struct vos_dtx_cmt_ent	*dce;
		d_iov_t			 kiov;
		umem_off_t		 umoff;

		dce = d_list_entry(cont->vc_dtx_committed_list.next,
				   struct vos_dtx_cmt_ent, dce_committed_link);
		umoff = DCE_OID_OFF(dce);
		d_iov_set(&kiov, &DCE_XID(dce), sizeof(DCE_XID(dce)));
		dbtree_delete(cont->vc_dtx_committed_hdl, BTR_PROBE_EQ,
			      &kiov, NULL);

		if (!umoff_is_null(umoff)) {
			rc = umem_free(umm, umoff);
			if (rc != 0)
				D_WARN("Failed to release OIDs array "DF_RC"\n",
				       DP_RC(rc));
		}
	}

	tmp = umem_off2ptr(umm, dbd->dbd_next);
	if (tmp == NULL) {
		/* The last blob for committed DTX blob. */
		D_ASSERT(cont_df->cd_dtx_committed_tail ==
			 cont_df->cd_dtx_committed_head);

		rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_tail,
				     sizeof(cont_df->cd_dtx_committed_tail));
		if (rc != 0)
			return rc;

		cont_df->cd_dtx_committed_tail = UMOFF_NULL;
	} else {
		rc = umem_tx_add_ptr(umm, &tmp->dbd_prev,
				     sizeof(tmp->dbd_prev));
		if (rc != 0)
			return rc;

		tmp->dbd_prev = UMOFF_NULL;
	}

	rc = umem_tx_add_ptr(umm, &cont_df->cd_dtx_committed_head,
			     sizeof(cont_df->cd_dtx_committed_head));
	if (rc != 0)
		return rc;

	cont_df->cd_dtx_committed_head = dbd->dbd_next;

	rc = umem_free(umm, dbd_off);

	return umem_tx_end(umm, rc);
}

void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat)
{
	struct vos_container	*cont;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

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

void
vos_dtx_mark_committable(struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	*dae = dth->dth_ent;

	if (dth->dth_active) {
		D_ASSERT(dae != NULL);

		dae->dae_committable = 1;
	}
}

int
vos_dtx_check_sync(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t *epoch)
{
	struct vos_container	*cont;
	struct daos_lru_cache	*occ;
	struct vos_object	*obj;
	daos_epoch_range_t	 epr = {0, *epoch};
	int			 rc;

	cont = vos_hdl2cont(coh);
	occ = vos_obj_cache_current();

	/* Sync epoch check inside vos_obj_hold(). We do not
	 * care about whether it is for punch or update, use
	 * DAOS_INTENT_COS to bypass DTX conflict check.
	 */
	rc = vos_obj_hold(occ, cont, oid, &epr, true,
			  DAOS_INTENT_COS, true, &obj, 0);
	if (rc != 0) {
		D_ERROR(DF_UOID" fail to check sync: rc = "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
	} else {
		*epoch = obj->obj_sync_epoch;
		vos_obj_release(occ, obj, false);
	}

	return rc;
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

			if (DAE_REC_CNT(dae) > DTX_INLINE_REC_CNT) {
				size_t	size;
				int	count;

				count = DAE_REC_CNT(dae) - DTX_INLINE_REC_CNT;
				size = sizeof(*dae->dae_records) * count;

				D_ALLOC(dae->dae_records, size);
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
					   &kiov, &riov);
			if (rc != 0) {
				D_FREE(dae->dae_records);
				dtx_evict_lid(cont, dae);
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
		if (daos_is_zero_dti(&dbd->dbd_committed_data[i].dce_xid) ||
		    dbd->dbd_committed_data[i].dce_epoch == 0) {
			D_WARN("Skip invalid committed DTX entry\n");
			continue;
		}

		D_ALLOC_PTR(dce);
		if (dce == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(&dce->dce_base, &dbd->dbd_committed_data[i],
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
vos_dtx_cleanup_internal(struct dtx_handle *dth)
{
	struct vos_container	*cont;
	struct vos_dtx_act_ent	*dae = NULL;
	d_iov_t			 kiov;
	int			 rc;

	if (!dtx_is_valid_handle(dth) || !dth->dth_active)
		return;

	dth->dth_active = 0;
	cont = vos_hdl2cont(dth->dth_coh);

	if (!dth->dth_solo) {
		d_iov_set(&kiov, &dth->dth_xid, sizeof(dth->dth_xid));
		rc = dbtree_delete(cont->vc_dtx_active_hdl, BTR_PROBE_EQ, &kiov,
				   &dae);
		if (rc != 0) {
			D_ERROR("Fail to remove DTX entry "DF_DTI":" DF_RC"\n",
				DP_DTI(&dth->dth_xid), DP_RC(rc));
		} else {
			dtx_act_ent_cleanup(cont, dae, true);
			dtx_evict_lid(cont, dae);
		}
	}
}

void
vos_dtx_cleanup(struct dtx_handle *dth)
{
	struct vos_container	*cont;

	if (!dtx_is_valid_handle(dth) || !dth->dth_active)
		return;

	cont = vos_hdl2cont(dth->dth_coh);
	/** This will abort the transaction and callback to
	 *  vos_dtx_cleanup_internal
	 */
	vos_tx_end(cont, dth, NULL, NULL, true /* don't care */, -DER_CANCELED);
}

/** Allocate space for saving the vos reservations and deferred actions */
int
vos_dtx_rsrvd_init(struct dtx_handle *dth)
{
	dth->dth_rsrvd_cnt = 0;
	dth->dth_deferred_cnt = 0;
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
	D_ASSERT(d_list_empty(&dth->dth_deferred_nvme));
	D_FREE(dth->dth_deferred);
	if (dth->dth_rsrvds != &dth->dth_rsrvd_inline)
		D_FREE(dth->dth_rsrvds);
}
