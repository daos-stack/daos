/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
	struct pl_obj_layout	*layout;
	uint32_t		po_ver;
	int			test_num;
	int			num_new_spares;
	int			fail_tgt;
	int			rc, i;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);

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

	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {

		/* Fail target and update the pool map */
		plt_fail_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			num_new_spares = pl_obj_find_rebuild(pl_map,
					&md_arr[test_num], NULL, po_ver,
					spare_tgt_ranks, shard_ids,
					SPARE_MAX_NUM, -1);

			D_ASSERT(num_new_spares >= 0 && num_new_spares < 2);
		}


		plt_fail_tgt_out(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			md_arr[test_num].omd_ver = po_ver;

			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					&layout);
			D_ASSERT(rc == 0);

			plt_obj_layout_check(layout, COMPONENT_NR,
					layout->ol_nr);
			pl_obj_layout_free(layout);
		}

	}

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
	struct pl_obj_layout	**layout;
	struct pl_obj_layout    *temp_layout;
	uint32_t		po_ver;
	int			test_num;
	int			num_reint;
	int			fail_tgt;
	int			rc, i;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.hi = 5;
	po_ver = 1;

	D_ALLOC_ARRAY(md_arr, TEST_PER_OC);
	D_ASSERT(md_arr != NULL);
	D_ALLOC_ARRAY(layout, TEST_PER_OC);
	D_ASSERT(layout != NULL);

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
					&layout[test_num]);
		D_ASSERT(rc == 0);
		plt_obj_layout_check(layout[test_num], COMPONENT_NR, 0);
	}

	/* fail all the targets */
	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {

		plt_fail_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);

		plt_fail_tgt_out(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);

	}

	for (fail_tgt = 0; fail_tgt < NUM_TARGETS; ++fail_tgt) {
		plt_reint_tgt(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
		pl_map = pl_map_find(pl_uuid, oid);

		for (test_num = 0; test_num < TEST_PER_OC; ++test_num) {
			rc = pl_obj_place(pl_map, &md_arr[test_num], NULL,
					&temp_layout);
			D_ASSERT(rc == 0);

			num_reint = pl_obj_find_reint(pl_map, &md_arr[test_num],
					NULL, po_ver,  spare_tgt_ranks,
					shard_ids, SPARE_MAX_NUM, -1);

			reint_check(layout[test_num], temp_layout,
					spare_tgt_ranks, shard_ids, num_reint,
					fail_tgt);
		}

		plt_reint_tgt_up(fail_tgt, &po_ver, po_map,  pl_debug_msg);
		pl_map_update(pl_uuid, po_map, false, PL_TYPE_JUMP_MAP);
	}


	free_pool_and_placement_map(po_map, pl_map);
	D_PRINT("\tReintegration: OK\n");
}

int
main(int argc, char **argv)
{
	daos_oclass_id_t	*test_classes;
	uint32_t		 num_test_oc;
	char			 oclass_name[50];
	int			 oc_index;
	int			 rc;

	rc = daos_debug_init(NULL);
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
		reint_object_class(test_classes[oc_index]);

	}

	D_FREE(test_classes);
	D_PRINT("all tests passed!\n");

	daos_debug_fini();
	return 0;
}
