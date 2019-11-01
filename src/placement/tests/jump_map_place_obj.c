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
#include <daos.h>
#include "place_obj_common.h"

#define DOM_NR		8
#define	NODE_PER_DOM	1
#define VOS_PER_TARGET	4
#define SPARE_MAX_NUM	(DOM_NR * 3)

#define COMPONENT_NR	(DOM_NR + DOM_NR * NODE_PER_DOM + \
			 DOM_NR * NODE_PER_DOM * VOS_PER_TARGET)

static bool			 pl_debug_msg;

int
main(int argc, char **argv)
{
	struct pool_buf		*buf;
	struct pl_map_init_attr	 mia;
	int			 i;
	int			 nr;
	struct pool_map		*po_map;
	struct pool_component	*comp;
	struct pl_obj_layout	*lo_1;
	struct pl_obj_layout	*lo_2;
	struct pl_obj_layout	*lo_3;
	struct pl_map		*pl_map;
	struct pool_component	 comps[COMPONENT_NR];
	uuid_t			 pl_uuid;
	daos_obj_id_t		 oid;
	uint32_t		 spare_tgt_candidate[SPARE_MAX_NUM];
	uint32_t		 spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		 shard_ids[SPARE_MAX_NUM];
	uint32_t		 failed_tgts[SPARE_MAX_NUM];
	static uint32_t		 po_ver;
	unsigned int		 spare_cnt;
	int			 rc;

	po_ver = 1;
	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = pl_init();
	if (rc != 0) {
		daos_debug_fini();
		return rc;
	}

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.lo = rand();
	oid.hi = 5;

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < DOM_NR; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RACK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= NODE_PER_DOM;
	}

	for (i = 0; i < DOM_NR * NODE_PER_DOM; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= VOS_PER_TARGET;
	}

	for (i = 0; i < DOM_NR * NODE_PER_DOM * VOS_PER_TARGET; i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= 1;
	}

	nr = ARRAY_SIZE(comps);
	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	D_ASSERT(rc == 0);

	rc = pool_map_create(buf, 1, &po_map);
	D_ASSERT(rc == 0);

	pool_map_print(po_map);

	mia.ia_type	    = PL_TYPE_JUMP_MAP;
	mia.ia_jump_map.domain  = PO_COMP_TP_RACK;

	rc = pl_map_create(po_map, &mia, &pl_map);
	D_ASSERT(rc == 0);

	pl_map_print(pl_map);

	/* initial placement when all nodes alive */
	daos_obj_generate_id(&oid, 0, OC_RP_4G2, 0);
	D_PRINT("\ntest initial placement when no failed shard ...\n");
	plt_obj_place(oid, &lo_1, pl_map);
	plt_obj_layout_check(lo_1);

	/* test plt_obj_place when some/all shards failed */
	D_PRINT("\ntest to fail all shards  and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_fail_tgt(lo_1->ol_shards[i].po_target, &po_ver, po_map,
				pl_debug_msg);
	plt_obj_place(oid, &lo_2, pl_map);
	plt_obj_layout_check(lo_2);
	D_ASSERT(!pt_obj_layout_match(lo_1, lo_2, DOM_NR));
	D_PRINT("spare target candidate:");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++) {
		spare_tgt_candidate[i] = lo_2->ol_shards[i].po_target;
		D_PRINT(" %d", spare_tgt_candidate[i]);
	}
	D_PRINT("\n");

	D_PRINT("\ntest to add back all failed shards and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_add_tgt(lo_1->ol_shards[i].po_target, &po_ver, po_map,
				pl_debug_msg);
	plt_obj_place(oid, &lo_3, pl_map);
	plt_obj_layout_check(lo_3);
	D_ASSERT(pt_obj_layout_match(lo_1, lo_3, DOM_NR));

	/* test pl_obj_find_rebuild */
	D_PRINT("\ntest pl_obj_find_rebuild to get correct spare tagets ...\n");
	failed_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[1] = lo_3->ol_shards[1].po_target;
	D_PRINT("failed target %d[0], %d[1]\n, expected %d[0], %d[1]\n",
		failed_tgts[0], failed_tgts[1], spare_tgt_candidate[0],
		spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 2, spare_tgt_ranks,
			pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			PL_TYPE_JUMP_MAP, SPARE_MAX_NUM, po_map, pl_map);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[0]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[1]);

	/* fail the to-be-spare target and select correct next spare */
	failed_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[1] = lo_3->ol_shards[1].po_target;
	failed_tgts[2] = spare_tgt_candidate[0];
	D_PRINT("\nfailed targets %d[1] %d %d[0], expected spare %d[1]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2],
		spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 3, spare_tgt_ranks,
			   pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			   PL_TYPE_JUMP_MAP, SPARE_MAX_NUM, po_map, pl_map);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[1]);

	failed_tgts[0] = spare_tgt_candidate[0];
	failed_tgts[1] = spare_tgt_candidate[1];
	failed_tgts[2] = lo_3->ol_shards[3].po_target;
	failed_tgts[3] = lo_3->ol_shards[0].po_target;
	failed_tgts[4] = lo_3->ol_shards[1].po_target;
	D_PRINT("\nfailed targets %d %d %d[3] %d[0] %d[1]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2], failed_tgts[3],
		failed_tgts[4]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 5, spare_tgt_ranks,
			   pl_debug_msg, shard_ids, &spare_cnt, &po_ver,
			   PL_TYPE_JUMP_MAP, SPARE_MAX_NUM, po_map, pl_map);
	D_ASSERT(spare_cnt == 3);

	pl_obj_layout_free(lo_1);
	pl_obj_layout_free(lo_2);
	pl_obj_layout_free(lo_3);

	pool_map_decref(po_map);
	pool_buf_free(buf);
	daos_debug_fini();
	D_PRINT("\nall tests passed!\n");
	return 0;
}
