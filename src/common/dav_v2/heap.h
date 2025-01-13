/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2024, Intel Corporation */

/*
 * heap.h -- internal definitions for heap
 */

#ifndef __DAOS_COMMON_HEAP_H
#define __DAOS_COMMON_HEAP_H 1

#include <stddef.h>
#include <stdint.h>

#include "memblock.h"
#include "bucket.h"
#include "memops.h"
#include "palloc.h"
#include "dav_internal.h"
#include <daos/mem.h>

#define HEAP_OFF_TO_PTR(heap, off) umem_cache_off2ptr(heap->layout_info.store, off)
#define HEAP_PTR_TO_OFF(heap, ptr) umem_cache_ptr2off(heap->layout_info.store, ptr)

#define BIT_IS_CLR(a, i)           (!((a) & (1ULL << (i))))
#define HEAP_ARENA_PER_THREAD      (0)

struct mbrt;

int
heap_boot(struct palloc_heap *heap, void *mmap_base, uint64_t heap_size, uint64_t cache_size,
	  struct mo_ops *p_ops, struct stats *stats);
int
heap_init(void *heap_start, uint64_t umem_cache_size, struct umem_store *store);
void
heap_cleanup(struct palloc_heap *heap);
int
heap_check(void *heap_start, uint64_t heap_size);
int
heap_get_max_nemb(struct palloc_heap *heap);
int
heap_create_alloc_class_buckets(struct palloc_heap *heap, struct alloc_class *c);
int
heap_mbrt_update_alloc_class_buckets(struct palloc_heap *heap, struct mbrt *mb,
				     struct alloc_class *c);
int
heap_extend(struct palloc_heap *heap, struct bucket *defb, size_t size);
bool
heap_mbrt_ismb_evictable(struct palloc_heap *heap, uint32_t zone_id);
void
heap_mbrt_setmb_usage(struct palloc_heap *heap, uint32_t zone_id, uint64_t usage);
int
heap_mbrt_getmb_usage(struct palloc_heap *heap, uint32_t zone_id, uint64_t *allotted,
		      uint64_t *maxsz);
void
heap_mbrt_incrmb_usage(struct palloc_heap *heap, uint32_t zone_id, int size);
void
heap_soemb_active_iter_init(struct palloc_heap *heap);
uint32_t
heap_soemb_active_get(struct palloc_heap *heap);
void
heap_soemb_reserve(struct palloc_heap *heap);
int
heap_ensure_zone0_initialized(struct palloc_heap *heap);
int
heap_zone_load(struct palloc_heap *heap, uint32_t zid);
int
heap_load_nonevictable_zones(struct palloc_heap *heap);
int
heap_update_mbrt_zinfo(struct palloc_heap *heap, bool init);
size_t
heap_zinfo_get_size(uint32_t nzones);

struct alloc_class *
heap_get_best_class(struct palloc_heap *heap, size_t size);
struct bucket *
mbrt_bucket_acquire(struct mbrt *mb, uint8_t class_id);
void
mbrt_bucket_release(struct bucket *b);
void
heap_set_root_ptrs(struct palloc_heap *heap, uint64_t **offp, uint64_t **sizep);
void
heap_set_stats_ptr(struct palloc_heap *heap, struct stats_persistent **sp);

int
heap_get_bestfit_block(struct palloc_heap *heap, struct bucket *b, struct memory_block *m);
pthread_mutex_t *
heap_get_run_lock(struct palloc_heap *heap, uint32_t chunk_id);

void
heap_discard_run(struct palloc_heap *heap, struct memory_block *m);

void
heap_memblock_on_free(struct palloc_heap *heap, const struct memory_block *m);

int
heap_free_chunk_reuse(struct palloc_heap *heap, struct bucket *bucket, struct memory_block *m);

void
heap_foreach_object(struct palloc_heap *heap, object_callback cb, void *arg,
		    struct memory_block start);

struct alloc_class_collection *
heap_alloc_classes(struct palloc_heap *heap);

void
heap_vg_open(struct palloc_heap *heap, object_callback cb, void *arg, int objects);
void
heap_vg_zone_open(struct palloc_heap *heap, uint32_t zone_id, object_callback cb, void *arg,
		  int objects);

static inline struct chunk_header *
heap_get_chunk_hdr(struct palloc_heap *heap, const struct memory_block *m)
{
	return GET_CHUNK_HDR(&heap->layout_info, m->zone_id, m->chunk_id);
}

static inline struct chunk *
heap_get_chunk(struct palloc_heap *heap, const struct memory_block *m)
{
	return GET_CHUNK(&heap->layout_info, m->zone_id, m->chunk_id);
}

static inline struct chunk_run *
heap_get_chunk_run(struct palloc_heap *heap, const struct memory_block *m)
{
	return GET_CHUNK_RUN(&heap->layout_info, m->zone_id, m->chunk_id);
}

struct mbrt *
heap_mbrt_get_mb(struct palloc_heap *heap, uint32_t zone_id);

void
heap_mbrt_log_alloc_failure(struct palloc_heap *heap, uint32_t zone_id);

int
heap_get_evictable_mb(struct palloc_heap *heap, uint32_t *zone_id);

struct heap_zone_limits {
	unsigned nzones_heap;
	unsigned nzones_cache;
	unsigned nzones_ne_max;
	unsigned nzones_e_max;
};

uint32_t
heap_off2mbid(struct palloc_heap *heap, uint64_t offset);

struct heap_zone_limits
heap_get_zone_limits(uint64_t heap_size, uint64_t cache_size, uint32_t nemb_pct);

int
heap_force_recycle(struct palloc_heap *heap);
int
heap_incr_empty_nemb_cnt(struct palloc_heap *heap);
int
heap_decr_empty_nemb_cnt(struct palloc_heap *heap);
#endif /* __DAOS_COMMON_HEAP_H */
