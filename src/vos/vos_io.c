/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
#include <daos/checksum.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <daos.h>
#include "vos_internal.h"
#include "evt_priv.h"

/** I/O context */
struct vos_io_context {
	daos_epoch_range_t	 ic_epr;
	daos_unit_oid_t		 ic_oid;
	struct vos_container	*ic_cont;
	daos_iod_t		*ic_iods;
	struct dcs_iod_csums	*iod_csums;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** BIO descriptor, has ic_iod_nr SGLs */
	struct bio_desc		*ic_biod;
	/** Checksums for bio_iovs in \ic_biod */
	struct dcs_csum_info	*ic_biov_csums;
	uint32_t		 ic_biov_csums_at;
	uint32_t		 ic_biov_csums_nr;
	/** current dkey info */
	struct vos_ilog_info	 ic_dkey_info;
	/** current akey info */
	struct vos_ilog_info	 ic_akey_info;
	/** cursor of SGL & IOV in BIO descriptor */
	unsigned int		 ic_sgl_at;
	unsigned int		 ic_iov_at;
	/** reserved SCM extents */
	unsigned int		 ic_actv_cnt;
	unsigned int		 ic_actv_at;
	struct pobj_action	*ic_actv;
	/** reserved offsets for SCM update */
	umem_off_t		*ic_umoffs;
	unsigned int		 ic_umoffs_cnt;
	unsigned int		 ic_umoffs_at;
	/** reserved NVMe extents */
	d_list_t		 ic_blk_exts;
	/** number DAOS IO descriptors */
	unsigned int		 ic_iod_nr;
	/** flags */
	unsigned int		 ic_update:1,
				 ic_size_fetch:1;
};

static inline struct umem_instance *
vos_ioc2umm(struct vos_io_context *ioc)
{
	return &ioc->ic_cont->vc_pool->vp_umm;
}

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

static struct dcs_csum_info *
vos_ioc2csum(struct vos_io_context *ioc)
{
	if (ioc->iod_csums != NULL)
		return ioc->iod_csums[ioc->ic_sgl_at].ic_data;
	return NULL;
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

	if (ioc->ic_umoffs != NULL) {
		D_FREE(ioc->ic_umoffs);
		ioc->ic_umoffs = NULL;
	}
}

static int
vos_ioc_reserve_init(struct vos_io_context *ioc)
{
	int			 i, total_acts = 0;

	if (!ioc->ic_update)
		return 0;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		daos_iod_t *iod = &ioc->ic_iods[i];

		total_acts += iod->iod_nr;
	}

	D_ALLOC_ARRAY(ioc->ic_umoffs, total_acts);
	if (ioc->ic_umoffs == NULL)
		return -DER_NOMEM;

	if (vos_ioc2umm(ioc)->umm_ops->mo_reserve == NULL)
		return 0;

	D_ALLOC_ARRAY(ioc->ic_actv, total_acts);
	if (ioc->ic_actv == NULL)
		return -DER_NOMEM;

	ioc->ic_actv_cnt = total_acts;
	return 0;
}

static void
vos_ioc_destroy(struct vos_io_context *ioc, bool evict)
{
	if (ioc->ic_biod != NULL)
		bio_iod_free(ioc->ic_biod);

	if (ioc->ic_biov_csums != NULL)
		D_FREE(ioc->ic_biov_csums);

	if (ioc->ic_obj)
		vos_obj_release(vos_obj_cache_current(), ioc->ic_obj, evict);

	vos_ioc_reserve_fini(ioc);
	vos_ilog_fetch_finish(&ioc->ic_dkey_info);
	vos_ilog_fetch_finish(&ioc->ic_akey_info);
	vos_cont_decref(ioc->ic_cont);
	D_FREE(ioc);
}

static int
vos_ioc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr, daos_iod_t *iods,
	       struct dcs_iod_csums *iod_csums, bool size_fetch,
	       struct vos_io_context **ioc_pp)
{
	struct vos_container *cont;
	struct vos_io_context *ioc = NULL;
	struct bio_io_context *bioc;
	int i, rc;

	if (iod_nr == 0) {
		D_ERROR("Invalid iod_nr (0).\n");
		rc = -DER_IO_INVAL;
		goto error;
	}

	D_ALLOC_PTR(ioc);
	if (ioc == NULL)
		return -DER_NOMEM;

	ioc->ic_iod_nr = iod_nr;
	ioc->ic_iods = iods;
	ioc->ic_epr.epr_hi = epoch;
	ioc->ic_epr.epr_lo = 0;
	ioc->ic_oid = oid;
	ioc->ic_cont = vos_hdl2cont(coh);
	vos_cont_addref(ioc->ic_cont);
	ioc->ic_update = !read_only;
	ioc->ic_size_fetch = size_fetch;
	ioc->ic_actv = NULL;
	ioc->ic_actv_cnt = ioc->ic_actv_at = 0;
	ioc->ic_umoffs_cnt = ioc->ic_umoffs_at = 0;
	ioc->iod_csums = iod_csums;
	vos_ilog_fetch_init(&ioc->ic_dkey_info);
	vos_ilog_fetch_init(&ioc->ic_akey_info);
	D_INIT_LIST_HEAD(&ioc->ic_blk_exts);

	rc = vos_ioc_reserve_init(ioc);
	if (rc != 0)
		goto error;

	cont = vos_hdl2cont(coh);

	bioc = cont->vc_pool->vp_io_ctxt;
	D_ASSERT(bioc != NULL);
	ioc->ic_biod = bio_iod_alloc(bioc, iod_nr, !read_only);
	if (ioc->ic_biod == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	ioc->ic_biov_csums_nr = 1;
	ioc->ic_biov_csums_at = 0;
	D_ALLOC_ARRAY(ioc->ic_biov_csums, ioc->ic_biov_csums_nr);
	if (ioc->ic_biov_csums == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	for (i = 0; i < iod_nr; i++) {
		int iov_nr = iods[i].iod_nr;
		struct bio_sglist *bsgl;

		if ((iods[i].iod_type == DAOS_IOD_SINGLE && iov_nr != 1) ||
		    (iov_nr == 0 && iods[i].iod_recxs != NULL)) {
			D_ERROR("Invalid iod_nr=%d, iod_type %d.\n",
				iov_nr, iods[i].iod_type);
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
	if (ioc != NULL)
		vos_ioc_destroy(ioc, false);
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

static int
bsgl_csums_resize(struct vos_io_context *ioc)
{
	struct dcs_csum_info *csums = ioc->ic_biov_csums;
	uint32_t	 dcb_nr = ioc->ic_biov_csums_nr;

	if (ioc->ic_size_fetch)
		return 0;

	if (ioc->ic_biov_csums_at == dcb_nr - 1) {
		struct dcs_csum_info *new_infos;
		uint32_t	 new_nr = dcb_nr * 2;

		D_REALLOC_ARRAY(new_infos, csums, new_nr);
		if (new_infos == NULL)
			return -DER_NOMEM;

		ioc->ic_biov_csums = new_infos;
		ioc->ic_biov_csums_nr = new_nr;
	}

	return 0;
}

/** Save the checksum to a list that can be retrieved later */
static int
save_csum(struct vos_io_context *ioc, struct dcs_csum_info *csum_info)
{
	int rc;

	rc = bsgl_csums_resize(ioc);
	if (rc != 0)
		return rc;

	/**
	 * it's expected that the csum the csum_info points to is in memory
	 * that will persist until fetch is complete ... so memcpy isn't needed
	 */
	ioc->ic_biov_csums[ioc->ic_biov_csums_at] = *csum_info;
	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
		/* poison the checksum */
		ioc->ic_biov_csums[ioc->ic_biov_csums_at].cs_csum[0] += 2;

	ioc->ic_biov_csums_at++;

	return 0;
}

/** Fetch the single value within the specified epoch range of an key */
static int
akey_fetch_single(daos_handle_t toh, const daos_epoch_range_t *epr,
		  daos_size_t *rsize, daos_size_t *gsize,
		  struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 kiov; /* iov to carry key bundle */
	d_iov_t			 riov; /* iov to carry record bundle */
	struct bio_iov		 biov; /* iov to return data buffer */
	int			 rc;
	struct dcs_csum_info	csum_info = {0};

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= epr->epr_hi;

	tree_rec_bundle2iov(&rbund, &riov);
	memset(&biov, 0, sizeof(biov));
	rbund.rb_biov	= &biov;
	rbund.rb_csum = &csum_info;

	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &kiov, &kiov,
			  &riov);
	if (rc == -DER_NONEXIST) {
		rbund.rb_rsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
		rc = 0;
	} else if (rc != 0) {
		goto out;
	} else if (kbund.kb_epoch < epr->epr_lo) {
		/* The single value is before the valid epoch range (after a
		 * punch when incarnation log is available
		 */
		rc = 0;
		rbund.rb_rsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
	}
	if (ci_is_valid(&csum_info))
		save_csum(ioc, &csum_info);

	rc = iod_fetch(ioc, &biov);
	if (rc != 0)
		goto out;

	*rsize = rbund.rb_rsize;
	*gsize = rbund.rb_gsize;
out:
	return rc;
}

static inline void
biov_set_hole(struct bio_iov *biov, ssize_t len)
{
	memset(biov, 0, sizeof(*biov));
	bio_iov_set_len(biov, len);
	bio_addr_set_hole(&biov->bi_addr, 1);
}

/**
 * Calculate the bio_iov and extent chunk alignment and set appropriate
 * prefix & suffix on the biov so that whole chunks are fetched in case needed
 * for checksum calculation and verification.
 * Should only be called when the entity has a valid checksum.
 */
static void
biov_align_lens(struct bio_iov *biov, struct evt_entry *ent, daos_size_t rsize)
{
	struct evt_extent aligned_extent;

	aligned_extent = evt_entry_align_to_csum_chunk(ent, rsize);
	bio_iov_set_extra(biov,
			  (ent->en_sel_ext.ex_lo - aligned_extent.ex_lo) *
			  rsize,
			  (aligned_extent.ex_hi - ent->en_sel_ext.ex_hi) *
			  rsize);
}

/** Fetch an extent from an akey */
static int
akey_fetch_recx(daos_handle_t toh, const daos_epoch_range_t *epr,
		daos_recx_t *recx, daos_size_t *rsize_p,
		struct vos_io_context *ioc)
{
	struct evt_entry	*ent;
	/* At present, this is not exposed in interface but passing it toggles
	 * sorting and clipping of rectangles
	 */
	struct evt_entry_array	 ent_array = { 0 };
	struct evt_extent	 extent;
	struct bio_iov		 biov = {0};
	daos_size_t		 holes; /* hole width */
	daos_size_t		 rsize;
	daos_off_t		 index;
	daos_off_t		 end;
	bool			 csum_enabled = false;
	int			 rc;

	index = recx->rx_idx;
	end   = recx->rx_idx + recx->rx_nr;

	extent.ex_lo = index;
	extent.ex_hi = end - 1;

	evt_ent_array_init(&ent_array);
	rc = evt_find(toh, epr, &extent, &ent_array);
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
				  DF_U64"/"DF_U64", "DF_EXT", "DF_ENT"\n",
				  lo, index, DP_EXT(&extent),
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

		bio_iov_set(&biov, ent->en_addr, nr * ent_array.ea_inob);

		if (ci_is_valid(&ent->en_csum)) {
			rc = save_csum(ioc, &ent->en_csum);
			if (rc != 0)
				return rc;
			biov_align_lens(&biov, ent, rsize);
			csum_enabled = true;
		} else {
			bio_iov_set_extra(&biov, 0, 0);
			if (csum_enabled)
				D_ERROR("Checksum found in some entries, "
					"but not all");
		}

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
key_ilog_check(struct vos_io_context *ioc, struct vos_krec_df *krec,
	       const struct vos_ilog_info *parent, daos_epoch_range_t *epr_out,
	       struct vos_ilog_info *info)
{
	struct umem_instance	*umm;
	daos_epoch_range_t	 epr = ioc->ic_epr;
	int			 rc;

	umm = vos_obj2umm(ioc->ic_obj);
	rc = vos_ilog_fetch(umm, vos_cont2hdl(ioc->ic_cont),
			    DAOS_INTENT_DEFAULT, &krec->kr_ilog,
			    epr.epr_hi, 0, parent, info);
	if (rc != 0)
		goto out;

	rc = vos_ilog_check(info, &epr, epr_out, true);
out:
	D_DEBUG(DB_TRACE, "ilog check returned "DF_RC" epr_in="DF_U64"-"DF_U64
		" punch="DF_U64" epr_out="DF_U64"-"DF_U64"\n", DP_RC(rc),
		epr.epr_lo, epr.epr_hi, info->ii_prior_punch,
		epr_out ? epr_out->epr_lo : 0,
		epr_out ? epr_out->epr_hi : 0);
	return rc;
}

static int
akey_fetch(struct vos_io_context *ioc, daos_handle_t ak_toh)
{
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct vos_krec_df	*krec = NULL;
	daos_epoch_range_t	 val_epr = {0};
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i, rc;
	int			 flags = 0;
	bool			 is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	D_DEBUG(DB_IO, "akey "DF_KEY" fetch %s epr "DF_U64"-"DF_U64"\n",
		DP_KEY(&iod->iod_name),
		iod->iod_type == DAOS_IOD_ARRAY ? "array" : "single",
		ioc->ic_epr.epr_lo, ioc->ic_epr.epr_hi);

	if (is_array)
		flags |= SUBTR_EVT;

	rc = key_tree_prepare(ioc->ic_obj, ak_toh,
			      VOS_BTR_AKEY, &iod->iod_name, flags,
			      DAOS_INTENT_DEFAULT, &krec, &toh);

	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_IO, "Nonexistent akey "DF_KEY"\n",
				DP_KEY(&iod->iod_name));
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
			rc = 0;
		} else {
			D_ERROR("Failed to fetch akey: "DF_RC"\n", DP_RC(rc));
		}
		goto out;
	}

	rc = key_ilog_check(ioc, krec, &ioc->ic_dkey_info, &val_epr,
			    &ioc->ic_akey_info);

	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_IO, "Nonexistent akey %.*s\n",
				(int)iod->iod_name.iov_len,
				(char *)iod->iod_name.iov_buf);
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
			rc = 0;
		} else {
			D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
				 "Fetch akey failed: rc="DF_RC"\n",
				 DP_RC(rc));
		}
		goto out;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_fetch_single(toh, &val_epr, &iod->iod_size,
				       &iod->iod_size, ioc);
		goto out;
	}

	iod->iod_size = 0;
	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t rsize;

		if (iod->iod_recxs[i].rx_nr == 0) {
			D_DEBUG(DB_IO,
				"Skip empty read IOD at %d: idx %lu, nr %lu\n",
				i, (unsigned long)iod->iod_recxs[i].rx_idx,
				(unsigned long)iod->iod_recxs[i].rx_nr);
			continue;
		}

		rc = akey_fetch_recx(toh, &val_epr, &iod->iod_recxs[i], &rsize,
				     ioc);
		if (rc != 0) {
			D_DEBUG(DB_IO, "Failed to fetch index %d: "DF_RC"\n", i,
				DP_RC(rc));
			goto out;
		}

		/*
		 * Empty tree or all holes, DAOS array API relies on zero
		 * iod_size to see if an array cell is empty.
		 */
		if (rsize == 0)
			continue;

		if (iod->iod_size == DAOS_REC_ANY)
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
		key_tree_release(toh, is_array);

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
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_krec_df	*krec;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY,
			      dkey, 0, DAOS_INTENT_DEFAULT, &krec,
			      &toh);

	if (rc == -DER_NONEXIST) {
		for (i = 0; i < ioc->ic_iod_nr; i++)
			iod_empty_sgl(ioc, i);
		D_DEBUG(DB_IO, "Nonexistent dkey\n");
		rc = 0;
		goto out;
	}

	if (rc != 0) {
		D_ERROR("Failed to prepare subtree: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = key_ilog_check(ioc, krec, &obj->obj_ilog_info, &ioc->ic_epr,
			    &ioc->ic_dkey_info);

	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			for (i = 0; i < ioc->ic_iod_nr; i++)
				iod_empty_sgl(ioc, i);
			D_DEBUG(DB_IO, "Nonexistent dkey\n");
			rc = 0;
		} else {
			D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
				 "Fetch dkey failed: rc="DF_RC"\n",
				 DP_RC(rc));
		}
		goto out;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_fetch(ioc, toh);
		if (rc != 0)
			break;
	}
out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, false);

	return rc;
}

int
vos_fetch_end(daos_handle_t ioh, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	/* NB: it's OK to use the stale ioc->ic_obj for fetch_end */
	D_ASSERT(!ioc->ic_update);
	vos_ioc_destroy(ioc, false);
	return err;
}

int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		bool size_fetch, daos_handle_t *ioh)
{
	struct vos_io_context *ioc;
	int i, rc;

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_ioc_create(coh, oid, true, epoch, iod_nr, iods, NULL,
			    size_fetch,
			    &ioc);
	if (rc != 0)
		return rc;

	rc = vos_obj_hold(vos_obj_cache_current(), ioc->ic_cont, oid,
			  &ioc->ic_epr, true, DAOS_INTENT_DEFAULT, true,
			  &ioc->ic_obj);
	if (rc != -DER_NONEXIST && rc != 0)
		goto error;

	if (rc == -DER_NONEXIST) {
		rc = 0;
		for (i = 0; i < iod_nr; i++)
			iod_empty_sgl(ioc, i);
	} else {
		rc = dkey_fetch(ioc, dkey);
		if (rc != 0)
			goto error;
	}

	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	return vos_fetch_end(vos_ioc2ioh(ioc), rc);
}

static umem_off_t
iod_update_umoff(struct vos_io_context *ioc)
{
	umem_off_t umoff;

	D_ASSERTF(ioc->ic_umoffs_at < ioc->ic_umoffs_cnt,
		  "Invalid ioc_reserve at/cnt: %u/%u\n",
		  ioc->ic_umoffs_at, ioc->ic_umoffs_cnt);

	umoff = ioc->ic_umoffs[ioc->ic_umoffs_at];
	ioc->ic_umoffs_at++;

	return umoff;
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
akey_update_single(daos_handle_t toh, uint32_t pm_ver, daos_size_t rsize,
		   daos_size_t gsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	struct dcs_csum_info	 csum;
	d_iov_t			 kiov, riov;
	struct bio_iov		*biov;
	umem_off_t		 umoff;
	daos_epoch_t		 epoch = ioc->ic_epr.epr_hi;
	int			 rc;

	ci_set_null(&csum);
	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= epoch;


	umoff = iod_update_umoff(ioc);
	D_ASSERT(!UMOFF_IS_NULL(umoff));

	D_ASSERT(ioc->ic_iov_at == 0);
	biov = iod_update_biov(ioc);

	tree_rec_bundle2iov(&rbund, &riov);

	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_UPDATE_FAIL)) {
		rbund.rb_csum	= &csum;
	} else {
		struct dcs_csum_info *value_csum = vos_ioc2csum(ioc);

		if (value_csum != NULL)
			rbund.rb_csum	= value_csum;
		else
			rbund.rb_csum	= &csum;
	}

	rbund.rb_biov	= biov;
	rbund.rb_rsize	= rsize;
	rbund.rb_gsize	= gsize;
	rbund.rb_off	= umoff;
	rbund.rb_ver	= pm_ver;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0)
		D_ERROR("Failed to update subtree: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Update a record extent.
 * See comment of vos_recx_fetch for explanation of @off_p.
 */
static int
akey_update_recx(daos_handle_t toh, uint32_t pm_ver, daos_recx_t *recx,
		 struct dcs_csum_info *csum, daos_size_t rsize,
		 struct vos_io_context *ioc)
{
	struct evt_entry_in	 ent;
	struct bio_iov		*biov;
	daos_epoch_t		 epoch = ioc->ic_epr.epr_hi;
	int rc;

	D_ASSERT(recx->rx_nr > 0);
	memset(&ent, 0, sizeof(ent));
	ent.ei_rect.rc_epc = epoch;
	ent.ei_rect.rc_ex.ex_lo = recx->rx_idx;
	ent.ei_rect.rc_ex.ex_hi = recx->rx_idx + recx->rx_nr - 1;
	ent.ei_ver = pm_ver;
	ent.ei_inob = rsize;

	if (ci_is_valid(csum)) {
		ent.ei_csum = *csum;
		/* change the checksum for fault injection*/
		if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_UPDATE_FAIL))
			ent.ei_csum.cs_csum[0] += 1;
	}

	biov = iod_update_biov(ioc);
	ent.ei_addr = biov->bi_addr;
	rc = evt_insert(toh, &ent);

	return rc;
}

static int
akey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_handle_t ak_toh)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_krec_df	*krec = NULL;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct dcs_csum_info	*iod_csums = vos_ioc2csum(ioc);
	struct dcs_csum_info	*recx_csum = NULL;
	bool			 is_array = (iod->iod_type == DAOS_IOD_ARRAY);
	int			 flags = SUBTR_CREATE;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i;
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "akey "DF_KEY" update %s value eph "DF_U64"\n",
		DP_KEY(&iod->iod_name), is_array ? "array" : "single",
		ioc->ic_epr.epr_hi);

	if (is_array)
		flags |= SUBTR_EVT;

	rc = key_tree_prepare(obj, ak_toh, VOS_BTR_AKEY,
			      &iod->iod_name, flags, DAOS_INTENT_UPDATE,
			      &krec, &toh);
	if (rc != 0)
		return rc;

	rc = vos_ilog_update(ioc->ic_cont, &krec->kr_ilog, &ioc->ic_epr,
			     &ioc->ic_dkey_info, &ioc->ic_akey_info);
	if (rc != 0) {
		D_ERROR("Failed to update akey ilog: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		uint64_t	gsize;

		gsize = (iod->iod_recxs == NULL) ? iod->iod_size :
						   (uintptr_t)iod->iod_recxs;
		rc = akey_update_single(toh, pm_ver, iod->iod_size, gsize, ioc);
		goto out;
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		umem_off_t	umoff = iod_update_umoff(ioc);

		if (iod->iod_recxs[i].rx_nr == 0) {
			D_ASSERT(UMOFF_IS_NULL(umoff));
			D_DEBUG(DB_IO,
				"Skip empty write IOD at %d: idx %lu, nr %lu\n",
				i, (unsigned long)iod->iod_recxs[i].rx_idx,
				(unsigned long)iod->iod_recxs[i].rx_nr);
			continue;
		}

		if (iod_csums != NULL)
			recx_csum = &iod_csums[i];
		rc = akey_update_recx(toh, pm_ver, &iod->iod_recxs[i],
				      recx_csum, iod->iod_size, ioc);
		if (rc != 0)
			goto out;
	}
out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, is_array);

	return rc;
}

static int
dkey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_key_t *dkey)
{
	struct vos_object	*obj = ioc->ic_obj;
	daos_handle_t		 ak_toh;
	struct vos_krec_df	*krec;
	bool			 subtr_created = false;
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY, dkey,
			      SUBTR_CREATE, DAOS_INTENT_UPDATE, &krec, &ak_toh);
	if (rc != 0) {
		D_ERROR("Error preparing dkey tree: rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}
	subtr_created = true;

	rc = vos_ilog_update(ioc->ic_cont, &krec->kr_ilog, &ioc->ic_epr,
			     &obj->obj_ilog_info, &ioc->ic_dkey_info);
	if (rc != 0) {
		D_ERROR("Failed to update dkey ilog: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);

		rc = akey_update(ioc, pm_ver, ak_toh);
		if (rc != 0)
			goto out;
	}
out:
	if (!subtr_created)
		return rc;

	if (rc != 0)
		goto release;

release:
	key_tree_release(ak_toh, false);

	return rc;
}

static daos_size_t
vos_recx2irec_size(daos_size_t rsize, struct dcs_csum_info *csum)
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
	struct vea_space_info	*vsi;
	struct vea_hint_context	*hint_ctxt;
	struct vea_resrvd_ext	*ext;
	uint32_t		 blk_cnt;
	umem_off_t		 umoff;
	int			 rc;

	if (media == DAOS_MEDIA_SCM) {
		if (ioc->ic_actv_cnt > 0) {
			struct pobj_action *act;

			D_ASSERT(ioc->ic_actv_cnt > ioc->ic_actv_at);
			D_ASSERT(ioc->ic_actv != NULL);
			act = &ioc->ic_actv[ioc->ic_actv_at];

			umoff = umem_reserve(vos_ioc2umm(ioc), act, size);
			if (!UMOFF_IS_NULL(umoff))
				ioc->ic_actv_at++;
		} else {
			umoff = umem_alloc(vos_ioc2umm(ioc), size);
		}

		if (!UMOFF_IS_NULL(umoff)) {
			ioc->ic_umoffs[ioc->ic_umoffs_cnt] = umoff;
			ioc->ic_umoffs_cnt++;
			*off = umoff;
		}

		return UMOFF_IS_NULL(umoff) ? -DER_NOSPACE : 0;
	}

	D_ASSERT(media == DAOS_MEDIA_NVME);

	vsi = ioc->ic_cont->vc_pool->vp_vea_info;
	D_ASSERT(vsi);
	hint_ctxt = ioc->ic_cont->vc_hint_ctxt[VOS_IOS_GENERIC];
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

	D_DEBUG(DB_TRACE, "media %hu offset "DF_U64" size %zd\n",
		biov->bi_addr.ba_type, biov->bi_addr.ba_off,
		bio_iov2len(biov));
	return 0;
}

/* Reserve single value record on specified media */
static int
vos_reserve_single(struct vos_io_context *ioc, uint16_t media,
		   daos_size_t size)
{
	struct vos_irec_df	*irec;
	daos_size_t		 scm_size;
	umem_off_t		 umoff;
	struct bio_iov		 biov;
	uint64_t		 off = 0;
	int			 rc;
	struct dcs_csum_info	*value_csum = vos_ioc2csum(ioc);

	/*
	 * TODO:
	 * To eliminate internal fragmentaion, misaligned record (record size
	 * isn't aligned with 4K) on NVMe could be split into two parts, large
	 * aligned part will be stored on NVMe and being referenced by
	 * vos_irec_df->ir_ex_addr, small unaligned part will be stored on SCM
	 * along with vos_irec_df, being referenced by vos_irec_df->ir_body.
	 */
	scm_size = (media == DAOS_MEDIA_SCM) ?
		vos_recx2irec_size(size, value_csum) :
		vos_recx2irec_size(0, value_csum);

	rc = vos_reserve(ioc, DAOS_MEDIA_SCM, scm_size, &off);
	if (rc) {
		D_ERROR("Reserve SCM for SV failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(ioc->ic_umoffs_cnt > 0);
	umoff = ioc->ic_umoffs[ioc->ic_umoffs_cnt - 1];
	irec = (struct vos_irec_df *) umem_off2ptr(vos_ioc2umm(ioc), umoff);
	vos_irec_init_csum(irec, value_csum);

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
		off = umoff + (payload_addr - (char *)irec);
	} else {
		rc = vos_reserve(ioc, DAOS_MEDIA_NVME, size, &off);
		if (rc) {
			D_ERROR("Reserve NVMe for SV failed. "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	}
done:
	bio_addr_set(&biov.bi_addr, media, off);
	bio_iov_set_len(&biov, size);
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
	if (size == 0 || media != DAOS_MEDIA_SCM) {
		ioc->ic_umoffs[ioc->ic_umoffs_cnt] = UMOFF_NULL;
		ioc->ic_umoffs_cnt++;
		if (size == 0) {
			bio_addr_set_hole(&biov.bi_addr, 1);
			goto done;
		}
	}

	/*
	 * TODO:
	 * To eliminate internal fragmentaion, misaligned recx (total recx size
	 * isn't aligned with 4K) on NVMe could be split into two evtree rects,
	 * larger rect will be stored on NVMe and small reminder on SCM.
	 */
	rc = vos_reserve(ioc, media, size, &off);
	if (rc) {
		D_ERROR("Reserve recx failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}
done:
	bio_addr_set(&biov.bi_addr, media, off);
	bio_iov_set_len(&biov, size);
	rc = iod_reserve(ioc, &biov);

	return rc;
}

/*
 * A simple media selection policy embedded in VOS, which select media by
 * akey type and record size.
 */
uint16_t
vos_media_select(struct vos_container *cont, daos_iod_type_t type,
		 daos_size_t size)
{
	struct vea_space_info *vsi = cont->vc_pool->vp_vea_info;

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

		media = vos_media_select(ioc->ic_cont, iod->iod_type, size);

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
dkey_update_begin(struct vos_io_context *ioc)
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
int
vos_publish_blocks(struct vos_container *cont, d_list_t *blk_list, bool publish,
		   enum vos_io_stream ios)
{
	struct vea_space_info	*vsi;
	struct vea_hint_context	*hint_ctxt;
	int			 rc;

	if (d_list_empty(blk_list))
		return 0;

	vsi = cont->vc_pool->vp_vea_info;
	D_ASSERT(vsi);
	hint_ctxt = cont->vc_hint_ctxt[ios];
	D_ASSERT(hint_ctxt);

	rc = publish ? vea_tx_publish(vsi, hint_ctxt, blk_list) :
		       vea_cancel(vsi, hint_ctxt, blk_list);
	if (rc)
		D_ERROR("Error on %s NVMe reservations. "DF_RC"\n",
			publish ? "publish" : "cancel", DP_RC(rc));

	return rc;
}

static void
update_cancel(struct vos_io_context *ioc)
{

	/* Cancel SCM reservations or free persistent allocations */
	if (ioc->ic_actv_at != 0) {
		D_ASSERT(ioc->ic_actv != NULL);
		umem_cancel(vos_ioc2umm(ioc), ioc->ic_actv, ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
	} else if (ioc->ic_umoffs_cnt != 0 && ioc->ic_actv_cnt == 0) {
		struct umem_instance *umem = vos_ioc2umm(ioc);
		int i;

		D_ASSERT(umem->umm_id == UMEM_CLASS_VMEM);

		for (i = 0; i < ioc->ic_umoffs_cnt; i++) {
			if (!UMOFF_IS_NULL(ioc->ic_umoffs[i]))
				umem_free(umem, ioc->ic_umoffs[i]);
		}
	}

	/* Cancel NVMe reservations */
	vos_publish_blocks(ioc->ic_cont, &ioc->ic_blk_exts, false,
			   VOS_IOS_GENERIC);
}

int
vos_update_end(daos_handle_t ioh, uint32_t pm_ver, daos_key_t *dkey, int err,
	       struct dtx_handle *dth)
{
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);
	struct umem_instance	*umem;

	D_ASSERT(ioc->ic_update);

	if (err != 0)
		goto out;

	umem = vos_ioc2umm(ioc);

	err = umem_tx_begin(umem, vos_txd_get());
	if (err)
		goto out;

	vos_dth_set(dth);

	err = vos_obj_hold(vos_obj_cache_current(), ioc->ic_cont, ioc->ic_oid,
			  &ioc->ic_epr, false, DAOS_INTENT_UPDATE, true,
			  &ioc->ic_obj);
	if (err != 0)
		goto abort;

	/* Commit the CoS DTXs via the IO PMDK transaction. */
	if (dth != NULL && dth->dth_dti_cos_count > 0 &&
	    dth->dth_dti_cos_done == 0) {
		vos_dtx_commit_internal(ioc->ic_obj->obj_cont, dth->dth_dti_cos,
					dth->dth_dti_cos_count, 0);
		dth->dth_dti_cos_done = 1;
	}

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
	err = vos_publish_blocks(ioc->ic_cont, &ioc->ic_blk_exts, true,
				 VOS_IOS_GENERIC);

	if (dth != NULL && err == 0)
		err = vos_dtx_prepared(dth);

abort:
	err = err ? umem_tx_abort(umem, err) : umem_tx_commit(umem);
out:
	if (err != 0) {
		vos_dtx_cleanup_dth(dth);
		update_cancel(ioc);
	}
	vos_ioc_destroy(ioc, err != 0);
	vos_dth_set(NULL);

	return err;
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		 struct dcs_iod_csums *iods_csums, daos_handle_t *ioh,
		 struct dtx_handle *dth)
{
	struct vos_io_context	*ioc;
	int			 rc;

	D_DEBUG(DB_TRACE,
		"Prepare IOC for "DF_UOID", iod_nr %d, epc "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, iods_csums,
			    false, &ioc);
	if (rc != 0)
		goto done;

	rc = dkey_update_begin(ioc);
	if (rc != 0) {
		D_ERROR(DF_UOID"dkey update begin failed. %d\n", DP_UOID(oid),
			rc);
		vos_update_end(vos_ioc2ioh(ioc), 0, dkey, rc, dth);
		goto done;
	}
	*ioh = vos_ioc2ioh(ioc);
done:
	return rc;
}

struct bio_desc *
vos_ioh2desc(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	D_ASSERT(ioc->ic_biod != NULL);
	return ioc->ic_biod;
}

struct dcs_csum_info *
vos_ioh2ci(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	return ioc->ic_biov_csums;
}

uint32_t
vos_ioh2ci_nr(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	return ioc->ic_biov_csums_at;
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
vos_obj_copy(struct vos_io_context *ioc, d_sg_list_t *sgls,
	     unsigned int sgl_nr)
{
	int rc, err;

	D_ASSERT(sgl_nr == ioc->ic_iod_nr);
	rc = bio_iod_prep(ioc->ic_biod);
	if (rc)
		return rc;

	err = bio_iod_copy(ioc->ic_biod, sgls, sgl_nr, false);
	rc = bio_iod_post(ioc->ic_biod);

	return err ? err : rc;
}

int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uint32_t pm_ver, daos_key_t *dkey, unsigned int iod_nr,
	       daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
	       d_sg_list_t *sgls)
{
	daos_handle_t ioh;
	int rc;

	rc = vos_update_begin(coh, oid, epoch, dkey, iod_nr, iods, iods_csums,
			      &ioh, NULL);
	if (rc) {
		D_ERROR("Update "DF_UOID" failed "DF_RC"\n", DP_UOID(oid),
			DP_RC(rc));
		return rc;
	}

	if (sgls) {
		rc = vos_obj_copy(vos_ioh2ioc(ioh), sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed "DF_RC"\n", DP_UOID(oid),
				DP_RC(rc));
	}

	rc = vos_update_end(ioh, pm_ver, dkey, rc, NULL);
	return rc;
}

int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
	      d_sg_list_t *sgls)
{
	daos_handle_t ioh;
	bool size_fetch = (sgls == NULL);
	int rc;

	rc = vos_fetch_begin(coh, oid, epoch, dkey, iod_nr, iods, size_fetch,
			     &ioh);
	if (rc) {
		if (rc == -DER_INPROGRESS)
			D_DEBUG(DB_TRACE, "Cannot fetch "DF_UOID" because of "
				"conflict modification: "DF_RC"\n",
				DP_UOID(oid), DP_RC(rc));
		else
			D_ERROR("Fetch "DF_UOID" failed "DF_RC"\n",
				DP_UOID(oid), DP_RC(rc));
		return rc;
	}

	if (!size_fetch) {
		struct vos_io_context *ioc = vos_ioh2ioc(ioh);
		int i, j;

		for (i = 0; i < iod_nr; i++) {
			struct bio_sglist *bsgl = bio_iod_sgl(ioc->ic_biod, i);
			d_sg_list_t *sgl = &sgls[i];

			/* Inform caller the nonexistent of object/key */
			if (bsgl->bs_nr_out == 0) {
				for (j = 0; j < sgl->sg_nr; j++)
					sgl->sg_iovs[j].iov_len = 0;
			}
		}

		rc = vos_obj_copy(ioc, sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed "DF_RC"\n",
				DP_UOID(oid), DP_RC(rc));
	}

	rc = vos_fetch_end(ioh, rc);
	return rc;
}

/**
 * @} vos_obj_update() & vos_obj_fetch() functions
 */
