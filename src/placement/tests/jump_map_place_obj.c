/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include "daos_api.h"
#include <daos.h>
#include "place_obj_common.h"

#define DOM_NR		18
#define	NODE_PER_DOM	1
#define VOS_PER_TARGET	4
#define SPARE_MAX_NUM	(DOM_NR * 3)
#define COMPONENT_NR	(DOM_NR + DOM_NR * NODE_PER_DOM + \
			 DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)
#define NUM_TARGETS	(DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)

#define TEST_PER_OC 1000

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

		plt_obj_place(oid, &layout, pl_map, false);
		plt_obj_layout_check(layout, COMPONENT_NR, 0);

		pl_obj_layout_free(layout);
	}

	free_pool_and_placement_map(po_map, pl_map);
	D_PRINT("\tPlacement: OK\n");
}

void
rebuild_object_class(daos_oclass_id_t cid)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		shard_ids[SPARE_MAX_NUM];
	daos_obj_id_t		oid;
	uuid_t			pl_uuid;
	struct daos_obj_md	*md_arr;
	struct daos_obj_md	md = { 0 };
	struct pl_obj_layout	**org_layout;
	struct pl_obj_layout    *layout;
	uint32_t		po_ver;
	int			test_num;
	int			num_new_spares;
	int			fail_tgt;
	int			spares_left;
	int			rc, i;

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

	/* Generate layouts for later comparison*/
	for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
		rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					&org_layout[test_num]);
		D_ASSERT(rc == 0);
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
			D_ASSERT(rc == 0);

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
			D_ASSERT(rc == 0);
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
	D_PRINT("\tRebuild: OK\n");
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
		D_ASSERT(rc == 0);
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
			D_ASSERT(rc == 0);
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
			D_ASSERT(rc == 0);

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
			D_ASSERT(rc == 0);

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
	D_PRINT("\tReintegration: OK\n");
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
		D_ASSERT(rc == 0);
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
			D_ASSERT(rc == 0);

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
			D_ASSERT(rc == 0);

			plt_obj_layout_check(org_layout[test_num], COMPONENT_NR,
					org_layout[test_num]->ol_nr);

		}

	}

	/* Cleanup Memory */
	for (i = 0; i < TEST_PER_OC; ++i)
		D_FREE(org_layout[i]);
	D_FREE(org_layout);
	free_pool_and_placement_map(po_map, pl_map);
	D_PRINT("\tRebuild with Drain: OK\n");
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
		D_ASSERT(rc == 0);

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
	D_PRINT("\tAddition: OK\n");
}


int
main(int argc, char **argv)
{
	daos_oclass_id_t	*test_classes;
	uint32_t		 num_test_oc;
	char			 oclass_name[50];
	int			 oc_index;
	int			 rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = pl_init();
	if (rc != 0) {
		daos_debug_fini();
		return rc;
	}

	num_test_oc = getObjectClasses(&test_classes);

	for (oc_index = 0; oc_index < num_test_oc; ++oc_index) {

		daos_oclass_id2name(test_classes[oc_index],  oclass_name);
		D_PRINT("Running oclass test: %s\n", oclass_name);

		placement_object_class(test_classes[oc_index]);
		rebuild_object_class(test_classes[oc_index]);
		drain_object_class(test_classes[oc_index]);
		reint_object_class(test_classes[oc_index]);
		add_object_class(test_classes[oc_index]);

	}

	D_FREE(test_classes);
	D_PRINT("all tests passed!\n");

	daos_debug_fini();
	return 0;
}
