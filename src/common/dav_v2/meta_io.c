/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2017-2024, Intel Corporation */

/*
 * meta_io.c -- IO to/from meta blob bypassing WAL.
 */

#include <errno.h>
#include <daos/mem.h>

/** Maximum number of sets of pages in-flight at a time */
#define MAX_INFLIGHT_SETS 4

int
meta_clear_pages(struct umem_store *store, daos_off_t start_off, daos_size_t size,
		 daos_size_t hop_dist, int cnt)
{
	struct umem_store_iod    iod;
	struct umem_store_region iod_region[MAX_INFLIGHT_SETS];
	d_sg_list_t              sgl;
	d_iov_t                  sg_iov[MAX_INFLIGHT_SETS];
	char                    *src;
	int                      rc;
	int                      i;

	D_ASSERT((size % 4096) == 0);
	D_ASSERT(hop_dist != 0);

	D_ALLOC(src, size);
	if (src == NULL)
		return ENOMEM;

	sgl.sg_iovs = sg_iov;
	for (i = 0; i < MAX_INFLIGHT_SETS; i++)
		d_iov_set(&sg_iov[i], src, size);
	do {
		iod.io_nr     = (cnt > MAX_INFLIGHT_SETS) ? MAX_INFLIGHT_SETS : cnt;
		sgl.sg_nr     = iod.io_nr;
		sgl.sg_nr_out = iod.io_nr;

		for (i = 0; i < iod.io_nr; i++) {
			iod_region[i].sr_addr = start_off;
			iod_region[i].sr_size = size;
			start_off += hop_dist;
		}
		iod.io_regions = iod_region;

		rc = store->stor_ops->so_write(store, &iod, &sgl);
		D_ASSERT(rc == 0);

		cnt -= iod.io_nr;
	} while (cnt > 0);

	D_FREE(src);
	return 0;
}

/*
 * meta_update -- Write size bytes from addr src to meta blob at offset off.
 */
int
meta_update(struct umem_store *store, void *src, daos_off_t off, daos_size_t size)
{
	struct umem_store_iod iod;
	d_sg_list_t           sgl;
	d_iov_t               sg_iov;
	int                   rc;

	iod.io_nr             = 1;
	iod.io_region.sr_addr = off;
	iod.io_region.sr_size = size;
	iod.io_regions        = &iod.io_region;
	sgl.sg_nr             = 1;
	sgl.sg_nr_out         = 1;
	sgl.sg_iovs           = &sg_iov;
	d_iov_set(&sg_iov, src, size);

	D_ASSERT(store != NULL);
	if (store->stor_ops->so_write == NULL)
		return 0;

	rc = store->stor_ops->so_write(store, &iod, &sgl);
	if (rc != 0) {
		D_ERROR("Failed to write to meta at offset %lu, size %lu, rc = %d\n", off, size,
			rc);
		return EFAULT;
	}
	return 0;
}

/*
 * meta_fetch -- Fetch size bytes from offset off in the meta blob to addr dest.
 */
int
meta_fetch(struct umem_store *store, void *dest, daos_off_t off, daos_size_t size)
{
	struct umem_store_iod iod;
	d_sg_list_t           sgl;
	d_iov_t               sg_iov;
	int                   rc;

	iod.io_nr             = 1;
	iod.io_region.sr_addr = off;
	iod.io_region.sr_size = size;
	iod.io_regions        = &iod.io_region;
	sgl.sg_nr             = 1;
	sgl.sg_nr_out         = 1;
	sgl.sg_iovs           = &sg_iov;
	d_iov_set(&sg_iov, dest, size);

	D_ASSERT(store != NULL);
	if (store->stor_ops->so_write == NULL)
		return 0;

	rc = store->stor_ops->so_read(store, &iod, &sgl);
	if (rc != 0) {
		D_ERROR("Failed to read from meta at offset %lu, size %lu, rc = %d\n", off, size,
			rc);
		return EFAULT;
	}
	return 0;
}

/*
 * meta_fetch_batch -- Fetch nelems of elem_size bytes starting from metablob offset start_off and
 * hop distance of hop_dist to the buffer dest.
 */
int
meta_fetch_batch(struct umem_store *store, void *dest, daos_off_t start_off, daos_size_t elem_size,
		 daos_size_t hop_dist, int nelems)
{
	struct umem_store_iod    iod;
	struct umem_store_region iod_region[MAX_INFLIGHT_SETS];
	d_sg_list_t              sgl;
	d_iov_t                  sg_iov[MAX_INFLIGHT_SETS];
	int                      rc;
	int                      i;

	D_ASSERT((elem_size % 4096) == 0);
	D_ASSERT(hop_dist != 0);

	sgl.sg_iovs = sg_iov;
	while (nelems > 0) {
		iod.io_nr     = (nelems > MAX_INFLIGHT_SETS) ? MAX_INFLIGHT_SETS : nelems;
		sgl.sg_nr     = iod.io_nr;
		sgl.sg_nr_out = iod.io_nr;

		for (i = 0; i < iod.io_nr; i++) {
			d_iov_set(&sg_iov[i], dest, elem_size);
			iod_region[i].sr_addr = start_off;
			iod_region[i].sr_size = elem_size;
			start_off += hop_dist;
			dest += elem_size;
		}
		iod.io_regions = iod_region;

		rc = store->stor_ops->so_read(store, &iod, &sgl);
		if (rc)
			return -1;

		nelems -= iod.io_nr;
	}
	return 0;
}
