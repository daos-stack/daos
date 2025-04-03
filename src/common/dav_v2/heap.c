/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2024, Intel Corporation
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 */

/*
 * heap.c -- heap implementation
 */

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <float.h>
#include <sys/queue.h>

#include "bucket.h"
#include "dav_internal.h"
#include "memblock.h"
#include "queue.h"
#include "heap.h"
#include "out.h"
#include "util.h"
#include "sys_util.h"
#include "valgrind_internal.h"
#include "recycler.h"
#include "container.h"
#include "alloc_class.h"
#include "meta_io.h"

#define HEAP_NEMB_PCT_DEFAULT 80
#define HEAP_NEMB_EMPTY_THRESHOLD 16

#define MAX_RUN_LOCKS MAX_CHUNK
#define MAX_RUN_LOCKS_VG MAX_CHUNK /* avoid perf issues /w drd */

#define ZINFO_VERSION    0x1

struct zinfo_element {
	unsigned char z_allotted   : 1;
	unsigned char z_evictable  : 1;
	unsigned char z_usage_hint : 3;
};

struct zinfo_vec {
	uint32_t             version;
	uint32_t             num_elems;
	struct zinfo_element z[];
};

TAILQ_HEAD(mbrt_q, mbrt);

/*
 * Memory Bucket Runtime.
 */
struct mbrt {
	TAILQ_ENTRY(mbrt) mb_link;
	struct mbrt_q        *qptr;
	uint32_t              mb_id;
	uint32_t              garbage_reclaimed;
	uint64_t              space_usage;
	uint64_t              prev_usage;
	struct palloc        *heap;
	struct bucket_locked *default_bucket; /* bucket for free chunks */
	struct bucket_locked *buckets[MAX_ALLOCATION_CLASSES];
	struct recycler      *recyclers[MAX_ALLOCATION_CLASSES];
	bool                  laf[MAX_ALLOCATION_CLASSES]; /* last allocation failed? */
	bool                  laf_updated;
	bool                  is_global_mbrt;
	bool                  is_evictable;
};

enum mb_usage_hint {
	MB_U0_HINT   = 0,
	MB_U30_HINT  = 1,
	MB_U75_HINT  = 2,
	MB_U90_HINT  = 3,
	MB_UMAX_HINT = 4,
};

#define MB_U90         (ZONE_MAX_SIZE * 9 / 10)
#define MB_U75         (ZONE_MAX_SIZE * 75 / 100)
#define MB_U30         (ZONE_MAX_SIZE * 3 / 10)
#define MB_USAGE_DELTA (ZONE_MAX_SIZE / 20)

size_t mb_usage_byhint[MB_UMAX_HINT] = {1, MB_U30 + 1, MB_U75 + 1, MB_U90 + 1};

struct mbrt_qbs {
	struct mbrt_q mb_u90;
	struct mbrt_q mb_u75;
	struct mbrt_q mb_u30;
	struct mbrt_q mb_u0;
	struct mbrt_q mb_ue;
};

#define SOEMB_ACTIVE_CNT 3

struct soemb_rt {
	struct mbrt    *svec[SOEMB_ACTIVE_CNT];
	int             cur_idx;
	int             fur_idx;
	struct mbrt_qbs qbs;
};

struct heap_rt {
	struct alloc_class_collection *alloc_classes;
	pthread_mutex_t                run_locks[MAX_RUN_LOCKS];
	unsigned                       nlocks;
	unsigned                       nzones;
	unsigned                       nzones_e;
	unsigned                       nzones_ne;
	unsigned                       zones_exhausted;
	unsigned                       zones_exhausted_e;
	unsigned                       zones_exhausted_ne;
	unsigned                       zones_nextne_gc;
	unsigned                       zones_unused_first;
	unsigned                       zinfo_vec_size;
	unsigned                       mb_create_waiters;
	unsigned                       mb_pressure;
	unsigned                       nemb_pct;
	unsigned                       empty_nemb_cnt;
	unsigned                       empty_nemb_gcth;
	void                          *mb_create_wq;
	struct zinfo_vec              *zinfo_vec;
	struct mbrt                   *default_mb;
	struct mbrt                  **mbs;
	struct mbrt                   *active_evictable_mb;
	struct mbrt_qbs                emb_qbs;
	struct soemb_rt                smbrt;
	unsigned int                   soemb_cnt;
};

static void
heap_reclaim_zone_garbage(struct palloc_heap *heap, struct bucket *bucket, uint32_t zone_id);

static inline void
heap_zinfo_set(struct palloc_heap *heap, uint32_t zid, bool allotted, bool evictable)
{
	struct zinfo_element *ze;

	if (heap->rt->zinfo_vec) {
		ze                  = heap->rt->zinfo_vec->z;
		ze[zid].z_allotted  = allotted;
		ze[zid].z_evictable = evictable;
		mo_wal_persist(&heap->p_ops, &ze[zid], sizeof(ze[zid]));
	} else
		D_ASSERT(zid == 0);
}

static inline void
heap_zinfo_get(struct palloc_heap *heap, uint32_t zid, bool *allotted, bool *evictable)
{
	struct zinfo_element *ze;

	if (heap->rt->zinfo_vec) {
		ze         = heap->rt->zinfo_vec->z;
		*allotted  = ze[zid].z_allotted;
		*evictable = ze[zid].z_evictable;
	} else {
		D_ASSERT(zid == 0);
		*allotted  = false;
		*evictable = false;
	}
}

static inline void
heap_zinfo_set_usage(struct palloc_heap *heap, uint32_t zid, enum mb_usage_hint val)
{
	struct zinfo_element *ze = heap->rt->zinfo_vec->z;

	D_ASSERT(heap->rt->zinfo_vec && ze[zid].z_allotted && val < MB_UMAX_HINT);
	ze[zid].z_usage_hint = val;
	mo_wal_persist(&heap->p_ops, &ze[zid], sizeof(ze[zid]));
}

static inline void
heap_zinfo_get_usage(struct palloc_heap *heap, uint32_t zid, enum mb_usage_hint *val)
{
	struct zinfo_element *ze = heap->rt->zinfo_vec->z;

	D_ASSERT(heap->rt->zinfo_vec && ze[zid].z_allotted && ze[zid].z_evictable &&
		 ze[zid].z_usage_hint < MB_UMAX_HINT);
	*val = ze[zid].z_usage_hint;
}

size_t
heap_zinfo_get_size(uint32_t nzones)
{
	return (sizeof(struct zinfo_vec) + sizeof(struct zinfo_element) * nzones);
}

static inline void
heap_zinfo_init(struct palloc_heap *heap)
{
	struct zinfo_vec *z = heap->rt->zinfo_vec;

	D_ASSERT(heap->layout_info.zone0->header.zone0_zinfo_size >=
		 heap_zinfo_get_size(heap->rt->nzones));

	z->version   = ZINFO_VERSION;
	z->num_elems = heap->rt->nzones;
	mo_wal_persist(&heap->p_ops, z, sizeof(*z));
	heap_zinfo_set(heap, 0, 1, false);
}

static void
mbrt_set_laf(struct mbrt *mb, int c_id)
{
	if (mb->mb_id == 0)
		return;
	D_ASSERT(c_id < MAX_ALLOCATION_CLASSES);

	mb->laf[c_id]   = true;
	mb->laf_updated = true;
}

static void
mbrt_clear_laf(struct mbrt *mb)
{
	if (mb->mb_id == 0)
		return;
	if (mb->laf_updated) {
		memset(mb->laf, 0, MAX_ALLOCATION_CLASSES);
		mb->laf_updated = false;
	}
}

static bool
mbrt_is_laf(struct mbrt *mb, int c_id)
{
	D_ASSERT(c_id < MAX_ALLOCATION_CLASSES);
	return mb->laf[c_id];
}

static void
mbrt_qbs_init(struct mbrt_qbs *qb)
{
	TAILQ_INIT(&qb->mb_u90);
	TAILQ_INIT(&qb->mb_u75);
	TAILQ_INIT(&qb->mb_u30);
	TAILQ_INIT(&qb->mb_u0);
	TAILQ_INIT(&qb->mb_ue);
}

static void
mbrt_qbs_fini(struct mbrt_qbs *qb)
{
	/* No op */
}

static void
mbrt_qbs_insertmb(struct mbrt_qbs *qb, struct mbrt *mb)
{
	D_ASSERT(mb->qptr == NULL);

	if (mb->space_usage > MB_U90) {
		TAILQ_INSERT_TAIL(&qb->mb_u90, mb, mb_link);
		mb->qptr = &qb->mb_u90;
	} else if (mb->space_usage > MB_U75) {
		TAILQ_INSERT_TAIL(&qb->mb_u75, mb, mb_link);
		mb->qptr = &qb->mb_u75;
	} else if (mb->space_usage > MB_U30) {
		TAILQ_INSERT_TAIL(&qb->mb_u30, mb, mb_link);
		mb->qptr = &qb->mb_u30;
	} else if (mb->space_usage) {
		TAILQ_INSERT_TAIL(&qb->mb_u0, mb, mb_link);
		mb->qptr = &qb->mb_u0;
	} else {
		TAILQ_INSERT_TAIL(&qb->mb_ue, mb, mb_link);
		mb->qptr = &qb->mb_ue;
	}

	mb->prev_usage = mb->space_usage;
}

static void
mbrt_qbs_insertmb_force(struct mbrt_qbs *qb, struct mbrt *mb, int hint)
{
	D_ASSERT(mb->qptr == NULL);

	switch (hint) {
	case MB_U90_HINT:
		TAILQ_INSERT_TAIL(&qb->mb_u90, mb, mb_link);
		mb->qptr = &qb->mb_u90;
		break;
	case MB_U75_HINT:
		TAILQ_INSERT_TAIL(&qb->mb_u75, mb, mb_link);
		mb->qptr = &qb->mb_u75;
		break;
	case MB_U30_HINT:
		TAILQ_INSERT_TAIL(&qb->mb_u30, mb, mb_link);
		mb->qptr = &qb->mb_u30;
		break;
	case MB_U0_HINT:
		TAILQ_INSERT_TAIL(&qb->mb_u0, mb, mb_link);
		mb->qptr = &qb->mb_u0;
		break;
	default:
		D_ASSERTF(0, "invalid usage hint %d", hint);
		break;
	}
}

static int
mbrt_qbs_update_mb(struct mbrt_qbs *qb, struct mbrt *mb)
{
	int hint = MB_UMAX_HINT;

	if (mb->qptr == NULL)
		return MB_UMAX_HINT;

	if (mb->space_usage == 0) {
		TAILQ_REMOVE(mb->qptr, mb, mb_link);
		TAILQ_INSERT_TAIL(&qb->mb_ue, mb, mb_link);
		mb->qptr       = &qb->mb_ue;
		mb->prev_usage = mb->space_usage;
		return MB_U0_HINT;
	} else if (mb->qptr == &qb->mb_ue) {
		TAILQ_REMOVE(mb->qptr, mb, mb_link);
		TAILQ_INSERT_TAIL(&qb->mb_u0, mb, mb_link);
		mb->qptr = &qb->mb_u0;
	}

	if (labs((int64_t)(mb->space_usage - mb->prev_usage)) < MB_USAGE_DELTA)
		return MB_UMAX_HINT;

	if (mb->space_usage > MB_U90) {
		if (mb->qptr != &qb->mb_u90) {
			TAILQ_REMOVE(mb->qptr, mb, mb_link);
			TAILQ_INSERT_TAIL(&qb->mb_u90, mb, mb_link);
			mb->qptr = &qb->mb_u90;
			hint     = MB_U90_HINT;
		}
	} else if (mb->space_usage > MB_U75) {
		if (mb->qptr != &qb->mb_u75) {
			TAILQ_REMOVE(mb->qptr, mb, mb_link);
			TAILQ_INSERT_TAIL(&qb->mb_u75, mb, mb_link);
			mb->qptr = &qb->mb_u75;
			hint     = MB_U75_HINT;
		}
	} else if (mb->space_usage > MB_U30) {
		if (mb->qptr != &qb->mb_u30) {
			TAILQ_REMOVE(mb->qptr, mb, mb_link);
			TAILQ_INSERT_TAIL(&qb->mb_u30, mb, mb_link);
			mb->qptr = &qb->mb_u30;
			hint     = MB_U30_HINT;
		}
	} else if (mb->qptr != &qb->mb_u0) {
		TAILQ_REMOVE(mb->qptr, mb, mb_link);
		TAILQ_INSERT_TAIL(&qb->mb_u0, mb, mb_link);
		mb->qptr = &qb->mb_u0;
		hint     = MB_U0_HINT;
	}
	mb->prev_usage = mb->space_usage;
	return hint;
}

static struct mbrt *
mbrt_qbs_getmb(struct mbrt_qbs *qb, int force)
{
	struct mbrt *mb = NULL;

	if ((mb = TAILQ_FIRST(&qb->mb_u30)) != NULL)
		TAILQ_REMOVE(&qb->mb_u30, mb, mb_link);
	else if ((mb = TAILQ_FIRST(&qb->mb_u0)) != NULL)
		TAILQ_REMOVE(&qb->mb_u0, mb, mb_link);
	else if ((mb = TAILQ_FIRST(&qb->mb_ue)) != NULL)
		TAILQ_REMOVE(&qb->mb_ue, mb, mb_link);

	if (mb) {
		mb->qptr = NULL;
		return mb;
	}

	if (!force)
		return NULL;

	if ((mb = TAILQ_FIRST(&qb->mb_u75)) != NULL)
		TAILQ_REMOVE(&qb->mb_u75, mb, mb_link);
	else if ((mb = TAILQ_FIRST(&qb->mb_u90)) != NULL)
		TAILQ_REMOVE(&qb->mb_u90, mb, mb_link);

	if (mb)
		mb->qptr = NULL;
	return mb;
}

static struct mbrt *
mbrt_qbs_getmb_ue(struct mbrt_qbs *qb)
{
	struct mbrt *mb = NULL;
	if ((mb = TAILQ_FIRST(&qb->mb_ue)) != NULL) {
		TAILQ_REMOVE(&qb->mb_ue, mb, mb_link);
		mb->qptr = NULL;
	}
	return mb;
}

static void
soemb_init(struct soemb_rt *smbrt)
{
	memset(smbrt->svec, 0, sizeof(struct mbrt *) * SOEMB_ACTIVE_CNT);
	mbrt_qbs_init(&smbrt->qbs);
	smbrt->cur_idx = 0;
	smbrt->fur_idx = 0;
}

static void
soemb_fini(struct soemb_rt *smbrt)
{
	mbrt_qbs_fini(&smbrt->qbs);
}

static void
heap_mbrt_setmb_nonevictable(struct palloc_heap *heap, struct mbrt *mb, uint32_t zid)
{
	D_ASSERT(zid < heap->rt->nzones);
	D_ASSERT(heap->rt->default_mb != NULL);

	heap->rt->mbs[zid] = mb ? mb : heap->rt->default_mb;
	if (mb)
		mb->is_evictable = false;
}

static void
heap_mbrt_setmb_evictable(struct palloc_heap *heap, struct mbrt *mb)
{
	D_ASSERT((mb->mb_id != 0) && (mb->mb_id < heap->rt->nzones));
	heap->rt->mbs[mb->mb_id] = mb;
	mb->is_evictable         = true;
}

static void
heap_mbrt_setmb_unused(struct palloc_heap *heap, uint32_t zid)
{
	D_ASSERT((zid < heap->rt->nzones) && (heap->rt->mbs[zid]->is_evictable == false));
	heap->rt->mbs[zid] = NULL;
}

bool
heap_mbrt_ismb_evictable(struct palloc_heap *heap, uint32_t zid)
{
	D_ASSERT(zid < heap->rt->nzones);
	return (!heap->rt->mbs[zid] || heap->rt->mbs[zid]->is_evictable);
}

bool
heap_mbrt_ismb_initialized(struct palloc_heap *heap, uint32_t zid)
{
	D_ASSERT(zid < heap->rt->nzones);
	return (heap->rt->mbs[zid] != 0);
}

bool
heap_mbrt_ismb_localrt(struct palloc_heap *heap, uint32_t zid)
{
	D_ASSERT(zid < heap->rt->nzones);
	return (heap->rt->mbs[zid] != heap->rt->default_mb);
}

/*
 * mbrt_bucket_acquire -- fetches by mbrt or by id a bucket exclusive
 * for the thread until mbrt_bucket_release is called
 */
struct bucket *
mbrt_bucket_acquire(struct mbrt *mb, uint8_t class_id)
{
	struct bucket_locked *b;

	D_ASSERT(mb != NULL);

	if (class_id == DEFAULT_ALLOC_CLASS_ID)
		b = mb->default_bucket;
	else
		b = mb->buckets[class_id];

	return bucket_acquire(b);
}

/*
 * mbrt_bucket_release -- puts the bucket back into the heap
 */
void
mbrt_bucket_release(struct bucket *b)
{
	bucket_release(b);
}

/*
 * heap_mbrt_setup_mb -- (internal) create and initializes a Memory Bucket runtime.
 */
static struct mbrt *
heap_mbrt_setup_mb(struct palloc_heap *heap, uint32_t zid)
{
	struct heap_rt     *rt = heap->rt;
	struct mbrt        *mb;
	struct alloc_class *c;
	uint8_t             i;

	D_ALLOC_PTR(mb);
	if (mb == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	mb->mb_id = zid;

	for (i = 0; i < MAX_ALLOCATION_CLASSES; ++i) {
		c = alloc_class_by_id(rt->alloc_classes, i);

		if (c == NULL)
			continue;

		mb->buckets[c->id] = bucket_locked_new(container_new_seglists(heap), c, mb);
		if (mb->buckets[c->id] == NULL)
			goto error_bucket_create;
	}

	mb->default_bucket =
	    bucket_locked_new(container_new_ravl(heap),
			      alloc_class_by_id(rt->alloc_classes, DEFAULT_ALLOC_CLASS_ID), mb);

	if (mb->default_bucket == NULL)
		goto error_bucket_create;

	return mb;

error_bucket_create:
	for (i = 0; i < MAX_ALLOCATION_CLASSES; ++i) {
		c = alloc_class_by_id(rt->alloc_classes, i);
		if (c != NULL) {
			if (mb->buckets[c->id] != NULL)
				bucket_locked_delete(mb->buckets[c->id]);
		}
	}
	D_FREE(mb);
	errno = ENOMEM;
	return NULL;
}

static void
heap_mbrt_cleanup_mb(struct mbrt *mb)
{
	uint8_t i;

	if (mb == NULL)
		return;

	for (i = 0; i < MAX_ALLOCATION_CLASSES; ++i) {
		if (mb->buckets[i] == NULL)
			continue;
		bucket_locked_delete(mb->buckets[i]);
	}
	bucket_locked_delete(mb->default_bucket);

	for (i = 0; i < MAX_ALLOCATION_CLASSES; ++i) {
		if (mb->recyclers[i] == NULL)
			continue;
		recycler_delete(mb->recyclers[i]);
	}
	D_DEBUG(DB_TRACE, "MB %u utilization = %lu\n", mb->mb_id, mb->space_usage);
	D_FREE(mb);
}

int
heap_mbrt_update_alloc_class_buckets(struct palloc_heap *heap, struct mbrt *mb,
				     struct alloc_class *c)
{
	uint8_t c_id = c->id;

	if ((heap->rt->default_mb == mb) || (mb->buckets[c_id] != NULL))
		return 0;

	/* Allocation class created post creation/loading of the memory bucket runtime */
	if (heap->rt->default_mb->buckets[c_id]) {
		mb->buckets[c_id] = bucket_locked_new(container_new_seglists(heap), c, mb);
		if (!mb->buckets[c_id])
			return ENOMEM;
	}
	return 0;
}

static inline int
heap_mbrt_init(struct palloc_heap *heap)
{
	struct heap_rt    *rt    = heap->rt;
	int                ret   = 0;
	struct umem_store *store = heap->layout_info.store;

	rt->default_mb          = NULL;
	rt->active_evictable_mb = NULL;
	rt->mb_create_waiters   = 0;
	rt->mb_create_wq        = NULL;
	rt->mb_pressure         = 0;
	rt->empty_nemb_cnt      = 0;
	rt->soemb_cnt           = 0;
	rt->empty_nemb_gcth     = HEAP_NEMB_EMPTY_THRESHOLD;

	d_getenv_uint("DAOS_NEMB_EMPTY_RECYCLE_THRESHOLD", &rt->empty_nemb_gcth);
	if (!rt->empty_nemb_gcth)
		rt->empty_nemb_gcth = HEAP_NEMB_EMPTY_THRESHOLD;

	ret = store->stor_ops->so_waitqueue_create(&rt->mb_create_wq);
	if (ret) {
		ret = daos_der2errno(ret);
		goto error;
	}

	D_ALLOC_ARRAY(rt->mbs, rt->nzones);
	if (rt->mbs == NULL) {
		ret = ENOMEM;
		goto error;
	}

	mbrt_qbs_init(&rt->emb_qbs);

	rt->default_mb = heap_mbrt_setup_mb(heap, 0);
	if (rt->default_mb == NULL) {
		ret = ENOMEM;
		goto error_default_mb_setup;
	}
	heap_mbrt_setmb_nonevictable(heap, NULL, 0);
	return 0;

error_default_mb_setup:
	D_FREE(rt->mbs);
error:
	return ret;
}

static inline void
heap_mbrt_fini(struct palloc_heap *heap)
{
	struct heap_rt    *rt = heap->rt;
	int                i;
	struct umem_store *store = heap->layout_info.store;

	for (i = 0; i < rt->zones_exhausted; i++) {
		if (heap_mbrt_ismb_localrt(heap, i))
			heap_mbrt_cleanup_mb(rt->mbs[i]);
	}
	heap_mbrt_cleanup_mb(rt->default_mb);

	mbrt_qbs_fini(&rt->emb_qbs);
	D_FREE(rt->mbs);
	rt->default_mb          = NULL;
	rt->active_evictable_mb = NULL;
	rt->mbs                 = NULL;
	D_ASSERT(rt->mb_create_waiters == 0);
	if (rt->mb_create_wq != NULL)
		store->stor_ops->so_waitqueue_destroy(rt->mb_create_wq);
	rt->mb_create_wq = NULL;
}

/*
 * heap_mbrt_get_mb - returns the reference to the mb runtime given
 *		      zone_id or mb_id.
 */
struct mbrt *
heap_mbrt_get_mb(struct palloc_heap *heap, uint32_t zone_id)
{
	D_ASSERTF(heap->rt->mbs[zone_id] != NULL, "zone_id %d is marked unused", zone_id);
	return heap->rt->mbs[zone_id];
}

void
heap_mbrt_log_alloc_failure(struct palloc_heap *heap, uint32_t zone_id)
{
	struct mbrt *mb = heap->rt->active_evictable_mb;

	if (mb && (mb->mb_id == zone_id)) {
		heap->rt->active_evictable_mb = NULL;
		mbrt_qbs_insertmb_force(&heap->rt->emb_qbs, mb, MB_U90_HINT);
		heap_zinfo_set_usage(heap, zone_id, MB_U90_HINT);
	}
}

void
heap_mbrt_setmb_usage(struct palloc_heap *heap, uint32_t zone_id, uint64_t usage)
{
	struct mbrt *mb = heap->rt->mbs[zone_id];

	D_ASSERT(zone_id < heap->rt->nzones);
	if (zone_id == 0) {
		heap->rt->default_mb->space_usage = usage;
		return;
	}

	if (!heap_mbrt_ismb_evictable(heap, zone_id)) {
		mbrt_qbs_insertmb(&heap->rt->smbrt.qbs, mb);
		return;
	}

	mb->space_usage = usage;

	if (heap->rt->active_evictable_mb == mb)
		return;

	if (mb->qptr)
		mbrt_qbs_update_mb(&heap->rt->emb_qbs, mb);
	else
		mbrt_qbs_insertmb(&heap->rt->emb_qbs, mb);
}

int
heap_mbrt_getmb_usage(struct palloc_heap *heap, uint32_t zone_id, uint64_t *allotted,
		      uint64_t *maxsz)
{
	struct mbrt *mb;

	if (zone_id == 0) {
		*maxsz    = heap->rt->nzones_ne * ZONE_MAX_SIZE;
		*allotted = heap->rt->default_mb->space_usage;
	} else {
		if (zone_id >= heap->rt->nzones) {
			errno = EINVAL;
			return -1;
		}
		mb = heap->rt->mbs[zone_id];
		if (!mb || !heap_mbrt_ismb_evictable(heap, zone_id)) {
			errno = EINVAL;
			return -1;
		}
		*maxsz    = ZONE_MAX_SIZE;
		*allotted = mb->space_usage;
	}
	return 0;
}

void
heap_mbrt_incrmb_usage(struct palloc_heap *heap, uint32_t zone_id, int size)
{
	struct mbrt *mb = heap->rt->mbs[zone_id];
	int          hint;

	if (!heap_mbrt_ismb_evictable(heap, zone_id))
		heap->rt->default_mb->space_usage += size;

	if (!heap_mbrt_ismb_localrt(heap, zone_id))
		return;

	mb->space_usage += size;

	if (heap->rt->active_evictable_mb == mb)
		return;

	if (heap_mbrt_ismb_evictable(heap, zone_id)) {
		hint = mbrt_qbs_update_mb(&heap->rt->emb_qbs, mb);
		if (hint != MB_UMAX_HINT)
			heap_zinfo_set_usage(heap, zone_id, hint);
		if (hint <= MB_U30_HINT)
			heap->rt->mb_pressure = 0;
	} else
		hint = mbrt_qbs_update_mb(&heap->rt->smbrt.qbs, mb);
}

static int
heap_mbrt_mb_reclaim_garbage(struct palloc_heap *heap, uint32_t zid)
{
	struct mbrt   *mb;
	struct bucket *b;

	mb = heap_mbrt_get_mb(heap, zid);

	if ((mb->mb_id != 0) && (mb->garbage_reclaimed))
		return 0;

	b = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
	heap_reclaim_zone_garbage(heap, b, zid);
	mbrt_bucket_release(b);

	if (mb->mb_id != 0)
		mb->garbage_reclaimed = 1;

	return 0;
}

void
heap_soemb_active_iter_init(struct palloc_heap *heap)
{
	heap->rt->smbrt.cur_idx = 0;
}

uint32_t
heap_soemb_active_get(struct palloc_heap *heap)
{
	struct soemb_rt *smbrt = &heap->rt->smbrt;
	struct mbrt     *mb    = NULL;

	if (heap->rt->nzones_e == 0)
		return 0;

	if (smbrt->cur_idx > smbrt->fur_idx)
		smbrt->fur_idx = smbrt->cur_idx;

	if (smbrt->cur_idx < SOEMB_ACTIVE_CNT) {
		mb = smbrt->svec[smbrt->cur_idx];
		smbrt->cur_idx++;
	}

	if (mb)
		return mb->mb_id;

	return 0;
}

static int
heap_create_soe_mb(struct palloc_heap *heap, uint32_t *mb_id);

void
heap_soemb_reserve(struct palloc_heap *heap)
{
	int              i, ret;
	uint32_t         mb_id;
	struct mbrt     *mb;
	struct soemb_rt *smbrt = &heap->rt->smbrt;

	if (heap->rt->nzones_e == 0)
		return;

	if (smbrt->fur_idx > 1) {
		mb = smbrt->svec[0];
		if (mb)
			mbrt_qbs_insertmb(&smbrt->qbs, mb);

		for (i = 1; i < SOEMB_ACTIVE_CNT; i++) {
			smbrt->svec[i - 1] = smbrt->svec[i];
		}

		smbrt->svec[SOEMB_ACTIVE_CNT - 1] = NULL;
		smbrt->fur_idx                    = 0;
	}

	for (i = 0; i < SOEMB_ACTIVE_CNT; i++) {
		if (smbrt->svec[i] != NULL)
			continue;
		mb = mbrt_qbs_getmb(&smbrt->qbs, 0);
		if (mb) {
			smbrt->svec[i] = mb;
			break;
		}
		ret = heap_create_soe_mb(heap, &mb_id);
		if (ret == 0) {
			smbrt->svec[i] = heap_mbrt_get_mb(heap, mb_id);
			break;
		}
		mb = mbrt_qbs_getmb(&smbrt->qbs, 1);
		if (mb) {
			smbrt->svec[i] = mb;
			break;
		}
		break;
	}
	smbrt->cur_idx = 0;
}

void
heap_set_root_ptrs(struct palloc_heap *heap, uint64_t **offp, uint64_t **sizep)
{
	*offp  = &heap->layout_info.zone0->header.reserved[0];
	*sizep = &heap->layout_info.zone0->header.reserved[1];
}

void
heap_set_stats_ptr(struct palloc_heap *heap, struct stats_persistent **sp)
{
	D_CASSERT(sizeof(struct stats_persistent) == sizeof(uint64_t));
	*sp = (struct stats_persistent *)&heap->layout_info.zone0->header.sp_usage_glob;
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(*sp, sizeof(*sp));
}

/*
 * heap_get_recycler - (internal) retrieves the recycler instance from the mbrt with
 *	the corresponding class id. Initializes the recycler if needed.
 */
static struct recycler *
heap_get_recycler(struct palloc_heap *heap, struct mbrt *mb, size_t id, size_t nallocs)
{
	struct recycler *r;

	D_ASSERT(mb != NULL);
	util_atomic_load_explicit64(&mb->recyclers[id], &r, memory_order_acquire);
	if (r != NULL)
		return r;

	r = recycler_new(heap, nallocs, mb);
	if (r && !util_bool_compare_and_swap64(&mb->recyclers[id], NULL, r)) {
		/*
		 * If a different thread succeeded in assigning the recycler
		 * first, the recycler this thread created needs to be deleted.
		 */
		recycler_delete(r);

		return heap_get_recycler(heap, mb, id, nallocs);
	}

	return r;
}

/*
 * heap_alloc_classes -- returns the allocation classes collection
 */
struct alloc_class_collection *
heap_alloc_classes(struct palloc_heap *heap)
{
	return heap->rt ? heap->rt->alloc_classes : NULL;
}

/*
 * heap_get_best_class -- returns the alloc class that best fits the
 *	requested size
 */
struct alloc_class *
heap_get_best_class(struct palloc_heap *heap, size_t size)
{
	return alloc_class_by_alloc_size(heap->rt->alloc_classes, size);
}

/*
 * heap_get_run_lock -- returns the lock associated with memory block
 */
pthread_mutex_t *
heap_get_run_lock(struct palloc_heap *heap, uint32_t chunk_id)
{
	return &heap->rt->run_locks[chunk_id % heap->rt->nlocks];
}

/*
 * heap_max_zone -- (internal) calculates how many zones can the heap fit
 */
static unsigned
heap_max_zone(size_t size)
{
	unsigned max_zone = 0;

	size -= sizeof(struct heap_header);

	while (size >= ZONE_MIN_SIZE) {
		max_zone++;
		size -= size <= ZONE_MAX_SIZE ? size : ZONE_MAX_SIZE;
	}

	return max_zone;
}

/*
 * zone_calc_size_idx -- (internal) calculates zone size index
 */
static uint32_t
zone_calc_size_idx(uint32_t zone_id, unsigned max_zone, size_t heap_size)
{
	ASSERT(max_zone > 0);
	if (zone_id < max_zone - 1)
		return MAX_CHUNK;

	ASSERT(heap_size >= zone_id * ZONE_MAX_SIZE);
	size_t zone_raw_size = heap_size - zone_id * ZONE_MAX_SIZE;

	ASSERT(zone_raw_size >= (sizeof(struct zone_header) +
			sizeof(struct chunk_header) * MAX_CHUNK) +
			sizeof(struct heap_header));
	zone_raw_size -= sizeof(struct zone_header) +
		sizeof(struct chunk_header) * MAX_CHUNK +
		sizeof(struct heap_header);

	size_t zone_size_idx = zone_raw_size / CHUNKSIZE;

	ASSERT(zone_size_idx <= MAX_CHUNK);

	return (uint32_t)zone_size_idx;
}

/*
 * heap_zone_init -- (internal) writes zone's first chunk and header
 */
static void
heap_zone_init(struct palloc_heap *heap, uint32_t zone_id, uint32_t first_chunk_id, int flags)
{
	struct zone *z        = ZID_TO_ZONE(&heap->layout_info, zone_id);
	uint32_t     size_idx = zone_calc_size_idx(zone_id, heap->rt->nzones, heap->size);

	ASSERT(size_idx > first_chunk_id);

	struct zone_header nhdr = {
		.size_idx = size_idx,
		.magic = ZONE_HEADER_MAGIC,
	};

	z->header = nhdr; /* write the entire header at once */

	if (flags) {
		D_ASSERT((flags == ZONE_EVICTABLE_MB) || (flags == ZONE_SOE_MB));
		z->header.flags = flags;
	}
	mo_wal_persist(&heap->p_ops, &z->header, sizeof(z->header));

	memblock_huge_init(heap, first_chunk_id, zone_id, size_idx - first_chunk_id);
}

/*
 * heap_get_adjacent_free_block -- locates adjacent free memory block in heap
 */
static int
heap_get_adjacent_free_block(struct palloc_heap *heap,
	const struct memory_block *in, struct memory_block *out, int prev)
{
	struct zone         *z   = ZID_TO_ZONE(&heap->layout_info, in->zone_id);
	struct chunk_header *hdr = &z->chunk_headers[in->chunk_id];

	out->zone_id = in->zone_id;

	if (prev) {
		if (in->chunk_id == 0)
			return ENOENT;

		struct chunk_header *prev_hdr =
			&z->chunk_headers[in->chunk_id - 1];
		out->chunk_id = in->chunk_id - prev_hdr->size_idx;

		if (z->chunk_headers[out->chunk_id].type != CHUNK_TYPE_FREE)
			return ENOENT;

		out->size_idx = z->chunk_headers[out->chunk_id].size_idx;
	} else { /* next */
		if (in->chunk_id + hdr->size_idx == z->header.size_idx)
			return ENOENT;

		out->chunk_id = in->chunk_id + hdr->size_idx;

		if (z->chunk_headers[out->chunk_id].type != CHUNK_TYPE_FREE)
			return ENOENT;

		out->size_idx = z->chunk_headers[out->chunk_id].size_idx;
	}
	memblock_rebuild_state(heap, out);

	return 0;
}

/*
 * heap_coalesce -- (internal) merges adjacent memory blocks
 */
static struct memory_block
heap_coalesce(struct palloc_heap *heap,
	const struct memory_block *blocks[], int n)
{
	struct memory_block ret = MEMORY_BLOCK_NONE;

	const struct memory_block *b = NULL;

	ret.size_idx = 0;
	for (int i = 0; i < n; ++i) {
		if (blocks[i] == NULL)
			continue;
		b = b ? b : blocks[i];
		ret.size_idx += blocks[i]->size_idx;
	}

	ASSERTne(b, NULL);

	ret.chunk_id = b->chunk_id;
	ret.zone_id = b->zone_id;
	ret.block_off = b->block_off;
	memblock_rebuild_state(heap, &ret);

	return ret;
}

/*
 * heap_coalesce_huge -- finds neighbors of a huge block, removes them from the
 *	volatile state and returns the resulting block
 */
static struct memory_block
heap_coalesce_huge(struct palloc_heap *heap, struct bucket *b,
	const struct memory_block *m)
{
	const struct memory_block *blocks[3] = {NULL, m, NULL};

	struct memory_block prev = MEMORY_BLOCK_NONE;

	if (heap_get_adjacent_free_block(heap, m, &prev, 1) == 0 &&
		bucket_remove_block(b, &prev) == 0) {
		blocks[0] = &prev;
	}

	struct memory_block next = MEMORY_BLOCK_NONE;

	if (heap_get_adjacent_free_block(heap, m, &next, 0) == 0 &&
		bucket_remove_block(b, &next) == 0) {
		blocks[2] = &next;
	}

	return heap_coalesce(heap, blocks, 3);
}

/*
 * heap_free_chunk_reuse -- reuses existing free chunk
 */
int
heap_free_chunk_reuse(struct palloc_heap *heap,
	struct bucket *bucket,
	struct memory_block *m)
{
	/*
	 * Perform coalescing just in case there
	 * are any neighboring free chunks.
	 */
	struct memory_block nm = heap_coalesce_huge(heap, bucket, m);

	if (nm.size_idx != m->size_idx)
		m->m_ops->prep_hdr(&nm, MEMBLOCK_FREE, NULL);

	*m = nm;

	return bucket_insert_block(bucket, m);
}

/*
 * heap_run_into_free_chunk -- (internal) creates a new free chunk in place of
 *	a run.
 */
static void
heap_run_into_free_chunk(struct palloc_heap *heap,
	struct bucket *bucket,
	struct memory_block *m)
{
	struct chunk_header *hdr = heap_get_chunk_hdr(heap, m);

	m->block_off = 0;
	m->size_idx = hdr->size_idx;

	STATS_SUB(heap->stats, transient, heap_run_active,
		m->size_idx * CHUNKSIZE);

	/*
	 * The only thing this could race with is heap_memblock_on_free()
	 * because that function is called after processing the operation,
	 * which means that a different thread might immediately call this
	 * function if the free() made the run empty.
	 * We could forgo this lock if it weren't for helgrind which needs it
	 * to establish happens-before relation for the chunk metadata.
	 */
	pthread_mutex_t *lock = m->m_ops->get_lock(m);

	util_mutex_lock(lock);

	*m = memblock_huge_init(heap, m->chunk_id, m->zone_id, m->size_idx);

	heap_free_chunk_reuse(heap, bucket, m);

	util_mutex_unlock(lock);
}

/*
 * heap_reclaim_run -- checks the run for available memory if unclaimed.
 *
 * Returns 1 if reclaimed chunk, 0 otherwise.
 */
static int
heap_reclaim_run(struct palloc_heap *heap, struct memory_block *m, int startup)
{
	struct chunk_run    *run  = heap_get_chunk_run(heap, m);
	struct chunk_header *hdr = heap_get_chunk_hdr(heap, m);
	struct mbrt         *mb   = heap_mbrt_get_mb(heap, m->zone_id);

	struct alloc_class *c = alloc_class_by_run(
		heap->rt->alloc_classes,
		run->hdr.block_size, hdr->flags, m->size_idx);

	struct recycler_element e = recycler_element_new(heap, m);

	if (c == NULL) {
		uint32_t size_idx = m->size_idx;
		struct run_bitmap b;

		m->m_ops->get_bitmap(m, &b);

		ASSERTeq(size_idx, m->size_idx);

		return e.free_space == b.nbits;
	}

	if (e.free_space == c->rdsc.nallocs)
		return 1;

	if (startup) {
		STATS_INC(heap->stats, transient, heap_run_active,
			m->size_idx * CHUNKSIZE);
		STATS_INC(heap->stats, transient, heap_run_allocated,
			(c->rdsc.nallocs - e.free_space) * run->hdr.block_size);
	}
	struct recycler *recycler = heap_get_recycler(heap, mb, c->id, c->rdsc.nallocs);

	if (recycler == NULL || recycler_put(recycler, e) < 0)
		ERR("lost runtime tracking info of %u run due to OOM", c->id);

	return 0;
}

/*
 * heap_reclaim_zone_garbage -- (internal) creates volatile state of unused runs
 */
static void
heap_reclaim_zone_garbage(struct palloc_heap *heap, struct bucket *bucket,
	uint32_t zone_id)
{
	struct zone *z = ZID_TO_ZONE(&heap->layout_info, zone_id);

	for (uint32_t i = 0; i < z->header.size_idx; ) {
		struct chunk_header *hdr = &z->chunk_headers[i];

		ASSERT(hdr->size_idx != 0);

		struct memory_block m = MEMORY_BLOCK_NONE;

		m.zone_id = zone_id;
		m.chunk_id = i;
		m.size_idx = hdr->size_idx;

		memblock_rebuild_state(heap, &m);
		m.m_ops->reinit_chunk(&m);

		switch (hdr->type) {
		case CHUNK_TYPE_RUN:
			if (heap_reclaim_run(heap, &m, 1) != 0)
				heap_run_into_free_chunk(heap, bucket, &m);
			break;
		case CHUNK_TYPE_FREE:
			heap_free_chunk_reuse(heap, bucket, &m);
			break;
		case CHUNK_TYPE_USED:
			break;
		default:
			ASSERT(0);
		}

		i = m.chunk_id + m.size_idx; /* hdr might have changed */
	}
}

static int
heap_reclaim_next_ne(struct palloc_heap *heap, uint32_t *zone_id)
{
	bool            allotted, evictable;
	uint32_t        i;
	struct heap_rt *h = heap->rt;

	if (h->zones_nextne_gc >= h->zones_exhausted)
		return -1;

	for (i = h->zones_nextne_gc; i < h->zones_exhausted; i++) {
		heap_zinfo_get(heap, i, &allotted, &evictable);
		if (!allotted)
			continue;
		if (!evictable && !heap_mbrt_ismb_localrt(heap, i)) {
			h->zones_nextne_gc = i + 1;
			*zone_id = i;
			return 0;
		}
	}
	return -1;
}

static void
heap_reclaim_setlast_ne(struct palloc_heap *heap, uint32_t zone_id)
{
	if (zone_id >= heap->rt->zones_nextne_gc)
		heap->rt->zones_nextne_gc = zone_id + 1;
}

static int
heap_get_next_unused_zone(struct palloc_heap *heap, uint32_t *zone_id)
{
	bool     allotted, evictable;
	uint32_t i;

	for (i = heap->rt->zones_unused_first; i < heap->rt->nzones; i++) {
		heap_zinfo_get(heap, i, &allotted, &evictable);
		if (!allotted)
			break;
	}
	if (i == heap->rt->nzones) {
		heap->rt->zones_unused_first = heap->rt->nzones;
		return -1;
	}

	*zone_id = i;
	return 0;
}

static void
heap_mark_zone_used_transient(struct palloc_heap *heap, struct mbrt *mb, uint32_t zone_id,
			      bool is_evictable)
{
	if (is_evictable) {
		D_ASSERT(mb != NULL);
		heap_mbrt_setmb_evictable(heap, mb);
		heap->rt->zones_exhausted_e++;
	} else {
		heap_mbrt_setmb_nonevictable(heap, mb, zone_id);
		heap->rt->zones_exhausted_ne++;
	}

	heap->rt->zones_unused_first = zone_id + 1;
	if (heap->rt->zones_exhausted < heap->rt->zones_unused_first)
		heap->rt->zones_exhausted = heap->rt->zones_unused_first;
}

static void
heap_mark_zone_used_persist(struct palloc_heap *heap, uint32_t zone_id)
{
	bool is_evictable = heap_mbrt_ismb_evictable(heap, zone_id);

	if (zone_id)
		heap_zinfo_set(heap, zone_id, true, is_evictable);
}

static void
heap_mark_zone_unused_transient(struct palloc_heap *heap, uint32_t zone_id)
{
	if (heap_mbrt_ismb_evictable(heap, zone_id))
		heap->rt->zones_exhausted_e--;
	else
		heap->rt->zones_exhausted_ne--;

	heap_mbrt_setmb_unused(heap, zone_id);

	if (heap->rt->zones_unused_first > zone_id)
		heap->rt->zones_unused_first = zone_id;
	if (heap->rt->zones_exhausted == (zone_id + 1))
		heap->rt->zones_exhausted = zone_id;
}

static int
heap_mark_zone_unused(struct palloc_heap *heap, uint32_t zone_id)
{
	struct umem_cache_range rg           = {0};
	bool                    is_evictable = heap_mbrt_ismb_evictable(heap, zone_id);
	int                     rc;
	struct mbrt            *mb = heap_mbrt_get_mb(heap, zone_id);

	D_ASSERT(is_evictable == false);

	if (heap_mbrt_ismb_localrt(heap, zone_id)) {
		heap->rt->soemb_cnt--;
		VALGRIND_DO_DESTROY_MEMPOOL_COND(ZID_TO_ZONE(&heap->layout_info, zone_id));
	}
	heap_mark_zone_unused_transient(heap, zone_id);
	rg.cr_off = GET_ZONE_OFFSET(zone_id);
	rg.cr_size =
	    ((heap->size - rg.cr_off) > ZONE_MAX_SIZE) ? ZONE_MAX_SIZE : heap->size - rg.cr_off;
	rc = umem_cache_map(heap->layout_info.store, &rg, 1);
	if (rc != 0) {
		rc = daos_der2errno(rc);
		ERR("Failed to remap zone %d in umem cache as unused rc=%d\n", zone_id, rc);
		heap_mark_zone_used_transient(heap, mb, zone_id, is_evictable);
		VALGRIND_DO_CREATE_MEMPOOL(ZID_TO_ZONE(&heap->layout_info, zone_id), 0, 0);
		return -1;
	}
	heap_zinfo_set_usage(heap, zone_id, MB_U0_HINT);
	heap_zinfo_set(heap, zone_id, false, false);
	return 0;
}

int
heap_populate_nemb_unused(struct palloc_heap *heap)
{
	struct bucket      *defb;
	struct memory_block m = MEMORY_BLOCK_NONE;
	struct mbrt        *mb;
	int                 rc;

	m.size_idx = MAX_CHUNK;

	mb   = heap_mbrt_get_mb(heap, 0);
	defb = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
	while (bucket_alloc_block(defb, &m) == 0) {
		rc = heap_mark_zone_unused(heap, m.zone_id);
		if (!rc)
			heap->rt->empty_nemb_cnt--;

		m          = MEMORY_BLOCK_NONE;
		m.size_idx = MAX_CHUNK;
	}
	mbrt_bucket_release(defb);

	return 0;
}

/*
 * heap_populate_bucket -- (internal) creates volatile state of memory blocks
 */
static int
heap_populate_bucket(struct palloc_heap *heap, struct bucket *bucket)
{
	struct mbrt            *mb = bucket_get_mbrt(bucket);
	struct umem_cache_range rg = {0};
	int                     rc;
	uint32_t                zone_id;

	if (mb->mb_id != 0) {
		if (!mb->garbage_reclaimed) {
			heap_reclaim_zone_garbage(heap, bucket, mb->mb_id);
			mb->garbage_reclaimed = 1;
			return 0;
		}
		return ENOMEM;
	}

	rc = heap_reclaim_next_ne(heap, &zone_id);
	if (!rc)
		goto reclaim_garbage;

	if (heap->rt->zones_exhausted_ne >= heap->rt->nzones_ne)
		return ENOMEM;

	rc = heap_get_next_unused_zone(heap, &zone_id);
	if (rc)
		return ENOMEM;

	heap_mark_zone_used_transient(heap, NULL, zone_id, false);

	/* Create a umem cache map for the new zone */
	rg.cr_off = GET_ZONE_OFFSET(zone_id);
	rg.cr_size =
	    ((heap->size - rg.cr_off) > ZONE_MAX_SIZE) ? ZONE_MAX_SIZE : heap->size - rg.cr_off;
	rc = umem_cache_map(heap->layout_info.store, &rg, 1);
	if (rc != 0) {
		rc = daos_der2errno(rc);
		ERR("Failed to map zone %d to umem cache rc=%d\n", zone_id, rc);
		heap_mark_zone_unused_transient(heap, zone_id);
		return rc;
	}

	struct zone *z = ZID_TO_ZONE(&heap->layout_info, zone_id);

	VALGRIND_DO_MAKE_MEM_UNDEFINED(ZID_TO_ZONE(&heap->layout_info, zone_id), rg.cr_size);
	if (rg.cr_size != ZONE_MAX_SIZE)
		VALGRIND_DO_MAKE_MEM_NOACCESS(ZID_TO_ZONE(&heap->layout_info, zone_id) + rg.cr_size,
					      (ZONE_MAX_SIZE - rg.cr_size));

	/*
	 * umem_cache_map() does not return a zeroed page.
	 * Explicitly memset the page.
	 */
	memset(z, 0, rg.cr_size);

	/* ignore zone and chunk headers */
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(z, sizeof(z->header) +
		sizeof(z->chunk_headers));

	heap_zone_init(heap, zone_id, 0, 0);
	heap_mark_zone_used_persist(heap, zone_id);

reclaim_garbage:
	heap_reclaim_zone_garbage(heap, bucket, zone_id);
	heap_reclaim_setlast_ne(heap, zone_id);
	/*
	 * It doesn't matter that this function might not have found any
	 * free blocks because there is still potential that subsequent calls
	 * will find something in later zones.
	 */
	return 0;
}

/*
 * heap_recycle_unused -- recalculate scores in the recycler and turn any
 *	empty runs into free chunks
 *
 * If force is not set, this function might effectively be a noop if not enough
 * of space was freed.
 */
static int
heap_recycle_unused(struct palloc_heap *heap, struct recycler *recycler,
	struct bucket *defb, int force)
{
	struct mbrt         *mb;
	struct memory_block *nm;
	struct empty_runs    r = recycler_recalc(recycler, force);
	struct bucket       *nb;

	if (VEC_SIZE(&r) == 0)
		return ENOMEM;

	mb = recycler_get_mbrt(recycler);
	D_ASSERT(mb != NULL);

	nb = defb == NULL ? mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID) : NULL;

	ASSERT(defb != NULL || nb != NULL);

	VEC_FOREACH_BY_PTR(nm, &r) {
		heap_run_into_free_chunk(heap, defb ? defb : nb, nm);
	}

	if (nb != NULL)
		mbrt_bucket_release(nb);

	VEC_DELETE(&r);

	return 0;
}

/*
 * heap_reclaim_garbage -- (internal) creates volatile state of unused runs
 */
static int
heap_reclaim_garbage(struct palloc_heap *heap, struct bucket *bucket)
{
	int              ret = ENOMEM;
	struct recycler *r;
	struct mbrt     *mb = bucket_get_mbrt(bucket);

	for (size_t i = 0; i < MAX_ALLOCATION_CLASSES; ++i) {
		r = mb->recyclers[i];
		if (r == NULL)
			continue;

		if (heap_recycle_unused(heap, r, bucket, 1) == 0)
			ret = 0;
	}

	return ret;
}

/*
 * heap_ensure_huge_bucket_filled --
 *	(internal) refills the default bucket if needed
 */
static int
heap_ensure_huge_bucket_filled(struct palloc_heap *heap,
	struct bucket *bucket)
{
	if (heap_reclaim_garbage(heap, bucket) == 0)
		return 0;

	if (heap_populate_bucket(heap, bucket) == 0)
		return 0;

	return ENOMEM;
}

/*
 * heap_discard_run -- puts the memory block back into the global heap.
 */
void
heap_discard_run(struct palloc_heap *heap, struct memory_block *m)
{
	struct mbrt *mb = heap_mbrt_get_mb(heap, m->zone_id);

	D_ASSERT(mb != NULL);
	if (heap_reclaim_run(heap, m, 0)) {
		struct bucket *b = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);

		heap_run_into_free_chunk(heap, b, m);

		mbrt_bucket_release(b);
	}
}

/*
 * heap_detach_and_try_discard_run -- detaches the active from a bucket and
 *	tries to discard the run if it is completely empty (has no allocations)
 */
static int
heap_detach_and_try_discard_run(struct palloc_heap *heap, struct bucket *b)
{
	int empty = 0;
	struct memory_block m;

	if (bucket_detach_run(b, &m, &empty) != 0)
		return -1;

	if (empty)
		heap_discard_run(heap, &m);

	return 0;
}

/*
 * heap_reuse_from_recycler -- (internal) try reusing runs that are currently
 *	in the recycler
 */
static int
heap_reuse_from_recycler(struct palloc_heap *heap,
	struct bucket *b, uint32_t units, int force)
{
	struct mbrt        *mb = bucket_get_mbrt(b);
	struct memory_block m  = MEMORY_BLOCK_NONE;

	m.size_idx = units;

	struct alloc_class *aclass = bucket_alloc_class(b);

	struct recycler *recycler = heap_get_recycler(heap, mb, aclass->id, aclass->rdsc.nallocs);

	if (recycler == NULL) {
		ERR("lost runtime tracking info of %u run due to OOM",
			aclass->id);
		return 0;
	}

	if (!force && recycler_get(recycler, &m) == 0)
		return bucket_attach_run(b, &m);

	heap_recycle_unused(heap, recycler, NULL, force);

	if (recycler_get(recycler, &m) == 0)
		return bucket_attach_run(b, &m);

	return ENOMEM;
}

/*
 * heap_run_create -- (internal) initializes a new run on an existing free chunk
 */
static int
heap_run_create(struct palloc_heap *heap, struct bucket *b,
	struct memory_block *m)
{
	struct alloc_class *aclass = bucket_alloc_class(b);
	*m = memblock_run_init(heap, m->chunk_id, m->zone_id, &aclass->rdsc);

	bucket_attach_run(b, m);

	STATS_INC(heap->stats, transient, heap_run_active,
		m->size_idx * CHUNKSIZE);

	return 0;
}

/*
 * heap_ensure_run_bucket_filled -- (internal) refills the bucket if needed
 */
static int
heap_ensure_run_bucket_filled(struct palloc_heap *heap, struct bucket *b,
	uint32_t units)
{
	int ret = 0;
	struct alloc_class *aclass = bucket_alloc_class(b);
	struct mbrt        *mb     = bucket_get_mbrt(b);
	struct memory_block m;
	struct bucket      *defb;

	D_ASSERT(mb != NULL);
	ASSERTeq(aclass->type, CLASS_RUN);

	if (mbrt_is_laf(mb, aclass->id))
		return ENOMEM;

	if (heap_detach_and_try_discard_run(heap, b) != 0)
		return ENOMEM;

	if (heap_reuse_from_recycler(heap, b, units, 0) == 0)
		goto out;

	m = MEMORY_BLOCK_NONE;

	m.size_idx = aclass->rdsc.size_idx;

	defb = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);

	/* cannot reuse an existing run, create a new one */
	if (heap_get_bestfit_block(heap, defb, &m) == 0) {
		ASSERTeq(m.block_off, 0);
		if (heap_run_create(heap, b, &m) != 0) {
			mbrt_bucket_release(defb);
			return ENOMEM;
		}
		mbrt_bucket_release(defb);
		goto out;
	}
	mbrt_bucket_release(defb);

	if (heap_reuse_from_recycler(heap, b, units, 1) == 0)
		goto out;

	mbrt_set_laf(mb, aclass->id);
	ret = ENOMEM;
out:
	return ret;
}

/*
 * heap_memblock_on_free -- bookkeeping actions executed at every free of a
 *	block
 */
void
heap_memblock_on_free(struct palloc_heap *heap, const struct memory_block *m)
{
	struct mbrt *mb = heap_mbrt_get_mb(heap, m->zone_id);

	if (m->type != MEMORY_BLOCK_RUN)
		return;

	struct chunk_header *hdr = heap_get_chunk_hdr(heap, m);
	struct chunk_run *run = heap_get_chunk_run(heap, m);

	ASSERTeq(hdr->type, CHUNK_TYPE_RUN);

	struct alloc_class *c = alloc_class_by_run(
		heap->rt->alloc_classes,
		run->hdr.block_size, hdr->flags, hdr->size_idx);

	if (c == NULL)
		return;

	struct recycler *recycler = heap_get_recycler(heap, mb, c->id, c->rdsc.nallocs);

	if (recycler == NULL) {
		ERR("lost runtime tracking info of %u run due to OOM",
			c->id);
	} else {
		recycler_inc_unaccounted(recycler, m);
		mbrt_clear_laf(mb);
	}
}

/*
 * heap_split_block -- (internal) splits unused part of the memory block
 */
static void
heap_split_block(struct palloc_heap *heap, struct bucket *b,
		struct memory_block *m, uint32_t units)
{
	struct alloc_class *aclass = bucket_alloc_class(b);

	ASSERT(units <= MAX_CHUNK);
	ASSERT(units > 0);

	if (aclass->type == CLASS_RUN) {
		ASSERT((uint64_t)m->block_off + (uint64_t)units <= UINT32_MAX);
		struct memory_block r = {m->chunk_id, m->zone_id,
			m->size_idx - units, (uint32_t)(m->block_off + units),
			NULL, NULL, 0, 0, NULL};
		memblock_rebuild_state(heap, &r);
		if (bucket_insert_block(b, &r) != 0)
			D_CRIT("failed to allocate memory block runtime tracking info\n");
	} else {
		uint32_t new_chunk_id = m->chunk_id + units;
		uint32_t new_size_idx = m->size_idx - units;

		struct memory_block n = memblock_huge_init(heap,
			new_chunk_id, m->zone_id, new_size_idx);

		*m = memblock_huge_init(heap, m->chunk_id, m->zone_id, units);

		if (bucket_insert_block(b, &n) != 0)
			D_CRIT("failed to allocate memory block runtime tracking info\n");
	}

	m->size_idx = units;
}

/*
 * heap_get_bestfit_block --
 *	extracts a memory block of equal size index
 */
int
heap_get_bestfit_block(struct palloc_heap *heap, struct bucket *b,
	struct memory_block *m)
{
	struct alloc_class *aclass = bucket_alloc_class(b);
	uint32_t units = m->size_idx;

	while (bucket_alloc_block(b, m) != 0) {
		if (aclass->type == CLASS_HUGE) {
			if (heap_ensure_huge_bucket_filled(heap, b) != 0)
				return ENOMEM;
		} else {
			if (heap_ensure_run_bucket_filled(heap, b, units) != 0)
				return ENOMEM;
		}
	}

	ASSERT(m->size_idx >= units);

	if (units != m->size_idx)
		heap_split_block(heap, b, m, units);

	m->m_ops->ensure_header_type(m, aclass->header_type);
	m->header_type = aclass->header_type;

	return 0;
}

/*
 * heap_create_alloc_class_buckets -- allocates all cache bucket
 * instances of the specified type
 */
int
heap_create_alloc_class_buckets(struct palloc_heap *heap, struct alloc_class *c)
{
	struct mbrt *default_mb = heap->rt->default_mb;

	if (default_mb->buckets[c->id] == NULL) {
		default_mb->buckets[c->id] =
		    bucket_locked_new(container_new_seglists(heap), c, default_mb);
		if (default_mb->buckets[c->id] == NULL)
			return -1;
	}

	return 0;
}

/*
 * heap_write_header -- (internal) creates a clean header
 */
static int
heap_write_header(struct umem_store *store, size_t heap_size, size_t umem_cache_size,
		  uint32_t nemb_pct)
{
	struct heap_header *newhdr;
	int                 rc;

	D_ALLOC_PTR(newhdr);
	if (!newhdr)
		return -1;

	strncpy(newhdr->signature, HEAP_SIGNATURE, HEAP_SIGNATURE_LEN);
	newhdr->major           = HEAP_MAJOR;
	newhdr->minor           = HEAP_MINOR;
	newhdr->heap_size       = heap_size;
	newhdr->cache_size      = umem_cache_size;
	newhdr->heap_hdr_size   = sizeof(struct heap_header);
	newhdr->chunksize       = CHUNKSIZE;
	newhdr->chunks_per_zone = MAX_CHUNK;
	newhdr->nemb_pct        = (uint8_t)nemb_pct;
	newhdr->checksum        = 0;

	util_checksum(newhdr, sizeof(*newhdr), &newhdr->checksum, 1, 0);
	rc = meta_update(store, newhdr, 0, sizeof(*newhdr));
	D_FREE(newhdr);

	return rc;
}

/*
 * heap_cleanup -- cleanups the volatile heap state
 */
void
heap_cleanup(struct palloc_heap *heap)
{
	struct heap_rt *rt = heap->rt;
	unsigned        i;

	alloc_class_collection_delete(rt->alloc_classes);

	for (i = 0; i < rt->nlocks; ++i)
		util_mutex_destroy(&rt->run_locks[i]);

#if VG_MEMCHECK_ENABLED
	VALGRIND_DO_DESTROY_MEMPOOL(heap->layout_info.zone0);
	if (On_memcheck) {
		for (i = 0; i < heap->rt->zones_exhausted; i++) {
			if (!heap_mbrt_ismb_initialized(heap, i) ||
			    !heap_mbrt_ismb_localrt(heap, i))
				continue;
			if (umem_cache_offisloaded(heap->layout_info.store, GET_ZONE_OFFSET(i)))
				VALGRIND_DO_DESTROY_MEMPOOL(ZID_TO_ZONE(&heap->layout_info, i));
		}
	}
#endif
	heap_mbrt_fini(heap);
	soemb_fini(&heap->rt->smbrt);

	D_FREE(rt);
	heap->rt = NULL;
}

/*
 * heap_verify_header -- (internal) verifies if the heap header is consistent
 */
static int
heap_verify_header(struct heap_header *hdr, size_t heap_size, size_t cache_size)
{
	if (util_checksum(hdr, sizeof(*hdr), &hdr->checksum, 0, 0) != 1) {
		D_CRIT("heap: invalid header's checksum\n");
		return -1;
	}

	if ((hdr->major != HEAP_MAJOR) || (hdr->minor > HEAP_MINOR)) {
		D_ERROR("Version mismatch of heap layout\n");
		return -1;
	}

	if (hdr->heap_size != heap_size) {
		D_ERROR("Metadata store size mismatch, created with %lu , opened with %lu\n",
			hdr->heap_size, heap_size);
		return -1;
	}

	if (hdr->cache_size != cache_size) {
		D_ERROR("umem cache size mismatch, created with %lu , opened with %lu\n",
			hdr->cache_size, cache_size);
		return -1;
	}

	if (hdr->nemb_pct > 100) {
		D_ERROR("nemb pct value (%d) in heap header is incorrect\n", hdr->nemb_pct);
		return -1;
	}

	if ((hdr->heap_hdr_size != sizeof(struct heap_header)) || (hdr->chunksize != CHUNKSIZE) ||
	    (hdr->chunks_per_zone != MAX_CHUNK)) {
		D_ERROR("incompatible heap layout: hdr_sz=%lu, chunk_sz=%lu, max_chunks=%lu\n",
			hdr->heap_hdr_size, hdr->chunksize, hdr->chunks_per_zone);
		return -1;
	}

	return 0;
}

int
heap_zone_load(struct palloc_heap *heap, uint32_t zid)
{
	struct umem_cache_range rg    = {0};
	struct umem_store      *store = heap->layout_info.store;
	int                     rc;

	D_ASSERT(heap->rt->nzones > zid);

	rg.cr_off  = GET_ZONE_OFFSET(zid);
	rg.cr_size = ((store->stor_size - rg.cr_off) > ZONE_MAX_SIZE)
			 ? ZONE_MAX_SIZE
			 : (store->stor_size - rg.cr_off);
	rc         = umem_cache_load(store, &rg, 1, 0);
	if (rc) {
		D_ERROR("Failed to load pages to umem cache");
		return daos_der2errno(rc);
	}
	return 0;
}

int
heap_ensure_zone0_initialized(struct palloc_heap *heap)
{
	struct mbrt   *mb;
	struct bucket *b;
	int            rc = 0;

	heap_mbrt_setmb_nonevictable(heap, NULL, 0);
	if (heap->layout_info.zone0->header.magic != ZONE_HEADER_MAGIC) {
		/* If not magic the content should be zero, indicating new file */
		D_ASSERT(heap->layout_info.zone0->header.magic == 0);
		mb = heap_mbrt_get_mb(heap, 0);
		b  = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
		rc = heap_populate_bucket(heap, b);
		mbrt_bucket_release(b);
	}
#if VG_MEMCHECK_ENABLED
	else {
		if (On_memcheck)
			palloc_heap_vg_zone_open(heap, 0, 1);
	}
#endif
	heap_mbrt_setmb_usage(heap, 0, heap->layout_info.zone0->header.sp_usage);
	return rc;
}

D_CASSERT(sizeof(struct zone) == 4096);
D_CASSERT(sizeof(struct heap_header) == 4096);

#define MAX_HEADER_FETCH 4

/*
 * heap_boot -- opens the heap region of the dav_obj pool
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_boot(struct palloc_heap *heap, void *mmap_base, uint64_t heap_size, uint64_t cache_size,
	  struct mo_ops *p_ops, struct stats *stats)
{
	struct heap_rt         *h;
	struct heap_header     *newhdr;
	int                     err;
	struct heap_zone_limits hzl;
	uint32_t                nemb_pct = HEAP_NEMB_PCT_DEFAULT;

	D_ALLOC_PTR(newhdr);
	if (!newhdr)
		return ENOMEM;

	err = meta_fetch(p_ops->umem_store, newhdr, 0, sizeof(*newhdr));
	if (err) {
		ERR("failed to read the heap header");
		D_FREE(newhdr);
		return err;
	}
	err = heap_verify_header(newhdr, heap_size, cache_size);
	if (err) {
		ERR("incompatible heap detected");
		D_FREE(newhdr);
		return EINVAL;
	}
	if (newhdr->nemb_pct)
		nemb_pct = newhdr->nemb_pct;
	D_FREE(newhdr);

	D_ALLOC_PTR_NZ(h);
	if (h == NULL) {
		err = ENOMEM;
		goto error_heap_malloc;
	}

	h->alloc_classes = alloc_class_collection_new();
	if (h->alloc_classes == NULL) {
		err = ENOMEM;
		goto error_alloc_classes_new;
	}

	hzl = heap_get_zone_limits(heap_size, cache_size, nemb_pct);

	h->nzones             = hzl.nzones_heap;
	h->nzones_ne          = hzl.nzones_ne_max;
	h->nzones_e           = hzl.nzones_e_max;
	h->zones_exhausted    = 0;
	h->zones_exhausted_e  = 0;
	h->zones_exhausted_ne = 0;
	h->zones_nextne_gc    = 0;
	h->zones_unused_first = 0;
	h->zinfo_vec          = NULL;

	h->nlocks = On_valgrind ? MAX_RUN_LOCKS_VG : MAX_RUN_LOCKS;
	for (unsigned i = 0; i < h->nlocks; ++i)
		util_mutex_init(&h->run_locks[i]);

	soemb_init(&h->smbrt);

	heap->rt = h;

	heap->p_ops = *p_ops;
	heap->layout_info.store = p_ops->umem_store;
	heap->layout_info.zone0 = mmap_base;
	heap->size              = heap_size;
	heap->base              = mmap_base;
	heap->stats             = stats;
	heap->alloc_pattern = PALLOC_CTL_DEBUG_NO_PATTERN;
	VALGRIND_DO_CREATE_MEMPOOL(heap->layout_info.zone0, 0, 0);

	err = heap_mbrt_init(heap);
	if (err)
		goto error_mbrt_init;

	return 0;

error_mbrt_init:
	alloc_class_collection_delete(h->alloc_classes);
error_alloc_classes_new:
	D_FREE(h);
	heap->rt = NULL;
error_heap_malloc:
	return err;
}

static unsigned int
heap_get_nemb_pct()
{
	unsigned int nemb_pct;

	nemb_pct = HEAP_NEMB_PCT_DEFAULT;
	d_getenv_uint("DAOS_MD_ON_SSD_NEMB_PCT", &nemb_pct);
	if ((nemb_pct > 100) || (nemb_pct == 0)) {
		D_ERROR("Invalid value %d for tunable DAOS_MD_ON_SSD_NEMB_PCT", nemb_pct);
		nemb_pct = HEAP_NEMB_PCT_DEFAULT;
	}
	D_INFO("DAOS_MD_ON_SSD_NEMB_PCT set to %d", nemb_pct);

	return nemb_pct;
}

int
heap_get_max_nemb(struct palloc_heap *heap)
{
	return heap->rt->nzones_ne;
}

/*
 * heap_init -- initializes the heap
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_init(void *heap_start, uint64_t umem_cache_size, struct umem_store *store)
{
	int      nzones;
	uint32_t nemb_pct  = heap_get_nemb_pct();
	uint64_t heap_size = store->stor_size;

	if (heap_size < HEAP_MIN_SIZE)
		return EINVAL;

	D_ASSERT(store->stor_priv != NULL);

	nzones = heap_max_zone(heap_size);
	meta_clear_pages(store, sizeof(struct heap_header), 4096, ZONE_MAX_SIZE, nzones);

	if (heap_write_header(store, heap_size, umem_cache_size, nemb_pct))
		return ENOMEM;

	return 0;
}

static inline int
heap_create_evictable_mb(struct palloc_heap *heap, uint32_t *mb_id)
{
	uint32_t                zone_id;
	struct umem_cache_range rg = {0};
	int                     rc;
	struct zone            *z;
	struct umem_pin_handle *pin_handle = NULL;
	struct umem_store      *store      = heap->layout_info.store;
	struct mbrt            *mb;

	D_ASSERT(heap->rt->active_evictable_mb == NULL);

	if (heap->rt->zones_exhausted_e >= heap->rt->nzones_e)
		return -1;

	heap->rt->mb_create_waiters++;
	if (heap->rt->mb_create_waiters > 1) {
		D_ASSERT(store->stor_ops->so_waitqueue_wait != NULL);
		store->stor_ops->so_waitqueue_wait(heap->rt->mb_create_wq, false);
		D_ASSERT((int)heap->rt->mb_create_waiters >= 0);
		rc    = 1;
		errno = EBUSY;
		goto out;
	}

	rc = heap_get_next_unused_zone(heap, &zone_id);
	if (rc) {
		D_ERROR("Failed to obtain free zone for evictable mb");
		rc    = 1;
		errno = ENOMEM;
		goto out;
	}

	mb = heap_mbrt_setup_mb(heap, zone_id);
	if (mb == NULL) {
		ERR("Failed to setup mbrt for zone %u\n", zone_id);
		rc    = 1;
		errno = ENOMEM;
		goto out;
	}

	heap_mark_zone_used_transient(heap, mb, zone_id, true);

	/* Create a umem cache map for the new zone */
	rg.cr_off = GET_ZONE_OFFSET(zone_id);
	rg.cr_size =
	    ((heap->size - rg.cr_off) > ZONE_MAX_SIZE) ? ZONE_MAX_SIZE : heap->size - rg.cr_off;

	rc = umem_cache_map(heap->layout_info.store, &rg, 1);
	if (rc != 0) {
		ERR("Failed to map zone %u to umem cache\n", zone_id);
		errno = daos_der2errno(rc);
		goto error;
	}

	D_DEBUG(DB_TRACE, "Creating evictable zone %d\n", zone_id);

	z = ZID_TO_ZONE(&heap->layout_info, zone_id);
	VALGRIND_DO_CREATE_MEMPOOL(z, 0, 0);
	VALGRIND_DO_MAKE_MEM_UNDEFINED(z, rg.cr_size);
	if (rg.cr_size != ZONE_MAX_SIZE)
		VALGRIND_DO_MAKE_MEM_NOACCESS(z + rg.cr_size, (ZONE_MAX_SIZE - rg.cr_size));

	memset(z, 0, rg.cr_size);

	rc = umem_cache_pin(heap->layout_info.store, &rg, 1, false, &pin_handle);
	if (rc) {
		errno = daos_der2errno(rc);
		goto error;
	}

	/* ignore zone and chunk headers */
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(z, sizeof(z->header) + sizeof(z->chunk_headers));

	rc = lw_tx_begin(heap->p_ops.base);
	if (rc)
		goto error;

	heap_zone_init(heap, zone_id, 0, ZONE_EVICTABLE_MB);
	rc = heap_mbrt_mb_reclaim_garbage(heap, zone_id);
	if (rc) {
		ERR("Failed to initialize evictable zone %u", zone_id);
		lw_tx_end(heap->p_ops.base, NULL);
		goto error;
	}
	heap_mark_zone_used_persist(heap, zone_id);
	lw_tx_end(heap->p_ops.base, NULL);
	umem_cache_unpin(heap->layout_info.store, pin_handle);

	*mb_id = zone_id;
	rc     = 0;
	goto out;

error:
	if (pin_handle)
		umem_cache_unpin(heap->layout_info.store, pin_handle);
	heap_mark_zone_unused_transient(heap, zone_id);
	heap_mbrt_cleanup_mb(mb);
	rc = -1;

out:
	heap->rt->mb_create_waiters--;
	D_ASSERT((int)heap->rt->mb_create_waiters >= 0);
	if (heap->rt->mb_create_waiters) {
		D_ASSERT(store->stor_ops->so_waitqueue_wakeup != NULL);
		store->stor_ops->so_waitqueue_wakeup(heap->rt->mb_create_wq, false);
	}
	return rc;
}

static int
heap_create_soe_mb(struct palloc_heap *heap, uint32_t *mb_id)
{
	uint32_t                zone_id;
	struct umem_cache_range rg = {0};
	int                     rc;
	struct zone            *z;
	struct mbrt            *mb;

	if (heap->rt->zones_exhausted_ne >= heap->rt->nzones_ne)
		return -1;

	rc = heap_get_next_unused_zone(heap, &zone_id);
	if (rc) {
		D_ERROR("Failed to obtain free zone for evictable mb");
		rc    = 1;
		errno = ENOMEM;
		goto out;
	}

	mb = heap_mbrt_setup_mb(heap, zone_id);
	if (mb == NULL) {
		ERR("Failed to setup mbrt for zone %u\n", zone_id);
		rc    = 1;
		errno = ENOMEM;
		goto out;
	}

	heap_mark_zone_used_transient(heap, mb, zone_id, false);

	/* Create a umem cache map for the new zone */
	rg.cr_off = GET_ZONE_OFFSET(zone_id);
	rg.cr_size =
	    ((heap->size - rg.cr_off) > ZONE_MAX_SIZE) ? ZONE_MAX_SIZE : heap->size - rg.cr_off;

	rc = umem_cache_map(heap->layout_info.store, &rg, 1);
	if (rc != 0) {
		ERR("Failed to map zone %u to umem cache\n", zone_id);
		errno = daos_der2errno(rc);
		goto error;
	}

	D_DEBUG(DB_TRACE, "Creating evictable zone %d\n", zone_id);

	z = ZID_TO_ZONE(&heap->layout_info, zone_id);
	VALGRIND_DO_CREATE_MEMPOOL(z, 0, 0);
	VALGRIND_DO_MAKE_MEM_UNDEFINED(z, rg.cr_size);
	if (rg.cr_size != ZONE_MAX_SIZE)
		VALGRIND_DO_MAKE_MEM_NOACCESS(z + rg.cr_size, (ZONE_MAX_SIZE - rg.cr_size));

	memset(z, 0, rg.cr_size);

	/* ignore zone and chunk headers */
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(z, sizeof(z->header) + sizeof(z->chunk_headers));

	heap_zone_init(heap, zone_id, 0, ZONE_SOE_MB);
	rc = heap_mbrt_mb_reclaim_garbage(heap, zone_id);
	if (rc) {
		ERR("Failed to initialize evictable zone %u", zone_id);
		goto error;
	}
	heap_mark_zone_used_persist(heap, zone_id);

	*mb_id = zone_id;
	rc     = 0;
	heap_incr_empty_nemb_cnt(heap);
	heap->rt->soemb_cnt++;
	goto out;

error:
	heap_mark_zone_unused_transient(heap, zone_id);
	heap_mbrt_cleanup_mb(mb);
	rc = -1;

out:
	return rc;
}

int
heap_get_evictable_mb(struct palloc_heap *heap, uint32_t *mb_id)
{
	struct mbrt       *mb;
	int                ret;

retry:
	if (heap->rt->active_evictable_mb != NULL) {
		if ((heap->rt->mb_pressure) ||
		    (heap->rt->active_evictable_mb->space_usage <= MB_U75)) {
			*mb_id = heap->rt->active_evictable_mb->mb_id;
			return 0;
		}
		mb                            = heap->rt->active_evictable_mb;
		heap->rt->active_evictable_mb = NULL;
		heap_mbrt_setmb_usage(heap, mb->mb_id, mb->space_usage);
	}
	heap->rt->mb_pressure = 0;

	mb = mbrt_qbs_getmb(&heap->rt->emb_qbs, 0);
	if (mb)
		goto out;

	if ((ret = heap_create_evictable_mb(heap, mb_id)) >= 0) {
		if (ret)
			goto retry;
		mb = heap_mbrt_get_mb(heap, *mb_id);
		D_ASSERT(mb != NULL);
		if (heap->rt->active_evictable_mb) {
			mbrt_qbs_insertmb(&heap->rt->emb_qbs, mb);
			*mb_id   = heap->rt->active_evictable_mb->mb_id;
			return 0;
		}
		goto out;
	}
	mb = mbrt_qbs_getmb(&heap->rt->emb_qbs, 1);

	heap->rt->mb_pressure = 1;

	if (mb == NULL) {
		D_ERROR("Failed to get an evictable MB");
		*mb_id = 0;
		return 0;
	}
out:
	heap->rt->active_evictable_mb = mb;
	*mb_id                        = mb->mb_id;
	return 0;
}

uint32_t
heap_off2mbid(struct palloc_heap *heap, uint64_t offset)
{
	struct memory_block m = memblock_from_offset_opt(heap, offset, 0);

	if (heap_mbrt_ismb_localrt(heap, m.zone_id))
		return m.zone_id;
	else
		return 0;
}

int
heap_update_mbrt_zinfo(struct palloc_heap *heap, bool init)
{
	bool               allotted, evictable;
	struct zone       *z0       = heap->layout_info.zone0;
	int                nemb_cnt = 1, emb_cnt = 0, i;
	struct mbrt       *mb;
	struct zone       *z;
	enum mb_usage_hint usage_hint;
	int                last_allocated = 0;

	heap->rt->zinfo_vec      = HEAP_OFF_TO_PTR(heap, z0->header.zone0_zinfo_off);
	heap->rt->zinfo_vec_size = z0->header.zone0_zinfo_size;

	if (init)
		heap_zinfo_init(heap);
	else {
		D_ASSERT(heap->rt->zinfo_vec->num_elems == heap->rt->nzones);
		heap_zinfo_get(heap, 0, &allotted, &evictable);
		D_ASSERT((evictable == false) && (allotted == true));
	}

	for (i = 1; i < heap->rt->nzones; i++) {
		heap_zinfo_get(heap, i, &allotted, &evictable);
		if (!allotted) {
			if (!heap->rt->zones_unused_first)
				heap->rt->zones_unused_first = i;
			continue;
		}
		if (!evictable) {
			heap_mbrt_setmb_nonevictable(heap, NULL, i);
			nemb_cnt++;
		} else {
			mb = heap_mbrt_setup_mb(heap, i);
			if (mb == NULL)
				return ENOMEM;
			heap_mbrt_setmb_evictable(heap, mb);
			if (umem_cache_offisloaded(heap->layout_info.store, GET_ZONE_OFFSET(i))) {
				z = ZID_TO_ZONE(&heap->layout_info, i);
				D_ASSERT(z->header.flags & ZONE_EVICTABLE_MB);
				heap_mbrt_setmb_usage(heap, i, z->header.sp_usage);
			} else {
				heap_zinfo_get_usage(heap, i, &usage_hint);
				heap_mbrt_setmb_usage(heap, i, mb_usage_byhint[(int)usage_hint]);
			}
			emb_cnt++;
		}
		last_allocated = i;
	}
	heap->rt->zones_exhausted    = last_allocated + 1;
	heap->rt->zones_exhausted_ne = nemb_cnt;
	heap->rt->zones_exhausted_e  = emb_cnt;

	D_ASSERT(heap->rt->nzones_e >= heap->rt->zones_exhausted_e);
	D_ASSERT(heap->rt->nzones_ne >= heap->rt->zones_exhausted_ne);
	return 0;
}

/*
 * heap_load_nonevictable_zones() -> Populate the heap with non-evictable MBs.
 */
int
heap_load_nonevictable_zones(struct palloc_heap *heap)
{
	int          i, rc;
	bool         allotted, evictable;
	struct zone *zone;
	struct mbrt *mb;

	for (i = 1; i < heap->rt->zones_exhausted; i++) {
		heap_zinfo_get(heap, i, &allotted, &evictable);
		if (!allotted)
			continue;
		if (!evictable) {
			rc = heap_zone_load(heap, i);
			if (rc)
				return rc;
			zone = ZID_TO_ZONE(&heap->layout_info, i);
			D_ASSERT((zone->header.flags & ZONE_EVICTABLE_MB) == 0);
			if (zone->header.flags & ZONE_SOE_MB) {
				mb = heap_mbrt_setup_mb(heap, i);
				if (mb == NULL) {
					D_ERROR("failed to load soe mb");
					return ENOMEM;
				}
				heap_mbrt_setmb_nonevictable(heap, mb, i);
				mbrt_qbs_insertmb(&heap->rt->smbrt.qbs, mb);
				heap->rt->soemb_cnt++;
			}
			if (!zone->header.sp_usage)
				heap_incr_empty_nemb_cnt(heap);
			heap_mbrt_incrmb_usage(heap, i, zone->header.sp_usage);
		}
	}
	return 0;
}

#if 0
/*
 * heap_verify_zone_header --
 *	(internal) verifies if the zone header is consistent
 */
static int
heap_verify_zone_header(struct zone_header *hdr)
{
	if (hdr->magic != ZONE_HEADER_MAGIC) /* not initialized */
		return 0;

	if (hdr->size_idx == 0) {
		D_CRIT("heap: invalid zone size\n");
		return -1;
	}

	return 0;
}

/*
 * heap_verify_chunk_header --
 *	(internal) verifies if the chunk header is consistent
 */
static int
heap_verify_chunk_header(struct chunk_header *hdr)
{
	if (hdr->type == CHUNK_TYPE_UNKNOWN) {
		D_CRIT("heap: invalid chunk type\n");
		return -1;
	}

	if (hdr->type >= MAX_CHUNK_TYPE) {
		D_CRIT("heap: unknown chunk type\n");
		return -1;
	}

	if (hdr->flags & ~CHUNK_FLAGS_ALL_VALID) {
		D_CRIT("heap: invalid chunk flags\n");
		return -1;
	}

	return 0;
}

/*
 * heap_verify_zone -- (internal) verifies if the zone is consistent
 */
static int
heap_verify_zone(struct zone *zone)
{
	if (zone->header.magic == 0)
		return 0; /* not initialized, and that is OK */

	if (zone->header.magic != ZONE_HEADER_MAGIC) {
		D_CRIT("heap: invalid zone magic\n");
		return -1;
	}

	if (heap_verify_zone_header(&zone->header))
		return -1;

	uint32_t i;

	for (i = 0; i < zone->header.size_idx; ) {
		if (heap_verify_chunk_header(&zone->chunk_headers[i]))
			return -1;

		i += zone->chunk_headers[i].size_idx;
	}

	if (i != zone->header.size_idx) {
		D_CRIT("heap: chunk sizes mismatch\n");
		return -1;
	}

	return 0;
}

/*
 * heap_check -- verifies if the heap is consistent and can be opened properly
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_check(void *heap_start, uint64_t heap_size)
{
	if (heap_size < HEAP_MIN_SIZE) {
		D_CRIT("heap: invalid heap size\n");
		return -1;
	}

	struct heap_layout *layout = heap_start;

	if (heap_verify_header(&layout->header, heap_size))
		return -1;

	for (unsigned i = 0; i < heap_max_zone(heap_size); ++i) {
		if (heap_verify_zone(ZID_TO_ZONE(layout, i)))
			return -1;
	}

	return 0;
}
#endif

/*
 * heap_zone_foreach_object -- (internal) iterates through objects in a zone
 */
static int
heap_zone_foreach_object(struct palloc_heap *heap, object_callback cb,
	void *arg, struct memory_block *m)
{
	struct zone *zone = ZID_TO_ZONE(&heap->layout_info, m->zone_id);

	if (zone->header.magic == 0)
		return 0;

	for (; m->chunk_id < zone->header.size_idx; ) {
		struct chunk_header *hdr = heap_get_chunk_hdr(heap, m);

		memblock_rebuild_state(heap, m);
		m->size_idx = hdr->size_idx;

		if (m->m_ops->iterate_used(m, cb, arg) != 0)
			return 1;

		m->chunk_id += m->size_idx;
		m->block_off = 0;
	}

	return 0;
}

/*
 * heap_foreach_object -- (internal) iterates through objects in the heap
 */
void
heap_foreach_object(struct palloc_heap *heap, object_callback cb, void *arg,
	struct memory_block m)
{
	for (; m.zone_id < heap->rt->nzones; ++m.zone_id) {
		if (heap_zone_foreach_object(heap, cb, arg, &m) != 0)
			break;

		m.chunk_id = 0;
	}
}

struct heap_zone_limits
heap_get_zone_limits(uint64_t heap_size, uint64_t cache_size, uint32_t nemb_pct)
{
	struct heap_zone_limits zd = {0};

	D_ASSERT(nemb_pct <= 100);

	if (heap_size < sizeof(struct heap_header))
		zd.nzones_heap = 0;
	else
		zd.nzones_heap = heap_max_zone(heap_size);

	zd.nzones_cache = cache_size / ZONE_MAX_SIZE;

	if (!zd.nzones_heap || !zd.nzones_cache)
		return zd;

	if (zd.nzones_heap <= zd.nzones_cache) {
		zd.nzones_ne_max = zd.nzones_heap;
		return zd;
	}

	if (zd.nzones_cache <= UMEM_CACHE_MIN_PAGES) {
		zd.nzones_ne_max = zd.nzones_cache;
		return zd;
	}

	zd.nzones_ne_max = (((unsigned long)zd.nzones_cache * nemb_pct) / 100);
	if (!zd.nzones_ne_max)
		zd.nzones_ne_max = UMEM_CACHE_MIN_PAGES;

	zd.nzones_e_max = zd.nzones_heap - zd.nzones_ne_max;

	return zd;
}

int
heap_incr_empty_nemb_cnt(struct palloc_heap *heap)
{
	return ++heap->rt->empty_nemb_cnt;
}

int
heap_decr_empty_nemb_cnt(struct palloc_heap *heap)
{
	return heap->rt->empty_nemb_cnt ? --heap->rt->empty_nemb_cnt : 0;
}

static void
heap_recycle_soembs(struct palloc_heap *heap)
{
	struct mbrt        *mb;
	struct bucket      *defb, *b;
	struct memory_block m = MEMORY_BLOCK_NONE;
	int                 i, rc;

	for (i = 0; i < SOEMB_ACTIVE_CNT; i++) {
		mb = heap->rt->smbrt.svec[i];
		if (mb && (mb->space_usage == 0)) {
			mbrt_qbs_insertmb(&heap->rt->smbrt.qbs, mb);
			heap->rt->smbrt.svec[i] = NULL;
		}
	}

	while ((mb = mbrt_qbs_getmb_ue(&heap->rt->smbrt.qbs)) != NULL) {
		defb = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
		if (!mb->garbage_reclaimed) {
			heap_reclaim_zone_garbage(heap, defb, mb->mb_id);
			mb->garbage_reclaimed = 1;
		}
		mbrt_bucket_release(defb);
		for (i = 0; i < MAX_ALLOCATION_CLASSES; i++) {
			if (mb->buckets[i] == NULL)
				continue;
			b = bucket_acquire(mb->buckets[i]);
			heap_detach_and_try_discard_run(heap, b);
			mbrt_bucket_release(b);
		}
		defb = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
		heap_reclaim_garbage(heap, defb);
		m          = MEMORY_BLOCK_NONE;
		m.size_idx = MAX_CHUNK;
		if (bucket_alloc_block(defb, &m) == 0) {
			rc = heap_mark_zone_unused(heap, m.zone_id);
			if (rc)
				mbrt_qbs_insertmb_force(&heap->rt->smbrt.qbs, mb, MB_U0_HINT);
			else
				heap->rt->empty_nemb_cnt--;
			mbrt_bucket_release(defb);
			heap_mbrt_cleanup_mb(mb);
		} else {
			mbrt_bucket_release(defb);
			mbrt_qbs_insertmb_force(&heap->rt->smbrt.qbs, mb, MB_U0_HINT);
		}
	}

	return;
}

int
heap_force_recycle(struct palloc_heap *heap)
{
	struct bucket *defb;
	struct mbrt   *mb;
	uint32_t       zone_id;
	uint32_t       max_reclaim = heap->rt->empty_nemb_gcth * 2;

	mb   = heap_mbrt_get_mb(heap, 0);

	if (heap->rt->empty_nemb_cnt < heap->rt->empty_nemb_gcth) {
		if ((mb->space_usage > mb->prev_usage) ||
		    ((mb->prev_usage - mb->space_usage) <
		     (ZONE_MAX_SIZE * heap->rt->empty_nemb_gcth))) {
			if (mb->space_usage > mb->prev_usage)
				mb->prev_usage = mb->space_usage;
			return 0;
		}
	}

	heap_recycle_soembs(heap);

	defb = mbrt_bucket_acquire(mb, DEFAULT_ALLOC_CLASS_ID);
	while (heap_reclaim_next_ne(heap, &zone_id) == 0) {
		heap_reclaim_zone_garbage(heap, defb, zone_id);
		heap_reclaim_setlast_ne(heap, zone_id);
		if (--max_reclaim == 0)
			break;
	}

	heap_reclaim_garbage(heap, defb);
	mbrt_bucket_release(defb);
	heap_populate_nemb_unused(heap);
	mb->prev_usage = mb->space_usage;

	return 0;
}

#if VG_MEMCHECK_ENABLED
void
heap_vg_zone_open(struct palloc_heap *heap, uint32_t zone_id, object_callback cb, void *args,
		  int objects)
{
	struct memory_block  m = MEMORY_BLOCK_NONE;
	uint32_t             chunks;
	struct chunk_header *hdr;
	struct zone         *z = ZID_TO_ZONE(&heap->layout_info, zone_id);
	uint32_t             c;

	m.zone_id  = zone_id;
	m.chunk_id = 0;

	VALGRIND_DO_MAKE_MEM_UNDEFINED(z, ZONE_MAX_SIZE);

	VALGRIND_DO_MAKE_MEM_DEFINED(&z->header, sizeof(z->header));

	D_ASSERT(z->header.magic == ZONE_HEADER_MAGIC);

	chunks = z->header.size_idx;

	for (c = 0; c < chunks;) {
		hdr = &z->chunk_headers[c];

		/* define the header before rebuilding state */
		VALGRIND_DO_MAKE_MEM_DEFINED(hdr, sizeof(*hdr));

		m.chunk_id = c;
		m.size_idx = hdr->size_idx;

		memblock_rebuild_state(heap, &m);

		m.m_ops->vg_init(&m, objects, cb, args);
		m.block_off = 0;

		ASSERT(hdr->size_idx > 0);

		c += hdr->size_idx;
	}

	/* mark all unused chunk headers after last as not accessible */
	VALGRIND_DO_MAKE_MEM_NOACCESS(&z->chunk_headers[chunks],
				      (MAX_CHUNK - chunks) * sizeof(struct chunk_header));
}

/*
 * heap_vg_open -- notifies Valgrind about heap layout
 */
void
heap_vg_open(struct palloc_heap *heap, object_callback cb, void *arg, int objects)
{
	unsigned zones = heap_max_zone(heap->size);

	ASSERTne(cb, NULL);

	for (unsigned i = 1; i < zones; ++i) {
		if (!umem_cache_offisloaded(heap->layout_info.store, GET_ZONE_OFFSET(i)))
			continue;

		if (!heap_mbrt_ismb_initialized(heap, i))
			continue;

		if (heap_mbrt_ismb_localrt(heap, i))
			VALGRIND_DO_CREATE_MEMPOOL(ZID_TO_ZONE(&heap->layout_info, i), 0, 0);

		heap_vg_zone_open(heap, i, cb, arg, objects);
	}
}
#endif
