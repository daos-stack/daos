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

#define DOM_NR		8
#define	TARGET_PER_DOM	4
#define VOS_PER_TARGET	8
#define SPARE_MAX_NUM	(DOM_NR * 3)

static struct pool_map		*po_map;
static struct pl_map		*pl_map;
static struct pool_component	 comps[DOM_NR + DOM_NR * TARGET_PER_DOM];
static uint32_t			 po_ver = 1;
static bool			 pl_debug_msg;

static void
plt_obj_place(daos_obj_id_t oid, struct pl_obj_layout **layout)
{
	struct daos_obj_md	 md;
	int			 i;
	int			 rc;

	memset(&md, 0, sizeof(md));
	md.omd_id  = oid;
	md.omd_ver = 1;

	rc = pl_obj_place(pl_map, &md, NULL, layout);
	D_ASSERT(rc == 0);

	D_PRINT("Layout of object "DF_OID"\n", DP_OID(oid));
	for (i = 0; i < (*layout)->ol_nr; i++)
		D_PRINT("%d ", (*layout)->ol_shards[i].po_target);

	D_PRINT("\n");
}

static void
plt_obj_layout_check(struct pl_obj_layout *layout)
{
	int i;

	for (i = 0; i < layout->ol_nr; i++)
		D_ASSERT(layout->ol_shards[i].po_target != -1);
}

static bool
pt_obj_layout_match(struct pl_obj_layout *lo_1, struct pl_obj_layout *lo_2)
{
	int	i;

	D_ASSERT(lo_1->ol_nr == lo_2->ol_nr);
	D_ASSERT(lo_1->ol_nr > 0 && lo_1->ol_nr <= DOM_NR);

	for (i = 0; i < lo_1->ol_nr; i++) {
		if (lo_1->ol_shards[i].po_target !=
		    lo_2->ol_shards[i].po_target)
			return false;
	}

	return true;
}

static void
plt_set_tgt_status(uint32_t id, int status, uint32_t ver)
{
	struct pool_target	*target;
	char			*str;
	int			 rc;

	switch (status) {
	case PO_COMP_ST_UP:
		str = "PO_COMP_ST_UP";
		break;
	case PO_COMP_ST_UPIN:
		str = "PO_COMP_ST_UPIN";
		break;
	case PO_COMP_ST_DOWN:
		str = "PO_COMP_ST_DOWN";
		break;
	case PO_COMP_ST_DOWNOUT:
		str = "PO_COMP_ST_DOWNOUT";
		break;
	default:
		str = "unknown";
		break;
	};

	rc = pool_map_find_target(po_map, id, &target);
	D_ASSERT(rc == 1);
	if (pl_debug_msg)
		D_PRINT("set target id %d, rank %d as %s, ver %d.\n",
			id, target->ta_comp.co_rank, str, ver);
	target->ta_comp.co_status = status;
	target->ta_comp.co_fseq = ver;
	rc = pool_map_set_version(po_map, ver);
	D_ASSERT(rc == 0);
}

static void
plt_fail_tgt(uint32_t id)
{
	po_ver++;
	plt_set_tgt_status(id, PO_COMP_ST_DOWN, po_ver);
}

static void
plt_add_tgt(uint32_t id)
{
	po_ver++;
	plt_set_tgt_status(id, PO_COMP_ST_UP, po_ver);
}

static void
plt_spare_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *spare_tgt_ranks,
		   uint32_t *shard_ids, uint32_t *spare_cnt)
{
	struct daos_obj_md	md = { 0 };
	int			i;
	int			rc;

	for (i = 0; i < failed_cnt; i++)
		plt_fail_tgt(failed_tgts[i]);

	rc = pl_map_update(pl_uuid, po_map, false);
	D_ASSERT(rc == 0);
	pl_map = pl_map_find(pl_uuid, oid);
	D_ASSERT(pl_map != NULL);
	dc_obj_fetch_md(oid, &md);
	md.omd_ver = po_ver;
	*spare_cnt = pl_obj_find_rebuild(pl_map, &md, NULL, po_ver,
					 spare_tgt_ranks, shard_ids,
					 SPARE_MAX_NUM);
	D_PRINT("spare_cnt %d for version %d -\n", *spare_cnt, po_ver);
	for (i = 0; i < *spare_cnt; i++)
		D_PRINT("shard %d, spare target rank %d\n",
			shard_ids[i], spare_tgt_ranks[i]);

	pl_map_decref(pl_map);

	for (i = 0; i < failed_cnt; i++)
		plt_add_tgt(failed_tgts[i]);
}

int
main(int argc, char **argv)
{
	struct pool_buf		*buf;
	struct pl_map_init_attr	 mia;
	int			 i;
	int			 nr;
	struct pool_component	*comp;
	struct pl_obj_layout	*lo_1;
	struct pl_obj_layout	*lo_2;
	struct pl_obj_layout	*lo_3;
	uuid_t			 pl_uuid;
	daos_obj_id_t		 oid;
	uint32_t		 spare_tgt_candidate[SPARE_MAX_NUM];
	uint32_t		 spare_tgt_ranks[SPARE_MAX_NUM];
	uint32_t		 shard_ids[SPARE_MAX_NUM];
	uint32_t		 failed_tgts[SPARE_MAX_NUM];
	unsigned int		 spare_cnt;
	int			 rc;

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.lo = rand();
	oid.hi = 5;

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < DOM_NR; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RACK;
		comp->co_status = PO_COMP_ST_UP;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= TARGET_PER_DOM;
	}

	for (i = 0; i < DOM_NR * TARGET_PER_DOM; i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UP;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= VOS_PER_TARGET;
	}

	nr = ARRAY_SIZE(comps);
	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	D_ASSERT(rc == 0);

	rc = pool_map_create(buf, 1, &po_map);
	D_ASSERT(rc == 0);

	pool_map_print(po_map);

	mia.ia_type	    = PL_TYPE_RING;
	mia.ia_ring.ring_nr = 1;
	mia.ia_ring.domain  = PO_COMP_TP_RACK;

	rc = pl_map_create(po_map, &mia, &pl_map);
	D_ASSERT(rc == 0);

	pl_map_print(pl_map);

	/* initial placement when all nodes alive */
	daos_obj_generate_id(&oid, 0, DAOS_OC_R4_RW);
	D_PRINT("\ntest initial placement when no failed shard ...\n");
	plt_obj_place(oid, &lo_1);
	plt_obj_layout_check(lo_1);

	/* test plt_obj_place when some/all shards failed */
	D_PRINT("\ntest to fail all shards  and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_fail_tgt(lo_1->ol_shards[i].po_target);
	plt_obj_place(oid, &lo_2);
	plt_obj_layout_check(lo_2);
	D_ASSERT(!pt_obj_layout_match(lo_1, lo_2));
	D_PRINT("spare target candidate:");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++) {
		spare_tgt_candidate[i] = lo_2->ol_shards[i].po_target;
		D_PRINT(" %d", spare_tgt_candidate[i]);
	}
	D_PRINT("\n");

	D_PRINT("\ntest to add back all failed shards and new placement ...\n");
	for (i = 0; i < SPARE_MAX_NUM && i < lo_1->ol_nr; i++)
		plt_add_tgt(lo_1->ol_shards[i].po_target);
	plt_obj_place(oid, &lo_3);
	plt_obj_layout_check(lo_3);
	D_ASSERT(pt_obj_layout_match(lo_1, lo_3));
	pl_map_decref(pl_map);
	pl_map = NULL;

	/* test pl_obj_find_rebuild */
	D_PRINT("\ntest pl_obj_find_rebuild to get correct spare tagets ...\n");
	failed_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[1] = lo_3->ol_shards[1].po_target;
	D_PRINT("failed target %d[0], %d[1], expected spare %d %d\n",
		failed_tgts[0], failed_tgts[1], spare_tgt_candidate[0],
		spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 2, spare_tgt_ranks,
			   shard_ids, &spare_cnt);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 0);
	D_ASSERT(shard_ids[1] == 1);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[0]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[1]);

	/* fail the to-be-spare target and select correct next spare */
	failed_tgts[0] = lo_3->ol_shards[1].po_target;
	failed_tgts[1] = spare_tgt_candidate[0];
	failed_tgts[2] = lo_3->ol_shards[0].po_target;
	D_PRINT("\nfailed targets %d[1] %d %d[0], expected spare %d[0] %d[1]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2],
		spare_tgt_candidate[2], spare_tgt_candidate[1]);
	plt_spare_tgts_get(pl_uuid, oid, failed_tgts, 3, spare_tgt_ranks,
			   shard_ids, &spare_cnt);
	/* should get next spare targets, the first spare candidate failed,
	 * and shard[0].fseq > shard[1].fseq, so will select shard[1]'s
	 * next spare first.
	 */
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 1);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[1]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[2]);

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
			   shard_ids, &spare_cnt);
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

	pool_map_decref(po_map);
	pool_buf_free(buf);
	daos_debug_fini();
	D_PRINT("\nall tests passed!\n");
	return 0;
}
