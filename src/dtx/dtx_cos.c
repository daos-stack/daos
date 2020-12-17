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
 * dtx/dtx_cos.c
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include "dtx_internal.h"

/* The record for the DTX CoS B+tree in DRAM. Each record contains current
 * committable DTXs that modify (update or punch) something under the same
 * object and the same dkey.
 */
struct dtx_cos_rec {
	daos_unit_oid_t		 dcr_oid;
	/* The DTXs in the list only modify some SVT value or EVT value
	 * (neither obj nor dkey/akey) that will not be shared by other
	 * modifications.
	 */
	d_list_t		 dcr_reg_list;
	/* XXX: The DTXs in the list modify (create/punch) some object or
	 *	dkey/akey that may be shared by other modifications, then
	 *	needs to be committed ASAP; otherwise, there may be a lot
	 *	of prepared ilog entries that will much affect subsequent
	 *	operation efficiency.
	 */
	d_list_t		 dcr_prio_list;
	/* The list for those DTXs that need to committed via explicit DTX
	 * commit RPC instead of piggyback via dispatched update/punch RPC.
	 */
	d_list_t		 dcr_expcmt_list;
	/* The number of the PUNCH DTXs in the dcr_reg_list. */
	int			 dcr_reg_count;
	/* The number of the DTXs in the dcr_prio_list. */
	int			 dcr_prio_count;
	/* The number of the DTXs in the dcr_explicit_list. */
	int			 dcr_expcmt_count;
};

/* Above dtx_cos_rec is consisted of a series of dtx_cos_rec_child uints.
 * Each dtx_cos_rec_child contains one DTX that modifies something under
 * related object and dkey (that attached to the dtx_cos_rec).
 */
struct dtx_cos_rec_child {
	/* Link into the container::sc_dtx_cos_list. */
	d_list_t		 dcrc_gl_committable;
	/* Link into related dcr_{reg,prio}_list. */
	d_list_t		 dcrc_lo_link;
	/* The DTX identifier. */
	struct dtx_entry	*dcrc_dte;
	/* The DTX epoch. */
	daos_epoch_t		 dcrc_epoch;
	/* Pointer to the dtx_cos_rec. */
	struct dtx_cos_rec	*dcrc_ptr;
};

struct dtx_cos_rec_bundle {
	struct dtx_entry	*dte;
	daos_epoch_t		 epoch;
	uint32_t		 flags;
};

static int
dtx_cos_hkey_size(void)
{
	return sizeof(struct dtx_cos_key);
}

static void
dtx_cos_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct dtx_cos_key));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtx_cos_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct dtx_cos_key *hkey1 = (struct dtx_cos_key *)&rec->rec_hkey[0];
	struct dtx_cos_key *hkey2 = (struct dtx_cos_key *)hkey;
	int		    rc;

	rc = memcmp(hkey1, hkey2, sizeof(struct dtx_cos_key));

	return dbtree_key_cmp_rc(rc);
}

static int
dtx_cos_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		  d_iov_t *val_iov, struct btr_record *rec)
{
	struct ds_cont_child		*cont = tins->ti_priv;
	struct dtx_cos_key		*key;
	struct dtx_cos_rec_bundle	*rbund;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	key = (struct dtx_cos_key *)key_iov->iov_buf;
	rbund = (struct dtx_cos_rec_bundle *)val_iov->iov_buf;

	D_ALLOC_PTR(dcr);
	if (dcr == NULL)
		return -DER_NOMEM;

	dcr->dcr_oid = key->oid;
	D_INIT_LIST_HEAD(&dcr->dcr_reg_list);
	D_INIT_LIST_HEAD(&dcr->dcr_prio_list);
	D_INIT_LIST_HEAD(&dcr->dcr_expcmt_list);

	D_ALLOC_PTR(dcrc);
	if (dcrc == NULL) {
		D_FREE_PTR(dcr);
		return -DER_NOMEM;
	}

	dcrc->dcrc_dte = dtx_entry_get(rbund->dte);
	dcrc->dcrc_epoch = rbund->epoch;
	dcrc->dcrc_ptr = dcr;

	d_list_add_tail(&dcrc->dcrc_gl_committable,
			&cont->sc_dtx_cos_list);
	cont->sc_dtx_committable_count++;

	if (rbund->flags & DCF_EXP_CMT) {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_expcmt_list);
		dcr->dcr_expcmt_count = 1;
	} else if (rbund->flags & DCF_SHARED) {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_prio_list);
		dcr->dcr_prio_count = 1;
	} else {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_reg_list);
		dcr->dcr_reg_count = 1;
	}

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dcr);

	return 0;
}

static int
dtx_cos_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct ds_cont_child		*cont = tins->ti_priv;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;
	struct dtx_cos_rec_child	*next;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_list_for_each_entry_safe(dcrc, next, &dcr->dcr_reg_list,
				   dcrc_lo_link) {
		d_list_del(&dcrc->dcrc_lo_link);
		d_list_del(&dcrc->dcrc_gl_committable);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);
		cont->sc_dtx_committable_count--;
	}
	d_list_for_each_entry_safe(dcrc, next, &dcr->dcr_prio_list,
				   dcrc_lo_link) {
		d_list_del(&dcrc->dcrc_lo_link);
		d_list_del(&dcrc->dcrc_gl_committable);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);
		cont->sc_dtx_committable_count--;
	}
	d_list_for_each_entry_safe(dcrc, next, &dcr->dcr_expcmt_list,
				   dcrc_lo_link) {
		d_list_del(&dcrc->dcrc_lo_link);
		d_list_del(&dcrc->dcrc_gl_committable);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);
		cont->sc_dtx_committable_count--;
	}
	D_FREE_PTR(dcr);

	return 0;
}

static int
dtx_cos_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct dtx_cos_rec	*dcr;

	D_ASSERT(val_iov != NULL);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, dcr, sizeof(struct dtx_cos_rec));

	return 0;
}

static int
dtx_cos_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val)
{
	struct ds_cont_child		*cont = tins->ti_priv;
	struct dtx_cos_rec_bundle	*rbund;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rbund = (struct dtx_cos_rec_bundle *)val->iov_buf;

	D_ALLOC_PTR(dcrc);
	if (dcrc == NULL)
		return -DER_NOMEM;

	dcrc->dcrc_dte = dtx_entry_get(rbund->dte);
	dcrc->dcrc_epoch = rbund->epoch;
	dcrc->dcrc_ptr = dcr;

	d_list_add_tail(&dcrc->dcrc_gl_committable,
			&cont->sc_dtx_cos_list);
	cont->sc_dtx_committable_count++;

	if (rbund->flags & DCF_EXP_CMT) {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_expcmt_list);
		dcr->dcr_expcmt_count++;
	} else if (rbund->flags & DCF_SHARED) {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_prio_list);
		dcr->dcr_prio_count++;
	} else {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_reg_list);
		dcr->dcr_reg_count++;
	}

	return 0;
}

btr_ops_t dtx_btr_cos_ops = {
	.to_hkey_size	= dtx_cos_hkey_size,
	.to_hkey_gen	= dtx_cos_hkey_gen,
	.to_hkey_cmp	= dtx_cos_hkey_cmp,
	.to_rec_alloc	= dtx_cos_rec_alloc,
	.to_rec_free	= dtx_cos_rec_free,
	.to_rec_fetch	= dtx_cos_rec_fetch,
	.to_rec_update	= dtx_cos_rec_update,
};

int
dtx_fetch_committable(struct ds_cont_child *cont, uint32_t max_cnt,
		      daos_unit_oid_t *oid, daos_epoch_t epoch,
		      struct dtx_entry ***dtes)
{
	struct dtx_entry		**dte_buf = NULL;
	struct dtx_cos_rec_child	 *dcrc;
	uint32_t			  count;
	uint32_t			  i = 0;

	count = min(cont->sc_dtx_committable_count, max_cnt);
	if (count == 0) {
		*dtes = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(dte_buf, count);
	if (dte_buf == NULL)
		return -DER_NOMEM;

	d_list_for_each_entry(dcrc, &cont->sc_dtx_cos_list,
			      dcrc_gl_committable) {
		if (oid != NULL &&
		    daos_unit_oid_compare(dcrc->dcrc_ptr->dcr_oid, *oid) != 0)
			continue;

		if (epoch < dcrc->dcrc_epoch)
			continue;

		dte_buf[i] = dcrc->dcrc_dte;
		if (++i >= count)
			break;
	}

	if (i == 0) {
		D_FREE(dte_buf);
		*dtes = NULL;
	} else {
		*dtes = dte_buf;
	}

	return i;
}

int
dtx_list_cos(struct ds_cont_child *cont, daos_unit_oid_t *oid,
	     uint64_t dkey_hash, int max, struct dtx_id **dtis)
{
	struct dtx_cos_key		 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_id			*dti = NULL;
	struct dtx_cos_rec		*dcr = NULL;
	struct dtx_cos_rec_child	*dcrc;
	int				 count;
	int				 rc;
	int				 i = 0;

	key.oid = *oid;
	key.dkey_hash = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->sc_dtx_cos_hdl, &kiov, &riov);
	if (rc != 0)
		return rc == -DER_NONEXIST ? 0 : rc;

	dcr = (struct dtx_cos_rec *)riov.iov_buf;
	if (dcr->dcr_prio_count == 0)
		return 0;

	/* There are too many priority DTXs to be committed, as to cannot be
	 * piggybacked via normal dispatched RPC. Return the specified @max
	 * DTXs. If some DTX in the left part caused current modification
	 * failure (conflict), related RPC will be retried sometime later.
	 */
	if (dcr->dcr_prio_count > max)
		count = max;
	else
		count = dcr->dcr_prio_count;

	D_ALLOC_ARRAY(dti, count);
	if (dti == NULL)
		return -DER_NOMEM;

	d_list_for_each_entry(dcrc, &dcr->dcr_prio_list, dcrc_lo_link)
		dti[i++] = dcrc->dcrc_dte->dte_xid;

	D_ASSERT(i == count);
	*dtis = dti;

	return count;
}

int
dtx_add_cos(struct ds_cont_child *cont, struct dtx_entry *dte,
	    daos_unit_oid_t *oid, uint64_t dkey_hash,
	    daos_epoch_t epoch, uint32_t flags)
{
	struct dtx_cos_key		key;
	struct dtx_cos_rec_bundle	rbund;
	d_iov_t				kiov;
	d_iov_t				riov;
	int				rc;

	D_ASSERT(dte->dte_mbs != NULL);
	D_ASSERT(epoch != DAOS_EPOCH_MAX);

	key.oid = *oid;
	key.dkey_hash = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));

	rbund.dte = dte;
	rbund.epoch = epoch;
	rbund.flags = flags;
	d_iov_set(&riov, &rbund, sizeof(rbund));

	rc = dbtree_upsert(cont->sc_dtx_cos_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);

	D_CDEBUG(rc != 0, DLOG_ERR, DB_IO, "Insert DTX "DF_DTI" to CoS "
		 "cache, "DF_UOID", key %lu, flags %x: rc = "DF_RC"\n",
		 DP_DTI(&dte->dte_xid), DP_UOID(*oid), (unsigned long)dkey_hash,
		 flags, DP_RC(rc));

	return rc;
}

int
dtx_del_cos(struct ds_cont_child *cont, struct dtx_id *xid,
	    daos_unit_oid_t *oid, uint64_t dkey_hash)
{
	struct dtx_cos_key		 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;
	int				 found = 0;
	int				 rc;

	key.oid = *oid;
	key.dkey_hash = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->sc_dtx_cos_hdl, &kiov, &riov);
	if (rc != 0)
		goto out;

	dcr = (struct dtx_cos_rec *)riov.iov_buf;

	d_list_for_each_entry(dcrc, &dcr->dcr_prio_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dte->dte_xid, xid, sizeof(*xid)) != 0)
			continue;

		d_list_del(&dcrc->dcrc_gl_committable);
		d_list_del(&dcrc->dcrc_lo_link);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);

		cont->sc_dtx_committable_count--;
		dcr->dcr_prio_count--;

		D_GOTO(out, found = 1);
	}

	d_list_for_each_entry(dcrc, &dcr->dcr_reg_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dte->dte_xid, xid, sizeof(*xid)) != 0)
			continue;

		d_list_del(&dcrc->dcrc_gl_committable);
		d_list_del(&dcrc->dcrc_lo_link);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);

		cont->sc_dtx_committable_count--;
		dcr->dcr_reg_count--;

		D_GOTO(out, found = 2);
	}

	d_list_for_each_entry(dcrc, &dcr->dcr_expcmt_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dte->dte_xid, xid, sizeof(*xid)) != 0)
			continue;

		d_list_del(&dcrc->dcrc_gl_committable);
		d_list_del(&dcrc->dcrc_lo_link);
		dtx_entry_put(dcrc->dcrc_dte);
		D_FREE_PTR(dcrc);

		cont->sc_dtx_committable_count--;
		dcr->dcr_expcmt_count--;

		D_GOTO(out, found = 3);
	}

out:
	if (found > 0 && dcr->dcr_reg_count == 0 && dcr->dcr_prio_count == 0 &&
	    dcr->dcr_expcmt_count == 0)
		rc = dbtree_delete(cont->sc_dtx_cos_hdl, BTR_PROBE_EQ,
				   &kiov, NULL);

	D_CDEBUG(rc != 0, DLOG_ERR, DB_IO, "Remove DTX "DF_DTI" from CoS "
		 "cache, "DF_UOID", key %lu, %s shared entry: rc = "DF_RC"\n",
		 DP_DTI(xid), DP_UOID(*oid), (unsigned long)dkey_hash,
		 found == 1 ? "has" : "has not", DP_RC(rc));

	return rc == -DER_NONEXIST ? 0 : rc;
}

uint64_t
dtx_cos_oldest(struct ds_cont_child *cont)
{
	struct dtx_cos_rec_child	*dcrc;

	if (d_list_empty(&cont->sc_dtx_cos_list))
		return 0;

	dcrc = d_list_entry(cont->sc_dtx_cos_list.next,
			    struct dtx_cos_rec_child, dcrc_gl_committable);

	return dcrc->dcrc_epoch;
}
