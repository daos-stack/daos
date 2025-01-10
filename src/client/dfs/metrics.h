/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DFS_METRICS_H__
#define __DFS_METRICS_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos.h>
#include <daos_fs.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

/*
 * Report read/write counts on a per-I/O size.
 * Buckets starts at [0; 256B[ and are increased by power of 2
 * (i.e. [256B; 512B[, [512B; 1KB[) up to [4MB; infinity[
 * Since 4MB = 2^22 and 256B = 2^8, this means
 * (22 - 8 + 1) = 15 buckets plus the 4MB+ bucket, so
 * 16 buckets in total.
 */
#define NR_SIZE_BUCKETS 16

/* define a set of ops that we'll count if metrics are enabled */
#define D_FOREACH_DFS_OP_STAT(ACTION)                                                              \
	ACTION(CHMOD)                                                                              \
	ACTION(CHOWN)                                                                              \
	ACTION(CREATE)                                                                             \
	ACTION(GETSIZE)                                                                            \
	ACTION(GETXATTR)                                                                           \
	ACTION(LSXATTR)                                                                            \
	ACTION(MKDIR)                                                                              \
	ACTION(OPEN)                                                                               \
	ACTION(OPENDIR)                                                                            \
	ACTION(READ)                                                                               \
	ACTION(READDIR)                                                                            \
	ACTION(READLINK)                                                                           \
	ACTION(RENAME)                                                                             \
	ACTION(RMXATTR)                                                                            \
	ACTION(SETATTR)                                                                            \
	ACTION(SETXATTR)                                                                           \
	ACTION(STAT)                                                                               \
	ACTION(SYMLINK)                                                                            \
	ACTION(SYNC)                                                                               \
	ACTION(TRUNCATE)                                                                           \
	ACTION(UNLINK)                                                                             \
	ACTION(WRITE)

#define DFS_OP_STAT_DEFINE(name, ...) DOS_##name,

enum dfs_op_stat {
	D_FOREACH_DFS_OP_STAT(DFS_OP_STAT_DEFINE) DOS_LIMIT,
};

#define DFS_OP_STAT_INCR(_dfs, _name)                                                              \
	if (_dfs->metrics != NULL)                                                                 \
		d_tm_inc_counter(_dfs->metrics->dm_op_stats[(_name)], 1);

struct dfs_metrics {
	struct d_tm_node_t *dm_op_stats[DOS_LIMIT];
	struct d_tm_node_t *dm_read_bytes;
	struct d_tm_node_t *dm_write_bytes;
	struct d_tm_node_t *dm_mount_time;
};

bool
dfs_metrics_enabled();

void
dfs_metrics_init(dfs_t *dfs);

void
dfs_metrics_fini(dfs_t *dfs);

#endif /* __DFS_METRICS_H__ */