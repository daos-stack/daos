/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PROFILE_H__
#define __DAOS_PROFILE_H__

#include <daos_errno.h>
#include <daos/debug.h>
#include <gurt/hash.h>
#include <gurt/common.h>
#include <cart/api.h>
#include <daos_types.h>

struct daos_profile_chunk {
	d_list_t  dpc_chunk_list;
	uint32_t  dpc_chunk_offset;
	uint32_t  dpc_chunk_size;
	uint64_t *dpc_chunks;
};

/* The profile structure to record single operation */
struct daos_profile_op {
	int                        dpo_op;              /* operation */
	char                      *dpo_op_name;         /* name of the op */
	int                        dpo_acc_cnt;         /* total number of val */
	int                        dpo_acc_val;         /* current total val */
	d_list_t                   dpo_chunk_list;      /* list of all chunks */
	d_list_t                   dpo_chunk_idle_list; /* idle list of profile chunk */
	int                        dpo_chunk_total_cnt; /* Count in idle list & list */
	int                        dpo_chunk_cnt;       /* count in list */
	struct daos_profile_chunk *dpo_current_chunk;   /* current chunk */
};

/* Holding the total trunk list for a specific profiling module */
struct daos_profile {
	struct daos_profile_op *dp_ops;
	int                     dp_ops_cnt;
	int                     dp_avg;
	int                     dp_xid;
	int                     dp_rank;
	char                   *dp_dir_path;    /* Where to dump the profiling */
	char                  **dp_names;       /* profile name */
	void                   *dp_dump_thread; /* dump thread for profile */
	unsigned int            dp_empty : 1;
};

enum profile_op {
	OBJ_PF_UPDATE_PREP = 0,
	OBJ_PF_UPDATE_DISPATCH,
	OBJ_PF_UPDATE_LOCAL,
	OBJ_PF_UPDATE_END,
	OBJ_PF_BULK_TRANSFER,
	OBJ_PF_UPDATE_REPLY,
	OBJ_PF_UPDATE,
	VOS_UPDATE_END,
	PF_MAX_CNT,
};

int
daos_profile_init(struct daos_profile **dp_p, char *path, int avg, int rank, int tgt_id);
int
daos_profile_count(struct daos_profile *dp, int id, int val);
int
daos_profile_stop(struct daos_profile *dp);
void
daos_profile_dump(struct daos_profile *dp);
void
daos_profile_destroy(struct daos_profile *dp);
#endif /* __DAOS_PROFILE_H__ */
