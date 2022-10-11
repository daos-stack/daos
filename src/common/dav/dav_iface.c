/**
 * (C) Copyright 2015-2022 Intel Corporation.
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

#include "dav_internal.h"
#include "heap.h"
#include "palloc.h"
#include "pmemops.h"
#include "obj.h"

#define	DAV_HEAP_INIT	0x1
#define MEGABYTE	((uintptr_t)1 << 20)

/*
 * pmemobj_get_uuid_lo -- (internal) evaluates XOR sum of least significant
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
	pmemops_persist(&hdl->p_ops, hdl->do_phdr, offsetof(struct dav_phdr, dp_unused));
}

static dav_obj_t *
dav_obj_open_internal(int fd, int flags, size_t sz)
{
	int rc;
	dav_obj_t *hdl = NULL;
	void *base;
	char *heap_base;
	uint64_t heap_size;
	int persist_hdr = 0;

	base = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) {
		close(fd);
		return NULL;
	}

	rc = posix_memalign((void **)&hdl, 64, sizeof(dav_obj_t));
	if (rc != 0) {
		errno = ENOMEM;
		goto out1;
	}
	memset(hdl, 0, sizeof(dav_obj_t));

	hdl->do_fd = fd;
	hdl->do_base = base;
	hdl->do_size = sz;

	if (flags & DAV_HEAP_INIT) {
		setup_dav_phdr(hdl);
		heap_base = (char *)hdl->do_base + hdl->do_phdr->dp_heap_offset;
		heap_size = hdl->do_phdr->dp_heap_size;
		rc = heap_init(heap_base, heap_size, &hdl->do_phdr->dp_heap_size, NULL);
		if (rc) {
			errno = rc;
			goto out2;
		}
		persist_hdr = 1;
	} else {
		hdl->do_phdr = hdl->do_base;
		heap_base = (char *)hdl->do_base + hdl->do_phdr->dp_heap_offset;
		heap_size = hdl->do_phdr->dp_heap_size;
	}

	hdl->do_stats = stats_new(hdl);
	if (hdl->do_stats == NULL)
		goto out2;

	hdl->do_heap = Zalloc(sizeof(struct palloc_heap));
	if (hdl->do_heap == NULL) {
		errno = ENOMEM;
		goto out2;
	}

	hdl->p_ops.base = hdl;
	/* REVISIT: code for stats
	 */
	rc = heap_boot(hdl->do_heap, heap_base, heap_size,
		&hdl->do_phdr->dp_heap_size, hdl->do_base,
		&hdl->p_ops, hdl->do_stats, NULL);

	if (rc) {
		errno = rc;
		goto out2;
	}

	rc = heap_buckets_init(hdl->do_heap);
	if (rc) {
		heap_cleanup(hdl->do_heap);
		goto out2;
	}

	rc = dav_create_clogs(hdl);
	if (rc) {
		heap_cleanup(hdl->do_heap);
		goto out2;
	}

	if (persist_hdr)
		persist_dav_phdr(hdl);

	return hdl;

out2:
	if (hdl->do_stats)
		stats_delete(hdl, hdl->do_stats);
	if (hdl->do_path)
		Free(hdl->do_path);
	if (hdl->do_heap)
		Free(hdl->do_heap);
	Free(hdl);
out1:
	munmap(base, sz);
	return NULL;

}

dav_obj_t *
dav_obj_create(const char *path, int flags, size_t sz, mode_t mode)
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
		/* REVISIT: May have to do additional validation */
	} else {
		fd = open(path, O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC, mode);
		if (fd == -1)
			return NULL;

		if (ftruncate(fd, (off_t)sz) == -1) {
			close(fd);
			errno = ENOSPC;
			return NULL;
		}
	}

	hdl = dav_obj_open_internal(fd, DAV_HEAP_INIT, sz);
	if (hdl == NULL) {
		close(fd);
		return NULL;
	}
	hdl->do_path = strdup(path);
	return hdl;
}

dav_obj_t *
dav_obj_open(const char *path, int flags)
{
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

	hdl = dav_obj_open_internal(fd, 0, (size_t)statbuf.st_size);
	if (hdl == NULL) {
		close(fd);
		return NULL;
	}
	hdl->do_path = strdup(path);
	return hdl;
}

void
dav_obj_close(dav_obj_t *hdl)
{
	dav_destroy_clogs(hdl);

	stats_delete(hdl, hdl->do_stats);
	heap_cleanup(hdl->do_heap);
	Free(hdl->do_heap);
	munmap(hdl->do_base, hdl->do_size);
	close(hdl->do_fd);
	Free(hdl->do_path);
	Free(hdl);
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

	if (p->class_id < 0 || p->class_id >= MAX_ALLOCATION_CLASSES) {
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

