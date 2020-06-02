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
 * vos/vos_dtx_cos.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include "vos_layout.h"
#include "vos_internal.h"

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
	/* The number of the PUNCH DTXs in the dcr_reg_list. */
	int			 dcr_reg_count;
	/* The number of the DTXs in the dcr_prio_list. */
	int			 dcr_prio_count;
};

/* Above dtx_cos_rec is consisted of a series of dtx_cos_rec_child uints.
 * Each dtx_cos_rec_child contains one DTX that modifies something under
 * related object and dkey (that attached to the dtx_cos_rec).
 */
struct dtx_cos_rec_child {
	/* Link into the container::vc_dtx_committable_list. */
	d_list_t		 dcrc_gl_committable;
	/* Link into related dcr_{reg,prio}_list. */
	d_list_t		 dcrc_lo_link;
	/* The DTX identifier. */
	struct dtx_id		 dcrc_dti;
	/* The DTX epoch. */
	daos_epoch_t		 dcrc_epoch;
	/* Pointer to the dtx_cos_rec. */
	struct dtx_cos_rec	*dcrc_ptr;
};

struct dtx_cos_rec_bundle {
	struct dtx_id		*dti;
	daos_epoch_t		 epoch;
	uint32_t		 flags;
};

struct dtx_cos_key {
	daos_unit_oid_t		 oid;
	uint64_t		 dkey;
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
	struct vos_container		*cont = tins->ti_priv;
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

	D_ALLOC_PTR(dcrc);
	if (dcrc == NULL) {
		D_FREE_PTR(dcr);
		return -DER_NOMEM;
	}

	dcrc->dcrc_dti = *rbund->dti;
	dcrc->dcrc_epoch = rbund->epoch;
	dcrc->dcrc_ptr = dcr;

	d_list_add_tail(&dcrc->dcrc_gl_committable,
			&cont->vc_dtx_committable_list);
	cont->vc_dtx_committable_count++;

	if (rbund->flags & DCF_SHARED) {
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
	struct vos_container		*cont = tins->ti_priv;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;
	struct dtx_cos_rec_child	*next;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_list_for_each_entry_safe(dcrc, next, &dcr->dcr_reg_list,
				   dcrc_lo_link) {
		d_list_del(&dcrc->dcrc_lo_link);
		d_list_del(&dcrc->dcrc_gl_committable);
		D_FREE_PTR(dcrc);
		cont->vc_dtx_committable_count--;
	}
	d_list_for_each_entry_safe(dcrc, next, &dcr->dcr_prio_list,
				   dcrc_lo_link) {
		d_list_del(&dcrc->dcrc_lo_link);
		d_list_del(&dcrc->dcrc_gl_committable);
		D_FREE_PTR(dcrc);
		cont->vc_dtx_committable_count--;
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
	struct vos_container		*cont = tins->ti_priv;
	struct dtx_cos_rec_bundle	*rbund;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rbund = (struct dtx_cos_rec_bundle *)val->iov_buf;

	D_ALLOC_PTR(dcrc);
	if (dcrc == NULL)
		return -DER_NOMEM;

	dcrc->dcrc_dti = *rbund->dti;
	dcrc->dcrc_epoch = rbund->epoch;
	dcrc->dcrc_ptr = dcr;

	d_list_add_tail(&dcrc->dcrc_gl_committable,
			&cont->vc_dtx_committable_list);
	cont->vc_dtx_committable_count++;

	if (rbund->flags & DCF_SHARED) {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_prio_list);
		dcr->dcr_prio_count++;
	} else {
		d_list_add_tail(&dcrc->dcrc_lo_link, &dcr->dcr_reg_list);
		dcr->dcr_reg_count++;
	}

	return 0;
}

static btr_ops_t dtx_btr_cos_ops = {
	.to_hkey_size	= dtx_cos_hkey_size,
	.to_hkey_gen	= dtx_cos_hkey_gen,
	.to_hkey_cmp	= dtx_cos_hkey_cmp,
	.to_rec_alloc	= dtx_cos_rec_alloc,
	.to_rec_free	= dtx_cos_rec_free,
	.to_rec_fetch	= dtx_cos_rec_fetch,
	.to_rec_update	= dtx_cos_rec_update,
};

int
vos_dtx_cos_register(void)
{
	int	rc;

	D_DEBUG(DB_DF, "Registering DTX CoS class: %d\n",
		VOS_BTR_DTX_COS);

	rc = dbtree_class_register(VOS_BTR_DTX_COS, 0, &dtx_btr_cos_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX CoS dbtree: rc = "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

int
vos_dtx_list_cos(daos_handle_t coh, daos_unit_oid_t *oid, uint64_t dkey_hash,
		 int max, struct dtx_id **dtis)
{
	struct vos_container		*cont;
	struct dtx_cos_key		 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_id			*dti = NULL;
	struct dtx_cos_rec		*dcr = NULL;
	struct dtx_cos_rec_child	*dcrc;
	int				 count;
	int				 rc;
	int				 i = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	key.oid = *oid;
	key.dkey = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->vc_dtx_cos_hdl, &kiov, &riov);
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
		dti[i++] = dcrc->dcrc_dti;

	D_ASSERT(i == count);
	*dtis = dti;

	return count;
}

int
vos_dtx_add_cos(daos_handle_t coh, daos_unit_oid_t *oid, struct dtx_id *dti,
		uint64_t dkey_hash, daos_epoch_t epoch, uint64_t gen,
		uint32_t flags)
{
	struct vos_container		*cont;
	struct dtx_cos_key		 key;
	struct dtx_cos_rec_bundle	 rbund;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	/* If the DTX is started befoe DTX resync operation (for rebuild),
	 * then it is possbile that the DTX resync ULT may have aborted
	 * or committed the DTX during current ULT waiting for the reply.
	 * Let's check DTX status locally before marking as 'committable'.
	 */
	if (gen == 0)
		goto add;

	if (gen < cont->vc_dtx_resync_gen) {
		rc = vos_dtx_check(coh, dti);
		switch (rc) {
		case DTX_ST_PREPARED:
			rc = vos_dtx_lookup_cos(coh, oid, dti, dkey_hash);
			/* The resync ULT has already added it into the
			 * CoS cache, current ULT needs to do nothing.
			 */
			if (rc == 0)
				return 0;

			/* Normal case, then add it to CoS cache. */
			if (rc == -DER_NONEXIST)
				break;

			return rc >= 0 ? -DER_INVAL : rc;
		case DTX_ST_COMMITTED:
			/* The DTX has been committed by resync ULT by race. */
			return 0;
		case -DER_NONEXIST:
			/* The DTX has been aborted by resync ULT, ask the
			 * client to retry.
			 */
			return -DER_TX_RESTART;
		default:
			return rc >= 0 ? -DER_INVAL : rc;
		}
	}

add:
	D_ASSERT(epoch != DAOS_EPOCH_MAX);

	key.oid = *oid;
	key.dkey = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));

	rbund.dti = dti;
	rbund.epoch = epoch;
	rbund.flags = flags;
	d_iov_set(&riov, &rbund, sizeof(rbund));

	rc = dbtree_upsert(cont->vc_dtx_cos_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);

	D_DEBUG(DB_IO, "Insert DTX "DF_DTI" to CoS cache, key %llu, "
		"%s shared entry: rc = "DF_RC"\n",
		DP_DTI(dti), (unsigned long long)dkey_hash,
		flags & DCF_SHARED ? "has" : "has not", DP_RC(rc));

	return rc;
}

int
vos_dtx_lookup_cos(daos_handle_t coh, daos_unit_oid_t *oid,
		   struct dtx_id *xid, uint64_t dkey_hash)
{
	struct vos_container		*cont;
	struct dtx_cos_key		 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;
	int				 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	key.oid = *oid;
	key.dkey = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->vc_dtx_cos_hdl, &kiov, &riov);
	if (rc != 0)
		return rc;

	dcr = (struct dtx_cos_rec *)riov.iov_buf;

	d_list_for_each_entry(dcrc, &dcr->dcr_prio_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dti, xid, sizeof(*xid)) == 0)
			return 0;
	}

	d_list_for_each_entry(dcrc, &dcr->dcr_reg_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dti, xid, sizeof(*xid)) == 0)
			return 0;
	}

	return -DER_NONEXIST;
}

int
vos_dtx_fetch_committable(daos_handle_t coh, uint32_t max_cnt,
			  daos_unit_oid_t *oid, daos_epoch_t epoch,
			  struct dtx_entry **dtes)
{
	struct dtx_entry		*dte = NULL;
	struct dtx_cos_rec_child	*dcrc;
	struct vos_container		*cont;
	uint32_t			 count;
	uint32_t			 i = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	count = min(cont->vc_dtx_committable_count, max_cnt);
	if (count == 0) {
		*dtes = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(dte, count);
	if (dte == NULL)
		return -DER_NOMEM;

	d_list_for_each_entry(dcrc, &cont->vc_dtx_committable_list,
			      dcrc_gl_committable) {
		if (oid != NULL &&
		    daos_unit_oid_compare(dcrc->dcrc_ptr->dcr_oid, *oid) != 0)
			continue;

		if (epoch < dcrc->dcrc_epoch)
			continue;

		dte[i].dte_xid = dcrc->dcrc_dti;
		dte[i].dte_oid = dcrc->dcrc_ptr->dcr_oid;

		if (++i >= count)
			break;
	}

	if (i == 0) {
		D_FREE(dte);
		*dtes = NULL;
	} else {
		*dtes = dte;
	}

	return min(count, i);
}

int
vos_dtx_del_cos(struct vos_container *cont, daos_unit_oid_t *oid,
		struct dtx_id *xid, uint64_t dkey_hash)
{
	struct dtx_cos_key		 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_cos_rec		*dcr;
	struct dtx_cos_rec_child	*dcrc;
	int				 found = 0;
	int				 rc;

	key.oid = *oid;
	key.dkey = dkey_hash;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->vc_dtx_cos_hdl, &kiov, &riov);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			return 0;

		D_ERROR("Fail to remove "DF_DTI" from CoS cache: %d\n",
			DP_DTI(xid), rc);

		return rc;
	}

	dcr = (struct dtx_cos_rec *)riov.iov_buf;

	d_list_for_each_entry(dcrc, &dcr->dcr_prio_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dti, xid, sizeof(*xid)) != 0)
			continue;

		d_list_del(&dcrc->dcrc_gl_committable);
		d_list_del(&dcrc->dcrc_lo_link);
		D_FREE_PTR(dcrc);

		cont->vc_dtx_committable_count--;
		dcr->dcr_prio_count--;

		D_GOTO(out, found = 1);
	}

	d_list_for_each_entry(dcrc, &dcr->dcr_reg_list, dcrc_lo_link) {
		if (memcmp(&dcrc->dcrc_dti, xid, sizeof(*xid)) != 0)
			continue;

		d_list_del(&dcrc->dcrc_gl_committable);
		d_list_del(&dcrc->dcrc_lo_link);
		D_FREE_PTR(dcrc);

		cont->vc_dtx_committable_count--;
		dcr->dcr_reg_count--;

		D_GOTO(out, found = 2);
	}

out:
	if (found > 0) {
		if (dcr->dcr_reg_count == 0 && dcr->dcr_prio_count == 0)
			rc = dbtree_delete(cont->vc_dtx_cos_hdl, BTR_PROBE_EQ,
					   &kiov, NULL);

		D_CDEBUG(rc != 0, DLOG_ERR, DB_IO, "Remove DTX "DF_DTI" from "
			 "CoS cache, key %lu, %s shared entry: rc = "DF_RC"\n",
			 DP_DTI(xid), (unsigned long)dkey_hash,
			 found == 1 ? "has" : "has not", DP_RC(rc));
	}

	return rc;
}

uint64_t
vos_dtx_cos_oldest(struct vos_container *cont)
{
	struct dtx_cos_rec_child	*dcrc;

	if (d_list_empty(&cont->vc_dtx_committable_list))
		return 0;

	dcrc = d_list_entry(cont->vc_dtx_committable_list.next,
			    struct dtx_cos_rec_child, dcrc_gl_committable);
	return dcrc->dcrc_epoch;
}
