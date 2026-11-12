/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>

#include <daos/placement.h>
#include <daos/object.h>
#include "place_obj_common.h"
#include <daos/pool_map.h>
#include <daos/common.h>

#define DEFAULT_NODES          8
#define DEFAULT_RANKS_PER_NODE 2
#define DEFAULT_TGTS_PER_RANK  16
#define DEFAULT_OBJ_COUNT      100000
#define DEFAULT_OBJ_CLASS      "EC_2P1G1"

#define MAX_OPERATIONS         32
#define MAX_OP_ARGS            32
#define MAX_SHARDS             128

struct test_ctx {
	int                 nodes;
	int                 ranks_per_node;
	int                 targets_per_rank;
	int                 num_oids;
	uint64_t            setup_layout_test_mem;
	uint64_t            setup_daos_api_mem;
	struct test_obj_md *oids;
	uuid_t              uuid;
	daos_oclass_id_t    oclass;
	struct pool_map    *pool_map;
	struct pl_map      *pl_map;
	bool                print_memory;
};

struct test_obj_md {
	struct daos_obj_md obj_md;
};

struct oid_layout {
	daos_obj_id_t        oid;
	uint32_t             nr;
	uint32_t             grp_size;
	uint32_t             grp_nr;
	struct pl_obj_shard *ol_shards;
};

enum operation_type { OP_EXCLUDE, OP_REINT, OP_DRAIN, OP_ADD, OP_INVALID };

enum operation_component { RANK, NODE, TARGET, INVALID };

struct operation {
	enum operation_type      type;
	enum operation_component component;
	int                      args[MAX_OP_ARGS];
	int                      nr_args;
};

struct shard_diff {
	uint32_t *shard_ids;
	uint32_t *spare_tgts;
	int       nr;
};

void
pool_map_and_pl_map_init(struct test_ctx *ctx);

void
generate_oids(struct test_ctx *ctx);

int
capture_layouts(struct test_ctx *ctx, struct pl_obj_layout **layouts, double *ms_place_out);

void
free_layouts(struct pl_obj_layout **layouts, int num_oids);

void
free_diffs(struct shard_diff *diff, int num_oids);

int
compare_layout(struct test_ctx *ctx, struct pl_obj_layout **pre_layout, struct shard_diff *diff,
	       struct pl_obj_layout **post_layout, int max_diff, enum operation_type op_type);

int
validate_configuration(int64_t nodes, int64_t ranks_per_node, int64_t tgts_per_rank,
		       int64_t obj_count, const char *object_class_str,
		       struct operation *operations, int operation_count);

void
cleanup(struct test_ctx *ctx);

int
fetch_targets(struct test_ctx *ctx, struct operation *op, struct pool_target_id_list *tgts);
int
set_tgt_status_and_find_diff(struct test_ctx *ctx, struct pool_target_id_list *tgts,
			     enum operation_type op_type, struct shard_diff *diff,
			     double *ms_rebuild_out);

int
add_node(struct test_ctx *ctx, uint32_t node_id);