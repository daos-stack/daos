/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC    DD_FAC(tests)
#include <stdio.h>

#include <daos/placement.h>
#include <daos/object.h>
#include "place_obj_common.h"
#include <daos/pool_map.h>
#include <daos/common.h>

#define DEFAULT_NODES      8
#define DEFAULT_RANKS      2
#define DEFAULT_TARGETS    16
#define DEFAULT_OBJ_COUNT  1000
#define DEFAULT_OBJ_CLASS  "EC_2P1G1"

#define MAX_OPERATIONS 32
#define MAX_OIDS 100000
#define MAX_SHARDS 32

struct test_ctx {
	int			 nodes;
	int			 ranks_per_node;
	int			 targets_per_rank;
	int			 num_oids;
	daos_oclass_id_t	 oclass;
	struct pool_map		*pool_map;
	struct pl_map		*pl_map;
};

struct test_oid { daos_obj_id_t oid; };

struct oid_layout {
	daos_obj_id_t	 oid;
	int		 shard_nr;
	uint32_t	 targets[MAX_SHARDS];
};

enum operation_type {
    OP_ADD_NODE,
    OP_EXCLUDE_NODE,
    OP_EXCLUDE_RANK,
    OP_REINTEGRATE_RANK,
    OP_INVALID
};

struct operation {
    enum operation_type type;
    char args[64];
};

struct op_map {
    const char *name;
    enum operation_type type;
};

int pool_map_init(struct test_ctx *ctx);

int placement_map_init(struct test_ctx *ctx);

int generate_oids(struct test_ctx *ctx, struct test_oid *oids);

int capture_layouts(struct test_ctx *ctx,
		struct test_oid *oids,
		struct oid_layout *layouts);

int validate_configuration(uint32_t nodes, uint32_t ranks, uint32_t targets,
                              const char *object_class_str, struct operation *operations, int operation_count);
					
void cleanup(struct test_ctx *ctx);
