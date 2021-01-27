/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)
#include <spdk/env.h>
#include <spdk/blob.h>
#include <spdk/thread.h>
#include "bio_internal.h"

static void
dma_free_chunk(struct bio_dma_chunk *chunk)
{
	D_ASSERT(chunk->bdc_ptr != NULL);
	D_ASSERT(chunk->bdc_pg_idx == 0);
	D_ASSERT(chunk->bdc_ref == 0);
	D_ASSERT(d_list_empty(&chunk->bdc_link));

	spdk_dma_free(chunk->bdc_ptr);
	D_FREE(chunk);
}

static struct bio_dma_chunk *
dma_alloc_chunk(unsigned int cnt)
{
	struct bio_dma_chunk *chunk;
	ssize_t bytes = (ssize_t)cnt << BIO_DMA_PAGE_SHIFT;

	D_ASSERT(bytes > 0);
	D_ALLOC_PTR(chunk);
	if (chunk == NULL) {
		D_ERROR("Failed to allocate chunk\n");
		return NULL;
	}

	chunk->bdc_ptr = spdk_dma_malloc(bytes, BIO_DMA_PAGE_SZ, NULL);
	if (chunk->bdc_ptr == NULL) {
		D_ERROR("Failed to allocate %u pages DMA buffer\n", cnt);
		D_FREE(chunk);
		return NULL;
	}
	D_INIT_LIST_HEAD(&chunk->bdc_link);

	return chunk;
}


static void
dma_buffer_shrink(struct bio_dma_buffer *buf, unsigned int cnt)
{
	struct bio_dma_chunk *chunk, *tmp;

	d_list_for_each_entry_safe(chunk, tmp, &buf->bdb_idle_list, bdc_link) {
		if (cnt == 0)
			break;

		d_list_del_init(&chunk->bdc_link);
		dma_free_chunk(chunk);

		D_ASSERT(buf->bdb_tot_cnt > 0);
		buf->bdb_tot_cnt--;
		cnt--;
	}
}

static int
dma_buffer_grow(struct bio_dma_buffer *buf, unsigned int cnt)
{
	struct bio_dma_chunk *chunk;
	int i, rc = 0;

	if ((buf->bdb_tot_cnt + cnt) > bio_chk_cnt_max) {
		D_ERROR("Exceeding per-xstream DMA buffer size\n");
		return -DER_OVERFLOW;
	}

	for (i = 0; i < cnt; i++) {
		chunk = dma_alloc_chunk(bio_chk_sz);
		if (chunk == NULL) {
			rc = -DER_NOMEM;
			break;
		}

		d_list_add_tail(&chunk->bdc_link, &buf->bdb_idle_list);
		buf->bdb_tot_cnt++;
	}

	return rc;
}

void
dma_buffer_destroy(struct bio_dma_buffer *buf)
{
	D_ASSERT(d_list_empty(&buf->bdb_used_list));
	D_ASSERT(buf->bdb_active_iods == 0);
	dma_buffer_shrink(buf, buf->bdb_tot_cnt);

	D_ASSERT(buf->bdb_tot_cnt == 0);
	buf->bdb_cur_chk = NULL;
	ABT_mutex_free(&buf->bdb_mutex);
	ABT_cond_free(&buf->bdb_wait_iods);

	D_FREE(buf);
}

struct bio_dma_buffer *
dma_buffer_create(unsigned int init_cnt)
{
	struct bio_dma_buffer *buf;
	int rc;

	D_ALLOC_PTR(buf);
	if (buf == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&buf->bdb_idle_list);
	D_INIT_LIST_HEAD(&buf->bdb_used_list);
	buf->bdb_cur_chk = NULL;
	buf->bdb_tot_cnt = 0;
	buf->bdb_active_iods = 0;

	rc = ABT_mutex_create(&buf->bdb_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE(buf);
		return NULL;
	}

	rc = ABT_cond_create(&buf->bdb_wait_iods);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&buf->bdb_mutex);
		D_FREE(buf);
		return NULL;
	}

	rc = dma_buffer_grow(buf, init_cnt);
	if (rc != 0) {
		dma_buffer_destroy(buf);
		return NULL;
	}

	return buf;
}

struct bio_sglist *
bio_iod_sgl(struct bio_desc *biod, unsigned int idx)
{
	struct bio_sglist	*bsgl = NULL;

	D_ASSERTF(idx < biod->bd_sgl_cnt, "Invalid sgl index %d/%d\n",
		  idx, biod->bd_sgl_cnt);

	bsgl = &biod->bd_sgls[idx];
	D_ASSERT(bsgl != NULL);

	return bsgl;
}

struct bio_desc *
bio_iod_alloc(struct bio_io_context *ctxt, unsigned int sgl_cnt, bool update)
{
	struct bio_desc	*biod;

	D_ASSERT(ctxt != NULL && ctxt->bic_umem != NULL);
	D_ASSERT(sgl_cnt != 0);

	D_ALLOC(biod, offsetof(struct bio_desc, bd_sgls[sgl_cnt]));
	if (biod == NULL)
		return NULL;

	biod->bd_ctxt = ctxt;
	biod->bd_update = update;
	biod->bd_sgl_cnt = sgl_cnt;

	biod->bd_dma_done = ABT_EVENTUAL_NULL;
	return biod;
}

void
bio_iod_free(struct bio_desc *biod)
{
	int i;

	D_ASSERT(!biod->bd_buffer_prep);

	if (biod->bd_dma_done != ABT_EVENTUAL_NULL)
		ABT_eventual_free(&biod->bd_dma_done);

	for (i = 0; i < biod->bd_sgl_cnt; i++)
		bio_sgl_fini(&biod->bd_sgls[i]);
	D_FREE(biod);
}

static inline struct bio_dma_buffer *
iod_dma_buf(struct bio_desc *biod)
{
	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt->bxc_dma_buf);

	return biod->bd_ctxt->bic_xs_ctxt->bxc_dma_buf;
}

static inline bool
dma_chunk_is_huge(struct bio_dma_chunk *chunk)
{
	return d_list_empty(&chunk->bdc_link);
}

/*
 * Release all the DMA chunks held by @biod, once the use count of any
 * chunk drops to zero, put it back to free list.
 */
static void
iod_release_buffer(struct bio_desc *biod)
{
	struct bio_dma_buffer *bdb;
	struct bio_rsrvd_dma *rsrvd_dma = &biod->bd_rsrvd;
	int i;

	if (rsrvd_dma->brd_chk_max == 0) {
		D_ASSERT(rsrvd_dma->brd_rg_max == 0);
		biod->bd_buffer_prep = 0;
		return;
	}

	D_ASSERT(rsrvd_dma->brd_regions != NULL);
	D_FREE(rsrvd_dma->brd_regions);
	rsrvd_dma->brd_regions = NULL;
	rsrvd_dma->brd_rg_max = rsrvd_dma->brd_rg_cnt = 0;

	bdb = iod_dma_buf(biod);
	D_ASSERT(rsrvd_dma->brd_dma_chks != NULL);
	for (i = 0; i < rsrvd_dma->brd_chk_cnt; i++) {
		struct bio_dma_chunk *chunk = rsrvd_dma->brd_dma_chks[i];

		D_ASSERT(chunk != NULL);
		D_ASSERT(chunk->bdc_ref > 0);
		chunk->bdc_ref--;

		D_DEBUG(DB_IO, "Release chunk:%p[%p] idx:%u ref:%u huge:%d\n",
			chunk, chunk->bdc_ptr, chunk->bdc_pg_idx,
			chunk->bdc_ref, dma_chunk_is_huge(chunk));

		if (dma_chunk_is_huge(chunk)) {
			dma_free_chunk(chunk);
		} else if (chunk->bdc_ref == 0) {
			chunk->bdc_pg_idx = 0;
			if (chunk == bdb->bdb_cur_chk)
				bdb->bdb_cur_chk = NULL;
			d_list_move_tail(&chunk->bdc_link, &bdb->bdb_idle_list);
		}
		rsrvd_dma->brd_dma_chks[i] = NULL;
	}

	D_FREE(rsrvd_dma->brd_dma_chks);
	rsrvd_dma->brd_dma_chks = NULL;
	rsrvd_dma->brd_chk_max = rsrvd_dma->brd_chk_cnt = 0;

	biod->bd_buffer_prep = 0;
}

struct bio_copy_args {
	/* DRAM sg lists to be copied to/from */
	d_sg_list_t	*ca_sgls;
	int		 ca_sgl_cnt;
	/* Current sgl index */
	int		 ca_sgl_idx;
	/* Current IOV index inside of current sgl */
	int		 ca_iov_idx;
	/* Current offset inside of current IOV */
	ssize_t		 ca_iov_off;
};

static int
iterate_biov(struct bio_desc *biod,
	     int (*cb_fn)(struct bio_desc *, struct bio_iov *,
			  struct bio_copy_args *),
	     struct bio_copy_args *arg)
{
	int i, j, rc = 0;

	for (i = 0; i < biod->bd_sgl_cnt; i++) {
		struct bio_sglist *bsgl = &biod->bd_sgls[i];

		if (arg != NULL) {
			D_ASSERT(i < arg->ca_sgl_cnt);
			arg->ca_sgl_idx = i;
			arg->ca_iov_idx = 0;
			arg->ca_iov_off = 0;
			if (!biod->bd_update)
				arg->ca_sgls[i].sg_nr_out = 0;
		}

		if (bsgl->bs_nr_out == 0)
			continue;

		for (j = 0; j < bsgl->bs_nr_out; j++) {
			struct bio_iov *biov = &bsgl->bs_iovs[j];

			if (bio_iov2req_len(biov) == 0)
				continue;

			rc = cb_fn(biod, biov, arg);
			if (rc)
				break;
		}
		if (rc)
			break;
	}

	return rc;
}

static void *
chunk_reserve(struct bio_dma_chunk *chk, unsigned int chk_pg_idx,
	      unsigned int pg_cnt, unsigned int pg_off)
{
	D_ASSERT(chk != NULL);

	/* Huge chunk is dedicated for single huge IOV */
	if (dma_chunk_is_huge(chk))
		return NULL;

	D_ASSERTF(chk->bdc_pg_idx <= bio_chk_sz, "%u > %u\n",
		  chk->bdc_pg_idx, bio_chk_sz);

	D_ASSERTF(chk_pg_idx == chk->bdc_pg_idx ||
		  (chk_pg_idx + 1) == chk->bdc_pg_idx, "%u, %u\n",
		  chk_pg_idx, chk->bdc_pg_idx);

	/* The chunk doesn't have enough unused pages */
	if (chk_pg_idx + pg_cnt > bio_chk_sz)
		return NULL;

	D_DEBUG(DB_TRACE, "Reserved on chunk:%p[%p], idx:%u, cnt:%u, off:%u\n",
		chk, chk->bdc_ptr, chk_pg_idx, pg_cnt, pg_off);

	chk->bdc_pg_idx = chk_pg_idx + pg_cnt;
	return chk->bdc_ptr + (chk_pg_idx << BIO_DMA_PAGE_SHIFT) + pg_off;
}

static inline struct bio_rsrvd_region *
iod_last_region(struct bio_desc *biod)
{
	unsigned int cnt = biod->bd_rsrvd.brd_rg_cnt;

	D_ASSERT(!cnt || cnt <= biod->bd_rsrvd.brd_rg_max);
	return (cnt != 0) ? &biod->bd_rsrvd.brd_regions[cnt - 1] : NULL;
}

static struct bio_dma_chunk *
chunk_get_idle(struct bio_dma_buffer *bdb, struct bio_desc *biod)
{
	struct bio_dma_chunk *chk;
	int rc;

	if (d_list_empty(&bdb->bdb_idle_list)) {
		if (bdb->bdb_tot_cnt == bio_chk_cnt_max) {
			D_CRIT("Maximum per-xstream DMA buffer isn't big "
			       "enough (chk_sz:%u chk_cnt:%u iods:%u) to "
			       "sustain the workload.\n", bio_chk_sz,
			       bio_chk_cnt_max, bdb->bdb_active_iods);

			biod->bd_retry = 1;
			return NULL;
		}

		rc = dma_buffer_grow(bdb, 1);
		if (rc != 0)
			return NULL;
	}

	D_ASSERT(!d_list_empty(&bdb->bdb_idle_list));
	chk = d_list_entry(bdb->bdb_idle_list.next, struct bio_dma_chunk,
			   bdc_link);
	d_list_move_tail(&chk->bdc_link, &bdb->bdb_used_list);

	return chk;
}

static int
iod_add_chunk(struct bio_desc *biod, struct bio_dma_chunk *chk)
{
	struct bio_rsrvd_dma *rsrvd_dma = &biod->bd_rsrvd;
	unsigned int max, cnt;

	max = rsrvd_dma->brd_chk_max;
	cnt = rsrvd_dma->brd_chk_cnt;

	if (cnt == max) {
		struct bio_dma_chunk **chunks;
		int size = sizeof(struct bio_dma_chunk *);
		unsigned new_cnt = cnt + 10;

		D_ALLOC_ARRAY(chunks, new_cnt);
		if (chunks == NULL)
			return -DER_NOMEM;

		if (max != 0) {
			memcpy(chunks, rsrvd_dma->brd_dma_chks, max * size);
			D_FREE(rsrvd_dma->brd_dma_chks);
		}

		rsrvd_dma->brd_dma_chks = chunks;
		rsrvd_dma->brd_chk_max = new_cnt;
	}

	chk->bdc_ref++;
	rsrvd_dma->brd_dma_chks[cnt] = chk;
	rsrvd_dma->brd_chk_cnt++;
	return 0;
}

static int
iod_add_region(struct bio_desc *biod, struct bio_dma_chunk *chk,
	       unsigned int chk_pg_idx, uint64_t off, uint64_t end)
{
	struct bio_rsrvd_dma *rsrvd_dma = &biod->bd_rsrvd;
	unsigned int max, cnt;

	max = rsrvd_dma->brd_rg_max;
	cnt = rsrvd_dma->brd_rg_cnt;

	if (cnt == max) {
		struct bio_rsrvd_region *rgs;
		int size = sizeof(struct bio_rsrvd_region);
		unsigned new_cnt = cnt + 20;

		D_ALLOC_ARRAY(rgs, new_cnt);
		if (rgs == NULL)
			return -DER_NOMEM;

		if (max != 0) {
			memcpy(rgs, rsrvd_dma->brd_regions, max * size);
			D_FREE(rsrvd_dma->brd_regions);
		}

		rsrvd_dma->brd_regions = rgs;
		rsrvd_dma->brd_rg_max = new_cnt;
	}

	rsrvd_dma->brd_regions[cnt].brr_chk = chk;
	rsrvd_dma->brd_regions[cnt].brr_pg_idx = chk_pg_idx;
	rsrvd_dma->brd_regions[cnt].brr_off = off;
	rsrvd_dma->brd_regions[cnt].brr_end = end;
	rsrvd_dma->brd_rg_cnt++;
	return 0;
}

/* Convert offset of @biov into memory pointer */
static int
dma_map_one(struct bio_desc *biod, struct bio_iov *biov,
	    struct bio_copy_args *arg)
{
	struct bio_rsrvd_region *last_rg;
	struct bio_dma_buffer *bdb;
	struct bio_dma_chunk *chk = NULL;
	uint64_t off, end;
	unsigned int pg_cnt, pg_off, chk_pg_idx;
	int rc;

	D_ASSERT(arg == NULL);
	D_ASSERT(biov && bio_iov2raw_len(biov) != 0);

	if (bio_addr_is_hole(&biov->bi_addr)) {
		bio_iov_set_raw_buf(biov, NULL);
		return 0;
	}

	if (biov->bi_addr.ba_type == DAOS_MEDIA_SCM) {
		struct umem_instance *umem = biod->bd_ctxt->bic_umem;
		bio_iov_set_raw_buf(biov,
				    umem_off2ptr(umem, bio_iov2raw_off(biov)));
		return 0;
	}

	D_ASSERT(biov->bi_addr.ba_type == DAOS_MEDIA_NVME);
	bdb = iod_dma_buf(biod);
	off = bio_iov2raw_off(biov);
	end = bio_iov2raw_off(biov) + bio_iov2raw_len(biov);
	pg_cnt = ((end + BIO_DMA_PAGE_SZ - 1) >> BIO_DMA_PAGE_SHIFT) -
			(off >> BIO_DMA_PAGE_SHIFT);
	pg_off = off & ((uint64_t)BIO_DMA_PAGE_SZ - 1);

	/*
	 * For huge IOV, we'll bypass our per-xstream DMA buffer cache and
	 * allocate chunk from the SPDK reserved huge pages directly, this
	 * kind of huge chunk will be freed immediately on I/O completion.
	 *
	 * We assume the contiguous huge IOV is quite rare, so there won't
	 * be high contention over the SPDK huge page cache.
	 */
	if (pg_cnt > bio_chk_sz) {
		chk = dma_alloc_chunk(pg_cnt);
		if (chk == NULL)
			return -DER_NOMEM;

		rc = iod_add_chunk(biod, chk);
		if (rc) {
			dma_free_chunk(chk);
			return rc;
		}
		bio_iov_set_raw_buf(biov, chk->bdc_ptr + pg_off);
		chk_pg_idx = 0;

		D_DEBUG(DB_IO, "Huge chunk:%p[%p], cnt:%u, off:%u\n",
			chk, chk->bdc_ptr, pg_cnt, pg_off);

		goto add_region;
	}

	last_rg = iod_last_region(biod);

	/* First, try consecutive reserve from the last reserved region */
	if (last_rg) {
		uint64_t cur_pg, prev_pg_start, prev_pg_end;

		D_DEBUG(DB_TRACE, "Last region %p:%d ["DF_U64","DF_U64")\n",
			last_rg->brr_chk, last_rg->brr_pg_idx,
			last_rg->brr_off, last_rg->brr_end);

		chk = last_rg->brr_chk;
		chk_pg_idx = last_rg->brr_pg_idx;
		D_ASSERT(chk_pg_idx < bio_chk_sz);

		prev_pg_start = last_rg->brr_off >> BIO_DMA_PAGE_SHIFT;
		prev_pg_end = last_rg->brr_end >> BIO_DMA_PAGE_SHIFT;
		cur_pg = off >> BIO_DMA_PAGE_SHIFT;
		D_ASSERT(prev_pg_start <= prev_pg_end);

		/* Consecutive in page */
		if (cur_pg == prev_pg_end) {
			chk_pg_idx += (prev_pg_end - prev_pg_start);
			bio_iov_set_raw_buf(biov,
				chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off));
			if (bio_iov2raw_buf(biov) != NULL) {
				D_DEBUG(DB_TRACE, "Consecutive reserve %p.\n",
					bio_iov2raw_buf(biov));
				last_rg->brr_end = end;
				return 0;
			}
		}
	}

	/* Try to reserve from the last DMA chunk in io descriptor */
	if (chk != NULL) {
		chk_pg_idx = chk->bdc_pg_idx;
		bio_iov_set_raw_buf(biov, chunk_reserve(chk, chk_pg_idx,
							pg_cnt, pg_off));
		if (bio_iov2raw_buf(biov) != NULL) {
			D_DEBUG(DB_IO, "Last chunk reserve %p.\n",
				bio_iov2raw_buf(biov));
			goto add_region;
		}
	}

	/*
	 * Try to reserve the DMA buffer from the 'current chunk' of the
	 * per-xstream DMA buffer. It could be different with the last chunk
	 * in io descriptor, because dma_map_one() may yield in the future.
	 */
	if (bdb->bdb_cur_chk != NULL && bdb->bdb_cur_chk != chk) {
		chk = bdb->bdb_cur_chk;
		chk_pg_idx = chk->bdc_pg_idx;
		bio_iov_set_raw_buf(biov, chunk_reserve(chk, chk_pg_idx,
							pg_cnt, pg_off));
		if (bio_iov2raw_buf(biov) != NULL) {
			D_DEBUG(DB_IO, "Current chunk reserve %p.\n",
				bio_iov2raw_buf(biov));
			goto add_chunk;
		}
	}

	/*
	 * Switch to another idle chunk, if there isn't any idle chunk
	 * available, grow buffer.
	 */
	chk = chunk_get_idle(bdb, biod);
	if (chk == NULL)
		return -DER_OVERFLOW;

	bdb->bdb_cur_chk = chk;
	chk_pg_idx = chk->bdc_pg_idx;

	D_ASSERT(chk_pg_idx == 0);
	bio_iov_set_raw_buf(biov,
			    chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off));
	if (bio_iov2raw_buf(biov) != NULL) {
		D_DEBUG(DB_IO, "New chunk reserve %p.\n",
			bio_iov2raw_buf(biov));
		goto add_chunk;
	}

	return -DER_OVERFLOW;

add_chunk:
	rc = iod_add_chunk(biod, chk);
	if (rc) {
		/* Revert the reservation in chunk */
		D_ASSERT(chk->bdc_pg_idx >= pg_cnt);
		chk->bdc_pg_idx -= pg_cnt;
		return rc;
	}
add_region:
	return iod_add_region(biod, chk, chk_pg_idx, off, end);
}

static void
rw_completion(void *cb_arg, int err)
{
	struct bio_xs_context	*xs_ctxt;
	struct bio_desc		*biod = cb_arg;
	struct media_error_msg	*mem = NULL;

	D_ASSERT(biod->bd_inflights > 0);
	biod->bd_inflights--;

	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
	xs_ctxt = biod->bd_ctxt->bic_xs_ctxt;
	D_ASSERT(xs_ctxt->bxc_blob_rw > 0);
	xs_ctxt->bxc_blob_rw--;

	/* Induce NVMe Read/Write Error*/
	if (biod->bd_update)
		err = DAOS_FAIL_CHECK(DAOS_NVME_WRITE_ERR) ? -EIO : err;
	else
		err = DAOS_FAIL_CHECK(DAOS_NVME_READ_ERR) ? -EIO : err;

	/* Return the error value of the first NVMe IO error */
	if (biod->bd_result == 0 && err != 0)
		biod->bd_result = daos_errno2der(-err);

	/* Report all NVMe IO errors */
	if (err != 0) {
		D_ALLOC_PTR(mem);
		if (mem == NULL)
			goto skip_media_error;
		mem->mem_err_type = biod->bd_update ? MET_WRITE : MET_READ;
		mem->mem_bs = xs_ctxt->bxc_blobstore;
		mem->mem_tgt_id = xs_ctxt->bxc_tgt_id;
		spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error,
				     mem);
	}

skip_media_error:
	if (biod->bd_inflights == 0 && biod->bd_dma_issued)
		ABT_eventual_set(biod->bd_dma_done, NULL, 0);
}

static void
dma_rw(struct bio_desc *biod, bool prep)
{
	struct spdk_io_channel	*channel;
	struct spdk_blob	*blob;
	struct bio_rsrvd_dma	*rsrvd_dma = &biod->bd_rsrvd;
	struct bio_rsrvd_region	*rg;
	struct bio_xs_context	*xs_ctxt;
	uint64_t		 pg_idx, pg_cnt, pg_end;
	void			*payload, *pg_rmw = NULL;
	bool			 rmw_read = (prep && biod->bd_update);
	unsigned int		 pg_off;
	int			 i;

	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
	xs_ctxt = biod->bd_ctxt->bic_xs_ctxt;
	blob = biod->bd_ctxt->bic_blob;
	channel = xs_ctxt->bxc_io_channel;

	biod->bd_inflights = 0;
	biod->bd_dma_issued = 0;
	biod->bd_result = 0;

	/* Bypass NVMe I/O, used by daos_perf for performance evaluation */
	if (daos_io_bypass & IOBP_NVME)
		return;

	if (!is_blob_valid(biod->bd_ctxt)) {
		D_ERROR("Blobstore is invalid. blob:%p, closing:%d\n",
			blob, biod->bd_ctxt->bic_closing);
		biod->bd_result = -DER_NO_HDL;
		return;
	}

	D_ASSERT(channel != NULL);
	biod->bd_ctxt->bic_inflight_dmas++;

	D_DEBUG(DB_IO, "DMA start, blob:%p, update:%d, rmw:%d\n",
		blob, biod->bd_update, rmw_read);

	for (i = 0; i < rsrvd_dma->brd_rg_cnt; i++) {
		rg = &rsrvd_dma->brd_regions[i];

		D_ASSERT(rg->brr_chk != NULL);
		pg_idx = rg->brr_off >> BIO_DMA_PAGE_SHIFT;
		payload = rg->brr_chk->bdc_ptr +
			(rg->brr_pg_idx << BIO_DMA_PAGE_SHIFT);

		if (!rmw_read) {
			pg_cnt = (rg->brr_end + BIO_DMA_PAGE_SZ - 1) >>
					BIO_DMA_PAGE_SHIFT;
			D_ASSERT(pg_cnt > pg_idx);
			pg_cnt -= pg_idx;

			biod->bd_inflights++;
			xs_ctxt->bxc_blob_rw++;
			/* NVMe poll needs be scheduled */
			if (bio_need_nvme_poll(xs_ctxt))
				bio_yield();

			D_DEBUG(DB_IO, "%s blob:%p payload:%p, "
				"pg_idx:"DF_U64", pg_cnt:"DF_U64"\n",
				biod->bd_update ? "Write" : "Read",
				blob, payload, pg_idx, pg_cnt);

			if (biod->bd_update)
				spdk_blob_io_write(blob, channel, payload,
					page2io_unit(biod->bd_ctxt, pg_idx),
					page2io_unit(biod->bd_ctxt, pg_cnt),
					rw_completion, biod);
			else
				spdk_blob_io_read(blob, channel, payload,
					page2io_unit(biod->bd_ctxt, pg_idx),
					page2io_unit(biod->bd_ctxt, pg_cnt),
					rw_completion, biod);
			continue;
		}

		/*
		 * Since DAOS doesn't support partial overwrite yet, we don't
		 * do RMW for partial update, only zeroing the page instead.
		 */
		pg_off = rg->brr_off & ((uint64_t)BIO_DMA_PAGE_SZ - 1);

		if (pg_off != 0 && payload != pg_rmw) {
			D_DEBUG(DB_IO, "Front partial blob:%p payload:%p, "
				"pg_idx:"DF_U64" pg_off:%d\n",
				blob, payload, pg_idx, pg_off);

			memset(payload, 0, BIO_DMA_PAGE_SZ);
			pg_rmw = payload;
		}

		pg_end = rg->brr_end >> BIO_DMA_PAGE_SHIFT;
		D_ASSERT(pg_end >= pg_idx);
		payload += (pg_end - pg_idx) << BIO_DMA_PAGE_SHIFT;
		pg_off = rg->brr_end & ((uint64_t)BIO_DMA_PAGE_SZ - 1);

		if (pg_off != 0 && payload != pg_rmw) {
			D_DEBUG(DB_IO, "Rear partial blob:%p payload:%p, "
				"pg_idx:"DF_U64" pg_off:%d\n",
				blob, payload, pg_idx, pg_off);

			memset(payload, 0, BIO_DMA_PAGE_SZ);
			pg_rmw = payload;
		}
	}

	if (xs_ctxt->bxc_tgt_id == -1) {
		D_DEBUG(DB_IO, "Self poll completion, blob:%p\n", blob);
		xs_poll_completion(xs_ctxt, &biod->bd_inflights);
	} else {
		biod->bd_dma_issued = 1;
		if (biod->bd_inflights != 0)
			ABT_eventual_wait(biod->bd_dma_done, NULL);
	}

	biod->bd_ctxt->bic_inflight_dmas--;
	D_DEBUG(DB_IO, "DMA done, blob:%p, update:%d, rmw:%d\n",
		blob, biod->bd_update, rmw_read);
}

void
bio_memcpy(struct bio_desc *biod, uint16_t media, void *media_addr,
	   void *addr, ssize_t n)
{
	struct umem_instance *umem = biod->bd_ctxt->bic_umem;

	if (biod->bd_update && media == DAOS_MEDIA_SCM) {
		/*
		 * We could do no_drain copy and rely on the tx commit to
		 * drain controller, however, test shows calling a persistent
		 * copy and drain controller here is faster.
		 */
		pmemobj_memcpy_persist(umem->umm_pool, media_addr, addr, n);
	} else {
		if (biod->bd_update)
			memcpy(media_addr, addr, n);
		else
			memcpy(addr, media_addr, n);
	}
}

static int
copy_one(struct bio_desc *biod, struct bio_iov *biov,
	 struct bio_copy_args *arg)
{
	d_sg_list_t	*sgl;
	void		*addr = bio_iov2req_buf(biov);
	ssize_t		 size = bio_iov2req_len(biov);
	uint16_t	 media = bio_iov2media(biov);

	D_ASSERT(arg->ca_sgl_idx < arg->ca_sgl_cnt);
	sgl = &arg->ca_sgls[arg->ca_sgl_idx];

	while (arg->ca_iov_idx < sgl->sg_nr) {
		d_iov_t *iov;
		ssize_t nob, buf_len;

		iov = &sgl->sg_iovs[arg->ca_iov_idx];
		buf_len = biod->bd_update ? iov->iov_len : iov->iov_buf_len;

		if (buf_len <= arg->ca_iov_off) {
			D_ERROR("Invalid iov[%d] "DF_U64"/"DF_U64" %d\n",
				arg->ca_iov_idx, arg->ca_iov_off,
				buf_len, biod->bd_update);
			return -DER_INVAL;
		}

		nob = min(size, buf_len - arg->ca_iov_off);
		if (addr != NULL) {
			D_DEBUG(DB_TRACE, "bio copy %p size %zd\n",
				addr, nob);
			bio_memcpy(biod, media, addr, iov->iov_buf +
					arg->ca_iov_off, nob);
			addr += nob;
		} else {
			/* fetch on hole */
			D_ASSERT(!biod->bd_update);
		}

		arg->ca_iov_off += nob;
		if (!biod->bd_update) {
			/* the first population for fetch */
			if (arg->ca_iov_off == nob)
				sgl->sg_nr_out++;

			iov->iov_len = arg->ca_iov_off;
			/* consumed an iov, move to the next */
			if (iov->iov_len == iov->iov_buf_len) {
				arg->ca_iov_off = 0;
				arg->ca_iov_idx++;
			}
		} else {
			/* consumed an iov, move to the next */
			if (arg->ca_iov_off == iov->iov_len) {
				arg->ca_iov_off = 0;
				arg->ca_iov_idx++;
			}
		}

		size -= nob;
		if (size == 0)
			return 0;
	}

	D_DEBUG(DB_TRACE, "Consumed all iovs, "DF_U64" bytes left\n", size);
	return -DER_REC2BIG;
}

static void
dma_drop_iod(struct bio_dma_buffer *bdb)
{
	D_ASSERT(bdb->bdb_active_iods > 0);
	bdb->bdb_active_iods--;

	ABT_mutex_lock(bdb->bdb_mutex);
	ABT_cond_broadcast(bdb->bdb_wait_iods);
	ABT_mutex_unlock(bdb->bdb_mutex);
}

int
bio_iod_prep(struct bio_desc *biod)
{
	struct bio_dma_buffer *bdb;
	int rc, retry_cnt = 0;

	if (biod->bd_buffer_prep)
		return -DER_INVAL;

retry:
	rc = iterate_biov(biod, dma_map_one, NULL);
	if (rc) {
		/*
		 * To avoid deadlock, held buffers need be released
		 * before waiting for other active IODs.
		 */
		iod_release_buffer(biod);

		if (!biod->bd_retry)
			return rc;

		biod->bd_retry = 0;
		bdb = iod_dma_buf(biod);
		if (!bdb->bdb_active_iods) {
			D_ERROR("Per-xstream DMA buffer isn't large enough "
				"to satisfy large IOD %p\n", biod);
			return rc;
		}

		D_DEBUG(DB_IO, "IOD %p waits for active IODs. %d\n",
			biod, retry_cnt++);

		ABT_mutex_lock(bdb->bdb_mutex);
		ABT_cond_wait(bdb->bdb_wait_iods, bdb->bdb_mutex);
		ABT_mutex_unlock(bdb->bdb_mutex);

		D_DEBUG(DB_IO, "IOD %p finished waiting. %d\n",
			biod, retry_cnt);

		goto retry;
	}
	biod->bd_buffer_prep = 1;

	/* All SCM IOVs, no DMA transfer prepared */
	if (biod->bd_rsrvd.brd_rg_cnt == 0)
		return 0;

	bdb = iod_dma_buf(biod);
	bdb->bdb_active_iods++;

	rc = ABT_eventual_create(0, &biod->bd_dma_done);
	if (rc != ABT_SUCCESS) {
		rc = -DER_NOMEM;
		goto failed;
	}

	dma_rw(biod, true);
	if (biod->bd_result) {
		rc = biod->bd_result;
		goto failed;
	}

	return 0;
failed:
	iod_release_buffer(biod);
	dma_drop_iod(bdb);
	return rc;
}

int
bio_iod_post(struct bio_desc *biod)
{
	struct bio_dma_buffer *bdb;

	if (!biod->bd_buffer_prep)
		return -DER_INVAL;

	/* No more actions for SCM IOVs */
	if (biod->bd_rsrvd.brd_rg_cnt == 0) {
		iod_release_buffer(biod);
		return 0;
	}

	if (biod->bd_update)
		dma_rw(biod, false);
	else
		biod->bd_result = 0;

	iod_release_buffer(biod);
	bdb = iod_dma_buf(biod);
	dma_drop_iod(bdb);

	return biod->bd_result;
}

int
bio_iod_copy(struct bio_desc *biod, d_sg_list_t *sgls, unsigned int nr_sgl)
{
	struct bio_copy_args arg = { 0 };

	if (!biod->bd_buffer_prep)
		return -DER_INVAL;

	if (biod->bd_sgl_cnt != nr_sgl)
		return -DER_INVAL;

	arg.ca_sgls = sgls;
	arg.ca_sgl_cnt = nr_sgl;

	return iterate_biov(biod, copy_one, &arg);
}

static int
flush_one(struct bio_desc *biod, struct bio_iov *biov,
	  struct bio_copy_args *arg)
{
	struct umem_instance *umem = biod->bd_ctxt->bic_umem;

	D_ASSERT(arg == NULL);
	D_ASSERT(biov);

	if (bio_addr_is_hole(&biov->bi_addr))
		return 0;

	if (biov->bi_addr.ba_type != DAOS_MEDIA_SCM)
		return 0;

	D_ASSERT(bio_iov2raw_buf(biov) != NULL);
	D_ASSERT(bio_iov2req_len(biov) != 0);
	pmemobj_flush(umem->umm_pool, bio_iov2req_buf(biov),
		      bio_iov2req_len(biov));
	return 0;
}

void
bio_iod_flush(struct bio_desc *biod)
{
	D_ASSERT(biod->bd_buffer_prep);
	if (biod->bd_update)
		iterate_biov(biod, flush_one, NULL);
}

static int
bio_rwv(struct bio_io_context *ioctxt, struct bio_sglist *bsgl_in,
	d_sg_list_t *sgl, bool update)
{
	struct bio_sglist	*bsgl;
	struct bio_desc		*biod;
	int			 i, rc;

	/* allocate blob I/O descriptor */
	biod = bio_iod_alloc(ioctxt, 1 /* single bsgl */, update);
	if (biod == NULL)
		return -DER_NOMEM;

	/*
	 * copy the passed in @bsgl_in to the bsgl attached on bio_desc,
	 * since we don't want following bio ops change caller's bsgl.
	 */
	bsgl = bio_iod_sgl(biod, 0);

	rc = bio_sgl_init(bsgl, bsgl_in->bs_nr);
	if (rc)
		goto out;

	for (i = 0; i < bsgl->bs_nr; i++) {
		D_ASSERT(bio_iov2buf(&bsgl_in->bs_iovs[i]) == NULL);
		D_ASSERT(bio_iov2len(&bsgl_in->bs_iovs[i]) != 0);
		bsgl->bs_iovs[i] = bsgl_in->bs_iovs[i];
	}
	bsgl->bs_nr_out = bsgl->bs_nr;

	/* map the biov to DMA safe buffer, fill DMA buffer if read operation */
	rc = bio_iod_prep(biod);
	if (rc)
		goto out;

	for (i = 0; i < bsgl->bs_nr; i++)
		D_ASSERT(bio_iov2raw_buf(&bsgl->bs_iovs[i]) != NULL);

	rc = bio_iod_copy(biod, sgl, 1 /* single sgl */);
	if (rc)
		D_ERROR("Copy biod failed, "DF_RC"\n", DP_RC(rc));

	/* release DMA buffer, write data back to NVMe device for write */
	rc = bio_iod_post(biod);

out:
	bio_iod_free(biod); /* also calls bio_sgl_fini */

	return rc;
}

int
bio_readv(struct bio_io_context *ioctxt, struct bio_sglist *bsgl,
	  d_sg_list_t *sgl)
{
	int	rc;

	rc = bio_rwv(ioctxt, bsgl, sgl, false);
	if (rc)
		D_ERROR("Readv to blob:%p failed for xs:%p, rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, rc);
	else
		D_DEBUG(DB_IO, "Readv to blob %p for xs:%p successfully\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	return rc;
}

int
bio_writev(struct bio_io_context *ioctxt, struct bio_sglist *bsgl,
	   d_sg_list_t *sgl)
{
	int	rc;

	rc = bio_rwv(ioctxt, bsgl, sgl, true);
	if (rc)
		D_ERROR("Writev to blob:%p failed for xs:%p, rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, rc);
	else
		D_DEBUG(DB_IO, "Writev to blob %p for xs:%p successfully\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	return rc;
}

static int
bio_rw(struct bio_io_context *ioctxt, bio_addr_t addr, d_iov_t *iov,
	bool update)
{
	struct bio_sglist	bsgl;
	struct bio_iov		biov;
	d_sg_list_t		sgl;
	int			rc;

	bio_iov_set(&biov, addr, iov->iov_len);
	bsgl.bs_iovs = &biov;
	bsgl.bs_nr = bsgl.bs_nr_out = 1;

	sgl.sg_iovs = iov;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;

	rc = bio_rwv(ioctxt, &bsgl, &sgl, update);
	if (rc)
		D_ERROR("%s to blob:%p failed for xs:%p, rc:%d\n",
			update ? "Write" : "Read", ioctxt->bic_blob,
			ioctxt->bic_xs_ctxt, rc);
	else
		D_DEBUG(DB_IO, "%s to blob %p for xs:%p successfully\n",
			update ? "Write" : "Read", ioctxt->bic_blob,
			ioctxt->bic_xs_ctxt);

	return rc;
}

int
bio_read(struct bio_io_context *ioctxt, bio_addr_t addr, d_iov_t *iov)
{
	return bio_rw(ioctxt, addr, iov, false);
}


int
bio_write(struct bio_io_context *ioctxt, bio_addr_t addr, d_iov_t *iov)
{
	return bio_rw(ioctxt, addr, iov, true);
}
