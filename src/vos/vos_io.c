/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "vos_policy.h"
#include <daos/mem.h>

/** I/O context */
struct vos_io_context {
	EVT_ENT_ARRAY_LG_PTR(ic_ent_array);
	/** The epoch bound including uncertainty */
	daos_epoch_t		 ic_bound;
	daos_epoch_range_t	 ic_epr;
	daos_unit_oid_t		 ic_oid;
	struct vos_container	*ic_cont;
	daos_iod_t		*ic_iods;
	struct dcs_iod_csums	*ic_iod_csums;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** BIO descriptor, has ic_iod_nr SGLs */
	struct bio_desc		*ic_biod;
	struct vos_ts_set	*ic_ts_set;
	/** Checksums for bio_iovs in \ic_biod */
	struct dcs_ci_list	 ic_csum_list;
	/** current dkey info */
	struct vos_ilog_info	 ic_dkey_info;
	/** current akey info */
	struct vos_ilog_info	 ic_akey_info;
	/** cursor of SGL & IOV in BIO descriptor */
	unsigned int		 ic_sgl_at;
	unsigned int		 ic_iov_at;
	/** reserved SCM extents */
	struct umem_rsrvd_act	*ic_rsrvd_scm;
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
	/** duped SG lists for dedup verify */
	struct bio_sglist	*ic_dedup_bsgls;
	/** bulk data buffers for dedup verify */
	struct bio_desc		**ic_dedup_bufs;
	/** the total size of the IO */
	uint64_t		 ic_io_size;
	/** flags */
	unsigned int		 ic_update:1,
				 ic_size_fetch:1,
				 ic_save_recx:1,
				 ic_dedup:1, /** candidate for dedup */
				 ic_dedup_verify:1,
				 ic_read_ts_only:1,
				 ic_check_existence:1,
				 ic_remove:1,
				 ic_skip_fetch:1,
				 ic_agg_needed:1,
				 ic_ec:1; /**< see VOS_OF_EC */
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
		D_ERROR(DF_UUID ": Init dedup hash failed. " DF_RC "\n", DP_UUID(pool->vp_id),
			DP_RC(rc));
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
		BIO_ADDR_SET_DEDUP(&biov->bi_addr);
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

	if (!ci_is_valid(csum) || csum_len == 0 ||
	    BIO_ADDR_IS_DEDUP(&biov->bi_addr))
		return;

	if (bio_addr_is_hole(&biov->bi_addr))
		return;

	if (vos_dedup_lookup(pool, csum, csum_len, NULL))
		return;

	D_ALLOC_PTR(entry);
	if (entry == NULL) {
		return;
	}
	D_INIT_LIST_HEAD(&entry->de_link);

	D_ASSERT(csum_len != 0);
	D_ALLOC(entry->de_csum_buf, csum_len);
	if (entry->de_csum_buf == NULL) {
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

static void
vos_dedup_free_bsgl(struct vos_io_context *ioc, unsigned int sgl_idx,
		    unsigned int *buf_idx)
{
	struct bio_sglist	*bsgl_dup;
	int			 i;

	D_ASSERT(*buf_idx >= sgl_idx);
	bsgl_dup = &ioc->ic_dedup_bsgls[sgl_idx];
	D_ASSERT(bsgl_dup != NULL);

	for (i = 0; i < bsgl_dup->bs_nr_out; i++) {
		struct bio_iov	*biov = &bsgl_dup->bs_iovs[i];

		if (biov->bi_buf == NULL)
			goto next;

		D_ASSERT(!BIO_ADDR_IS_DEDUP(&biov->bi_addr));
		if (!BIO_ADDR_IS_DEDUP_BUF(&biov->bi_addr))
			goto next;

		biov->bi_buf = NULL;
		D_ASSERT(ioc->ic_dedup_bufs[*buf_idx] != NULL);
		bio_buf_free(ioc->ic_dedup_bufs[*buf_idx]);
		ioc->ic_dedup_bufs[*buf_idx] = NULL;
next:
		D_ASSERT(ioc->ic_dedup_bufs[*buf_idx] == NULL);
		(*buf_idx)++;
	}
	bio_sgl_fini(bsgl_dup);
}

static struct vos_io_context *
vos_ioh2ioc(daos_handle_t ioh)
{
	return (struct vos_io_context *)ioh.cookie;
}

static void
vos_dedup_verify_fini(daos_handle_t ioh)
{
	struct vos_io_context	*ioc;
	unsigned int		 buf_idx = 0;
	int			 i;

	D_ASSERT(daos_handle_is_valid(ioh));
	ioc = vos_ioh2ioc(ioh);

	if (ioc->ic_dedup_bsgls == NULL) {
		D_ASSERT(ioc->ic_dedup_bufs == NULL);
		return;
	}

	D_ASSERT(ioc->ic_dedup_verify);
	D_ASSERT(ioc->ic_dedup_bufs != NULL);

	for (i = 0; i < ioc->ic_iod_nr; i++)
		vos_dedup_free_bsgl(ioc, i, &buf_idx);

	D_FREE(ioc->ic_dedup_bsgls);
	D_FREE(ioc->ic_dedup_bufs);
}

static int
vos_dedup_dup_bsgl(struct vos_io_context *ioc, unsigned int sgl_idx,
		   unsigned int *buf_idx, void *bulk_ctxt,
		   unsigned int bulk_perm)
{
	struct bio_io_context	*bioc;
	struct bio_desc		*buf;
	struct bio_sglist	*bsgl, *bsgl_dup;
	int			 i, rc;

	D_ASSERT(ioc->ic_dedup_verify);
	D_ASSERT(*buf_idx >= sgl_idx);

	bsgl = bio_iod_sgl(ioc->ic_biod, sgl_idx);
	D_ASSERT(bsgl != NULL);
	bsgl_dup = &ioc->ic_dedup_bsgls[sgl_idx];
	D_ASSERT(bsgl_dup != NULL);

	rc = bio_sgl_init(bsgl_dup, bsgl->bs_nr_out);
	if (rc != 0)
		return rc;

	bsgl_dup->bs_nr_out = bsgl->bs_nr_out;

	bioc = vos_data_ioctxt(ioc->ic_cont->vc_pool);
	for (i = 0; i < bsgl->bs_nr_out; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		struct bio_iov	*biov_dup = &bsgl_dup->bs_iovs[i];

		if (bio_iov2buf(biov) == NULL)
			goto next;

		*biov_dup = *biov;
		/* Original biov isn't deduped, don't duplicate buffer */
		if (!BIO_ADDR_IS_DEDUP(&biov->bi_addr))
			goto next;

		D_ASSERT(bio_iov2len(biov) != 0);
		buf = bio_buf_alloc(bioc, bio_iov2len(biov), bulk_ctxt,
				    bulk_perm);
		if (buf == NULL) {
			D_ERROR("Failed to alloc "DF_U64" bytes\n",
				bio_iov2len(biov));
			/* clear original/copied buffer addr */
			biov_dup->bi_buf = NULL;
			return -DER_NOMEM;
		}
		ioc->ic_dedup_bufs[*buf_idx] = buf;

		biov_dup->bi_buf = bio_buf_addr(buf);
		D_ASSERT(biov_dup->bi_buf != NULL);

		BIO_ADDR_CLEAR_DEDUP(&biov_dup->bi_addr);
		BIO_ADDR_SET_DEDUP_BUF(&biov_dup->bi_addr);
		biov_dup->bi_addr.ba_off = UMOFF_NULL;
next:
		(*buf_idx)++;
	}

	return 0;
}

int
vos_dedup_verify_init(daos_handle_t ioh, void *bulk_ctxt,
		      unsigned int bulk_perm)
{
	struct vos_io_context	*ioc;
	struct bio_sglist	*bsgl;
	unsigned int		 buf_idx = 0;
	int			 i, rc = 0;

	D_ASSERT(daos_handle_is_valid(ioh));
	ioc = vos_ioh2ioc(ioh);

	if (!ioc->ic_dedup_verify)
		return 0;

	D_ASSERT(ioc->ic_dedup_bsgls == NULL);
	D_ALLOC_ARRAY(ioc->ic_dedup_bsgls, ioc->ic_iod_nr);
	if (ioc->ic_dedup_bsgls == NULL)
		return -DER_NOMEM;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		bsgl = bio_iod_sgl(ioc->ic_biod, i);
		D_ASSERT(bsgl != NULL);

		buf_idx += bsgl->bs_nr_out;
	}

	D_ASSERT(buf_idx > 0);
	D_ALLOC_ARRAY(ioc->ic_dedup_bufs, buf_idx);
	if (ioc->ic_dedup_bufs == NULL) {
		D_FREE(ioc->ic_dedup_bsgls);
		return -DER_NOMEM;
	}

	buf_idx = 0;
	for (i = 0; i < ioc->ic_iod_nr; i++) {
		rc = vos_dedup_dup_bsgl(ioc, i, &buf_idx,
					bulk_ctxt, bulk_perm);
		if (rc)
			break;
	}

	if (rc)
		vos_dedup_verify_fini(ioh);

	return rc;
}

static inline struct umem_instance *
vos_ioc2umm(struct vos_io_context *ioc)
{
	return &ioc->ic_cont->vc_pool->vp_umm;
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
	if (ioc->ic_rsrvd_scm != NULL) {
		D_ASSERT(umem_rsrvd_act_cnt(ioc->ic_rsrvd_scm) == 0);
		umem_rsrvd_act_free(&ioc->ic_rsrvd_scm);
	}

	D_ASSERT(d_list_empty(&ioc->ic_blk_exts));
	D_ASSERT(d_list_empty(&ioc->ic_dedup_entries));
	D_FREE(ioc->ic_umoffs);
}

static int
vos_ioc_reserve_init(struct vos_io_context *ioc, struct dtx_handle *dth)
{
	struct umem_rsrvd_act	*scm;
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

	umem_rsrvd_act_alloc(vos_ioc2umm(ioc), &ioc->ic_rsrvd_scm, total_acts);
	if (ioc->ic_rsrvd_scm == NULL)
		return -DER_NOMEM;

	if (!dtx_is_valid_handle(dth) || dth->dth_deferred == NULL)
		return 0;

	/** Reserve enough space for any deferred actions */
	umem_rsrvd_act_alloc(vos_ioc2umm(ioc), &scm, total_acts);
	if (scm == NULL) {
		D_FREE(ioc->ic_rsrvd_scm);
		return -DER_NOMEM;
	}

	dth->dth_deferred[dth->dth_deferred_cnt++] = scm;

	return 0;
}

static void
vos_ioc_destroy(struct vos_io_context *ioc, bool evict)
{
	if (ioc->ic_biod != NULL)
		bio_iod_free(ioc->ic_biod);

	dcs_csum_info_list_fini(&ioc->ic_csum_list);

	if (ioc->ic_obj)
		vos_obj_release(vos_obj_cache_current(ioc->ic_cont->vc_pool->vp_sysdb),
				ioc->ic_obj, evict);

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
	       uint32_t dedup_th, struct dtx_handle *dth,
	       struct vos_io_context **ioc_pp)
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

	ioc->ic_io_size = 0;
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
	ioc->ic_dedup = ((vos_flags & VOS_OF_DEDUP) != 0);
	ioc->ic_dedup_verify = ((vos_flags & VOS_OF_DEDUP_VERIFY) != 0);
	ioc->ic_skip_fetch = ((vos_flags & VOS_OF_SKIP_FETCH) != 0);
	ioc->ic_agg_needed = 0; /** Will be set if we detect a need for aggregation */
	ioc->ic_dedup_th = dedup_th;
	if (vos_flags & VOS_OF_FETCH_CHECK_EXISTENCE)
		ioc->ic_read_ts_only = ioc->ic_check_existence = 1;
	else if (vos_flags & VOS_OF_FETCH_SET_TS_ONLY)
		ioc->ic_read_ts_only = 1;
	ioc->ic_remove = ((vos_flags & VOS_OF_REMOVE) != 0);
	ioc->ic_ec = ((vos_flags & VOS_OF_EC) != 0);
	ioc->ic_umoffs_cnt = ioc->ic_umoffs_at = 0;
	ioc->ic_iod_csums = iod_csums;
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

	cont = vos_hdl2cont(coh);
	rc = vos_ts_set_allocate(&ioc->ic_ts_set, vos_flags, cflags, iod_nr,
				 dth, cont->vc_pool->vp_sysdb);
	if (rc != 0)
		goto error;

	if (ioc->ic_read_ts_only || ioc->ic_check_existence) {
		*ioc_pp = ioc;
		return 0;
	}

	bioc = vos_data_ioctxt(cont->vc_pool);
	ioc->ic_biod = bio_iod_alloc(bioc, vos_ioc2umm(ioc), iod_nr,
			read_only ? BIO_IOD_TYPE_FETCH : BIO_IOD_TYPE_UPDATE);
	if (ioc->ic_biod == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	rc = dcs_csum_info_list_init(&ioc->ic_csum_list, iod_nr);
	if (rc != 0)
		goto error;

	for (i = 0; i < iod_nr; i++) {
		int iov_nr = iods[i].iod_nr;
		struct bio_sglist *bsgl;

		if ((iods[i].iod_type == DAOS_IOD_SINGLE && iov_nr != 1)) {
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

		D_REALLOC_ARRAY(biovs, bsgl->bs_iovs, iov_nr, iov_nr * 2);
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

/** Save the checksum to a list that can be retrieved later */
static int
save_csum(struct vos_io_context *ioc, struct dcs_csum_info *csum_info,
	  struct evt_entry *entry, daos_size_t rec_size)
{
	struct dcs_csum_info ci_duplicate;

	if (ioc->ic_size_fetch)
		return 0;

	if (entry == NULL)
		return dcs_csum_info_save(&ioc->ic_csum_list, csum_info);

	ci_duplicate = *csum_info;
	evt_entry_csum_update(&entry->en_ext, &entry->en_sel_ext, &ci_duplicate, rec_size);
	return dcs_csum_info_save(&ioc->ic_csum_list, &ci_duplicate);
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
	bool			 standalone = ioc->ic_cont->vc_pool->vp_sysdb;

	d_iov_set(&kiov, &key, sizeof(key));
	key.sk_epoch	= ioc->ic_bound;
	key.sk_minor_epc = VOS_SUB_OP_MAX;

	tree_rec_bundle2iov(&rbund, &riov);
	memset(&biov, 0, sizeof(biov));
	rbund.rb_biov	= &biov;
	rbund.rb_csum = &csum_info;

	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &kiov, &kiov,
			  &riov);
	if (vos_dtx_hit_inprogress(standalone))
		D_GOTO(out, rc = (rc == 0 ? -DER_INPROGRESS : rc));

	if (rc == -DER_NONEXIST) {
		rbund.rb_gsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
		rc = 0;
	} else if (rc != 0) {
		goto out;
	} else if (key.sk_epoch < epr->epr_lo) {
		/* The single value is before the valid epoch range (after a
		 * punch when incarnation log is available)
		 */
		rc = 0;
		rbund.rb_gsize = 0;
		bio_addr_set_hole(&biov.bi_addr, 1);
	} else if (key.sk_epoch > epr->epr_hi) {
		/* Uncertainty violation */
		D_GOTO(out, rc = -DER_TX_RESTART);
	}

	if (ci_is_valid(&csum_info))
		save_csum(ioc, &csum_info, NULL, 0);

	if (BIO_ADDR_IS_CORRUPTED(&rbund.rb_biov->bi_addr)) {
		D_DEBUG(DB_CSUM, "Found corrupted record\n");
		return -DER_CSUM;
	}

	rc = iod_fetch(ioc, &biov);
	if (rc != 0)
		goto out;

	*rsize = rbund.rb_gsize;
	ioc->ic_io_size += rbund.rb_rsize;
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
	struct evt_filter	 filter;
	struct bio_iov		 biov = {0};
	daos_size_t		 holes; /* hole width */
	daos_size_t		 rsize;
	daos_off_t		 index;
	daos_off_t		 end;
	bool			 csum_enabled = false;
	bool			 with_shadow = (shadow_ep != DAOS_EPOCH_MAX);
	uint32_t		 inob;
	int			 rc;
	bool			 standalone = ioc->ic_cont->vc_pool->vp_sysdb;

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
	evt_ent_array_init(ioc->ic_ent_array, 0);

	rc = evt_find(toh, &filter, ioc->ic_ent_array);
	if (rc != 0 || vos_dtx_hit_inprogress(standalone))
		D_GOTO(failed, rc = (rc == 0 ? -DER_INPROGRESS : rc));

	holes = 0;
	rsize = 0;
	inob = ioc->ic_ent_array->ea_inob;
	if (ioc->ic_skip_fetch)
		goto fill;

	evt_ent_array_for_each(ent, ioc->ic_ent_array) {
		daos_off_t	 lo = ent->en_sel_ext.ex_lo;
		daos_off_t	 hi = ent->en_sel_ext.ex_hi;
		daos_size_t	 nr;

		D_ASSERTF(hi >= lo, "hi < lo, filter.fr_ex: " DF_EXT ", ent: " DF_ENT "\n",
			  DP_EXT(&filter.fr_ex), DP_ENT(ent));
		nr = hi - lo + 1;

		if (BIO_ADDR_IS_CORRUPTED(&ent->en_addr)) {
			D_DEBUG(DB_CSUM, "Found corrupted entity: "DF_ENT"\n",
				DP_ENT(ent));
			rc = -DER_CSUM;
			goto failed;
		}

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
					       shadow_ep, inob,
					       DRT_SHADOW);
				if (rc != 0)
					goto failed;
			}
			biov_set_hole(&biov, holes * inob);
			/* skip the hole */
			rc = iod_fetch(ioc, &biov);
			if (rc != 0)
				goto failed;
			holes = 0;
		}

		if (rsize == 0)
			rsize = inob;
		D_ASSERT(rsize == inob);

		if (ioc->ic_save_recx) {
			rc = save_recx(ioc, lo, nr, ent->en_epoch,
				       inob, DRT_NORMAL);
			if (rc != 0)
				goto failed;
		}
		bio_iov_set(&biov, ent->en_addr, nr * inob);
		ioc->ic_io_size += nr * inob;
		if (ci_is_valid(&ent->en_csum)) {
			rc = save_csum(ioc, &ent->en_csum, ent, rsize);
			if (rc != 0)
				goto failed;
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

fill:
	D_ASSERT(index <= end);
	if (index < end)
		holes += end - index;

	if (holes != 0) { /* trailing holes */
		if (with_shadow) {
			rc = save_recx(ioc, end - holes, holes, shadow_ep,
				       inob, DRT_SHADOW);
			if (rc != 0)
				goto failed;
		}
		biov_set_hole(&biov, holes * inob);
		rc = iod_fetch(ioc, &biov);
		if (rc != 0)
			goto failed;
	}
	if (rsize_p && *rsize_p == 0)
		*rsize_p = rsize;
failed:
	evt_ent_array_fini(ioc->ic_ent_array);
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
	       struct vos_ilog_info *info, bool has_cond)
{
	struct umem_instance	*umm;
	daos_epoch_range_t	 epr = ioc->ic_epr;
	int			 rc;

	umm = vos_obj2umm(ioc->ic_obj);
	rc = vos_ilog_fetch(umm, vos_cont2hdl(ioc->ic_cont),
			    DAOS_INTENT_DEFAULT, &krec->kr_ilog,
			    epr.epr_hi, ioc->ic_bound, has_cond, NULL, parent, info);
	if (rc != 0)
		goto out;

	rc = vos_ilog_check(info, &epr, epr_out, true);
out:
	D_CDEBUG(rc == 0, DB_TRACE, DB_IO, "ilog check returned "DF_RC" epr_in="
		 DF_X64"-"DF_X64" punch="DF_PUNCH" epr_out="DF_X64"-"DF_X64"\n",
		 DP_RC(rc), epr.epr_lo, epr.epr_hi, DP_PUNCH(&info->ii_prior_punch),
		 epr_out ? epr_out->epr_lo : 0, epr_out ? epr_out->epr_hi : 0);
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
	bool		standalone = ioc->ic_cont->vc_pool->vp_sysdb;

	if (*rc == 0)
		return false;

	if (*rc != -DER_NONEXIST)
		return true;

	if (vos_dtx_hit_inprogress(standalone)) {
		*rc = -DER_INPROGRESS;
		return true;
	}

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
	bool			 has_cond = false;
	struct daos_recx_ep_list *shadow;
	bool			 standalone = ioc->ic_cont->vc_pool->vp_sysdb;

	D_DEBUG(DB_IO, "akey "DF_KEY" fetch %s epr "DF_X64"-"DF_X64"\n",
		DP_KEY(&iod->iod_name),
		iod->iod_type == DAOS_IOD_ARRAY ? "array" : "single",
		ioc->ic_epr.epr_lo, ioc->ic_epr.epr_hi);

	if (is_array) {
		if (iod->iod_nr == 0 || iod->iod_recxs == NULL) {
			D_ASSERT(iod->iod_nr == 0 && iod->iod_recxs == NULL);
			D_DEBUG(DB_TRACE, "akey "DF_KEY" fetch array bypassed - NULL iod_recxs.\n",
				DP_KEY(&iod->iod_name));
			return 0;
		}
		flags |= SUBTR_EVT;
	}

	rc = key_tree_prepare(ioc->ic_obj, ak_toh,
			      VOS_BTR_AKEY, &iod->iod_name, flags,
			      DAOS_INTENT_DEFAULT, &krec,
			      (ioc->ic_check_existence || ioc->ic_read_ts_only) ? NULL : &toh,
			      ioc->ic_ts_set);

	if (stop_check(ioc, VOS_OF_COND_AKEY_FETCH, iod, &rc, true)) {
		if (rc == 0 && !ioc->ic_read_ts_only)
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
		VOS_TX_LOG_FAIL(rc, "Failed to get akey "DF_KEY" "DF_RC"\n",
				DP_KEY(&iod->iod_name), DP_RC(rc));
		goto out;
	}

	if (ioc->ic_ts_set != NULL) {
		if (ioc->ic_ts_set->ts_flags & VOS_OF_COND_PER_AKEY &&
		    iod->iod_flags & VOS_OF_COND_AKEY_FETCH) {
			has_cond = true;
		} else if (!(ioc->ic_ts_set->ts_flags & VOS_OF_COND_PER_AKEY) &&
			   ioc->ic_ts_set->ts_flags & VOS_OF_COND_AKEY_FETCH) {
			has_cond = true;
		}
	}

	rc = key_ilog_check(ioc, krec, &ioc->ic_dkey_info, &val_epr,
			    &ioc->ic_akey_info, has_cond);

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

			if (vos_dtx_continue_detect(rc, standalone))
				continue;

			if (rc != 0) {
				VOS_TX_LOG_FAIL(rc, "Failed to fetch index %d: "
						DF_RC"\n", i, DP_RC(rc));
				goto out;
			}
		}

		if (vos_dtx_hit_inprogress(standalone)) {
			D_DEBUG(DB_IO, "inprogress %d: idx %lu, nr %lu rsize "
				DF_U64"\n", i,
				(unsigned long)iod->iod_recxs[i].rx_idx,
				(unsigned long)iod->iod_recxs[i].rx_nr, rsize);
			continue;
		}

		D_DEBUG(DB_IO, "read IOD at %d: idx %lu, nr %lu rsize "
			DF_U64"\n", i, (unsigned long)iod->iod_recxs[i].rx_idx,
			(unsigned long)iod->iod_recxs[i].rx_nr, rsize);

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

	if (vos_dtx_hit_inprogress(standalone))
		goto out;

	ioc_trim_tail_holes(ioc);
out:
	if (daos_handle_is_valid(toh))
		key_tree_release(toh, is_array);

	return vos_dtx_hit_inprogress(standalone) ? -DER_INPROGRESS : rc;
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
	bool			 has_cond;
	bool			 standalone = ioc->ic_cont->vc_pool->vp_sysdb;

	rc = obj_tree_init(obj);
	if (rc != 0)
		return rc;

	rc = key_tree_prepare(obj, obj->obj_toh, VOS_BTR_DKEY,
			      dkey, 0, DAOS_INTENT_DEFAULT, &krec,
			      &toh, ioc->ic_ts_set);
	if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL,
		       &rc, true)) {
		D_DEBUG(DB_IO, "Stop fetch "DF_UOID": "DF_RC"\n", DP_UOID(obj->obj_id),
			DP_RC(rc));
		if (rc == 0 && !ioc->ic_read_ts_only) {
			for (i = 0; i < ioc->ic_iod_nr; i++)
				iod_empty_sgl(ioc, i);
		} else {
			VOS_TX_LOG_FAIL(rc, "Failed to fetch dkey: "DF_RC"\n",
					DP_RC(rc));
		}
		goto out;
	}

	if (ioc->ic_ts_set != NULL && ioc->ic_ts_set->ts_flags & VOS_OF_COND_DKEY_FETCH)
		has_cond = true;
	else
		has_cond = false;

	rc = key_ilog_check(ioc, krec, &obj->obj_ilog_info, &ioc->ic_epr,
			    &ioc->ic_dkey_info, has_cond);

	if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL,
		       &rc, false)) {
		if (rc == 0 && !ioc->ic_read_ts_only) {
			D_DEBUG(DB_IO, "Stop fetch "DF_UOID": "DF_RC"\n", DP_UOID(obj->obj_id),
				DP_RC(rc));
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
		if (vos_dtx_continue_detect(rc, standalone))
			continue;

		if (rc != 0)
			break;
	}

	/* Add this check to prevent some new added logic after above for(). */
	if (vos_dtx_hit_inprogress(standalone))
		goto out;

out:
	if (daos_handle_is_valid(toh))
		key_tree_release(toh, false);

	return vos_dtx_hit_inprogress(standalone) ? -DER_INPROGRESS : rc;
}

uint64_t
vos_get_io_size(daos_handle_t ioh)
{
	return vos_ioh2ioc(ioh)->ic_io_size;
}

int
vos_fetch_end(daos_handle_t ioh, daos_size_t *size, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	/* NB: it's OK to use the stale ioc->ic_obj for fetch_end */
	D_ASSERT(!ioc->ic_update);
	if (size != NULL && err == 0)
		*size = ioc->ic_io_size;
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
			    NULL, vos_flags, shadows, 0, dth, &ioc);
	if (rc != 0)
		return rc;

	vos_dth_set(dth, ioc->ic_cont->vc_pool->vp_sysdb);

	rc = vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0);
	D_ASSERT(rc == 0);

	rc = vos_obj_hold(vos_obj_cache_current(ioc->ic_cont->vc_pool->vp_sysdb),
			  ioc->ic_cont, oid, &ioc->ic_epr, ioc->ic_bound, VOS_OBJ_VISIBLE,
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
	vos_dth_set(NULL, ioc->ic_cont->vc_pool->vp_sysdb);

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
		return vos_fetch_end(vos_ioc2ioh(ioc), NULL, rc);
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
	struct dcs_csum_info	*value_csum;
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

	value_csum = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);

	if (value_csum != NULL)
		rbund.rb_csum	= value_csum;
	else
		rbund.rb_csum	= &csum;

	rbund.rb_biov		= biov;
	rbund.rb_rsize		= rsize;
	rbund.rb_gsize		= gsize;
	rbund.rb_off		= umoff;
	rbund.rb_ver		= pm_ver;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0)
		D_ERROR("Failed to update subtree: "DF_RC"\n", DP_RC(rc));

	ioc->ic_io_size += rsize;

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
	ioc->ic_io_size += recx->rx_nr * rsize;
	biov = iod_update_biov(ioc);
	ent.ei_addr = biov->bi_addr;
	/* Don't make this flag persistent */
	BIO_ADDR_CLEAR_DEDUP(&ent.ei_addr);

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
vos_evt_mark_agg(struct umem_instance *umm, struct evt_root *root, daos_epoch_t epoch)
{
	uint64_t	feats;

	feats = evt_feats_get(root);

	vos_feats_agg_time_update(epoch, &feats);

	return evt_feats_set(root, umm, feats);
}

static int
vos_btr_mark_agg(struct umem_instance *umm, struct btr_root *root, daos_epoch_t epoch)
{
	uint64_t	feats;

	feats = dbtree_feats_get(root);

	vos_feats_agg_time_update(epoch, &feats);

	return dbtree_feats_set(root, umm, feats);
}

int
vos_key_mark_agg(struct vos_container *cont, struct vos_krec_df *krec, daos_epoch_t epoch)
{
	struct umem_instance	*umm;

	if ((cont->vc_pool->vp_feats & VOS_POOL_FEAT_AGG_OPT) == 0)
		return 0;

	umm = vos_cont2umm(cont);
	if (krec->kr_bmap & KREC_BF_BTR)
		return vos_btr_mark_agg(umm, &krec->kr_btr, epoch);

	return vos_evt_mark_agg(umm, &krec->kr_evt, epoch);
}

int
vos_mark_agg(struct vos_container *cont, struct btr_root *dkey_root, struct btr_root *obj_root,
	     daos_epoch_t epoch)
{
	struct umem_instance	*umm;
	int			 rc;

	if ((cont->vc_pool->vp_feats & VOS_POOL_FEAT_AGG_OPT) == 0)
		return 0;

	umm = vos_cont2umm(cont);
	rc = vos_btr_mark_agg(umm, dkey_root, epoch);
	if (rc == 0)
		rc = vos_btr_mark_agg(umm, obj_root, epoch);

	return rc;
}

static int
vos_ioc_mark_agg(struct vos_io_context *ioc)
{
	if (!ioc->ic_agg_needed)
		return 0;

	return vos_mark_agg(ioc->ic_cont, &ioc->ic_obj->obj_df->vo_tree,
			    &ioc->ic_cont->vc_cont_df->cd_obj_root, ioc->ic_epr.epr_hi);
}

static int
akey_update(struct vos_io_context *ioc, uint32_t pm_ver, daos_handle_t ak_toh,
	    uint16_t minor_epc)
{
	struct vos_object	*obj = ioc->ic_obj;
	struct vos_krec_df	*krec = NULL;
	daos_iod_t		*iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct dcs_csum_info	*iod_csums = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);
	struct dcs_csum_info	*recx_csum;
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
	if (rc < 0) {
		D_ERROR("akey "DF_KEY" update, key_tree_prepare failed, "DF_RC"\n",
			DP_KEY(&iod->iod_name), DP_RC(rc));
		return rc;
	}

	if (rc == 1) {
		rc = 0;
		ioc->ic_agg_needed = 1;
	}

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
		uint64_t	gsize = iod->iod_size;

		/* See obj_singv_ec_rw_filter. */
		if (ioc->ic_ec && iod->iod_recxs != NULL)
			gsize = (uintptr_t)iod->iod_recxs;

		rc = akey_update_single(toh, pm_ver, iod->iod_size, gsize, ioc,
					minor_epc);
		if (rc)
			D_ERROR("akey "DF_KEY" update, akey_update_single failed, "DF_RC"\n",
				DP_KEY(&iod->iod_name), DP_RC(rc));
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

		recx_csum = recx_csum_at(iod_csums, i, iod);
		rc = akey_update_recx(toh, pm_ver, &iod->iod_recxs[i],
				      recx_csum, iod->iod_size, ioc,
				      minor_epc);
		if (rc == 1) {
			ioc->ic_agg_needed = 1;
			rc = 0;
		}
		if (rc != 0) {
			VOS_TX_LOG_FAIL(rc, DF_UOID" akey "DF_KEY" update, akey_update_recx"
					" failed, "DF_RC"\n", DP_UOID(obj->obj_id),
					DP_KEY(&iod->iod_name), DP_RC(rc));
			goto out;
		}
	}

out:
	if (daos_handle_is_valid(toh))
		key_tree_release(toh, is_array);

	if (rc == 0 && ioc->ic_agg_needed)
		rc = vos_key_mark_agg(ioc->ic_cont, krec, ioc->ic_epr.epr_hi);

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

	if (rc == 0 && ioc->ic_agg_needed)
		rc = vos_key_mark_agg(ioc->ic_cont, krec, ioc->ic_epr.epr_hi);

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
vos_reserve_scm(struct vos_container *cont, struct umem_rsrvd_act *rsrvd_scm,
		daos_size_t size)
{
	umem_off_t	umoff;

	D_ASSERT(size > 0);

	if (vos_cont2umm(cont)->umm_ops->mo_reserve != NULL) {
		umoff = umem_reserve(vos_cont2umm(cont), rsrvd_scm, size);
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
	uint64_t	now;
	int		rc;

	if (media == DAOS_MEDIA_SCM) {
		umem_off_t	umoff;

		umoff = vos_reserve_scm(ioc->ic_cont, ioc->ic_rsrvd_scm, size);
		if (!UMOFF_IS_NULL(umoff)) {
			ioc->ic_umoffs[ioc->ic_umoffs_cnt] = umoff;
			ioc->ic_umoffs_cnt++;
			*off = umoff;
			return 0;
		}

		now = daos_gettime_coarse();
		if (now - ioc->ic_cont->vc_io_nospc_ts > VOS_NOSPC_ERROR_INTVL) {
			D_ERROR("Reserve "DF_U64" from SCM failed\n", size);
			umempobj_log_fraginfo(vos_cont2pool(ioc->ic_cont)->vp_umm.umm_pool);
			ioc->ic_cont->vc_io_nospc_ts = now;
		}
		return -DER_NOSPACE;
	}

	D_ASSERT(media == DAOS_MEDIA_NVME);
	rc = vos_reserve_blocks(ioc->ic_cont, &ioc->ic_blk_exts, size, VOS_IOS_GENERIC, off);
	if (rc == -DER_NOSPACE) {
		now = daos_gettime_coarse();
		if (now - ioc->ic_cont->vc_io_nospc_ts > VOS_NOSPC_ERROR_INTVL) {
			D_ERROR("Reserve "DF_U64" from NVMe failed. "DF_RC"\n", size, DP_RC(rc));
			ioc->ic_cont->vc_io_nospc_ts = now;
		}
	} else if (rc) {
		D_ERROR("Reserve "DF_U64" from NVMe failed. "DF_RC"\n", size, DP_RC(rc));
	}
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

	D_DEBUG(DB_TRACE, "media %d offset "DF_X64" size %zd\n",
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
	struct dcs_csum_info	*value_csum = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);

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
	irec = (struct vos_irec_df *)umem_off2ptr(vos_ioc2umm(ioc), umoff);
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
	struct dcs_csum_info	*iod_csums = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);
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

		media = vos_policy_media_select(vos_cont2pool(ioc->ic_cont),
					 iod->iod_type, size, VOS_IOS_GENERIC);

		if (iod->iod_type == DAOS_IOD_SINGLE) {
			rc = vos_reserve_single(ioc, media, size);
		} else {
			daos_size_t csum_len;

			recx_csum = recx_csum_at(iod_csums, i, iod);
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
vos_publish_scm(struct vos_container *cont, struct umem_rsrvd_act *rsrvd_scm,
		bool publish)
{
	int	rc = 0;

	if (publish)
		rc = umem_tx_publish(vos_cont2umm(cont), rsrvd_scm);
	else
		umem_cancel(vos_cont2umm(cont), rsrvd_scm);

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
	       daos_size_t *size, struct dtx_handle *dth)
{
	struct vos_dtx_act_ent	**daes = NULL;
	struct vos_dtx_cmt_ent	**dces = NULL;
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);
	struct umem_instance	*umem;
	bool			 tx_started = false;

	D_ASSERT(ioc->ic_update);
	vos_dedup_verify_fini(ioh);

	umem = vos_ioc2umm(ioc);

	if (err != 0)
		goto abort;

	err = vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0);
	D_ASSERT(err == 0);

	err = vos_tx_begin(dth, umem, ioc->ic_cont->vc_pool->vp_sysdb);
	if (err != 0)
		goto abort;

	tx_started = true;

	/* Commit the CoS DTXs via the IO PMDK transaction. */
	if (dtx_is_valid_handle(dth) && dth->dth_dti_cos_count > 0 &&
	    !dth->dth_cos_done) {
		D_ALLOC_ARRAY(daes, dth->dth_dti_cos_count);
		if (daes == NULL)
			D_GOTO(abort, err = -DER_NOMEM);

		D_ALLOC_ARRAY(dces, dth->dth_dti_cos_count);
		if (dces == NULL)
			D_GOTO(abort, err = -DER_NOMEM);

		err = vos_dtx_commit_internal(ioc->ic_cont, dth->dth_dti_cos,
					      dth->dth_dti_cos_count, 0, NULL, daes, dces);
		if (err <= 0)
			D_FREE(daes);
	}

	err = vos_obj_hold(vos_obj_cache_current(ioc->ic_cont->vc_pool->vp_sysdb),
			   ioc->ic_cont, ioc->ic_oid, &ioc->ic_epr, ioc->ic_bound,
			   VOS_OBJ_CREATE | VOS_OBJ_VISIBLE, DAOS_INTENT_UPDATE,
			   &ioc->ic_obj, ioc->ic_ts_set);
	if (err != 0)
		goto abort;

	/* Update tree index */
	err = dkey_update(ioc, pm_ver, dkey, dtx_is_valid_handle(dth) ?
			  dth->dth_op_seq : VOS_SUB_OP_MAX);
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

	if (err == 0 && ioc->ic_epr.epr_hi > ioc->ic_obj->obj_df->vo_max_write) {
		err = umem_tx_xadd_ptr(umem, &ioc->ic_obj->obj_df->vo_max_write,
				       sizeof(ioc->ic_obj->obj_df->vo_max_write),
				       UMEM_XADD_NO_SNAPSHOT);
		if (err == 0)
			ioc->ic_obj->obj_df->vo_max_write = ioc->ic_epr.epr_hi;
	}

	if (err == 0)
		err = vos_ioc_mark_agg(ioc);

	if (err == 0)
		vos_ts_set_upgrade(ioc->ic_ts_set);

	if (err == -DER_NONEXIST || err == -DER_EXIST || err == 0)
		vos_ts_set_update(ioc->ic_ts_set, ioc->ic_epr.epr_hi);

	if (err == 0)
		vos_ts_set_wupdate(ioc->ic_ts_set, ioc->ic_epr.epr_hi);

	err = vos_tx_end(ioc->ic_cont, dth, &ioc->ic_rsrvd_scm,
			 &ioc->ic_blk_exts, tx_started, ioc->ic_biod, err);
	if (err == 0)
		vos_dedup_process(vos_cont2pool(ioc->ic_cont), &ioc->ic_dedup_entries, false);

	if (dtx_is_valid_handle(dth)) {
		if (err == 0)
			dth->dth_cos_done = 1;
		else
			dth->dth_cos_done = 0;

		if (daes != NULL)
			vos_dtx_post_handle(ioc->ic_cont, daes, dces, dth->dth_dti_cos_count,
					    false, err != 0);
	}

	if (err != 0)
		update_cancel(ioc);

	vos_space_unhold(vos_cont2pool(ioc->ic_cont), &ioc->ic_space_held[0]);

	if (size != NULL && err == 0)
		*size = ioc->ic_io_size;
	D_FREE(daes);
	D_FREE(dces);
	vos_ioc_destroy(ioc, err != 0);

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

void
vos_update_renew_epoch(daos_handle_t ioh, struct dtx_handle *dth)
{
	struct vos_io_context	*ioc = vos_ioh2ioc(ioh);

	D_ASSERT(dtx_is_valid_handle(dth));

	ioc->ic_epr.epr_hi = dth->dth_epoch;
	ioc->ic_bound = MAX(dth->dth_epoch_bound, ioc->ic_epr.epr_hi);
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
		 daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
		 uint32_t dedup_th, daos_handle_t *ioh, struct dtx_handle *dth)
{
	struct vos_io_context	*ioc;
	int			 rc;

	if (oid.id_shard % 3 == 1 && DAOS_FAIL_CHECK(DAOS_DTX_FAIL_IO))
		return -DER_IO;

	if (dtx_is_valid_handle(dth))
		epoch = dth->dth_epoch;

	D_DEBUG(DB_TRACE, "Prepare IOC for "DF_UOID", iod_nr %d, epc "
		DF_X64", flags="DF_X64"\n", DP_UOID(oid), iod_nr, epoch, flags);

	rc = vos_check_akeys(iod_nr, iods);
	if (rc != 0) {
		D_ERROR("Detected duplicate akeys, operation not allowed\n");
		return rc;
	}

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, iods_csums,
			    flags, NULL, dedup_th, dth, &ioc);
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
		D_ERROR(DF_UOID ": dkey update begin failed. " DF_RC "\n", DP_UOID(oid), DP_RC(rc));
		goto error;
	}
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	vos_update_end(vos_ioc2ioh(ioc), 0, dkey, rc, NULL, dth);
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

struct dcs_ci_list *
vos_ioh2ci(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	return &ioc->ic_csum_list;
}

uint32_t
vos_ioh2ci_nr(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	return ioc->ic_csum_list.dcl_csum_infos_nr;
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

	if (ioc->ic_dedup_verify) {
		D_ASSERT(ioc->ic_dedup_bsgls != NULL);
		return &ioc->ic_dedup_bsgls[idx];
	}

	return bio_iod_sgl(ioc->ic_biod, idx);
}

void *
vos_iod_bulk_at(daos_handle_t ioh, unsigned int sgl_idx, unsigned int iov_idx,
		unsigned int *bulk_off)
{
	struct vos_io_context	*ioc;
	struct bio_desc		*buf;
	struct bio_sglist	*bsgl_dup;
	int			 buf_idx, i;

	if (daos_handle_is_inval(ioh))
		return NULL;

	ioc = vos_ioh2ioc(ioh);
	if (ioc->ic_dedup_verify) {
		D_ASSERT(ioc->ic_dedup_bsgls != NULL);
		D_ASSERT(ioc->ic_dedup_bufs != NULL);

		buf_idx = 0;
		for (i = 0; i < sgl_idx; i++) {
			bsgl_dup = &ioc->ic_dedup_bsgls[i];

			buf_idx += bsgl_dup->bs_nr_out;
		}

		bsgl_dup = &ioc->ic_dedup_bsgls[sgl_idx];
		D_ASSERT(iov_idx < bsgl_dup->bs_nr_out);
		buf_idx += iov_idx;

		buf = ioc->ic_dedup_bufs[buf_idx];
		if (buf != NULL)
			return bio_buf_bulk(buf, bulk_off);
		/* Not deduped data, fallthrough to bio_iod_bulk() */
	}

	return bio_iod_bulk(ioc->ic_biod, sgl_idx, iov_idx, bulk_off);
}

void
vos_set_io_csum(daos_handle_t ioh, struct dcs_iod_csums *csums)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	D_ASSERT(ioc != NULL);

	ioc->ic_iod_csums = csums;
}

/*
 * Check if the dedup data is identical to the RDMA data in a temporal
 * allocated DRAM extent, if memcmp fails, allocate a new SCM extent and
 * update it's address in VOS tree, otherwise, keep using the original
 * dedup data address in VOS tree.
 */
int
vos_dedup_verify(daos_handle_t ioh)
{
	struct vos_io_context	*ioc;
	struct bio_sglist	*bsgl, *bsgl_dup;
	int			 i, j, rc;
	umem_off_t		 off;

	D_ASSERT(daos_handle_is_valid(ioh));
	ioc = vos_ioh2ioc(ioh);

	if (!ioc->ic_dedup_verify)
		return 0;

	D_ASSERT(ioc->ic_dedup_bsgls != NULL);
	for (i = 0; i < ioc->ic_iod_nr; i++) {
		bsgl = bio_iod_sgl(ioc->ic_biod, i);
		D_ASSERT(bsgl != NULL);
		bsgl_dup = &ioc->ic_dedup_bsgls[i];
		D_ASSERT(bsgl_dup != NULL);

		D_ASSERT(bsgl->bs_nr_out == bsgl_dup->bs_nr_out);
		for (j = 0; j < bsgl->bs_nr_out; j++) {
			struct bio_iov	*biov = &bsgl->bs_iovs[j];
			struct bio_iov	*biov_dup = &bsgl_dup->bs_iovs[j];
			bio_addr_t	*addr = &biov->bi_addr;
			bio_addr_t	*addr_dup = &biov_dup->bi_addr;

			/* Hole */
			if (bio_iov2buf(biov) == NULL) {
				D_ASSERT(bio_iov2buf(biov_dup) == NULL);
				continue;
			}

			/* Non-deduped extent */
			if (!BIO_ADDR_IS_DEDUP(addr)) {
				D_ASSERT(!BIO_ADDR_IS_DEDUP(addr_dup));
				D_ASSERT(!BIO_ADDR_IS_DEDUP_BUF(addr_dup));
				continue;
			}
			D_ASSERT(BIO_ADDR_IS_DEDUP_BUF(addr_dup));

			D_ASSERT(bio_iov2len(biov) == bio_iov2len(biov_dup));
			rc = memcmp(bio_iov2buf(biov), bio_iov2buf(biov_dup),
				    bio_iov2len(biov));

			if (rc == 0) {	/* verify succeeded */
				D_DEBUG(DB_IO, "Verify dedup succeeded\n");
				continue;
			}

			/*
			 * Allocate new extent and replace the deduped address
			 * with new allocated address, so that the new address
			 * will be updated in VOS tree in later tx commit.
			 *
			 * TODO:
			 * - Support NVMe;
			 * - Deal with SCM leak on tx commit failure or server
			 *   crash;
			 */
			off = umem_atomic_alloc(vos_ioc2umm(ioc),
						bio_iov2len(biov),
						UMEM_TYPE_ANY);
			if (off == UMOFF_NULL) {
				D_ERROR("Failed to alloc "DF_U64" bytes SCM\n",
					bio_iov2len(biov));
				goto error;
			}

			biov->bi_addr.ba_off = off;
			biov->bi_buf = umem_off2ptr(vos_ioc2umm(ioc),
						bio_iov2off(biov));
			BIO_ADDR_CLEAR_DEDUP(&biov->bi_addr);

			umem_atomic_copy(vos_ioc2umm(ioc),
					 biov->bi_buf, biov_dup->bi_buf,
					 bio_iov2len(biov), UMEM_COMMIT_IMMEDIATE);

			/* For error cleanup */
			biov_dup->bi_addr.ba_off = biov->bi_addr.ba_off;

			D_DEBUG(DB_IO, "Verify dedup extents failed, "
				"use newly allocated extent\n");
		}
	}

	return 0;
error:
	for (i = 0; i < ioc->ic_iod_nr; i++) {
		bsgl_dup = &ioc->ic_dedup_bsgls[i];

		for (j = 0; j < bsgl_dup->bs_nr_out; j++) {
			struct bio_iov	*biov_dup = &bsgl_dup->bs_iovs[j];

			if (bio_iov2off(biov_dup) == UMOFF_NULL)
				continue;

			umem_atomic_free(vos_ioc2umm(ioc),
					 bio_iov2off(biov_dup));
			biov_dup->bi_addr.ba_off = UMOFF_NULL;
		}
	}

	return -DER_NOSPACE;
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
	int rc;

	D_ASSERT(sgl_nr == ioc->ic_iod_nr);
	rc = bio_iod_prep(ioc->ic_biod, BIO_CHK_TYPE_IO, NULL, 0);
	if (rc)
		return rc;

	rc = bio_iod_copy(ioc->ic_biod, sgls, sgl_nr);
	rc = bio_iod_post(ioc->ic_biod, rc);

	return rc;
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
			      iods_csums, 0, &ioh, dth);
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

	rc = vos_update_end(ioh, pm_ver, dkey, rc, NULL, dth);
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
			      (daos_key_t *)dkey, 1, &iod, NULL, 0,
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
			    NULL, NULL);
	D_DEBUG(DB_IO, DF_UOID" remove "DF_RECX" epr_hi "DF_X64", epr_lo "
		DF_X64", "DF_RC"\n", DP_UOID(oid), DP_RECX(*recx), epr->epr_hi,
		epr->epr_lo, DP_RC(rc));
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

	rc = vos_fetch_end(ioh, NULL, rc);
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

int
vos_obj_layout_upgrade(daos_handle_t coh, daos_unit_oid_t oid, uint32_t layout_ver)
{
	return  vos_oi_upgrade_layout_ver(vos_hdl2cont(coh), oid, layout_ver);
}

/**
 * @} vos_obj_update() & vos_obj_fetch() functions
 */
