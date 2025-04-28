/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2024, Intel Corporation */

/*
 * meta_io.h -- definitions of statistics
 */

#ifndef __DAOS_COMMON_META_IO_H
#define __DAOS_COMMON_META_IO_H 1

#include <daos_types.h>

struct umem_store;
/*
 * meta_clear_pages - fill zeros at various offsets in the meta blob.
 */
int
meta_clear_pages(struct umem_store *store, daos_off_t start_off, daos_size_t size,
		 daos_size_t hop_dist, int cnt);

/*
 * meta_update -- Write size bytes from addr src to meta blob at offset off.
 */
int
meta_update(struct umem_store *store, void *src, daos_off_t off, daos_size_t size);

/*
 * meta_fetch -- Fetch size bytes from offset off in the meta blob to addr dest.
 */
int
meta_fetch(struct umem_store *store, void *dest, daos_off_t off, daos_size_t size);

/*
 * meta_fetch_batch -- Fetch nelems of elem_size bytes starting from metablob offset
 * start_off and hop distance of hop_dist to the buffer dest.
 */
int
meta_fetch_batch(struct umem_store *store, void *dest, daos_off_t start_off, daos_size_t elem_size,
		 daos_size_t hop_dist, int nelems);

#endif /* __DAOS_COMMON_META_IO_H */
