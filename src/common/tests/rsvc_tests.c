/*
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>
#include <daos/rsvc.h>

#define RANK_LIST(name, ...)                                                                       \
	d_rank_t      _##name##_array[] = {__VA_ARGS__};                                           \
	d_rank_list_t name = {.rl_nr = ARRAY_SIZE(_##name##_array), .rl_ranks = _##name##_array};

static d_rank_t
at(d_rank_list_t *ranks, int index)
{
	assert_true(index >= 0);
	assert_true(index < ranks->rl_nr);
	return ranks->rl_ranks[index];
}

static void
copy(struct rsvc_client *dst, struct rsvc_client *src)
{
	int rc;

	*dst = *src;

	rc = d_rank_list_dup(&dst->sc_ranks, src->sc_ranks);
	assert_rc_equal(rc, 0);
}

static void
assert_leader_equal(struct rsvc_client *x, struct rsvc_client *y)
{
	assert_int_equal(x->sc_leader_known, y->sc_leader_known);
	assert_int_equal(at(x->sc_ranks, x->sc_leader_index), at(y->sc_ranks, y->sc_leader_index));
	assert_int_equal(x->sc_leader_term, y->sc_leader_term);
	assert_int_equal(x->sc_leader_aliveness, y->sc_leader_aliveness);
}

static void
assert_leader_unknown(struct rsvc_client *x)
{
	assert_false(x->sc_leader_known);
	assert_int_equal(x->sc_leader_index, -1);
	assert_int_equal(x->sc_leader_term, -1);
	assert_int_equal(x->sc_leader_aliveness, 0);
}

static void
prepare(struct rsvc_client *client, d_rank_list_t *ranks, int leader_index)
{
	crt_endpoint_t   ep   = {.ep_grp = NULL, .ep_tag = 0};
	struct rsvc_hint hint = {.sh_flags = RSVC_HINT_VALID, .sh_term = 1};
	int              rc;

	rc = rsvc_client_init(client, ranks);
	assert_rc_equal(rc, 0);

	/* Simulate rsvc_client_choose to avoid randomization. */
	assert_true(0 <= leader_index && leader_index < ranks->rl_nr);
	client->sc_next = (leader_index + 1) % client->sc_ranks->rl_nr;
	ep.ep_rank      = client->sc_ranks->rl_ranks[leader_index];

	hint.sh_rank = ep.ep_rank;
	rc           = rsvc_client_complete_rpc(client, &ep, 0 /* rc_crt */, 0 /* rc_svc */, &hint);
	assert_rc_equal(rc, RSVC_CLIENT_PROCEED);
}

static bool
subtract_cb(d_rank_t rank, void *arg)
{
	d_rank_list_t *ranks_to_subtract = arg;

	return d_rank_list_find(ranks_to_subtract, rank, NULL);
}

static void
rsvc_test_subtract_below_leader(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0, 1, 2, 3, 4);
	RANK_LIST(ranks_to_subtract, 0);

	prepare(&client, &ranks, 2 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, client_tmp.sc_ranks->rl_nr - 1);
	assert_leader_equal(&client, &client_tmp);
	assert_int_equal(at(client.sc_ranks, client.sc_next),
			 at(client_tmp.sc_ranks, client_tmp.sc_next));
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

static void
rsvc_test_subtract_leader(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0, 1, 2, 3, 4);
	RANK_LIST(ranks_to_subtract, 2);

	prepare(&client, &ranks, 2 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, ranks.rl_nr - 1);
	assert_leader_unknown(&client);
	assert_int_equal(at(client.sc_ranks, client.sc_next),
			 at(client_tmp.sc_ranks, client_tmp.sc_next));
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

static void
rsvc_test_subtract_next(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0, 1, 2, 3, 4);
	RANK_LIST(ranks_to_subtract, 3);

	prepare(&client, &ranks, 2 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, client_tmp.sc_ranks->rl_nr - 1);
	assert_leader_equal(&client, &client_tmp);
	assert_int_equal(at(client.sc_ranks, client.sc_next), 4);
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

static void
rsvc_test_subtract_next_wrap(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0, 1, 2, 3, 4);
	RANK_LIST(ranks_to_subtract, 4);

	prepare(&client, &ranks, 3 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, client_tmp.sc_ranks->rl_nr - 1);
	assert_leader_equal(&client, &client_tmp);
	assert_int_equal(at(client.sc_ranks, client.sc_next), 0);
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

static void
rsvc_test_subtract_next_end_up_empty(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0);
	RANK_LIST(ranks_to_subtract, 0);

	prepare(&client, &ranks, 0 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, client_tmp.sc_ranks->rl_nr - 1);
	assert_leader_unknown(&client);
	assert_int_equal(client.sc_next, -1);
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

static void
rsvc_test_subtract_above_next(void **state)
{
	struct rsvc_client client;
	struct rsvc_client client_tmp;
	RANK_LIST(ranks, 0, 1, 2, 3, 4);
	RANK_LIST(ranks_to_subtract, 4);

	prepare(&client, &ranks, 2 /* leader_index */);

	copy(&client_tmp, &client);
	rsvc_client_subtract(&client, subtract_cb, &ranks_to_subtract);
	assert_int_equal(client.sc_ranks->rl_nr, client_tmp.sc_ranks->rl_nr - 1);
	assert_leader_equal(&client, &client_tmp);
	assert_int_equal(at(client.sc_ranks, client.sc_next),
			 at(client_tmp.sc_ranks, client_tmp.sc_next));
	rsvc_client_fini(&client_tmp);

	rsvc_client_fini(&client);
}

int
main(void)
{
	/* clang-format off */
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(rsvc_test_subtract_below_leader),
		cmocka_unit_test(rsvc_test_subtract_leader),
		cmocka_unit_test(rsvc_test_subtract_next),
		cmocka_unit_test(rsvc_test_subtract_next_wrap),
		cmocka_unit_test(rsvc_test_subtract_next_end_up_empty),
		cmocka_unit_test(rsvc_test_subtract_above_next)
	};
	/* clang-format on */

	return cmocka_run_group_tests(tests, NULL, NULL);
}
