/**
 * (C) Copyright 2015-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include "tx.h"

#define	DAV_HEAP_INIT	0x1
#define MEGABYTE	((uintptr_t)1 << 20)

static bool
is_zone_evictable(void *arg, uint32_t zid)
{
	struct dav_obj *hdl = (struct dav_obj *)arg;

	return heap_mbrt_ismb_evictable(hdl->do_heap, zid);
}

static int
dav_uc_callback(int evt_type, void *arg, uint32_t zid)
{
	struct dav_obj *hdl = (struct dav_obj *)arg;
	struct zone    *z   = ZID_TO_ZONE(&hdl->do_heap->layout_info, zid);

	switch (evt_type) {
	case UMEM_CACHE_EVENT_PGLOAD:
		if (hdl->do_booted) {
			VALGRIND_DO_CREATE_MEMPOOL(z, 0, 0);
#if VG_MEMCHECK_ENABLED
			if (On_memcheck)
				palloc_heap_vg_zone_open(hdl->do_heap, zid, 1);
#endif
			D_ASSERT(z->header.flags & ZONE_EVICTABLE_MB);
			heap_mbrt_setmb_usage(hdl->do_heap, zid, z->header.sp_usage);
		}
		break;
	case UMEM_CACHE_EVENT_PGEVICT:
		if (hdl->do_booted) {
			VALGRIND_DO_DESTROY_MEMPOOL_COND(z);
		}
		break;
	default:
		D_ERROR("Unknown umem cache event type in callback");
	}
	return 0;
}

static dav_obj_t *
dav_obj_open_internal(int fd, int flags, size_t scm_sz, const char *path, struct umem_store *store)
{
	dav_obj_t              *hdl = NULL;
	void                   *mmap_base;
	int                     err = 0;
	int                     rc;
	struct heap_zone_limits hzl;
	struct zone            *z0;

	hzl = heap_get_zone_limits(store->stor_size, scm_sz, 100);

	if (hzl.nzones_heap == 0) {
		ERR("Insufficient heap size.");
		errno = EINVAL;
		return NULL;
	}

	if ((hzl.nzones_cache <= UMEM_CACHE_MIN_PAGES) && (hzl.nzones_heap > hzl.nzones_cache)) {
		ERR("Insufficient scm size.");
		errno = EINVAL;
		return NULL;
	}

	if (hzl.nzones_cache * ZONE_MAX_SIZE != scm_sz)
		D_WARN("scm size %lu is not aligned to zone size %lu, some scm will be unused",
		       scm_sz, ZONE_MAX_SIZE);

	if (hzl.nzones_heap < hzl.nzones_cache)
		D_WARN("scm size %lu exceeds metablob size %lu, some scm will be unused", scm_sz,
		       store->stor_size);

	mmap_base = mmap(NULL, scm_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mmap_base == MAP_FAILED)
		return NULL;

	D_ALIGNED_ALLOC(hdl, CACHELINE_SIZE, sizeof(dav_obj_t));
	if (hdl == NULL) {
		err = ENOMEM;
		goto out0;
	}

	hdl->do_fd = fd;
	hdl->do_base            = mmap_base;
	hdl->do_size_mem        = scm_sz;
	hdl->do_size_mem_usable = hzl.nzones_cache * ZONE_MAX_SIZE;
	hdl->do_size_meta       = store->stor_size;
	hdl->p_ops.base         = hdl;
	hdl->do_store           = store;
	hdl->p_ops.umem_store   = store;

	if (hdl->do_store->stor_priv == NULL) {
		D_ERROR("Missing backing store for the heap");
		err = EINVAL;
		goto out1;
	}

	if (flags & DAV_HEAP_INIT) {
		rc = heap_init(mmap_base, scm_sz, store);
		if (rc) {
			err = errno;
			goto out1;
		}
	}

	D_STRNDUP(hdl->do_path, path, strlen(path));
	D_ALLOC_PTR(hdl->do_heap);
	if (hdl->do_heap == NULL) {
		err = ENOMEM;
		goto out2;
	}

	hdl->do_stats = stats_new(hdl);
	if (hdl->do_stats == NULL)
		goto out2;

	rc = heap_boot(hdl->do_heap, hdl->do_base, hdl->do_store->stor_size, scm_sz, &hdl->p_ops,
		       hdl->do_stats);
	if (rc) {
		err = rc;
		goto out2;
	}

	heap_set_root_ptrs(hdl->do_heap, &hdl->do_root_offsetp, &hdl->do_root_sizep);
	heap_set_stats_ptr(hdl->do_heap, &hdl->do_stats->persistent);

	rc = umem_cache_alloc(store, ZONE_MAX_SIZE, hzl.nzones_heap, hzl.nzones_cache,
			      heap_get_max_nemb(hdl->do_heap), 4096, mmap_base, is_zone_evictable,
			      dav_uc_callback, hdl);
	if (rc != 0) {
		D_ERROR("Could not allocate page cache: rc=" DF_RC "\n", DP_RC(rc));
		err = daos_der2errno(rc);
		goto out3;
	}

	if (!(flags & DAV_HEAP_INIT)) {
		rc = heap_zone_load(hdl->do_heap, 0);
		if (rc) {
			err = rc;
			goto out4;
		}
		D_ASSERT(store != NULL);
		rc = hdl->do_store->stor_ops->so_wal_replay(hdl->do_store, dav_wal_replay_cb, hdl);
		if (rc) {
			err = daos_der2errno(rc);
			goto out4;
		}
	}

	rc = dav_create_clogs(hdl);
	if (rc) {
		err = rc;
		goto out4;
	}

	rc = lw_tx_begin(hdl);
	if (rc) {
		D_ERROR("lw_tx_begin failed with err %d\n", rc);
		err = ENOMEM;
		goto out5;
	}
	rc = heap_ensure_zone0_initialized(hdl->do_heap);
	if (rc) {
		lw_tx_end(hdl, NULL);
		D_ERROR("Failed to initialize zone0, rc = %d", daos_errno2der(rc));
		err = rc;
		goto out5;
	}
	lw_tx_end(hdl, NULL);

	z0 = ZID_TO_ZONE(&hdl->do_heap->layout_info, 0);
	if (z0->header.zone0_zinfo_off) {
		D_ASSERT(z0->header.zone0_zinfo_size);
		D_ASSERT(OFFSET_TO_ZID(z0->header.zone0_zinfo_off) == 0);

		rc = heap_update_mbrt_zinfo(hdl->do_heap, false);
		if (rc) {
			D_ERROR("Failed to update mbrt with zinfo errno = %d", rc);
			err = rc;
			goto out5;
		}

		rc = heap_load_nonevictable_zones(hdl->do_heap);
		if (rc) {
			D_ERROR("Failed to load required zones during boot, errno= %d", rc);
			err = rc;
			goto out5;
		}
	} else {
		D_ASSERT(z0->header.zone0_zinfo_size == 0);
		rc = lw_tx_begin(hdl);
		if (rc) {
			D_ERROR("lw_tx_begin failed with err %d\n", rc);
			err = ENOMEM;
			goto out5;
		}
		rc = obj_realloc(hdl, &z0->header.zone0_zinfo_off, &z0->header.zone0_zinfo_size,
				 heap_zinfo_get_size(hzl.nzones_heap));
		if (rc != 0) {
			lw_tx_end(hdl, NULL);
			D_ERROR("Failed to setup zinfo");
			goto out5;
		}
		rc = heap_update_mbrt_zinfo(hdl->do_heap, true);
		if (rc) {
			D_ERROR("Failed to update mbrt with zinfo errno = %d", rc);
			err = rc;
			goto out5;
		}
		lw_tx_end(hdl, NULL);
	}
	umem_cache_post_replay(hdl->do_store);

#if VG_MEMCHECK_ENABLED
	if (On_memcheck)
		palloc_heap_vg_open(hdl->do_heap, 1);
#endif

	hdl->do_booted = 1;

	return hdl;
out5:
	dav_destroy_clogs(hdl);
out4:
	umem_cache_free(hdl->do_store);
out3:
	heap_cleanup(hdl->do_heap);
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
out1:
	D_FREE(hdl);
out0:
	munmap(mmap_base, scm_sz);
	errno = err;
	return NULL;

}

DAV_FUNC_EXPORT dav_obj_t *
dav_obj_create_v2(const char *path, int flags, size_t sz, mode_t mode, struct umem_store *store)
{
	int fd;
	dav_obj_t *hdl;
	struct stat statbuf;
	int         create = 0;

	SUPPRESS_UNUSED(flags);

	if (sz == 0) {
		/* Open the file and obtain the size */
		fd = open(path, O_RDWR|O_CLOEXEC);
		if (fd == -1) {
			DS_ERROR(errno, "obj_create_v2 open %s to fetch size", path);
			return NULL;
		}

		if (fstat(fd, &statbuf) != 0)
			goto out;
		sz = statbuf.st_size;
	} else {
		fd = open(path, O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC, mode);
		if (fd == -1) {
			DS_ERROR(errno, "obj_create_v2 open %s to alloc", path);
			return NULL;
		}

		if (fallocate(fd, 0, 0, (off_t)sz) == -1) {
			errno = ENOSPC;
			goto out;
		}
		create = 1;
	}

	hdl = dav_obj_open_internal(fd, DAV_HEAP_INIT, sz, path, store);
	if (hdl == NULL)
		goto out;

	DAV_DBG("pool %s created, size="DF_U64"", hdl->do_path, sz);
	return hdl;

out:
	close(fd);
	if (create)
		unlink(path);
	return NULL;
}

DAV_FUNC_EXPORT dav_obj_t *
dav_obj_open_v2(const char *path, int flags, struct umem_store *store)
{
	size_t size;
	int fd;
	dav_obj_t *hdl;
	struct stat statbuf;

	SUPPRESS_UNUSED(flags);

	fd = open(path, O_RDWR|O_CLOEXEC);
	if (fd == -1) {
		DS_ERROR(errno, "obj_create_v2 open %s", path);
		return NULL;
	}

	if (fstat(fd, &statbuf) != 0) {
		close(fd);
		return NULL;
	}
	size = (size_t)statbuf.st_size;

	hdl = dav_obj_open_internal(fd, 0, size, path, store);
	if (hdl == NULL) {
		close(fd);
		return NULL;
	}
	DAV_DBG("pool %s is open, size="DF_U64"", hdl->do_path, size);
	return hdl;
}

DAV_FUNC_EXPORT void
dav_obj_close_v2(dav_obj_t *hdl)
{

	if (hdl == NULL) {
		ERR("NULL handle");
		return;
	}
	dav_destroy_clogs(hdl);
	heap_cleanup(hdl->do_heap);
	D_FREE(hdl->do_heap);

	stats_delete(hdl, hdl->do_stats);

	munmap(hdl->do_base, hdl->do_size_mem);
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

DAV_FUNC_EXPORT void *
dav_get_base_ptr_v2(dav_obj_t *hdl)
{
	return hdl->do_heap->layout_info.zone0;
}

DAV_FUNC_EXPORT int
dav_class_register_v2(dav_obj_t *pop, struct dav_alloc_class_desc *p)
{
	uint8_t                        id        = (uint8_t)p->class_id;
	struct alloc_class_collection *ac = heap_alloc_classes(pop->do_heap);
	enum header_type               lib_htype = MAX_HEADER_TYPES;
	size_t                         runsize_bytes;
	uint32_t                       size_idx;
	struct alloc_class            *c;

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

	runsize_bytes = CHUNKSIZE;
	while (((p->units_per_block * p->unit_size) + RUN_BASE_METADATA_SIZE) > runsize_bytes)
		runsize_bytes += CHUNKSIZE;

	/* aligning the buffer might require up-to to 'alignment' bytes */
	if (p->alignment != 0)
		runsize_bytes += p->alignment;

	size_idx = (uint32_t)(runsize_bytes / CHUNKSIZE);

	if (size_idx > MAX_CHUNK)
		size_idx = MAX_CHUNK;

	c = alloc_class_new(id, heap_alloc_classes(pop->do_heap), CLASS_RUN, lib_htype,
			    p->unit_size, p->alignment, size_idx);
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

DAV_FUNC_EXPORT size_t
dav_obj_pgsz_v2()
{
	return ZONE_MAX_SIZE;
}
