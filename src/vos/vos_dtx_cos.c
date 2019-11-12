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
 * vos/vos_dtx_cos.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include "vos_layout.h"
#include "vos_internal.h"

struct dtx_cos_rec_bundle {
	struct vos_container	*crb_cont;
	daos_epoch_t		 crb_time;
	daos_unit_oid_t		 crb_oid;
};

/* The record for the DTX CoS B+tree in DRAM. Each record contains current
 * committable DTXs that modify (update or punch) something under the same
 * object and the same dkey.
 */
struct dtx_cos_rec {
	/* Pointer to the container. */
	struct vos_container	*dcr_cont;
	/* Link in committable or priority list */
	d_list_t		 dcr_link;
	/* Epoch of dtx */
	daos_epoch_t		 dcr_time;
	/* Oid of dtx.  Assume 1 per dtx for now */
	daos_unit_oid_t		 dcr_oid;
	/* cache of xid */
	struct dtx_id		 dcr_id;
	/* The number of times this record has been checked. */
	int			 dcr_check_count;
};

static int
dtx_cos_hkey_size(void)
{
	return sizeof(struct dtx_id);
}

static void
dtx_cos_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct dtx_id));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtx_cos_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct dtx_id *hkey1 = (struct dtx_id *)&rec->rec_hkey[0];
	struct dtx_id *hkey2 = (struct dtx_id *)hkey;
	int		    rc;

	rc = memcmp(hkey1, hkey2, sizeof(struct dtx_id));

	return dbtree_key_cmp_rc(rc);
}

static int
dtx_cos_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		  d_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_id			*xid;
	struct dtx_cos_rec_bundle	*dcrb;
	struct dtx_cos_rec		*dcr;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcrb = (struct dtx_cos_rec_bundle *)val_iov->iov_buf;
	xid = (struct dtx_id *)key_iov->iov_buf;

	D_ALLOC_PTR(dcr);
	if (dcr == NULL)
		return -DER_NOMEM;

	dcr->dcr_cont = dcrb->crb_cont;
	dcr->dcr_oid = dcrb->crb_oid;
	dcr->dcr_time = dcrb->crb_time;
	dcr->dcr_id = *xid;

	d_list_add_tail(&dcr->dcr_link,
			&dcrb->crb_cont->vc_dtx_committable);
	dcrb->crb_cont->vc_dtx_committable_count++;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, dcr);

	return 0;
}

static int
dtx_cos_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct dtx_cos_rec		*dcr;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	dcr = (struct dtx_cos_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);

	d_list_del(&dcr->dcr_link);
	if (dcr->dcr_check_count > DTX_CHECK_THRESHOLD)
		dcr->dcr_cont->vc_dtx_priority_count--;
	else
		dcr->dcr_cont->vc_dtx_committable_count--;
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

static btr_ops_t dtx_btr_cos_ops = {
	.to_hkey_size	= dtx_cos_hkey_size,
	.to_hkey_gen	= dtx_cos_hkey_gen,
	.to_hkey_cmp	= dtx_cos_hkey_cmp,
	.to_rec_alloc	= dtx_cos_rec_alloc,
	.to_rec_free	= dtx_cos_rec_free,
	.to_rec_fetch	= dtx_cos_rec_fetch,
};

int
vos_dtx_cos_register(void)
{
	int	rc;

	D_DEBUG(DB_DF, "Registering DTX CoS class: %d\n",
		VOS_BTR_DTX_COS);

	rc = dbtree_class_register(VOS_BTR_DTX_COS, 0, &dtx_btr_cos_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX CoS dbtree: rc = %d\n", rc);

	return rc;
}

int
vos_dtx_add_cos(daos_handle_t coh, daos_unit_oid_t *oid, struct dtx_id *dti,
		daos_epoch_t epoch, bool check)
{
	struct vos_container		*cont;
	struct dtx_id			 key;
	struct dtx_cos_rec_bundle	 rbund;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	if (check) {
		struct daos_lru_cache   *occ = vos_obj_cache_current();
		struct vos_object       *obj = NULL;
		daos_epoch_range_t       epr = {0, epoch};

		/* Sync epoch check inside vos_obj_hold(). We do not
		* care about whether it is for punch or update , so
		* use DAOS_INTENT_COS to bypass DTX conflict check.
		*/
		rc = vos_obj_hold(occ, cont, *oid, &epr, true, DAOS_INTENT_COS,
				  true, &obj);
		if (rc != 0)
			return rc;

		vos_obj_release(occ, obj);
	}

	D_ASSERT(epoch != DAOS_EPOCH_MAX);

	key = *dti;
	d_iov_set(&kiov, &key, sizeof(key));

	rbund.crb_cont = cont;
	rbund.crb_oid = *oid;
	rbund.crb_time = epoch;
	d_iov_set(&riov, &rbund, sizeof(rbund));

	rc = dbtree_upsert(cont->vc_dtx_cos_hdl, BTR_PROBE_EQ,
			   DAOS_INTENT_UPDATE, &kiov, &riov);

	D_DEBUG(DB_TRACE, "Insert DTX "DF_DTI" to CoS cache rc = %d\n",
		DP_DTI(dti), rc);

	return rc;
}

int
vos_dtx_lookup_cos(daos_handle_t coh, struct dtx_id *xid)
{
	struct vos_container		*cont;
	struct dtx_id			 key;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	struct dtx_cos_rec		*dcr;
	int				 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	key = *xid;
	d_iov_set(&kiov, &key, sizeof(key));
	d_iov_set(&riov, NULL, 0);

	rc = dbtree_lookup(cont->vc_dtx_cos_hdl, &kiov, &riov);
	if (rc != 0)
		return rc;

	dcr = (struct dtx_cos_rec *)riov.iov_buf;
	dcr->dcr_check_count++;

	if (dcr->dcr_check_count > DTX_CHECK_THRESHOLD) {
		d_list_del(&dcr->dcr_link);
		d_list_add_tail(&dcr->dcr_link, &cont->vc_dtx_priority);
		cont->vc_dtx_committable_count--;
		cont->vc_dtx_priority_count++;
	}

	return 0;
}

static void
fetch_committable(d_list_t *list, struct dtx_entry *dte, daos_unit_oid_t *oid,
		  daos_epoch_t epoch, uint32_t *cursor, uint32_t count)
{
	struct dtx_cos_rec	*dcr;

	if (*cursor >= count)
		return;

	d_list_for_each_entry(dcr, list, dcr_link) {
		if (oid != NULL &&
		    daos_unit_oid_compare(dcr->dcr_oid, *oid) != 0)
			continue;

		if (epoch < dcr->dcr_time)
			continue;

		dte[*cursor].dte_xid = dcr->dcr_id;
		dte[*cursor].dte_oid = dcr->dcr_oid;

		(*cursor)++;
		if (*cursor >= count)
			break;
	}
}

int
vos_dtx_fetch_committable(daos_handle_t coh, uint32_t max_cnt,
			  daos_unit_oid_t *oid, daos_epoch_t epoch,
			  struct dtx_entry **dtes)
{
	struct dtx_entry	*dte;
	struct vos_container	*cont;
	uint32_t		 count;
	uint32_t		 committable;
	uint32_t		 i = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	committable = cont->vc_dtx_committable_count;

	if (oid == NULL) {
		if (committable < DTX_THRESHOLD_COUNT)
			committable = 0;
	}

	count = min(committable + cont->vc_dtx_priority_count, max_cnt);
	if (count == 0) {
		*dtes = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(dte, count);
	if (dte == NULL)
		return -DER_NOMEM;

	fetch_committable(&cont->vc_dtx_priority, dte, oid, epoch, &i, count);
	if (committable)
		fetch_committable(&cont->vc_dtx_committable, dte, oid, epoch,
				  &i, count);

	if (i == 0) {
		D_FREE(dte);
		*dtes = NULL;
	} else {
		*dtes = dte;
	}

	return i;
}


void
vos_dtx_del_cos(struct vos_container *cont, struct dtx_id *xid)
{
	struct dtx_id		 key;
	d_iov_t				 kiov;

	key = *xid;
	d_iov_set(&kiov, &key, sizeof(key));

	dbtree_delete(cont->vc_dtx_cos_hdl, BTR_PROBE_EQ, &kiov, NULL);
}

uint64_t
vos_dtx_cos_oldest(struct vos_container *cont)
{
	struct dtx_cos_rec	*dcr;

	if (d_list_empty(&cont->vc_dtx_committable))
		return 0;

	dcr = d_list_entry(cont->vc_dtx_committable.next,
			   struct dtx_cos_rec, dcr_link);

	return dcr->dcr_time;
}

