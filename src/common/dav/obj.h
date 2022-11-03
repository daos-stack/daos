/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2021, Intel Corporation */

/*
 * obj.h -- internal definitions for obj module
 */

#ifndef LIBPMEMOBJ_OBJ_H
#define LIBPMEMOBJ_OBJ_H 1

#include <stddef.h>
#include <stdint.h>

#include <gurt/common.h>
#include "dav_internal.h"
#include "stats.h"
#include "page_size.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "fault_injection.h"

#define OBJ_OFF_TO_PTR(pop, off) ((void *)((uintptr_t)(((dav_obj_t *)(pop))->do_base) + (off)))
#define OBJ_PTR_TO_OFF(pop, ptr) ((uintptr_t)(ptr) - (uintptr_t)(((dav_obj_t *)(pop))->do_base))
#define OBJ_OID_IS_NULL(oid)	((oid).off == 0)
#define OBJ_LIST_EMPTY(head)	OBJ_OID_IS_NULL((head)->pe_first)
#define OBJ_OFF_FROM_HEAP(pop, off)\
	((off) >= ((dav_obj_t *)(pop))->do_phdr->dp_heap_offset &&\
	(off) < ((dav_obj_t *)(pop))->do_phdr->dp_heap_offset +\
		((dav_obj_t *)(pop))->do_phdr->dp_heap_size)

#define OBJ_OFF_IS_VALID(pop, off)\
	(OBJ_OFF_FROM_HEAP(pop, off) ||\
	(OBJ_PTR_TO_OFF(pop, &((dav_obj_t *)(pop))->do_phdr->dp_root_offset) == (off)) ||\
	(OBJ_PTR_TO_OFF(pop, &((dav_obj_t *)(pop))->do_phdr->dp_root_size) == (off)))

#define OBJ_PTR_IS_VALID(pop, ptr)\
	OBJ_OFF_IS_VALID(pop, OBJ_PTR_TO_OFF(pop, ptr))

#define OBJ_PTR_FROM_POOL(pop, ptr)\
	((uintptr_t)(ptr) >= (uintptr_t)(((dav_obj_t *)pop)->do_base) &&\
	(uintptr_t)(ptr) < (uintptr_t)(((dav_obj_t *)pop)->do_base) +\
		(((dav_obj_t *)pop)->do_phdr->dp_heap_offset +\
		 ((dav_obj_t *)pop)->do_phdr->dp_heap_size))

#define	OBJ_OFFRANGE_FROM_HEAP(pop, start, end)\
	(((start) >= ((dav_obj_t *)pop)->do_phdr->dp_heap_offset) &&\
	 ((end) <=  (((dav_obj_t *)pop)->do_phdr->dp_heap_offset + \
		     ((dav_obj_t *)pop)->do_phdr->dp_heap_size)))

typedef void (*persist_local_fn)(const void *, size_t);
typedef void (*flush_local_fn)(const void *, size_t);
typedef void (*drain_local_fn)(void);

typedef void *(*memcpy_local_fn)(void *dest, const void *src, size_t len,
		unsigned flags);
typedef void *(*memmove_local_fn)(void *dest, const void *src, size_t len,
		unsigned flags);
typedef void *(*memset_local_fn)(void *dest, int c, size_t len, unsigned flags);

typedef uint64_t type_num_t;

#define CONVERSION_FLAG_OLD_SET_CACHE ((1ULL) << 0)

/*
 * Stored in the 'size' field of oobh header, determines whether the object
 * is internal or not. Internal objects are skipped in pmemobj iteration
 * functions.
 */
#define OBJ_INTERNAL_OBJECT_MASK ((1ULL) << 15)

#define CLASS_ID_FROM_FLAG(flag)\
((uint16_t)((flag) >> 48))

#define ARENA_ID_FROM_FLAG(flag)\
((uint16_t)((flag) >> 32))

/*
 * (debug helper macro) logs notice message if used inside a transaction
 */
#ifdef DEBUG
#define _POBJ_DEBUG_NOTICE_IN_TX()\
	_pobj_debug_notice(__func__, NULL, 0)
#else
#define _POBJ_DEBUG_NOTICE_IN_TX() do {} while (0)
#endif

#if FAULT_INJECTION
void
pmemobj_inject_fault_at(enum pmem_allocation_type type, int nth,
							const char *at);

int
pmemobj_fault_injection_enabled(void);
#else
static inline void
pmemobj_inject_fault_at(enum pmem_allocation_type type, int nth,
						const char *at)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(type, nth, at);

	abort();
}

static inline int
pmemobj_fault_injection_enabled(void)
{
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
