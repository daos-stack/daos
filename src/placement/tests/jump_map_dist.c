/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include <daos/object.h>
#include <math.h>

#include "place_obj_common.h"
/* Gain some internal knowledge of pool server */
#include "../../pool/rpc.h"
#include "../../pool/srv_pool_map.h"

static int test_num_objs = 1024;
static int test_obj_class = OC_EC_8P2G2;

static void
layout_count_tgt(struct pl_obj_layout *layout, uint32_t *tgt_counters, uint32_t size)
{
	int grp;
	int sz;
	int index;

	for (grp = 0; grp < layout->ol_grp_nr; ++grp) {
		for (sz = 0; sz < layout->ol_grp_size; ++sz) {
			struct pl_obj_shard shard;

			index = (grp * layout->ol_grp_size) + sz;
			shard = layout->ol_shards[index];
			D_ASSERT(shard.po_target < size);
			tgt_counters[shard.po_target]++;
		}
	}
}

#define MAX_OID_HI ((1UL << 32) - 1)
static void
layout_dist_test(void **state, uint32_t pd_nr, uint32_t doms_per_pd, uint32_t nodes_per_dom,
		 uint32_t tgts_per_node, uint32_t pda)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	struct pl_obj_layout	**layouts;
	daos_obj_id_t		*oids;
	char			 obj_class_name[64] = {0};
	uint32_t		*tgt_counters;
	uint32_t		 tgt_max = 0, tgt_min = -1, tgt_avg = 0;
	double			 std_dev;
	uint64_t		 lo = 0, hi = 0;
	uint32_t		 i, total_tgts;
	int			 rc;

	if (pd_nr == 0)
		pd_nr = 1;
	total_tgts = pd_nr * doms_per_pd * nodes_per_dom * tgts_per_node;
	daos_oclass_id2name(test_obj_class, obj_class_name);
	print_message("\nWith %d PDs, %d domains each PD, %d nodes each domain, "
		      "%d targets each node = %d targets, num_objs %d, obj_class %s\n",
		      pd_nr, doms_per_pd, nodes_per_dom, tgts_per_node, total_tgts,
		      test_num_objs, obj_class_name);
	gen_maps(pd_nr, doms_per_pd, nodes_per_dom, tgts_per_node, &po_map, &pl_map);

	D_ALLOC_ARRAY(layouts, test_num_objs);
	D_ASSERT(layouts != NULL);
	D_ALLOC_ARRAY(oids, test_num_objs);
	D_ASSERT(oids != NULL);
	D_ALLOC_ARRAY(tgt_counters, total_tgts);
	D_ASSERT(tgt_counters != NULL);

	for (i = 0; i < test_num_objs; i++) {
		if (i % MAX_OID_HI == 0) {
			lo++;
			hi = 0;
		}
		gen_oid(&oids[i], lo, hi++, test_obj_class);
		rc = plt_obj_place(oids[i], pda, &layouts[i], pl_map, false);
		assert_rc_equal(rc, 0);
		layout_count_tgt(layouts[i], tgt_counters, total_tgts);
		pl_obj_layout_free(layouts[i]);
	}

	for (i = 0; i < total_tgts; i++) {
		tgt_max = max(tgt_counters[i], tgt_max);
		tgt_min = min(tgt_counters[i], tgt_min);
		tgt_avg += tgt_counters[i];
	}
	tgt_avg /= total_tgts;

	std_dev = 0;
	print_message("Place %d object (class %s), on %d targets, #shards on each target -\n",
		      test_num_objs, obj_class_name, total_tgts);
	for (i = 0; i < total_tgts; i++) {
		if (i > 0 && i % 8 == 0)
			print_message("\n");
		std_dev += pow(tgt_counters[i] - tgt_avg, 2);
		print_message("[%4d]: %4d;||", i, tgt_counters[i]);
	}
	print_message("\n");
	std_dev /= total_tgts;
	std_dev = sqrt(std_dev);

	print_message("\nPlace %d object (class %s), on %d targets, statistics of #shards on tgts\n"
		      "\t\tmin:      %d\n"
		      "\t\taverage:  %d\n"
		      "\t\tmax:      %d\n"
		      "\t\tstd_dev:  %.2f\n",
		      test_num_objs, obj_class_name, total_tgts,
		      tgt_min, tgt_avg, tgt_max, std_dev);

	free_pool_and_placement_map(po_map, pl_map);
	D_FREE(oids);
	D_FREE(layouts);
	D_FREE(tgt_counters);
}

static void
basic_dist_test(void **state)
{
	layout_dist_test(state, 0, 16, 8, 8, 0);
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

static const struct CMUnitTest dist_tests[] = {
	/* Standard configurations */
	T("Basic obj layout distribution test", basic_dist_test),
};

int
dist_tests_run(bool verbose, uint32_t num_objs, int obj_class)
{
	int rc = 0;

	g_verbose = verbose;
	if (num_objs != 0)
		test_num_objs = num_objs;
	if (obj_class != 0)
		test_obj_class = obj_class;

	rc += cmocka_run_group_tests_name("Obj placement distribution Tests",
					  dist_tests, NULL, NULL);

	return rc;
}
