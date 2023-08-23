/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>
#include "place_obj_common.h"

#define DOM_NR          8
#define NODE_PER_DOM    1
#define VOS_PER_TARGET  4
#define SPARE_MAX_NUM   (DOM_NR * 3)

#define COMPONENT_NR    (DOM_NR + DOM_NR * NODE_PER_DOM + \
			 DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)

static bool                      pl_debug_msg;

int
main(int argc, char **argv)
{
	int			 i;
	struct pool_map		*po_map;
	struct pl_obj_layout	*lo_1;
	struct pl_obj_layout	*lo_2;
	struct pl_obj_layout	*lo_3;
	struct pl_map		*pl_map;
	uuid_t			 pl_uuid;
	daos_obj_id_t		 oid;
	uint32_t		 spare_tgt_candidate[SPARE_MAX_NUM];
	uint32_t		 spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		 shard_ids[SPARE_MAX_NUM];
	uint32_t		 failed_tgts[SPARE_MAX_NUM];
	static uint32_t		 po_ver;
	unsigned int		 spare_cnt;
	int			 rc;
	uint32_t                 reint_tgts[SPARE_MAX_NUM];

	po_ver = 1;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = pl_init();
	if (rc != 0) {
		daos_debug_fini();
		return rc;
	}

	gen_pool_and_placement_map(1, DOM_NR, NODE_PER_DOM,
				   VOS_PER_TARGET, PL_TYPE_RING,
				   PO_COMP_TP_RANK, &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);
	pool_map_print(po_map);
	pl_map_print(pl_map);

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.lo = rand();
	oid.hi = 5;

	/* initial placement when all nodes alive */
	rc = daos_obj_set_oid_by_class(&oid, 0, OC_RP_4G2, 0);
	assert_success(rc == 0);
	D_PRINT("\ntest initial placement when no failed shard ...\n");
	assert_success(plt_obj_place(oid, 0, &lo_1, pl_map, true));
	plt_obj_layout_check(lo_1, COMPONENT_NR, 0);

	/* test plt_obj_place when some/all shards failed */
	D_PRINT("\ntest to fail all shards  and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_fail_tgt(lo_1->ol_shards[i].po_target, &po_ver, po_map,
			     pl_debug_msg);
	assert_success(plt_obj_place(oid, 0, &lo_2, pl_map, true));
	plt_obj_layout_check(lo_2, COMPONENT_NR, 0);
	D_ASSERT(!plt_obj_layout_match(lo_1, lo_2));
	D_PRINT("spare target candidate:");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++) {
		spare_tgt_candidate[i] = lo_2->ol_shards[i].po_target;
		D_PRINT(" %d", spare_tgt_candidate[i]);
	}
	D_PRINT("\n");

	D_PRINT("\ntest to add back all failed shards and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_reint_tgt_up(lo_1->ol_shards[i].po_target, &po_ver, po_map,
			    pl_debug_msg);
	assert_success(plt_obj_place(oid, 0, &lo_3, pl_map, true));
	plt_obj_layout_check(lo_3, COMPONENT_NR, 0);
	D_ASSERT(plt_obj_layout_match(lo_1, lo_3));

	/* test pl_obj_find_rebuild */
	D_PRINT("\ntest pl_obj_find_rebuild to get correct spare targets ...\n");
	failed_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[1] = lo_3->ol_shards[1].po_target;
	D_PRINT("failed target %d[0], %d[1], expected spare %d %d\n",
		failed_tgts[0], failed_tgts[1], spare_tgt_candidate[0],
		spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 2, spare_tgt_ranks,
			   pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			   PL_TYPE_RING, SPARE_MAX_NUM, po_map, pl_map);
	plt_obj_rebuild_unique_check(shard_ids, spare_cnt, COMPONENT_NR);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 0);
	D_ASSERT(shard_ids[1] == 1);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[0]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[1]);

	/* test pl_obj_find_reint */
	D_PRINT("\ntest pl_obj_find_reint to get correct reintegration "
		"targets ...\n");
	reint_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[0] = lo_3->ol_shards[1].po_target;
	plt_reint_tgts_get(pl_uuid, oid, failed_tgts, 1, reint_tgts, 1,
			spare_tgt_ranks, shard_ids, &spare_cnt, PL_TYPE_RING,
			SPARE_MAX_NUM, po_map, pl_map, &po_ver, pl_debug_msg);
	plt_obj_rebuild_unique_check(shard_ids, spare_cnt, COMPONENT_NR);
	D_PRINT("reintegrated target %d. expected target %d\n",
		reint_tgts[0], lo_3->ol_shards[0].po_target);
	D_ASSERT(spare_cnt == 1);
	D_ASSERT(shard_ids[0] == 0);
	D_ASSERT(spare_tgt_ranks[0] == lo_3->ol_shards[0].po_target);

	/* fail the to-be-spare target and select correct next spare */
	failed_tgts[0] = lo_3->ol_shards[1].po_target;
	failed_tgts[1] = spare_tgt_candidate[0];
	failed_tgts[2] = lo_3->ol_shards[0].po_target;
	D_PRINT("\nfailed targets %d[1] %d %d[0], expected spare %d[0] %d[1]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2],
		spare_tgt_candidate[2], spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 3, spare_tgt_ranks,
			   pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			   PL_TYPE_RING, SPARE_MAX_NUM, po_map, pl_map);
	plt_obj_rebuild_unique_check(shard_ids, spare_cnt, COMPONENT_NR);
	/* should get next spare targets, the first spare candidate failed,
	 * and shard[0].fseq > shard[1].fseq, so will select shard[1]'s
	 * next spare first.
	 */
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 1);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[1]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[2]);


	/* test pl_obj_find_reint */
	D_PRINT("\ntest pl_obj_find_reint to get correct reintregation "
		"targets ...\n");
	reint_tgts[0] = lo_3->ol_shards[0].po_target;
	reint_tgts[1] = spare_tgt_candidate[0];
	failed_tgts[0] = lo_3->ol_shards[1].po_target;
	plt_reint_tgts_get(pl_uuid, oid, failed_tgts, 1, reint_tgts, 2,
			spare_tgt_ranks, shard_ids, &spare_cnt, PL_TYPE_RING,
			SPARE_MAX_NUM, po_map, pl_map, &po_ver, pl_debug_msg);
	plt_obj_rebuild_unique_check(shard_ids, spare_cnt, COMPONENT_NR);
	D_PRINT("reintegrated target %d and %d. expected target "
		"%d and %d\n", reint_tgts[0], reint_tgts[1],
		lo_3->ol_shards[0].po_target, spare_tgt_ranks[0]);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(spare_tgt_ranks[1] == lo_3->ol_shards[0].po_target);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[0]);

	failed_tgts[0] = spare_tgt_candidate[0];
	failed_tgts[1] = spare_tgt_candidate[1];
	failed_tgts[2] = lo_3->ol_shards[3].po_target;
	failed_tgts[3] = lo_3->ol_shards[0].po_target;
	failed_tgts[4] = lo_3->ol_shards[1].po_target;
	D_PRINT("\nfailed targets %d %d %d[3] %d[0] %d[1], "
		"expected spare %d[0] %d[1] %d[3]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2], failed_tgts[3],
		failed_tgts[4], spare_tgt_candidate[3], spare_tgt_candidate[4],
		spare_tgt_candidate[2]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 5, spare_tgt_ranks,
			   pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			   PL_TYPE_RING, SPARE_MAX_NUM, po_map, pl_map);
	plt_obj_rebuild_unique_check(shard_ids, spare_cnt, COMPONENT_NR);
	D_ASSERT(spare_cnt == 3);
	D_ASSERT(shard_ids[0] == 3);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(shard_ids[2] == 1);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[2]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[3]);
	D_ASSERT(spare_tgt_ranks[2] == spare_tgt_candidate[4]);

	pl_obj_layout_free(lo_1);
	pl_obj_layout_free(lo_2);
	pl_obj_layout_free(lo_3);

	free_pool_and_placement_map(po_map, pl_map);
	pl_fini();
	daos_debug_fini();
	D_PRINT("\nall tests passed!\n");
	return 0;
}
