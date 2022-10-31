/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2020, Intel Corporation */

/*
 * palloc.h -- internal definitions for persistent allocator
 */

#ifndef __DAOS_COMMON_PALLOC_H
#define __DAOS_COMMON_PALLOC_H 1

#include <stddef.h>
#include <stdint.h>

#include "memops.h"
#include "ulog.h"
#include "valgrind_internal.h"
#include "stats.h"
#include "dav.h"

#define PALLOC_CTL_DEBUG_NO_PATTERN (-1)

struct palloc_heap {
	struct mo_ops p_ops;
	struct heap_layout *layout;
	struct heap_rt *rt;
	uint64_t *sizep;
	uint64_t growsize;

	struct stats *stats;
	struct pool_set *set;

	void *base;

	int alloc_pattern;
};

struct memory_block;

typedef int (*palloc_constr)(void *base, void *ptr,
		size_t usable_size, void *arg);

int palloc_operation(struct palloc_heap *heap, uint64_t off, uint64_t *dest_off,
	size_t size, palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags,
	uint16_t class_id, uint16_t arena_id,
	struct operation_context *ctx);

int
palloc_reserve(struct palloc_heap *heap, size_t size,
	       palloc_constr constructor, void *arg,
	       uint64_t extra_field, uint16_t object_flags,
	       uint16_t class_id, uint16_t arena_id,
	       struct dav_action *act);

int palloc_action_isalloc(struct dav_action *act);
void palloc_get_prange(struct dav_action *act, uint64_t *const off, uint64_t *const size,
		       int persist_udata);
uint64_t palloc_get_realoffset(struct palloc_heap *heap, uint64_t off);

void
palloc_defer_free(struct palloc_heap *heap, uint64_t off,
	struct dav_action *act);

void
palloc_cancel(struct palloc_heap *heap,
	struct dav_action *actv, size_t actvcnt);

void
palloc_publish(struct palloc_heap *heap,
	struct dav_action *actv, size_t actvcnt,
	struct operation_context *ctx);

void
palloc_set_value(struct palloc_heap *heap, struct dav_action *act,
	uint64_t *ptr, uint64_t value);

uint64_t palloc_first(struct palloc_heap *heap);
uint64_t palloc_next(struct palloc_heap *heap, uint64_t off);

size_t palloc_usable_size(struct palloc_heap *heap, uint64_t off);
uint64_t palloc_extra(struct palloc_heap *heap, uint64_t off);
uint16_t palloc_flags(struct palloc_heap *heap, uint64_t off);

int palloc_boot(struct palloc_heap *heap, void *heap_start,
		uint64_t heap_size, uint64_t *sizep,
		void *base, struct mo_ops *p_ops,
		struct stats *stats, struct pool_set *set);

int palloc_buckets_init(struct palloc_heap *heap);
int palloc_init(void *heap_start, uint64_t heap_size, uint64_t *sizep, struct mo_ops *p_ops);
void *palloc_heap_end(struct palloc_heap *h);
int palloc_heap_check(void *heap_start, uint64_t heap_size);
int palloc_heap_check_remote(void *heap_start, uint64_t heap_size, struct remote_ops *ops);
void palloc_heap_cleanup(struct palloc_heap *heap);
size_t palloc_heap(void *heap_start);

/* foreach callback, terminates iteration if return value is non-zero */
typedef int (*object_callback)(const struct memory_block *m, void *arg);

#if VG_MEMCHECK_ENABLED
void palloc_heap_vg_open(struct palloc_heap *heap, int objects);
#endif

#endif /* __DAOS_COMMON_PALLOC_H */
