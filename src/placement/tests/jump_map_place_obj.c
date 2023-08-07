/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include <daos/object.h>
#include "place_obj_common.h"
/* Gain some internal knowledge of pool server */
#include "../../pool/rpc.h"
#include "../../pool/srv_pool_map.h"

bool g_verbose;

static void
object_class_is_verified(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;

	/*
	 * ---------------------------------------------------------
	 * with a single target
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 1, 1, 1, &po_map, &pl_map);

	assert_invalid_param(pl_map, OC_UNKNOWN, 0);
	assert_placement_success(pl_map, OC_S1, 0);
	assert_placement_success(pl_map, OC_SX, 0);

	/* Replication should fail because there's only 1 target */
	assert_invalid_param(pl_map, OC_RP_2G1, 0);
	assert_invalid_param(pl_map, OC_RP_3G1, 0);
	assert_invalid_param(pl_map, OC_RP_4G1, 0);
	assert_invalid_param(pl_map, OC_RP_6G1, 0);

	/* Multiple groups should fail because there's only 1 target */
	assert_invalid_param(pl_map, OC_S2, 0);
	assert_invalid_param(pl_map, OC_S4, 0);
	assert_invalid_param(pl_map, OC_S32, 0);
	free_pool_and_placement_map(po_map, pl_map);


	/*
	 * ---------------------------------------------------------
	 * with 2 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 1, 1, 2, &po_map, &pl_map);

	assert_placement_success(pl_map, OC_S1, 0);
	assert_placement_success(pl_map, OC_S2, 0);
	assert_placement_success(pl_map, OC_SX, 0);

	/*
	 * Even though there are 2 targets, these will still fail because
	 * placement requires a domain for each redundancy.
	 */
	assert_invalid_param(pl_map, OC_RP_2G1, 0);
	assert_invalid_param(pl_map, OC_RP_2G2, 0);
	assert_invalid_param(pl_map, OC_RP_3G1, 0);
	assert_invalid_param(pl_map, OC_RP_4G1, 0);
	assert_invalid_param(pl_map, OC_RP_6G1, 0);
	/* The following require more targets than available. */
	assert_invalid_param(pl_map, OC_S4, 0);
	assert_invalid_param(pl_map, OC_S32, 0);
	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 1 target each
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 2, 1, 1, &po_map, &pl_map);

	assert_placement_success(pl_map, OC_S1, 0);
	assert_placement_success(pl_map, OC_RP_2G1, 0);
	assert_placement_success(pl_map, OC_RP_2GX, 0);
	assert_invalid_param(pl_map, OC_RP_2G2, 0);
	assert_invalid_param(pl_map, OC_RP_2G4, 0);

	assert_invalid_param(pl_map, OC_RP_2G32, 0);
	assert_invalid_param(pl_map, OC_RP_3G1, 0);

	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 2 targets each = 4 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 2, 1, 2, &po_map, &pl_map);
	assert_placement_success(pl_map, OC_RP_2G2, 0);
	assert_invalid_param(pl_map, OC_RP_2G4, 0);

	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 4 targets each = 8 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 2, 1, 4, &po_map, &pl_map);
	assert_placement_success(pl_map, OC_RP_2G4, 0);
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2, 0);

	free_pool_and_placement_map(po_map, pl_map);
	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 1 nodes each, 4 targets each = 8 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 2, 1, 4, &po_map, &pl_map);
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2, 0);

	free_pool_and_placement_map(po_map, pl_map);

	/* The End */
}

/*
 * Test context structures and functions to make testing placement and
 * asserting expectations easier and more readable.
 */

/* Results provided by the pl_obj_find_rebuild/addition/reint functions */
struct remap_result {
	uint32_t		*tgt_ranks;
	uint32_t		*ids; /* shard ids */
	uint32_t		 nr;
	uint32_t		 out_nr;
	/* Should skip this 'find' operation. This is
	 * a workaround for DAOS-6516
	 */
	bool			 skip;
};

static void
rr_init(struct remap_result *rr, uint32_t nr)
{
	D_ALLOC_ARRAY(rr->ids, nr);
	D_ALLOC_ARRAY(rr->tgt_ranks, nr);
	rr->nr = nr;
	rr->out_nr = 0;
}

static void
rr_fini(struct remap_result *rr)
{
	D_FREE(rr->ids);
	D_FREE(rr->tgt_ranks);
	memset(rr, 0, sizeof(*rr));
}

static void
rr_reset(struct remap_result *rr)
{
	memset(rr->ids, 0, rr->nr * sizeof(*rr->ids));
	memset(rr->tgt_ranks, 0, rr->nr * sizeof(*rr->tgt_ranks));
	rr->out_nr = 0;
}


static void
rr_print(struct remap_result *map)
{
	int i;

	if (map->skip) {
		print_message("\t Skipped\n");
		return;
	}
	for (i = 0; i < map->out_nr; i++)
		print_message("\tshard %d -> target %d\n",
			      map->ids[i], map->tgt_ranks[i]);
	if (i == 0)
		print_message("\t(Nothing)\n");
}

typedef int (*find_fn)(struct pl_map *map, uint32_t gl_layout_ver,
		       struct daos_obj_md *md, struct daos_obj_shard_md *shard_md,
		       uint32_t reint_ver, uint32_t *tgt_rank,
		       uint32_t *shard_id, unsigned int array_size);

static void
rr_find(struct pl_map *pl_map, struct daos_obj_md *md, uint32_t ver,
	struct remap_result *rr, find_fn fn)
{
	if (rr->skip)
		rr_reset(rr);
	else
		rr->out_nr = fn(pl_map, PLT_LAYOUT_VERSION, md, NULL, ver, rr->tgt_ranks,
				rr->ids, rr->nr);
}

/* Testing context */
struct jm_test_ctx {
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	struct pl_obj_layout	*layout;
	uuid_t			 pl_uuid;
	/* remember shard's original targets */
	uint32_t		*shard_targets;


	/* results from scanning (find_rebuild/reint/addition) */
	struct remap_result	rebuild;
	struct remap_result	reint;
	struct remap_result	new;


	uint32_t		 ver; /* Maintain version of pool map */

	daos_obj_id_t		 oid; /* current oid used for testing */

	/* configuration of the system. Number of domains(racks), nodes
	 * per domain, and targets per node
	 * target_nr is used for standard config, domain_target_nr used for
	 * non standard configs
	 */
	bool			 is_standard_config;
	uint32_t		 domain_nr;
	uint32_t		 node_nr;
	uint32_t		 target_nr;
	uint32_t		 *domain_target_nr;

	daos_oclass_id_t	 object_class;
	bool			 are_maps_generated;
	bool			 is_layout_set;
	bool			 enable_print_layout;
	bool			 enable_print_debug_msgs;
	bool			 enable_print_pool;
};

/* shard: struct pl_obj_shard * */
#define jtc_for_each_layout_shard(ctx, shard, i) \
	for (i = 0, shard = jtc_get_layout_shard(ctx, 0); \
		i < jtc_get_layout_nr(ctx); \
		i++, shard = jtc_get_layout_shard(ctx, i))

static void
__jtc_maps_free(struct jm_test_ctx *ctx)
{
	if (ctx->are_maps_generated) {
		free_pool_and_placement_map(ctx->po_map, ctx->pl_map);
		ctx->po_map = NULL;
		ctx->pl_map = NULL;
	}
}

static void
__jtc_layout_free(struct jm_test_ctx *ctx)
{
	if (ctx->is_layout_set) {
		pl_obj_layout_free(ctx->layout);
		ctx->layout = NULL;
	}
}

static void
jtc_print_pool(struct jm_test_ctx *ctx)
{
	if (ctx->enable_print_pool)
		pool_map_print(ctx->po_map);
}

static void
jtc_print_layout_force(struct jm_test_ctx *ctx)
{
	print_layout(ctx->layout);
}

static void
jtc_maps_gen(struct jm_test_ctx *ctx)
{
	/* Allocates the maps. must be freed with jtc_maps_free if already
	 * allocated
	 */
	__jtc_maps_free(ctx);

	gen_pool_and_placement_map(1, ctx->domain_nr, ctx->node_nr,
				   ctx->target_nr, PL_TYPE_JUMP_MAP,
				   PO_COMP_TP_RANK, &ctx->po_map, &ctx->pl_map);

	assert_non_null(ctx->po_map);
	assert_non_null(ctx->pl_map);
	ctx->are_maps_generated = true;
}


static int
jtc_pool_map_extend(struct jm_test_ctx *ctx, uint32_t domain_count,
		    uint32_t node_count, uint32_t target_count)
{
	struct pool_buf	*map_buf;
	uint32_t	map_version;
	int		ntargets;
	int		rc, i;
	d_rank_list_t	rank_list;
	uint32_t	domains[] = {255, 0, 5, /* root */
				     1, 101, 1,
				     1, 102, 1,
				     1, 103, 1,
				     1, 104, 1,
				     1, 105, 1};
	const size_t	tuple_size = 3;
	const size_t	max_domains = 5;
	uint32_t	domain_tree_len;
	uint32_t	domains_only_len;
	uint32_t	ranks_per_domain;
	uint32_t	*domain_tree;
	uuid_t		target_uuids[] = {"12345678", "23456789",
					  "34567890", "4567890a" };

	/* Only support add same node/target domain for the moment */
	assert_int_equal(ctx->target_nr, target_count);
	assert_int_equal(ctx->node_nr, node_count);
	if (domain_count > max_domains)
		fail_msg("Only %lu domains can be added", max_domains);

	/* Build the fault domain tree */
	ranks_per_domain = node_count / domain_count;
	/* Update domains array to be consistent with input params */
	domains[tuple_size - 1] = domain_count; /* root */
	for (i = 0; i < domain_count; i++) {
		uint32_t start_idx = (i + 1) * tuple_size;

		domains[start_idx + tuple_size - 1] = ranks_per_domain;
	}

	domains_only_len = (domain_count + 1) * tuple_size;
	domain_tree_len = domains_only_len + node_count;
	D_ALLOC_ARRAY(domain_tree, domain_tree_len);
	assert_non_null(domain_tree);

	memcpy(domain_tree, domains,
	       sizeof(uint32_t) * domains_only_len);

	rank_list.rl_nr = node_count;
	D_ALLOC_ARRAY(rank_list.rl_ranks, node_count);
	assert_non_null(rank_list.rl_ranks);
	for (i = 0; i < node_count; i++) {
		uint32_t idx = domains_only_len + i;

		domain_tree[idx] = ctx->domain_nr + i;
		rank_list.rl_ranks[i] = ctx->domain_nr + i;
	}

	ntargets = node_count * target_count;
	if (ntargets > ARRAY_SIZE(target_uuids))
		fail_msg("Only %lu targets can be added",
			 ARRAY_SIZE(target_uuids));

	map_version = pool_map_get_version(ctx->po_map) + 1;

	rc = gen_pool_buf(ctx->po_map, &map_buf, map_version, domain_tree_len, node_count,
			  ntargets, domain_tree, target_count);
	D_FREE(domain_tree);
	assert_success(rc);

	/* Extend the current pool map */
	rc = pool_map_extend(ctx->po_map, map_version, map_buf);
	D_FREE(map_buf);
	assert_success(rc);

	ctx->domain_nr += domain_count;

	jtc_print_pool(ctx);

	D_FREE(rank_list.rl_ranks);

	return rc;
}

static void
jtc_scan(struct jm_test_ctx *ctx)
{
	struct daos_obj_md md = {.omd_id = ctx->oid, .omd_ver = ctx->ver};

	rr_find(ctx->pl_map, &md, ctx->ver, &ctx->reint, pl_obj_find_reint);
	rr_find(ctx->pl_map, &md, ctx->ver, &ctx->new, pl_obj_find_addition);
	rr_find(ctx->pl_map, &md, ctx->ver, &ctx->rebuild, pl_obj_find_rebuild);

	if (ctx->enable_print_layout) {
		print_message("-- Rebuild Scan --\n");
		rr_print(&ctx->rebuild);

		print_message("-- Reint Scan --\n");
		rr_print(&ctx->reint);

		print_message("-- New Scan --\n");
		rr_print(&ctx->new);
	}
}

static int
jtc_create_layout(struct jm_test_ctx *ctx)
{
	int rc;

	D_ASSERT(ctx != NULL);
	D_ASSERT(ctx->pl_map != NULL);

	/* place object will allocate the layout so need to free first
	 * if already allocated
	 */
	__jtc_layout_free(ctx);
	rc = plt_obj_place(ctx->oid, 0, &ctx->layout, ctx->pl_map,
			   ctx->enable_print_layout);

	if (rc == 0)
		ctx->is_layout_set = true;
	return rc;
}

static int
jtc_layout_shard_tgt(struct jm_test_ctx *ctx, uint32_t shard_idx)
{
	return  ctx->layout->ol_shards[shard_idx].po_target;
}

static void
jtc_set_status_on_target(struct jm_test_ctx *ctx, const int status,
			 const uint32_t id)
{
	struct pool_target_id_list tgts;
	struct pool_target_id tgt_id = {.pti_id = id};

	tgts.pti_ids = &tgt_id;
	tgts.pti_number = 1;

	int rc = ds_pool_map_tgts_update(ctx->po_map, &tgts, status,
					 false, &ctx->ver, ctx->enable_print_debug_msgs);

	/* Make sure pool map changed */
	assert_success(rc);
	ctx->ver = pool_map_get_version(ctx->po_map);
	assert_true(ctx->ver > 0);

	pool_map_update_failed_cnt(ctx->po_map);
	rc = pool_map_set_version(ctx->po_map, ctx->ver);
	assert_success(rc);

	pl_map_update(ctx->pl_uuid, ctx->po_map, false, PL_TYPE_JUMP_MAP);
	jtc_print_pool(ctx);
}

static void
jtc_set_status_on_shard_target(struct jm_test_ctx *ctx, const int status,
			       const uint32_t shard_idx)
{
	int id = jtc_layout_shard_tgt(ctx, shard_idx);

	D_ASSERT(id >= 0);
	jtc_set_status_on_target(ctx, status, id);
}

static void
jtc_set_status_on_all_shards(struct jm_test_ctx *ctx, const int status)
{
	int i;

	for (i = 0; i < ctx->layout->ol_nr; i++)
		jtc_set_status_on_shard_target(ctx, status, i);
	jtc_print_pool(ctx);
}

static void
jtc_set_status_on_first_shard(struct jm_test_ctx *ctx, const int status)
{
	jtc_set_status_on_target(ctx, status, jtc_layout_shard_tgt(ctx, 0));
}

static void
jtc_set_object_meta(struct jm_test_ctx *ctx,
		    daos_oclass_id_t object_class, uint64_t lo, uint64_t hi)
{
	ctx->object_class = object_class;
	gen_oid(&ctx->oid, lo, hi, object_class);
}

static struct pl_obj_shard *
jtc_get_layout_shard(struct jm_test_ctx *ctx, const int shard_idx)
{
	if (shard_idx < ctx->layout->ol_nr)
		return &ctx->layout->ol_shards[shard_idx];

	return NULL;
}

static uint32_t
jtc_get_layout_nr(struct jm_test_ctx *ctx)
{
	return ctx->layout->ol_nr;
}

/* return the number of targets with -1 as target/shard */
static int
jtc_get_layout_bad_count(struct jm_test_ctx *ctx)
{
	struct pl_obj_shard	*shard;
	int			 i;
	int			 result = 0;

	jtc_for_each_layout_shard(ctx, shard, i)
		if (shard->po_shard == -1 || shard->po_target == -1)
			result++;

	return result;

}

static int
jtc_get_layout_rebuild_count(struct jm_test_ctx *ctx)
{
	uint32_t result = 0;
	uint32_t i;
	struct pl_obj_shard *shard;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_rebuilding)
			result++;
	}

	return result;
}

static bool
jtc_layout_has_duplicate(struct jm_test_ctx *ctx)
{
	int i;
	int target_num;
	bool *target_set;
	bool result = false;

	D_ASSERT(ctx != NULL);
	D_ASSERT(ctx->po_map != NULL);
	const uint32_t total_targets = pool_map_target_nr(ctx->po_map);

	D_ALLOC_ARRAY(target_set, total_targets);
	D_ASSERT(target_set != NULL);

	for (i = 0; i < ctx->layout->ol_nr; i++) {
		target_num = ctx->layout->ol_shards[i].po_target;

		if (target_num != -1) {
			if (target_set[target_num]) { /* already saw */
				print_message("Found duplicate target: %d\n",
					      target_num);
				result = true;
			}
			target_set[target_num] = true;
		}
	}
	D_FREE(target_set);

	return result;
}

static void
jtc_enable_debug(struct jm_test_ctx *ctx)
{
	ctx->enable_print_layout = true;
	ctx->enable_print_debug_msgs = true;
}

static void
jtc_set_standard_config(struct jm_test_ctx *ctx, uint32_t domain_nr,
			uint32_t node_nr, uint32_t target_nr)
{
	ctx->is_standard_config = true;
	ctx->domain_nr = domain_nr;
	ctx->node_nr = node_nr;
	ctx->target_nr = target_nr;
	jtc_maps_gen(ctx);
}

static void
__jtc_init(struct jm_test_ctx *ctx, daos_oclass_id_t object_class,
	   bool enable_debug)
{
	memset(ctx, 0, sizeof(*ctx));

	if (enable_debug)
		jtc_enable_debug(ctx);

	ctx->ver = 1; /* Should start with pool map version 1 */
	uuid_generate(ctx->pl_uuid);

	jtc_set_object_meta(ctx, object_class, 1, UINT64_MAX);

	/* hopefully 10x domain is enough */
	rr_init(&ctx->rebuild, 32);
	rr_init(&ctx->reint, 32);
	rr_init(&ctx->new, 32);
}

static void
jtc_init(struct jm_test_ctx *ctx, uint32_t domain_nr, uint32_t node_nr,
	 uint32_t target_nr, daos_oclass_id_t object_class, bool enable_debug)
{
	__jtc_init(ctx, object_class, enable_debug);

	jtc_set_standard_config(ctx, domain_nr, node_nr, target_nr);
}

static void
jtc_init_non_standard(struct jm_test_ctx *ctx, uint32_t domain_nr,
		      uint32_t target_nr[], daos_oclass_id_t object_class,
		      bool enable_debug)
{
	__jtc_init(ctx, object_class, enable_debug);

	ctx->is_standard_config = false;
	ctx->domain_nr = domain_nr;
	ctx->node_nr = 1;
	ctx->domain_target_nr = target_nr;

	gen_pool_and_placement_map_non_standard(domain_nr, (int *)target_nr,
						PL_TYPE_JUMP_MAP,
						&ctx->po_map,
						&ctx->pl_map);
	ctx->are_maps_generated = true;
}

static void
jtc_init_with_layout(struct jm_test_ctx *ctx, uint32_t domain_nr,
		     uint32_t node_nr, uint32_t target_nr,
		     daos_oclass_id_t object_class, bool enable_debug)
{
	jtc_init(ctx, domain_nr, node_nr, target_nr, object_class,
		 enable_debug);
	assert_success(jtc_create_layout(ctx));
}

static void
jtc_fini(struct jm_test_ctx *ctx)
{
	__jtc_layout_free(ctx);
	__jtc_maps_free(ctx);

	rr_fini(&ctx->rebuild);
	rr_fini(&ctx->reint);
	rr_fini(&ctx->new);

	if (ctx->shard_targets)
		D_FREE(ctx->shard_targets);

	memset(ctx, 0, sizeof(*ctx));
}

#define JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(ctx) \
	__jtc_create_and_assert_healthy_layout(__FILE__, __LINE__, ctx)

#define assert_int_equal_s(a, b, file, line) \
	do {\
	uint64_t __a = a; \
	uint64_t __b = b; \
	if (__a != __b) \
		fail_msg("%s:%d"DF_U64" != "DF_U64"\n", file, line, __a, __b); \
	} while (0)

static void
__jtc_create_and_assert_healthy_layout(char *file, int line,
				       struct jm_test_ctx *ctx)
{
	int rc = jtc_create_layout(ctx);

	if (rc != 0)
		fail_msg("%s:%d Layout create failed: "DF_RC"\n",
			 file, line, DP_RC(rc));
	jtc_scan(ctx);

	assert_int_equal_s(0, jtc_get_layout_rebuild_count(ctx),
			   file, line);
	assert_int_equal_s(0, jtc_get_layout_bad_count(ctx),
			   file, line);
	assert_int_equal_s(false, jtc_layout_has_duplicate(ctx), file, line);
	assert_int_equal_s(0, ctx->rebuild.out_nr, file, line);
	assert_int_equal_s(0, ctx->reint.out_nr, file, line);
	assert_int_equal_s(0, ctx->new.out_nr, file, line);
}

static int
jtc_get_layout_target_count(struct jm_test_ctx *ctx)
{
	if (ctx->layout != NULL)
		return ctx->layout->ol_nr;
	return 0;
}

static bool
jtc_has_shard_with_target_rebuilding(struct jm_test_ctx *ctx, int shard_id,
				     uint32_t *target)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_shard == shard_id && shard->po_rebuilding) {
			if (target != NULL)
				*target = shard->po_target;
			return true;
		}
	}

	return false;
}

static bool
jtc_has_shard_with_rebuilding_not_set(struct jm_test_ctx *ctx, int shard_id)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_shard == shard_id && !shard->po_rebuilding)
			return true;
	}

	return false;
}

static bool
jtc_has_shard_target_rebuilding(struct jm_test_ctx *ctx, uint32_t shard_id,
			       uint32_t target)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_shard == shard_id &&
		    shard->po_target == target &&
		    shard->po_rebuilding)
			return true;
	}
	return false;
}

static bool
jtc_has_shard_target_not_rebuilding(struct jm_test_ctx *ctx, uint32_t shard_id,
				    uint32_t target)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_shard == shard_id &&
		    shard->po_target == target &&
		    !shard->po_rebuilding)
			return true;
	}
	return false;
}

static bool
jtc_has_shard_moving_to_target(struct jm_test_ctx *ctx, uint32_t shard_id,
			       uint32_t target)
{
	return jtc_has_shard_target_rebuilding(ctx, shard_id, target);
}

static bool
jtc_layout_has_target(struct jm_test_ctx *ctx, uint32_t id)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_target == id)
			return true;
	}
	return false;
}

static bool
jtc_set_oid_with_shard_in_targets(struct jm_test_ctx *ctx, int *target_id,
				  int target_nr, int oc)
{
	int i, j;

	for (i = 0; i < 50; i++) {
		jtc_set_object_meta(ctx, oc, i + 1, UINT_MAX);
		assert_success(jtc_create_layout(ctx));
		for (j = 0; j < target_nr; j++)
			if (jtc_layout_has_target(ctx, target_id[j]))
				return true;
	}
	return false;
}

static void
jtc_snapshot_layout_targets(struct jm_test_ctx *ctx)
{
	struct pl_obj_shard *shard;
	int i;

	if (ctx->shard_targets)
		D_FREE(ctx->shard_targets);
	D_ALLOC_ARRAY(ctx->shard_targets, jtc_get_layout_nr(ctx));

	jtc_for_each_layout_shard(ctx, shard, i) {
		ctx->shard_targets[i] = shard->po_target;
	}
}

#define jtc_assert_scan_and_layout(ctx) do {\
	jtc_scan(ctx); \
	assert_success(jtc_create_layout(ctx)); \
} while (0)

/*
 * test that the layout has correct number of targets in rebuilding,
 * correct number of items from scan for find_reubild, find_reint, find_addition
 */
#define jtc_assert_rebuild_reint_new(ctx, l_rebuilding, s_rebuild, s_reint, \
				     s_new) \
	do {\
	if (l_rebuilding != jtc_get_layout_rebuild_count(&ctx)) \
		fail_msg("Expected %d rebuilding but found %d", l_rebuilding, \
		jtc_get_layout_rebuild_count(&ctx)); \
	if (s_rebuild != ctx.rebuild.out_nr) \
		fail_msg("Expected rebuild scan to return %d but found %d", \
		s_rebuild, ctx.rebuild.out_nr); \
	if (s_reint != ctx.reint.out_nr) \
		fail_msg("Expected reint scan to return %d but found %d", \
		s_reint, ctx.reint.out_nr); \
	if (s_new != ctx.new.out_nr) \
		fail_msg("Expected new scan to return %d but found %d", \
		s_new, ctx.new.out_nr); \
	} while (0)

#define UP	POOL_REINT
#define UPIN	POOL_ADD_IN
#define DOWN	POOL_EXCLUDE
#define DOWNOUT	POOL_EXCLUDE_OUT
#define DRAIN	POOL_DRAIN
#define EXTEND	POOL_EXTEND

/*
 * ------------------------------------------------
 * Begin Test cases using the jump map test context
 * ------------------------------------------------
 */

/* test with a variety of different system configuration for each object
 * class, there is nothing being "rebuilt", and there are no duplicates.
 */
static void
all_healthy(void **state)
{
	struct jm_test_ctx	 ctx;
	daos_oclass_id_t	*object_classes = NULL;
	int			 num_test_oc;
	int			 i;

	/* pick some specific object classes to verify the number of
	 * targets in the layout is expected
	 */
	jtc_init_with_layout(&ctx, 1, 1, 1, OC_S1, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(1, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 1, 1, 2, OC_S2, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(2, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 32, 1, 8, OC_SX, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(32 * 8, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 2, 1, 1, OC_RP_2G1, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(2, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 2, 1, 2, OC_RP_2G2, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(4, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 32, 1, 8, OC_RP_2GX, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(32 * 8, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	jtc_init_with_layout(&ctx, 18, 1, 1, OC_EC_16P2G1, g_verbose);
	JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	assert_int_equal(18, jtc_get_layout_target_count(&ctx));
	jtc_fini(&ctx);

	/* Test all object classes */
	num_test_oc = get_object_classes(&object_classes);
	jtc_init(&ctx, (1 << 10), 1, 16, 0, g_verbose);
	for (i = 0; i < num_test_oc; ++i) {
		struct daos_oclass_attr *oa;
		daos_obj_id_t oid;
		int grp_sz;
		uint32_t grp_nr;

		gen_oid(&oid, 0, 0, object_classes[i]);
		oa = daos_oclass_attr_find(oid, &grp_nr);
		grp_sz = daos_oclass_grp_size(oa);

		/* skip those gigantic layouts for saving time */
		if (grp_sz != DAOS_OBJ_REPL_MAX &&
		    grp_nr != DAOS_OBJ_GRP_MAX &&
		    grp_sz * grp_nr > (16 << 10))
			continue;

		jtc_set_object_meta(&ctx, object_classes[i], 0, 1);
		JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	}
	D_FREE(object_classes);
	jtc_fini(&ctx);
}

/*
 * ------------------------------------------------
 * Transition to DOWN state
 * ------------------------------------------------
 */
static void
down_to_target(void **state)
{
	struct jm_test_ctx	 ctx;

	jtc_init_with_layout(&ctx, 4, 1, 8, OC_RP_4G1, g_verbose);
	jtc_set_status_on_shard_target(&ctx, DOWN, 0);
	assert_success(jtc_create_layout(&ctx));
	jtc_scan(&ctx);

	assert_int_equal(ctx.rebuild.out_nr, 1);
	assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
	jtc_fini(&ctx);
}

static void
check_grp_rebuilding_shard(struct jm_test_ctx *ctx, uint32_t grp_size,
			   uint32_t grp_nr, uint32_t rebuilding_expect)
{
	int i, j;

	for (i = 0; i < grp_nr; i++) {
		uint32_t rebuilding = 0;

		for (j = 0; j < grp_size; j++) {
			struct pl_obj_shard *shard;

			shard = jtc_get_layout_shard(ctx, i * grp_size + j);
			if (shard->po_rebuilding)
				rebuilding++;
		}
		assert_true(rebuilding <= rebuilding_expect);
	}
}

static void
down_continuously(void **state)
{
	struct jm_test_ctx	 ctx;
	struct pl_obj_shard	 prev_first_shard;
	int			 i;

	/* start with 16 targets (4x4) and pick an object class that uses 4
	 * targets
	 */
	jtc_init_with_layout(&ctx, 4, 1, 4, OC_RP_2G2, g_verbose);
	jtc_print_pool(&ctx);

	prev_first_shard = *jtc_get_layout_shard(&ctx, 0);

	/* loop through rest of targets, marking each as down. By the end the
	 * pool map includes only 4 targets that are still UPIN
	 */
	for (i = 0; i < 16 - 8; i++) {
		jtc_set_status_on_first_shard(&ctx, DOWN);
		jtc_assert_scan_and_layout(&ctx);
		/* single rebuild target in layout */
		check_grp_rebuilding_shard(&ctx, 2, 2, 1);
		/* for shard 0 (first shard) layout has 1 that is in rebuild
		 * state, but none in good state
		 */
		is_true(jtc_has_shard_with_target_rebuilding(&ctx, 0, NULL));
		is_false(jtc_has_shard_with_rebuilding_not_set(&ctx, 0));
		/* scan returns 1 target to rebuild, shard id should be 0,
		 * target should not be the "DOWN"ed target, and rebuild target
		 * should be same as target in layout
		 */
		assert_int_equal(0, ctx.rebuild.ids[0]);
		assert_int_not_equal(prev_first_shard.po_target,
				     ctx.rebuild.tgt_ranks[0]);
		assert_int_equal(jtc_get_layout_shard(&ctx, 0)->po_target,
				 ctx.rebuild.tgt_ranks[0]);

		prev_first_shard = *jtc_get_layout_shard(&ctx, 0);
	}

	jtc_set_status_on_first_shard(&ctx, DOWN);
	jtc_assert_scan_and_layout(&ctx);

	jtc_fini(&ctx);
}

/*
 * ------------------------------------------------
 * Transition from DOWN to DOWNOUT
 * ------------------------------------------------
 */
/*
 * This test simulates the first shard's target repeatedly being rebuilt, then
 * failing again.
 */
static void
chained_rebuild_completes_first_shard(void **state)
{
	struct jm_test_ctx	ctx;
	int			i;

	jtc_init_with_layout(&ctx, 9, 1, 1, OC_EC_2P1G1, g_verbose);

	/* fail/rebuild 6 targets, should still be one good one */
	for (i = 0; i < 6; i++) {
		/* First take it down, then downout indicating
		 * rebuild is done
		 */
		jtc_set_status_on_first_shard(&ctx, DOWN);
		jtc_set_status_on_first_shard(&ctx, DOWNOUT);
		jtc_assert_scan_and_layout(&ctx);

		assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
		assert_int_equal(0, ctx.rebuild.out_nr);
		assert_int_equal(0, ctx.reint.out_nr);
		assert_int_equal(0, ctx.new.out_nr);
		assert_int_equal(0, jtc_get_layout_rebuild_count(&ctx));
	}

	jtc_fini(&ctx);
}

/*
 * This test simulates all the shards' targets have failed and the new
 * targets are rebuilt successfully (failed goes to DOWNOUT state). Keep
 * "failing" until only enough targets are left for a single layout.
 * Should still be able to get that layout.
 */
static void
chained_rebuild_completes_all_at_once(void **state)
{
	struct jm_test_ctx	 ctx;

	jtc_init_with_layout(&ctx, 9, 1, 1, OC_EC_2P1G1, g_verbose);
	int i;

	/* fail two sets of layouts, should still be one good one layout */
	for (i = 0; i < 2; i++) {
		jtc_set_status_on_all_shards(&ctx, DOWN);
		jtc_set_status_on_all_shards(&ctx, DOWNOUT);
		jtc_assert_scan_and_layout(&ctx);

		assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
		assert_int_equal(0, ctx.rebuild.out_nr);
		assert_int_equal(0, ctx.reint.out_nr);
		assert_int_equal(0, ctx.new.out_nr);
	}

	jtc_fini(&ctx);
}

/*
 * ------------------------------------------------
 * Transition from DOWN to DOWNOUT to UP
 * ------------------------------------------------
 */
static void
one_is_being_reintegrated(void **state)
{
	struct jm_test_ctx	ctx;
	uint32_t		orig_target;
	uint32_t		rebuilt_target;
	int			shard_idx;
	const daos_oclass_id_t	oc = OC_RP_3G2;
	const uint32_t		oc_expected_target = 6;

	for (shard_idx = 0; shard_idx < oc_expected_target; shard_idx++) {
		verbose_msg("\nshard index: %d\n", shard_idx);
		/* create a layout with 4 targets (2 replica, 2 shards) */
		jtc_init_with_layout(&ctx, oc_expected_target + 1, 1, 2, oc,
				     g_verbose);

		/* simulate that the original target went down, but is now being
		 * reintegrated
		 */
		orig_target = jtc_layout_shard_tgt(&ctx, shard_idx);

		jtc_set_status_on_target(&ctx, DOWN, orig_target);
		jtc_set_status_on_target(&ctx, DOWNOUT, orig_target);
		jtc_assert_scan_and_layout(&ctx);
		rebuilt_target = jtc_layout_shard_tgt(&ctx, shard_idx);

		jtc_set_status_on_target(&ctx, UP, orig_target);
		ctx.rebuild.skip = true; /* DAOS-6516 */
		jtc_assert_scan_and_layout(&ctx);

		/* Should have 1 target rebuilding and 1 returned
		 * from find_reint
		 */
		/* NB: since extending target will go through NEW->UP->UPIN,
		 * so find_reint() and find_addition() might both find
		 * some candidates if there are UP targets in the pool map,
		 * no matter these UP targets are from NEW or DOWNOUT.
		 * But reintegration and extening will never happen at the
		 * same time, so it is ok for now. To satisfy the test,
		 * let's set both reint and new number as 1 for now.
		 */
		jtc_assert_rebuild_reint_new(ctx, 1, 0, 1, 1);

		/*
		 * make sure the original target is rebuilding and target
		 * that was rebuilt to when the target original target
		 * went down is not rebuilding
		 */
		is_true(jtc_has_shard_target_rebuilding(&ctx, shard_idx,
							orig_target));
		is_true(jtc_has_shard_target_not_rebuilding(&ctx, shard_idx,
							    rebuilt_target));

		/* Make sure the number of shard/targets in the layout is
		 * correct. There should be 1 extra shard/target item in the
		 * layout which has rebuilding set.
		 * Will actually have more items because the groups need to
		 * have the same size, but one of the groups will have an
		 * invalid shard/target.
		 */
		assert_int_equal(oc_expected_target + 1,
				 jtc_get_layout_nr(&ctx) -
				 jtc_get_layout_bad_count(&ctx));

		jtc_fini(&ctx);
	}
}

static void
down_back_to_up_in_same_order(void **state)
{
	struct jm_test_ctx	 ctx;
	uint32_t orig_shard_targets[2];

	jtc_init(&ctx, 6, 1, 4, OC_RP_4G1, g_verbose);
	ctx.enable_print_pool = false;
	jtc_assert_scan_and_layout(&ctx);

	/* remember the initial shards' targets */
	orig_shard_targets[0] = jtc_get_layout_shard(&ctx, 0)->po_target;
	orig_shard_targets[1] = jtc_get_layout_shard(&ctx, 1)->po_target;

	/* take a target down ... this one will impact first shard  */
	jtc_set_status_on_target(&ctx, DOWN, orig_shard_targets[0]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 1, 1, 1, 1);

	/* take a target down ... this one will impact second shard  */
	jtc_set_status_on_target(&ctx, DOWN, orig_shard_targets[1]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 2, 2, 2, 2);

	/* Bot are rebuilt now so status is DOWNOUT */
	jtc_set_status_on_target(&ctx, DOWNOUT, orig_shard_targets[0]);
	jtc_set_status_on_target(&ctx, DOWNOUT, orig_shard_targets[1]);

	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 0, 0, 0, 0);

	ctx.rebuild.skip = true; /* DAOS-6516 */

	jtc_set_status_on_target(&ctx, UP, orig_shard_targets[0]);
	jtc_assert_scan_and_layout(&ctx);

	/* NOTE: This is a really important test case. Even though this test
	 * seems like it should only move one shard (because only one target is
	 * being reintegrated), this particular combination happens to trigger
	 * extra data movement, resulting in two shards moving - one moving back
	 * to the reintegrated target, and one moving between two otherwise
	 * healthy targets because of the retry/collision mechanism of the jump
	 * map algorithm.
	 * Due to layout colocation, if the oid has been changed, then it could
	 * be 2 or even 3 as well, with current oid setting, this is 1.
	 */
	assert_int_equal(1, ctx.reint.out_nr);
	jtc_assert_rebuild_reint_new(ctx, 1, 0, 1, 1);

	/* Take second downed target up */
	jtc_set_status_on_target(&ctx, UP, orig_shard_targets[1]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 2, 0, 2, 2);


	jtc_fini(&ctx);
}

static void
down_back_to_up_in_reverse_order(void **state)
{
	struct jm_test_ctx	 ctx;
	uint32_t orig_shard_targets[2];

	jtc_init(&ctx, 6, 1, 4, OC_RP_4G1, g_verbose);
	ctx.enable_print_pool = false;
	jtc_assert_scan_and_layout(&ctx);

	/* remember the initial shards' targets */
	orig_shard_targets[0] = jtc_get_layout_shard(&ctx, 0)->po_target;
	orig_shard_targets[1] = jtc_get_layout_shard(&ctx, 1)->po_target;

	/* take a target down ... this one will impact first shard  */
	jtc_set_status_on_target(&ctx, DOWN, orig_shard_targets[0]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 1, 1, 1, 1);

	/* take a target down ... this one will impact second shard  */
	jtc_set_status_on_target(&ctx, DOWN, orig_shard_targets[1]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 2, 2, 2, 2);

	/* Bot are rebuilt now so status is DOWNOUT */
	jtc_set_status_on_target(&ctx, DOWNOUT, orig_shard_targets[0]);
	jtc_set_status_on_target(&ctx, DOWNOUT, orig_shard_targets[1]);

	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 0, 0, 0, 0);

	ctx.rebuild.skip = true; /* DAOS-6516 */

	jtc_set_status_on_target(&ctx, UP, orig_shard_targets[1]);
	jtc_assert_scan_and_layout(&ctx);
	assert_int_equal(1, ctx.reint.out_nr);
	jtc_assert_rebuild_reint_new(ctx, 1, 0, 1, 1);

	jtc_set_status_on_target(&ctx, UP, orig_shard_targets[0]);
	jtc_assert_scan_and_layout(&ctx);
	jtc_assert_rebuild_reint_new(ctx, 2, 0, 2, 2);

	jtc_fini(&ctx);
}

static void
all_are_being_reintegrated(void **state)
{
	struct jm_test_ctx	ctx;
	int			i;

	/* create a layout with 6 targets (3 replica, 2 shards) */
	jtc_init_with_layout(&ctx, 12, 1, 2, OC_RP_3G2, g_verbose);
	ctx.enable_print_pool = false;

	/* simulate that the original targets went down, but are now being
	 * reintegrated
	 */
	jtc_snapshot_layout_targets(&ctx); /* snapshot original targets */
	for (i = 0; i < jtc_get_layout_nr(&ctx); i++) {
		jtc_set_status_on_target(&ctx, DOWN, ctx.shard_targets[i]);
		jtc_set_status_on_target(&ctx, DOWNOUT, ctx.shard_targets[i]);
	}
	for (i = 0; i < jtc_get_layout_nr(&ctx); i++)
		jtc_set_status_on_target(&ctx, UP, ctx.shard_targets[i]);

	ctx.rebuild.skip = true; /* DAOS-6516 */
	jtc_assert_scan_and_layout(&ctx);

	/* Should be all 6 targets */
	assert_int_equal(6, ctx.reint.out_nr);
	assert_int_equal(6, jtc_get_layout_rebuild_count(&ctx));

	/* should have nothing in rebuild or addition */
	assert_int_equal(0, ctx.rebuild.out_nr);
	assert_int_equal(6, ctx.new.out_nr);

	/* each shard idx should have a rebuild target and a non
	 * rebuild target. The rebuild target should be the original shard
	 * before all went down.
	 */
	for (i = 0; i < 6; i++) {
		is_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
		is_true(jtc_has_shard_target_rebuilding(&ctx, i,
							ctx.shard_targets[i]));
	}

	jtc_fini(&ctx);
}

static void
down_up_sequences(void **state)
{
	struct jm_test_ctx	ctx;
	uint32_t		shard_target_1;
	uint32_t		shard_target_2;

	jtc_init(&ctx, 6, 1, 2, OC_RP_2G2, g_verbose);
	jtc_print_pool(&ctx);
	ctx.enable_print_pool = false;
	ctx.rebuild.skip = true; /* DAOS-6516 */

	jtc_assert_scan_and_layout(&ctx);
	shard_target_1 = jtc_get_layout_shard(&ctx, 0)->po_target;
	jtc_set_status_on_target(&ctx, DOWN, shard_target_1);
	jtc_set_status_on_target(&ctx, DOWNOUT, shard_target_1);

	jtc_assert_scan_and_layout(&ctx);
	shard_target_2 = jtc_get_layout_shard(&ctx, 0)->po_target;
	jtc_set_status_on_target(&ctx, DOWN, shard_target_2);
	jtc_set_status_on_target(&ctx, DOWNOUT, shard_target_2);

	jtc_set_status_on_target(&ctx, UP, shard_target_1);
	jtc_assert_scan_and_layout(&ctx);
	is_true(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_1));

	jtc_set_status_on_target(&ctx, UP, shard_target_2);
	jtc_assert_scan_and_layout(&ctx);
	is_true(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_1));

	is_false(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_2));

	jtc_fini(&ctx);
}

static void
down_up_sequences1(void **state)
{
	struct jm_test_ctx	ctx;
	uint32_t		shard_target_1;
	uint32_t		shard_target_2;

	jtc_init(&ctx, 6, 1, 2, OC_RP_2G2, g_verbose);
	jtc_print_pool(&ctx);
	ctx.rebuild.skip = true; /* DAOS-6516 */

	jtc_assert_scan_and_layout(&ctx);
	shard_target_1 = jtc_get_layout_shard(&ctx, 0)->po_target;
	jtc_set_status_on_target(&ctx, DOWN, shard_target_1);
	jtc_set_status_on_target(&ctx, DOWNOUT, shard_target_1);

	jtc_assert_scan_and_layout(&ctx);
	shard_target_2 = jtc_get_layout_shard(&ctx, 0)->po_target;
	jtc_set_status_on_target(&ctx, DOWN, shard_target_2);
	jtc_set_status_on_target(&ctx, DOWNOUT, shard_target_2);

	jtc_set_status_on_target(&ctx, UP, shard_target_2);
	jtc_assert_scan_and_layout(&ctx);
	is_true(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_2));

	jtc_set_status_on_target(&ctx, UP, shard_target_1);
	jtc_assert_scan_and_layout(&ctx);
	is_true(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_1));

	is_false(jtc_has_shard_moving_to_target(&ctx, 0, shard_target_2));

	jtc_fini(&ctx);
}

/*
 * ------------------------
 * Transition to DRAIN
 * ------------------------
 */

static void
drain_all_with_extra_domains(void **state)
{
	/*
	 * Drain all shards. There are plenty of extra domains to drain to.
	 * Number of targets should double, 1 DRAIN target
	 * (not "rebuilding") and the target being drained to (is "rebuilding")
	 */
	struct jm_test_ctx	 ctx;
	int			 i;
	const int		 shards_nr = 4; /* 2 x 2 */

	jtc_init_with_layout(&ctx, 4, 1, 2, OC_RP_2G2, false);

	/* drain all targets */
	jtc_set_status_on_all_shards(&ctx, DRAIN);
	jtc_assert_scan_and_layout(&ctx);

	/* there should be 2 targets for each shard, one
	 * rebuilding and one not
	 */
	assert_int_equal(8, jtc_get_layout_target_count(&ctx));

	assert_int_equal(4, jtc_get_layout_rebuild_count(&ctx));
	for (i = 0; i < shards_nr; i++) {
		is_true(jtc_has_shard_with_target_rebuilding(&ctx, i, NULL));
		is_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
	}

	jtc_fini(&ctx);
}

static void
drain_all_with_enough_targets(void **state)
{
	/*
	 * Drain all shards. There are extra targets, but not domains, to
	 * drain to.
	 */
	struct jm_test_ctx	 ctx;
	int			 i;
	const int		 shards_nr = 2; /* 2 x 1 */

	jtc_init_with_layout(&ctx, 2, 1, 4, OC_RP_2G1, g_verbose);

	/* drain all targets */
	jtc_set_status_on_all_shards(&ctx, DRAIN);
	jtc_assert_scan_and_layout(&ctx);

	/* there should be 2 targets for each shard, one
	 * rebuilding and one not
	 */
	for (i = 0; i < shards_nr; i++) {
		assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
		is_true(jtc_has_shard_with_target_rebuilding(&ctx, i, NULL));
		is_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
	}

	jtc_fini(&ctx);
}

static void
drain_target_same_shard_repeatedly_for_all_shards(void **state)
{
	struct jm_test_ctx	ctx;
	int			i;
	uint32_t		shard_id = 0;
	uint32_t		target;
	uint32_t		new_target;

	for (shard_id = 0; shard_id < 18; shard_id++) {
		jtc_init_with_layout(&ctx, 18 * 2, 1, 4, OC_EC_16P2G1,
				     g_verbose);
		ctx.enable_print_pool = false;
		verbose_msg("\nTesting with shard id: %d\n", shard_id);
		for (i = 0; i < 18 * 2 * 4 - 18; i++) {
			target = jtc_layout_shard_tgt(&ctx, shard_id);

			jtc_set_status_on_target(&ctx, DRAIN, target);
			jtc_assert_scan_and_layout(&ctx);
			is_true(jtc_has_shard_with_target_rebuilding(&ctx,
				shard_id, &new_target));

			is_true(jtc_has_shard_target_not_rebuilding(&ctx,
				shard_id, target));

			/* Drain finished, take target all the way down */
			jtc_set_status_on_target(&ctx, DOWNOUT, target);
			jtc_assert_scan_and_layout(&ctx);
			is_true(jtc_has_shard_target_not_rebuilding(&ctx,
				shard_id, new_target));
			verbose_msg("%d finished successfully\n\n", i);
		}

		target = jtc_layout_shard_tgt(&ctx, 0);

		jtc_set_status_on_target(&ctx, DRAIN, target);
		jtc_assert_scan_and_layout(&ctx);

		jtc_fini(&ctx);
	}
}

/*
 * ------------------------------------------------
 * Addition
 * ------------------------------------------------
 */
static void
one_server_is_added(void **state)
{
	struct jm_test_ctx	ctx;
	int			new_target_ids[] = {12, 13, 14, 15};

	jtc_init(&ctx, 4, 1, 3, OC_UNKNOWN, g_verbose);
	/* set oid so that it would place a shard in one of the last targets */
	assert_success(jtc_pool_map_extend(&ctx, 1, 1, 3));

	jtc_set_status_on_target(&ctx, EXTEND, 12);
	jtc_set_status_on_target(&ctx, EXTEND, 13);
	jtc_set_status_on_target(&ctx, EXTEND, 14);
	pool_map_init_in_fseq(ctx.po_map);

	/* Make sure that the oid will place on the added target ids */
	is_true(jtc_set_oid_with_shard_in_targets(&ctx, new_target_ids,
						  ARRAY_SIZE(new_target_ids),
						  OC_RP_3G1));
	jtc_assert_scan_and_layout(&ctx);

	/* might have more than one because of other potential data movement,
	 * but should have at least 1
	 */
	is_true(ctx.new.out_nr > 0);
	assert_int_equal(0, ctx.rebuild.out_nr);
	assert_int_equal(1, ctx.reint.out_nr);

	assert_int_equal(ctx.new.out_nr, jtc_get_layout_rebuild_count(&ctx));

	jtc_fini(&ctx);
}

/*
 * ------------------------------------------------
 * Leave in multiple states at same time (no addition)
 * ------------------------------------------------
 */
static void
placement_handles_multiple_states(void **state)
{
	struct jm_test_ctx ctx;
	int ver_after_reint;
	int ver_after_fail;
	int ver_after_drain;
	int ver_after_reint_complete;
	uint32_t reint_tgt_id;
	uint32_t fail_tgt_id;
	uint32_t rebuilding;

	jtc_init_with_layout(&ctx, 4, 1, 8, OC_RP_3G1, g_verbose);

	/* first shard goes down, rebuilt, then reintegrated */
	jtc_set_status_on_shard_target(&ctx, DOWN, 0);
	jtc_set_status_on_shard_target(&ctx, DOWNOUT, 0);
	jtc_set_status_on_shard_target(&ctx, UP, 0);
	reint_tgt_id = jtc_layout_shard_tgt(&ctx, 0);
	assert_success(jtc_create_layout(&ctx));

	rebuilding = jtc_get_layout_rebuild_count(&ctx);
	/* One thing reintegrating */
	assert_int_equal(1, rebuilding);

	/*
	 * Reintegration is now in progress. Grab the version from here
	 * for find reint count
	 */
	ver_after_reint = ctx.ver;

	/* second shard goes down */
	jtc_set_status_on_shard_target(&ctx, DOWN, 1);
	fail_tgt_id = jtc_layout_shard_tgt(&ctx, 1);
	assert_success(jtc_create_layout(&ctx));

	ver_after_fail = ctx.ver;

	rebuilding = jtc_get_layout_rebuild_count(&ctx);
	/* One reintegrating plus one failure recovery, but due to co-location,
	 * some other shards might change their location either after remap.
	 **/
	assert_true(rebuilding >= 2);

	/* third shard is queued for drain */
	jtc_set_status_on_shard_target(&ctx, DRAIN, 2);
	assert_success(jtc_create_layout(&ctx));

	/*
	 * Reintegration is still running, but these other operations have
	 * happened too and are now queued.
	 */
	ver_after_drain = ctx.ver;

	/* During drain or extending, some targets might be in both original
	 * and extending area.
	 */
	/* is_false(jtc_layout_has_duplicate(&ctx)); */

	/*
	 * Compute placement in this state. All three shards should
	 * be moving around
	 */
	jtc_scan(&ctx);
	rebuilding = jtc_get_layout_rebuild_count(&ctx);
	assert_true(rebuilding >= 3);

	/*
	 * Compute find_reint() using the correct version of rebuild which
	 * would have launched when reintegration started
	 *
	 * find_reint() should find two shards to move at this version, one for
	 * reint(UP), one for rebuild(DOWN).
	 */
	ctx.ver = ver_after_reint;
	jtc_scan(&ctx);
	assert_int_equal(ctx.reint.out_nr, 2);

	/* Complete the reintegration */
	ctx.ver = ver_after_drain; /* Restore the version first */
	jtc_set_status_on_target(&ctx, UPIN, reint_tgt_id);
	ver_after_reint_complete = ctx.ver;

	/* This would start processing the failure - so check that it'd just
	 * move one thing
	 */
	ctx.ver = ver_after_fail;
	jtc_scan(&ctx);
	assert_int_equal(ctx.rebuild.out_nr, 1);

	/* Complete the rebuild */
	ctx.ver = ver_after_reint_complete; /* Restore the version first */
	jtc_set_status_on_target(&ctx, DOWNOUT, fail_tgt_id);

	/* This would start processing the drain - so check that it'd just
	 * move one thing
	 */
	ctx.ver = ver_after_drain;
	jtc_scan(&ctx);
	assert_int_equal(ctx.rebuild.out_nr, 1);

	/* Remainder is simple / out of scope for this test */

	jtc_fini(&ctx);
}


/*
 * ------------------------------------------------
 * Leave in multiple states at same time (including addition)
 * ------------------------------------------------
 */
static void
placement_handles_multiple_states_with_addition(void **state)
{
	struct jm_test_ctx	ctx;
	uint32_t		rebuilding;

	jtc_init_with_layout(&ctx, 3, 1, 4, OC_RP_3G1, g_verbose);
	/* first shard goes down, rebuilt, then back up */
	jtc_set_status_on_shard_target(&ctx, DOWN, 0);
	jtc_set_status_on_shard_target(&ctx, DOWNOUT, 0);
	jtc_set_status_on_shard_target(&ctx, UP, 0);

	/* a new domain is added */
	jtc_pool_map_extend(&ctx, 1, 1, 4);
	jtc_set_status_on_target(&ctx, EXTEND, 12);
	jtc_set_status_on_target(&ctx, EXTEND, 13);
	jtc_set_status_on_target(&ctx, EXTEND, 14);
	jtc_set_status_on_target(&ctx, EXTEND, 15);
	pool_map_init_in_fseq(ctx.po_map);

	assert_int_equal(pool_map_target_nr(ctx.po_map), 16);

	/* second shard goes down */
	jtc_set_status_on_shard_target(&ctx, DOWN, 1);

	assert_success(jtc_create_layout(&ctx));

	is_false(jtc_layout_has_duplicate(&ctx));

	jtc_scan(&ctx);
	rebuilding = jtc_get_layout_rebuild_count(&ctx);

	/* 1 each for down, up, new ... maybe? */
	assert_true(rebuilding == 2 || rebuilding == 3);

	/* Both DOWN and UP target will be remapped during remap */
	assert_int_equal(ctx.rebuild.out_nr, 2);
	/* Adding new targets might make original shard to be remapped
	 * to new location.
	 */
	assert_true(ctx.reint.out_nr == 1 || ctx.reint.out_nr == 2);

	/* JCH might cause multiple shards remap to the new target */
	assert_true(ctx.new.out_nr >= 1);

	jtc_fini(&ctx);
}

/* The following will test non standard layouts and that:
 * - a layout is able to be created with several different randomly generated
 *   object IDs
 * - that no duplicate targets are used
 * - layout contains expected number of targets
 */

#define TEST_NON_STANDARD_SYSTEMS(domain_count, domain_targets, oc, \
				   expected_target_nr) \
	test_non_standard_systems(__FILE__, __LINE__, domain_count, \
		domain_targets, oc, \
		expected_target_nr)

static void
test_non_standard_systems(const char *file, uint32_t line,
			  uint32_t domain_count, uint32_t *domain_targets,
			  int oc, int expected_target_nr)
{
	struct jm_test_ctx	ctx;
	int			i;

	jtc_init_non_standard(&ctx, domain_count, domain_targets, oc,
			      g_verbose);

	/* test several different object IDs */
	srand(time(NULL));
	for (i = 0; i < 1024; i++) {
		jtc_set_object_meta(&ctx, oc, rand(), rand());
		assert_success(jtc_create_layout(&ctx));
		if (expected_target_nr != ctx.layout->ol_nr) {
			jtc_print_layout_force(&ctx);
			fail_msg("%s:%d expected_target_nr(%d) "
				 "!= ctx.layout->ol_nr(%d)",
				file, line, expected_target_nr,
				ctx.layout->ol_nr);
		}
#if 0
		if (jtc_layout_has_duplicate(&ctx)) {
			jtc_print_layout_force(&ctx);
			print_message("%s:%d Found duplicate for i=%d\n",
				      file, line, i);
		}
#endif
	}

	jtc_fini(&ctx);
}

static void
unbalanced_config(void **state)
{
	uint32_t domain_targets_nr = 10;
	uint32_t domain_targets[domain_targets_nr];
	uint32_t total_targets = 0;

	uint32_t i;

	/* First domain is huge, second is small, 2 targets used */
	domain_targets[0] = 50;
	domain_targets[1] = 2;
	TEST_NON_STANDARD_SYSTEMS(2, domain_targets, OC_RP_2G1, 2);

	/* Reverse: First domain is small, second is huge */
	domain_targets[0] = 2;
	domain_targets[1] = 50;
	TEST_NON_STANDARD_SYSTEMS(2, domain_targets,
				  OC_RP_2G1, 2);

	/* each domain has a different number of targets */
	for (i = 0; i < domain_targets_nr; i++) {
		domain_targets[i] = (i + 1) * 2;
		total_targets += domain_targets[i];
	}

	TEST_NON_STANDARD_SYSTEMS(domain_targets_nr, domain_targets,
				  OC_RP_3G2, 6);

	TEST_NON_STANDARD_SYSTEMS(domain_targets_nr, domain_targets,
				  OC_RP_3GX, (total_targets / 3) * 3);

	/* 2 domains with plenty of targets, 1 domain only has 1. Should still
	 * have plenty of places to put shards
	 */
	domain_targets[0] = 1;
	domain_targets[1] = 5;
	domain_targets[2] = 5;
	TEST_NON_STANDARD_SYSTEMS(3, domain_targets, OC_RP_2G2, 4);
}

static void
check_grp_not_in_same_domain(struct jm_test_ctx *ctx, uint32_t grp_size,
			     uint32_t grp_nr, uint32_t tgt_nr, bool no_duplicate)
{
	int i, j, k;
	uint32_t tgt;
	uint32_t other_tgt;
	int	 miss_cnt = 0;

	for (i = 0; i < grp_nr; i++) {
		for (j = 0; j < grp_size; j++) {
			tgt = jtc_layout_shard_tgt(ctx, grp_size * i + j);
			for (k = j + 1; k < grp_size; k++) {
				other_tgt = jtc_layout_shard_tgt(ctx, grp_size * i + k);
				if (tgt/tgt_nr == other_tgt/tgt_nr) {
					print_message("tgt %u other tgt %u\n", tgt, other_tgt);
					print_message("grp_size %u grp_nr %u tgt_nr %u\n",
						      grp_size, grp_nr, tgt_nr);
					print_message("i %d, j %d k %d\n", i, j, k);
					miss_cnt++;
				}
			}
		}
	}

	assert_true(miss_cnt <= 0);

	if (no_duplicate)
		assert_true(!jtc_layout_has_duplicate(ctx));
}

static void
same_group_shards_not_in_same_domain(void **state)
{
	struct jm_test_ctx	ctx;

	if (!fail_domain_node) {
		print_message("check EC_4P2GX 4 x 2\n");
		jtc_init_with_layout(&ctx, 4, 2, 8, OC_EC_4P2GX, g_verbose);
		check_grp_not_in_same_domain(&ctx, 6, 10, 8, true);
		jtc_fini(&ctx);

		print_message("check EC_8P2GX 6 x 2\n");
		jtc_init_with_layout(&ctx, 6, 2, 8, OC_EC_8P2GX, g_verbose);
		check_grp_not_in_same_domain(&ctx, 10, 9, 8, true);
		jtc_fini(&ctx);
	}

	print_message("check EC_4P2GX 8 x 1\n");
	jtc_init_with_layout(&ctx, 8, 1, 8, OC_EC_4P2GX, g_verbose);
	check_grp_not_in_same_domain(&ctx, 6, 10, 8, true);
	jtc_fini(&ctx);

	print_message("check EC_8P2GX 12 x 1\n");
	jtc_init_with_layout(&ctx, 12, 1, 8, OC_EC_8P2GX, g_verbose);
	check_grp_not_in_same_domain(&ctx, 10, 9, 8, true);
	jtc_fini(&ctx);

	print_message("check EC_2P1G32 16 x 2\n");
	jtc_init_with_layout(&ctx, 16, 2, 4, OC_EC_2P1G32, g_verbose);
	check_grp_not_in_same_domain(&ctx, 3, 32, 4, true);
	jtc_fini(&ctx);

	print_message("check EC_16P2G32 18 x 1\n");
	jtc_init_with_layout(&ctx, 18, 1, 32, OC_EC_16P2G32, g_verbose);
	check_grp_not_in_same_domain(&ctx, 18, 32, 32, true);
	jtc_fini(&ctx);

	print_message("check EC_16P2G32 32 x 1\n");
	jtc_init_with_layout(&ctx, 32, 1, 18, OC_EC_16P2G32, g_verbose);
	check_grp_not_in_same_domain(&ctx, 18, 32, 18, false);
	jtc_fini(&ctx);
}

static void
same_group_shards_not_in_same_domain_multiple_objs(void **state)
{
	struct jm_test_ctx	ctx;
	int			h;

	jtc_init(&ctx, 4, 1, 8, OC_EC_2P1GX, g_verbose);
	for (h = 0; h < 1000; ++h) {
		jtc_set_object_meta(&ctx, OC_EC_2P1GX, h + 1, UINT_MAX);
		assert_success(jtc_create_layout(&ctx));
		check_grp_not_in_same_domain(&ctx, 3, 10, 8, true);
	}
	jtc_fini(&ctx);

	jtc_init(&ctx, 6, 1, 8, OC_EC_4P1GX, g_verbose);
	for (h = 0; h < 1000; ++h) {
		jtc_set_object_meta(&ctx, OC_EC_4P1GX, h + 1, UINT_MAX);
		assert_success(jtc_create_layout(&ctx));
		check_grp_not_in_same_domain(&ctx, 5, 9, 8, true);
	}
	jtc_fini(&ctx);
}

static void
large_shards_over_limited_targets(void **state)
{
	struct jm_test_ctx	ctx;
	int i;

	jtc_init_with_layout(&ctx, 4, 1, 8, OC_RP_2G8, g_verbose);
	for (i = 0; i < 8; i++) {
		jtc_set_status_on_target(&ctx, DOWN, i);
		jtc_scan(&ctx);
		jtc_set_status_on_target(&ctx, DOWNOUT, i);
	}

	assert_success(jtc_create_layout(&ctx));

	for (i = 24; i < 32; i++) {
		jtc_set_status_on_target(&ctx, DOWN, i);
		jtc_scan(&ctx);
		jtc_set_status_on_target(&ctx, DOWNOUT, i);
	}

	assert_success(jtc_create_layout(&ctx));

	jtc_fini(&ctx);
}

static void
_multiple_shards_in_the_same_target(void **state, uint32_t obj_class,
				    uint32_t grp_size, uint32_t grp_cnt)
{
	struct jm_test_ctx	ctx;
	int			tgt;
	int			miss;
	int			i;
	int			j;
	int			k;

	jtc_init_with_layout(&ctx, grp_size, 1, grp_cnt, obj_class, g_verbose);
	for (i = 0; i < grp_size * grp_cnt - 1; i++) {
		jtc_set_status_on_target(&ctx, DOWN, i);
		jtc_scan(&ctx);
		jtc_set_status_on_target(&ctx, DOWNOUT, i);
	}

	miss = 0;
	assert_success(jtc_create_layout(&ctx));
	tgt = jtc_layout_shard_tgt(&ctx, 0);
	for (i = 0; i < grp_cnt; i++) {
		for (j = 0; j < grp_size; j++) {
			int other_tgt = jtc_layout_shard_tgt(&ctx, grp_size * i + j);

			if (other_tgt == -1 || other_tgt != tgt)
				miss++;
		}
	}

	assert_rc_equal(miss, 0);

	for (i = 0; i < grp_size * grp_cnt - 1; i++) {
		jtc_set_status_on_target(&ctx, UP, i);
		jtc_scan(&ctx);
		jtc_set_status_on_target(&ctx, UPIN, i);
	}

	miss = 0;
	assert_success(jtc_create_layout(&ctx));
	for (i = 0; i < grp_cnt; i++) {
		for (j = 0; j < grp_size; j++) {
			tgt = jtc_layout_shard_tgt(&ctx, grp_size * i + j);
			for (k = j + 1; k < grp_size; k++) {
				int other_tgt = jtc_layout_shard_tgt(&ctx, grp_size * i + k);

				if (other_tgt == tgt || other_tgt / grp_cnt == tgt / grp_cnt)
					miss++;
			}
		}
	}

	assert_rc_equal(miss, 0);
	jtc_fini(&ctx);
}

static void
multiple_shards_in_the_same_target(void **state)
{
	print_message("Check OC_RP_2G4\n");
	_multiple_shards_in_the_same_target(state, OC_RP_2G4, 2, 4);
	print_message("Check OC_RP_6G1\n");
	_multiple_shards_in_the_same_target(state, OC_RP_6G1, 6, 1);
	print_message("Check OC_EC_4P2G8\n");
	_multiple_shards_in_the_same_target(state, OC_EC_4P2G8, 6, 8);
	print_message("Check OC_EC_8P2G8\n");
	_multiple_shards_in_the_same_target(state, OC_EC_8P2G8, 10, 8);
	print_message("Check OC_EC_16P2G8\n");
	_multiple_shards_in_the_same_target(state, OC_EC_16P2G8, 18, 8);
}

static void
shards_over_xpf(void **state)
{
	struct jm_test_ctx	ctx;
	int			tgt = -1;
	int			i;
	int			rebuilding_shards = 0;

	jtc_init_with_layout(&ctx, 8, 1, 8, OC_RP_XSF, g_verbose);
	for (i = 56; i < 64; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	assert_success(jtc_create_layout(&ctx));
	for (i = 0; i < ctx.layout->ol_nr; i++) {
		if (ctx.layout->ol_shards[i].po_rebuilding) {
			tgt = ctx.layout->ol_shards[i].po_target;
			break;
		}
	}

	assert_true(tgt != -1);
	for (i = (tgt / 8) * 8; i < (tgt/ 8 + 1) * 8; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	assert_success(jtc_create_layout(&ctx));
	for (i = 56; i < 64; i++)
		jtc_set_status_on_target(&ctx, DOWNOUT, i);

	assert_success(jtc_create_layout(&ctx));

	for (i = 0; i < ctx.layout->ol_nr; i++) {
		if (ctx.layout->ol_shards[i].po_rebuilding)
			rebuilding_shards++;
	}

	assert_true(rebuilding_shards == 2);

	jtc_fini(&ctx);
}

static void
_same_group_shards_not_in_same_domain_fail(void **state, uint32_t oclass, uint32_t grp_size,
					   uint32_t grp_nr, uint32_t domain_nr, uint32_t rank_nr,
					   uint32_t tgt_nr)
{
	struct jm_test_ctx	ctx;
	int	i;

	jtc_init_with_layout(&ctx, domain_nr, rank_nr, tgt_nr, oclass, g_verbose);
	check_grp_not_in_same_domain(&ctx, grp_size, grp_nr, tgt_nr, true);

	for (i = 0; i < tgt_nr; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	assert_success(jtc_create_layout(&ctx));
	check_grp_not_in_same_domain(&ctx, grp_size, grp_nr, tgt_nr, true);

	for (i = tgt_nr; i < 2 * tgt_nr; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	assert_success(jtc_create_layout(&ctx));
	check_grp_not_in_same_domain(&ctx, grp_size, grp_nr, tgt_nr, true);

	jtc_fini(&ctx);
}

static void
same_group_shards_not_in_same_domain_with_fail(void **state)
{
	if (!fail_domain_node) {
		print_message("check OC_EC_4P2G8 with 4 x 2.\n");
		_same_group_shards_not_in_same_domain_fail(state, OC_EC_4P2G8, 6, 8, 4, 2, 8);
		print_message("check OC_EC_16P2G8 with 10 x 2.\n");
		_same_group_shards_not_in_same_domain_fail(state, OC_EC_16P2G8, 18, 8, 10, 2, 8);
		print_message("check OC_EC_8P2G8 with 6 x 2.\n");
		_same_group_shards_not_in_same_domain_fail(state, OC_EC_8P2G8, 10, 8, 6, 2, 8);
	}

	print_message("check OC_EC_4P2G8 with 8 x 1.\n");
	_same_group_shards_not_in_same_domain_fail(state, OC_EC_4P2G8, 6, 8, 8, 1, 8);
	print_message("check OC_EC_8P2G8 with 12 x 1.\n");
	_same_group_shards_not_in_same_domain_fail(state, OC_EC_8P2G8, 10, 8, 12, 1, 8);
	print_message("check OC_EC_16P2G8 with 20 x 1.\n");
	_same_group_shards_not_in_same_domain_fail(state, OC_EC_16P2G8, 18, 8, 20, 1, 8);
}

static void
_fail_shard_during_reintegration(void **state, uint32_t domain_nr, uint32_t node_nr,
				 uint32_t tgt_nr, uint32_t objclass, uint32_t reint_tgt_start,
				 uint32_t reint_tgt_nr, uint32_t fail_tgt_start,
				 uint32_t fail_tgt_nr, uint32_t accept_remain)
{
	struct jm_test_ctx	ctx;
	struct pl_obj_layout	*reint_layout;
	int			i, j;
	uint32_t		grp_size;

	jtc_init_with_layout(&ctx, domain_nr, node_nr, tgt_nr, objclass, g_verbose);
	for (i = reint_tgt_start; i < reint_tgt_start + reint_tgt_nr; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	for (i = reint_tgt_start; i < reint_tgt_start + reint_tgt_nr; i++)
		jtc_set_status_on_target(&ctx, DOWNOUT, i);

	for (i = reint_tgt_start; i < reint_tgt_start + reint_tgt_nr; i++)
		jtc_set_status_on_target(&ctx, UP, i);

	assert_success(jtc_create_layout(&ctx));
	reint_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;
	/* Fail nodes during reintegration */
	for (i = fail_tgt_start; i < fail_tgt_start + fail_tgt_nr; i++)
		jtc_set_status_on_target(&ctx, DOWN, i);

	assert_success(jtc_create_layout(&ctx));
	grp_size = min(reint_layout->ol_grp_size, ctx.layout->ol_grp_size);
	for (i = 0; i < reint_layout->ol_grp_nr; i++) {
		int remain = 0;

		for (j = 0; j < grp_size; j++) {
			struct pl_obj_shard	*shard;
			struct pl_obj_shard	*new_shard;

			shard = &reint_layout->ol_shards[i * reint_layout->ol_grp_size + j];
			new_shard = &ctx.layout->ol_shards[i * ctx.layout->ol_grp_size + j];
			if (shard->po_target == new_shard->po_target &&
			    !new_shard->po_rebuilding && !new_shard->po_reintegrating)
				remain++;
		}
		assert_true(remain >= accept_remain);
	}

	pl_obj_layout_free(reint_layout);
	jtc_fini(&ctx);
}

static void
fail_shard_during_reintegration(void **state)
{
	print_message("check 4 2 8 OC_RP_3GX\n");
	_fail_shard_during_reintegration(state, 4, 2, 8, OC_RP_3GX, 0, 8, 8, 8, 2);
	print_message("check 8 1 8 OC_RP_3GX\n");
	_fail_shard_during_reintegration(state, 8, 1, 8, OC_RP_3GX, 0, 8, 8, 8, 2);
	print_message("check 32 1 16 OC_EC_8P2GX\n");
	_fail_shard_during_reintegration(state, 32, 1, 16, OC_EC_8P2GX, 0, 16, 16, 32, 8);
	print_message("check 200 1 16 OC_EC_16P2GX case 1\n");
	_fail_shard_during_reintegration(state, 200, 1, 16, OC_EC_16P2GX, 0, 16, 16, 32, 16);
	print_message("check 200 1 16 OC_EC_16P2GX case 2\n");
	_fail_shard_during_reintegration(state, 200, 1, 16, OC_EC_16P2GX, 0, 16, 16, 32, 16);
	print_message("check 200 1 16 OC_EC_16P2GX case 3\n");
	_fail_shard_during_reintegration(state, 200, 1, 16, OC_EC_16P2GX, 0, 16, 48, 32, 16);
}

static void
compare_layout(struct pl_obj_layout *layout1, struct pl_obj_layout *layout2,
	       uint32_t min_diff)
{
	int i, j;

	for (i = 0; i < layout1->ol_grp_nr; i++) {
		int diff = 0;

		for (j = 0; j < layout1->ol_grp_size; j++) {
			struct pl_obj_shard	*shard1;
			struct pl_obj_shard	*shard2;

			shard1 = &layout1->ol_shards[i * layout1->ol_grp_size + j];
			shard2 = &layout2->ol_shards[i * layout2->ol_grp_size + j];
			if (shard1->po_target != shard2->po_target || shard1->po_target == -1 ||
			    shard2->po_target == -1 || shard1->po_rebuilding ||
			    shard1->po_reintegrating || shard2->po_rebuilding ||
			    shard2->po_reintegrating)
				diff++;
		}
		assert_true(diff <= min_diff);
	}
}

static void
_fail_reintegrate_multiple_ranks(void **state, uint32_t domain_nr, uint32_t node_nr,
				 uint32_t tgt_nr, uint32_t objclass, uint32_t *fail_tgt_offsets,
				 uint32_t *fail_tgt_nrs, uint32_t fail_tgt_num,
				 uint32_t accept_remain)
{
	struct jm_test_ctx	ctx;
	struct pl_obj_layout	*origin_layout;
	struct pl_obj_layout	*fail_layout;
	struct pl_obj_layout	*reint_layout;
	int			i, j;

	jtc_init_with_layout(&ctx, domain_nr, node_nr, tgt_nr, objclass, g_verbose);
	assert_success(jtc_create_layout(&ctx));
	origin_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < fail_tgt_nrs[i]; j++) {
			uint32_t tgt = j + fail_tgt_offsets[i];

			jtc_set_status_on_target(&ctx, DOWN, tgt);
			jtc_set_status_on_target(&ctx, DOWNOUT, tgt);
		}
	}

	assert_success(jtc_create_layout(&ctx));
	fail_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;
	compare_layout(origin_layout, fail_layout, 2);

	pl_obj_layout_free(origin_layout);
	origin_layout = fail_layout;
	for (i = 2; i < 4; i++) {
		for (j = 0; j < fail_tgt_nrs[i]; j++) {
			uint32_t tgt = j + fail_tgt_offsets[i];

			jtc_set_status_on_target(&ctx, DOWN, tgt);
			jtc_set_status_on_target(&ctx, DOWNOUT, tgt);
		}
	}
	assert_success(jtc_create_layout(&ctx));
	fail_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;
	compare_layout(origin_layout, fail_layout, 2);

	pl_obj_layout_free(origin_layout);
	origin_layout = fail_layout;
	for (i = 3; i >= 2; i--) {
		for (j = 0; j < fail_tgt_nrs[i]; j++) {
			uint32_t tgt = j + fail_tgt_offsets[i];

			jtc_set_status_on_target(&ctx, UP, tgt);
			jtc_set_status_on_target(&ctx, UPIN, tgt);
		}
	}
	assert_success(jtc_create_layout(&ctx));
	reint_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;
	compare_layout(origin_layout, reint_layout, 2);

	pl_obj_layout_free(origin_layout);
	origin_layout = reint_layout;
	for (i = 0; i <= 2; i++) {
		for (j = 0; j < fail_tgt_nrs[i]; j++) {
			uint32_t tgt = j + fail_tgt_offsets[i];

			jtc_set_status_on_target(&ctx, UP, tgt);
			jtc_set_status_on_target(&ctx, UPIN, tgt);
		}
	}
	assert_success(jtc_create_layout(&ctx));
	reint_layout = ctx.layout;
	ctx.is_layout_set = false;
	ctx.layout = NULL;
	compare_layout(origin_layout, reint_layout, 2);

	pl_obj_layout_free(origin_layout);
	pl_obj_layout_free(reint_layout);
	jtc_fini(&ctx);
}

static void
fail_reintegrate_multiple_ranks(void **state)
{
	uint32_t fail_tgt_offsets[4];
	uint32_t fail_tgt_nrs[4];

	fail_tgt_offsets[0]=0;
	fail_tgt_offsets[1]=16;
	fail_tgt_offsets[2]=32;
	fail_tgt_offsets[3]=64;
	fail_tgt_nrs[0] = 8;
	fail_tgt_nrs[1] = 8;
	fail_tgt_nrs[2] = 8;
	fail_tgt_nrs[3] = 8;
	print_message("check 20 2 8 OC_RP_3GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_RP_3GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);
	print_message("check 20 2 8 EC_8P2_GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_EC_8P2GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);
	print_message("check 20 2 8 EC_16P2_GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_EC_16P2GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);

	print_message("check 40 1 8 OC_RP_3GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_RP_3GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);
	print_message("check 40 1 8 EC_8P2_GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_EC_8P2GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);
	print_message("check 40 1 8 EC_16P2_GX\n");
	_fail_reintegrate_multiple_ranks(state, 20, 2, 8, OC_EC_16P2GX, fail_tgt_offsets,
					 fail_tgt_nrs, 3, 2);


}

static void
fail_multiple_ranks(void **state)
{
	struct jm_test_ctx	ctx;
	int i;

	jtc_init(&ctx, 3, 2, 2, OC_RP_2G4, g_verbose);

	jtc_set_status_on_target(&ctx, DOWN, 0);
	jtc_set_status_on_target(&ctx, DOWN, 1);
	jtc_set_status_on_target(&ctx, DOWN, 4);
	jtc_set_status_on_target(&ctx, DOWN, 5);

	for (i = 0; i < 100; ++i) {
		daos_obj_set_oid(&ctx.oid, DAOS_OT_ARRAY_BYTE, OR_RP_2, 4, 0);
		ctx.oid.lo = i;
		assert_success(jtc_create_layout(&ctx));
		if (fail_domain_node)
			check_grp_not_in_same_domain(&ctx, 2, 4, 4, false);
		else
			check_grp_not_in_same_domain(&ctx, 2, 4, 2, false);

	}

	jtc_fini(&ctx);
}

/*
 * ------------------------------------------------
 * End Test Cases
 * ------------------------------------------------
 */

static int
placement_test_setup(void **state)
{
	assert_success(obj_class_init());

	return pl_init();
}

static int
placement_test_teardown(void **state)
{
	pl_fini();
	obj_class_fini();

	return 0;
}

#define WIP(dsc, test) { "WIP PLACEMENT "STR(__COUNTER__)" ("#test"): " dsc, \
			  test, placement_test_setup, placement_test_teardown }
#define T(dsc, test) { "PLACEMENT "STR(__COUNTER__)" ("#test"): " dsc, test, \
			  placement_test_setup, placement_test_teardown }

static const struct CMUnitTest tests[] = {
	/* Standard configurations */
	T("Target for first shard continually goes to DOWN state and "
	  "never finishes rebuild. Should still get new target until no more",
	  down_continuously),
	T("Object class is verified appropriately", object_class_is_verified),
	T("With all healthy targets, can create layout, nothing is in "
	  "rebuild, and no duplicates.", all_healthy),
	/* DOWN */
	T("Take a target down in a system with no servers available, but "
	  "should still collocate", down_to_target),
	T("Target for first shard continually goes to DOWN state and "
	  "never finishes rebuild. Should still get new target until no more",
	  down_continuously),
	/* DOWNOUT */
	T("Rebuild first shard's target repeatedly",
	  chained_rebuild_completes_first_shard),
	T("Rebuild all shards' targets", chained_rebuild_completes_all_at_once),
	/* UP */
	T("For each shard at a time, take the shard's target "
	    "DOWN->DOWNOUT->UP. Then verify that the reintegration looks "
	    "correct", one_is_being_reintegrated),
	T("With all targets being reintegrated, make sure the correct "
	    "targets are being rebuilt.", all_are_being_reintegrated),
	T("Take a single shard's target down, downout, then again with the "
	  "new target. Then reintegrate the first downed target, "
	  "then the second.", down_up_sequences),
	T("Take a single shard's target down, downout, then again with the "
	  "new target. Then reintegrate the second downed target, "
	  "then the first (Reverse of previous test).", down_up_sequences1),
	T("multiple shard targets go down, then are reintegrated in the "
	  "same order they were brought down",
	  down_back_to_up_in_same_order),
	T("multiple targets go down for the same shard, then are reintegrated "
	  "in reverse order than how they were brought down",
	  down_back_to_up_in_reverse_order),
	/* DRAIN */
	T("Drain all shards with extra domains", drain_all_with_extra_domains),
	T("Drain all shards with extra targets",
	  drain_all_with_enough_targets),
	T("Drain the target of the first shard repeatedly until there is no "
	    "where to drain to.",
	    drain_target_same_shard_repeatedly_for_all_shards),
	/* NEW */
	T("A server is added and an object id is chosen that requires "
	  "data movement to the new server",
	  one_server_is_added),
	/* Multiple */
	T("Placement can handle multiple states (excluding addition)",
	  placement_handles_multiple_states),
	T("Placement can handle multiple states (including addition)",
	  placement_handles_multiple_states_with_addition),
	/* Non-standard system setups*/
	T("Non-standard system configurations. All healthy",
	  unbalanced_config),
	T("shards in the same group not in the same domain",
	  same_group_shards_not_in_same_domain),
	T("shards in the same group not in the same domain with multiple objects",
	  same_group_shards_not_in_same_domain_multiple_objs),
	T("large shards over limited targets",
	  large_shards_over_limited_targets),
	T("multiple shards in the same target", multiple_shards_in_the_same_target),
	T("shards over xpf", shards_over_xpf),
	T("same group shards not in the same domain fail",
	  same_group_shards_not_in_same_domain_with_fail),
	T("fail shard during reintegration",
	  fail_shard_during_reintegration),
	T("fail reintegrate ranks", fail_reintegrate_multiple_ranks),
	T("fail multiple ranks", fail_multiple_ranks),
};

int
placement_tests_run(bool verbose)
{
	int rc = 0;

	g_verbose = verbose;

	rc += cmocka_run_group_tests_name("Jump Map Placement Tests", tests,
					  NULL, NULL);

	return rc;
}
