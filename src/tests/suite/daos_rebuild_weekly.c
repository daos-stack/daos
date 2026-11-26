/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Long-running rebuild tests which are too slow for every PR and are run
 * weekly (daos_test -k, ftest daos_test/rebuild_weekly.py).
 *
 * tests/suite/daos_rebuild_weekly.c
 */
#define D_LOGFAC DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static bool
rae_is_zero(const char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (buf[i] != 0)
			return false;
	return true;
}

#define RAE_DATA_NR   2 /* OC_EC_2P2G1 */
#define RAE_TGT_NR    4
#define RAE_STRIPE_NR 2048 /* long dkey: the stale leader's pass takes many seconds */
#define RAE_EXT_NR    256  /* extents per cell: slows P1's pass down to the leader's pace */
#define RAE_AGG_WAIT  45
#define RAE_EPH_WAIT  15
#define RAE_P1_WARM   25 /* > the 18 s sleep of the aggregation ULT after rebuild */
/* small cells so that the many stripes fit in the default (4 GB) pool */
#define RAE_CELL_SIZE DAOS_EC_CELL_MIN

static int
rebuild_ec_8nodes_small_cell_setup(void **state)
{
	return rebuild_ec_setup_cell(state, 8, DAOS_PROP_CO_REDUN_RF2, RAE_CELL_SIZE);
}

static void
rae_layout_get(test_arg_t *arg, daos_obj_id_t oid, d_rank_t *rank)
{
	struct daos_obj_layout *layout;
	int                     s, rc;

	rc = daos_obj_layout_get(arg->coh, oid, &layout);
	assert_rc_equal(rc, 0);
	assert_int_equal(layout->ol_shards[0]->os_replica_nr, RAE_TGT_NR);
	for (s = 0; s < RAE_TGT_NR; s++)
		rank[s] = layout->ol_shards[0]->os_shard_loc[s].sd_rank;
	daos_obj_layout_free(layout);
}

/* write one cell of a stripe as @ext_nr extents (a partial stripe update) */
static void
rae_write_cell(daos_handle_t oh, const char *dkey_str, int stripe, int cell, int ext_nr, char *buf)
{
	daos_recx_t *recxs;
	daos_iod_t   iod = {0};
	d_sg_list_t  sgl;
	d_iov_t      sg_iov;
	daos_key_t   dkey;
	uint64_t     ext_size = RAE_CELL_SIZE / ext_nr;
	int          i, rc;

	D_ALLOC_ARRAY(recxs, ext_nr);
	assert_non_null(recxs);
	for (i = 0; i < ext_nr; i++) {
		recxs[i].rx_idx = (uint64_t)stripe * RAE_DATA_NR * RAE_CELL_SIZE +
				  (uint64_t)cell * RAE_CELL_SIZE + i * ext_size;
		recxs[i].rx_nr = ext_size;
	}
	d_iov_set(&dkey, (void *)dkey_str, strlen(dkey_str));
	d_iov_set(&iod.iod_name, "a_key", strlen("a_key"));
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;
	iod.iod_nr    = ext_nr;
	iod.iod_recxs = recxs;
	d_iov_set(&sg_iov, buf, RAE_CELL_SIZE);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	D_FREE(recxs);
}

/*
 * Reproducer for the EC aggregation on an excluded (but still alive) target:
 * - The rank hosting P0 is excluded, then the rank hosting P1. Both shards are
 *   rebuilt on other ranks, P0 with a lower fseq than P1 so P0 is the parity
 *   leader. The old P0 rank still has the stale shard and keeps running EC
 *   aggregation as the "parity leader" of it: it pushes the parity to the
 *   (rebuilt) P1 and removes the replicas. Then P1 (non-leader) sees the parity
 *   while its iteration still holds the replicas and asks all its peers,
 *   including the real P0, to remove the replicas of the stripe. The real P0
 *   ends up with neither replica nor parity for the stripe.
 * - EC aggregation on the old P0 rank is blocked (DAOS_FORCE_EC_AGG_FAIL) until
 *   both parity shards are rebuilt, otherwise it aggregates right after its own
 *   exclusion (the pool "reclaim" property is not honored reliably on it) while
 *   the original P1 is still there, and has nothing left to do later. The real
 *   P0 is blocked the same way until the stale rank and P1 are done, so it
 *   cannot create the parity first.
 *   The EC_AGGREGATE handler runs on the same xstream as the EC agg ULT of P1,
 *   so it can only interleave when that ULT yields; the VOS recx iteration of
 *   P1 is snapshotted at probe time, so P1 must be in the middle of a pass over
 *   the dkey while the parity of a stripe it has not reached yet arrives. Each
 *   cell is written as many small extents so that the (non-leader) pass of P1
 *   is not much faster than the stale leader's, the dkey is long so the stale
 *   leader's pass takes many seconds, and P1 is made to rescan every 2 s.
 * - Then a data shard is excluded and rebuilt from P0 only (a parity shard is
 *   alive), so the rebuilt data shard misses the stripes.
 */
static void
rebuild_ec_agg_on_excluded_tgt(void **state)
{
	test_arg_t   *arg = *state;
	struct ioreq  req;
	daos_obj_id_t oid;
	daos_recx_t   recx;
	d_rank_t      rank[RAE_TGT_NR];
	d_rank_t      excl[3];
	daos_handle_t oh;
	const char   *dkey = "d_key_0";
	char          dkey1[16];
	char         *buf, *cbuf;
	uint32_t      p0, p1, data;
	uint64_t      hash;
	int           s, c, rc, bad = 0;

	print_message("BEGIN %s\n", __FUNCTION__);
	if (!test_runable(arg, 8))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	/* data cell @c of the stripe is EC shard (grp_start + c) % grp_size */
	hash = d_hash_murmur64((const unsigned char *)dkey, strlen(dkey), 5731);
	p0   = (hash % RAE_TGT_NR + RAE_DATA_NR) % RAE_TGT_NR;
	p1   = (hash % RAE_TGT_NR + RAE_DATA_NR + 1) % RAE_TGT_NR;
	/* another dkey with the same shard rotation, so the same P0/P1 shards */
	for (s = 1;; s++) {
		uint64_t hash1;

		snprintf(dkey1, sizeof(dkey1), "d_key_%d", s);
		hash1 = d_hash_murmur64((const unsigned char *)dkey1, strlen(dkey1), 5731);
		if (hash1 % RAE_TGT_NR == hash % RAE_TGT_NR)
			break;
	}

	buf  = malloc(RAE_CELL_SIZE);
	cbuf = malloc(RAE_CELL_SIZE);
	assert_non_null(buf);
	assert_non_null(cbuf);
	memset(buf, 'r', RAE_CELL_SIZE);

	if (arg->myrank == 0) {
		print_message("disable aggregation and write replicas (partial stripe updates, "
			      "%d stripes x %d cells x %d extents)\n",
			      RAE_STRIPE_NR, RAE_DATA_NR, RAE_EXT_NR);
		daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "disabled");
		rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
		assert_rc_equal(rc, 0);
		for (s = 0; s < RAE_STRIPE_NR; s++)
			for (c = 0; c < RAE_DATA_NR; c++)
				rae_write_cell(oh, dkey, s, c, RAE_EXT_NR, buf);

		rae_layout_get(arg, oid, rank);
		excl[0] = rank[p0];
		excl[1] = rank[p1];
		print_message("shard/rank: P0 %u/%u P1 %u/%u data %u/%u %u/%u\n", p0, rank[p0], p1,
			      rank[p1], (p0 + 2) % RAE_TGT_NR, rank[(p0 + 2) % RAE_TGT_NR],
			      (p0 + 3) % RAE_TGT_NR, rank[(p0 + 3) % RAE_TGT_NR]);
		/* no EC agg on the future stale rank until both parities are rebuilt */
		daos_debug_set_params(arg->group, excl[0], DMG_KEY_FAIL_LOC,
				      DAOS_FORCE_EC_AGG_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);
		/*
		 * The excluded ranks do not get the EC boundary IV refresh any more,
		 * they must have got one (sc_ec_agg_eph_valid) before being excluded,
		 * like in the field where the container exists long before the exclude.
		 */
		print_message("wait %d s for the EC boundary refresh to reach every rank\n",
			      RAE_EPH_WAIT);
		sleep(RAE_EPH_WAIT);
	}
	par_bcast(PAR_COMM_WORLD, excl, 2, PAR_INT, 0);

	print_message("exclude rank %u hosting P0\n", excl[0]);
	rebuild_pools_ranks(&arg, 1, &excl[0], 1, false);
	print_message("exclude rank %u hosting P1\n", excl[1]);
	rebuild_pools_ranks(&arg, 1, &excl[1], 1, false);

	if (arg->myrank == 0) {
		rae_layout_get(arg, oid, rank);
		assert_int_not_equal(rank[p0], excl[0]);
		assert_int_not_equal(rank[p0], excl[1]);
		assert_int_not_equal(rank[p1], excl[0]);
		assert_int_not_equal(rank[p1], excl[1]);
		/* exclude a data shard rank which is not the new P0 rank */
		data = (p0 + 2) % RAE_TGT_NR;
		if (rank[data] == rank[p0])
			data = (p0 + 3) % RAE_TGT_NR;
		assert_int_not_equal(rank[data], rank[p0]);
		excl[2] = rank[data];
		print_message("P0 rebuilt on rank %u, P1 on rank %u, data shard %u on rank %u\n",
			      rank[p0], rank[p1], data, excl[2]);

		/* P1 starts looping first, the real P0 stays blocked */
		daos_debug_set_params(arg->group, rank[p1], DMG_KEY_FAIL_LOC,
				      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_debug_set_params(arg->group, rank[p0], DMG_KEY_FAIL_LOC,
				      DAOS_FORCE_EC_AGG_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
		/*
		 * EC aggregation on the live ranks is paused during rebuild, including
		 * the "Reclaim" op which follows the exclude rebuild a few seconds later,
		 * and the aggregation ULT then sleeps for a while. The excluded rank is
		 * not paused (it does not get the rebuild scan broadcast). Make sure P1
		 * is really looping before the stale leader is let go.
		 */
		print_message("wait for the post-rebuild reclaim to finish\n");
		sleep(10);
		test_rebuild_wait(&arg, 1);
		/*
		 * P1 rescans the object only after a new write landed on its target
		 * (sc_ec_update_timestamp), keep writing to another dkey of the object
		 * so that P1 starts a new pass every 2 s while the stale leader works.
		 */
		print_message("write to dkey %s every second for %d s, let the excluded rank %u "
			      "aggregate after %d s\n",
			      dkey1, RAE_P1_WARM + RAE_AGG_WAIT, excl[0], RAE_P1_WARM);
		for (s = 0; s < RAE_P1_WARM + RAE_AGG_WAIT; s++) {
			if (s == RAE_P1_WARM) {
				/* now let the stale rank aggregate as the "parity leader" */
				daos_debug_set_params(arg->group, excl[0], DMG_KEY_FAIL_LOC,
						      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS, 0,
						      NULL);
			}
			rae_write_cell(oh, dkey1, s, 0, 1, buf);
			sleep(1);
		}

		print_message("unblock EC aggregation on the real P0 rank %u, wait %d s\n",
			      rank[p0], RAE_AGG_WAIT);
		daos_debug_set_params(arg->group, rank[p0], DMG_KEY_FAIL_LOC,
				      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS, 0, NULL);
		sleep(RAE_AGG_WAIT);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
		rc = daos_obj_close(oh, NULL);
		assert_rc_equal(rc, 0);
	}
	par_bcast(PAR_COMM_WORLD, &excl[2], 1, PAR_INT, 0);

	print_message("exclude rank %u hosting a data shard, it is rebuilt from P0\n", excl[2]);
	rebuild_pools_ranks(&arg, 1, &excl[2], 1, false);

	if (arg->myrank == 0) {
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
		for (s = 0; s < RAE_STRIPE_NR; s++)
			for (c = 0; c < RAE_DATA_NR; c++) {
				recx.rx_idx = (uint64_t)s * RAE_DATA_NR * RAE_CELL_SIZE +
					      (uint64_t)c * RAE_CELL_SIZE;
				recx.rx_nr = RAE_CELL_SIZE;
				memset(cbuf, 0, RAE_CELL_SIZE);
				lookup_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1, cbuf,
					     RAE_CELL_SIZE, &req);
				if (memcmp(cbuf, buf, RAE_CELL_SIZE) == 0)
					continue;
				bad++;
				print_message("BAD stripe %d cell %d shard %u: %s\n", s, c,
					      (p0 + 2 + c) % RAE_TGT_NR,
					      rae_is_zero(cbuf, RAE_CELL_SIZE) ? "HOLE(zeros)"
									       : "garbage");
			}
		ioreq_fini(&req);
		print_message("%d bad cells\n", bad);
	}
	par_barrier(PAR_COMM_WORLD);

	reintegrate_pools_ranks(&arg, 1, excl, 3, false);

	free(buf);
	free(cbuf);
	assert_int_equal(bad, 0);
	print_message("END %s\n", __FUNCTION__);
}

static const struct CMUnitTest rebuild_weekly_tests[] = {
    {"REBUILD_WEEKLY1: EC aggregation on an excluded target", rebuild_ec_agg_on_excluded_tgt,
     rebuild_ec_8nodes_small_cell_setup, test_teardown},
};

int
run_daos_rebuild_weekly_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_weekly_tests);
		sub_tests      = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS_Rebuild_Weekly", rebuild_weekly_tests,
				     ARRAY_SIZE(rebuild_weekly_tests), sub_tests, sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
