/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2022, Intel Corporation */

/*
 * dav_iface.h -- Interfaces exported by DAOS internal Allocator for VOS (DAV)
 */

#ifndef __DAOS_COMMON_DAV_CLOGS_H
#define __DAOS_COMMON_CLOGS_H 1

#include <stdint.h>
#include <sys/types.h>
#include "ulog.h"

#define LANE_TOTAL_SIZE (3072) /* 3 * 1024 (sum of 3 old lane sections) */
/*
 * We have 3 kilobytes to distribute be split between transactional redo
 * and undo logs.
 * Since by far the most space consuming operations are transactional
 * snapshots, most of the space, 2304 bytes, is assigned to the undo log.
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
			- 2 * sizeof(struct ulog)) /* 2304 for 64B ulog */
#define LANE_REDO_EXTERNAL_SIZE ALIGN_UP(704 - sizeof(struct ulog), \
					CACHELINE_SIZE) /* 640 for 64B ulog */

struct dav_clogs {
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

#endif /* __DAOS_COMMON_DAV_CLOGS_H */
