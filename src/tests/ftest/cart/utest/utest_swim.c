/*
 * (C) Copyright 2020-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT testing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>

#include <cmocka.h>

#include <cart/api.h>
#include "../cart/crt_internal.h"

static bool
rank_is_suspected(struct swim_context *ctx, swim_id_t id)
{
	struct swim_item *item;
	bool              found = false;

	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link)
	{
		if (item->si_id == id) {
			found = true;
			break;
		}
	}

	return found;
}

static void
test_setup(crt_context_t *crt_ctx)
{
	int rc;

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	assert_int_equal(rc, 0);

	rc = crt_context_create(crt_ctx);
	assert_int_equal(rc, 0);

	rc = crt_swim_init(0);
	assert_int_equal(rc, 0);

	rc = crt_rank_self_set(0, 1 /* group_version_min */);
	assert_int_equal(rc, 0);
}

static void
test_teardown(crt_context_t crt_ctx)
{
	int rc;

	crt_swim_fini();
	rc = crt_context_destroy(crt_ctx, 0);
	assert_int_equal(rc, 0);
	rc = crt_finalize();
	assert_int_equal(rc, 0);
}

static void
test_swim(void **state)
{
	crt_context_t crt_ctx;
	int           rc;

	test_setup(&crt_ctx);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1, d_hlc_get());
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 2, d_hlc_get());
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1, d_hlc_get());
	assert_int_equal(rc, -DER_ALREADY);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 0, d_hlc_get());
	assert_int_equal(rc, -DER_ALREADY);

	test_teardown(crt_ctx);
}

/*
 * Calling crt_swim_rank_check() on a suspected member with a newer incarnation
 * should mark the member ALIVE and remove it from the suspect list.
 */
static void
test_swim_rank_check_clears_suspect(void **state)
{
	struct crt_grp_priv      *grp_priv;
	struct crt_swim_membs    *csm;
	struct swim_context      *ctx;
	struct swim_member_update upd;
	crt_context_t             crt_ctx;
	uint64_t                  incarnation;
	int                       rc;

	test_setup(&crt_ctx);

	grp_priv = crt_grp_pub2priv(NULL);
	csm      = &grp_priv->gp_membs_swim;
	ctx      = csm->csm_ctx;

	/* Add the member to be suspected (rank 1) and a gossip source (rank 2). */
	incarnation = d_hlc_get();
	rc          = crt_swim_rank_add(grp_priv, 1, incarnation);
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(grp_priv, 2, d_hlc_get());
	assert_int_equal(rc, 0);

	/*
	 * Deliver a SUSPECT update about rank 1 from rank 2 (an alive, hence
	 * trustable, member) so that rank 1 is added to the suspect list.
	 */
	upd.smu_id                    = 1;
	upd.smu_state.sms_incarnation = incarnation;
	upd.smu_state.sms_status      = SWIM_MEMBER_SUSPECT;
	upd.smu_state.sms_delay       = 0;

	rc = swim_updates_parse(ctx, 2 /* from */, 1 /* id */, &upd, 1);
	assert_int_equal(rc, 0);
	assert_true(rank_is_suspected(ctx, 1));

	/*
	 * crt_swim_rank_check() with a newer incarnation must clear the
	 * suspicion of rank 1.
	 */
	rc = crt_swim_rank_check(grp_priv, 1, incarnation + 1);
	assert_int_equal(rc, 0);
	assert_false(rank_is_suspected(ctx, 1));

	test_teardown(crt_ctx);
}

static int
init_tests(void **state)
{
	unsigned int seed;

	/* Seed the random number generator once per test run */
	seed = (unsigned int)(time(NULL) & 0x0FFFFFFFFULL);
	fprintf(stdout, "Seeding this test run with seed=%u\n", seed);
	srand(seed);

	d_setenv("D_PROVIDER", "ofi+tcp", 1);
	d_setenv("D_INTERFACE", "lo", 1);

	return 0;
}

static int
fini_tests(void **state)
{
	return 0;
}

int main(int argc, char **argv)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_swim),
	    cmocka_unit_test(test_swim_rank_check_clears_suspect),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_swim", tests, init_tests,
		fini_tests);
}
