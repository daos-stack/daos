/**
 * (C) Copyright 2018 Intel Corporation.
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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(eio)

#include <spdk/env.h>
#include <spdk/blob.h>
#include "eio_internal.h"

static void
dma_buffer_shrink(struct eio_dma_buffer *buf, unsigned int cnt)
{
	struct eio_dma_chunk *chunk, *tmp;

	d_list_for_each_entry_safe(chunk, tmp, &buf->edb_idle_list, edc_link) {
		if (cnt == 0)
			break;

		d_list_del_init(&chunk->edc_link);

		D_ASSERT(chunk->edc_ptr != NULL);
		D_ASSERT(chunk->edc_pg_idx == 0);
		D_ASSERT(chunk->edc_ref == 0);

		spdk_dma_free(chunk->edc_ptr);
		D_FREE_PTR(chunk);

		D_ASSERT(buf->edb_tot_cnt > 0);
		buf->edb_tot_cnt--;
		cnt--;
	}
}

static int
dma_buffer_grow(struct eio_dma_buffer *buf, unsigned int cnt)
{
	struct eio_dma_chunk *chunk;
	ssize_t chk_bytes = eio_chk_sz << EIO_DMA_PAGE_SHIFT;
	int i, rc = 0;

	if ((buf->edb_tot_cnt + cnt) > eio_chk_cnt_max) {
		D_ERROR("Exceeding per-xstream DMA buffer size\n");
		return -DER_OVERFLOW;
	}

	for (i = 0; i < cnt; i++) {
		D_ALLOC_PTR(chunk);
		if (chunk == NULL) {
			D_ERROR("Failed to allocate chunk\n");
			rc = -DER_NOMEM;
			break;
		}

		chunk->edc_ptr = spdk_dma_malloc(chk_bytes, EIO_DMA_PAGE_SZ,
						 NULL);
		if (chunk->edc_ptr == NULL) {
			D_ERROR("Failed to allocate DMA buffer\n");
			D_FREE_PTR(chunk);
			rc = -DER_NOMEM;
			break;
		}

		d_list_add_tail(&chunk->edc_link, &buf->edb_idle_list);
		buf->edb_tot_cnt++;
	}

	return rc;
}

void
dma_buffer_destroy(struct eio_dma_buffer *buf)
{
	D_ASSERT(d_list_empty(&buf->edb_used_list));
	D_ASSERT(buf->edb_active_iods == 0);
	dma_buffer_shrink(buf, buf->edb_tot_cnt);

	D_ASSERT(buf->edb_tot_cnt == 0);
	buf->edb_cur_chk = NULL;
	ABT_mutex_free(&buf->edb_mutex);
	ABT_cond_free(&buf->edb_wait_iods);

	D_FREE_PTR(buf);
}

struct eio_dma_buffer *
dma_buffer_create(unsigned int init_cnt)
{
	struct eio_dma_buffer *buf;
	int rc;

	D_ALLOC_PTR(buf);
	if (buf == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&buf->edb_idle_list);
	D_INIT_LIST_HEAD(&buf->edb_used_list);
	buf->edb_cur_chk = NULL;
	buf->edb_tot_cnt = 0;
	buf->edb_active_iods = 0;

	rc = ABT_mutex_create(&buf->edb_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE_PTR(buf);
		return NULL;
	}

	rc = ABT_cond_create(&buf->edb_wait_iods);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&buf->edb_mutex);
		D_FREE_PTR(buf);
		return NULL;
	}

	rc = dma_buffer_grow(buf, init_cnt);
	if (rc != 0) {
		dma_buffer_destroy(buf);
		return NULL;
	}

	return buf;
}

struct eio_sglist *
eio_iod_sgl(struct eio_desc *eiod, unsigned int idx)
{
	if (idx >= eiod->ed_sgl_cnt) {
		D_ERROR("Invalid sgl index %d/%d\n", idx, eiod->ed_sgl_cnt);
		return NULL;
	}
	return &eiod->ed_sgls[idx];
}

struct eio_desc *
eio_iod_alloc(struct eio_io_context *ctxt, unsigned int sgl_cnt, bool update)
{
	struct eio_desc *eiod;
	int rc;

	D_ASSERT(ctxt != NULL && ctxt->eic_umem != NULL);
	D_ASSERT(sgl_cnt != 0);

	D_ALLOC_PTR(eiod);
	if (eiod == NULL)
		return NULL;

	eiod->ed_ctxt = ctxt;
	eiod->ed_update = update;
	eiod->ed_sgl_cnt = sgl_cnt;

	D_ALLOC(eiod->ed_sgls, sizeof(*eiod->ed_sgls) * sgl_cnt);
	if (eiod->ed_sgls == NULL) {
		D_FREE_PTR(eiod);
		return NULL;
	}

	rc = ABT_mutex_create(&eiod->ed_mutex);
	if (rc != ABT_SUCCESS)
		goto free_sgls;

	rc = ABT_cond_create(&eiod->ed_dma_done);
	if (rc != ABT_SUCCESS)
		goto free_mutex;

	return eiod;

free_mutex:
	ABT_mutex_free(&eiod->ed_mutex);
free_sgls:
	D_FREE(eiod->ed_sgls);
	D_FREE_PTR(eiod);
	return NULL;
}

void
eio_iod_free(struct eio_desc *eiod)
{
	int i;

	D_ASSERT(!eiod->ed_buffer_prep);

	ABT_cond_free(&eiod->ed_dma_done);
	ABT_mutex_free(&eiod->ed_mutex);

	for (i = 0; i < eiod->ed_sgl_cnt; i++)
		eio_sgl_fini(&eiod->ed_sgls[i]);
	D_FREE(eiod->ed_sgls);
	D_FREE_PTR(eiod);
}

static inline struct eio_dma_buffer *
iod_dma_buf(struct eio_desc *eiod)
{
	D_ASSERT(eiod->ed_ctxt->eic_xs_ctxt);
	D_ASSERT(eiod->ed_ctxt->eic_xs_ctxt->exc_dma_buf);

	return eiod->ed_ctxt->eic_xs_ctxt->exc_dma_buf;
}

/*
 * Release all the DMA chunks held by @eiod, once the use count of any
 * chunk drops to zero, put it back to free list.
 */
static void
iod_release_buffer(struct eio_desc *eiod)
{
	struct eio_dma_buffer *edb;
	struct eio_rsrvd_dma *rsrvd_dma = &eiod->ed_rsrvd;
	int i;

	if (rsrvd_dma->erd_chk_max == 0) {
		D_ASSERT(rsrvd_dma->erd_rg_max == 0);
		eiod->ed_buffer_prep = 0;
		return;
	}

	D_ASSERT(rsrvd_dma->erd_regions != NULL);
	D_FREE(rsrvd_dma->erd_regions);
	rsrvd_dma->erd_regions = NULL;
	rsrvd_dma->erd_rg_max = rsrvd_dma->erd_rg_cnt = 0;

	edb = iod_dma_buf(eiod);
	D_ASSERT(rsrvd_dma->erd_dma_chks != NULL);
	for (i = 0; i < rsrvd_dma->erd_chk_cnt; i++) {
		struct eio_dma_chunk *chunk = rsrvd_dma->erd_dma_chks[i];

		D_ASSERT(chunk != NULL);
		D_ASSERT(!d_list_empty(&chunk->edc_link));
		D_ASSERT(chunk->edc_ref > 0);
		chunk->edc_ref--;

		D_DEBUG(DB_IO, "Release chunk:%p[%p] idx:%u ref:%u\n", chunk,
			chunk->edc_ptr, chunk->edc_pg_idx, chunk->edc_ref);

		if (chunk->edc_ref == 0) {
			chunk->edc_pg_idx = 0;
			if (chunk == edb->edb_cur_chk)
				edb->edb_cur_chk = NULL;
			d_list_move_tail(&chunk->edc_link, &edb->edb_idle_list);
		}
		rsrvd_dma->erd_dma_chks[i] = NULL;
	}

	D_FREE(rsrvd_dma->erd_dma_chks);
	rsrvd_dma->erd_dma_chks = NULL;
	rsrvd_dma->erd_chk_max = rsrvd_dma->erd_chk_cnt = 0;

	eiod->ed_buffer_prep = 0;
}

struct eio_copy_args {
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
iterate_eiov(struct eio_desc *eiod,
	     int (*cb_fn)(struct eio_desc *, struct eio_iov *,
			  struct eio_copy_args *),
	     struct eio_copy_args *arg)
{
	int i, j, rc = 0;

	for (i = 0; i < eiod->ed_sgl_cnt; i++) {
		struct eio_sglist *esgl = &eiod->ed_sgls[i];

		if (arg != NULL) {
			D_ASSERT(i < arg->ca_sgl_cnt);
			arg->ca_sgl_idx = i;
			arg->ca_iov_idx = 0;
			arg->ca_iov_off = 0;
			if (!eiod->ed_update)
				arg->ca_sgls[i].sg_nr_out = 0;
		}

		if (esgl->es_nr_out == 0)
			continue;

		for (j = 0; j < esgl->es_nr_out; j++) {
			struct eio_iov *eiov = &esgl->es_iovs[j];

			if (eiov->ei_data_len == 0)
				continue;

			rc = cb_fn(eiod, eiov, arg);
			if (rc)
				break;
		}
	}

	return rc;
}

static void *
chunk_reserve(struct eio_dma_chunk *chk, unsigned int chk_pg_idx,
	      unsigned int pg_cnt, unsigned int pg_off)
{
	D_ASSERT(chk != NULL);
	D_ASSERTF(chk->edc_pg_idx <= eio_chk_sz, "%u > %u\n",
		  chk->edc_pg_idx, eio_chk_sz);

	D_ASSERTF(chk_pg_idx == chk->edc_pg_idx ||
		  (chk_pg_idx + 1) == chk->edc_pg_idx, "%u, %u\n",
		  chk_pg_idx, chk->edc_pg_idx);

	/* The chunk doesn't have enough unused pages */
	if (chk_pg_idx + pg_cnt > eio_chk_sz)
		return NULL;

	D_DEBUG(DB_IO, "Reserved on chunk:%p[%p], idx:%u, cnt:%u, off:%u\n",
		chk, chk->edc_ptr, chk_pg_idx, pg_cnt, pg_off);

	chk->edc_pg_idx = chk_pg_idx + pg_cnt;
	return chk->edc_ptr + (chk_pg_idx << EIO_DMA_PAGE_SHIFT) + pg_off;
}

static inline struct eio_rsrvd_region *
iod_last_region(struct eio_desc *eiod)
{
	unsigned int cnt = eiod->ed_rsrvd.erd_rg_cnt;

	D_ASSERT(!cnt || cnt < eiod->ed_rsrvd.erd_rg_max);
	return (cnt != 0) ? &eiod->ed_rsrvd.erd_regions[cnt - 1] : NULL;
}

static struct eio_dma_chunk *
chunk_get_idle(struct eio_dma_buffer *edb, struct eio_desc *eiod)
{
	struct eio_dma_chunk *chk;
	int rc;

	if (d_list_empty(&edb->edb_idle_list)) {
		if (edb->edb_tot_cnt == eio_chk_cnt_max) {
			D_CRIT("Maximum per-xstream DMA buffer isn't big "
			       "enough (chk_sz:%u chk_cnt:%u iods:%u) to "
			       "sustain the workload.\n", eio_chk_sz,
			       eio_chk_cnt_max, edb->edb_active_iods);

			eiod->ed_retry = 1;
			return NULL;
		}

		rc = dma_buffer_grow(edb, 1);
		if (rc != 0)
			return NULL;
	}

	D_ASSERT(!d_list_empty(&edb->edb_idle_list));
	chk = d_list_entry(edb->edb_idle_list.next, struct eio_dma_chunk,
			   edc_link);
	d_list_move_tail(&chk->edc_link, &edb->edb_used_list);

	return chk;
}

static int
iod_add_chunk(struct eio_desc *eiod, struct eio_dma_chunk *chk)
{
	struct eio_rsrvd_dma *rsrvd_dma = &eiod->ed_rsrvd;
	unsigned int max, cnt;

	max = rsrvd_dma->erd_chk_max;
	cnt = rsrvd_dma->erd_chk_cnt;

	if (cnt == max) {
		struct eio_dma_chunk **chunks;
		int size = sizeof(struct eio_dma_chunk *);
		unsigned new_cnt = cnt + 10;

		D_ALLOC(chunks, new_cnt * size);
		if (chunks == NULL)
			return -DER_NOMEM;

		if (max != 0) {
			memcpy(chunks, rsrvd_dma->erd_dma_chks, max * size);
			D_FREE(rsrvd_dma->erd_dma_chks);
		}

		rsrvd_dma->erd_dma_chks = chunks;
		rsrvd_dma->erd_chk_max = new_cnt;
	}

	chk->edc_ref++;
	rsrvd_dma->erd_dma_chks[cnt] = chk;
	rsrvd_dma->erd_chk_cnt++;
	return 0;
}

static int
iod_add_region(struct eio_desc *eiod, struct eio_dma_chunk *chk,
	       unsigned int chk_pg_idx, uint64_t off, uint64_t end)
{
	struct eio_rsrvd_dma *rsrvd_dma = &eiod->ed_rsrvd;
	unsigned int max, cnt;

	max = rsrvd_dma->erd_rg_max;
	cnt = rsrvd_dma->erd_rg_cnt;

	if (cnt == max) {
		struct eio_rsrvd_region *rgs;
		int size = sizeof(struct eio_rsrvd_region);
		unsigned new_cnt = cnt + 20;

		D_ALLOC(rgs, new_cnt * size);
		if (rgs == NULL)
			return -DER_NOMEM;

		if (max != 0) {
			memcpy(rgs, rsrvd_dma->erd_regions, max * size);
			D_FREE(rsrvd_dma->erd_regions);
		}

		rsrvd_dma->erd_regions = rgs;
		rsrvd_dma->erd_rg_max = new_cnt;
	}

	rsrvd_dma->erd_regions[cnt].err_chk = chk;
	rsrvd_dma->erd_regions[cnt].err_pg_idx = chk_pg_idx;
	rsrvd_dma->erd_regions[cnt].err_off = off;
	rsrvd_dma->erd_regions[cnt].err_end = end;
	rsrvd_dma->erd_rg_cnt++;
	return 0;
}

/* Convert offset of @eiov into memory pointer */
static int
dma_map_one(struct eio_desc *eiod, struct eio_iov *eiov,
	    struct eio_copy_args *arg)
{
	struct eio_rsrvd_region *last_rg;
	struct eio_dma_buffer *edb;
	struct eio_dma_chunk *chk = NULL;
	uint64_t off, end;
	unsigned int pg_cnt, pg_off, chk_pg_idx;
	int rc;

	D_ASSERT(arg == NULL);
	D_ASSERT(eiov && eiov->ei_data_len != 0);

	if (eio_addr_is_hole(&eiov->ei_addr)) {
		eiov->ei_buf = NULL;
		return 0;
	}

	if (eiov->ei_addr.ea_type == EIO_ADDR_SCM) {
		struct umem_instance *umem = eiod->ed_ctxt->eic_umem;
		umem_id_t ummid;

		ummid.pool_uuid_lo = eiod->ed_ctxt->eic_pmempool_uuid;
		ummid.off = eio_iov2off(eiov);

		eiov->ei_buf = umem_id2ptr(umem, ummid);
		return 0;
	}

	D_ASSERT(eiov->ei_addr.ea_type == EIO_ADDR_NVME);
	edb = iod_dma_buf(eiod);

	off = eio_iov2off(eiov);
	end = eio_iov2off(eiov) + eiov->ei_data_len;
	pg_cnt = ((end + EIO_DMA_PAGE_SZ - 1) >> EIO_DMA_PAGE_SHIFT) -
			(off >> EIO_DMA_PAGE_SHIFT);
	pg_off = off & ((uint64_t)EIO_DMA_PAGE_SZ - 1);

	if (pg_cnt > eio_chk_sz) {
		D_ERROR("IOV is too large "DF_U64"\n", eiov->ei_data_len);
		return -DER_OVERFLOW;
	}

	last_rg = iod_last_region(eiod);

	/* First, try consecutive reserve from the last reserved region */
	if (last_rg) {
		uint64_t cur_pg, prev_pg_start, prev_pg_end;

		D_DEBUG(DB_IO, "Last region %p:%d ["DF_U64","DF_U64")\n",
			last_rg->err_chk, last_rg->err_pg_idx,
			last_rg->err_off, last_rg->err_end);

		chk = last_rg->err_chk;
		chk_pg_idx = last_rg->err_pg_idx;
		D_ASSERT(chk_pg_idx < eio_chk_sz);

		prev_pg_start = last_rg->err_off >> EIO_DMA_PAGE_SHIFT;
		prev_pg_end = last_rg->err_end >> EIO_DMA_PAGE_SHIFT;
		cur_pg = off >> EIO_DMA_PAGE_SHIFT;
		D_ASSERT(prev_pg_start <= prev_pg_end);

		/* Consecutive in page */
		if (cur_pg == prev_pg_end) {
			chk_pg_idx += (prev_pg_end - prev_pg_start);
			eiov->ei_buf = chunk_reserve(chk, chk_pg_idx, pg_cnt,
						     pg_off);
			if (eiov->ei_buf != NULL) {
				D_DEBUG(DB_IO, "Consecutive reserve %p.\n",
					eiov->ei_buf);
				last_rg->err_end = end;
				return 0;
			}
		}
	}

	/* Try to reserve from the last DMA chunk in io descriptor */
	if (chk != NULL) {
		chk_pg_idx = chk->edc_pg_idx;
		eiov->ei_buf = chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off);
		if (eiov->ei_buf != NULL) {
			D_DEBUG(DB_IO, "Last chunk reserve %p.\n",
				eiov->ei_buf);
			goto add_region;
		}
	}

	/*
	 * Try to reserve the DMA buffer from the 'current chunk' of the
	 * per-xstream DMA buffer. It could be different with the last chunk
	 * in io descripotr, because dma_map_one() may yield in the future.
	 */
	if (edb->edb_cur_chk != NULL && edb->edb_cur_chk != chk) {
		chk = edb->edb_cur_chk;
		chk_pg_idx = chk->edc_pg_idx;
		eiov->ei_buf = chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off);
		if (eiov->ei_buf != NULL) {
			D_DEBUG(DB_IO, "Current chunk reserve %p.\n",
				eiov->ei_buf);
			goto add_chunk;
		}
	}

	/*
	 * Switch to another idle chunk, if there isn't any idle chunk
	 * available, grow buffer.
	 */
	chk = chunk_get_idle(edb, eiod);
	if (chk == NULL)
		return -DER_OVERFLOW;

	edb->edb_cur_chk = chk;
	chk_pg_idx = chk->edc_pg_idx;

	D_ASSERT(chk_pg_idx == 0);
	eiov->ei_buf = chunk_reserve(chk, chk_pg_idx, pg_cnt, pg_off);
	if (eiov->ei_buf != NULL) {
		D_DEBUG(DB_IO, "New chunk reserve %p.\n", eiov->ei_buf);
		goto add_chunk;
	}

	return -DER_OVERFLOW;

add_chunk:
	rc = iod_add_chunk(eiod, chk);
	if (rc)
		return rc;
add_region:
	return iod_add_region(eiod, chk, chk_pg_idx, off, end);
}

static void
rw_completion(void *cb_arg, int err)
{
	struct eio_desc *eiod = cb_arg;

	ABT_mutex_lock(eiod->ed_mutex);

	D_ASSERT(eiod->ed_inflights > 0);
	eiod->ed_inflights--;
	if (eiod->ed_result == 0 && err != 0)
		eiod->ed_result = err;

	if (eiod->ed_inflights == 0 && eiod->ed_dma_issued)
		ABT_cond_broadcast(eiod->ed_dma_done);

	ABT_mutex_unlock(eiod->ed_mutex);
}

static void
dma_rw(struct eio_desc *eiod, bool prep)
{
	struct spdk_io_channel	*channel;
	struct spdk_blob	*blob;
	struct eio_rsrvd_dma	*rsrvd_dma = &eiod->ed_rsrvd;
	struct eio_rsrvd_region	*rg;
	struct eio_xs_context	*xs_ctxt;
	uint64_t		 pg_idx, pg_cnt, pg_end;
	void			*payload, *pg_rmw = NULL;
	bool			 rmw_read = (prep && eiod->ed_update);
	unsigned int		 pg_off;
	int			 i;

	D_ASSERT(eiod->ed_ctxt->eic_xs_ctxt);
	xs_ctxt = eiod->ed_ctxt->eic_xs_ctxt;
	blob = eiod->ed_ctxt->eic_blob;
	channel = xs_ctxt->exc_io_channel;
	D_ASSERT(blob != NULL && channel != NULL);

	D_DEBUG(DB_IO, "DMA start, blob:%p, update:%d, rmw:%d\n",
		blob, eiod->ed_update, rmw_read);

	eiod->ed_inflights = 0;
	eiod->ed_dma_issued = 0;
	eiod->ed_result = 0;

	for (i = 0; i < rsrvd_dma->erd_rg_cnt; i++) {
		rg = &rsrvd_dma->erd_regions[i];

		D_ASSERT(rg->err_chk != NULL);
		pg_idx = rg->err_off >> EIO_DMA_PAGE_SHIFT;
		payload = rg->err_chk->edc_ptr +
			(rg->err_pg_idx << EIO_DMA_PAGE_SHIFT);

		if (!rmw_read) {
			pg_cnt = (rg->err_end + EIO_DMA_PAGE_SZ - 1) >>
					EIO_DMA_PAGE_SHIFT;
			D_ASSERT(pg_cnt > pg_idx);
			pg_cnt -= pg_idx;

			ABT_mutex_lock(eiod->ed_mutex);
			eiod->ed_inflights++;
			ABT_mutex_unlock(eiod->ed_mutex);

			D_DEBUG(DB_IO, "%s blob:%p payload:%p, "
				"pg_idx:"DF_U64", pg_cnt:"DF_U64"\n",
				eiod->ed_update ? "Write" : "Read",
				blob, payload, pg_idx, pg_cnt);

			if (eiod->ed_update)
				spdk_blob_io_write(blob, channel, payload,
						   pg_idx, pg_cnt,
						   rw_completion, eiod);
			else
				spdk_blob_io_read(blob, channel, payload,
						  pg_idx, pg_cnt,
						  rw_completion, eiod);
			continue;
		}

		/*
		 * Since DAOS doesn't support partial overwrite yet, we don't
		 * do RMW for partial update, only zeroing the page instead.
		 */
		pg_off = rg->err_off & ((uint64_t)EIO_DMA_PAGE_SZ - 1);

		if (pg_off != 0 && payload != pg_rmw) {
			D_DEBUG(DB_IO, "Front partial blob:%p payload:%p, "
				"pg_idx:"DF_U64" pg_off:%d\n",
				blob, payload, pg_idx, pg_off);

			memset(payload, 0, EIO_DMA_PAGE_SZ);
			pg_rmw = payload;
		}

		pg_end = rg->err_end >> EIO_DMA_PAGE_SHIFT;
		D_ASSERT(pg_end >= pg_idx);
		payload += (pg_end - pg_idx) << EIO_DMA_PAGE_SHIFT;
		pg_off = rg->err_end & ((uint64_t)EIO_DMA_PAGE_SZ - 1);

		if (pg_off != 0 && payload != pg_rmw) {
			D_DEBUG(DB_IO, "Rear partial blob:%p payload:%p, "
				"pg_idx:"DF_U64" pg_off:%d\n",
				blob, payload, pg_idx, pg_off);

			memset(payload, 0, EIO_DMA_PAGE_SZ);
			pg_rmw = payload;
		}
	}

	if (xs_ctxt->exc_xs_id == -1) {
		D_DEBUG(DB_IO, "Self poll completion, blob:%p\n", blob);
		xs_poll_completion(xs_ctxt, &eiod->ed_inflights);
	} else {
		ABT_mutex_lock(eiod->ed_mutex);
		eiod->ed_dma_issued = 1;
		if (eiod->ed_inflights != 0)
			ABT_cond_wait(eiod->ed_dma_done, eiod->ed_mutex);
		ABT_mutex_unlock(eiod->ed_mutex);
	}

	D_DEBUG(DB_IO, "DMA done, blob:%p, update:%d, rmw:%d\n",
		blob, eiod->ed_update, rmw_read);
}

static void
eio_memcpy(struct eio_desc *eiod, uint16_t media, void *media_addr,
	   void *addr, ssize_t n)
{
	struct umem_instance *umem = eiod->ed_ctxt->eic_umem;

	if (eiod->ed_update && media == EIO_ADDR_SCM) {
		pmemobj_memcpy_persist(umem->umm_u.pmem_pool, media_addr,
				       addr, n);
	} else {
		if (eiod->ed_update)
			memcpy(media_addr, addr, n);
		else
			memcpy(addr, media_addr, n);
	}
}

static int
copy_one(struct eio_desc *eiod, struct eio_iov *eiov,
	 struct eio_copy_args *arg)
{
	d_sg_list_t *sgl;
	void *addr = eiov->ei_buf;
	ssize_t size = eiov->ei_data_len;
	uint16_t media = eiov->ei_addr.ea_type;

	D_ASSERT(arg->ca_sgl_idx < arg->ca_sgl_cnt);
	sgl = &arg->ca_sgls[arg->ca_sgl_idx];

	D_ASSERT(arg->ca_iov_idx < sgl->sg_nr);
	while (arg->ca_iov_idx < sgl->sg_nr) {
		d_iov_t *iov;
		ssize_t nob, buf_len;

		iov = &sgl->sg_iovs[arg->ca_iov_idx];
		buf_len = eiod->ed_update ? iov->iov_len : iov->iov_buf_len;

		if (buf_len <= arg->ca_iov_off) {
			D_ERROR("Invalid iov[%d] "DF_U64"/"DF_U64" %d\n",
				arg->ca_iov_idx, arg->ca_iov_off,
				buf_len, eiod->ed_update);
			return -DER_INVAL;
		}

		nob = min(size, buf_len - arg->ca_iov_off);
		if (addr != NULL) {
			D_DEBUG(DB_IO, "eio copy %p size %zd\n",
				addr, nob);
			eio_memcpy(eiod, media, addr, iov->iov_buf +
					arg->ca_iov_off, nob);
			addr += nob;
		} else {
			/* fetch on hole */
			D_ASSERT(!eiod->ed_update);
		}

		arg->ca_iov_off += nob;
		if (!eiod->ed_update) {
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
	return -DER_INVAL;
}

static void
dma_drop_iod(struct eio_dma_buffer *edb)
{
	D_ASSERT(edb->edb_active_iods > 0);
	edb->edb_active_iods--;

	ABT_mutex_lock(edb->edb_mutex);
	ABT_cond_broadcast(edb->edb_wait_iods);
	ABT_mutex_unlock(edb->edb_mutex);
}

int
eio_iod_prep(struct eio_desc *eiod)
{
	struct eio_dma_buffer *edb;
	int rc, retry_cnt = 0;

	if (eiod->ed_buffer_prep)
		return -EINVAL;

retry:
	rc = iterate_eiov(eiod, dma_map_one, NULL);
	if (rc) {
		/*
		 * To avoid deadlock, held buffers need be released
		 * before waiting for other active IODs.
		 */
		iod_release_buffer(eiod);

		if (!eiod->ed_retry)
			return rc;

		eiod->ed_retry = 0;
		edb = iod_dma_buf(eiod);
		if (!edb->edb_active_iods) {
			D_ERROR("Per-xstream DMA buffer isn't large enough "
				"to satisfy large IOD %p\n", eiod);
			return rc;
		}

		D_DEBUG(DB_IO, "IOD %p waits for active IODs. %d\n",
			eiod, retry_cnt++);

		ABT_mutex_lock(edb->edb_mutex);
		ABT_cond_wait(edb->edb_wait_iods, edb->edb_mutex);
		ABT_mutex_unlock(edb->edb_mutex);

		D_DEBUG(DB_IO, "IOD %p finished waiting. %d\n",
			eiod, retry_cnt);

		goto retry;
	}
	eiod->ed_buffer_prep = 1;

	/* All SCM IOVs, no DMA transfer prepared */
	if (eiod->ed_rsrvd.erd_rg_cnt == 0)
		return 0;

	edb = iod_dma_buf(eiod);
	edb->edb_active_iods++;

	dma_rw(eiod, true);
	if (eiod->ed_result) {
		iod_release_buffer(eiod);
		dma_drop_iod(edb);
	}

	return eiod->ed_result;
}

int
eio_iod_post(struct eio_desc *eiod)
{
	struct eio_dma_buffer *edb;

	if (!eiod->ed_buffer_prep)
		return -DER_INVAL;

	/* No more actions for SCM IOVs */
	if (eiod->ed_rsrvd.erd_rg_cnt == 0) {
		iod_release_buffer(eiod);
		return 0;
	}

	if (eiod->ed_update)
		dma_rw(eiod, false);
	else
		eiod->ed_result = 0;

	iod_release_buffer(eiod);
	edb = iod_dma_buf(eiod);
	dma_drop_iod(edb);

	return eiod->ed_result;
}

int
eio_iod_copy(struct eio_desc *eiod, d_sg_list_t *sgls, unsigned int nr_sgl)
{
	struct eio_copy_args arg;

	if (!eiod->ed_buffer_prep)
		return -DER_INVAL;

	if (eiod->ed_sgl_cnt != nr_sgl)
		return -DER_INVAL;

	memset(&arg, 0, sizeof(arg));
	arg.ca_sgls = sgls;
	arg.ca_sgl_cnt = nr_sgl;

	return iterate_eiov(eiod, copy_one, &arg);
}
