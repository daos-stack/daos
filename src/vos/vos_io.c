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
	/** The epoch bound including uncertainty */
	daos_epoch_t		 ic_bound;
	daos_epoch_range_t	 ic_epr;
	daos_unit_oid_t		 ic_oid;
	struct vos_container	*ic_cont;
	daos_iod_t		*ic_iods;
	struct dcs_iod_csums	*iod_csums;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** BIO descriptor, has ic_iod_nr SGLs */
	struct bio_desc		*ic_biod;
	struct vos_ts_set	*ic_ts_set;
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
	struct vos_rsrvd_scm	*ic_rsrvd_scm;
	/** reserved offsets for SCM update */
	umem_off_t		*ic_umoffs;
	unsigned int		 ic_umoffs_cnt;
	unsigned int		 ic_umoffs_at;
	/** reserved NVMe extents */
	d_list_t		 ic_blk_exts;
	daos_size_t		 ic_space_held[DAOS_MEDIA_MAX];
	/** number DAOS IO descriptors */
	unsigned int		 ic_iod_nr;
	/** deduplication threshold size */
	uint32_t		 ic_dedup_th;
	/** dedup entries to be inserted after transaction done */
	d_list_t		 ic_dedup_entries;
	/** flags */
	unsigned int		 ic_update:1,
				 ic_size_fetch:1,
				 ic_save_recx:1,
				 ic_dedup:1, /** candidate for dedup */
				 ic_read_ts_only:1,
				 ic_check_existence:1,
				 ic_remove:1;
	/**
	 * Input shadow recx lists, one for each iod. Now only used for degraded
	 * mode EC obj fetch handling.
	 */
	struct daos_recx_ep_list *ic_shadows;
	/**
	 * Output recx/epoch lists, one for each iod. To save the recx list when
	 * vos_fetch_begin() with VOS_OF_FETCH_RECX_LIST flag. User can get it
	 * by vos_ioh2recx_list() and shall free it by daos_recx_ep_list_free().
	 */
	struct daos_recx_ep_list *ic_recx_lists;
};

static inline daos_size_t
recx_csum_len(daos_recx_t *recx, struct dcs_csum_info *csum,
	      daos_size_t rsize)
{
	if (!ci_is_valid(csum) || rsize == 0)
		return 0;
	return (daos_size_t)csum->cs_len * csum_chunk_count(csum->cs_chunksize,
			recx->rx_idx, recx->rx_idx + recx->rx_nr - 1, rsize);
}

struct dedup_entry {
	d_list_t	 de_link;
	uint8_t		*de_csum_buf;
	uint16_t	 de_csum_type;
	int		 de_csum_len;
	bio_addr_t	 de_addr;
	size_t           de_data_len;
	int		 de_ref;
};

static inline struct dedup_entry *
dedup_rlink2entry(d_list_t *rlink)
{
	return container_of(rlink, struct dedup_entry, de_link);
}

static bool
dedup_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	      const void *key, unsigned int csum_len)
{
	struct dedup_entry	*entry = dedup_rlink2entry(rlink);
	struct dcs_csum_info	*csum = (struct dcs_csum_info *)key;

	D_ASSERT(entry->de_csum_len != 0);
	D_ASSERT(csum_len != 0);

	/** different containers might use different checksum algorithm */
	if (entry->de_csum_type != csum->cs_type)
		return false;

	/** overall checksum size (for all chunks) should match */
	if (entry->de_csum_len != csum_len)
		return false;

	D_ASSERT(csum->cs_csum != NULL);
	D_ASSERT(entry->de_csum_buf != NULL);

	return memcmp(entry->de_csum_buf, csum->cs_csum, csum_len) == 0;
}

static uint32_t
dedup_key_hash(struct d_hash_table *htable, const void *key,
	       unsigned int csum_len)
{
	struct dcs_csum_info	*csum = (struct dcs_csum_info *)key;

	D_ASSERT(csum_len != 0);
	D_ASSERT(csum->cs_csum != NULL);

	return d_hash_string_u32((const char *)csum->cs_csum, csum_len);
}

static void
dedup_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dedup_entry	*entry = dedup_rlink2entry(rlink);

	entry->de_ref++;
}

static bool
dedup_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dedup_entry	*entry = dedup_rlink2entry(rlink);

	D_ASSERT(entry->de_ref > 0);
	entry->de_ref--;

	return entry->de_ref == 0;
}

static void
dedup_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dedup_entry	*entry = dedup_rlink2entry(rlink);

	D_ASSERT(entry->de_ref == 0);
	D_ASSERT(entry->de_csum_buf != NULL);

	D_FREE(entry->de_csum_buf);
	D_FREE(entry);
}

static d_hash_table_ops_t dedup_hash_ops = {
	.hop_key_cmp	= dedup_key_cmp,
	.hop_key_hash	= dedup_key_hash,
	.hop_rec_addref	= dedup_rec_addref,
	.hop_rec_decref	= dedup_rec_decref,
	.hop_rec_free	= dedup_rec_free,
};

int
vos_dedup_init(struct vos_pool *pool)
{
	int	rc;

	rc = d_hash_table_create(D_HASH_FT_NOLOCK, 13, /* 8k buckets */
				 NULL, &dedup_hash_ops,
				 &pool->vp_dedup_hash);

	if (rc)
		D_ERROR(DF_UUID": Init dedup hash failed. "DF_RC".\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
	return rc;
}

void
vos_dedup_fini(struct vos_pool *pool)
{
	if (pool->vp_dedup_hash) {
		d_hash_table_destroy(pool->vp_dedup_hash, true);
		pool->vp_dedup_hash = NULL;
	}
}

void
vos_dedup_invalidate(struct vos_pool *pool)
{
	vos_dedup_fini(pool);
	vos_dedup_init(pool);
}

static bool
vos_dedup_lookup(struct vos_pool *pool, struct dcs_csum_info *csum,
		 daos_size_t csum_len, struct bio_iov *biov)
{
	struct dedup_entry	*entry;
	d_list_t		*rlink;

	if (!ci_is_valid(csum))
		return false;

	rlink = d_hash_rec_find(pool->vp_dedup_hash, csum, csum_len);
	if (rlink == NULL)
		return false;

	entry = dedup_rlink2entry(rlink);
	if (biov) {
		biov->bi_addr = entry->de_addr;
		biov->bi_addr.ba_dedup = true;
		biov->bi_data_len = entry->de_data_len;
		D_DEBUG(DB_IO, "Found dedup entry\n");
	}

	D_ASSERT(entry->de_ref > 1);

	d_hash_rec_decref(pool->vp_dedup_hash, rlink);

	return true;
}

static void
vos_dedup_update(struct vos_pool *pool, struct dcs_csum_info *csum,
		 daos_size_t csum_len, struct bio_iov *biov, d_list_t *list)
{
	struct dedup_entry	*entry;

	if (!ci_is_valid(csum) || csum_len == 0 || biov->bi_addr.ba_dedup)
		return;

	if (bio_addr_is_hole(&biov->bi_addr))
		return;

	if (vos_dedup_lookup(pool, csum, csum_len, NULL))
		return;

	D_ALLOC_PTR(entry);
	if (entry == NULL) {
		D_ERROR("Failed to allocate dedup entry\n");
		return;
	}
	D_INIT_LIST_HEAD(&entry->de_link);

	D_ASSERT(csum_len != 0);
	D_ALLOC(entry->de_csum_buf, csum_len);
	if (entry->de_csum_buf == NULL) {
		D_ERROR("Failed to allocate csum buf "DF_U64"\n", csum_len);
		D_FREE(entry);
		return;
	}
	entry->de_csum_len	= csum_len;
	entry->de_csum_type	= csum->cs_type;
	entry->de_addr		= biov->bi_addr;
	entry->de_data_len	= biov->bi_data_len;
	memcpy(entry->de_csum_buf, csum->cs_csum, csum_len);

	d_list_add_tail(&entry->de_link, list);
	D_DEBUG(DB_IO, "Inserted dedup entry in list\n");
}

static void
vos_dedup_process(struct vos_pool *pool, d_list_t *list, bool abort)
{
	struct dedup_entry	*entry, *tmp;
	struct dcs_csum_info	 csum = { 0 };
	int			 rc;

	d_list_for_each_entry_safe(entry, tmp, list, de_link) {
		d_list_del_init(&entry->de_link);

		if (abort)
			goto free_entry;

		/*
		 * No yield since vos_dedup_update() is called, so it's safe
		 * to insert entries to hash without checking.
		 */
		csum.cs_csum = entry->de_csum_buf;
		csum.cs_type = entry->de_csum_type;

		rc = d_hash_rec_insert(pool->vp_dedup_hash, &csum,
				       entry->de_csum_len, &entry->de_link,
				       false);
		if (rc == 0) {
			D_DEBUG(DB_IO, "Inserted dedup entry\n");
			continue;
		}
		D_ERROR("Insert dedup entry failed. "DF_RC"\n", DP_RC(rc));
free_entry:
		D_FREE(entry->de_csum_buf);
		D_FREE(entry);
	}
}

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
	/** is enabled and has csums (might not for punch) */
	if (ioc->iod_csums != NULL && ioc->iod_csums[ioc->ic_sgl_at].ic_nr > 0)
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
	if (ioc->ic_rsrvd_scm != NULL) {
		D_ASSERT(ioc->ic_rsrvd_scm->rs_actv_at == 0);
		D_FREE(ioc->ic_rsrvd_scm);
	}

	D_ASSERT(d_list_empty(&ioc->ic_blk_exts));
	D_ASSERT(d_list_empty(&ioc->ic_dedup_entries));
	if (ioc->ic_umoffs != NULL) {
		D_FREE(ioc->ic_umoffs);
		ioc->ic_umoffs = NULL;
	}
}

static int
vos_ioc_reserve_init(struct vos_io_context *ioc, struct dtx_handle *dth)
{
	struct vos_rsrvd_scm	*scm;
	size_t			 size;
	int			 total_acts = 0;
	int			 i;

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

	size = sizeof(*ioc->ic_rsrvd_scm) +
		sizeof(struct pobj_action) * total_acts;
	D_ALLOC(ioc->ic_rsrvd_scm, size);
	if (ioc->ic_rsrvd_scm == NULL)
		return -DER_NOMEM;

	ioc->ic_rsrvd_scm->rs_actv_cnt = total_acts;

	if (!dtx_is_valid_handle(dth) || dth->dth_deferred == NULL)
		return 0;

	/** Reserve enough space for any deferred actions */
	D_ALLOC(scm, size);
	if (scm == NULL) {
		D_FREE(ioc->ic_rsrvd_scm);
		return -DER_NOMEM;
	}

	scm->rs_actv_cnt = total_acts;
	dth->dth_deferred[dth->dth_deferred_cnt++] = scm;

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
	vos_ts_set_free(ioc->ic_ts_set);
	D_FREE(ioc);
}

static int
vos_ioc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr,
	       daos_iod_t *iods, struct dcs_iod_csums *iod_csums,
	       uint32_t vos_flags, struct daos_recx_ep_list *shadows,
	       bool dedup, uint32_t dedup_th,
	       struct dtx_handle *dth, struct vos_io_context **ioc_pp)
{
	struct vos_container	*cont;
	struct vos_io_context	*ioc = NULL;
	struct bio_io_context	*bioc;
	daos_epoch_t		 bound;
	uint64_t		 cflags = 0;
	int			 i, rc;

	if (iod_nr == 0 &&
	    !(vos_flags &
	      (VOS_OF_FETCH_SET_TS_ONLY | VOS_OF_FETCH_CHECK_EXISTENCE))) {
		D_ERROR("Invalid iod_nr (0).\n");
		rc = -DER_IO_INVAL;
		goto error;
	}

	D_ALLOC_PTR(ioc);
	if (ioc == NULL)
		return -DER_NOMEM;

	ioc->ic_iod_nr = iod_nr;
	ioc->ic_iods = iods;
	ioc->ic_epr.epr_hi = dtx_is_valid_handle(dth) ? dth->dth_epoch : epoch;
	bound = dtx_is_valid_handle(dth) ? dth->dth_epoch_bound : epoch;
	ioc->ic_bound = MAX(bound, ioc->ic_epr.epr_hi);
	ioc->ic_epr.epr_lo = 0;
	ioc->ic_oid = oid;
	ioc->ic_cont = vos_hdl2cont(coh);
	vos_cont_addref(ioc->ic_cont);
	ioc->ic_update = !read_only;
	ioc->ic_size_fetch = ((vos_flags & VOS_OF_FETCH_SIZE_ONLY) != 0);
	ioc->ic_save_recx = ((vos_flags & VOS_OF_FETCH_RECX_LIST) != 0);
	ioc->ic_dedup = dedup;
	ioc->ic_dedup_th = dedup_th;
	if (vos_flags & VOS_OF_FETCH_CHECK_EXISTENCE)
		ioc->ic_read_ts_only = ioc->ic_check_existence = 1;
	else if (vos_flags & VOS_OF_FETCH_SET_TS_ONLY)
		ioc->ic_read_ts_only = 1;
	ioc->ic_remove =
		((vos_flags & VOS_OF_REMOVE) != 0);
	ioc->ic_umoffs_cnt = ioc->ic_umoffs_at = 0;
	ioc->iod_csums = iod_csums;
	vos_ilog_fetch_init(&ioc->ic_dkey_info);
	vos_ilog_fetch_init(&ioc->ic_akey_info);
	D_INIT_LIST_HEAD(&ioc->ic_blk_exts);
	ioc->ic_shadows = shadows;
	D_INIT_LIST_HEAD(&ioc->ic_dedup_entries);

	rc = vos_ioc_reserve_init(ioc, dth);
	if (rc != 0)
		goto error;

	if (dtx_is_valid_handle(dth)) {
		if (read_only) {
			cflags = VOS_TS_READ_AKEY;
			if (vos_flags & VOS_OF_COND_DKEY_FETCH)
				cflags |= VOS_TS_READ_DKEY;
		} else {
			cflags = VOS_TS_WRITE_AKEY;
			if (vos_flags & VOS_COND_AKEY_UPDATE_MASK)
				cflags |= VOS_TS_READ_AKEY;
			/** This can be improved but for now, keep it simple.
			 *  It will mean updating read timestamps on any akeys
			 *  that don't have a condition set.
			 */
			if (vos_flags & VOS_OF_COND_PER_AKEY)
				cflags |= VOS_TS_READ_AKEY;
			if (vos_flags & VOS_COND_DKEY_UPDATE_MASK)
				cflags |= VOS_TS_READ_DKEY;
		}
	}

	rc = vos_ts_set_allocate(&ioc->ic_ts_set, vos_flags, cflags, iod_nr,
				 dtx_is_valid_handle(dth) ?
				 &dth->dth_xid : NULL);
	if (rc != 0)
		goto error;

	if (ioc->ic_read_ts_only || ioc->ic_check_existence) {
		*ioc_pp = ioc;
		return 0;
	}

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

		D_REALLOC_ARRAY(biovs, bsgl->bs_iovs, (iov_nr * 2));
		if (biovs == NULL)
			return -DER_NOMEM;

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
save_csum(struct vos_io_context *ioc, struct dcs_csum_info *csum_info,
	  struct evt_entry *entry, daos_size_t rec_size)
{
	struct dcs_csum_info	*saved_csum_info;
	int			 rc;

	if (ioc->ic_size_fetch)
		return 0;

	rc = bsgl_csums_resize(ioc);
	if (rc != 0)
		return rc;

	/**
	 * it's expected that the csum the csum_info points to is in memory
	 * that will persist until fetch is complete ... so memcpy isn't needed
	 */
	saved_csum_info = &ioc->ic_biov_csums[ioc->ic_biov_csums_at];
	*saved_csum_info = *csum_info;
	if (entry != NULL)
		evt_entry_csum_update(&entry->en_ext, &entry->en_sel_ext,
				      saved_csum_info, rec_size);

	ioc->ic_biov_csums_at++;

	return 0;
}

/** Fetch the single value within the specified epoch range of an key */
static int
akey_fetch_single(daos_handle_t toh, const daos_epoch_range_t *epr,
		  daos_size_t *rsize, struct vos_io_context *ioc)
{
	struct vos_svt_key	 key;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 kiov; /* iov to carry key bundle */
	d_iov_t			 riov; /* iov to carry record bundle */
	struct bio_iov		 biov; /* iov to return data buffer */
	int			 rc;
	struct dcs_csum_info	csum_info = {0};

	d_iov_set(&kiov, &key, sizeof(key));
	key.sk_epoch	= ioc->ic_bound;
	key.sk_minor_epc = VOS_MINOR_EPC_MAX;

	tree_rec_bundle2iov(&rbund, &riov);
	memset(&biov, 0, sizeof(biov));
	rbund.rb_biov	= &biov;
	rbund.rb_csum = &csum_info;

	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &kiov, &kiov,
			  &riov);
	if (vos_dtx_hit_inprogress())
		D_GOTO(out, rc = (rc == 0 ? -DER_INPROGRESS : rc));

	if (rc == -DER_NONEXIST) {
		rbund.rb_rsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
		rc = 0;
	} else if (rc != 0) {
		goto out;
	} else if (key.sk_epoch < epr->epr_lo) {
		/* The single value is before the valid epoch range (after a
		 * punch when incarnation log is available
		 */
		rc = 0;
		rbund.rb_rsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
	} else if (key.sk_epoch > epr->epr_hi) {
		/* Uncertainty violation */
		D_GOTO(out, rc = -DER_TX_RESTART);
	}

	if (ci_is_valid(&csum_info))
		save_csum(ioc, &csum_info, NULL, 0);

	rc = iod_fetch(ioc, &biov);
	if (rc != 0)
		goto out;

	*rsize = rbund.rb_gsize;
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

/**
 * Save to recx/ep list, user can get it by vos_ioh2recx_list() and then free
 * the memory.
 */
static int
save_recx(struct vos_io_context *ioc, uint64_t rx_idx, uint64_t rx_nr,
	  daos_epoch_t ep, uint32_t rec_size, int type)
{
	struct daos_recx_ep_list	*recx_list;
	struct daos_recx_ep		 recx_ep;

	if (ioc->ic_recx_lists == NULL) {
		D_ALLOC_ARRAY(ioc->ic_recx_lists, ioc->ic_iod_nr);
		if (ioc->ic_recx_lists == NULL)
			return -DER_NOMEM;
	}

	recx_list = &ioc->ic_recx_lists[ioc->ic_sgl_at];
	recx_ep.re_recx.rx_idx = rx_idx;
	recx_ep.re_recx.rx_nr = rx_nr;
	recx_ep.re_ep = ep;
	recx_ep.re_type = type;
	recx_ep.re_rec_size = rec_size;

	return daos_recx_ep_add(recx_list, &recx_ep);
}

/** Fetch an extent from an akey */
static int
akey_fetch_recx(daos_handle_t toh, const daos_epoch_range_t *epr,
		daos_recx_t *recx, daos_epoch_t shadow_ep, daos_size_t *rsize_p,
		struct vos_io_context *ioc)
{
	struct evt_entry	*ent;
	/* At present, this is not exposed in interface but passing it toggles
	 * sorting and clipping of rectangles
	 */
	struct evt_entry_array	 ent_array = { 0 };
	struct evt_filter	 filter;
	struct bio_iov		 biov = {0};
	daos_size_t		 holes; /* hole width */
	daos_size_t		 rsize;
	daos_off_t		 index;
	daos_off_t		 end;
	bool			 csum_enabled = false;
	bool			 with_shadow = (shadow_ep != DAOS_EPOCH_MAX);
	int			 rc;

	index = recx->rx_idx;
	end   = recx->rx_idx + recx->rx_nr;

	filter.fr_ex.ex_lo = index;
	filter.fr_ex.ex_hi = end - 1;
	filter.fr_epoch = epr->epr_hi;
	filter.fr_epr.epr_lo = epr->epr_lo;
	filter.fr_epr.epr_hi = ioc->ic_bound;
	filter.fr_punch_epc = ioc->ic_akey_info.ii_prior_punch.pr_epc;
	filter.fr_punch_minor_epc =
		ioc->ic_akey_info.ii_prior_punch.pr_minor_epc;

	evt_ent_array_init(&ent_array);
	rc = evt_find(toh, &filter, &ent_array);
	if (rc != 0 || vos_dtx_hit_inprogress())
		D_GOTO(failed, rc = (rc == 0 ? -DER_INPROGRESS : rc));

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
				  lo, index, DP_EXT(&filter.fr_ex),
				  DP_ENT(ent));
			holes += lo - index;
		}

		/* Hole extent, with_shadow case only used for EC obj */
		if (bio_addr_is_hole(&ent->en_addr) ||
		    (with_shadow && (ent->en_epoch < shadow_ep))) {
			index = lo + nr;
			holes += nr;
			continue;
		}

		if (holes != 0) {
			if (with_shadow) {
				rc = save_recx(ioc, lo - holes, holes,
					       shadow_ep, ent_array.ea_inob,
					       DRT_SHADOW);
				if (rc != 0)
					goto failed;
			}
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

		if (ioc->ic_save_recx) {
			rc = save_recx(ioc, lo, nr, ent->en_epoch,
				       ent_array.ea_inob, DRT_NORMAL);
			if (rc != 0)
				goto failed;
		}
		bio_iov_set(&biov, ent->en_addr, nr * ent_array.ea_inob);

		if (ci_is_valid(&ent->en_csum)) {
			rc = save_csum(ioc, &ent->en_csum, ent, rsize);
			if (rc != 0)
				return rc;
			biov_align_lens(&biov, ent, rsize);
			csum_enabled = true;
		} else {
			bio_iov_set_extra(&biov, 0, 0);
			if (csum_enabled)
				D_ERROR("Checksum found in some entries, "
					"but not all\n");
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
		if (with_shadow) {
			rc = save_recx(ioc, end - holes, holes, shadow_ep,
				       ent_array.ea_inob, DRT_SHADOW);
			if (rc != 0)
				goto failed;
		}
		biov_set_hole(&biov, holes * ent_array.ea_inob);
		rc = iod_fetch(ioc, &biov);
		if (rc != 0)
			goto failed;
	}
	if (rsize_p && *rsize_p == 0)
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
			    epr.epr_hi, ioc->ic_bound, 0, parent, info);
	if (rc != 0)
		goto out;

	rc = vos_ilog_check(info, &epr, epr_out, true);
out:
	D_DEBUG(DB_TRACE, "ilog check returned "DF_RC" epr_in="DF_X64"-"DF_X64
		" punch="DF_PUNCH" epr_out="DF_X64"-"DF_X64"\n", DP_RC(rc),
		epr.epr_lo, epr.epr_hi, DP_PUNCH(&info->ii_prior_punch),
		epr_out ? epr_out->epr_lo : 0,
		epr_out ? epr_out->epr_hi : 0);
	return rc;
}

static void
akey_fetch_recx_get(daos_recx_t *iod_recx, struct daos_recx_ep_list *shadow,
		    daos_recx_t *fetch_recx, daos_epoch_t *shadow_ep)
{
	struct daos_recx_ep	*recx_ep;
	daos_recx_t		*recx;
	uint32_t		 i;

	if (shadow == NULL)
		goto no_shadow;

	for (i = 0; i < shadow->re_nr; i++) {
		recx_ep = &shadow->re_items[i];
		recx = &recx_ep->re_recx;
		if (!DAOS_RECX_PTR_OVERLAP(iod_recx, recx))
			continue;

		fetch_recx->rx_idx = iod_recx->rx_idx;
		fetch_recx->rx_nr = min((iod_recx->rx_idx + iod_recx->rx_nr),
					(recx->rx_idx + recx->rx_nr)) -
				    iod_recx->rx_idx;
		D_ASSERT(fetch_recx->rx_nr > 0 &&
			 fetch_recx->rx_nr <= iod_recx->rx_nr);
		iod_recx->rx_idx += fetch_recx->rx_nr;
		iod_recx->rx_nr -= fetch_recx->rx_nr;
		*shadow_ep = recx_ep->re_ep;
		return;
	}

no_shadow:
	*fetch_recx = *iod_recx;
	iod_recx->rx_idx += fetch_recx->rx_nr;
	iod_recx->rx_nr -= fetch_recx->rx_nr;
	*shadow_ep = DAOS_EPOCH_MAX;
}

static bool
stop_check(struct vos_io_context *ioc, uint64_t cond, daos_iod_t *iod, int *rc,
	   bool check_uncertainty)
{
	uint64_t	flags;

	if (*rc == 0)
		return false;

	if (*rc != -DER_NONEXIST)
		return true;

	if (vos_dtx_hit_inprogress())
		return true;

	if (ioc->ic_check_existence)
		goto check;

	if (ioc->ic_ts_set == NULL) {
		*rc = 0;
		return true;
	}

	if (ioc->ic_read_ts_only) {
		*rc = 0;
		goto check;
	}

	if (iod != NULL && ioc->ic_ts_set->ts_flags & VOS_OF_COND_PER_AKEY) {
		/** Per akey flags have been specified */
		flags = iod->iod_flags;
	} else {
		flags = ioc->ic_ts_set->ts_flags;
	}

	if ((flags & cond) == 0) {
		*rc = 0;
		if (check_uncertainty)
			goto check;
		return true;
	}
check:
	if (vos_ts_wcheck(ioc->ic_ts_set, ioc->ic_epr.epr_hi,
			  ioc->ic_bound))
		*rc = -DER_TX_RESTART;

	return true;
}

static bool
has_uncertainty(const struct vos_io_context *ioc,
		const struct vos_ilog_info *info)
{
	return vos_has_uncertainty(ioc->ic_ts_set, info, ioc->ic_epr.epr_hi,
				   ioc->ic_bound);
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
	struct daos_recx_ep_list *shadow;

	D_DEBUG(DB_IO, "akey "DF_KEY" fetch %s epr "DF_X64"-"DF_X64"\n",
		DP_KEY(&iod->iod_name),
		iod->iod_type == DAOS_IOD_ARRAY ? "array" : "single",
		ioc->ic_epr.epr_lo, ioc->ic_epr.epr_hi);

	if (is_array)
		flags |= SUBTR_EVT;

	rc = key_tree_prepare(ioc->ic_obj, ak_toh,
			      VOS_BTR_AKEY, &iod->iod_name, flags,
			      DAOS_INTENT_DEFAULT, &krec, &toh, ioc->ic_ts_set);

	if (stop_check(ioc, VOS_OF_COND_AKEY_FETCH, iod, &rc, true)) {
		if (rc == 0 && !ioc->ic_read_ts_only)
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
		VOS_TX_LOG_FAIL(rc, "Failed to get akey "DF_KEY" "DF_RC"\n",
				DP_KEY(&iod->iod_name), DP_RC(rc));
		goto out;
	}

	rc = key_ilog_check(ioc, krec, &ioc->ic_dkey_info, &val_epr,
			    &ioc->ic_akey_info);

	if (stop_check(ioc, VOS_OF_COND_AKEY_FETCH, iod, &rc, false)) {
		if (rc == 0 && !ioc->ic_read_ts_only) {
			if (has_uncertainty(ioc, &ioc->ic_akey_info))
				goto fetch_value;
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
		}
		VOS_TX_LOG_FAIL(rc, "Fetch akey failed: rc="DF_RC"\n",
				DP_RC(rc));
		goto out;
	}

fetch_value:
	if (ioc->ic_read_ts_only || ioc->ic_check_existence)
		goto out; /* skip value fetch */

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_fetch_single(toh, &val_epr, &iod->iod_size, ioc);
		goto out;
	}

	iod->iod_size = 0;
	shadow = (ioc->ic_shadows == NULL) ? NULL :
					     &ioc->ic_shadows[ioc->ic_sgl_at];
	for (i = 0; i < iod->iod_nr; i++) {
		daos_recx_t	iod_recx;
		daos_recx_t	fetch_recx;
		daos_epoch_t	shadow_ep;
		daos_size_t	rsize = 0;

		if (iod->iod_recxs[i].rx_nr == 0) {
			D_DEBUG(DB_IO,
				"Skip empty read IOD at %d: idx %lu, nr %lu\n",
				i, (unsigned long)iod->iod_recxs[i].rx_idx,
				(unsigned long)iod->iod_recxs[i].rx_nr);
			continue;
		}

		iod_recx = iod->iod_recxs[i];
		while (iod_recx.rx_nr > 0) {
			akey_fetch_recx_get(&iod_recx, shadow, &fetch_recx,
					    &shadow_ep);
			rc = akey_fetch_recx(toh, &val_epr, &fetch_recx,
					     shadow_ep, &rsize, ioc);

			if (vos_dtx_continue_detect(rc))
				continue;

			if (rc != 0) {
				VOS_TX_LOG_FAIL(rc, "Failed to fetch index %d: "
						DF_RC"\n", i, DP_RC(rc));
				goto out;
			}
		}

		if (vos_dtx_hit_inprogress())
			continue;

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

	if (vos_dtx_hit_inprogress())
		goto out;

	ioc_trim_tail_holes(ioc);
out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, is_array);

	return vos_dtx_hit_inprogress() ? -DER_INPROGRESS : rc;
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
			      &toh, ioc->ic_ts_set);

	if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL,
		       &rc, true)) {
		if (rc == 0 && !ioc->ic_read_ts_only) {
			for (i = 0; i < ioc->ic_iod_nr; i++)
				iod_empty_sgl(ioc, i);
		} else {
			VOS_TX_LOG_FAIL(rc, "Failed to fetch dkey: "DF_RC"\n",
					DP_RC(rc));
		}
		goto out;
	}

	rc = key_ilog_check(ioc, krec, &obj->obj_ilog_info, &ioc->ic_epr,
			    &ioc->ic_dkey_info);

	if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL,
		       &rc, false)) {
		if (rc == 0 && !ioc->ic_read_ts_only) {
			if (has_uncertainty(ioc, &ioc->ic_dkey_info)) {
				/** There is a value in the uncertainty range so
				 *  we need to continue the fetch.
				 */
				goto fetch_akey;
			}
			for (i = 0; i < ioc->ic_iod_nr; i++)
				iod_empty_sgl(ioc, i);
		} else {
			VOS_TX_LOG_FAIL(rc, "Fetch dkey failed: rc="DF_RC"\n",
					DP_RC(rc));
		}
		goto out;
	}

fetch_akey:
	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_fetch(ioc, toh);
		if (vos_dtx_continue_detect(rc))
			continue;

		if (rc != 0)
			break;
	}

	/* Add this check to prevent some new added logic after above for(). */
	if (vos_dtx_hit_inprogress())
		goto out;

out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, false);

	return vos_dtx_hit_inprogress() ? -DER_INPROGRESS : rc;
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

/** If the object/key doesn't exist, we should augment the set with any missing
 *  entries
 */
static void
vos_fetch_add_missing(struct vos_ts_set *ts_set, daos_key_t *dkey, int iod_nr,
		      daos_iod_t *iods)
{
	struct vos_akey_data	ad;

	ad.ad_is_iod = true;
	ad.ad_iods = iods;

	vos_ts_add_missing(ts_set, dkey, iod_nr, &ad);
}

int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int iod_nr,
		daos_iod_t *iods, uint32_t vos_flags,
		struct daos_recx_ep_list *shadows, daos_handle_t *ioh,
		struct dtx_handle *dth)
{
	struct vos_io_context	*ioc;
	int			 i, rc;

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_X64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_ioc_create(coh, oid, true, epoch, iod_nr, iods,
			    NULL, vos_flags, shadows, false, 0,
			    dth, &ioc);
	if (rc != 0)
		return rc;

	vos_dth_set(dth);

	rc = vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0);
	D_ASSERT(rc == 0);

	rc = vos_obj_hold(vos_obj_cache_current(), ioc->ic_cont, oid,
			  &ioc->ic_epr, ioc->ic_bound, VOS_OBJ_VISIBLE,
			  DAOS_INTENT_DEFAULT, &ioc->ic_obj, ioc->ic_ts_set);
	if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL,
		       &rc, false)) {
		if (rc == 0) {
			if (ioc->ic_read_ts_only)
				goto set_ioc;
			if (ioc->ic_obj != NULL &&
			    has_uncertainty(ioc, &ioc->ic_obj->obj_ilog_info))
				goto fetch_dkey;
			for (i = 0; i < iod_nr; i++)
				iod_empty_sgl(ioc, i);
			goto set_ioc;
		}
		goto out;
	}
fetch_dkey:
	if (dkey == NULL || dkey->iov_len == 0) {
		if (ioc->ic_read_ts_only)
			goto set_ioc;
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dkey_fetch(ioc, dkey);
	if (rc != 0)
		goto out;
set_ioc:
	*ioh = vos_ioc2ioh(ioc);
out:
	vos_dth_set(NULL);

	if (rc == -DER_NONEXIST || rc == -DER_INPROGRESS ||
	    (rc == 0 && ioc->ic_read_ts_only)) {
		if (vos_ts_wcheck(ioc->ic_ts_set, ioc->ic_epr.epr_hi,
				  ioc->ic_bound))
			rc = -DER_TX_RESTART;
	}

	if (rc == -DER_NONEXIST || rc == 0) {
		vos_fetch_add_missing(ioc->ic_ts_set, dkey, iod_nr, iods);
		vos_ts_set_update(ioc->ic_ts_set, ioc->ic_epr.epr_hi);
	}

	if (rc != 0) {
		daos_recx_ep_list_free(ioc->ic_recx_lists, ioc->ic_iod_nr);
		ioc->ic_recx_lists = NULL;
		return vos_fetch_end(vos_ioc2ioh(ioc), rc);
	}
	return 0;
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
		   daos_size_t gsize, struct vos_io_context *ioc,
		   uint16_t minor_epc)
{
	struct vos_svt_key	 key;
	struct vos_rec_bundle	 rbund;
	struct dcs_csum_info	 csum;
	d_iov_t			 kiov, riov;
	struct bio_iov		*biov;
	umem_off_t		 umoff;
	daos_epoch_t		 epoch = ioc->ic_epr.epr_hi;
	int			 rc;

	ci_set_null(&csum);
	d_iov_set(&kiov, &key, sizeof(key));
	key.sk_epoch		= epoch;
	key.sk_minor_epc	= minor_epc;

	umoff = iod_update_umoff(ioc);
	D_ASSERT(!UMOFF_IS_NULL(umoff));

	D_ASSERT(ioc->ic_iov_at == 0);
	biov = iod_update_biov(ioc);

	tree_rec_bundle2iov(&rbund, &riov);

	struct dcs_csum_info *value_csum = vos_ioc2csum(ioc);

	if (value_csum != NULL)
		rbund.rb_csum	= value_csum;
	else
		rbund.rb_csum	= &csum;

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
		 struct vos_io_context *ioc, uint16_t minor_epc)
{
	struct evt_entry_in	 ent;
	struct bio_iov		*biov;
	daos_epoch_t		 epoch = ioc->ic_epr.epr_hi;
	int rc;

	D_ASSERT(recx->rx_nr > 0);
	memset(&ent, 0, sizeof(ent));
	ent.ei_bound = ioc->ic_bound;
	ent.ei_rect.rc_epc = epoch;
	ent.ei_rect.rc_ex.ex_lo = recx->rx_idx;
	ent.ei_rect.rc_ex.ex_hi = recx->rx_idx + recx->rx_nr - 1;
	ent.ei_rect.rc_minor_epc = minor_epc;
	ent.ei_ver = pm_ver;
	ent.ei_inob = rsize;

	if (csum != NULL)
		ent.ei_csum = *csum;

	biov = iod_update_biov(ioc);
	ent.ei_addr = biov->bi_addr;
	ent.ei_addr.ba_dedup = false;	/* Don't make this flag persistent */

	if (ioc->ic_remove)
		return evt_remove_all(toh, &ent.ei_rect.rc_ex, &ioc->ic_epr);

	rc = evt_insert(toh, &ent, NULL);

	if (ioc->ic_dedup && !rc && (rsize * recx->rx_nr) >= ioc->ic_dedup_th) {
		daos_size_t csum_len = recx_csum_len(recx, csum, rsize);

		vos_dedup_update(vos_cont2pool(ioc->ic_cont), csum, csum_len,
				 biov, &ioc->ic_dedup_entries);
	}
	return rc;
}

static int
akey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_handle_t ak_toh,
	    uint16_t minor_epc)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_krec_df	*krec = NULL;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct dcs_csum_info	*iod_csums = vos_ioc2csum(ioc);
	struct dcs_csum_info	*recx_csum = NULL;
	uint32_t		 update_cond = 0;
	bool			 is_array = (iod->iod_type == DAOS_IOD_ARRAY);
	int			 flags = SUBTR_CREATE;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	int			 i;
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "akey "DF_KEY" update %s value eph "DF_X64"\n",
		DP_KEY(&iod->iod_name), is_array ? "array" : "single",
		ioc->ic_epr.epr_hi);

	if (is_array)
		flags |= SUBTR_EVT;

	rc = key_tree_prepare(obj, ak_toh, VOS_BTR_AKEY,
			      &iod->iod_name, flags, DAOS_INTENT_UPDATE,
			      &krec, &toh, ioc->ic_ts_set);
	if (rc != 0)
		return rc;

	if (ioc->ic_ts_set) {
		uint64_t akey_flags;

		if (ioc->ic_ts_set->ts_flags & VOS_OF_COND_PER_AKEY)
			akey_flags = iod->iod_flags;
		else
			akey_flags = ioc->ic_ts_set->ts_flags;

		switch (akey_flags) {
		case VOS_OF_COND_AKEY_UPDATE:
			update_cond = VOS_ILOG_COND_UPDATE;
			break;
		case VOS_OF_COND_AKEY_INSERT:
			update_cond = VOS_ILOG_COND_INSERT;
			break;
		default:
			break;
		}
	}

	rc = vos_ilog_update(ioc->ic_cont, &krec->kr_ilog, &ioc->ic_epr,
			     ioc->ic_bound, &ioc->ic_dkey_info,
			     &ioc->ic_akey_info, update_cond, ioc->ic_ts_set);
	if (update_cond == VOS_ILOG_COND_UPDATE && rc == -DER_NONEXIST) {
		D_DEBUG(DB_IO, "Conditional update on non-existent akey\n");
		goto out;
	}
	if (update_cond == VOS_ILOG_COND_INSERT && rc == -DER_EXIST) {
		D_DEBUG(DB_IO, "Conditional insert on existent akey\n");
		goto out;
	}

	if (rc != 0) {
		VOS_TX_LOG_FAIL(rc, "Failed to update akey ilog: "DF_RC"\n",
				DP_RC(rc));
		goto out;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		uint64_t	gsize;

		gsize = (iod->iod_recxs == NULL) ? iod->iod_size :
						   (uintptr_t)iod->iod_recxs;
		rc = akey_update_single(toh, pm_ver, iod->iod_size, gsize, ioc,
					minor_epc);
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
				      recx_csum, iod->iod_size, ioc,
				      minor_epc);
		if (rc != 0)
			goto out;
	}

out:
	if (!daos_handle_is_inval(toh))
		key_tree_release(toh, is_array);

	return rc;
}

static int
dkey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_key_t *dkey,
	    uint16_t minor_epc)
{
	struct vos_object	*obj = ioc->ic_obj;
	daos_handle_t		 ak_toh;
	struct vos_krec_df	*krec;
	uint32_t		 update_cond = 0;
	bool			 subtr_created = false;
	int			 i, rc;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY, dkey,
			      SUBTR_CREATE, DAOS_INTENT_UPDATE, &krec, &ak_toh,
			      ioc->ic_ts_set);
	if (rc != 0) {
		D_ERROR("Error preparing dkey tree: rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}
	subtr_created = true;

	if (ioc->ic_ts_set) {
		if (ioc->ic_ts_set->ts_flags & VOS_COND_UPDATE_OP_MASK)
			update_cond = VOS_ILOG_COND_UPDATE;
		else if (ioc->ic_ts_set->ts_flags & VOS_OF_COND_DKEY_INSERT)
			update_cond = VOS_ILOG_COND_INSERT;
	}

	rc = vos_ilog_update(ioc->ic_cont, &krec->kr_ilog, &ioc->ic_epr,
			     ioc->ic_bound, &obj->obj_ilog_info,
			     &ioc->ic_dkey_info, update_cond, ioc->ic_ts_set);
	if (update_cond == VOS_ILOG_COND_UPDATE && rc == -DER_NONEXIST) {
		D_DEBUG(DB_IO, "Conditional update on non-existent akey\n");
		goto out;
	}
	if (update_cond == VOS_ILOG_COND_INSERT && rc == -DER_EXIST) {
		D_DEBUG(DB_IO, "Conditional insert on existent akey\n");
		goto out;
	}
	if (rc != 0) {
		VOS_TX_LOG_FAIL(rc, "Failed to update dkey ilog: "DF_RC"\n",
				DP_RC(rc));
		goto out;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);

		rc = akey_update(ioc, pm_ver, ak_toh, minor_epc);
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

daos_size_t
vos_recx2irec_size(daos_size_t rsize, struct dcs_csum_info *csum)
{
	struct vos_rec_bundle	rbund;

	rbund.rb_csum	= csum;
	rbund.rb_rsize	= rsize;

	return vos_irec_size(&rbund);
}

umem_off_t
vos_reserve_scm(struct vos_container *cont, struct vos_rsrvd_scm *rsrvd_scm,
		daos_size_t size)
{
	umem_off_t	umoff;

	D_ASSERT(size > 0);

	if (vos_cont2umm(cont)->umm_ops->mo_reserve != NULL) {
		struct pobj_action *act;

		D_ASSERT(rsrvd_scm != NULL);
		D_ASSERT(rsrvd_scm->rs_actv_cnt > rsrvd_scm->rs_actv_at);

		act = &rsrvd_scm->rs_actv[rsrvd_scm->rs_actv_at];

		umoff = umem_reserve(vos_cont2umm(cont), act, size);
		if (!UMOFF_IS_NULL(umoff))
			rsrvd_scm->rs_actv_at++;
	} else {
		umoff = umem_alloc(vos_cont2umm(cont), size);
	}

	return umoff;
}

int
vos_reserve_blocks(struct vos_container *cont, d_list_t *rsrvd_nvme,
		   daos_size_t size, enum vos_io_stream ios, uint64_t *off)
{
	struct vea_space_info	*vsi;
	struct vea_hint_context	*hint_ctxt;
	struct vea_resrvd_ext	*ext;
	uint32_t		 blk_cnt;
	int			 rc;

	vsi = vos_cont2pool(cont)->vp_vea_info;
	D_ASSERT(vsi);

	hint_ctxt = cont->vc_hint_ctxt[ios];
	D_ASSERT(hint_ctxt);

	blk_cnt = vos_byte2blkcnt(size);

	rc = vea_reserve(vsi, blk_cnt, hint_ctxt, rsrvd_nvme);
	if (rc)
		return rc;

	ext = d_list_entry(rsrvd_nvme->prev, struct vea_resrvd_ext, vre_link);
	D_ASSERTF(ext->vre_blk_cnt == blk_cnt, "%u != %u\n",
		  ext->vre_blk_cnt, blk_cnt);
	D_ASSERT(ext->vre_blk_off != 0);

	*off = ext->vre_blk_off << VOS_BLK_SHIFT;
	return 0;
}

static int
reserve_space(struct vos_io_context *ioc, uint16_t media, daos_size_t size,
	      uint64_t *off)
{
	int	rc;

	if (media == DAOS_MEDIA_SCM) {
		umem_off_t	umoff;

		umoff = vos_reserve_scm(ioc->ic_cont, ioc->ic_rsrvd_scm, size);
		if (!UMOFF_IS_NULL(umoff)) {
			ioc->ic_umoffs[ioc->ic_umoffs_cnt] = umoff;
			ioc->ic_umoffs_cnt++;
			*off = umoff;
			return 0;
		}
		D_ERROR("Reserve "DF_U64" from SCM failed.\n", size);
		return -DER_NOSPACE;
	}

	D_ASSERT(media == DAOS_MEDIA_NVME);
	rc = vos_reserve_blocks(ioc->ic_cont, &ioc->ic_blk_exts, size,
				VOS_IOS_GENERIC, off);
	if (rc)
		D_ERROR("Reserve "DF_U64" from NVMe failed. "DF_RC"\n",
			size, DP_RC(rc));
	return rc;
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

	rc = reserve_space(ioc, DAOS_MEDIA_SCM, scm_size, &off);
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
		rc = reserve_space(ioc, DAOS_MEDIA_NVME, size, &off);
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
vos_reserve_recx(struct vos_io_context *ioc, uint16_t media, daos_size_t size,
		 struct dcs_csum_info *csum, daos_size_t csum_len)
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

	if (ioc->ic_dedup && size >= ioc->ic_dedup_th &&
	    vos_dedup_lookup(vos_cont2pool(ioc->ic_cont), csum, csum_len,
			     &biov)) {
		if (biov.bi_data_len == size) {
			D_ASSERT(biov.bi_addr.ba_off != 0);
			ioc->ic_umoffs[ioc->ic_umoffs_cnt] =
							biov.bi_addr.ba_off;
			ioc->ic_umoffs_cnt++;
			return iod_reserve(ioc, &biov);
		}
		memset(&biov, 0, sizeof(biov));
	}

	/*
	 * TODO:
	 * To eliminate internal fragmentaion, misaligned recx (total recx size
	 * isn't aligned with 4K) on NVMe could be split into two evtree rects,
	 * larger rect will be stored on NVMe and small reminder on SCM.
	 */
	rc = reserve_space(ioc, media, size, &off);
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

static int
akey_update_begin(struct vos_io_context *ioc)
{
	struct dcs_csum_info	*iod_csums = vos_ioc2csum(ioc);
	struct dcs_csum_info	*recx_csum;
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

		media = vos_media_select(vos_cont2pool(ioc->ic_cont),
					 iod->iod_type, size);

		recx_csum = (iod_csums != NULL) ? &iod_csums[i] : NULL;

		if (iod->iod_type == DAOS_IOD_SINGLE) {
			rc = vos_reserve_single(ioc, media, size);
		} else {
			daos_size_t csum_len;

			csum_len = recx_csum_len(&iod->iod_recxs[i], recx_csum,
						 iod->iod_size);
			rc = vos_reserve_recx(ioc, media, size, recx_csum,
					      csum_len);
		}
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

int
vos_publish_scm(struct vos_container *cont, struct vos_rsrvd_scm *rsrvd_scm,
		bool publish)
{
	int	rc = 0;

	if (rsrvd_scm == NULL || rsrvd_scm->rs_actv_at == 0)
		return 0;

	D_ASSERT(rsrvd_scm->rs_actv_at <= rsrvd_scm->rs_actv_cnt);

	if (publish)
		rc = umem_tx_publish(vos_cont2umm(cont), rsrvd_scm->rs_actv,
				     rsrvd_scm->rs_actv_at);
	else
		umem_cancel(vos_cont2umm(cont), rsrvd_scm->rs_actv,
			    rsrvd_scm->rs_actv_at);

	rsrvd_scm->rs_actv_at = 0;
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
	if (vos_cont2umm(ioc->ic_cont)->umm_ops->mo_reserve != NULL)
		return;

	if (ioc->ic_umoffs_cnt != 0) {
		struct umem_instance *umem = vos_ioc2umm(ioc);
		int i;

		D_ASSERT(umem->umm_id == UMEM_CLASS_VMEM);

		for (i = 0; i < ioc->ic_umoffs_cnt; i++) {
			if (!UMOFF_IS_NULL(ioc->ic_umoffs[i]))
				/* Ignore umem_free failure. */
				umem_free(umem, ioc->ic_umoffs[i]);
		}
	}

	/* Abort dedup entries */
	vos_dedup_process(vos_cont2pool(ioc->ic_cont), &ioc->ic_dedup_entries,
			  true /* abort */);
}

int
vos_update_end(daos_handle_t ioh, uint32_t pm_ver, daos_key_t *dkey, int err,
	       struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);
	struct umem_instance	*umem;
	uint64_t		 time = 0;
	bool			 tx_started = false;

	VOS_TIME_START(time, VOS_UPDATE_END);
	D_ASSERT(ioc->ic_update);

	if (err != 0)
		goto abort;

	err = vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0);
	D_ASSERT(err == 0);

	umem = vos_ioc2umm(ioc);

	err = vos_tx_begin(dth, umem);
	if (err != 0)
		goto abort;

	tx_started = true;

	vos_dth_set(dth);

	/* Commit the CoS DTXs via the IO PMDK transaction. */
	if (dtx_is_valid_handle(dth) && dth->dth_dti_cos_count > 0) {
		D_ALLOC_ARRAY(daes, dth->dth_dti_cos_count);
		if (daes == NULL)
			D_GOTO(abort, err = -DER_NOMEM);

		err = vos_dtx_commit_internal(ioc->ic_cont, dth->dth_dti_cos,
					      dth->dth_dti_cos_count,
					      0, NULL, daes);
		if (err <= 0)
			D_FREE(daes);
	}

	err = vos_obj_hold(vos_obj_cache_current(), ioc->ic_cont, ioc->ic_oid,
			   &ioc->ic_epr, ioc->ic_bound,
			   VOS_OBJ_CREATE | VOS_OBJ_VISIBLE, DAOS_INTENT_UPDATE,
			   &ioc->ic_obj, ioc->ic_ts_set);
	if (err != 0)
		goto abort;

	/* Update tree index */
	err = dkey_update(ioc, pm_ver, dkey, dtx_is_valid_handle(dth) ?
			  dth->dth_op_seq : VOS_MINOR_EPC_MAX);
	if (err) {
		VOS_TX_LOG_FAIL(err, "Failed to update tree index: "DF_RC"\n",
				DP_RC(err));
		goto abort;
	}

	/** Now that we are past the existence checks, ensure there isn't a
	 * read conflict
	 */
	if (vos_ts_set_check_conflict(ioc->ic_ts_set, ioc->ic_epr.epr_hi)) {
		err = -DER_TX_RESTART;
		goto abort;
	}

abort:
	if (err == -DER_NONEXIST || err == -DER_EXIST ||
	    err == -DER_INPROGRESS) {
		if (vos_ts_wcheck(ioc->ic_ts_set, ioc->ic_epr.epr_hi,
				  ioc->ic_bound)) {
			err = -DER_TX_RESTART;
		}
	}
	err = vos_tx_end(ioc->ic_cont, dth, &ioc->ic_rsrvd_scm,
			 &ioc->ic_blk_exts, tx_started, err);

	if (err == 0) {
		if (daes != NULL)
			vos_dtx_post_handle(ioc->ic_cont, daes,
					    dth->dth_dti_cos_count, false);
		vos_dedup_process(vos_cont2pool(ioc->ic_cont),
				  &ioc->ic_dedup_entries, false);
	} else {
		update_cancel(ioc);
	}

	D_FREE(daes);

	if (err == 0)
		vos_ts_set_upgrade(ioc->ic_ts_set);

	if (err == -DER_NONEXIST || err == -DER_EXIST || err == 0) {
		vos_ts_set_update(ioc->ic_ts_set, ioc->ic_epr.epr_hi);
		if (err == 0)
			vos_ts_set_wupdate(ioc->ic_ts_set, ioc->ic_epr.epr_hi);
	}

	VOS_TIME_END(time, VOS_UPDATE_END);
	vos_space_unhold(vos_cont2pool(ioc->ic_cont), &ioc->ic_space_held[0]);

	vos_ioc_destroy(ioc, err != 0);
	vos_dth_set(NULL);

	return err;
}

static int
vos_check_akeys(int iod_nr, daos_iod_t *iods)
{
	int	i, j;

	if (iod_nr == 0)
		return 0;

	for (i = 0; i < iod_nr - 1; i++) {
		for (j = i + 1; j < iod_nr; j++) {
			if (iods[i].iod_name.iov_len !=
			    iods[j].iod_name.iov_len)
				continue;

			if (iods[i].iod_name.iov_buf ==
			    iods[j].iod_name.iov_buf)
				return -DER_NO_PERM;

			if (iods[i].iod_name.iov_buf == NULL ||
			    iods[j].iod_name.iov_buf == NULL)
				continue;

			if (memcmp(iods[i].iod_name.iov_buf,
				   iods[j].iod_name.iov_buf,
				   iods[i].iod_name.iov_len) == 0)
				return -DER_NO_PERM;
		}
	}

	return 0;
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
		 daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
		 bool dedup, uint32_t dedup_th, daos_handle_t *ioh,
		 struct dtx_handle *dth)
{
	struct vos_io_context	*ioc;
	int			 rc;

	if (oid.id_shard % 3 == 1 && DAOS_FAIL_CHECK(DAOS_DTX_FAIL_IO))
		return -DER_IO;

	D_DEBUG(DB_TRACE, "Prepare IOC for "DF_UOID", iod_nr %d, epc "DF_X64
		", flags="DF_X64"\n", DP_UOID(oid), iod_nr,
		dtx_is_valid_handle(dth) ? dth->dth_epoch :  epoch, flags);

	rc = vos_check_akeys(iod_nr, iods);
	if (rc != 0) {
		D_ERROR("Detected duplicate akeys, operation not allowed\n");
		return rc;
	}

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, iods_csums,
			    flags, NULL, dedup, dedup_th, dth, &ioc);
	if (rc != 0)
		return rc;

	/* flags may have VOS_OF_CRIT to skip sys/held checks here */
	rc = vos_space_hold(vos_cont2pool(ioc->ic_cont), flags, dkey, iod_nr,
			    iods, iods_csums, &ioc->ic_space_held[0]);
	if (rc != 0) {
		D_ERROR(DF_UOID": Hold space failed. "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		goto error;
	}

	rc = dkey_update_begin(ioc);
	if (rc != 0) {
		D_ERROR(DF_UOID"dkey update begin failed. %d\n", DP_UOID(oid),
			rc);
		goto error;
	}
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	vos_update_end(vos_ioc2ioh(ioc), 0, dkey, rc, dth);
	return rc;
}

struct daos_recx_ep_list *
vos_ioh2recx_list(daos_handle_t ioh)
{
	return vos_ioh2ioc(ioh)->ic_recx_lists;
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

/*
 * XXX Dup these two helper functions for this moment, implement
 * non-transactional umem_alloc/free() later.
 */
static inline umem_off_t
umem_id2off(const struct umem_instance *umm, PMEMoid oid)
{
	if (OID_IS_NULL(oid))
		return UMOFF_NULL;

	return oid.off;
}

static inline PMEMoid
umem_off2id(const struct umem_instance *umm, umem_off_t umoff)
{
	PMEMoid	oid;

	if (UMOFF_IS_NULL(umoff))
		return OID_NULL;

	oid.pool_uuid_lo = umm->umm_pool_uuid_lo;
	oid.off = umem_off2offset(umoff);

	return oid;
}

/* Duplicate bio_sglist for landing RDMA transfer data */
int
vos_dedup_dup_bsgl(daos_handle_t ioh, struct bio_sglist *bsgl,
		   struct bio_sglist *bsgl_dup)
{
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);
	int			 i, rc;

	D_ASSERT(!daos_handle_is_inval(ioh));
	D_ASSERT(bsgl != NULL);
	D_ASSERT(bsgl_dup != NULL);

	rc = bio_sgl_init(bsgl_dup, bsgl->bs_nr_out);
	if (rc != 0)
		return rc;

	bsgl_dup->bs_nr_out = bsgl->bs_nr_out;

	for (i = 0; i < bsgl->bs_nr_out; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		struct bio_iov	*biov_dup = &bsgl_dup->bs_iovs[i];
		PMEMoid		 oid;

		if (bio_iov2buf(biov) == NULL)
			continue;

		*biov_dup = *biov;
		/* Original biov isn't deduped, don't duplicate buffer */
		if (!biov->bi_addr.ba_dedup)
			continue;

		D_ASSERT(bio_iov2len(biov) != 0);
		/* Support SCM only for this moment */
		rc = pmemobj_alloc(vos_ioc2umm(ioc)->umm_pool, &oid,
				   bio_iov2len(biov), UMEM_TYPE_ANY, NULL,
				   NULL);
		if (rc) {
			D_ERROR("Failed to alloc "DF_U64" bytes SCM\n",
				bio_iov2len(biov));
			return -DER_NOMEM;
		}

		biov_dup->bi_addr.ba_off = umem_id2off(vos_ioc2umm(ioc), oid);
		biov_dup->bi_buf = umem_off2ptr(vos_ioc2umm(ioc),
						bio_iov2off(biov_dup));
	}

	return 0;
}

void
vos_dedup_free_bsgl(daos_handle_t ioh, struct bio_sglist *bsgl)
{
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);
	int			 i;

	D_ASSERT(!daos_handle_is_inval(ioh));
	for (i = 0; i < bsgl->bs_nr_out; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		PMEMoid		 oid;

		if (UMOFF_IS_NULL(bio_iov2off(biov)))
			continue;
		/* Not duplicated buffer, don't free it */
		if (!biov->bi_addr.ba_dedup)
			continue;

		oid = umem_off2id(vos_ioc2umm(ioc), bio_iov2off(biov));
		pmemobj_free(&oid);
	}
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
vos_obj_update_ex(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		  uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
		  unsigned int iod_nr, daos_iod_t *iods,
		  struct dcs_iod_csums *iods_csums, d_sg_list_t *sgls,
		  struct dtx_handle *dth)
{
	daos_handle_t ioh;
	int rc;

	rc = vos_update_begin(coh, oid, epoch, flags, dkey, iod_nr, iods,
			      iods_csums, false, 0, &ioh, dth);
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

	rc = vos_update_end(ioh, pm_ver, dkey, rc, dth);
	return rc;
}

int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods,
	       struct dcs_iod_csums *iods_csums, d_sg_list_t *sgls)
{
	return vos_obj_update_ex(coh, oid, epoch, pm_ver, flags, dkey, iod_nr,
				 iods, iods_csums, sgls, NULL);
}

int
vos_obj_array_remove(daos_handle_t coh, daos_unit_oid_t oid,
		     const daos_epoch_range_t *epr, const daos_key_t *dkey,
		     const daos_key_t *akey, const daos_recx_t *recx)
{
	struct vos_io_context	*ioc;
	daos_iod_t		 iod;
	daos_handle_t		 ioh;
	int			 rc;

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_recxs = (daos_recx_t *)recx;
	iod.iod_nr = 1;
	iod.iod_name = *akey;
	iod.iod_size = 0;

	rc = vos_update_begin(coh, oid, epr->epr_hi, VOS_OF_REMOVE,
			      (daos_key_t *)dkey, 1, &iod, NULL, false, 0,
			      &ioh, NULL);
	if (rc) {
		D_ERROR("Update "DF_UOID" failed "DF_RC"\n", DP_UOID(oid),
			DP_RC(rc));
		return rc;
	}

	ioc = vos_ioh2ioc(ioh);
	/** Set lower bound of epoch range */
	ioc->ic_epr.epr_lo = epr->epr_lo;

	rc = vos_update_end(ioh, 0 /* don't care */, (daos_key_t *)dkey, rc,
			    NULL);
	return rc;
}

int
vos_obj_fetch_ex(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
		 daos_iod_t *iods, d_sg_list_t *sgls, struct dtx_handle *dth)
{
	daos_handle_t	ioh;
	bool		size_fetch = (sgls == NULL);
	uint32_t	fetch_flags = size_fetch ? VOS_OF_FETCH_SIZE_ONLY : 0;
	uint32_t	vos_flags = flags | fetch_flags;
	int		rc;

	rc = vos_fetch_begin(coh, oid, epoch, dkey, iod_nr, iods,
			     vos_flags, NULL, &ioh, dth);
	if (rc) {
		VOS_TX_TRACE_FAIL(rc, "Cannot fetch "DF_UOID": "DF_RC"\n",
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

int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
	      daos_iod_t *iods, d_sg_list_t *sgls)
{
	return vos_obj_fetch_ex(coh, oid, epoch, flags, dkey, iod_nr, iods,
				sgls, NULL);
}

/**
 * @} vos_obj_update() & vos_obj_fetch() functions
 */
