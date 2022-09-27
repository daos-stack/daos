/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2017-2021, Intel Corporation */

/*
 * stats.h -- definitions of statistics
 */

#ifndef LIBPMEMOBJ_STATS_H
#define LIBPMEMOBJ_STATS_H 1

struct stats_transient {
	uint64_t heap_run_allocated;
	uint64_t heap_run_active;
};

struct stats_persistent {
	uint64_t heap_curr_allocated;
};

struct stats {
	struct stats_transient *transient;
	struct stats_persistent *persistent;
};

#define STATS_INC(stats, type, name, value) \
	STATS_INC_##type(stats, name, value)

#define STATS_INC_transient(stats, name, value)\
	util_fetch_and_add64((&(stats)->transient->name), (value))

#define STATS_INC_persistent(stats, name, value)\
	util_fetch_and_add64((&(stats)->persistent->name), (value))

#define STATS_SUB(stats, type, name, value)\
	STATS_SUB_##type(stats, name, value)

#define STATS_SUB_transient(stats, name, value)\
	util_fetch_and_sub64((&(stats)->transient->name), (value))

#define STATS_SUB_persistent(stats, name, value)\
	util_fetch_and_sub64((&(stats)->persistent->name), (value))

#define STATS_SET(stats, type, name, value)\
	STATS_SET_##type(stats, name, value)

#define STATS_SET_transient(stats, name, value)\
	util_atomic_store_explicit64((&(stats)->transient->name),\
		(value), memory_order_release)\

#define STATS_SET_persistent(stats, name, value)\
	util_atomic_store_explicit64((&(stats)->persistent->name),\
		(value), memory_order_release)\

typedef struct dav_obj dav_obj_t;

struct stats *stats_new(dav_obj_t *pop);
void stats_delete(dav_obj_t *pop, struct stats *stats);

#endif
