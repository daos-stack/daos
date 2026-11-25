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
#include <limits.h>
#include <time.h>

#include <cmocka.h>

#include <cart/api.h>
#include "../cart/crt_internal.h"

struct swim_fini_test_data {
	int      reply_count;
	swim_id_t reply_from;
	swim_id_t reply_to;
	int      reply_rc;
	void    *reply_args;
};

static int
swim_fini_test_send_request(struct swim_context *ctx, swim_id_t id, swim_id_t to,
			    struct swim_member_update *upds, size_t nupds)
{
	(void)ctx;
	(void)id;
	(void)to;
	(void)upds;
	(void)nupds;
	return 0;
}

static int
swim_fini_test_send_reply(struct swim_context *ctx, swim_id_t from, swim_id_t to, int rc,
			  void *args)
{
	struct swim_fini_test_data *data = swim_data(ctx);

	data->reply_count++;
	data->reply_from = from;
	data->reply_to   = to;
	data->reply_rc   = rc;
	data->reply_args = args;
	return 0;
}

static swim_id_t
swim_fini_test_get_target(struct swim_context *ctx)
{
	(void)ctx;
	return SWIM_ID_INVALID;
}

static int
swim_fini_test_get_member_state(struct swim_context *ctx, swim_id_t id,
				struct swim_member_state *state)
{
	(void)ctx;
	(void)id;
	(void)state;
	return -DER_NONEXIST;
}

static int
swim_fini_test_set_member_state(struct swim_context *ctx, swim_id_t id,
				struct swim_member_state *state)
{
	(void)ctx;
	(void)id;
	(void)state;
	return 0;
}

static struct swim_ops swim_fini_test_ops = {
	.send_request     = swim_fini_test_send_request,
	.send_reply       = swim_fini_test_send_reply,
	.get_dping_target = swim_fini_test_get_target,
	.get_iping_target = swim_fini_test_get_target,
	.get_member_state = swim_fini_test_get_member_state,
	.set_member_state = swim_fini_test_set_member_state,
};

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

static void
progress_timeout_cb(const struct crt_cb_info *info)
{
	int *result = info->cci_arg;

	*result = info->cci_rc;
}

static void
test_nonblocking_progress_processes_abort(void **state)
{
	crt_endpoint_t       ep = {.ep_grp = NULL, .ep_rank = 0, .ep_tag = 0};
	struct crt_rpc_priv *rpc_priv;
	crt_context_t        crt_ctx;
	crt_rpc_t           *rpc;
	int                  result = 1;
	int                  rc;

	test_setup(&crt_ctx);
	assert_false(crt_gdata.cg_progress_legacy);

	rc = crt_req_create(crt_ctx, &ep, CRT_OPC_CTL_LS, &rpc);
	assert_int_equal(rc, 0);
	rpc_priv                  = container_of(rpc, struct crt_rpc_priv, crp_pub);
	rpc_priv->crp_complete_cb = progress_timeout_cb;
	rpc_priv->crp_arg         = &result;

	crt_rpc_lock(rpc_priv);
	rc = crt_context_req_track(rpc_priv);
	crt_rpc_unlock(rpc_priv);
	assert_int_equal(rc, CRT_REQ_TRACK_IN_INFLIGHQ);

	rc = crt_progress(crt_ctx, 0);
	assert_true(rc == 0 || rc == -DER_TIMEDOUT);
	assert_int_equal(result, 1);

	rc = crt_req_abort(rpc);
	assert_int_equal(rc, 0);
	rc = crt_progress(crt_ctx, 0);
	assert_true(rc == 0 || rc == -DER_TIMEDOUT);
	assert_int_equal(result, -DER_TIMEDOUT);

	test_teardown(crt_ctx);
}

static void
test_swim_fini_completes_pending_ipings(void **state)
{
	struct swim_fini_test_data data = {0};
	struct swim_context       *ctx;
	struct swim_item          *item;
	int                        args;
	int                        rc;

	ctx = swim_init(1, &swim_fini_test_ops, &data);
	assert_non_null(ctx);

	rc = swim_ipings_suspend(ctx, 2, 3, &args);
	assert_int_equal(rc, 0);

	item = TAILQ_FIRST(&ctx->sc_ipings);
	assert_non_null(item);
	item->u.si_deadline = d_time_ms(UINT_MAX);
	d_timeinc(&item->u.si_deadline, NSEC_PER_SEC);

	swim_fini(ctx);

	assert_int_equal(data.reply_count, 1);
	assert_int_equal(data.reply_from, 3);
	assert_int_equal(data.reply_to, 2);
	assert_int_equal(data.reply_rc, -DER_TIMEDOUT);
	assert_ptr_equal(data.reply_args, &args);
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
	d_setenv("D_PROGRESS_LEGACY", "0", 1);

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
	    cmocka_unit_test(test_nonblocking_progress_processes_abort),
	    cmocka_unit_test(test_swim_fini_completes_pending_ipings),
	    cmocka_unit_test(test_swim_rank_check_clears_suspect),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_swim", tests, init_tests,
		fini_tests);
}
