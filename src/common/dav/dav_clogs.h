/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2022, Intel Corporation */

/*
 * dav_iface.h -- Interfaces exported by DAOS internal Allocator for VOS (DAV)
 */

#ifndef LIBDAV_DAV_CLOGS_H
#define LIBDAV_DAV_CLOGS_H 1

#include <stdint.h>
#include <sys/types.h>
#include "ulog.h"

/*
 * Distance between lanes used by threads required to prevent threads from
 * false sharing part of lanes array. Used if properly spread lanes are
 * available. Otherwise less spread out lanes would be used.
 */
#define LANE_JUMP (64 / sizeof(uint64_t))

/*
 * Number of times the algorithm will try to reacquire the primary lane for the
 * thread. If this threshold is exceeded, a new primary lane is selected for the
 * thread.
 */
#define LANE_PRIMARY_ATTEMPTS 128

#define RLANE_DEFAULT 0

#define LANE_TOTAL_SIZE (3072) /* 3 * 1024 (sum of 3 old lane sections) */
/*
 * We have 3 kilobytes to distribute.
 * The smallest capacity is needed for the internal redo log for which we can
 * accurately calculate the maximum number of occupied space: 48 bytes,
 * 3 times sizeof(struct ulog_entry_val). One for bitmap OR, second for bitmap
 * AND, third for modification of the destination pointer. For future needs,
 * this has been bumped up to 12 ulog entries.
 *
 * The remaining part has to be split between transactional redo and undo logs,
 * and since by far the most space consuming operations are transactional
 * snapshots, most of the space, 2 kilobytes, is assigned to the undo log.
 * After that, the remainder, 640 bytes, or 40 ulog entries, is left for the
 * transactional redo logs.
 * Thanks to this distribution, all small and medium transactions should be
 * entirely performed without allocating any additional metadata.
 *
 * These values must be cacheline size aligned to be used for ulogs. Therefore
 * they are parametrized for the size of the struct ulog changes between
 * platforms.
 */
#define LANE_UNDO_SIZE (LANE_TOTAL_SIZE \
			- LANE_REDO_EXTERNAL_SIZE \
			- LANE_REDO_INTERNAL_SIZE \
			- 3 * sizeof(struct ulog)) /* 2048 for 64B ulog */
#define LANE_REDO_EXTERNAL_SIZE ALIGN_UP(704 - sizeof(struct ulog), \
					CACHELINE_SIZE) /* 640 for 64B ulog */
#define LANE_REDO_INTERNAL_SIZE ALIGN_UP(256 - sizeof(struct ulog), \
					CACHELINE_SIZE) /* 192 for 64B ulog */

struct dav_clogs {
	/*
	 * Redo log for self-contained and 'one-shot' allocator operations.
	 * Cannot be extended.
	 */
	struct ULOG(LANE_REDO_INTERNAL_SIZE) internal;
	/*
	 * Redo log for large operations/transactions.
	 * Can be extended by the use of internal ulog.
	 */
	struct ULOG(LANE_REDO_EXTERNAL_SIZE) external;
	/*
	 * Undo log for snapshots done in a transaction.
	 * Can be extended/shrunk by the use of internal ulog.
	 */
	struct ULOG(LANE_UNDO_SIZE) undo;
};

typedef struct dav_obj dav_obj_t;

int dav_create_clogs(dav_obj_t *hdl);
void dav_destroy_clogs(dav_obj_t *hdl);
int dav_hold_clogs(dav_obj_t *hdl);
int dav_release_clogs(dav_obj_t *hdl);

#endif /*LIBDAV_DAV_CLOGS_H*/
