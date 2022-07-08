/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)
#include "bio_internal.h"

static int (*bulk_create_fn)(void *ctxt, d_sg_list_t *sgl, unsigned int perm,
			     void **bulk_hdl);
static int (*bulk_free_fn)(void *bulk_hdl);

void bio_register_bulk_ops(int (*bulk_create)(void *ctxt, d_sg_list_t *sgl,
					      unsigned int perm,
					      void **bulk_hdl),
			   int (*bulk_free)(void *bulk_hdl))
{
	bulk_create_fn = bulk_create;
	bulk_free_fn = bulk_free;
}

static void
grp_sop_swap(void *array, int a, int b)
{
	struct bio_bulk_group	**bbgs = (struct bio_bulk_group **)array;
	struct bio_bulk_group	 *tmp;

	tmp = bbgs[a];
	bbgs[a] = bbgs[b];
	bbgs[b] = tmp;
}

static int
grp_sop_cmp(void *array, int a, int b)
{
	struct bio_bulk_group	**bbgs = (struct bio_bulk_group **)array;

	if (bbgs[a]->bbg_bulk_pgs > bbgs[b]->bbg_bulk_pgs)
		return 1;
	if (bbgs[a]->bbg_bulk_pgs < bbgs[b]->bbg_bulk_pgs)
		return -1;
	return 0;
}

static int
grp_sop_cmp_key(void *array, int i, uint64_t key)
{
	struct bio_bulk_group	**bbgs = (struct bio_bulk_group **)array;
	unsigned int		  pg_cnt = (unsigned int)key;

	if (bbgs[i]->bbg_bulk_pgs > pg_cnt)
		return 1;
	if (bbgs[i]->bbg_bulk_pgs < pg_cnt)
		return -1;
	return 0;
}

static daos_sort_ops_t	bulk_grp_sort_ops = {
	.so_swap	= grp_sop_swap,
	.so_cmp		= grp_sop_cmp,
	.so_cmp_key	= grp_sop_cmp_key,
};

static inline bool
bulk_hdl_is_inuse(struct bio_bulk_hdl *hdl)
{
	D_ASSERT(hdl->bbh_chunk != NULL);
	D_ASSERT(hdl->bbh_bulk != NULL);

	if (hdl->bbh_inuse) {
		D_ASSERT(d_list_empty(&hdl->bbh_link));
		return true;
	}
	D_ASSERT(!d_list_empty(&hdl->bbh_link));
	return false;
}

static inline bool
bulk_chunk_is_idle(struct bio_dma_chunk *chk)
{
	D_ASSERT(chk->bdc_ref == 0);
	D_ASSERT(chk->bdc_pg_idx == 0);
	D_ASSERT(chk->bdc_bulks != NULL);
	D_ASSERT(chk->bdc_bulk_cnt >= chk->bdc_bulk_idle);

	return chk->bdc_bulk_cnt == chk->bdc_bulk_idle;
}

static inline bool
bulk_grp_is_idle(struct bio_bulk_group *bbg)
{
	struct bio_dma_chunk	*chk;

	d_list_for_each_entry(chk, &bbg->bbg_dma_chks, bdc_link) {
		if (!bulk_chunk_is_idle(chk))
			return false;
	}
	return true;
}

static void
bulk_chunk_depopulate(struct bio_dma_chunk *chk, bool fini)
{
	struct bio_bulk_hdl	*hdl;
	int			 i, rc;

	D_ASSERT(bulk_chunk_is_idle(chk));

	for (i = 0; i < chk->bdc_bulk_cnt; i++) {
		hdl = &chk->bdc_bulks[i];

		D_ASSERT(!bulk_hdl_is_inuse(hdl));
		d_list_del_init(&hdl->bbh_link);
		rc = bulk_free_fn(hdl->bbh_bulk);
		if (rc)
			D_ERROR("Failed to free bulk hdl %p "DF_RC"\n",
				hdl->bbh_bulk, DP_RC(rc));
		hdl->bbh_bulk = NULL;
	}
	chk->bdc_bulk_cnt = chk->bdc_bulk_idle = 0;
	chk->bdc_bulk_grp = NULL;

	if (fini) {
		D_FREE(chk->bdc_bulks);
		chk->bdc_bulks = NULL;
	}
}

static inline void
bulk_grp_evict_one(struct bio_dma_buffer *bdb, struct bio_dma_chunk *chk,
		   bool fini)
{
	struct bio_bulk_group	*bbg = chk->bdc_bulk_grp;

	D_ASSERT(bbg != NULL);
	D_ASSERT(bbg->bbg_chk_cnt > 0);

	bulk_chunk_depopulate(chk, fini);
	bbg->bbg_chk_cnt--;
	d_list_move_tail(&chk->bdc_link, &bdb->bdb_idle_list);
}

static inline void
bulk_grp_reset(struct bio_bulk_group *bbg, unsigned int pg_cnt)
{
	D_ASSERT(d_list_empty(&bbg->bbg_lru_link));
	D_ASSERT(d_list_empty(&bbg->bbg_dma_chks));
	D_ASSERT(d_list_empty(&bbg->bbg_idle_bulks));
	D_ASSERT(bbg->bbg_chk_cnt == 0);

	bbg->bbg_bulk_pgs = pg_cnt;
}

static void
bulk_grp_evict(struct bio_dma_buffer *bdb, struct bio_bulk_group *bbg,
	       bool fini)
{
	struct bio_dma_chunk	*chk, *tmp;

	D_ASSERT(d_list_empty(&bbg->bbg_lru_link));

	d_list_for_each_entry_safe(chk, tmp, &bbg->bbg_dma_chks, bdc_link)
		bulk_grp_evict_one(bdb, chk, fini);

	D_ASSERT(d_list_empty(&bbg->bbg_idle_bulks));
}

static struct bio_bulk_group *
bulk_grp_add(struct bio_dma_buffer *bdb, unsigned int pgs)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg;
	int			 grp_idx, rc;

	/* If there is empty bulk group slot, add new bulk group */
	if (bbc->bbc_grp_cnt < bbc->bbc_grp_max) {
		grp_idx = bbc->bbc_grp_cnt;

		bbg = &bbc->bbc_grps[grp_idx];
		bbc->bbc_sorted[grp_idx] = bbg;

		bbc->bbc_grp_cnt++;
		if (bdb->bdb_stats.bds_bulk_grps)
			d_tm_set_gauge(bdb->bdb_stats.bds_bulk_grps, bbc->bbc_grp_cnt);
		goto done;
	}

	D_ASSERT(bbc->bbc_grp_cnt == bbc->bbc_grp_max);
	/* Try to evict an idle unused group */
	D_ASSERT(!d_list_empty(&bbc->bbc_grp_lru));
	d_list_for_each_entry(bbg, &bbc->bbc_grp_lru, bbg_lru_link) {
		if (bulk_grp_is_idle(bbg)) {
			/* Replace victim with new bulk group */
			d_list_del_init(&bbg->bbg_lru_link);
			bulk_grp_evict(bdb, bbg, false);
			goto done;
		}
	}

	/* Group array is full, and all groups are inuse */
	return NULL;
done:
	bulk_grp_reset(bbg, pgs);
	rc = daos_array_sort(bbc->bbc_sorted, bbc->bbc_grp_cnt, true,
			     &bulk_grp_sort_ops);
	D_ASSERT(rc == 0);

	return bbg;
}

static struct bio_bulk_group *
bulk_grp_get(struct bio_dma_buffer *bdb, unsigned int pgs)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg = NULL;
	int			 grp_idx;

	if (d_list_empty(&bbc->bbc_grp_lru))
		goto add_grp;

	D_ASSERT(bbc->bbc_grp_cnt > 0);
	/* Quick check on the last used bulk group */
	bbg = d_list_entry(bbc->bbc_grp_lru.prev, struct bio_bulk_group,
			   bbg_lru_link);
	if (bbg->bbg_bulk_pgs == pgs)
		return bbg;

	/* Find bulk group with bulk size >= requested size */
	grp_idx = daos_array_find_ge(bbc->bbc_sorted, bbc->bbc_grp_cnt, pgs,
				     &bulk_grp_sort_ops);
	if (grp_idx >= 0) {
		bbg = bbc->bbc_sorted[grp_idx];
		/* The group has exact matched bulk size */
		if (bbg->bbg_bulk_pgs == pgs)
			goto done;
	}
add_grp:
	/* Add new group with specified bulk size */
	bbg = bulk_grp_add(bdb, pgs);
done:
	if (bbg) {
		D_ASSERT(bbg->bbg_bulk_pgs >= pgs);
		d_list_del_init(&bbg->bbg_lru_link);
		d_list_add_tail(&bbg->bbg_lru_link, &bbc->bbc_grp_lru);
	}

	return bbg;
}

int
bulk_reclaim_chunk(struct bio_dma_buffer *bdb, struct bio_bulk_group *ex_grp)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg;
	struct bio_dma_chunk	*chk;

	d_list_for_each_entry(bbg, &bbc->bbc_grp_lru, bbg_lru_link) {
		if (ex_grp != NULL && ex_grp == bbg)
			continue;

		d_list_for_each_entry(chk, &bbg->bbg_dma_chks, bdc_link) {
			if (bulk_chunk_is_idle(chk)) {
				D_DEBUG(DB_IO, "Reclaim a bulk chunk (%u)\n",
					bbg->bbg_bulk_pgs);
				bulk_grp_evict_one(bdb, chk, false);
				return 0;
			}
		}
	}
	/* All bulk chunks are inuse */
	return -DER_AGAIN;
}

static int
bulk_create_hdl(struct bio_dma_chunk *chk, struct bio_bulk_args *arg)
{
	struct bio_bulk_group	*bbg = chk->bdc_bulk_grp;
	d_sg_list_t		 sgl;
	struct bio_bulk_hdl	*bbh;
	unsigned int		 bulk_idx, pgs;
	int			 rc;

	D_ASSERT(chk->bdc_bulk_cnt == chk->bdc_bulk_idle);
	bulk_idx = chk->bdc_bulk_cnt;
	D_ASSERT(bulk_idx < bio_chk_sz);

	bbh = &chk->bdc_bulks[bulk_idx];
	D_ASSERT(bbh->bbh_chunk == chk);
	D_ASSERT(bbh->bbh_bulk == NULL);
	D_ASSERT(d_list_empty(&bbh->bbh_link));

	D_ASSERT(bbg != NULL);
	pgs = bbg->bbg_bulk_pgs;
	bbh->bbh_pg_idx = (bulk_idx * pgs);
	D_ASSERT(bbh->bbh_pg_idx < bio_chk_sz);

	rc = d_sgl_init(&sgl, 1);
	if (rc)
		return rc;

	sgl.sg_nr_out = sgl.sg_nr;
	sgl.sg_iovs[0].iov_buf = chk->bdc_ptr +
				(bbh->bbh_pg_idx << BIO_DMA_PAGE_SHIFT);
	sgl.sg_iovs[0].iov_buf_len = (pgs << BIO_DMA_PAGE_SHIFT);
	sgl.sg_iovs[0].iov_len = (pgs << BIO_DMA_PAGE_SHIFT);

	rc = bulk_create_fn(arg->ba_bulk_ctxt, &sgl, arg->ba_bulk_perm,
			    &bbh->bbh_bulk);
	if (rc) {
		D_ERROR("Create bulk handle failed. "DF_RC"\n", DP_RC(rc));
		bbh->bbh_bulk = NULL;
	} else {
		D_ASSERT(bbh->bbh_bulk != NULL);
		chk->bdc_bulk_cnt++;
		chk->bdc_bulk_idle++;
		d_list_add_tail(&bbh->bbh_link, &bbg->bbg_idle_bulks);
	}

	d_sgl_fini(&sgl, false);
	return rc;
}

static int
bulk_chunk_populate(struct bio_dma_chunk *chk, struct bio_bulk_group *bbg,
		    struct bio_bulk_args *arg)
{
	struct bio_bulk_hdl	*hdl;
	int			 i, tot_bulks, rc;

	if (chk->bdc_bulks == NULL) {
		D_ALLOC_ARRAY(chk->bdc_bulks, bio_chk_sz);
		if (chk->bdc_bulks == NULL)
			return -DER_NOMEM;

		for (i = 0; i < bio_chk_sz; i++) {
			hdl = &chk->bdc_bulks[i];

			D_INIT_LIST_HEAD(&hdl->bbh_link);
			hdl->bbh_chunk = chk;
		}
	}

	D_ASSERT(bulk_chunk_is_idle(chk));
	D_ASSERT(chk->bdc_bulk_cnt == 0);

	chk->bdc_bulk_grp = bbg;
	chk->bdc_type = BIO_CHK_TYPE_IO;

	D_ASSERT(bbg->bbg_bulk_pgs <= bio_chk_sz);
	tot_bulks = bio_chk_sz / bbg->bbg_bulk_pgs;

	for (i = 0; i < tot_bulks; i++) {
		rc = bulk_create_hdl(chk, arg);
		if (rc)
			goto error;
	}
	return 0;
error:
	bulk_chunk_depopulate(chk, true);
	return rc;
}

static int
bulk_grp_grow(struct bio_dma_buffer *bdb, struct bio_bulk_group *bbg,
	      struct bio_bulk_args *arg)
{
	struct bio_dma_chunk	*chk;
	int			 rc;

	/* Try grab an idle chunk first */
	if (!d_list_empty(&bdb->bdb_idle_list))
		goto populate;

	/* Grow DMA buffer when not reaching DMA upper bound */
	if (bdb->bdb_tot_cnt < bio_chk_cnt_max) {
		rc = dma_buffer_grow(bdb, 1);
		if (rc == 0)
			goto populate;
	}

	/* Try to evict an unused chunk from other bulk group */
	rc = bulk_reclaim_chunk(bdb, bbg);
	if (rc)
		return rc;

populate:
	D_ASSERT(!d_list_empty(&bdb->bdb_idle_list));

	chk = d_list_entry(bdb->bdb_idle_list.next,
			   struct bio_dma_chunk, bdc_link);
	rc = bulk_chunk_populate(chk, bbg, arg);
	if (rc)
		return rc;

	d_list_move_tail(&chk->bdc_link, &bbg->bbg_dma_chks);
	bbg->bbg_chk_cnt++;

	return 0;
}

static void
bulk_hdl_unhold(struct bio_bulk_hdl *hdl)
{
	struct bio_dma_chunk	*chk = hdl->bbh_chunk;
	struct bio_bulk_group	*bbg;

	D_ASSERT(bulk_hdl_is_inuse(hdl));

	hdl->bbh_inuse--;
	if (hdl->bbh_inuse == 0) {
		hdl->bbh_bulk_off = 0;
		hdl->bbh_used_bytes = 0;
		hdl->bbh_shareable = 0;
		hdl->bbh_remote_idx = 0;

		D_ASSERT(chk != NULL);
		D_ASSERT(chk->bdc_bulk_idle < chk->bdc_bulk_cnt);
		chk->bdc_bulk_idle++;

		bbg = chk->bdc_bulk_grp;
		D_ASSERT(bbg != NULL);
		d_list_add_tail(&hdl->bbh_link, &bbg->bbg_idle_bulks);
	}
}

static inline bool
is_exclusive_biov(struct bio_iov *biov)
{
	/* NVMe IOV or IOV with extra data for csum can't share bulk handle with others */
	return (bio_iov2media(biov) != DAOS_MEDIA_SCM) ||
		(bio_iov2raw_len(biov) != bio_iov2req_len(biov));
}

static void
bulk_hdl_hold(struct bio_bulk_hdl *hdl, unsigned int pg_off, unsigned int remote_idx,
	      struct bio_iov *biov)
{
	struct bio_dma_chunk	*chk = hdl->bbh_chunk;

	D_ASSERT(!bulk_hdl_is_inuse(hdl));

	d_list_del_init(&hdl->bbh_link);
	hdl->bbh_inuse = 1;
	/* biov->bi_prefix_len is for csum, not included in bulk transfer */
	hdl->bbh_bulk_off = pg_off + biov->bi_prefix_len;
	hdl->bbh_remote_idx = remote_idx;
	hdl->bbh_shareable = !is_exclusive_biov(biov);

	D_ASSERT(chk != NULL);
	D_ASSERT(chk->bdc_bulk_idle > 0);
	chk->bdc_bulk_idle--;
}

static inline unsigned int
bulk_hdl2len(struct bio_bulk_hdl *hdl)
{
	struct bio_dma_chunk	*chk = hdl->bbh_chunk;
	struct bio_bulk_group	*bbg;

	D_ASSERT(chk != NULL);
	bbg = chk->bdc_bulk_grp;
	D_ASSERT(bbg != NULL);

	return bbg->bbg_bulk_pgs << BIO_DMA_PAGE_SHIFT;
}

static struct bio_bulk_hdl *
bulk_get_shared_hdl(struct bio_desc *biod, struct bio_iov *biov, unsigned int remote_idx)
{
	struct bio_bulk_hdl	*prev_hdl;

	if (is_exclusive_biov(biov))
		return NULL;

	D_ASSERT(biod->bd_bulk_hdls != NULL);
	if (biod->bd_bulk_cnt == 0)
		return NULL;

	prev_hdl = biod->bd_bulk_hdls[biod->bd_bulk_cnt - 1];
	if (prev_hdl == NULL || !prev_hdl->bbh_shareable ||
	    (prev_hdl->bbh_remote_idx != remote_idx))
		return NULL;

	D_ASSERT(bulk_hdl_is_inuse(prev_hdl));
	D_ASSERT(prev_hdl->bbh_bulk_off == 0);
	D_ASSERT(prev_hdl->bbh_used_bytes > 0);
	D_ASSERT(prev_hdl->bbh_used_bytes <= bulk_hdl2len(prev_hdl));

	if (prev_hdl->bbh_used_bytes + bio_iov2len(biov) > bulk_hdl2len(prev_hdl))
		return NULL;

	prev_hdl->bbh_inuse++;
	return prev_hdl;
}

static struct bio_bulk_hdl *
bulk_get_hdl(struct bio_desc *biod, struct bio_iov *biov, unsigned int pg_cnt,
	     unsigned int pg_off, struct bio_bulk_args *arg)
{
	struct bio_dma_buffer	*bdb = iod_dma_buf(biod);
	struct bio_bulk_group	*bbg;
	struct bio_bulk_hdl	*hdl;
	int			 rc;

	hdl = bulk_get_shared_hdl(biod, biov, arg->ba_sgl_idx);
	if (hdl != NULL) {
		D_DEBUG(DB_IO, "Reuse shared bulk handle %p\n", hdl);
		return hdl;
	}

	bbg = bulk_grp_get(bdb, pg_cnt);
	if (bbg == NULL) {
		biod->bd_retry = 1;
		return NULL;
	}

	if (!d_list_empty(&bbg->bbg_idle_bulks))
		goto done;

	rc = bulk_grp_grow(bdb, bbg, arg);
	if (rc) {
		if (rc == -DER_AGAIN)
			biod->bd_retry = 1;
		else
			D_ERROR("Failed to grow bulk grp (%u pages) "DF_RC"\n",
				pg_cnt, DP_RC(rc));

		return NULL;
	}
done:
	D_ASSERT(!d_list_empty(&bbg->bbg_idle_bulks));
	hdl = d_list_entry(bbg->bbg_idle_bulks.next, struct bio_bulk_hdl,
			   bbh_link);

	bulk_hdl_hold(hdl, pg_off, arg->ba_sgl_idx, biov);
	return hdl;
}

static inline bool
bypass_bulk_cache(struct bio_desc *biod, struct bio_iov *biov,
		  unsigned int pg_cnt)
{
	/* Hole, no RDMA */
	if (bio_addr_is_hole(&biov->bi_addr))
		return true;
	/* Huge IOV, allocate DMA buffer & create bulk handle on-the-fly */
	if (pg_cnt > bio_chk_sz)
		return true;
	/* Get buffer operation */
	if (biod->bd_type == BIO_IOD_TYPE_GETBUF)
		return false;
	/* Direct SCM RDMA or deduped SCM extent */
	if (bio_iov2media(biov) == DAOS_MEDIA_SCM) {
		if (bio_scm_rdma || BIO_ADDR_IS_DEDUP(&biov->bi_addr))
			return true;
	}

	return false;
}

static int
bulk_iod_init(struct bio_desc *biod)
{
	int	i, max_bulks = 0;

	D_ASSERT(biod->bd_bulk_hdls == NULL);
	for (i = 0; i < biod->bd_sgl_cnt; i++) {
		struct bio_sglist *bsgl = &biod->bd_sgls[i];

		max_bulks += bsgl->bs_nr_out;
	}

	D_ALLOC_ARRAY(biod->bd_bulk_hdls, max_bulks);
	if (biod->bd_bulk_hdls == NULL) {
		D_ERROR("Failed to allocate bulk handle array\n");
		return -DER_NOMEM;
	}
	biod->bd_bulk_max = max_bulks;
	biod->bd_bulk_cnt = 0;

	return 0;
}

static inline void *
bulk_hdl2addr(struct bio_bulk_hdl *hdl, unsigned int pg_off)
{
	struct bio_dma_chunk	*chk = hdl->bbh_chunk;
	unsigned int		 chk_pg_idx = hdl->bbh_pg_idx;
	void			*payload;

	D_ASSERT(bulk_hdl_is_inuse(hdl));

	payload = chk->bdc_ptr + (chk_pg_idx << BIO_DMA_PAGE_SHIFT);
	if (hdl->bbh_shareable) {
		D_ASSERT(hdl->bbh_bulk_off == 0);
		D_ASSERT(pg_off == 0);
		payload += hdl->bbh_used_bytes;
	} else {
		D_ASSERT(hdl->bbh_used_bytes == 0);
		payload += pg_off;
	}

	return payload;
}

/* Try to round up the bulk size to fully utilize the chunk */
static inline unsigned int
roundup_pgs(unsigned int pgs)
{
	D_ASSERT(bio_chk_sz % 2 == 0);
	D_ASSERT(bio_chk_sz >= pgs);
	return bio_chk_sz / (bio_chk_sz / pgs);
}

int
bulk_map_one(struct bio_desc *biod, struct bio_iov *biov, void *data)
{
	struct bio_bulk_args	*arg = data;
	struct bio_bulk_hdl	*hdl = NULL;
	uint64_t		 off, end;
	unsigned int		 pg_cnt, pg_off;
	int			 rc = 0;

	D_ASSERT(bulk_create_fn != NULL && bulk_free_fn != NULL);
	D_ASSERT(arg != NULL && arg->ba_bulk_ctxt != NULL);

	D_ASSERT(biod && biod->bd_chk_type == BIO_CHK_TYPE_IO);
	D_ASSERT(biod->bd_rdma);
	D_ASSERT(biov);

	if (biod->bd_bulk_hdls == NULL) {
		rc = bulk_iod_init(biod);
		if (rc)
			return rc;
	}

	/* Zero length IOV */
	if (bio_iov2req_len(biov) == 0) {
		D_ASSERT(bio_iov2raw_len(biov) == 0);
		bio_iov_set_raw_buf(biov, NULL);
		goto done;
	}

	dma_biov2pg(biov, &off, &end, &pg_cnt, &pg_off);

	if (bypass_bulk_cache(biod, biov, pg_cnt)) {
		rc = dma_map_one(biod, biov, NULL);
		goto done;
	}
	D_ASSERT(!BIO_ADDR_IS_DEDUP(&biov->bi_addr));

	hdl = bulk_get_hdl(biod, biov, roundup_pgs(pg_cnt), pg_off, arg);
	if (hdl == NULL) {
		if (biod->bd_retry)
			return -DER_AGAIN;

		D_ERROR("Failed to grab cached bulk (%d pages)\n", pg_cnt);
		return -DER_NOMEM;
	}

	bio_iov_set_raw_buf(biov, bulk_hdl2addr(hdl, pg_off));
	rc = iod_add_region(biod, hdl->bbh_chunk, hdl->bbh_pg_idx, hdl->bbh_used_bytes,
			    off, end, bio_iov2media(biov));
	if (rc) {
		bulk_hdl_unhold(hdl);
		return rc;
	}

	/* Update the used bytes for shared handle */
	if (hdl->bbh_shareable) {
		D_ASSERT(hdl->bbh_bulk_off == 0);
		hdl->bbh_used_bytes += bio_iov2len(biov);
	}
done:
	D_ASSERT(biod->bd_bulk_hdls != NULL);
	D_ASSERT(biod->bd_bulk_cnt < biod->bd_bulk_max);

	biod->bd_bulk_hdls[biod->bd_bulk_cnt] = hdl;
	biod->bd_bulk_cnt++;

	return rc;
}

void
bulk_iod_release(struct bio_desc *biod)
{
	struct bio_bulk_hdl	*hdl;
	int			 i;

	if (biod->bd_bulk_hdls == NULL) {
		D_ASSERT(biod->bd_bulk_cnt == 0);
		return;
	}

	D_ASSERT(biod->bd_chk_type == BIO_CHK_TYPE_IO);
	for (i = 0; i < biod->bd_bulk_cnt; i++) {
		hdl = biod->bd_bulk_hdls[i];

		/* Bypassed bulk cache */
		if (hdl == NULL)
			continue;

		bulk_hdl_unhold(hdl);
		biod->bd_bulk_hdls[i] = NULL;
	}

	biod->bd_bulk_cnt = 0;
}

void
bulk_cache_destroy(struct bio_dma_buffer *bdb)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg, *tmp;
	int			 i;

	if (bbc->bbc_grps == NULL) {
		D_ASSERT(d_list_empty(&bbc->bbc_grp_lru));
		return;
	}

	D_ASSERT(bbc->bbc_grp_cnt <= bbc->bbc_grp_max);

	d_list_for_each_entry_safe(bbg, tmp, &bbc->bbc_grp_lru, bbg_lru_link) {
		d_list_del_init(&bbg->bbg_lru_link);
		bulk_grp_evict(bdb, bbg, true);
	}

	for (i = 0; i < bbc->bbc_grp_max; i++) {
		bbg = &bbc->bbc_grps[i];
		bulk_grp_reset(bbg, 0);
	}

	D_FREE(bbc->bbc_grps);
	bbc->bbc_grps = NULL;
	bbc->bbc_grp_max = bbc->bbc_grp_cnt = 0;

	D_FREE(bbc->bbc_sorted);
	bbc->bbc_sorted = NULL;
}

#define BIO_BULK_GRPS_MAX	64
int
bulk_cache_create(struct bio_dma_buffer *bdb)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg;
	int			 i;

	D_ASSERT(bbc->bbc_grps == NULL);
	D_INIT_LIST_HEAD(&bbc->bbc_grp_lru);

	D_ALLOC_ARRAY(bbc->bbc_grps, BIO_BULK_GRPS_MAX);
	if (bbc->bbc_grps == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(bbc->bbc_sorted, BIO_BULK_GRPS_MAX);
	if (bbc->bbc_sorted == NULL) {
		D_FREE(bbc->bbc_grps);
		bbc->bbc_grps = NULL;
		return -DER_NOMEM;
	}

	bbc->bbc_grp_max = BIO_BULK_GRPS_MAX;
	bbc->bbc_grp_cnt = 0;

	for (i = 0; i < bbc->bbc_grp_max; i++) {
		bbg = &bbc->bbc_grps[i];

		D_INIT_LIST_HEAD(&bbg->bbg_lru_link);
		D_INIT_LIST_HEAD(&bbg->bbg_dma_chks);
		D_INIT_LIST_HEAD(&bbg->bbg_idle_bulks);
		bbg->bbg_bulk_pgs = 0;
		bbg->bbg_chk_cnt = 0;
	}
	return 0;
}

void *
bio_iod_bulk(struct bio_desc *biod, int sgl_idx, int iov_idx,
	     unsigned int *bulk_off)
{
	struct bio_bulk_hdl	*hdl;
	struct bio_sglist	*bsgl = NULL;
	int			 i, bulk_idx = 0;

	/* Pass in NULL 'biod' is allowed */
	if (biod == NULL)
		return NULL;

	/* Bulk cache bypassed */
	if (biod->bd_bulk_hdls == NULL)
		return NULL;

	D_ASSERTF(biod->bd_bulk_cnt == biod->bd_bulk_max, "bulk_cnt:%u, bulk_max:%u\n",
		  biod->bd_bulk_cnt, biod->bd_bulk_max);
	D_ASSERT(sgl_idx < biod->bd_sgl_cnt);

	for (i = 0; i < sgl_idx; i++) {
		bsgl = &biod->bd_sgls[i];

		bulk_idx += bsgl->bs_nr_out;
	}

	bsgl = &biod->bd_sgls[sgl_idx];
	D_ASSERT(iov_idx < bsgl->bs_nr_out);

	bulk_idx += iov_idx;
	D_ASSERT(bulk_idx < biod->bd_bulk_cnt);

	hdl = biod->bd_bulk_hdls[bulk_idx];
	if (hdl == NULL)
		return NULL;

	D_ASSERT(bulk_hdl_is_inuse(hdl));
	*bulk_off = hdl->bbh_bulk_off;
	D_ASSERT(*bulk_off < bulk_hdl2len(hdl));

	return hdl->bbh_bulk;
}
