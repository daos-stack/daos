/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <uuid/uuid.h>

#include <daos/mem.h>
#include "dav_internal.h"
#include "heap.h"
#include "palloc.h"
#include "mo_wal.h"
#include "obj.h"

#define	DAV_HEAP_INIT	0x1
#define MEGABYTE	((uintptr_t)1 << 20)

/*
 * get_uuid_lo -- (internal) evaluates XOR sum of least significant
 * 8 bytes with most significant 8 bytes.
 */
static inline uint64_t
get_uuid_lo(uuid_t uuid)
{
	uint64_t uuid_lo = 0;

	for (int i = 0; i < 8; i++)
		uuid_lo = (uuid_lo << 8) | (uuid[i] ^ uuid[8 + i]);

	return uuid_lo;
}

static void
setup_dav_phdr(dav_obj_t *hdl)
{
	struct dav_phdr *hptr;
	uuid_t	uuid;

	ASSERT(hdl->do_base != NULL);
	hptr = (struct dav_phdr *)(hdl->do_base);
	uuid_generate(uuid);
	hptr->dp_uuid_lo = get_uuid_lo(uuid);
	hptr->dp_root_offset = 0;
	hptr->dp_root_size = 0;
	hptr->dp_heap_offset = sizeof(struct dav_phdr);
	hptr->dp_heap_size = hdl->do_size - sizeof(struct dav_phdr);
	hptr->dp_stats_persistent.heap_curr_allocated = 0;
	hdl->do_phdr = hptr;
}

static void
persist_dav_phdr(dav_obj_t *hdl)
{
	mo_wal_persist(&hdl->p_ops, hdl->do_phdr, offsetof(struct dav_phdr, dp_unused));
}

static dav_obj_t *
dav_obj_open_internal(int fd, int flags, size_t sz, const char *path, struct umem_store *store)
{
	dav_obj_t *hdl = NULL;
	void      *base;
	char      *heap_base;
	uint64_t   heap_size;
	uint64_t   num_pages;
	int        persist_hdr = 0;
	int        err         = 0;
	int        rc;

	base = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) {
		return NULL;
	}

	D_ALIGNED_ALLOC(hdl, CACHELINE_SIZE, sizeof(dav_obj_t));
	if (hdl == NULL) {
		err = ENOMEM;
		goto out0;
	}

	/* REVISIT: In future pass the meta instance as argument instead of fd */
	hdl->do_fd = fd;
	hdl->do_base = base;
	hdl->do_size = sz;
	hdl->p_ops.base = hdl;

	hdl->do_store = store;
	if (hdl->do_store->stor_priv == NULL) {
		D_ERROR("meta context not defined. WAL commit disabled for %s\n", path);
	} else {
		rc = umem_cache_alloc(store, 0);
		if (rc != 0) {
			D_ERROR("Could not allocate page cache: rc=" DF_RC "\n", DP_RC(rc));
			err = rc;
			goto out1;
		}
	}

	D_STRNDUP(hdl->do_path, path, strlen(path));

	num_pages = (sz + UMEM_CACHE_PAGE_SZ - 1) >> UMEM_CACHE_PAGE_SZ_SHIFT;
	rc = umem_cache_map_range(hdl->do_store, 0, base, num_pages);
	if (rc != 0) {
		D_ERROR("Could not allocate page cache: rc=" DF_RC "\n", DP_RC(rc));
		err = rc;
		goto out2;
	}

	if (flags & DAV_HEAP_INIT) {
		setup_dav_phdr(hdl);
		heap_base = (char *)hdl->do_base + hdl->do_phdr->dp_heap_offset;
		heap_size = hdl->do_phdr->dp_heap_size;

		rc = lw_tx_begin(hdl);
		if (rc) {
			err = ENOMEM;
			goto out2;
		}

		rc = heap_init(heap_base, heap_size, &hdl->do_phdr->dp_heap_size,
			       &hdl->p_ops);
		if (rc) {
			err = rc;
			goto out2;
		}
		persist_hdr = 1;
	} else {
		hdl->do_phdr = hdl->do_base;

		D_ASSERT(store != NULL);

		rc = store->stor_ops->so_load(store, hdl->do_base);
		if (rc) {
			D_ERROR("Failed to read blob to vos file %s, rc = %d\n", path, rc);
			goto out2;
		}

		rc = hdl->do_store->stor_ops->so_wal_replay(hdl->do_store, dav_wal_replay_cb, hdl);
		if (rc) {
			err = rc;
			goto out2;
		}

		heap_base = (char *)hdl->do_base + hdl->do_phdr->dp_heap_offset;
		heap_size = hdl->do_phdr->dp_heap_size;

		rc = lw_tx_begin(hdl);
		if (rc) {
			err = ENOMEM;
			goto out2;
		}
	}

	hdl->do_stats = stats_new(hdl);
	if (hdl->do_stats == NULL)
		goto out2;

	D_ALLOC_PTR(hdl->do_heap);
	if (hdl->do_heap == NULL) {
		err = ENOMEM;
		goto out2;
	}

	rc = heap_boot(hdl->do_heap, heap_base, heap_size,
		&hdl->do_phdr->dp_heap_size, hdl->do_base,
		&hdl->p_ops, hdl->do_stats, NULL);
	if (rc) {
		err = rc;
		goto out2;
	}

#if VG_MEMCHECK_ENABLED
	if (On_memcheck)
		palloc_heap_vg_open(hdl->do_heap, 1);
#endif

	rc = heap_buckets_init(hdl->do_heap);
	if (rc) {
		err = rc;
		heap_cleanup(hdl->do_heap);
		goto out2;
	}

	rc = dav_create_clogs(hdl);
	if (rc) {
		err = rc;
		heap_cleanup(hdl->do_heap);
		goto out2;
	}

	if (persist_hdr)
		persist_dav_phdr(hdl);

	lw_tx_end(hdl, NULL);

#if VG_MEMCHECK_ENABLED
	if (On_memcheck) {
		/* mark unused part of the pool as not accessible */
		void *end = palloc_heap_end(hdl->do_heap);

		VALGRIND_DO_MAKE_MEM_NOACCESS(end,
					      OBJ_OFF_TO_PTR(hdl, heap_size) - end);
	}
#endif
	return hdl;

out2:
	if (hdl->do_stats)
		stats_delete(hdl, hdl->do_stats);
	if (hdl->do_heap)
		D_FREE(hdl->do_heap);
	if (hdl->do_utx) {
		dav_umem_wtx_cleanup(hdl->do_utx);
		D_FREE(hdl->do_utx);
	}
	D_FREE(hdl->do_path);
	umem_cache_free(hdl->do_store);
out1:
	D_FREE(hdl);
out0:
	munmap(base, sz);
	errno = err;
	return NULL;

}

dav_obj_t *
dav_obj_create(const char *path, int flags, size_t sz, mode_t mode, struct umem_store *store)
{
	int fd;
	dav_obj_t *hdl;
	struct stat statbuf;

	SUPPRESS_UNUSED(flags);

	if (sz == 0) {
		/* Open the file and obtain the size */
		fd = open(path, O_RDWR|O_CLOEXEC);
		if (fd == -1)
			return NULL;

		if (fstat(fd, &statbuf) != 0) {
			close(fd);
			return NULL;
		}
		sz = statbuf.st_size;
	} else {
		fd = open(path, O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC, mode);
		if (fd == -1)
			return NULL;

		if (fallocate(fd, 0, 0, (off_t)sz) == -1) {
			close(fd);
			errno = ENOSPC;
			return NULL;
		}
	}

	if (!store->stor_size || (sz < store->stor_size)) {
		ERR("Invalid umem_store size");
		errno = EINVAL;
		close(fd);
		return NULL;
	}

	hdl = dav_obj_open_internal(fd, DAV_HEAP_INIT, store->stor_size, path, store);
	if (hdl == NULL) {
		close(fd);
		return NULL;
	}
	DAV_DBG("pool %s created, size="DF_U64"", hdl->do_path, sz);
	return hdl;
}

dav_obj_t *
dav_obj_open(const char *path, int flags, struct umem_store *store)
{
	size_t size;
	int fd;
	dav_obj_t *hdl;
	struct stat statbuf;

	SUPPRESS_UNUSED(flags);

	fd = open(path, O_RDWR|O_CLOEXEC);
	if (fd == -1)
		return NULL;

	if (fstat(fd, &statbuf) != 0) {
		close(fd);
		return NULL;
	}
	size = (size_t)statbuf.st_size;

	if (!store->stor_size || (size < store->stor_size)) {
		ERR("Invalid umem_store size");
		errno = EINVAL;
		close(fd);
		return NULL;
	}

	hdl = dav_obj_open_internal(fd, 0, store->stor_size, path, store);
	if (hdl == NULL) {
		close(fd);
		return NULL;
	}
	DAV_DBG("pool %s is open, size="DF_U64"", hdl->do_path, size);
	return hdl;
}

void
dav_obj_close(dav_obj_t *hdl)
{

	if (hdl == NULL) {
		ERR("NULL handle");
		return;
	}
	dav_destroy_clogs(hdl);
	heap_cleanup(hdl->do_heap);
	D_FREE(hdl->do_heap);

	stats_delete(hdl, hdl->do_stats);

	munmap(hdl->do_base, hdl->do_size);
	close(hdl->do_fd);
	if (hdl->do_utx) {
		dav_umem_wtx_cleanup(hdl->do_utx);
		D_FREE(hdl->do_utx);
	}
	umem_cache_free(hdl->do_store);
	DAV_DBG("pool %s is closed", hdl->do_path);
	D_FREE(hdl->do_path);
	D_FREE(hdl);
}

void *
dav_get_base_ptr(dav_obj_t *hdl)
{
	return hdl->do_base;
}

int
dav_class_register(dav_obj_t *pop, struct dav_alloc_class_desc *p)
{
	uint8_t id = (uint8_t)p->class_id;
	struct alloc_class_collection *ac = heap_alloc_classes(pop->do_heap);

	if (p->unit_size <= 0 || p->unit_size > DAV_MAX_ALLOC_SIZE ||
		p->units_per_block <= 0) {
		errno = EINVAL;
		return -1;
	}

	if (p->alignment != 0 && p->unit_size % p->alignment != 0) {
		ERR("unit size must be evenly divisible by alignment");
		errno = EINVAL;
		return -1;
	}

	if (p->alignment > (MEGABYTE * 2)) {
		ERR("alignment cannot be larger than 2 megabytes");
		errno = EINVAL;
		return -1;
	}

	if (p->class_id >= MAX_ALLOCATION_CLASSES) {
		ERR("class id outside of the allowed range");
		errno = ERANGE;
		return -1;
	}

	enum header_type lib_htype = MAX_HEADER_TYPES;

	switch (p->header_type) {
	case DAV_HEADER_LEGACY:
		lib_htype = HEADER_LEGACY;
		break;
	case DAV_HEADER_COMPACT:
		lib_htype = HEADER_COMPACT;
		break;
	case DAV_HEADER_NONE:
		lib_htype = HEADER_NONE;
		break;
	case MAX_DAV_HEADER_TYPES:
	default:
		ERR("invalid header type");
		errno = EINVAL;
		return -1;
	}

	if (id == 0) {
		if (alloc_class_find_first_free_slot(ac, &id) != 0) {
			ERR("no available free allocation class identifier");
			errno = EINVAL;
			return -1;
		}
	} else {
		if (alloc_class_reserve(ac, id) != 0) {
			ERR("attempted to overwrite an allocation class");
			errno = EEXIST;
			return -1;
		}
	}

	size_t runsize_bytes =
		CHUNK_ALIGN_UP((p->units_per_block * p->unit_size) +
		RUN_BASE_METADATA_SIZE);

	/* aligning the buffer might require up-to to 'alignment' bytes */
	if (p->alignment != 0)
		runsize_bytes += p->alignment;

	uint32_t size_idx = (uint32_t)(runsize_bytes / CHUNKSIZE);

	if (size_idx > UINT16_MAX)
		size_idx = UINT16_MAX;

	struct alloc_class *c = alloc_class_new(id,
		heap_alloc_classes(pop->do_heap), CLASS_RUN,
		lib_htype, p->unit_size, p->alignment, size_idx);
	if (c == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (heap_create_alloc_class_buckets(pop->do_heap, c) != 0) {
		alloc_class_delete(ac, c);
		return -1;
	}

	p->class_id = c->id;
	p->units_per_block = c->rdsc.nallocs;

	return 0;
}

