/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2022, Intel Corporation */

/*
 * dav_flags.h -- Interfaces exported by DAOS internal Allocator for VOS (DAV)
 */

#ifndef __DAOS_COMMON_DAV_INTERNAL_H
#define __DAOS_COMMON_DAV_INTERNAL_H 1

#include "dav.h"
#include "dav_clogs.h"
#include "heap.h"
#include "mo_wal.h"
#include "wal_tx.h"

#define DAV_MAX_ALLOC_SIZE ((size_t)0x3FFDFFFC0)

enum dav_tx_failure_behavior {
	DAV_TX_FAILURE_ABORT,
	DAV_TX_FAILURE_RETURN,
};

enum dav_stats_enabled {
	DAV_STATS_ENABLED_TRANSIENT,
	DAV_STATS_ENABLED_BOTH,
	DAV_STATS_ENABLED_PERSISTENT,
	DAV_STATS_DISABLED,
};

enum dav_arenas_assignment_type {
	DAV_ARENAS_ASSIGNMENT_THREAD_KEY,
	DAV_ARENAS_ASSIGNMENT_GLOBAL,
};

#define	DAV_PHDR_SIZE	4096

/* DAV header data that will be persisted */
struct dav_phdr {
	uint64_t		dp_uuid_lo;
	uint64_t		dp_heap_offset;
	uint64_t		dp_heap_size;
	uint64_t		dp_root_offset;
	uint64_t		dp_root_size;
	struct stats_persistent dp_stats_persistent;
	char	 dp_unused[DAV_PHDR_SIZE - sizeof(uint64_t)*5 -
			sizeof(struct stats_persistent)];
};

/*REVISIT*/
struct bio_meta_instance;

/* DAV object handle */
typedef struct dav_obj {
	char				*do_path;
	uint64_t			 do_size;
	void				*do_base;
	struct palloc_heap		*do_heap;
	struct dav_phdr			*do_phdr;
	struct operation_context	*external;
	struct operation_context	*undo;
	struct mo_ops			 p_ops;	/* REVISIT */
	struct stats			*do_stats;
	int				 do_fd;
	int				 nested_tx;
	struct bio_meta_instance	*do_mi;
	struct umem_tx			*utx;
	struct wal_tx			 do_wtx;
 /* DI */
	int tc;
	pid_t ts[10];

	struct dav_clogs		 clogs __attribute__ ((aligned (CACHELINE_SIZE)));
} dav_obj_t;

void chk_tid(dav_obj_t *pop); /* DI */

#endif /* __DAOS_COMMON_DAV_INTERNAL_H */
