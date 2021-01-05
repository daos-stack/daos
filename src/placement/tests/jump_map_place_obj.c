/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include "place_obj_common.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>

static bool g_verbose;

#define skip_msg(msg) do { print_message("Skipping > "msg"\n"); skip(); } \
			while (0)

void verbose_print(char *msg, ...)
{
	if (g_verbose) {
		va_list vargs;

		va_start(vargs, msg);
		vprint_message(msg, vargs);
		va_end(vargs);
	}
}

#define DOM_NR		18
#define	NODE_PER_DOM	1
#define VOS_PER_TARGET	4
#define SPARE_MAX_NUM	(DOM_NR * 3)
#define COMPONENT_NR	(DOM_NR + DOM_NR * NODE_PER_DOM + \
			 DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)
#define NUM_TARGETS	(DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)

#define TEST_PER_OC 10

static bool			 pl_debug_msg;

void
placement_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	struct pl_obj_layout	*layout;
	daos_obj_id_t		oid;
	int			test_num;

	gen_pool_and_placement_map(DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_JUMP_MAP,
				   &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);

	srand(time(NULL));
	oid.hi = 5;

	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		oid.lo = rand();
		daos_obj_generate_id(&oid, 0, cid, 0);

		assert_success(plt_obj_place(oid, &layout, pl_map, false));
		plt_obj_layout_check(layout, COMPONENT_NR, 0);

		pl_obj_layout_free(layout);
	}

	free_pool_and_placement_map(po_map, pl_map);
	verbose_print("\tPlacement: OK\n");
}

void
rebuild_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		 spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		 shard_ids[SPARE_MAX_NUM];
	daos_obj_id_t		 oid;
	uuid_t			 pl_uuid;
	struct daos_obj_md	 *md_arr;
	struct daos_obj_md	 md = { 0 };
	struct pl_obj_layout	**org_layout;
	struct pl_obj_layout	*layout;
	uint32_t		 po_ver;
	int			 test_num;
	int			 num_new_spares;
	int			 fail_tgt;
	int			 spares_left;
	int			 rc, i;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);
	D_ALLOC_ARRAY(org_layout, TEST_PER_OC);
	D_ASSERT(org_layout != NULL);

	gen_pool_and_placement_map(DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_JUMP_MAP,
				   &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);

	/* Create array of object IDs to use later */
	for (i = 0; i < TEST_PER_OC; ++i) {
		oid.lo = rand();
		daos_obj_generate_id(&oid, 0, cid, 0);
		dc_obj_fetch_md(oid, &md);
		md.omd_ver = po_ver;
		md_arr[i] = md;
	}

	/* Generate layouts for later comparison */
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
				  &org_layout[test_num]);
		assert_success(rc);
		plt_obj_layout_check(org_layout[test_num], COMPONENT_NR, 0);
	}

	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {
		/* Fail target and update the pool map */
		plt_fail_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		/*
		 * For each failed target regenerate all layouts and
		 * fetch rebuild targets, then verify basic conditions met
		 */
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &layout);
			assert_success(rc);

			num_new_spares = pl_obj_find_rebuild(pl_map,
						&md_arr[test_num], NULL, po_ver,
						spare_tgt_ranks, shard_ids,
						SPARE_MAX_NUM);

			spares_left = NUM_TARGETS - layout->ol_nr + fail_tgt;
			plt_obj_rebuild_layout_check(layout,
					org_layout[test_num], COMPONENT_NR,
					&fail_tgt, 1, spares_left,
					num_new_spares, spare_tgt_ranks,
					shard_ids);

			pl_obj_layout_free(layout);
		}

		/* Move target to Down state */
		plt_fail_tgt_out(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		/* Verify post rebuild layout */
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &layout);
			assert_success(rc);
			D_ASSERT(layout->ol_nr == org_layout[test_num]->ol_nr);

			plt_obj_layout_check(layout, COMPONENT_NR,
					     layout->ol_nr);
			pl_obj_layout_free(layout);
		}

	}

	/* Cleanup Memory */
	for (i = 0; i < TEST_PER_OC; ++i)
		D_FREE(org_layout[i]);
	D_FREE(org_layout);
	free_pool_and_placement_map(po_map, pl_map);
	verbose_print("\tRebuild: OK\n");
}

void
reint_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		shard_ids[SPARE_MAX_NUM];
	daos_obj_id_t		oid;
	uuid_t			pl_uuid;
	struct daos_obj_md	*md_arr;
	struct daos_obj_md	md = { 0 };
	struct pl_obj_layout	***layout;
	struct pl_obj_layout	*temp_layout;
	uint32_t		po_ver;
	int			test_num;
	int			num_reint;
	int			fail_tgt;
	int			spares_left;
	int			rc, i;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);
	D_ALLOC_ARRAY(layout, NUM_TARGETS + 1);
	D_ASSERT(layout != NULL);

	for (i = 0; i < NUM_TARGETS + 1; ++i) {
		D_ALLOC_ARRAY(layout[i], TEST_PER_OC);
		D_ASSERT(layout[i] != NULL);
	}

	gen_pool_and_placement_map(DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_JUMP_MAP,
				   &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);

	for (i = 0; i < TEST_PER_OC; ++i) {
		oid.lo = rand();
		daos_obj_generate_id(&oid, 0, cid, 0);
		dc_obj_fetch_md(oid, &md);
		md.omd_ver = po_ver;
		md_arr[i] = md;
	}

	/* Generate original layouts for later comparison*/
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
				  &layout[0][test_num]);
		assert_success(rc);
		plt_obj_layout_check(layout[0][test_num], COMPONENT_NR, 0);
	}

	/* fail all the targets */
	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {

		plt_fail_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);

		plt_fail_tgt_out(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);

		/* Generate layouts for all N target failures*/
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &layout[fail_tgt + 1][test_num]);
			assert_success(rc);
			plt_obj_layout_check(layout[fail_tgt + 1][test_num],
					     COMPONENT_NR, NUM_TARGETS);
		}
	}

	/* Reintegrate targets one-by-one and compare layouts */
	spares_left = NUM_TARGETS - (layout[0][0]->ol_nr + fail_tgt);
	for (fail_tgt = NUM_TARGETS-1; fail_tgt >= 0; --fail_tgt) {
		plt_reint_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;
			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &temp_layout);
			assert_success(rc);

			num_reint = pl_obj_find_reint(pl_map, &md_arr[test_num],
						NULL, po_ver,  spare_tgt_ranks,
						shard_ids, SPARE_MAX_NUM);

			plt_obj_reint_layout_check(temp_layout,
					layout[fail_tgt][test_num],
					COMPONENT_NR, &fail_tgt, 1, spares_left,
					num_reint, spare_tgt_ranks, shard_ids);

			pl_obj_layout_free(temp_layout);
		}

		/* Set the target to up */
		plt_reint_tgt_up(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);

		/*
		 * Verify that the post-reintegration layout matches the
		 * pre-failure layout
		 */
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &temp_layout);
			assert_success(rc);

			plt_obj_layout_check(temp_layout, COMPONENT_NR,
					     NUM_TARGETS);

			D_ASSERT(plt_obj_layout_match(temp_layout,
						layout[fail_tgt][test_num]));

			pl_obj_layout_free(temp_layout);
		}

	}
	/* Cleanup Memory */
	for (i = 0; i <= NUM_TARGETS; ++i) {
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num)
			D_FREE(layout[i][test_num]);
		D_FREE(layout[i]);
	}
	D_FREE(layout);
	free_pool_and_placement_map(po_map, pl_map);
	verbose_print("\tReintegration: OK\n");
}

void
drain_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		shard_ids[SPARE_MAX_NUM];
	uint32_t		po_ver;
	daos_obj_id_t		oid;
	uuid_t			pl_uuid;
	struct daos_obj_md	*md_arr;
	struct daos_obj_md	md = { 0 };
	struct pl_obj_layout	*layout;
	struct pl_obj_layout	**org_layout;
	int			test_num;
	int			num_new_spares;
	int			fail_tgt;
	int			rc, i;
	int			spares_left;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);
	D_ALLOC_ARRAY(org_layout, TEST_PER_OC);
	D_ASSERT(org_layout != NULL);

	gen_pool_and_placement_map(DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_JUMP_MAP,
				   &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);

	for (i = 0; i < TEST_PER_OC; ++i) {
		oid.lo = rand();
		daos_obj_generate_id(&oid, 0, cid, 0);
		dc_obj_fetch_md(oid, &md);
		md.omd_ver = po_ver;
		md_arr[i] = md;
	}

	/* Generate layouts for later comparison*/
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		md_arr[test_num].omd_ver = po_ver;

		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
				  &org_layout[test_num]);
		assert_success(rc);
		plt_obj_layout_check(org_layout[test_num], COMPONENT_NR, 0);
	}

	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {
		/* Drain target and update the pool map */
		plt_drain_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		spares_left = NUM_TARGETS - (org_layout[0]->ol_nr + fail_tgt);
		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;
			rc = pl_obj_place(pl_map, &md_arr[test_num],
					  NULL, &layout);
			assert_success(rc);

			num_new_spares = pl_obj_find_rebuild(pl_map,
						&md_arr[test_num], NULL, po_ver,
						spare_tgt_ranks, shard_ids,
						SPARE_MAX_NUM);

			plt_obj_layout_check(layout, COMPONENT_NR,
					     layout->ol_nr);

			plt_obj_drain_layout_check(layout,
					org_layout[test_num], COMPONENT_NR,
					&fail_tgt, 1, spares_left,
					num_new_spares, spare_tgt_ranks,
					shard_ids);

			pl_obj_layout_free(layout);
		}

		/* Move target to Down-Out state */
		plt_fail_tgt_out(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;
			pl_obj_layout_free(org_layout[test_num]);

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					  &org_layout[test_num]);
			assert_success(rc);

			plt_obj_layout_check(org_layout[test_num], COMPONENT_NR,
					     org_layout[test_num]->ol_nr);

		}

	}

	/* Cleanup Memory */
	for (i = 0; i < TEST_PER_OC; ++i)
		D_FREE(org_layout[i]);
	D_FREE(org_layout);
	free_pool_and_placement_map(po_map, pl_map);
	verbose_print("\tRebuild with Drain: OK\n");
}

#define NUM_TO_EXTEND 2

void
add_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		shard_ids[SPARE_MAX_NUM];
	uint32_t		po_ver;
	d_rank_list_t		rank_list;
	daos_obj_id_t		oid;
	uuid_t			pl_uuid;
	struct daos_obj_md	*md_arr;
	struct daos_obj_md	md = { 0 };
	struct pl_obj_layout	*layout;
	struct pl_obj_layout	**org_layout;
	int			test_num;
	int			num_new_spares;
	int			rc, i;
	uuid_t target_uuids[NUM_TO_EXTEND] = {"e0ab4def", "60fcd487"};
	int32_t domains[NUM_TO_EXTEND] = {1, 1};

	if (is_max_class_obj(cid))
		return;

	rank_list.rl_nr = NUM_TO_EXTEND;
	rank_list.rl_ranks = malloc(sizeof(d_rank_t) * NUM_TO_EXTEND);
	rank_list.rl_ranks[0] = DOM_NR + 1;
	rank_list.rl_ranks[1] = DOM_NR + 2;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);
	D_ALLOC_ARRAY(org_layout, TEST_PER_OC);
	D_ASSERT(org_layout != NULL);

	gen_pool_and_placement_map(DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_JUMP_MAP,
				   &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);

	for (i = 0; i < TEST_PER_OC; ++i) {
		oid.lo = rand();
		daos_obj_generate_id(&oid, 0, cid, 0);
		dc_obj_fetch_md(oid, &md);
		md.omd_ver = po_ver;
		md_arr[i] = md;
	}

	/* Generate layouts for later comparison*/
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		md_arr[test_num].omd_ver = po_ver;

		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
				  &org_layout[test_num]);
		D_ASSERTF(rc == 0, "rc == %d\n", rc);
		plt_obj_layout_check(org_layout[test_num], COMPONENT_NR, 0);
	}
	extend_test_pool_map(po_map, NUM_TO_EXTEND, target_uuids, &rank_list,
			     NUM_TO_EXTEND, domains, NULL, NULL,
			     VOS_PER_TARGET);


	/* test normal placement for pools currently being extended */
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL, &layout);
		assert_success(rc);

		num_new_spares = pl_obj_find_addition(pl_map, &md_arr[test_num],
						      NULL, po_ver,
						      spare_tgt_ranks,
						      shard_ids,
						      SPARE_MAX_NUM);
		D_ASSERT(num_new_spares >= 0);

		plt_obj_add_layout_check(layout, org_layout[test_num],
					 COMPONENT_NR, num_new_spares,
					 spare_tgt_ranks, shard_ids);

		pl_obj_layout_free(layout);
	}

	/* Cleanup Memory */
	for (i = 0; i < TEST_PER_OC; ++i)
		D_FREE(org_layout[i]);
	D_FREE(org_layout);
	free_pool_and_placement_map(po_map, pl_map);
	verbose_print("\tAddition: OK\n");
}


static void
test_all_object_classes(void (*object_class_test)(daos_oclass_id_t))
{
	daos_oclass_id_t	*test_classes;
	uint32_t		 num_test_oc;
	char			 oclass_name[50];
	int			 oc_index;

	num_test_oc = get_object_classes(&test_classes);

	for (oc_index = 0; oc_index < num_test_oc; ++oc_index) {

		daos_oclass_id2name(test_classes[oc_index],  oclass_name);
		verbose_print("Running oclass test: %s\n", oclass_name);
		object_class_test(test_classes[oc_index]);
	}

	D_FREE(test_classes);
}

static void
placement_all_object_classes(void **state)
{
	test_all_object_classes(placement_object_class);
}

static void
rebuild_all_object_classes(void **state)
{
	test_all_object_classes(rebuild_object_class);
}

static void
reint_all_object_classes(void **state)
{
	test_all_object_classes(reint_object_class);
}

static void
drain_all_object_classes(void **state)
{
	test_all_object_classes(drain_object_class);
}

static void
add_all_object_classes(void **state)
{
	test_all_object_classes(add_object_class);
}

static void
gen_maps(int num_domains, int nodes_per_domain, int vos_per_target,
	 struct pool_map **po_map, struct pl_map **pl_map)
{
	*po_map = NULL;
	*pl_map = NULL;
	gen_pool_and_placement_map(num_domains, nodes_per_domain,
				   vos_per_target, PL_TYPE_JUMP_MAP,
				   po_map, pl_map);
	assert_non_null(*po_map);
	assert_non_null(*pl_map);
}

static void
gen_oid(daos_obj_id_t *oid, uint64_t lo, uint64_t hi, daos_oclass_id_t cid)
{
	oid->lo = lo;
	/* make sure top 32 bits are unset (DAOS only) */
	oid->hi = hi & 0xFFFFFFFF;
	daos_obj_generate_id(oid, 0, cid, 0);
}

#define assert_placement_success(pl_map, cid) \
	do {\
		daos_obj_id_t __oid; \
		struct pl_obj_layout *__layout = NULL; \
		gen_oid(&__oid, 1, UINT64_MAX, cid); \
		assert_success(plt_obj_place(__oid, &__layout, pl_map, \
				false)); \
		pl_obj_layout_free(__layout); \
	} while (0)
#define assert_invalid_param(pl_map, cid) \
	do {\
		daos_obj_id_t __oid; \
		struct pl_obj_layout *__layout = NULL; \
		gen_oid(&__oid, 1, UINT64_MAX, cid); \
		assert_err(plt_obj_place(__oid, &__layout, pl_map, \
				false), -DER_INVAL); \
	} while (0)

static void
object_class_is_verified(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
#define DAOS_6295 0
	/*
	 * ---------------------------------------------------------
	 * with a single target
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 1, 1, &po_map, &pl_map);

	assert_invalid_param(pl_map, OC_UNKNOWN);
	assert_placement_success(pl_map, OC_S1);
	assert_placement_success(pl_map, OC_SX);

	/* Replication should fail because there's only 1 target */
	assert_invalid_param(pl_map, OC_RP_2G1);
	assert_invalid_param(pl_map, OC_RP_3G1);
	assert_invalid_param(pl_map, OC_RP_4G1);
	assert_invalid_param(pl_map, OC_RP_8G1);

#if DAOS_6295
	/* Multiple groups should fail because there's only 1 target */
	print_message("Skipping rest until DAOS-XXXX is resolved");
	assert_invalid_param(pl_map, OC_S2);
	assert_invalid_param(pl_map, OC_S4);
	assert_invalid_param(pl_map, OC_S512);
#endif
	free_pool_and_placement_map(po_map, pl_map);


	/*
	 * ---------------------------------------------------------
	 * with 2 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(1, 1, 2, &po_map, &pl_map);

	assert_placement_success(pl_map, OC_S1);
	assert_placement_success(pl_map, OC_S2);
	assert_placement_success(pl_map, OC_SX);

	/*
	 * Even though there are 2 targets, these will still fail because
	 * placement requires a domain for each redundancy.
	 */
	assert_invalid_param(pl_map, OC_RP_2G1);
	assert_invalid_param(pl_map, OC_RP_2G2);
	assert_invalid_param(pl_map, OC_RP_3G1);
	assert_invalid_param(pl_map, OC_RP_4G1);
	assert_invalid_param(pl_map, OC_RP_8G1);
#if DAOS_6295
	/* The following require more targets than available. */
	assert_invalid_param(pl_map, OC_S4);
	assert_invalid_param(pl_map, OC_S512);
#endif
	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 1 target each
	 * ---------------------------------------------------------
	 */
	gen_maps(2, 1, 1, &po_map, &pl_map);

	assert_placement_success(pl_map, OC_S1);
	assert_placement_success(pl_map, OC_RP_2G1);
	assert_placement_success(pl_map, OC_RP_2GX);
#if DAOS_6295
	assert_invalid_param(pl_map, OC_RP_2G2);
	assert_invalid_param(pl_map, OC_RP_2G4);
#endif

	assert_invalid_param(pl_map, OC_RP_2G512);
	assert_invalid_param(pl_map, OC_RP_3G1);

	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 2 targets each = 4 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(2, 1, 2, &po_map, &pl_map);
	assert_placement_success(pl_map, OC_RP_2G2);
#if DAOS_6295
	assert_invalid_param(pl_map, OC_RP_2G4);
#endif

	free_pool_and_placement_map(po_map, pl_map);

	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 4 targets each = 8 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(2, 1, 4, &po_map, &pl_map);
#if DAOS_6295
	assert_placement_success(pl_map, OC_RP_2G4);
#endif
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2);

	free_pool_and_placement_map(po_map, pl_map);
	/*
	 * ---------------------------------------------------------
	 * With 2 domains, 2 nodes each, 2 targets each = 8 targets
	 * ---------------------------------------------------------
	 */
	gen_maps(2, 2, 2, &po_map, &pl_map);
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2);

	free_pool_and_placement_map(po_map, pl_map);

	/* The End */
	skip_msg("DAOS-6295: a bunch commented out in this test. ");
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
};

static void rr_init(struct remap_result *rr, uint32_t nr)
{
	D_ALLOC_ARRAY(rr->ids, nr);
	D_ALLOC_ARRAY(rr->tgt_ranks, nr);
	rr->nr = nr;
	rr->out_nr = 0;
}

static void rr_fini(struct remap_result *rr)
{
	D_FREE(rr->ids);
	D_FREE(rr->tgt_ranks);
	memset(rr, 0, sizeof(*rr));
}

/* Testing context */
struct jm_test_ctx {
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	struct pl_obj_layout	*layout;

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
	if (ctx->are_maps_generated)
		free_pool_and_placement_map(ctx->po_map, ctx->pl_map);
}

static void
__jtc_inc_ver(struct jm_test_ctx *ctx)
{
	ctx->ver++;
}

static void
__jtc_layout_free(struct jm_test_ctx *ctx)
{
	if (ctx->is_layout_set)
		pl_obj_layout_free(ctx->layout);
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

	gen_pool_and_placement_map(ctx->domain_nr, ctx->node_nr,
				   ctx->target_nr, PL_TYPE_JUMP_MAP,
				   &ctx->po_map, &ctx->pl_map);

	assert_non_null(ctx->po_map);
	assert_non_null(ctx->pl_map);
	ctx->are_maps_generated = true;
}

static void
jtc_print_remap(struct remap_result *map)
{
	int i;

	for (i = 0; i < map->out_nr; i++) {
		print_message("shard %d -> target %d\n",
			      map->ids[i], map->tgt_ranks[i]);
	}
	if (i > 0)
		print_message("\n");
	else
		print_message("(Nothing)\n\n");
}

static void
jtc_scan(struct jm_test_ctx *ctx)
{
	struct daos_obj_md md = {.omd_id = ctx->oid, .omd_ver = ctx->ver};

	ctx->reint.out_nr =
		pl_obj_find_reint(ctx->pl_map, &md, NULL, ctx->ver,
				  ctx->reint.tgt_ranks,
				  ctx->reint.ids, ctx->reint.nr);
	ctx->new.out_nr =
		pl_obj_find_addition(ctx->pl_map, &md, NULL, ctx->ver,
				    ctx->new.tgt_ranks,
				    ctx->new.ids, ctx->new.nr);
	ctx->rebuild.out_nr =
		pl_obj_find_rebuild(ctx->pl_map, &md, NULL, ctx->ver,
				    ctx->rebuild.tgt_ranks,
				    ctx->rebuild.ids, ctx->rebuild.nr);

	if (ctx->enable_print_layout) {
		print_message("Rebuild Scan\n");
		jtc_print_remap(&ctx->rebuild);
		print_message("Reint Scan\n");
		jtc_print_remap(&ctx->reint);
		print_message("New Scan\n");
		jtc_print_remap(&ctx->new);
	}
}

static int
jtc_create_layout(struct jm_test_ctx *ctx)
{
	int rc;

	/* place object will allocate the layout so need to free first
	 * if already allocated
	 */
	__jtc_layout_free(ctx);
	rc = plt_obj_place(ctx->oid, &ctx->layout, ctx->pl_map,
			   ctx->enable_print_layout);

	if (rc == 0)
		ctx->is_layout_set = true;
	return rc;
}

static void
jtc_set_status_on_domain(struct jm_test_ctx *ctx, enum pool_comp_state status,
			 int id)
{
	plt_set_domain_status(id, status, &ctx->ver, ctx->po_map,
			      ctx->enable_print_debug_msgs, PO_COMP_TP_RACK);
	jtc_print_pool(ctx);
}

static int
__jtc_layout_tgt(struct jm_test_ctx *ctx, uint32_t shard_idx)
{
	return  ctx->layout->ol_shards[shard_idx].po_target;
}

static void
jtc_set_status_on_target(struct jm_test_ctx *ctx, const int status,
			 const uint32_t id)
{
	uuid_t uuid;

	__jtc_inc_ver(ctx);
	plt_set_tgt_status(id, status, ctx->ver, ctx->po_map,
			   ctx->enable_print_debug_msgs);

	pl_map_update(uuid, ctx->po_map, false, PL_TYPE_JUMP_MAP);
	ctx->pl_map = pl_map_find(uuid, ctx->oid);
	jtc_print_pool(ctx);
}

static void
jtc_set_status_on_shard_target(struct jm_test_ctx *ctx, const int status,
			       const uint32_t shard_idx)
{

	int id = __jtc_layout_tgt(ctx, shard_idx);

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
	jtc_set_status_on_target(ctx, status, __jtc_layout_tgt(ctx, 0));
}

static void
jtc_set_status_on_shard_idx(struct jm_test_ctx *ctx, const int status,
			    const int shard_idx)
{
	jtc_set_status_on_target(ctx, status, __jtc_layout_tgt(ctx, shard_idx));
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
	ctx->enable_print_pool = true;
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
	 uint32_t target_nr[], daos_oclass_id_t object_class, bool enable_debug)
{
	__jtc_init(ctx, object_class, g_verbose);

	ctx->is_standard_config = false;
	ctx->domain_nr = domain_nr;
	ctx->node_nr = 1;
	ctx->domain_target_nr = target_nr;

	gen_pool_and_placement_map_non_standard(domain_nr, (int *)target_nr,
						PL_TYPE_JUMP_MAP,
						&ctx->po_map,
						&ctx->pl_map);

}

static void
jtc_init_with_layout(struct jm_test_ctx *ctx, uint32_t domain_nr,
		     uint32_t node_nr, uint32_t target_nr,
		     daos_oclass_id_t object_class, bool enable_debug)
{
	jtc_init(ctx, domain_nr, node_nr, target_nr, object_class,
		 enable_debug);
	jtc_create_layout(ctx);
}

static void
jtc_fini(struct jm_test_ctx *ctx)
{
	__jtc_layout_free(ctx);
	__jtc_maps_free(ctx);

	rr_fini(&ctx->rebuild);
	rr_fini(&ctx->reint);
	rr_fini(&ctx->new);
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
jtc_shard_target_is_rebuilding(struct jm_test_ctx *ctx, int target_id)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_target == target_id && shard->po_rebuilding)
			return true;
	}

	return false;
}

static bool
jtc_has_shard_with_rebuilding_set(struct jm_test_ctx *ctx, int shard_id)
{
	struct pl_obj_shard	*shard;
	int			 i;

	jtc_for_each_layout_shard(ctx, shard, i) {
		if (shard->po_shard == shard_id && shard->po_rebuilding)
			return true;
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
jtc_set_oid_with_shard_in_target(struct jm_test_ctx *ctx, int target_id)
{
	int i;

	for (i = 0; i < 50; i++) {
		jtc_set_object_meta(ctx, OC_RP_3G1, i + 1, UINT_MAX);
		assert_success(jtc_create_layout(ctx));
		if (jtc_layout_has_target(ctx, target_id))
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

#define jtc_assert_scan_and_layout(ctx) do {\
	assert_success(jtc_create_layout(ctx)); \
	jtc_scan(ctx); \
} while (0)

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
	char			 oclass_name[64];
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

	/*
	 * smallest possible, but have to have enough domains for all object
	 * classes. Minimum is 18 for EC_16P2G1 for now
	 */
	jtc_init(&ctx, 18, 1, 1, 0, g_verbose);
	num_test_oc = get_object_classes(&object_classes);
	for (i = 0; i < num_test_oc; ++i) {
		jtc_set_object_meta(&ctx, object_classes[i], 0, 1);

		daos_oclass_id2name(object_classes[i], oclass_name);
		JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	}
	jtc_fini(&ctx);

	/* A bit larger */
	jtc_init(&ctx, 128, 1, 8, 0, g_verbose);
	for (i = 0; i < num_test_oc; ++i) {
		jtc_set_object_meta(&ctx, object_classes[i], 0, 1);
		JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	}
	jtc_fini(&ctx);

	/* many more domains and targets */
	jtc_init(&ctx, 1024, 1, 128, 0, g_verbose);
	for (i = 0; i < num_test_oc; ++i) {
		jtc_set_object_meta(&ctx, object_classes[i], 0, 1);
		JTC_CREATE_AND_ASSERT_HEALTHY_LAYOUT(&ctx);
	}
	jtc_fini(&ctx);
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
	prev_first_shard = *jtc_get_layout_shard(&ctx, 0);

	/* loop through rest of targets, marking each as down. By the end the
	 * pool map includes only 4 targets that are still UPIN
	 */
	for (i = 0; i < 16 - 4; i++) {
		jtc_set_status_on_first_shard(&ctx, PO_COMP_ST_DOWN);
		jtc_assert_scan_and_layout(&ctx);
		/* single rebuild target in layout */
		assert_int_equal(1, jtc_get_layout_rebuild_count(&ctx));

		/* for shard 0 (first shard) layout has 1 that is in rebuild
		 * state, but none in good state
		 */
		assert_true(jtc_has_shard_with_rebuilding_set(&ctx, 0));
		assert_false(jtc_has_shard_with_rebuilding_not_set(&ctx, 0));
		/* scan returns 1 target to rebuild, shard id should be 0,
		 * target should not be the "DOWN"ed target, and rebuild target
		 * should be same as target in layout
		 */
		assert_int_equal(1, ctx.rebuild.out_nr);
		assert_int_equal(0, ctx.rebuild.ids[0]);
		assert_int_not_equal(prev_first_shard.po_target,
				     ctx.rebuild.tgt_ranks[0]);
		assert_int_equal(jtc_get_layout_shard(&ctx, 0)->po_target,
				 ctx.rebuild.tgt_ranks[0]);
		/* should be no reintegration or addition happening */
		assert_int_equal(0, ctx.reint.out_nr);
		assert_int_equal(0, ctx.new.out_nr);

		prev_first_shard = *jtc_get_layout_shard(&ctx, 0);
	}

	jtc_set_status_on_first_shard(&ctx, PO_COMP_ST_DOWN);
	jtc_assert_scan_and_layout(&ctx);

	/* no where to rebuild to now */
	assert_int_equal(0, jtc_get_layout_rebuild_count(&ctx));
	assert_int_equal(0, ctx.rebuild.out_nr);

	jtc_fini(&ctx);
}

static void
one_is_being_reintegrated(void **state)
{
	struct jm_test_ctx	ctx;

	/* create a layout with 4 targets (2 replica, 2 shards) */
	jtc_init_with_layout(&ctx, 6, 1, 2, OC_RP_2G2, g_verbose);

	/* simulate that the original target went down, but is now being
	 * reintegrated
	 */
	jtc_set_status_on_shard_idx(&ctx, PO_COMP_ST_UP, 3);
	jtc_create_layout(&ctx);
	skip_msg("DAOS-6303");
	jtc_scan(&ctx);

	/* Should have 1 target in rebuild and 1 returned from find_reint */
	assert_int_equal(1, jtc_get_layout_rebuild_count(&ctx));
	assert_int_equal(1, ctx.reint.out_nr);

	/* should have nothing in rebuild or addition */
	assert_int_equal(0, ctx.new.out_nr);
	skip_msg("DAOS-6349");
	assert_int_equal(0, ctx.rebuild.out_nr);

	/* Should have 5 items in the layout, 4 with no rebuild set, but
	 * the third shard (had UP set on it) should also have an item with
	 * rebuild set.
	 * Will actually have 6 items because the groups need to have the same
	 * size, but one of the groups will have an invalid shard/target.
	 */
	assert_int_equal(5, jtc_get_layout_nr(&ctx) -
			    jtc_get_layout_bad_count(&ctx));
	assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, 0));
	assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, 1));
	assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, 2));
	assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, 3));
	assert_true(jtc_has_shard_with_rebuilding_set(&ctx, 3));

	jtc_fini(&ctx);
}

static void
all_are_being_reintegrated(void **state)
{
	struct jm_test_ctx	ctx;
	int			i;

	/* create a layout with 6 targets (3 replica, 2 shards) */
	jtc_init_with_layout(&ctx, 6, 1, 2, OC_RP_3G2, g_verbose);

	/* simulate that the original targets went down, but are now being
	 * reintegrated
	 */
	jtc_set_status_on_all_shards(&ctx, PO_COMP_ST_UP);
	jtc_create_layout(&ctx);
	skip_msg("DAOS-6303");

	jtc_scan(&ctx);

	/* Should be all 6 targets */
	assert_int_equal(6, jtc_get_layout_rebuild_count(&ctx));
	assert_int_equal(6, ctx.reint.out_nr);

	/* should have nothing in rebuild or addition */

	skip_msg("DAOS-6349");
	assert_int_equal(0, ctx.rebuild.out_nr);
	assert_int_equal(0, ctx.new.out_nr);

	/* each shard idx should have a rebuild target and a non
	 * rebuild target
	 */
	for (i = 0; i < 6; i++) {
		assert_true(jtc_has_shard_with_rebuilding_set(&ctx, i));
		assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
	}

	jtc_fini(&ctx);
}

static void
one_server_is_added(void **state)
{
	struct jm_test_ctx	ctx;
	int			new_targets[] = {12, 13, 14, 15};

	jtc_init(&ctx, 4, 1, 4, OC_UNKNOWN, g_verbose);
	/* set oid so that it would place a shard in one of the last targets */
	assert_true(jtc_set_oid_with_shard_in_targets(&ctx, new_targets,
						      ARRAY_SIZE(new_targets),
						      OC_RP_3G1));
	jtc_set_status_on_domain(&ctx, PO_COMP_ST_NEW, 3);

	jtc_assert_scan_and_layout(&ctx);

	/* might have more than one because of other potential data movement,
	 * but should have at least 1
	 */
	assert_true(ctx.new.out_nr > 0);
	assert_int_equal(0, ctx.rebuild.out_nr);
	assert_int_equal(0, ctx.reint.out_nr);
	skip_msg("DAOS-6303");
	assert_int_equal(ctx.new.out_nr, jtc_get_layout_rebuild_count(&ctx));

	jtc_fini(&ctx);
}

/*
 * Will create a system configuration with 3 domains, 4 targets each, then
 * 1 target is added to the last domain. Generate an object id that would be
 * placed on the new target.
 */
static void
one_target_is_added_to_end_node(void **state)
{
	struct jm_test_ctx	ctx;
	uint32_t		domain_targets[] = {4, 4, 5};

	jtc_init_non_standard(&ctx, ARRAY_SIZE(domain_targets), domain_targets,
			      OC_UNKNOWN, g_verbose);

	/* set oid so that it would place on target 12 (12 is the
	 * target that is "added")
	 */
	if (!jtc_set_oid_with_shard_in_target(&ctx, 12))
		fail_msg("Unable to run test because wasn't able to find an "
			 "object id that would have a shard placed on "
			 "target 8");
	jtc_set_status_on_target(&ctx, PO_COMP_ST_NEW, 12);

	jtc_assert_scan_and_layout(&ctx);

	assert_true(jtc_layout_has_target(&ctx, 12));

	assert_true(ctx.new.out_nr > 0);
	assert_int_equal(0, ctx.rebuild.out_nr);
	assert_int_equal(0, ctx.reint.out_nr);
	skip_msg("DAOS-6303");
	assert_true(jtc_shard_target_is_rebuilding(&ctx, 12));
	assert_int_equal(ctx.new.out_nr, jtc_get_layout_rebuild_count(&ctx));

	jtc_fini(&ctx);
}

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
		jtc_set_status_on_first_shard(&ctx, PO_COMP_ST_DOWNOUT);
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
		jtc_set_status_on_all_shards(&ctx, PO_COMP_ST_DOWNOUT);
		jtc_assert_scan_and_layout(&ctx);

		assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
		assert_int_equal(0, ctx.rebuild.out_nr);
		assert_int_equal(0, ctx.reint.out_nr);
		assert_int_equal(0, ctx.new.out_nr);
	}

	jtc_fini(&ctx);
}

/*
 * Drain all shards. There are plenty of extra domains to drain to.
 * Number of targets should double, 1 DRAIN target
 * (not "rebuilding") and the target being drained to (is "rebuilding")
 */
static void
drain_all_with_extra_domains(void **state)
{
	struct jm_test_ctx	 ctx;
	int			 i;
	const int		 shards_nr = 4; /* 2 x 2 */

	jtc_init_with_layout(&ctx, 4, 1, 2, OC_RP_2G2, false);


	/* drain all targets */
	jtc_set_status_on_all_shards(&ctx, PO_COMP_ST_DRAIN);
	jtc_assert_scan_and_layout(&ctx);

	/* there should be 2 targets for each shard, one
	 * rebuilding and one not
	 */
	assert_int_equal(8, jtc_get_layout_target_count(&ctx));
	skip_msg("DAOS-6300 - too many are marked as rebuild");
	assert_int_equal(4, jtc_get_layout_rebuild_count(&ctx));
	for (i = 0; i < shards_nr; i++) {
		assert_true(jtc_has_shard_with_rebuilding_set(&ctx, i));
		assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
	}

	jtc_fini(&ctx);
}

/*
 * Drain all shards. There are extra targets, but not domains, to drain to.
 */
static void
drain_all_with_enough_targets(void **state)
{
	struct jm_test_ctx	 ctx;
	int			 i;
	const int		 shards_nr = 2; /* 2 x 1 */

	jtc_init_with_layout(&ctx, 2, 1, 4, OC_RP_2G1, g_verbose);

	/* drain all targets */
	jtc_set_status_on_all_shards(&ctx, PO_COMP_ST_DRAIN);
	jtc_assert_scan_and_layout(&ctx);

	/* there should be 2 targets for each shard, one
	 * rebuilding and one not
	 */
	for (i = 0; i < shards_nr; i++) {
		skip_msg("DAOS-6300 - Not drained to other target?");
		assert_int_equal(0, jtc_get_layout_bad_count(&ctx));
		assert_true(jtc_has_shard_with_rebuilding_set(&ctx, i));
		assert_true(jtc_has_shard_with_rebuilding_not_set(&ctx, i));
	}

	jtc_fini(&ctx);
}

static void
placement_handles_multiple_states(void **state)
{
	struct jm_test_ctx	 ctx;

	jtc_init_with_layout(&ctx, 3, 1, 8, OC_RP_2G1, g_verbose);
	jtc_set_status_on_shard_target(&ctx, PO_COMP_ST_DOWN, 0);
	jtc_set_status_on_shard_target(&ctx, PO_COMP_ST_UP, 1);
	jtc_set_status_on_domain(&ctx, PO_COMP_ST_NEW, 2);

	skip_msg("DAOS-6301: Hits D_ASSERT(original->ol_nr == new->ol_nr)");
	assert_success(jtc_create_layout(&ctx));

	assert_false(jtc_layout_has_duplicate(&ctx));

	jtc_scan(&ctx);
	uint32_t rebuilding = jtc_get_layout_rebuild_count(&ctx);

	/* 1 each for down, up, new ... maybe? */
	assert_int_equal(3, rebuilding);

	assert_int_equal(ctx.rebuild.out_nr, 1);
	assert_int_equal(ctx.reint.out_nr, 1);
	assert_int_equal(ctx.new.out_nr, 1);

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
		if (jtc_layout_has_duplicate(&ctx)) {
			jtc_print_layout_force(&ctx);
			fail_msg("%s:%d Found duplicate for i=%d\n",
				 file, line, i);
		}
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

	skip_msg("DAOS-6348: The following two tests have duplicate targets "
		 "picked");
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

/*
 * ------------------------------------------------
 * End Test Cases
 * ------------------------------------------------
 */

static int
placement_test_setup(void **state)
{
	return pl_init();
}

static int
placement_test_teardown(void **state)
{
	pl_fini();

	return 0;
}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define T(dsc, test) { "PLACEMENT "STR(__COUNTER__)": " dsc, test, \
			  placement_test_setup, placement_test_teardown }

static const struct CMUnitTest tests[] = {
	/* Standard configurations */
	T("Object class is verified appropriately", object_class_is_verified),
	T("Test placement on all object classes", placement_all_object_classes),
	T("Test rebuild on all object classes", rebuild_all_object_classes),
	T("Test reintegrate on all object classes", reint_all_object_classes),
	T("Test drain on all object classes", drain_all_object_classes),
	T("Test add on all object classes", add_all_object_classes),
	T("With all healthy targets, can create layout, nothing is in "
	  "rebuild, and no duplicates.", all_healthy),
	/* DOWN */
	T("Target for first shard continually goes to DOWN state and "
	  "never finishes rebuild. Should still get new target until no more",
	  down_continuously),
	/* UP */
	T("One target is being reintegrated", one_is_being_reintegrated),
	T("With all targets being reintegrated", all_are_being_reintegrated),
	/* NEW */
	T("A server is added and an object id is chosen that requires "
	  "data movement to the new server",
	  one_server_is_added),
	T("One target added to last server", one_target_is_added_to_end_node),
	/* DOWNOUT */
	T("Rebuild first shard's target repeatedly",
	  chained_rebuild_completes_first_shard),
	T("Rebuild all shards' targets", chained_rebuild_completes_all_at_once),
	/* DRAIN */
	T("Drain all shards with extra domains", drain_all_with_extra_domains),
	T("Drain all shards with extra targets",
	      drain_all_with_enough_targets),
	T("Placement can handle multiple states",
	  placement_handles_multiple_states),
	T("Non-standard system configurations. All healthy",
	  unbalanced_config),
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
