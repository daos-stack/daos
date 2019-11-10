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
#include <daos/checksum.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <daos.h>
#include "vos_internal.h"
#include "evt_priv.h"

/** I/O context */
struct vos_io_context {
	daos_epoch_t		 ic_epoch;
	daos_unit_oid_t		 ic_oid;
	struct vos_container	*ic_cont;
	daos_iod_t		*ic_iods;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** BIO descriptor, has ic_iod_nr SGLs */
	struct bio_desc		*ic_biod;
	/** current dkey info */
	struct vos_krec_df	*ic_dkey_krec;
	daos_handle_t		 ic_dkey_loh;
	struct ilog_entries	 ic_dkey_entries;
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
vos_ioc_destroy(struct vos_io_context *ioc)
{
	if (ioc->ic_biod != NULL)
		bio_iod_free(ioc->ic_biod);

	if (ioc->ic_obj)
		vos_obj_release(vos_obj_cache_current(), ioc->ic_obj);

	vos_ioc_reserve_fini(ioc);
	ilog_fetch_finish(&ioc->ic_dkey_entries);
	vos_cont_decref(ioc->ic_cont);
	D_FREE(ioc);
}

static int
vos_ioc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr, daos_iod_t *iods,
	       bool size_fetch, struct vos_io_context **ioc_pp)
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
	ioc->ic_epoch = epoch;
	ioc->ic_oid = oid;
	ioc->ic_cont = vos_hdl2cont(coh);
	vos_cont_addref(ioc->ic_cont);
	ioc->ic_update = !read_only;
	ioc->ic_size_fetch = size_fetch;
	ioc->ic_actv = NULL;
	ioc->ic_actv_cnt = ioc->ic_actv_at = 0;
	ioc->ic_umoffs_cnt = ioc->ic_umoffs_at = 0;
	ioc->ic_dkey_krec = NULL;
	ioc->ic_dkey_loh = DAOS_HDL_INVAL;
	ilog_fetch_init(&ioc->ic_dkey_entries);
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
akey_fetch_single(daos_handle_t toh, const daos_epoch_range_t *epr,
		  daos_size_t *rsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 kiov; /* iov to carry key bundle */
	d_iov_t			 riov; /* iov to carray record bundle */
	struct bio_iov		 biov; /* iov to return data buffer */
	int			 rc;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= epr->epr_hi;

	tree_rec_bundle2iov(&rbund, &riov);
	memset(&biov, 0, sizeof(biov));
	rbund.rb_biov	= &biov;
	rbund.rb_csum	= &iod->iod_csums[0];

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
	/* Get the iod_csum pointer and manipulate the checksum value
	 * for fault injection.
	 */
	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
		rbund.rb_csum->cs_csum[0] += 2;

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
akey_fetch_recx(daos_handle_t toh, const daos_epoch_range_t *epr,
		daos_recx_t *recx, daos_csum_buf_t *csum, daos_size_t *rsize_p,
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
	int			 rc;
	int			 csum_copied;

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
	csum_copied = 0;
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

		if (dcb_is_valid(csum) &&
		    csum_copied * ent->en_csum.cs_len < csum->cs_buf_len &&
		    csum->cs_chunksize > 0) {
			uint8_t	    *csum_ptr;
			daos_size_t  csum_nr;

			D_ASSERT(lo >= recx->rx_idx);
			/** Make sure the entity csum matches expected csum */
			D_ASSERT(csum->cs_len == ent->en_csum.cs_len);
			D_ASSERT(csum->cs_type == ent->en_csum.cs_type);
			D_ASSERT(csum->cs_chunksize ==
				 ent->en_csum.cs_chunksize);

			/** Note: only need checksums for requested data, not
			 * necessarily all checksums from the entire recx entry
			 */
			csum_ptr = dcb_off2csum(csum,
				(uint32_t)((lo - recx->rx_idx) * rsize));
			csum_nr = csum_chunk_count(csum->cs_chunksize,
						   lo, hi, rsize);
			memcpy(csum_ptr, ent->en_csum.cs_csum,
			       csum_nr * ent->en_csum.cs_len);
			if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
				csum_ptr[0] += 2; /* poison the checksum */

			csum_copied += csum_nr;
			D_ASSERT(csum_copied <= csum->cs_nr);
		}

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

int
key_ilog_fetch(struct vos_object *obj, uint32_t intent,
	       const daos_epoch_range_t *epr, struct vos_krec_df *krec,
	       struct ilog_entries *entries)
{
	daos_handle_t		 loh;
	struct ilog_desc_cbs	 cbs;
	struct umem_instance	*umm = vos_obj2umm(obj);
	int			 rc;

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(obj->obj_cont));
	rc = ilog_open(umm, &krec->kr_ilog, &cbs, &loh);

	if (rc != 0) {
		D_ERROR("Could not open ilog: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = ilog_fetch(loh, intent, entries);
	if (rc == -DER_NONEXIST)
		goto out;
	if (rc != 0)
		D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
			 "Could not fetch ilog: "DF_RC"\n", DP_RC(rc));
out:
	ilog_close(loh);

	return rc;
}

static int
key_ilog_update_range(struct vos_io_context *ioc, struct ilog_entries *entries,
		      daos_epoch_range_t *epr)
{
	struct ilog_entry	*entry;
	bool			 has_updates = false;

	ilog_foreach_entry_reverse(entries, entry) {
		if (entry->ie_status == ILOG_REMOVED)
			continue;

		if (entry->ie_id.id_epoch > epr->epr_hi)
			continue; /* skip newer entries */

		if (entry->ie_punch) {
			if (entry->ie_status == ILOG_UNCOMMITTED)
				/* A key punch is in-flight */
				return -DER_INPROGRESS;

			if (entry->ie_id.id_epoch < epr->epr_lo)
				break; /* entry is outside of range */

			/* Add 1 to ignore the punched epoch */
			epr->epr_lo = entry->ie_id.id_epoch + 1;
			break;
		}

		/* Even if the key is unavailable, we still don't know if the
		 * value is in progress or non-existent so let it continue
		 */
		has_updates = true;

		if (entry->ie_id.id_epoch < epr->epr_lo)
			break; /* entry is outside of range */
	}

	if (!has_updates)
		return -DER_NONEXIST;

	return 0;
}

static int
key_ilog_check(struct vos_io_context *ioc, int prior_rc,
	       const daos_epoch_range_t *orig_epr, struct vos_krec_df *krec,
	       struct ilog_entries *entries, daos_epoch_range_t *update_epr,
	       daos_epoch_range_t *iod_eprs, int idx)
{
	daos_epoch_t		 hi;
	int			 rc;

	hi = ioc->ic_epoch;
	if (iod_eprs && iod_eprs[idx].epr_lo != 0)
		hi = iod_eprs[idx].epr_lo;

	if (update_epr->epr_hi == hi)
		return prior_rc;

	*update_epr = *orig_epr;
	/* This checks to make sure we always stay in range */
	D_ASSERT(orig_epr->epr_hi >= hi);
	update_epr->epr_hi = hi;

	rc = key_ilog_update_range(ioc, &ioc->ic_dkey_entries,
				   update_epr);
	if (rc == 0)
		rc = key_ilog_update_range(ioc, entries, update_epr);

	return rc;
}

static int
akey_fetch(struct vos_io_context *ioc, const daos_epoch_range_t *epr,
	   daos_handle_t ak_toh)
{
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct vos_krec_df	*krec = NULL;
	struct	ilog_entries	 entries;
	daos_epoch_range_t	 val_epr = {0};
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i, rc;
	int			 flags = 0;
	int			 prior_rc;
	bool			 is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	D_DEBUG(DB_IO, "akey "DF_KEY" fetch %s eph "DF_U64"\n",
		DP_KEY(&iod->iod_name),
		iod->iod_type == DAOS_IOD_ARRAY ? "array" : "single",
		ioc->ic_epoch);

	if (is_array)
		flags |= SUBTR_EVT;

	ilog_fetch_init(&entries);

	rc = key_tree_prepare(ioc->ic_obj, ak_toh,
			      VOS_BTR_AKEY, &iod->iod_name, flags,
			      DAOS_INTENT_DEFAULT, &krec, &toh);

	if (rc == 0)
		rc = key_ilog_fetch(ioc->ic_obj, DAOS_INTENT_DEFAULT, epr,
				    krec, &entries);

	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_IO, "Nonexistent akey %.*s\n",
				(int)iod->iod_name.iov_len,
				(char *)iod->iod_name.iov_buf);
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
			rc = 0;
		}
		goto out;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = key_ilog_check(ioc, 0, epr, krec, &entries, &val_epr,
				    iod->iod_eprs, 0);

		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				D_DEBUG(DB_IO, "Nonexistent akey %.*s\n",
					(int)iod->iod_name.iov_len,
					(char *)iod->iod_name.iov_buf);
				iod_empty_sgl(ioc, ioc->ic_sgl_at);
				rc = 0;
				goto out;
			}

			if (rc == -DER_INPROGRESS)
				D_DEBUG(DB_TRACE, "Cannot fetch akey because of"
					" conflicting modification\n");
			else
				D_ERROR("Fetch akey failed: rc="DF_RC"\n",
					DP_RC(rc));
			goto out;
		}

		rc = akey_fetch_single(toh, &val_epr, &iod->iod_size, ioc);

		goto out;
	}

	iod->iod_size = 0;
	prior_rc = 0;
	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t rsize;
		prior_rc = rc = key_ilog_check(ioc, prior_rc, epr, krec,
					       &entries, &val_epr,
					       iod->iod_eprs, i);
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_IO, "Nonexistent akey %.*s\n",
				(int)iod->iod_name.iov_len,
				(char *)iod->iod_name.iov_buf);
			rc = 0;
			continue;
		}

		if (rc != 0) {
			if (rc == -DER_INPROGRESS)
				D_DEBUG(DB_TRACE, "Cannot fetch akey because"
					" of conflicting modification\n");
			else
				D_ERROR("Fetch akey failed: rc="DF_RC"\n",
					DP_RC(rc));
			goto out;
		}

		D_DEBUG(DB_IO, "fetch %d eph "DF_U64"\n", i, val_epr.epr_hi);
		rc = akey_fetch_recx(toh, &val_epr, &iod->iod_recxs[i],
				     daos_iod_csum(iod, i), &rsize, ioc);
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

	ilog_fetch_finish(&entries);
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
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	daos_epoch_range_t	 epr = {0, ioc->ic_epoch};
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY,
			      dkey, 0, DAOS_INTENT_DEFAULT, &ioc->ic_dkey_krec,
			      &toh);

	if (rc == 0)
		rc = key_ilog_fetch(ioc->ic_obj, DAOS_INTENT_DEFAULT, &epr,
				    ioc->ic_dkey_krec, &ioc->ic_dkey_entries);

	if (rc == -DER_NONEXIST) {
		for (i = 0; i < ioc->ic_iod_nr; i++)
			iod_empty_sgl(ioc, i);
		D_DEBUG(DB_IO, "Nonexistent dkey\n");
		rc = 0;
		goto out;
	}

	if (rc != 0) {
		D_ERROR("Failed to prepare subtree: %d\n", rc);
		goto out;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_fetch(ioc, &epr, toh);
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

	rc = vos_obj_hold(vos_obj_cache_current(), ioc->ic_cont, oid, epoch,
			  true, DAOS_INTENT_DEFAULT, &ioc->ic_obj);
	if (rc != 0)
		goto error;

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

static umem_off_t
iod_update_umoff(struct vos_io_context *ioc)
{
	umem_off_t umoff;

	D_ASSERT(ioc->ic_umoffs_at < ioc->ic_umoffs_cnt);
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
akey_update_single(daos_handle_t toh, daos_epoch_t epoch, uint32_t pm_ver,
		   daos_size_t rsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_csum_buf_t		 csum;
	d_iov_t			 kiov, riov;
	struct bio_iov		*biov;
	umem_off_t		 umoff;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	int			 rc;

	dcb_set_null(&csum);
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
		if (iod->iod_csums)
			rbund.rb_csum	= &iod->iod_csums[0];
		else
			rbund.rb_csum	= &csum;
	}

	rbund.rb_biov	= biov;
	rbund.rb_rsize	= rsize;
	rbund.rb_off	= umoff;
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
		 daos_recx_t *recx, daos_csum_buf_t *iod_csum,
		 daos_size_t rsize,
		 struct vos_io_context *ioc)
{
	struct evt_entry_in ent;
	struct bio_iov *biov;
	int rc;

	D_ASSERT(recx->rx_nr > 0);
	memset(&ent, 0, sizeof(ent));
	ent.ei_rect.rc_epc = epoch;
	ent.ei_rect.rc_ex.ex_lo = recx->rx_idx;
	ent.ei_rect.rc_ex.ex_hi = recx->rx_idx + recx->rx_nr - 1;
	ent.ei_ver = pm_ver;
	ent.ei_inob = rsize;

	if (dcb_is_valid(iod_csum)) {
		ent.ei_csum = *iod_csum;
		/* change the checksum for fault injection*/
		if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_UPDATE_FAIL))
			ent.ei_csum.cs_csum[0] += 1;
	}

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
key_ilog_update(struct vos_io_context *ioc, daos_handle_t loh,
		daos_epoch_t *epoch, const daos_epoch_range_t *eprs, int idx,
		daos_epoch_range_t *max_epr)
{
	int			 rc;

	if (!eprs || eprs[idx].epr_lo == 0 || *epoch == eprs[idx].epr_lo)
		return 0;

	*epoch = eprs[idx].epr_lo;
	update_bounds(max_epr, &eprs[idx]);

	rc =  ilog_update(loh, *epoch, false);
	if (rc == 0)
		rc = ilog_update(ioc->ic_dkey_loh, *epoch, false);

	return rc;
}

static int
akey_update(struct vos_io_context *ioc, uint32_t pm_ver,
	    daos_handle_t ak_toh, daos_epoch_range_t *dkey_epr)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_krec_df	*krec = NULL;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	daos_handle_t		 loh = DAOS_HDL_INVAL;
	bool			 is_array = (iod->iod_type == DAOS_IOD_ARRAY);
	int			 flags = SUBTR_CREATE;
	daos_epoch_t		 epoch = 0;
	struct ilog_desc_cbs	 cbs;
	daos_epoch_range_t	 akey_epr = {ioc->ic_epoch, ioc->ic_epoch};
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i;
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "akey "DF_KEY" update %s value eph "DF_U64"\n",
		DP_KEY(&iod->iod_name), is_array ? "array" : "single",
		ioc->ic_epoch);

	if (is_array)
		flags |= SUBTR_EVT;

	rc = key_tree_prepare(obj, ak_toh, VOS_BTR_AKEY,
			      &iod->iod_name, flags, DAOS_INTENT_UPDATE,
			      &krec, &toh);
	if (rc != 0)
		return rc;

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(ioc->ic_obj->obj_cont));
	rc = ilog_open(vos_ioc2umm(ioc), &krec->kr_ilog, &cbs, &loh);
	if (rc != 0)
		return rc;

	rc = key_ilog_update(ioc, loh, &epoch, &akey_epr, 0, &akey_epr);
	if (rc != 0) {
		D_ERROR("Failed to update ilog: rc = %s\n",
			d_errstr(rc));
		goto out;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = key_ilog_update(ioc, loh, &epoch, iod->iod_eprs, 0,
				     &akey_epr);
		if (rc != 0) {
			D_ERROR("Failed to update ilog: rc = %s\n",
				d_errstr(rc));
			goto out;
		}

		D_DEBUG(DB_IO, "Single update eph "DF_U64"\n", epoch);
		rc = akey_update_single(toh, epoch, pm_ver, iod->iod_size, ioc);
		if (rc)
			goto failed;
		goto out;
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		rc = key_ilog_update(ioc, loh, &epoch, iod->iod_eprs, i,
				     &akey_epr);
		if (rc != 0) {
			D_ERROR("Failed to update ilog: rc = %s\n",
				d_errstr(rc));
			goto out;
		}

		D_DEBUG(DB_IO, "Array update %d eph "DF_U64"\n", i, epoch);
		daos_csum_buf_t *csum = daos_iod_csum(iod, i);
		rc = akey_update_recx(toh, epoch, pm_ver, &iod->iod_recxs[i],
				      csum, iod->iod_size, ioc);
		if (rc != 0)
			goto failed;
	}
out:
	update_bounds(dkey_epr, &akey_epr);
failed:
	if (!daos_handle_is_inval(loh))
		ilog_close(loh);

	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, is_array);

	return rc;
}

static int
dkey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_key_t *dkey)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_obj_df	*obj_df;
	daos_epoch_range_t	 dkey_epr = {ioc->ic_epoch, ioc->ic_epoch};
	daos_handle_t		 ak_toh;
	struct ilog_desc_cbs	 cbs;
	bool			 subtr_created = false;
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY, dkey,
			      SUBTR_CREATE, DAOS_INTENT_UPDATE,
			      &ioc->ic_dkey_krec, &ak_toh);
	if (rc != 0) {
		D_ERROR("Error preparing dkey tree: rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}
	subtr_created = true;

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(ioc->ic_obj->obj_cont));
	rc = ilog_open(vos_ioc2umm(ioc), &ioc->ic_dkey_krec->kr_ilog, &cbs,
		       &ioc->ic_dkey_loh);
	if (rc != 0) {
		D_ERROR("Error opening dkey ilog: rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);

		rc = akey_update(ioc, pm_ver, ak_toh, &dkey_epr);
		if (rc != 0)
			goto out;
	}

out:
	if (!subtr_created)
		return rc;

	if (!daos_handle_is_inval(ioc->ic_dkey_loh))
		ilog_close(ioc->ic_dkey_loh);

	if (rc != 0)
		goto release;

	obj_df = obj->obj_df;
	D_ASSERT(obj_df != NULL);
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
	struct vos_irec_df	*irec;
	daos_size_t		 scm_size;
	umem_off_t		 umoff;
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

	D_ASSERT(ioc->ic_umoffs_cnt > 0);
	umoff = ioc->ic_umoffs[ioc->ic_umoffs_cnt - 1];
	irec = (struct vos_irec_df *) umem_off2ptr(vos_ioc2umm(ioc), umoff);
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
		off = umoff + (payload_addr - (char *)irec);
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
		ioc->ic_umoffs[ioc->ic_umoffs_cnt] = UMOFF_NULL;
		ioc->ic_umoffs_cnt++;
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
		umem_cancel(vos_ioc2umm(ioc), ioc->ic_actv, ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
	} else if (ioc->ic_umoffs_cnt != 0) {
		struct umem_instance *umem = vos_ioc2umm(ioc);
		int i, rc;

		rc = umem_tx_begin(umem, vos_txd_get());
		if (rc) {
			D_ERROR("TX start for update rollback: %d\n", rc);
			return;
		}

		for (i = 0; i < ioc->ic_umoffs_cnt; i++) {
			if (UMOFF_IS_NULL(ioc->ic_umoffs[i]))
				continue;
			umem_free(umem, ioc->ic_umoffs[i]);
		}

		rc =  umem_tx_commit(umem);
		if (rc) {
			D_ERROR("TX commit for update rollback: %d\n", rc);
			return;
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
			   ioc->ic_epoch, false, DAOS_INTENT_UPDATE,
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
	if (err != 0)
		update_cancel(ioc);
	vos_ioc_destroy(ioc);
	vos_dth_set(NULL);

	return err;
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		 daos_handle_t *ioh, struct dtx_handle *dth)
{
	struct vos_io_context	*ioc;
	int			 rc;

	D_DEBUG(DB_IO, "Prepare IOC for "DF_UOID", iod_nr %d, epc "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, false, &ioc);
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

	err = bio_iod_copy(ioc->ic_biod, sgls, sgl_nr);
	rc = bio_iod_post(ioc->ic_biod);

	return err ? err : rc;
}

int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uint32_t pm_ver, daos_key_t *dkey, unsigned int iod_nr,
	       daos_iod_t *iods, d_sg_list_t *sgls)
{
	daos_handle_t ioh;
	int rc;

	rc = vos_update_begin(coh, oid, epoch, dkey, iod_nr, iods, &ioh, NULL);
	if (rc) {
		D_ERROR("Update "DF_UOID" failed %d\n", DP_UOID(oid), rc);
		return rc;
	}

	if (sgls) {
		rc = vos_obj_copy(vos_ioh2ioc(ioh), sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed %d\n", DP_UOID(oid), rc);
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

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_fetch_begin(coh, oid, epoch, dkey, iod_nr, iods, size_fetch,
			     &ioh);
	if (rc) {
		if (rc == -DER_INPROGRESS)
			D_DEBUG(DB_TRACE, "Cannot fetch "DF_UOID" because of "
				"conflict modification: %d\n",
				DP_UOID(oid), rc);
		else
			D_ERROR("Fetch "DF_UOID" failed %d\n",
				DP_UOID(oid), rc);
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
			D_ERROR("Copy "DF_UOID" failed %d\n",
				DP_UOID(oid), rc);
	}

	rc = vos_fetch_end(ioh, rc);
	return rc;
}

/**
 * @} vos_obj_update() & vos_obj_fetch() functions
 */
