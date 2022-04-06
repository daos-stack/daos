/**
 * (C) Copyright 2021-2022 Intel Corporation.
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

static void
basic_pda_test(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;

	/* --------------------------------------------------------- */
	print_message("\nWith 2 domains, 2 nodes each, 2 targets each = 8 targets\n");
	gen_maps(1, 2, 2, 2, &po_map, &pl_map);
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2, 0);

	free_pool_and_placement_map(po_map, pl_map);

	/* --------------------------------------------------------- */
	print_message("\nWith 4 PDs, 4 domains each PD, 2 nodes each domain, "
		      "8 targets each node = 256 targets\n");
	gen_maps(4, 4, 2, 8, &po_map, &pl_map);
	print_message("place OC_RP_4G2 pda 3\n");
	assert_placement_success_print(pl_map, OC_RP_4G2, 3);
	print_message("place OC_EC_4P2G2 pda 1\n");
	assert_placement_success_print(pl_map, OC_EC_4P2G2, 1);
	print_message("place OC_EC8P2G2 pda 4\n");
	assert_placement_success_print(pl_map, OC_EC_8P2G2, 4);

	/* --------------------------------------------------------- */
	print_message("place OC_EC16P2G8 pda 3, need 18 domains will fail\n");
	assert_invalid_param(pl_map, OC_EC_16P2G8, 3);

	free_pool_and_placement_map(po_map, pl_map);
}

/* set a fault domain's status */
void
test_set_tgt(struct pool_map *po_map, uint32_t id, uint32_t status)
{
	struct pool_target	*tgt = NULL;
	static int		 fseq = 10;
	int			 rc;

	rc = pool_map_find_target(po_map, id, &tgt);
	assert_rc_equal(rc, 1);
	D_ASSERT(tgt != NULL);

	tgt->ta_comp.co_status = status;
	tgt->ta_comp.co_fseq = fseq++;
	print_message("set target id %d status as %d\n", id, status);
}

static bool
layout_with_colocated_shards(struct pl_obj_layout *layout, uint32_t tgts_per_dom)
{
	uint32_t	i, j, dom, new_dom, tgt, new_tgt;

	for (i = 0; i < layout->ol_nr; i++) {
		tgt =  layout->ol_shards[i].po_target;
		dom = tgt / tgts_per_dom;
		for (j = i + 1; j < layout->ol_nr; j++) {
			new_tgt = layout->ol_shards[j].po_target;
			assert_true(new_tgt != tgt);
			assert_true(tgt != -1 && new_tgt != -1);
			new_dom = new_tgt / tgts_per_dom;
			if (dom == new_dom)
				return true;
		}
	}

	return false;
}

static void
pd_use_all_dom(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		 num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt;
	uint32_t		 num_tgts, pda;
	daos_obj_id_t		 oid;
	struct pl_obj_layout	*layout = NULL;

	/* --------------------------------------------------------- */
	num_pd = 2;
	fdoms_per_pd = 4;
	nodes_per_fdom = 1;
	vos_per_tgt = 1;
	num_tgts = num_pd * fdoms_per_pd * nodes_per_fdom * vos_per_tgt;
	print_message("\nWith %d PDs, %d domains each PD, %d node each domain, "
		      "%d targets each node = %d targets\n", num_pd, fdoms_per_pd,
		      nodes_per_fdom, vos_per_tgt, num_tgts);

	gen_maps(num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt, &po_map, &pl_map);
	print_message("place OC_RP_4G2 pda 3\n");
	pda = 3;
	gen_oid(&oid, 1, UINT64_MAX, OC_RP_4G2);
	assert_success(plt_obj_place(oid, pda, &layout, pl_map, true));
	assert_true(layout_with_colocated_shards(layout, nodes_per_fdom * vos_per_tgt) == false);
	pl_obj_layout_free(layout);
}

static void
pd_shards_colocated(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		 num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt;
	uint32_t		 num_tgts, pda;
	daos_obj_id_t		 oid;
	struct pl_obj_layout	*layout = NULL;

	/* --------------------------------------------------------- */
	num_pd = 2;
	fdoms_per_pd = 4;
	nodes_per_fdom = 1;
	vos_per_tgt = 2;
	num_tgts = num_pd * fdoms_per_pd * nodes_per_fdom * vos_per_tgt;
	print_message("\nWith %d PDs, %d domains each PD, %d node each domain, "
		      "%d targets each node = %d targets\n", num_pd, fdoms_per_pd,
		      nodes_per_fdom, vos_per_tgt, num_tgts);

	gen_maps(num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt, &po_map, &pl_map);
	print_message("place OC_EC_4P2G2 pda 3\n");
	pda = 3;
	gen_oid(&oid, 1, UINT64_MAX, OC_EC_4P2G2);
	assert_success(plt_obj_place(oid, pda, &layout, pl_map, true));
	assert_true(layout_with_colocated_shards(layout, nodes_per_fdom * vos_per_tgt) == true);
	pl_obj_layout_free(layout);
}

static void
pd_with_failed_dom(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		 num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt;
	uint32_t		 num_tgts, i, pda, start, num;
	uint32_t		 shard, tgt, new_tgt, fault_dom, new_dom;
	daos_obj_id_t		 oid;
	struct pl_obj_layout	*layout = NULL;

	/* --------------------------------------------------------- */
	num_pd = 4;
	fdoms_per_pd = 4;
	nodes_per_fdom = 1;
	vos_per_tgt = 8;
	num_tgts = num_pd * fdoms_per_pd * nodes_per_fdom * vos_per_tgt;
	print_message("\nWith %d PDs, %d domains each PD, %d node each domain, "
		      "%d targets each node = %d targets\n", num_pd, fdoms_per_pd,
		      nodes_per_fdom, vos_per_tgt, num_tgts);

	gen_maps(num_pd, fdoms_per_pd, nodes_per_fdom, vos_per_tgt, &po_map, &pl_map);
	print_message("place OC_RP_4G2 pda 4\n");
	pda = 4;
	gen_oid(&oid, 1, UINT64_MAX, OC_RP_4G2);
	assert_success(plt_obj_place(oid, pda, &layout, pl_map, true));

	shard = 5;
	tgt = layout->ol_shards[shard].po_target;
	pl_obj_layout_free(layout);

	fault_dom = tgt / (nodes_per_fdom * vos_per_tgt);
	print_message("place OC_RP_4G2 pda 4, fail all tgts of dom[%d]\n", fault_dom);
	start = fault_dom * nodes_per_fdom * vos_per_tgt;
	num = nodes_per_fdom * vos_per_tgt;
	for (i = start; i < start + num; i++)
		test_set_tgt(po_map, i, PO_COMP_ST_DOWN);
	layout = NULL;
	assert_success(plt_obj_place(oid, pda, &layout, pl_map, true));
	new_tgt = layout->ol_shards[shard].po_target;
	new_dom = new_tgt / (nodes_per_fdom * vos_per_tgt);
	assert_true(new_tgt != tgt);
	assert_true(new_dom != fault_dom);
	print_message("shard[%d] changed from tgt[%d]/dom[%d] to tgt[%d]/dom[%d]\n",
		      shard, tgt, fault_dom, new_tgt, new_dom);
	print_message("checking should not with co-located shards ...\n");
	assert_true(layout_with_colocated_shards(layout, nodes_per_fdom * vos_per_tgt) == false);

	pl_obj_layout_free(layout);
	free_pool_and_placement_map(po_map, pl_map);
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

#define T(dsc, test) { "PLACEMENT "STR(__COUNTER__)" ("#test"): " dsc, test, \
			  placement_test_setup, placement_test_teardown }

static const struct CMUnitTest pda_tests[] = {
	/* Standard configurations */
	T("Base PDA test", basic_pda_test),
	T("PDA test - use all domains", pd_use_all_dom),
	T("PDA test - colocated shards", pd_shards_colocated),
	T("PDA test - PD with failed domain", pd_with_failed_dom),
};

int
pda_tests_run(bool verbose)
{
	int rc = 0;

	g_verbose = verbose;

	rc += cmocka_run_group_tests_name("Jump Map Placement PDA Tests", pda_tests,
					  NULL, NULL);

	return rc;
}
