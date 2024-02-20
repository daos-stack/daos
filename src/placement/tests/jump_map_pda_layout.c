/**
 * (C) Copyright 2021-2023 Intel Corporation.
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
print_layout_with_pd(struct pl_obj_layout *layout, int grp_tgt_nr)
{
	int grp;
	int sz;
	int index;
	int grps_per_line;

	if (layout->ol_grp_size == 3)
		grps_per_line = 2;
	else if (layout->ol_grp_size >= 5)
		grps_per_line = 1;
	else
		grps_per_line = 8 / layout->ol_grp_size;

	for (grp = 0; grp < layout->ol_grp_nr; ++grp) {
		printf("[");
		for (sz = 0; sz < layout->ol_grp_size; ++sz) {
			struct pl_obj_shard shard;

			index = (grp * layout->ol_grp_size) + sz;
			shard = layout->ol_shards[index];
			printf("%3d=>%3d_PD%d%s ", shard.po_shard, shard.po_target,
			       shard.po_target / grp_tgt_nr,
			       shard.po_rebuilding ? "R" : "");
		}
		printf("\b]");
		if (grp > 0 && ((grp + 1) % grps_per_line) == 0)
			printf("\n");
	}
	printf("\n");
}

static int
plt_obj_place_with_pd(daos_obj_id_t oid, uint32_t pda, struct pl_obj_layout **layout,
		      struct pl_map *pl_map, bool print_layout_flag, int grp_tgt_nr)
{
	struct daos_obj_md	 md;
	int			 rc;

	memset(&md, 0, sizeof(md));
	md.omd_id  = oid;
	md.omd_pda = pda;
	md.omd_pdom_lvl = PO_COMP_TP_GRP;
	md.omd_fdom_lvl = PO_COMP_TP_RANK;
	D_ASSERT(pl_map != NULL);
	md.omd_ver = pool_map_get_version(pl_map->pl_poolmap);

	rc = pl_obj_place(pl_map, 1, &md, 0, NULL, layout);
	if (print_layout_flag) {
		if (*layout != NULL)
			print_layout_with_pd(*layout, grp_tgt_nr);
		else
			print_message("No layout created.\n");
	}

	return rc;
}

#define assert_placement_success_pda(pl_map, cid, pda, grp_tgt_nr)			\
	do {										\
		daos_obj_id_t __oid;							\
		struct pl_obj_layout *__layout = NULL;					\
		gen_oid(&__oid, 1, UINT64_MAX, cid);					\
		assert_success(plt_obj_place_with_pd(__oid, pda, &__layout, pl_map,	\
				true, grp_tgt_nr));					\
		pl_obj_layout_free(__layout);						\
	} while (0)

static void
pda_layout_show(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	int			 grp_nr, srvs_per_grp, engs_per_srv, tgts_per_eng;
	int			 tgts_total, tgts_per_grp;

	grp_nr		= 4;
	srvs_per_grp	= 4;
	engs_per_srv	= 2;
	tgts_per_eng	= 16;

	tgts_per_grp	= srvs_per_grp * engs_per_srv * tgts_per_eng;
	tgts_total	= grp_nr * tgts_per_grp;

	/* --------------------------------------------------------- */
	print_message("\nWith %d server groups (PDs),\n"
			"     %d servers for each group,\n"
			"     %d engines for each server,\n"
			"     %d targets for each engine,\n"
			"     %d targets totalty (each server group with %d targets).\n",
			grp_nr, srvs_per_grp, engs_per_srv, tgts_per_eng,
			tgts_total, tgts_per_grp);
	gen_maps_adv(grp_nr, srvs_per_grp, engs_per_srv, tgts_per_eng, PO_COMP_TP_NODE,
		     &po_map, &pl_map);

	print_message("press enter to show layout of OC_S32 object, PDA -1 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_S32, -1, tgts_per_grp);

	print_message("press enter to show layout of OC_S32 object, PDA 1 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_S32, 1, tgts_per_grp);

	print_message("press enter to show layout of OC_RP_3G32 object, PDA -1 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_RP_3G32, -1, tgts_per_grp);

	print_message("press enter to show layout of OC_RP_3G32 object, PDA 1 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_RP_3G32, 1, tgts_per_grp);

	print_message("press enter to show layout of OC_RP_2G32 object, PDA 2 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_RP_2G32, 2, tgts_per_grp);

	print_message("press enter to show layout of OC_RP_2G32 object, PDA 1 ...\n");
	getchar();
	assert_placement_success_pda(pl_map, OC_RP_2G32, 1, tgts_per_grp);

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

static const struct CMUnitTest pda_layout_tests[] = {
	/* Standard configurations */
	T("PDA layout show", pda_layout_show),
};

int
pda_layout_run(bool verbose)
{
	int rc = 0;

	g_verbose = verbose;

	rc += cmocka_run_group_tests_name("Jump Map Placement PDA demo", pda_layout_tests,
					  NULL, NULL);

	return rc;
}
