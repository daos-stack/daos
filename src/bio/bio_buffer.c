/**
 * (C) Copyright 2018-2023 Intel Corporation.
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

	if (bio_spdk_inited)
		spdk_dma_free(chunk->bdc_ptr);
	else
		free(chunk->bdc_ptr);

	D_FREE(chunk);
}

static struct bio_dma_chunk *
dma_alloc_chunk(unsigned int cnt)
{
	struct bio_dma_chunk *chunk;
	ssize_t bytes = (ssize_t)cnt << BIO_DMA_PAGE_SHIFT;
	int rc;

	D_ASSERT(bytes > 0);

	if (DAOS_FAIL_CHECK(DAOS_NVME_ALLOCBUF_ERR)) {
		D_ERROR("Injected DMA buffer allocation error.\n");
		return NULL;
	}

	D_ALLOC_PTR(chunk);
	if (chunk == NULL) {
		return NULL;
	}

	if (bio_spdk_inited) {
		chunk->bdc_ptr = spdk_dma_malloc_socket(bytes, BIO_DMA_PAGE_SZ, NULL,
							bio_numa_node);
	} else {
		rc = posix_memalign(&chunk->bdc_ptr, BIO_DMA_PAGE_SZ, bytes);
		if (rc)
			chunk->bdc_ptr = NULL;
	}

	if (chunk->bdc_ptr == NULL) {
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
		if (buf->bdb_stats.bds_chks_tot)
			d_tm_set_gauge(buf->bdb_stats.bds_chks_tot, buf->bdb_tot_cnt);
	}
}

int
dma_buffer_grow(struct bio_dma_buffer *buf, unsigned int cnt)
{
	struct bio_dma_chunk *chunk;
	int i, rc = 0;

	D_ASSERT((buf->bdb_tot_cnt + cnt) <= bio_chk_cnt_max);

	for (i = 0; i < cnt; i++) {
		chunk = dma_alloc_chunk(bio_chk_sz);
		if (chunk == NULL) {
			rc = -DER_NOMEM;
			break;
		}

		d_list_add_tail(&chunk->bdc_link, &buf->bdb_idle_list);
		buf->bdb_tot_cnt++;
		if (buf->bdb_stats.bds_chks_tot)
			d_tm_set_gauge(buf->bdb_stats.bds_chks_tot, buf->bdb_tot_cnt);
	}

	return rc;
}

void
dma_buffer_destroy(struct bio_dma_buffer *buf)
{
	D_ASSERT(d_list_empty(&buf->bdb_used_list));
	D_ASSERT(buf->bdb_active_iods == 0);
	D_ASSERT(buf->bdb_queued_iods == 0);

	bulk_cache_destroy(buf);
	dma_buffer_shrink(buf, buf->bdb_tot_cnt);

	D_ASSERT(buf->bdb_tot_cnt == 0);
	ABT_mutex_free(&buf->bdb_mutex);
	ABT_cond_free(&buf->bdb_wait_iod);
	ABT_cond_free(&buf->bdb_fifo);

	D_FREE(buf);
}

static inline char *
chk_type2str(int chk_type)
{
	switch (chk_type) {
	case BIO_CHK_TYPE_IO:
		return "io";
	case BIO_CHK_TYPE_LOCAL:
		return "local";
	case BIO_CHK_TYPE_REBUILD:
		return "rebuild";
	default:
		return "unknown";
	}
}

static void
dma_metrics_init(struct bio_dma_buffer *bdb, int tgt_id)
{
	struct bio_dma_stats	*stats = &bdb->bdb_stats;
	char			 desc[40];
	int			 i, rc;

	rc = d_tm_add_metric(&stats->bds_chks_tot, D_TM_GAUGE, "Total chunks", "chunk",
			     "dmabuff/total_chunks/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create total_chunks telemetry: "DF_RC"\n", DP_RC(rc));

	for (i = BIO_CHK_TYPE_IO; i < BIO_CHK_TYPE_MAX; i++) {
		snprintf(desc, sizeof(desc), "Used chunks (%s)", chk_type2str(i));
		rc = d_tm_add_metric(&stats->bds_chks_used[i], D_TM_GAUGE, desc, "chunk",
				     "dmabuff/used_chunks_%s/tgt_%d", chk_type2str(i), tgt_id);
		if (rc)
			D_WARN("Failed to create used_chunks_%s telemetry: "DF_RC"\n",
			       chk_type2str(i), DP_RC(rc));
	}

	rc = d_tm_add_metric(&stats->bds_bulk_grps, D_TM_GAUGE, "Total bulk grps", "grp",
			     "dmabuff/bulk_grps/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create total_bulk_grps telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_active_iods, D_TM_GAUGE, "Active requests", "req",
			     "dmabuff/active_reqs/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create active_requests telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_queued_iods, D_TM_GAUGE, "Queued requests", "req",
			     "dmabuff/queued_reqs/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create queued_requests telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_grab_errs, D_TM_COUNTER, "Grab buffer errors", "err",
			     "dmabuff/grab_errs/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create grab_errs telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_grab_retries, D_TM_STATS_GAUGE, "Grab buffer retry count",
			     "retry", "dmabuff/grab_retries/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create grab_retries telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_wal_sz, D_TM_STATS_GAUGE, "WAL tx size",
			     "bytes", "dmabuff/wal_sz/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create WAL size telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_wal_qd, D_TM_STATS_GAUGE, "WAL tx QD",
			     "commits", "dmabuff/wal_qd/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create WAL QD telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->bds_wal_waiters, D_TM_STATS_GAUGE, "WAL waiters",
			     "transactions", "dmabuff/wal_waiters/tgt_%d", tgt_id);
	if (rc)
		D_WARN("Failed to create WAL waiters telemetry: "DF_RC"\n", DP_RC(rc));
}

struct bio_dma_buffer *
dma_buffer_create(unsigned int init_cnt, int tgt_id)
{
	struct bio_dma_buffer *buf;
	int rc;

	D_ALLOC_PTR(buf);
	if (buf == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&buf->bdb_idle_list);
	D_INIT_LIST_HEAD(&buf->bdb_used_list);
	buf->bdb_tot_cnt = 0;
	buf->bdb_active_iods = 0;

	rc = ABT_mutex_create(&buf->bdb_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE(buf);
		return NULL;
	}

	rc = ABT_cond_create(&buf->bdb_wait_iod);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&buf->bdb_mutex);
		D_FREE(buf);
		return NULL;
	}

	rc = ABT_cond_create(&buf->bdb_fifo);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&buf->bdb_mutex);
		ABT_cond_free(&buf->bdb_wait_iod);
		D_FREE(buf);
		return NULL;
	}

	rc = bulk_cache_create(buf);
	if (rc != 0) {
		ABT_mutex_free(&buf->bdb_mutex);
		ABT_cond_free(&buf->bdb_wait_iod);
		ABT_cond_free(&buf->bdb_fifo);
		D_FREE(buf);
		return NULL;
	}

	dma_metrics_init(buf, tgt_id);

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
bio_iod_alloc(struct bio_io_context *ctxt, struct umem_instance *umem,
	      unsigned int sgl_cnt, unsigned int type)
{
	struct bio_desc	*biod;

	D_ASSERT(ctxt != NULL);
	D_ASSERT(sgl_cnt != 0);

	D_ALLOC(biod, offsetof(struct bio_desc, bd_sgls[sgl_cnt]));
	if (biod == NULL)
		return NULL;

	D_ASSERT(type < BIO_IOD_TYPE_MAX);
	biod->bd_umem = umem;
	biod->bd_ctxt = ctxt;
	biod->bd_type = type;
	biod->bd_sgl_cnt = sgl_cnt;

	biod->bd_dma_done = ABT_EVENTUAL_NULL;
	return biod;
}

static inline void
iod_dma_completion(struct bio_desc *biod, int err)
{
	if (biod->bd_completion != NULL) {
		D_ASSERT(biod->bd_comp_arg != NULL);
		biod->bd_completion(biod->bd_comp_arg, err);
	} else if (biod->bd_dma_done != ABT_EVENTUAL_NULL) {
		ABT_eventual_set(biod->bd_dma_done, NULL, 0);
	}
}

void
iod_dma_wait(struct bio_desc *biod)
{
	struct bio_xs_context	*xs_ctxt = biod->bd_ctxt->bic_xs_ctxt;
	int			 rc;

	D_ASSERT(xs_ctxt != NULL);
	if (xs_ctxt->bxc_self_polling) {
		D_DEBUG(DB_IO, "Self poll completion\n");
		rc = xs_poll_completion(xs_ctxt, &biod->bd_inflights, 0);
		if (rc)
			D_ERROR("Self poll completion failed. "DF_RC"\n", DP_RC(rc));
	} else if (biod->bd_inflights != 0) {
		rc = ABT_eventual_wait(biod->bd_dma_done, NULL);
		if (rc != ABT_SUCCESS)
			D_ERROR("ABT eventual wait failed. %d\n", rc);
	}
}

void
bio_iod_free(struct bio_desc *biod)
{
	int i;

	if (biod->bd_async_post && biod->bd_dma_done != ABT_EVENTUAL_NULL)
		iod_dma_wait(biod);

	D_ASSERT(!biod->bd_buffer_prep);

	if (biod->bd_dma_done != ABT_EVENTUAL_NULL)
		ABT_eventual_free(&biod->bd_dma_done);

	for (i = 0; i < biod->bd_sgl_cnt; i++)
		bio_sgl_fini(&biod->bd_sgls[i]);

	D_FREE(biod->bd_bulk_hdls);

	D_FREE(biod);
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

	/* Release bulk handles */
	bulk_iod_release(biod);

	/* No reserved DMA regions */
	if (rsrvd_dma->brd_rg_cnt == 0) {
		D_ASSERT(rsrvd_dma->brd_rg_max == 0);
		D_ASSERT(rsrvd_dma->brd_chk_max == 0);
		biod->bd_buffer_prep = 0;
		return;
	}

	D_ASSERT(rsrvd_dma->brd_regions != NULL);
	D_FREE(rsrvd_dma->brd_regions);
	rsrvd_dma->brd_regions = NULL;
	rsrvd_dma->brd_rg_max = rsrvd_dma->brd_rg_cnt = 0;
	biod->bd_nvme_bytes = 0;

	/* All DMA chunks are used through cached bulk handle */
	if (rsrvd_dma->brd_chk_cnt == 0) {
		D_ASSERT(rsrvd_dma->brd_dma_chks == NULL);
		D_ASSERT(rsrvd_dma->brd_chk_max == 0);
		biod->bd_buffer_prep = 0;
		return;
	}

	/* Release the DMA chunks not from cached bulk handle */
	D_ASSERT(rsrvd_dma->brd_dma_chks != NULL);
	bdb = iod_dma_buf(biod);
	for (i = 0; i < rsrvd_dma->brd_chk_cnt; i++) {
		struct bio_dma_chunk *chunk = rsrvd_dma->brd_dma_chks[i];

		D_ASSERT(chunk != NULL);
		D_ASSERT(chunk->bdc_ref > 0);
		D_ASSERT(chunk->bdc_type == biod->bd_chk_type);
		D_ASSERT(chunk->bdc_bulk_grp == NULL);
		chunk->bdc_ref--;

		D_DEBUG(DB_IO, "Release chunk:%p[%p] idx:%u ref:%u huge:%d "
			"type:%u\n", chunk, chunk->bdc_ptr, chunk->bdc_pg_idx,
			chunk->bdc_ref, dma_chunk_is_huge(chunk),
			chunk->bdc_type);

		if (dma_chunk_is_huge(chunk)) {
			dma_free_chunk(chunk);
		} else if (chunk->bdc_ref == 0) {
			chunk->bdc_pg_idx = 0;
			D_ASSERT(bdb->bdb_used_cnt[chunk->bdc_type] > 0);
			bdb->bdb_used_cnt[chunk->bdc_type] -= 1;
			if (bdb->bdb_stats.bds_chks_used[chunk->bdc_type])
				d_tm_set_gauge(bdb->bdb_stats.bds_chks_used[chunk->bdc_type],
					       bdb->bdb_used_cnt[chunk->bdc_type]);

			if (chunk == bdb->bdb_cur_chk[chunk->bdc_type])
				bdb->bdb_cur_chk[chunk->bdc_type] = NULL;
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
	/* Total size to be copied */
	unsigned int	 ca_size_tot;
	/* Copied size */
	unsigned int	 ca_size_copied;

};

static int
copy_one(struct bio_desc *biod, struct bio_iov *biov, void *data)
{
	struct bio_copy_args	*arg = data;
	d_sg_list_t		*sgl;
	void			*addr = bio_iov2req_buf(biov);
	ssize_t			 size = bio_iov2req_len(biov);
	uint16_t		 media = bio_iov2media(biov);

	if (bio_iov2req_len(biov) == 0)
		return 0;

	D_ASSERT(biod->bd_type < BIO_IOD_TYPE_GETBUF);
	D_ASSERT(arg->ca_sgl_idx < arg->ca_sgl_cnt);
	sgl = &arg->ca_sgls[arg->ca_sgl_idx];

	while (arg->ca_iov_idx < sgl->sg_nr) {
		d_iov_t *iov;
		ssize_t nob, buf_len;

		iov = &sgl->sg_iovs[arg->ca_iov_idx];
		buf_len = (biod->bd_type == BIO_IOD_TYPE_UPDATE) ?
					iov->iov_len : iov->iov_buf_len;

		if (buf_len <= arg->ca_iov_off) {
			D_ERROR("Invalid iov[%d] "DF_U64"/"DF_U64" %d\n",
				arg->ca_iov_idx, arg->ca_iov_off,
				buf_len, biod->bd_type);
			return -DER_INVAL;
		}

		if (iov->iov_buf == NULL) {
			D_ERROR("Invalid iov[%d], iov_buf is NULL\n",
				arg->ca_iov_idx);
			return -DER_INVAL;
		}

		nob = min(size, buf_len - arg->ca_iov_off);
		if (arg->ca_size_tot) {
			if ((nob + arg->ca_size_copied) > arg->ca_size_tot) {
				D_ERROR("Copy size %u is not aligned with IOVs %u/"DF_U64"\n",
					arg->ca_size_tot, arg->ca_size_copied, nob);
				return -DER_INVAL;
			}
			arg->ca_size_copied += nob;
		}

		if (addr != NULL) {
			D_DEBUG(DB_TRACE, "bio copy %p size %zd\n",
				addr, nob);
			bio_memcpy(biod, media, addr, iov->iov_buf +
					arg->ca_iov_off, nob);
			addr += nob;
		} else {
			/* fetch on hole */
			D_ASSERT(biod->bd_type == BIO_IOD_TYPE_FETCH);
		}

		arg->ca_iov_off += nob;
		if (biod->bd_type == BIO_IOD_TYPE_FETCH) {
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

		/* Specified copy size finished, abort */
		if (arg->ca_size_tot && (arg->ca_size_copied == arg->ca_size_tot))
			return 1;

		size -= nob;
		if (size == 0)
			return 0;
	}

	D_DEBUG(DB_TRACE, "Consumed all iovs, "DF_U64" bytes left\n", size);
	return -DER_REC2BIG;
}

static int
iterate_biov(struct bio_desc *biod,
	     int (*cb_fn)(struct bio_desc *, struct bio_iov *, void *data),
	     void *data)
{
	int i, j, rc = 0;

	for (i = 0; i < biod->bd_sgl_cnt; i++) {
		struct bio_sglist *bsgl = &biod->bd_sgls[i];

		if (data != NULL) {
			if (cb_fn == copy_one) {
				struct bio_copy_args *arg = data;

				D_ASSERT(i < arg->ca_sgl_cnt);
				arg->ca_sgl_idx = i;
				arg->ca_iov_idx = 0;
				arg->ca_iov_off = 0;
				if (biod->bd_type == BIO_IOD_TYPE_FETCH)
					arg->ca_sgls[i].sg_nr_out = 0;
			} else if (cb_fn == bulk_map_one) {
				struct bio_bulk_args *arg = data;

				arg->ba_sgl_idx = i;
			}
		}

		if (bsgl->bs_nr_out == 0)
			continue;

		for (j = 0; j < bsgl->bs_nr_out; j++) {
			struct bio_iov *biov = &bsgl->bs_iovs[j];

			rc = cb_fn(biod, biov, data);
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

static int
chunk_get_idle(struct bio_dma_buffer *bdb, struct bio_dma_chunk **chk_ptr)
{
	struct bio_dma_chunk *chk;
	int rc;

	if (d_list_empty(&bdb->bdb_idle_list)) {
		/* Try grow buffer first */
		if (bdb->bdb_tot_cnt < bio_chk_cnt_max) {
			rc = dma_buffer_grow(bdb, 1);
			if (rc == 0)
				goto done;
		}

		/* Try to reclaim an unused chunk from bulk groups */
		rc = bulk_reclaim_chunk(bdb, NULL);
		if (rc)
			return rc;
	}
done:
	D_ASSERT(!d_list_empty(&bdb->bdb_idle_list));
	chk = d_list_entry(bdb->bdb_idle_list.next, struct bio_dma_chunk,
			   bdc_link);
	d_list_move_tail(&chk->bdc_link, &bdb->bdb_used_list);
	*chk_ptr = chk;

	return 0;
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

int
iod_add_region(struct bio_desc *biod, struct bio_dma_chunk *chk,
	       unsigned int chk_pg_idx, unsigned int chk_off, uint64_t off,
	       uint64_t end, uint8_t media)
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
	rsrvd_dma->brd_regions[cnt].brr_chk_off = chk_off;
	rsrvd_dma->brd_regions[cnt].brr_off = off;
	rsrvd_dma->brd_regions[cnt].brr_end = end;
	rsrvd_dma->brd_regions[cnt].brr_media = media;
	rsrvd_dma->brd_rg_cnt++;

	if (media == DAOS_MEDIA_NVME)
		biod->bd_nvme_bytes += (end - off);

	return 0;
}

static inline bool
direct_scm_access(struct bio_desc *biod, struct bio_iov *biov)
{
	/* Get buffer operation */
	if (biod->bd_type == BIO_IOD_TYPE_GETBUF)
		return false;

	if (bio_iov2media(biov) != DAOS_MEDIA_SCM)
		return false;
	/*
	 * Direct access SCM when:
	 *
	 * - It's inline I/O, or;
	 * - Direct SCM RDMA enabled, or;
	 * - It's deduped SCM extent;
	 */
	if (!biod->bd_rdma || bio_scm_rdma)
		return true;

	if (BIO_ADDR_IS_DEDUP(&biov->bi_addr)) {
		D_ASSERT(biod->bd_type == BIO_IOD_TYPE_UPDATE);
		return true;
	}

	return false;
}

static bool
iod_expand_region(struct bio_iov *biov, struct bio_rsrvd_region *last_rg,
		  uint64_t off, uint64_t end, unsigned int pg_cnt, unsigned int pg_off)
{
	uint64_t		cur_pg, prev_pg_start, prev_pg_end;
	unsigned int		chk_pg_idx;
	struct bio_dma_chunk	*chk = last_rg->brr_chk;

	chk_pg_idx = last_rg->brr_pg_idx;
	D_ASSERT(chk_pg_idx < bio_chk_sz);

	prev_pg_start = last_rg->brr_off >> BIO_DMA_PAGE_SHIFT;
	prev_pg_end = last_rg->brr_end >> BIO_DMA_PAGE_SHIFT;
	cur_pg = off >> BIO_DMA_PAGE_SHIFT;
	D_ASSERT(prev_pg_start <= prev_pg_end);

	/* Only merge NVMe regions */
	if (bio_iov2media(biov) == DAOS_MEDIA_SCM ||
	    bio_iov2media(biov) != last_rg->brr_media)
		return false;

	/* Not consecutive with prev rg */
	if (cur_pg != prev_pg_end)
		return false;

	D_DEBUG(DB_TRACE, "merging IOVs: ["DF_U64", "DF_U64"), ["DF_U64", "DF_U64")\n",
		last_rg->brr_off, last_rg->brr_end, off, end);

	if (last_rg->brr_off < off)
		chk_pg_idx += (prev_pg_end - prev_pg_start);
	else
		/* The prev region must be covered by one page */
		D_ASSERTF(prev_pg_end == prev_pg_start,
			  ""DF_U64" != "DF_U64"\n", prev_pg_end, prev_pg_start);

	bio_iov_set_raw_buf(biov, chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off));
	if (bio_iov2raw_buf(biov) == NULL)
		return false;

	if (off < last_rg->brr_off)
		last_rg->brr_off = off;
	if (end > last_rg->brr_end)
		last_rg->brr_end = end;

	D_DEBUG(DB_TRACE, "Consecutive reserve %p.\n", bio_iov2raw_buf(biov));
	return true;
}

static bool
iod_pad_region(struct bio_iov *biov, struct bio_rsrvd_region *last_rg, unsigned int *chk_off)
{
	struct bio_dma_chunk	*chk = last_rg->brr_chk;
	unsigned int		 chk_pg_idx = last_rg->brr_pg_idx;
	unsigned int		 off, pg_off;
	void			*payload;

	if (bio_iov2media(biov) != DAOS_MEDIA_SCM ||
	    last_rg->brr_media != DAOS_MEDIA_SCM)
		return false;

	D_ASSERT(last_rg->brr_end > last_rg->brr_off);
	off = last_rg->brr_chk_off + (last_rg->brr_end - last_rg->brr_off);
	pg_off = off & (BIO_DMA_PAGE_SZ - 1);

	/* The last page is used up */
	if (pg_off == 0)
		return false;

	/* The last page doesn't have enough free space */
	if (pg_off + bio_iov2raw_len(biov) > BIO_DMA_PAGE_SZ)
		return false;

	payload = chk->bdc_ptr + (chk_pg_idx << BIO_DMA_PAGE_SHIFT) + off;
	bio_iov_set_raw_buf(biov, payload);
	*chk_off = off;	/* Set for current region */

	D_DEBUG(DB_TRACE, "Padding reserve %p.\n", bio_iov2raw_buf(biov));
	return true;
}

/* Convert offset of @biov into memory pointer */
int
dma_map_one(struct bio_desc *biod, struct bio_iov *biov, void *arg)
{
	struct bio_rsrvd_region *last_rg;
	struct bio_dma_buffer *bdb;
	struct bio_dma_chunk *chk = NULL, *cur_chk;
	uint64_t off, end;
	unsigned int pg_cnt, pg_off, chk_pg_idx, chk_off = 0;
	int rc;

	D_ASSERT(arg == NULL);
	D_ASSERT(biov);
	D_ASSERT(biod && biod->bd_chk_type < BIO_CHK_TYPE_MAX);

	if ((bio_iov2raw_len(biov) == 0) || bio_addr_is_hole(&biov->bi_addr)) {
		bio_iov_set_raw_buf(biov, NULL);
		return 0;
	}

	if (direct_scm_access(biod, biov)) {
		struct umem_instance *umem = biod->bd_umem;

		D_ASSERT(umem != NULL);
		bio_iov_set_raw_buf(biov, umem_off2ptr(umem, bio_iov2raw_off(biov)));
		return 0;
	}
	D_ASSERT(!BIO_ADDR_IS_DEDUP(&biov->bi_addr));

	bdb = iod_dma_buf(biod);
	dma_biov2pg(biov, &off, &end, &pg_cnt, &pg_off);

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

		chk->bdc_type = biod->bd_chk_type;
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
		D_DEBUG(DB_TRACE, "Last region %p:%d ["DF_U64","DF_U64")\n",
			last_rg->brr_chk, last_rg->brr_pg_idx,
			last_rg->brr_off, last_rg->brr_end);

		chk = last_rg->brr_chk;
		D_ASSERT(biod->bd_chk_type == chk->bdc_type);

		/* Expand the last NVMe region when it's contiguous with current NVMe region. */
		if (iod_expand_region(biov, last_rg, off, end, pg_cnt, pg_off))
			return 0;

		/*
		 * If prev region is SCM having unused bytes in last chunk page, try to reserve
		 * from the unused bytes for current SCM region.
		 */
		if (iod_pad_region(biov, last_rg, &chk_off)) {
			chk_pg_idx = last_rg->brr_pg_idx;
			goto add_region;
		}
	}

	/* Try to reserve from the last DMA chunk in io descriptor */
	if (chk != NULL) {
		D_ASSERT(biod->bd_chk_type == chk->bdc_type);
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
	cur_chk = bdb->bdb_cur_chk[biod->bd_chk_type];
	if (cur_chk != NULL && cur_chk != chk) {
		chk = cur_chk;
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
	rc = chunk_get_idle(bdb, &chk);
	if (rc) {
		if (rc == -DER_AGAIN)
			biod->bd_retry = 1;
		else
			D_ERROR("Failed to get idle chunk. "DF_RC"\n", DP_RC(rc));

		return rc;
	}

	D_ASSERT(chk != NULL);
	chk->bdc_type = biod->bd_chk_type;
	bdb->bdb_cur_chk[chk->bdc_type] = chk;
	bdb->bdb_used_cnt[chk->bdc_type] += 1;
	if (bdb->bdb_stats.bds_chks_used[chk->bdc_type])
		d_tm_set_gauge(bdb->bdb_stats.bds_chks_used[chk->bdc_type],
			       bdb->bdb_used_cnt[chk->bdc_type]);
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
	return iod_add_region(biod, chk, chk_pg_idx, chk_off, off, end,
			      bio_iov2media(biov));
}

static inline bool
injected_nvme_error(struct bio_desc *biod)
{
	if (biod->bd_type == BIO_IOD_TYPE_UPDATE)
		return DAOS_FAIL_CHECK(DAOS_NVME_WRITE_ERR) != 0;
	else
		return DAOS_FAIL_CHECK(DAOS_NVME_READ_ERR) != 0;
}

static void
dma_drop_iod(struct bio_dma_buffer *bdb)
{
	D_ASSERT(bdb->bdb_active_iods > 0);
	bdb->bdb_active_iods--;
	if (bdb->bdb_stats.bds_active_iods)
		d_tm_set_gauge(bdb->bdb_stats.bds_active_iods, bdb->bdb_active_iods);

	ABT_mutex_lock(bdb->bdb_mutex);
	ABT_cond_broadcast(bdb->bdb_wait_iod);
	ABT_mutex_unlock(bdb->bdb_mutex);
}

static void
rw_completion(void *cb_arg, int err)
{
	struct bio_xs_context	*xs_ctxt;
	struct bio_xs_blobstore	*bxb;
	struct bio_io_context	*io_ctxt;
	struct bio_desc		*biod = cb_arg;
	struct media_error_msg	*mem;

	D_ASSERT(biod->bd_type < BIO_IOD_TYPE_GETBUF);
	D_ASSERT(biod->bd_inflights > 0);
	biod->bd_inflights--;

	bxb = biod->bd_ctxt->bic_xs_blobstore;
	D_ASSERT(bxb != NULL);
	D_ASSERT(bxb->bxb_blob_rw > 0);
	bxb->bxb_blob_rw--;

	io_ctxt = biod->bd_ctxt;
	D_ASSERT(io_ctxt != NULL);
	D_ASSERT(io_ctxt->bic_inflight_dmas > 0);
	io_ctxt->bic_inflight_dmas--;

	if (err != 0 || injected_nvme_error(biod)) {
		/* Report only one NVMe I/O error per IOD */
		if (biod->bd_result != 0)
			goto done;

		if (biod->bd_type == BIO_IOD_TYPE_FETCH || glb_criteria.fc_enabled)
			biod->bd_result = -DER_NVME_IO;
		else
			biod->bd_result = -DER_IO;

		D_ALLOC_PTR(mem);
		if (mem == NULL) {
			D_ERROR("NVMe I/O error report is skipped\n");
			goto done;
		}
		mem->mem_err_type = (biod->bd_type == BIO_IOD_TYPE_UPDATE) ? MET_WRITE : MET_READ;
		mem->mem_bs = bxb->bxb_blobstore;
		D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
		xs_ctxt = biod->bd_ctxt->bic_xs_ctxt;
		mem->mem_tgt_id = xs_ctxt->bxc_tgt_id;
		spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error, mem);
	}

done:
	if (biod->bd_inflights == 0) {
		iod_dma_completion(biod, err);
		if (biod->bd_async_post && biod->bd_buffer_prep) {
			iod_release_buffer(biod);
			dma_drop_iod(iod_dma_buf(biod));
		}
		D_DEBUG(DB_IO, "DMA complete, type:%d\n", biod->bd_type);
	}
}

void
bio_memcpy(struct bio_desc *biod, uint16_t media, void *media_addr,
	   void *addr, ssize_t n)
{
	D_ASSERT(biod->bd_type < BIO_IOD_TYPE_GETBUF);
	if (biod->bd_type == BIO_IOD_TYPE_UPDATE && media == DAOS_MEDIA_SCM) {
		struct umem_instance *umem = biod->bd_umem;

		D_ASSERT(umem != NULL);
		/*
		 * We could do no_drain copy and rely on the tx commit to
		 * drain controller, however, test shows calling a persistent
		 * copy and drain controller here is faster.
		 */
		if (DAOS_ON_VALGRIND && umem_tx_inprogress(umem)) {
			/** Ignore the update to what is reserved block.
			 *  Ordinarily, this wouldn't be inside a transaction
			 *  but in MVCC tests, it can happen.
			 */
			umem_tx_xadd_ptr(umem, media_addr, n,
					 UMEM_XADD_NO_SNAPSHOT);
		}
		umem_atomic_copy(umem, media_addr, addr, n, UMEM_RESERVED_MEM);
	} else {
		if (biod->bd_type == BIO_IOD_TYPE_UPDATE)
			memcpy(media_addr, addr, n);
		else
			memcpy(addr, media_addr, n);
	}
}

static void
scm_rw(struct bio_desc *biod, struct bio_rsrvd_region *rg)
{
	struct umem_instance	*umem = biod->bd_umem;
	void			*payload;

	D_ASSERT(biod->bd_rdma);
	D_ASSERT(!bio_scm_rdma);
	D_ASSERT(umem != NULL);

	payload = rg->brr_chk->bdc_ptr + (rg->brr_pg_idx << BIO_DMA_PAGE_SHIFT);
	payload += rg->brr_chk_off;

	D_DEBUG(DB_IO, "SCM RDMA, type:%d payload:%p len:"DF_U64"\n",
		biod->bd_type, payload, rg->brr_end - rg->brr_off);

	bio_memcpy(biod, DAOS_MEDIA_SCM, umem_off2ptr(umem, rg->brr_off),
		   payload, rg->brr_end - rg->brr_off);
}

static void
nvme_rw(struct bio_desc *biod, struct bio_rsrvd_region *rg)
{
	struct spdk_io_channel	*channel;
	struct spdk_blob	*blob;
	struct bio_xs_context	*xs_ctxt;
	uint64_t		 pg_idx, pg_cnt, rw_cnt;
	void			*payload;
	struct bio_xs_blobstore	*bxb = biod->bd_ctxt->bic_xs_blobstore;

	D_ASSERT(bxb != NULL);
	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
	xs_ctxt = biod->bd_ctxt->bic_xs_ctxt;
	blob = biod->bd_ctxt->bic_blob;
	channel = bxb->bxb_io_channel;

	/* Bypass NVMe I/O, used by daos_perf for performance evaluation */
	if (daos_io_bypass & IOBP_NVME)
		return;

	/* No locking for BS state query here is tolerable */
	if (bxb->bxb_blobstore->bb_state == BIO_BS_STATE_FAULTY) {
		D_ERROR("Blobstore is marked as FAULTY.\n");
		if (biod->bd_type == BIO_IOD_TYPE_FETCH || glb_criteria.fc_enabled)
			biod->bd_result = -DER_NVME_IO;
		else
			biod->bd_result = -DER_IO;
		return;
	}

	if (!is_blob_valid(biod->bd_ctxt)) {
		D_ERROR("Blobstore is invalid. blob:%p, closing:%d\n",
			blob, biod->bd_ctxt->bic_closing);
		biod->bd_result = -DER_NO_HDL;
		return;
	}

	D_ASSERT(channel != NULL);
	D_ASSERT(rg->brr_chk_off == 0);
	payload = rg->brr_chk->bdc_ptr + (rg->brr_pg_idx << BIO_DMA_PAGE_SHIFT);
	pg_idx = rg->brr_off >> BIO_DMA_PAGE_SHIFT;
	pg_cnt = (rg->brr_end + BIO_DMA_PAGE_SZ - 1) >> BIO_DMA_PAGE_SHIFT;
	D_ASSERT(pg_cnt > pg_idx);
	pg_cnt -= pg_idx;

	while (pg_cnt > 0) {

		drain_inflight_ios(xs_ctxt, bxb);

		biod->bd_dma_issued = 1;
		biod->bd_inflights++;
		bxb->bxb_blob_rw++;
		biod->bd_ctxt->bic_inflight_dmas++;

		rw_cnt = (pg_cnt > bio_chk_sz) ? bio_chk_sz : pg_cnt;

		D_DEBUG(DB_IO, "%s blob:%p payload:%p, pg_idx:"DF_U64", pg_cnt:"DF_U64"/"DF_U64"\n",
			biod->bd_type == BIO_IOD_TYPE_UPDATE ? "Write" : "Read",
			blob, payload, pg_idx, pg_cnt, rw_cnt);

		D_ASSERT(biod->bd_type < BIO_IOD_TYPE_GETBUF);
		if (biod->bd_type == BIO_IOD_TYPE_UPDATE)
			spdk_blob_io_write(blob, channel, payload,
					   page2io_unit(biod->bd_ctxt, pg_idx, BIO_DMA_PAGE_SZ),
					   page2io_unit(biod->bd_ctxt, rw_cnt, BIO_DMA_PAGE_SZ),
					   rw_completion, biod);
		else {
			spdk_blob_io_read(blob, channel, payload,
					  page2io_unit(biod->bd_ctxt, pg_idx, BIO_DMA_PAGE_SZ),
					  page2io_unit(biod->bd_ctxt, rw_cnt, BIO_DMA_PAGE_SZ),
					  rw_completion, biod);
			if (DAOS_ON_VALGRIND)
				VALGRIND_MAKE_MEM_DEFINED(payload, rw_cnt * BIO_DMA_PAGE_SZ);
		}

		pg_cnt -= rw_cnt;
		pg_idx += rw_cnt;
		payload += (rw_cnt * BIO_DMA_PAGE_SZ);
	}
}

static void
dma_rw(struct bio_desc *biod)
{
	struct bio_rsrvd_dma	*rsrvd_dma = &biod->bd_rsrvd;
	struct bio_rsrvd_region	*rg;
	int			 i;

	biod->bd_inflights = 1;

	D_ASSERT(biod->bd_type < BIO_IOD_TYPE_GETBUF);
	D_DEBUG(DB_IO, "DMA start, type:%d\n", biod->bd_type);

	for (i = 0; i < rsrvd_dma->brd_rg_cnt; i++) {
		rg = &rsrvd_dma->brd_regions[i];

		D_ASSERT(rg->brr_chk != NULL);
		D_ASSERT(rg->brr_end > rg->brr_off);

		if (rg->brr_media == DAOS_MEDIA_SCM)
			scm_rw(biod, rg);
		else
			nvme_rw(biod, rg);
	}

	D_ASSERT(biod->bd_inflights > 0);
	biod->bd_inflights -= 1;

	if (!biod->bd_async_post) {
		iod_dma_wait(biod);
		D_DEBUG(DB_IO, "Wait DMA done, type:%d\n", biod->bd_type);
	}
}

static inline bool
iod_should_retry(struct bio_desc *biod, struct bio_dma_buffer *bdb)
{
	/*
	 * When there isn't any in-flight IODs, it means the whole DMA buffer
	 * isn't large enough to satisfy current huge IOD, don't retry.
	 *
	 * When current IOD is for copy target, take the source IOD into account.
	 */
	if (biod->bd_copy_dst) {
		D_ASSERT(bdb->bdb_active_iods >= 1);
		return bdb->bdb_active_iods > 1;
	}
	return bdb->bdb_active_iods != 0;
}

static inline void
iod_fifo_wait(struct bio_desc *biod, struct bio_dma_buffer *bdb)
{
	if (!biod->bd_in_fifo) {
		biod->bd_in_fifo = 1;
		D_ASSERT(bdb->bdb_queued_iods == 0);
		bdb->bdb_queued_iods = 1;
		if (bdb->bdb_stats.bds_queued_iods)
			d_tm_set_gauge(bdb->bdb_stats.bds_queued_iods, bdb->bdb_queued_iods);
	}

	/* First waiter in the FIFO queue waits on 'bdb_wait_iod' */
	ABT_mutex_lock(bdb->bdb_mutex);
	ABT_cond_wait(bdb->bdb_wait_iod, bdb->bdb_mutex);
	ABT_mutex_unlock(bdb->bdb_mutex);
}

static void
iod_fifo_in(struct bio_desc *biod, struct bio_dma_buffer *bdb)
{
	/* No prior waiters */
	if (!bdb || bdb->bdb_queued_iods == 0)
		return;
	/*
	 * Non-blocking prep request is usually from high priority job like checkpointing,
	 * so we allow it jump the queue.
	 */
	if (biod->bd_non_blocking)
		return;

	biod->bd_in_fifo = 1;
	bdb->bdb_queued_iods++;
	if (bdb->bdb_stats.bds_queued_iods)
		d_tm_set_gauge(bdb->bdb_stats.bds_queued_iods, bdb->bdb_queued_iods);

	/* Except the first waiter, all other waiters in FIFO queue wait on 'bdb_fifo' */
	ABT_mutex_lock(bdb->bdb_mutex);
	ABT_cond_wait(bdb->bdb_fifo, bdb->bdb_mutex);
	ABT_mutex_unlock(bdb->bdb_mutex);
}

static void
iod_fifo_out(struct bio_desc *biod, struct bio_dma_buffer *bdb)
{
	if (!biod->bd_in_fifo)
		return;

	biod->bd_in_fifo = 0;
	D_ASSERT(bdb != NULL);
	D_ASSERT(bdb->bdb_queued_iods > 0);
	bdb->bdb_queued_iods--;
	if (bdb->bdb_stats.bds_queued_iods)
		d_tm_set_gauge(bdb->bdb_stats.bds_queued_iods, bdb->bdb_queued_iods);

	/* Wakeup next one in the FIFO queue */
	if (bdb->bdb_queued_iods) {
		ABT_mutex_lock(bdb->bdb_mutex);
		ABT_cond_signal(bdb->bdb_fifo);
		ABT_mutex_unlock(bdb->bdb_mutex);
	}
}

#define	DMA_INFO_DUMP_INTVL	60	/* seconds */
static void
dump_dma_info(struct bio_dma_buffer *bdb)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg;
	uint64_t		 cur;
	int			 i, bulk_grps = 0, bulk_chunks = 0;

	cur = daos_gettime_coarse();
	if ((bdb->bdb_dump_ts + DMA_INFO_DUMP_INTVL) > cur)
		return;

	bdb->bdb_dump_ts = cur;
	D_EMIT("DMA buffer isn't sufficient to sustain current workload, "
	       "enlarge the nr_hugepages in server YAML if possible.\n");

	D_EMIT("chk_size:%u, tot_chk:%u/%u, active_iods:%u, queued_iods:%u, used:%u,%u,%u\n",
	       bio_chk_sz, bdb->bdb_tot_cnt, bio_chk_cnt_max, bdb->bdb_active_iods,
	       bdb->bdb_queued_iods, bdb->bdb_used_cnt[BIO_CHK_TYPE_IO],
	       bdb->bdb_used_cnt[BIO_CHK_TYPE_LOCAL], bdb->bdb_used_cnt[BIO_CHK_TYPE_REBUILD]);

	/* cached bulk info */
	for (i = 0; i < bbc->bbc_grp_cnt; i++) {
		bbg = &bbc->bbc_grps[i];

		if (bbg->bbg_chk_cnt == 0)
			continue;

		bulk_grps++;
		bulk_chunks += bbg->bbg_chk_cnt;

		D_EMIT("bulk_grp %d: bulk_size:%u, chunks:%u\n",
		       i, bbg->bbg_bulk_pgs, bbg->bbg_chk_cnt);
	}
	D_EMIT("bulk_grps:%d, bulk_chunks:%d\n", bulk_grps, bulk_chunks);
}

static int
iod_map_iovs(struct bio_desc *biod, void *arg)
{
	struct bio_dma_buffer	*bdb;
	int			 rc, retry_cnt = 0;

	/* NVMe context isn't allocated */
	if (biod->bd_ctxt->bic_xs_ctxt == NULL)
		bdb = NULL;
	else
		bdb = iod_dma_buf(biod);

	iod_fifo_in(biod, bdb);
retry:
	rc = iterate_biov(biod, arg ? bulk_map_one : dma_map_one, arg);
	if (rc) {
		/*
		 * To avoid deadlock, held buffers need be released
		 * before waiting for other active IODs.
		 */
		iod_release_buffer(biod);

		if (!biod->bd_retry)
			goto out;

		D_ASSERT(bdb != NULL);
		if (bdb->bdb_stats.bds_grab_errs)
			d_tm_inc_counter(bdb->bdb_stats.bds_grab_errs, 1);
		dump_dma_info(bdb);

		biod->bd_retry = 0;
		if (!iod_should_retry(biod, bdb)) {
			D_ERROR("Per-xstream DMA buffer isn't large enough "
				"to satisfy large IOD %p\n", biod);
			goto out;
		}

		if (biod->bd_non_blocking) {
			rc = -DER_AGAIN;
			goto out;
		}

		retry_cnt++;
		D_DEBUG(DB_IO, "IOD %p waits for active IODs. %d\n", biod, retry_cnt);

		iod_fifo_wait(biod, bdb);

		D_DEBUG(DB_IO, "IOD %p finished waiting. %d\n", biod, retry_cnt);

		goto retry;
	}

	biod->bd_buffer_prep = 1;
	if (retry_cnt && bdb->bdb_stats.bds_grab_retries)
		d_tm_set_gauge(bdb->bdb_stats.bds_grab_retries, retry_cnt);
out:
	iod_fifo_out(biod, bdb);
	return rc;
}

int
iod_prep_internal(struct bio_desc *biod, unsigned int type, void *bulk_ctxt,
		  unsigned int bulk_perm)
{
	struct bio_bulk_args	 bulk_arg;
	struct bio_dma_buffer	*bdb;
	void			*arg = NULL;
	int			 rc;

	if (biod->bd_buffer_prep)
		return -DER_INVAL;

	biod->bd_chk_type = type;
	/* For rebuild pull, the DMA buffer will be used as RDMA client */
	biod->bd_rdma = (bulk_ctxt != NULL) || (type == BIO_CHK_TYPE_REBUILD);

	if (bulk_ctxt != NULL && !(daos_io_bypass & IOBP_SRV_BULK_CACHE)) {
		bulk_arg.ba_bulk_ctxt = bulk_ctxt;
		bulk_arg.ba_bulk_perm = bulk_perm;
		bulk_arg.ba_sgl_idx = 0;
		arg = &bulk_arg;
	}

	rc = iod_map_iovs(biod, arg);
	if (rc)
		return rc;

	/* All direct SCM access, no DMA buffer prepared */
	if (biod->bd_rsrvd.brd_rg_cnt == 0)
		return 0;

	bdb = iod_dma_buf(biod);
	bdb->bdb_active_iods++;
	if (bdb->bdb_stats.bds_active_iods)
		d_tm_set_gauge(bdb->bdb_stats.bds_active_iods, bdb->bdb_active_iods);

	if (biod->bd_type < BIO_IOD_TYPE_GETBUF) {
		rc = ABT_eventual_create(0, &biod->bd_dma_done);
		if (rc != ABT_SUCCESS) {
			rc = -DER_NOMEM;
			goto failed;
		}
	}

	biod->bd_dma_issued = 0;
	biod->bd_inflights = 0;
	biod->bd_result = 0;

	/* Load data from media to buffer on read */
	if (biod->bd_type == BIO_IOD_TYPE_FETCH)
		dma_rw(biod);

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
bio_iod_prep(struct bio_desc *biod, unsigned int type, void *bulk_ctxt,
	     unsigned int bulk_perm)
{
	return iod_prep_internal(biod, type, bulk_ctxt, bulk_perm);
}

int
bio_iod_try_prep(struct bio_desc *biod, unsigned int type, void *bulk_ctxt,
		 unsigned int bulk_perm)
{
	if (biod->bd_type == BIO_IOD_TYPE_FETCH)
		return -DER_NOTSUPPORTED;

	biod->bd_non_blocking = 1;
	return iod_prep_internal(biod, type, bulk_ctxt, bulk_perm);
}

int
bio_iod_post(struct bio_desc *biod, int err)
{
	biod->bd_dma_issued = 0;
	biod->bd_inflights = 0;
	biod->bd_result = err;

	if (!biod->bd_buffer_prep) {
		biod->bd_result = -DER_INVAL;
		goto out;
	}

	/* No more actions for direct accessed SCM IOVs */
	if (biod->bd_rsrvd.brd_rg_cnt == 0) {
		iod_release_buffer(biod);
		goto out;
	}

	/* Land data from buffer to media on write */
	if (err == 0 && biod->bd_type == BIO_IOD_TYPE_UPDATE)
		dma_rw(biod);

	if (biod->bd_inflights == 0) {
		if (biod->bd_buffer_prep) {
			iod_release_buffer(biod);
			dma_drop_iod(iod_dma_buf(biod));
		}
	}
out:
	if (!biod->bd_dma_issued && biod->bd_type == BIO_IOD_TYPE_UPDATE)
		iod_dma_completion(biod, biod->bd_result);
	return biod->bd_result;
}

int
bio_iod_post_async(struct bio_desc *biod, int err)
{
	/* Async post is for UPDATE only */
	if (biod->bd_type != BIO_IOD_TYPE_UPDATE)
		goto out;

	/* Async post is only for MD on SSD */
	if (!bio_nvme_configured(SMD_DEV_TYPE_META))
		goto out;
	/*
	 * When the value on data blob is too large, don't do async post to
	 * avoid calculating checksum for large values.
	 */
	if (biod->bd_nvme_bytes > bio_max_async_sz)
		goto out;

	biod->bd_async_post = 1;
out:
	return bio_iod_post(biod, err);
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
flush_one(struct bio_desc *biod, struct bio_iov *biov, void *arg)
{
	struct umem_instance *umem = biod->bd_umem;

	D_ASSERT(arg == NULL);
	D_ASSERT(biov);
	D_ASSERT(umem != NULL);

	if (bio_iov2req_len(biov) == 0)
		return 0;

	if (bio_addr_is_hole(&biov->bi_addr))
		return 0;

	if (!direct_scm_access(biod, biov))
		return 0;

	D_ASSERT(bio_iov2raw_buf(biov) != NULL);
	umem_atomic_flush(umem, bio_iov2req_buf(biov),
		      bio_iov2req_len(biov));
	return 0;
}

void
bio_iod_flush(struct bio_desc *biod)
{
	D_ASSERT(biod->bd_buffer_prep);
	if (biod->bd_type == BIO_IOD_TYPE_UPDATE)
		iterate_biov(biod, flush_one, NULL);
}

/* To keep the passed in @bsgl_in intact, copy it to the bsgl attached in bio_desc */
static struct bio_sglist *
iod_dup_sgl(struct bio_desc *biod, struct bio_sglist *bsgl_in)
{
	struct bio_sglist	*bsgl;
	int			 i, rc;

	bsgl = bio_iod_sgl(biod, 0);

	rc = bio_sgl_init(bsgl, bsgl_in->bs_nr);
	if (rc)
		return NULL;

	for (i = 0; i < bsgl->bs_nr; i++) {
		D_ASSERT(bio_iov2req_buf(&bsgl_in->bs_iovs[i]) == NULL);
		D_ASSERT(bio_iov2req_len(&bsgl_in->bs_iovs[i]) != 0);
		bsgl->bs_iovs[i] = bsgl_in->bs_iovs[i];
	}
	bsgl->bs_nr_out = bsgl->bs_nr;

	return bsgl;
}

static int
bio_rwv(struct bio_io_context *ioctxt, struct bio_sglist *bsgl_in,
	d_sg_list_t *sgl, bool update)
{
	struct bio_sglist	*bsgl;
	struct bio_desc		*biod;
	int			 i, rc;

	for (i = 0; i < bsgl_in->bs_nr; i++) {
		if (bio_addr_is_hole(&bsgl_in->bs_iovs[i].bi_addr)) {
			D_ERROR("Read/write a hole isn't allowed\n");
			return -DER_INVAL;
		}
	}

	/* allocate blob I/O descriptor */
	biod = bio_iod_alloc(ioctxt, NULL, 1 /* single bsgl */,
			     update ? BIO_IOD_TYPE_UPDATE : BIO_IOD_TYPE_FETCH);
	if (biod == NULL)
		return -DER_NOMEM;

	bsgl = iod_dup_sgl(biod, bsgl_in);
	if (bsgl == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	/* map the biov to DMA safe buffer, fill DMA buffer if read operation */
	rc = bio_iod_prep(biod, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc)
		goto out;

	for (i = 0; i < bsgl->bs_nr; i++)
		D_ASSERT(bio_iov2raw_buf(&bsgl->bs_iovs[i]) != NULL);

	rc = bio_iod_copy(biod, sgl, 1 /* single sgl */);
	if (rc)
		D_ERROR("Copy biod failed, "DF_RC"\n", DP_RC(rc));

	/* release DMA buffer, write data back to NVMe device for write */
	rc = bio_iod_post(biod, rc);

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

struct bio_desc *
bio_buf_alloc(struct bio_io_context *ioctxt, unsigned int len, void *bulk_ctxt,
	      unsigned int bulk_perm)
{
	struct bio_sglist	*bsgl;
	struct bio_desc		*biod;
	unsigned int		 chk_type;
	int			 rc;

	biod = bio_iod_alloc(ioctxt, NULL, 1, BIO_IOD_TYPE_GETBUF);
	if (biod == NULL)
		return NULL;

	bsgl = bio_iod_sgl(biod, 0);
	rc = bio_sgl_init(bsgl, 1);
	if (rc)
		goto error;

	D_ASSERT(len > 0);
	bio_iov_set_len(&bsgl->bs_iovs[0], len);
	bsgl->bs_nr_out = bsgl->bs_nr;

	chk_type = (bulk_ctxt != NULL) ? BIO_CHK_TYPE_IO : BIO_CHK_TYPE_LOCAL;
	rc = bio_iod_prep(biod, chk_type, bulk_ctxt, bulk_perm);
	if (rc)
		goto error;

	return biod;
error:
	bio_iod_free(biod);
	return NULL;
}

void
bio_buf_free(struct bio_desc *biod)
{
	D_ASSERT(biod != NULL);
	D_ASSERT(biod->bd_type == BIO_IOD_TYPE_GETBUF);
	bio_iod_post(biod, 0);
	bio_iod_free(biod);
}

void *
bio_buf_bulk(struct bio_desc *biod, unsigned int *bulk_off)
{
	D_ASSERT(biod != NULL);
	D_ASSERT(biod->bd_type == BIO_IOD_TYPE_GETBUF);
	D_ASSERT(biod->bd_buffer_prep);

	return bio_iod_bulk(biod, 0, 0, bulk_off);
}

void *
bio_buf_addr(struct bio_desc *biod)
{
	struct bio_sglist	*bsgl;

	D_ASSERT(biod != NULL);
	D_ASSERT(biod->bd_type == BIO_IOD_TYPE_GETBUF);
	D_ASSERT(biod->bd_buffer_prep);

	bsgl = bio_iod_sgl(biod, 0);
	return bio_iov2buf(&bsgl->bs_iovs[0]);
}

struct bio_copy_desc {
	struct bio_desc		*bcd_iod_src;
	struct bio_desc		*bcd_iod_dst;
};

static void
free_copy_desc(struct bio_copy_desc *copy_desc)
{
	if (copy_desc->bcd_iod_src)
		bio_iod_free(copy_desc->bcd_iod_src);
	if (copy_desc->bcd_iod_dst)
		bio_iod_free(copy_desc->bcd_iod_dst);
	D_FREE(copy_desc);
}

static struct bio_copy_desc *
alloc_copy_desc(struct bio_io_context *ioctxt, struct umem_instance *umem,
		struct bio_sglist *bsgl_src, struct bio_sglist *bsgl_dst)
{
	struct bio_copy_desc	*copy_desc;
	struct bio_sglist	*bsgl_read, *bsgl_write;

	D_ALLOC_PTR(copy_desc);
	if (copy_desc == NULL)
		return NULL;

	copy_desc->bcd_iod_src = bio_iod_alloc(ioctxt, umem, 1, BIO_IOD_TYPE_FETCH);
	if (copy_desc->bcd_iod_src == NULL)
		goto free;

	copy_desc->bcd_iod_dst = bio_iod_alloc(ioctxt, umem, 1, BIO_IOD_TYPE_UPDATE);
	if (copy_desc->bcd_iod_dst == NULL)
		goto free;

	bsgl_read = iod_dup_sgl(copy_desc->bcd_iod_src, bsgl_src);
	if (bsgl_read == NULL)
		goto free;

	bsgl_write = iod_dup_sgl(copy_desc->bcd_iod_dst, bsgl_dst);
	if (bsgl_write == NULL)
		goto free;

	return copy_desc;
free:
	free_copy_desc(copy_desc);
	return NULL;
}

int
bio_copy_prep(struct bio_io_context *ioctxt, struct umem_instance *umem,
	      struct bio_sglist *bsgl_src, struct bio_sglist *bsgl_dst,
	      struct bio_copy_desc **desc)
{
	struct bio_copy_desc	*copy_desc;
	int			 rc, ret;

	copy_desc = alloc_copy_desc(ioctxt, umem, bsgl_src, bsgl_dst);
	if (copy_desc == NULL) {
		*desc = NULL;
		return -DER_NOMEM;
	}

	rc = bio_iod_prep(copy_desc->bcd_iod_src, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc)
		goto free;

	copy_desc->bcd_iod_dst->bd_copy_dst = 1;
	rc = bio_iod_prep(copy_desc->bcd_iod_dst, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc) {
		ret = bio_iod_post(copy_desc->bcd_iod_src, 0);
		D_ASSERT(ret == 0);
		goto free;
	}

	*desc = copy_desc;
	return 0;
free:
	free_copy_desc(copy_desc);
	*desc = NULL;
	return rc;
}

int
bio_copy_run(struct bio_copy_desc *copy_desc, unsigned int copy_size,
	     struct bio_csum_desc *csum_desc)
{
	struct bio_sglist	*bsgl_src;
	d_sg_list_t		 sgl_src;
	struct bio_copy_args	 arg = { 0 };
	int			 rc;

	/* TODO: Leverage DML or SPDK accel APIs to support cusm generation */
	if (csum_desc != NULL)
		return -DER_NOSYS;

	bsgl_src = bio_iod_sgl(copy_desc->bcd_iod_src, 0);
	rc = bio_sgl_convert(bsgl_src, &sgl_src);
	if (rc)
		return rc;

	arg.ca_sgls = &sgl_src;
	arg.ca_sgl_cnt = 1;
	arg.ca_size_tot = copy_size;
	rc = iterate_biov(copy_desc->bcd_iod_dst, copy_one, &arg);
	if (rc > 0)	/* Abort on reaching specified copy size */
		rc = 0;

	d_sgl_fini(&sgl_src, false);
	return rc;
}

int
bio_copy_post(struct bio_copy_desc *copy_desc, int err)
{
	int	rc;

	rc = bio_iod_post(copy_desc->bcd_iod_src, 0);
	D_ASSERT(rc == 0);
	rc = bio_iod_post(copy_desc->bcd_iod_dst, err);

	free_copy_desc(copy_desc);

	return rc;
}

struct bio_sglist *
bio_copy_get_sgl(struct bio_copy_desc *copy_desc, bool src)
{
	struct bio_desc	*biod;

	biod = src ? copy_desc->bcd_iod_src : copy_desc->bcd_iod_dst;
	D_ASSERT(biod != NULL);

	return bio_iod_sgl(biod, 0);
}

int
bio_copy(struct bio_io_context *ioctxt, struct umem_instance *umem,
	 struct bio_sglist *bsgl_src, struct bio_sglist *bsgl_dst,
	 unsigned int copy_size, struct bio_csum_desc *csum_desc)
{
	struct bio_copy_desc	*copy_desc;
	int			 rc;

	rc = bio_copy_prep(ioctxt, umem, bsgl_src, bsgl_dst, &copy_desc);
	if (rc)
		return rc;

	rc = bio_copy_run(copy_desc, copy_size, csum_desc);
	rc = bio_copy_post(copy_desc, rc);

	return rc;
}
