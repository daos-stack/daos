/**
 * (C) Copyright 2018-2019 Intel Corporation.
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
 * This file is part of daos
 *
 * vos/vos_io.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"
#include "evt_priv.h"

/** I/O context */
struct vos_io_context {
	daos_epoch_t		 ic_epoch;
	/** number DAOS IO descriptors */
	unsigned int		 ic_iod_nr;
	daos_iod_t		*ic_iods;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** BIO descriptor, has ic_iod_nr SGLs */
	struct bio_desc		*ic_biod;
	/** cursor of SGL & IOV in BIO descriptor */
	unsigned int		 ic_sgl_at;
	unsigned int		 ic_iov_at;
	/** reserved SCM extents */
	unsigned int		 ic_actv_cnt;
	unsigned int		 ic_actv_at;
	struct pobj_action	*ic_actv;
	/** reserved mmids for SCM update */
	umem_id_t		*ic_mmids;
	unsigned int		 ic_mmids_cnt;
	unsigned int		 ic_mmids_at;
	/** reserved NVMe extents */
	d_list_t		 ic_blk_exts;
	/** flags */
	unsigned int		 ic_update:1,
				 ic_size_fetch:1;
};

static struct vos_io_context *
vos_ioh2ioc(daos_handle_t ioh)
{
	return (struct vos_io_context *)ioh.cookie;
}

static daos_handle_t
vos_ioc2ioh(struct vos_io_context *ioc)
{
	daos_handle_t ioh;

	ioh.cookie = (uint64_t)ioc;
	return ioh;
}

static void
iod_empty_sgl(struct vos_io_context *ioc, unsigned int sgl_at)
{
	struct bio_sglist *bsgl;

	D_ASSERT(sgl_at < ioc->ic_iod_nr);
	ioc->ic_iods[sgl_at].iod_size = 0;
	bsgl = bio_iod_sgl(ioc->ic_biod, sgl_at);
	bsgl->bs_nr_out = 0;
}

static void
vos_ioc_reserve_fini(struct vos_io_context *ioc)
{
	D_ASSERT(d_list_empty(&ioc->ic_blk_exts));
	D_ASSERT(ioc->ic_actv_at == 0);

	if (ioc->ic_actv_cnt != 0) {
		D_FREE(ioc->ic_actv);
		ioc->ic_actv = NULL;
	}

	if (ioc->ic_mmids != NULL) {
		D_FREE(ioc->ic_mmids);
		ioc->ic_mmids = NULL;
	}
}

static int
vos_ioc_reserve_init(struct vos_io_context *ioc)
{
	int i, total_acts = 0;

	ioc->ic_actv = NULL;
	ioc->ic_actv_cnt = ioc->ic_actv_at = 0;
	ioc->ic_mmids_cnt = ioc->ic_mmids_at = 0;
	D_INIT_LIST_HEAD(&ioc->ic_blk_exts);

	if (!ioc->ic_update)
		return 0;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		daos_iod_t *iod = &ioc->ic_iods[i];

		total_acts += iod->iod_nr;
	}

	D_ALLOC_ARRAY(ioc->ic_mmids, total_acts);
	if (ioc->ic_mmids == NULL)
		return -DER_NOMEM;

	if (vos_obj2umm(ioc->ic_obj)->umm_ops->mo_reserve == NULL)
		return 0;

	D_ALLOC_ARRAY(ioc->ic_actv, total_acts);
	if (ioc->ic_actv == NULL)
		return -DER_NOMEM;

	ioc->ic_actv_cnt = total_acts;
	return 0;
}

static void
vos_ioc_destroy(struct vos_io_context *ioc)
{
	if (ioc->ic_biod != NULL)
		bio_iod_free(ioc->ic_biod);

	if (ioc->ic_obj)
		vos_obj_release(vos_obj_cache_current(), ioc->ic_obj);

	vos_ioc_reserve_fini(ioc);
	D_FREE(ioc);
}

static int
vos_ioc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr, daos_iod_t *iods,
	       bool size_fetch, struct vos_io_context **ioc_pp)
{
	struct vos_io_context *ioc;
	struct bio_io_context *bioc;
	int i, rc;

	D_ALLOC_PTR(ioc);
	if (ioc == NULL)
		return -DER_NOMEM;

	ioc->ic_iod_nr = iod_nr;
	ioc->ic_iods = iods;
	ioc->ic_epoch = epoch;
	ioc->ic_update = !read_only;
	ioc->ic_size_fetch = size_fetch;

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, read_only,
			  read_only ? DAOS_INTENT_DEFAULT : DAOS_INTENT_UPDATE,
			  &ioc->ic_obj);
	if (rc != 0)
		goto error;

	rc = vos_ioc_reserve_init(ioc);
	if (rc != 0)
		goto error;

	bioc = ioc->ic_obj->obj_cont->vc_pool->vp_io_ctxt;
	D_ASSERT(bioc != NULL);
	ioc->ic_biod = bio_iod_alloc(bioc, iod_nr, !read_only);
	if (ioc->ic_biod == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	for (i = 0; i < iod_nr; i++) {
		int iov_nr = iods[i].iod_nr;
		struct bio_sglist *bsgl;

		if (iods[i].iod_type == DAOS_IOD_SINGLE && iov_nr != 1) {
			D_ERROR("Invalid sv iod_nr=%d\n", iov_nr);
			rc = -DER_IO_INVAL;
			goto error;
		}

		/* Don't bother to initialize SGLs for size fetch */
		if (ioc->ic_size_fetch)
			continue;

		bsgl = bio_iod_sgl(ioc->ic_biod, i);
		rc = bio_sgl_init(bsgl, iov_nr);
		if (rc != 0)
			goto error;
	}

	*ioc_pp = ioc;
	return 0;
error:
	vos_ioc_destroy(ioc);
	return rc;
}

static int
iod_fetch(struct vos_io_context *ioc, struct bio_iov *biov)
{
	struct bio_sglist *bsgl;
	int iov_nr, iov_at;

	if (ioc->ic_size_fetch)
		return 0;

	bsgl = bio_iod_sgl(ioc->ic_biod, ioc->ic_sgl_at);
	D_ASSERT(bsgl != NULL);
	iov_nr = bsgl->bs_nr;
	iov_at = ioc->ic_iov_at;

	D_ASSERT(iov_nr > iov_at);
	D_ASSERT(iov_nr >= bsgl->bs_nr_out);

	if (iov_at == iov_nr - 1) {
		struct bio_iov *biovs;

		D_ALLOC_ARRAY(biovs, (iov_nr * 2));
		if (biovs == NULL)
			return -DER_NOMEM;

		memcpy(biovs, &bsgl->bs_iovs[0], iov_nr * sizeof(*biovs));
		D_FREE(bsgl->bs_iovs);

		bsgl->bs_iovs = biovs;
		bsgl->bs_nr = iov_nr * 2;
	}

	bsgl->bs_iovs[iov_at] = *biov;
	bsgl->bs_nr_out++;
	ioc->ic_iov_at++;
	return 0;
}

/** Fetch the single value within the specified epoch range of an key */
static int
akey_fetch_single(daos_handle_t toh, daos_epoch_t epoch,
		  daos_size_t *rsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov; /* iov to carry key bundle */
	daos_iov_t		 riov; /* iov to carray record bundle */
	struct bio_iov		 biov; /* iov to return data buffer */
	int			 rc;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= epoch;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_biov	= &biov;
	rbund.rb_csum	= &iod->iod_csums[0];
	memset(&biov, 0, sizeof(biov));

	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &kiov, &kiov,
			  &riov);
	if (rc == -DER_NONEXIST) {
		rbund.rb_rsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
		rc = 0;
	} else if (rc != 0) {
		goto out;
	}

	rc = iod_fetch(ioc, &biov);
	if (rc != 0)
		goto out;

	*rsize = rbund.rb_rsize;
out:
	return rc;
}

static inline void
biov_set_hole(struct bio_iov *biov, ssize_t len)
{
	memset(biov, 0, sizeof(*biov));
	biov->bi_data_len = len;
	bio_addr_set_hole(&biov->bi_addr, 1);
}

/** Fetch an extent from an akey */
static int
akey_fetch_recx(daos_handle_t toh, daos_epoch_t epoch, daos_recx_t *recx,
		daos_size_t *rsize_p, struct vos_io_context *ioc)
{
	struct evt_entry	*ent;
	/* At present, this is not exposed in interface but passing it toggles
	 * sorting and clipping of rectangles
	 */
	struct evt_entry_array	 ent_array = { 0 };
	struct evt_rect		 rect;
	struct bio_iov		 biov = {0};
	daos_size_t		 holes; /* hole width */
	daos_size_t		 rsize;
	daos_off_t		 index;
	daos_off_t		 end;
	int			 rc;

	index = recx->rx_idx;
	end   = recx->rx_idx + recx->rx_nr;

	rect.rc_ex.ex_lo = index;
	rect.rc_ex.ex_hi = end - 1;
	rect.rc_epc = epoch;

	evt_ent_array_init(&ent_array);
	rc = evt_find(toh, &rect, &ent_array);
	if (rc != 0)
		goto failed;

	holes = 0;
	rsize = 0;
	evt_ent_array_for_each(ent, &ent_array) {
		daos_off_t	 lo = ent->en_sel_ext.ex_lo;
		daos_off_t	 hi = ent->en_sel_ext.ex_hi;
		daos_size_t	 nr;

		D_ASSERT(hi >= lo);
		nr = hi - lo + 1;

		if (lo != index) {
			D_ASSERTF(lo > index,
				  DF_U64"/"DF_U64", "DF_RECT", "DF_ENT"\n",
				  lo, index, DP_RECT(&rect),
				  DP_ENT(ent));
			holes += lo - index;
		}

		if (bio_addr_is_hole(&ent->en_addr)) { /* hole extent */
			index = lo + nr;
			holes += nr;
			continue;
		}

		if (holes != 0) {
			biov_set_hole(&biov, holes * ent_array.ea_inob);
			/* skip the hole */
			rc = iod_fetch(ioc, &biov);
			if (rc != 0)
				goto failed;
			holes = 0;
		}

		if (rsize == 0)
			rsize = ent_array.ea_inob;
		D_ASSERT(rsize == ent_array.ea_inob);

		biov.bi_data_len = nr * ent_array.ea_inob;
		biov.bi_addr = ent->en_addr;
		rc = iod_fetch(ioc, &biov);
		if (rc != 0)
			goto failed;

		index = lo + nr;
	}

	D_ASSERT(index <= end);
	if (index < end)
		holes += end - index;

	if (holes != 0) { /* trailing holes */
		biov_set_hole(&biov, holes * ent_array.ea_inob);
		rc = iod_fetch(ioc, &biov);
		if (rc != 0)
			goto failed;
	}
	if (rsize_p)
		*rsize_p = rsize;
failed:
	evt_ent_array_fini(&ent_array);
	return rc;
}

/* Trim the tail holes for the current sgl */
static void
ioc_trim_tail_holes(struct vos_io_context *ioc)
{
	struct bio_sglist *bsgl;
	struct bio_iov *biov;
	int i;

	if (ioc->ic_size_fetch)
		return;

	bsgl = bio_iod_sgl(ioc->ic_biod, ioc->ic_sgl_at);
	for (i = ioc->ic_iov_at - 1; i >= 0; i--) {
		biov = &bsgl->bs_iovs[i];
		if (bio_addr_is_hole(&biov->bi_addr))
			bsgl->bs_nr_out--;
		else
			break;
	}

	if (bsgl->bs_nr_out == 0)
		iod_empty_sgl(ioc, ioc->ic_sgl_at);
}

static int
akey_fetch(struct vos_io_context *ioc, daos_handle_t ak_toh)
{
	daos_iod_t	*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	daos_epoch_t	 epoch = ioc->ic_epoch;
	struct vos_krec_df *krec = NULL;
	daos_handle_t	 toh = DAOS_HDL_INVAL;
	int		 i, rc;
	int		 flags = 0;

	D_DEBUG(DB_IO, "akey %d %s fetch %s eph "DF_U64"\n",
		(int)iod->iod_name.iov_len, (char *)iod->iod_name.iov_buf,
		iod->iod_type == DAOS_IOD_ARRAY ? "array" : "single",
		ioc->ic_epoch);

	if (iod->iod_type == DAOS_IOD_ARRAY)
		flags |= SUBTR_EVT;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		if (iod->iod_eprs && iod->iod_eprs[0].epr_lo != 0)
			epoch = iod->iod_eprs[0].epr_lo;

		rc = key_tree_prepare(ioc->ic_obj, epoch, ak_toh, VOS_BTR_AKEY,
				      &iod->iod_name, flags,
				      DAOS_INTENT_DEFAULT, NULL, &toh);
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				D_DEBUG(DB_IO, "Nonexistent akey %.*s\n",
					(int)iod->iod_name.iov_len,
					(char *)iod->iod_name.iov_buf);
				iod_empty_sgl(ioc, ioc->ic_sgl_at);
				rc = 0;
			}
			return rc;
		}

		rc = akey_fetch_single(toh, ioc->ic_epoch, &iod->iod_size, ioc);

		key_tree_release(toh, false);

		return rc;
	}

	iod->iod_size = 0;
	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t rsize;
		if (iod->iod_eprs && iod->iod_eprs[i].epr_lo)
			epoch = iod->iod_eprs[i].epr_lo;

		/* If epoch on each iod_eprs are out of boundary, then it needs
		 * to re-prepare the key tree.
		 */
		if (daos_handle_is_inval(toh) || (epoch > krec->kr_latest ||
						  epoch < krec->kr_earliest)) {
			if (!daos_handle_is_inval(toh)) {
				key_tree_release(toh, true);
				toh = DAOS_HDL_INVAL;
			}

			D_DEBUG(DB_IO, "repare the key tree for eph "DF_U64"\n",
				epoch);
			rc = key_tree_prepare(ioc->ic_obj, epoch, ak_toh,
					      VOS_BTR_AKEY, &iod->iod_name,
					      flags, DAOS_INTENT_DEFAULT,
					      &krec, &toh);
			if (rc != 0) {
				if (rc == -DER_NONEXIST) {
					D_DEBUG(DB_IO, "Nonexist akey %.*s\n",
						(int)iod->iod_name.iov_len,
						(char *)iod->iod_name.iov_buf);
					rc = 0;
					continue;
				}
				return rc;
			}
		}

		D_DEBUG(DB_IO, "fetch %d eph "DF_U64"\n", i, epoch);
		rc = akey_fetch_recx(toh, epoch, &iod->iod_recxs[i],
				     &rsize, ioc);
		if (rc != 0) {
			D_DEBUG(DB_IO, "Failed to fetch index %d: %d\n", i, rc);
			goto out;
		}

		/*
		 * Empty tree or all holes, DAOS array API relies on zero
		 * iod_size to see if an array cell is empty.
		 */
		if (rsize == 0)
			continue;

		if (iod->iod_size == 0)
			iod->iod_size = rsize;

		if (iod->iod_size != rsize) {
			D_ERROR("Cannot support mixed record size "
				DF_U64"/"DF_U64"\n", iod->iod_size, rsize);
			rc = -DER_INVAL;
			goto out;
		}
	}

	ioc_trim_tail_holes(ioc);
out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, true);
	return rc;
}

static void
iod_set_cursor(struct vos_io_context *ioc, unsigned int sgl_at)
{
	D_ASSERT(sgl_at < ioc->ic_iod_nr);
	D_ASSERT(ioc->ic_iods != NULL);

	ioc->ic_sgl_at = sgl_at;
	ioc->ic_iov_at = 0;
}

static int
dkey_fetch(struct vos_io_context *ioc, daos_key_t *dkey)
{
	struct vos_object *obj = ioc->ic_obj;
	daos_handle_t	   toh;
	int		   i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, ioc->ic_epoch, obj->obj_toh, VOS_BTR_DKEY,
			      dkey, 0, DAOS_INTENT_DEFAULT, NULL, &toh);
	if (rc == -DER_NONEXIST) {
		for (i = 0; i < ioc->ic_iod_nr; i++)
			iod_empty_sgl(ioc, i);
		D_DEBUG(DB_IO, "Nonexistent dkey\n");
		return 0;
	}

	if (rc != 0) {
		D_ERROR("Failed to prepare subtree: %d\n", rc);
		return rc;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_fetch(ioc, toh);
		if (rc != 0)
			break;
	}

	key_tree_release(toh, false);
	return rc;
}

int
vos_fetch_end(daos_handle_t ioh, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	/* NB: it's OK to use the stale ioc->ic_obj for fetch_end */
	D_ASSERT(!ioc->ic_update);
	vos_ioc_destroy(ioc);
	return err;
}

int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		bool size_fetch, daos_handle_t *ioh)
{
	struct vos_io_context *ioc;
	int i, rc;

	rc = vos_ioc_create(coh, oid, true, epoch, iod_nr, iods, size_fetch,
			    &ioc);
	if (rc != 0)
		return rc;

	if (vos_obj_is_empty(ioc->ic_obj)) {
		for (i = 0; i < iod_nr; i++)
			iod_empty_sgl(ioc, i);
	} else {
		rc = dkey_fetch(ioc, dkey);
		if (rc != 0)
			goto error;
	}

	D_DEBUG(DB_IO, "Prepared io context for fetching %d iods\n", iod_nr);
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	return vos_fetch_end(vos_ioc2ioh(ioc), rc);
}

static umem_id_t
iod_update_mmid(struct vos_io_context *ioc)
{
	umem_id_t mmid;

	D_ASSERT(ioc->ic_mmids_at < ioc->ic_mmids_cnt);
	mmid = ioc->ic_mmids[ioc->ic_mmids_at];
	ioc->ic_mmids_at++;

	return mmid;
}

static struct bio_iov *
iod_update_biov(struct vos_io_context *ioc)
{
	struct bio_sglist *bsgl;
	struct bio_iov *biov;

	bsgl = bio_iod_sgl(ioc->ic_biod, ioc->ic_sgl_at);
	D_ASSERT(bsgl->bs_nr_out != 0);
	D_ASSERT(bsgl->bs_nr_out > ioc->ic_iov_at);

	biov = &bsgl->bs_iovs[ioc->ic_iov_at];
	ioc->ic_iov_at++;

	return biov;
}

static int
akey_update_single(daos_handle_t toh, daos_epoch_t epoch, uint32_t pm_ver,
		   daos_size_t rsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_csum_buf_t		 csum;
	daos_iov_t		 kiov, riov;
	struct bio_iov		*biov;
	umem_id_t		 mmid;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	int			 rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= epoch;

	daos_csum_set(&csum, NULL, 0);

	mmid = iod_update_mmid(ioc);
	D_ASSERT(!UMMID_IS_NULL(mmid));

	D_ASSERT(ioc->ic_iov_at == 0);
	biov = iod_update_biov(ioc);

	tree_rec_bundle2iov(&rbund, &riov);
	if (iod->iod_csums)
		rbund.rb_csum	= &iod->iod_csums[0];
	else
		rbund.rb_csum	= &csum;

	rbund.rb_biov	= biov;
	rbund.rb_rsize	= rsize;
	rbund.rb_mmid	= mmid;
	rbund.rb_ver	= pm_ver;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0)
		D_ERROR("Failed to update subtree: %d\n", rc);

	return rc;
}

/**
 * Update a record extent.
 * See comment of vos_recx_fetch for explanation of @off_p.
 */
static int
akey_update_recx(daos_handle_t toh, daos_epoch_t epoch, uint32_t pm_ver,
		 daos_recx_t *recx, daos_size_t rsize,
		 struct vos_io_context *ioc)
{
	struct evt_entry_in ent;
	struct bio_iov *biov;
	int rc;

	D_ASSERT(recx->rx_nr > 0);
	ent.ei_rect.rc_epc = epoch;
	ent.ei_rect.rc_ex.ex_lo = recx->rx_idx;
	ent.ei_rect.rc_ex.ex_hi = recx->rx_idx + recx->rx_nr - 1;
	ent.ei_ver = pm_ver;
	ent.ei_inob = rsize;

	biov = iod_update_biov(ioc);
	ent.ei_addr = biov->bi_addr;
	rc = evt_insert(toh, &ent);

	return rc;
}

static void
update_bounds(daos_epoch_range_t *epr_bound,
	      const daos_epoch_range_t *new_epr)
{
	daos_epoch_t	epoch;

	D_ASSERT(epr_bound != NULL);
	D_ASSERT(new_epr != NULL);
	D_ASSERT(epr_bound->epr_hi != DAOS_EPOCH_MAX);

	epoch = new_epr->epr_lo;

	if (epoch > epr_bound->epr_hi)
		epr_bound->epr_hi = epoch;
	if (epoch < epr_bound->epr_lo)
		epr_bound->epr_lo = epoch;
}

static int
akey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_handle_t ak_toh,
	    daos_epoch_range_t *dkey_epr)
{
	struct vos_object  *obj = ioc->ic_obj;
	struct vos_krec_df *krec = NULL;
	daos_iod_t	   *iod = &ioc->ic_iods[ioc->ic_sgl_at];
	bool		    is_array = (iod->iod_type == DAOS_IOD_ARRAY);
	int		    flags = SUBTR_CREATE;
	daos_epoch_t	    epoch = ioc->ic_epoch;
	daos_epoch_range_t  akey_epr = {DAOS_EPOCH_MAX, 0};
	daos_handle_t	    toh = DAOS_HDL_INVAL;
	int		    i;
	int		    rc = 0;

	D_DEBUG(DB_TRACE, "akey %d %s update %s value eph "DF_U64"\n",
		(int)iod->iod_name.iov_len, (char *)iod->iod_name.iov_buf,
		is_array ? "array" : "single", ioc->ic_epoch);

	if (is_array)
		flags |= SUBTR_EVT;

	if (iod->iod_eprs == NULL || iod->iod_eprs[0].epr_lo == 0)
		akey_epr.epr_hi = akey_epr.epr_lo = epoch;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		if (iod->iod_eprs && iod->iod_eprs[0].epr_lo != 0) {
			epoch = iod->iod_eprs[0].epr_lo;
			update_bounds(&akey_epr, &iod->iod_eprs[0]);
		}
		rc = key_tree_prepare(obj, epoch, ak_toh, VOS_BTR_AKEY,
				      &iod->iod_name, flags, DAOS_INTENT_UPDATE,
				      &krec, &toh);
		if (rc != 0)
			return rc;

		D_DEBUG(DB_IO, "Single update eph "DF_U64"\n", epoch);
		rc = akey_update_single(toh, epoch, pm_ver, iod->iod_size, ioc);
		if (rc)
			goto failed;
		goto out;
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		if (iod->iod_eprs && iod->iod_eprs[i].epr_lo != 0) {
			update_bounds(&akey_epr, &iod->iod_eprs[i]);
			epoch = iod->iod_eprs[i].epr_lo;
		}

		if (daos_handle_is_inval(toh) ||
		    (epoch > krec->kr_latest || epoch < krec->kr_earliest)) {
			if (!daos_handle_is_inval(toh)) {
				key_tree_release(toh, is_array);
				toh = DAOS_HDL_INVAL;
			}

			/* re-prepare the tree if epoch is different */
			rc = key_tree_prepare(obj, epoch, ak_toh, VOS_BTR_AKEY,
					      &iod->iod_name, flags,
					      DAOS_INTENT_UPDATE, &krec, &toh);
			if (rc != 0)
				return rc;
		}

		D_DEBUG(DB_IO, "Array update %d eph "DF_U64"\n", i, epoch);
		rc = akey_update_recx(toh, epoch, pm_ver, &iod->iod_recxs[i],
				      iod->iod_size, ioc);
		if (rc != 0)
			goto failed;
	}
out:
	rc = vos_df_ts_update(ioc->ic_obj, &krec->kr_latest, &akey_epr);
	update_bounds(dkey_epr, &akey_epr);
failed:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, is_array);

	return rc;
}

static int
dkey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_key_t *dkey)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_obj_df	*obj_df;
	struct vos_krec_df	*krec = NULL;
	daos_epoch_range_t	 dkey_epr = {ioc->ic_epoch, ioc->ic_epoch};
	daos_handle_t		 ak_toh;
	bool			 subtr_created = false;
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);

		if (!subtr_created) {
			rc = key_tree_prepare(obj, ioc->ic_epoch, obj->obj_toh,
					      VOS_BTR_DKEY, dkey, SUBTR_CREATE,
					      DAOS_INTENT_UPDATE,
					      &krec, &ak_toh);
			if (rc != 0) {
				D_ERROR("Error preparing dkey tree: %d\n", rc);
				goto out;
			}
			subtr_created = true;
		}

		rc = akey_update(ioc, pm_ver, ak_toh, &dkey_epr);
		if (rc != 0)
			goto out;
	}

out:
	if (!subtr_created)
		return rc;

	if (rc != 0)
		goto release;

	obj_df = obj->obj_df;
	D_ASSERT(krec != NULL);
	D_ASSERT(obj_df != NULL);
	rc = vos_df_ts_update(obj, &krec->kr_latest, &dkey_epr);
	if (rc != 0)
		goto release;
	rc = vos_df_ts_update(obj, &obj_df->vo_latest, &dkey_epr);
release:
	key_tree_release(ak_toh, false);

	return rc;
}

static daos_size_t
vos_recx2irec_size(daos_size_t rsize, daos_csum_buf_t *csum)
{
	struct vos_rec_bundle	rbund;

	rbund.rb_csum	= csum;
	rbund.rb_rsize	= rsize;

	return vos_irec_size(&rbund);
}

static int
vos_reserve(struct vos_io_context *ioc, uint16_t media, daos_size_t size,
	    uint64_t *off)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vea_space_info	*vsi;
	struct vea_hint_context	*hint_ctxt;
	struct vea_resrvd_ext	*ext;
	uint32_t		 blk_cnt;
	umem_id_t		 mmid;
	int			 rc;

	if (media == DAOS_MEDIA_SCM) {
		if (ioc->ic_actv_cnt > 0) {
			struct pobj_action *act;

			D_ASSERT(ioc->ic_actv_cnt > ioc->ic_actv_at);
			D_ASSERT(ioc->ic_actv != NULL);
			act = &ioc->ic_actv[ioc->ic_actv_at];

			mmid = umem_reserve(vos_obj2umm(obj), act, size);
			if (!UMMID_IS_NULL(mmid))
				ioc->ic_actv_at++;
		} else {
			mmid = umem_alloc(vos_obj2umm(obj), size);
		}

		if (!UMMID_IS_NULL(mmid)) {
			ioc->ic_mmids[ioc->ic_mmids_cnt] = mmid;
			ioc->ic_mmids_cnt++;
			*off = mmid.off;
		}

		return UMMID_IS_NULL(mmid) ? -DER_NOSPACE : 0;
	}

	D_ASSERT(media == DAOS_MEDIA_NVME);

	vsi = obj->obj_cont->vc_pool->vp_vea_info;
	D_ASSERT(vsi);
	hint_ctxt = obj->obj_cont->vc_hint_ctxt;
	D_ASSERT(hint_ctxt);
	blk_cnt = vos_byte2blkcnt(size);

	rc = vea_reserve(vsi, blk_cnt, hint_ctxt, &ioc->ic_blk_exts);
	if (rc)
		return rc;

	ext = d_list_entry(ioc->ic_blk_exts.prev, struct vea_resrvd_ext,
			   vre_link);
	D_ASSERTF(ext->vre_blk_cnt == blk_cnt, "%u != %u\n",
		  ext->vre_blk_cnt, blk_cnt);
	D_ASSERT(ext->vre_blk_off != 0);

	*off = ext->vre_blk_off << VOS_BLK_SHIFT;
	return 0;
}

static int
iod_reserve(struct vos_io_context *ioc, struct bio_iov *biov)
{
	struct bio_sglist *bsgl;

	bsgl = bio_iod_sgl(ioc->ic_biod, ioc->ic_sgl_at);
	D_ASSERT(bsgl->bs_nr != 0);
	D_ASSERT(bsgl->bs_nr > bsgl->bs_nr_out);
	D_ASSERT(bsgl->bs_nr > ioc->ic_iov_at);

	bsgl->bs_iovs[ioc->ic_iov_at] = *biov;
	ioc->ic_iov_at++;
	bsgl->bs_nr_out++;

	D_DEBUG(DB_IO, "media %hu offset "DF_U64" size %zd\n",
		biov->bi_addr.ba_type, biov->bi_addr.ba_off,
		biov->bi_data_len);
	return 0;
}

/* Reserve single value record on specified media */
static int
vos_reserve_single(struct vos_io_context *ioc, uint16_t media,
		   daos_size_t size)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_irec_df	*irec;
	daos_size_t		 scm_size;
	umem_id_t		 mmid;
	struct bio_iov		 biov;
	uint64_t		 off = 0;
	int			 rc;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];


	/*
	 * TODO:
	 * To eliminate internal fragmentaion, misaligned record (record size
	 * isn't aligned with 4K) on NVMe could be split into two parts, large
	 * aligned part will be stored on NVMe and being referenced by
	 * vos_irec_df->ir_ex_addr, small unaligned part will be stored on SCM
	 * along with vos_irec_df, being referenced by vos_irec_df->ir_body.
	 */
	scm_size = (media == DAOS_MEDIA_SCM) ?
		vos_recx2irec_size(size, iod->iod_csums) :
		vos_recx2irec_size(0, iod->iod_csums);

	rc = vos_reserve(ioc, DAOS_MEDIA_SCM, scm_size, &off);
	if (rc) {
		D_ERROR("Reserve SCM for SV failed. %d\n", rc);
		return rc;
	}

	D_ASSERT(ioc->ic_mmids_cnt > 0);
	mmid = ioc->ic_mmids[ioc->ic_mmids_cnt - 1];
	irec = (struct vos_irec_df *) umem_id2ptr(vos_obj2umm(obj), mmid);
	vos_irec_init_csum(irec, iod->iod_csums);

	memset(&biov, 0, sizeof(biov));
	if (size == 0) { /* punch */
		bio_addr_set_hole(&biov.bi_addr, 1);
		goto done;
	}

	if (media == DAOS_MEDIA_SCM) {
		char *payload_addr;

		/* Get the record payload offset */
		payload_addr = vos_irec2data(irec);
		D_ASSERT(payload_addr >= (char *)irec);
		off = mmid.off + (payload_addr - (char *)irec);
	} else {
		rc = vos_reserve(ioc, DAOS_MEDIA_NVME, size, &off);
		if (rc) {
			D_ERROR("Reserve NVMe for SV failed. %d\n", rc);
			return rc;
		}
	}
done:
	bio_addr_set(&biov.bi_addr, media, off);
	biov.bi_data_len = size;
	rc = iod_reserve(ioc, &biov);

	return rc;
}

static int
vos_reserve_recx(struct vos_io_context *ioc, uint16_t media, daos_size_t size)
{
	struct bio_iov	biov;
	uint64_t	off = 0;
	int		rc;

	memset(&biov, 0, sizeof(biov));
	/* recx punch */
	if (size == 0) {
		ioc->ic_mmids[ioc->ic_mmids_cnt] = UMMID_NULL;
		ioc->ic_mmids_cnt++;
		bio_addr_set_hole(&biov.bi_addr, 1);
		goto done;
	}

	/*
	 * TODO:
	 * To eliminate internal fragmentaion, misaligned recx (total recx size
	 * isn't aligned with 4K) on NVMe could be split into two evtree rects,
	 * larger rect will be stored on NVMe and small reminder on SCM.
	 */
	rc = vos_reserve(ioc, media, size, &off);
	if (rc) {
		D_ERROR("Reserve recx failed. %d\n", rc);
		return rc;
	}
done:
	bio_addr_set(&biov.bi_addr, media, off);
	biov.bi_data_len = size;
	rc = iod_reserve(ioc, &biov);

	return rc;
}

/*
 * A simple media selection policy embedded in VOS, which select media by
 * akey type and record size.
 */
static uint16_t
akey_media_select(struct vos_io_context *ioc, daos_iod_type_t type,
		  daos_size_t size)
{
	struct vea_space_info *vsi;

	vsi = ioc->ic_obj->obj_cont->vc_pool->vp_vea_info;
	if (vsi == NULL)
		return DAOS_MEDIA_SCM;
	else
		return (size >= VOS_BLK_SZ) ? DAOS_MEDIA_NVME : DAOS_MEDIA_SCM;
}

static int
akey_update_begin(struct vos_io_context *ioc)
{
	daos_iod_t *iod = &ioc->ic_iods[ioc->ic_sgl_at];
	int i, rc;

	if (iod->iod_type == DAOS_IOD_SINGLE && iod->iod_nr != 1) {
		D_ERROR("Invalid sv iod_nr=%d\n", iod->iod_nr);
		return -DER_IO_INVAL;
	}

	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t size;
		uint16_t media;

		size = (iod->iod_type == DAOS_IOD_SINGLE) ? iod->iod_size :
				iod->iod_recxs[i].rx_nr * iod->iod_size;

		media = akey_media_select(ioc, iod->iod_type, size);

		if (iod->iod_type == DAOS_IOD_SINGLE)
			rc = vos_reserve_single(ioc, media, size);
		else
			rc = vos_reserve_recx(ioc, media, size);
		if (rc)
			return rc;
	}
	return 0;
}

static int
dkey_update_begin(struct vos_io_context *ioc, daos_key_t *dkey)
{
	int i, rc = 0;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_update_begin(ioc);
		if (rc != 0)
			break;
	}

	return rc;
}

/* Publish or cancel the NVMe block reservations */
static int
process_blocks(struct vos_io_context *ioc, bool publish)
{
	struct vea_space_info	*vsi;
	struct vea_hint_context	*hint_ctxt;
	int			 rc;

	if (d_list_empty(&ioc->ic_blk_exts))
		return 0;

	vsi = ioc->ic_obj->obj_cont->vc_pool->vp_vea_info;
	D_ASSERT(vsi);
	hint_ctxt = ioc->ic_obj->obj_cont->vc_hint_ctxt;
	D_ASSERT(hint_ctxt);

	rc = publish ? vea_tx_publish(vsi, hint_ctxt, &ioc->ic_blk_exts) :
		       vea_cancel(vsi, hint_ctxt, &ioc->ic_blk_exts);
	if (rc)
		D_ERROR("Error on %s NVMe reservations. %d\n",
			publish ? "publish" : "cancel", rc);

	return rc;
}

static void
update_cancel(struct vos_io_context *ioc)
{
	/* Cancel SCM reservations or free persistent allocations */
	if (ioc->ic_actv_at != 0) {
		D_ASSERT(ioc->ic_actv != NULL);
		umem_cancel(vos_obj2umm(ioc->ic_obj), ioc->ic_actv,
			    ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
	} else if (ioc->ic_mmids_cnt != 0) {
		struct umem_instance *umem = vos_obj2umm(ioc->ic_obj);
		int i, rc;

		rc = umem_tx_begin(umem, vos_txd_get());
		if (rc) {
			D_ERROR("TX start for update rollback: %d\n", rc);
			return;
		}

		for (i = 0; i < ioc->ic_mmids_cnt; i++) {
			if (UMMID_IS_NULL(ioc->ic_mmids[i]))
				continue;
			umem_free(umem, ioc->ic_mmids[i]);
		}

		rc =  umem_tx_commit(umem);
		if (rc) {
			D_ERROR("TX commit for update rollback: %d\n", rc);
			return;
		}
	}

	/* Cancel NVMe reservations */
	process_blocks(ioc, false);
}

int
vos_update_end(daos_handle_t ioh, uint32_t pm_ver, daos_key_t *dkey, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);
	struct umem_instance *umem;

	D_ASSERT(ioc->ic_update);
	D_ASSERT(ioc->ic_obj != NULL);

	if (err != 0)
		goto out;

	err = vos_obj_revalidate(vos_obj_cache_current(), ioc->ic_epoch,
				 &ioc->ic_obj);
	if (err)
		goto out;

	umem = vos_obj2umm(ioc->ic_obj);
	err = umem_tx_begin(umem, vos_txd_get());
	if (err)
		goto out;

	/* Publish SCM reservations */
	if (ioc->ic_actv_at != 0) {
		err = umem_tx_publish(umem, ioc->ic_actv, ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
		D_DEBUG(DB_TRACE, "publish ioc %p actv_at %d rc %d\n",
			ioc, ioc->ic_actv_cnt, err);
		if (err)
			goto abort;
	}

	/* Update tree index */
	err = dkey_update(ioc, pm_ver, dkey);
	if (err) {
		D_ERROR("Failed to update tree index: %d\n", err);
		goto abort;
	}

	/* Publish NVMe reservations */
	err = process_blocks(ioc, true);

abort:
	err = err ? umem_tx_abort(umem, err) : umem_tx_commit(umem);
out:
	if (err != 0)
		update_cancel(ioc);
	vos_ioc_destroy(ioc);

	return err;
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		 daos_handle_t *ioh)
{
	struct vos_io_context *ioc;
	int rc;

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, false, &ioc);
	if (rc != 0)
		return rc;

	if (ioc->ic_actv_cnt != 0) {
		rc = dkey_update_begin(ioc, dkey);
		if (rc)
			goto error;
	} else {
		struct umem_instance *umem = vos_obj2umm(ioc->ic_obj);

		rc = umem_tx_begin(umem, vos_txd_get());
		if (rc)
			goto error;

		rc = dkey_update_begin(ioc, dkey);
		if (rc)
			D_ERROR(DF_UOID"dkey update begin failed. %d\n",
				DP_UOID(oid), rc);

		rc = rc ? umem_tx_abort(umem, rc) : umem_tx_commit(umem);
		if (rc)
			goto error;
	}

	D_DEBUG(DB_IO, "Prepared io context for updating %d iods\n", iod_nr);
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	vos_update_end(vos_ioc2ioh(ioc), 0, dkey, rc);
	return rc;
}

struct bio_desc *
vos_ioh2desc(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	D_ASSERT(ioc->ic_biod != NULL);
	return ioc->ic_biod;
}

struct bio_sglist *
vos_iod_sgl_at(daos_handle_t ioh, unsigned int idx)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	if (idx > ioc->ic_iod_nr) {
		D_ERROR("Invalid SGL index %d >= %d\n",
			idx, ioc->ic_iod_nr);
		return NULL;
	}
	return bio_iod_sgl(ioc->ic_biod, idx);
}

/**
 * @defgroup vos_obj_update() & vos_obj_fetch() functions
 * @{
 */

/**
 * vos_obj_update() & vos_obj_fetch() are two helper functions used
 * for inline update and fetch, so far it's used by rdb, rebuild and
 * some test programs (daos_perf, vos tests, etc).
 *
 * Caveat: These two functions may yield, please use with caution.
 */
static int
vos_obj_copy(struct vos_io_context *ioc, daos_sg_list_t *sgls,
	     unsigned int sgl_nr)
{
	int rc, err;

	D_ASSERT(sgl_nr == ioc->ic_iod_nr);
	rc = bio_iod_prep(ioc->ic_biod);
	if (rc)
		return rc;

	err = bio_iod_copy(ioc->ic_biod, sgls, sgl_nr);
	rc = bio_iod_post(ioc->ic_biod);

	return err ? err : rc;
}

int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uint32_t pm_ver, daos_key_t *dkey, unsigned int iod_nr,
	       daos_iod_t *iods, daos_sg_list_t *sgls)
{
	daos_handle_t ioh;
	int rc;

	D_DEBUG(DB_IO, "Update "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_update_begin(coh, oid, epoch, dkey, iod_nr, iods, &ioh);
	if (rc) {
		D_ERROR("Update "DF_UOID" failed %d\n", DP_UOID(oid), rc);
		return rc;
	}

	if (sgls) {
		rc = vos_obj_copy(vos_ioh2ioc(ioh), sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed %d\n", DP_UOID(oid), rc);
	}

	rc = vos_update_end(ioh, pm_ver, dkey, rc);
	return rc;
}

int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
	      daos_sg_list_t *sgls)
{
	daos_handle_t ioh;
	bool size_fetch = (sgls == NULL);
	int rc;

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_fetch_begin(coh, oid, epoch, dkey, iod_nr, iods, size_fetch,
			     &ioh);
	if (rc) {
		D_ERROR("Fetch "DF_UOID" failed %d\n", DP_UOID(oid), rc);
		return rc;
	}

	if (!size_fetch) {
		struct vos_io_context *ioc = vos_ioh2ioc(ioh);
		int i, j;

		for (i = 0; i < iod_nr; i++) {
			struct bio_sglist *bsgl = bio_iod_sgl(ioc->ic_biod, i);
			daos_sg_list_t *sgl = &sgls[i];

			/* Inform caller the nonexistent of object/key */
			if (bsgl->bs_nr_out == 0) {
				for (j = 0; j < sgl->sg_nr; j++)
					sgl->sg_iovs[j].iov_len = 0;
			}
		}

		rc = vos_obj_copy(ioc, sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed %d\n",
				DP_UOID(oid), rc);
	}

	rc = vos_fetch_end(ioh, rc);
	return rc;
}

/**
 * @} vos_obj_update() & vos_obj_fetch() functions
 */
