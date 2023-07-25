/*
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for fault domain parsing
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include "../fault_domain.h"

#define DOM_LEN	(sizeof(struct d_fault_domain) / sizeof(uint32_t))

static uint32_t	test_compressed[] = {3, 0, 2, /* root */
				     2, 1, 2, /* first layer */
				     2, 2, 1,
				     1, 4, 1, /* second layer */
				     1, 5, 2,
				     1, 6, 1,
				     /* ranks */
				     0, 1, 2, 3};

#define TEST_NUM_DOMAINS	(6)
#define TEST_NUM_RANKS		(4)

static void
test_fd_tree_init(void **state)
{
	uint32_t		comp[DOM_LEN] = {1, 0, 0};
	struct d_fd_tree	tree = {0};
	uint32_t		i;

	/* bad inputs */
	assert_rc_equal(d_fd_tree_init(&tree, NULL, DOM_LEN), -DER_INVAL);

	assert_rc_equal(d_fd_tree_init(NULL, comp, DOM_LEN), -DER_INVAL);

	for (i = 0; i < DOM_LEN; i++) {
		assert_rc_equal(d_fd_tree_init(&tree, comp, i), -DER_INVAL);
	}

	/* success */
	assert_rc_equal(d_fd_tree_init(&tree, comp, DOM_LEN), 0);
}

static void
test_fd_tree_next_bad_input(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};

	assert_rc_equal(d_fd_tree_init(&tree, test_compressed,
				       ARRAY_SIZE(test_compressed)), 0);

	assert_rc_equal(d_fd_tree_next(&tree, NULL), -DER_INVAL);
	assert_rc_equal(d_fd_tree_next(NULL, &next), -DER_INVAL);

	/* uninitialized tree */
	tree.fdt_compressed = NULL;
	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_UNINIT);

	tree.fdt_compressed = test_compressed;
	tree.fdt_len = 0;
	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_UNINIT);
}

static void
expect_domains(struct d_fd_tree *tree, size_t num_domains, size_t *next_idx)
{
	struct d_fd_node	next = {0};
	size_t			i;
	size_t			exp_idx = *next_idx;

	for (i = 0; i < num_domains; i++) {
		print_message("Checking domain %lu\n", i);

		assert_rc_equal(d_fd_tree_next(tree, &next), 0);
		assert_int_equal(next.fdn_type, D_FD_NODE_TYPE_DOMAIN);
		assert_non_null(next.fdn_val.dom);
		assert_int_equal(next.fdn_val.dom->fd_level,
				 tree->fdt_compressed[exp_idx++]);
		assert_int_equal(next.fdn_val.dom->fd_id,
				 tree->fdt_compressed[exp_idx++]);
		assert_int_equal(next.fdn_val.dom->fd_children_nr,
				 tree->fdt_compressed[exp_idx++]);
	}

	*next_idx = exp_idx;
}

static void
expect_ranks(struct d_fd_tree *tree, size_t num_ranks, size_t *next_idx)
{
	struct d_fd_node	next = {0};
	size_t			i;
	size_t			exp_idx = *next_idx;

	for (i = 0; i < num_ranks; i++) {
		print_message("Checking rank %lu\n", i);

		assert_rc_equal(d_fd_tree_next(tree, &next), 0);
		assert_int_equal(next.fdn_type, D_FD_NODE_TYPE_RANK);
		assert_int_equal(next.fdn_val.rank,
				 tree->fdt_compressed[exp_idx++]);
	}

	*next_idx = exp_idx;
}

static void
test_fd_tree_next(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			exp_idx = 0;

	assert_rc_equal(d_fd_tree_init(&tree, test_compressed,
				       ARRAY_SIZE(test_compressed)), 0);

	expect_domains(&tree, TEST_NUM_DOMAINS, &exp_idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &exp_idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_NONEXIST);
}

static void
test_fd_tree_next_trunc_ranks(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t			idx = 0;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS + 1;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_domains(&tree, TEST_NUM_DOMAINS, &idx);
	expect_ranks(&tree, 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_trunc_domains(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t			idx = 0;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS -
	      (sizeof(struct d_fault_domain) / sizeof(uint32_t));
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_domains(&tree, TEST_NUM_DOMAINS - 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_trunc_domain_in_tuple(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t			idx = 0;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS - 1;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_domains(&tree, TEST_NUM_DOMAINS - 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_len_bigger_than_tree(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			exp_idx = 0;
	size_t			len;

	/* We can only detect this condition if the tree is well-formed */
	len = ARRAY_SIZE(test_compressed) + 25;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);

	expect_domains(&tree, TEST_NUM_DOMAINS, &exp_idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &exp_idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_NONEXIST);
}

static void
test_fd_tree_reset(void **state)
{
	struct d_fd_tree	tree = {0};
	size_t			idx = 0;

	/* Bad input */
	assert_rc_equal(d_fd_tree_reset(NULL), -DER_INVAL);
	assert_rc_equal(d_fd_tree_reset(&tree), -DER_UNINIT);

	/* Success */
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed,
				       ARRAY_SIZE(test_compressed)), 0);

	expect_domains(&tree, TEST_NUM_DOMAINS, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);

	assert_rc_equal(d_fd_tree_reset(&tree), 0);

	/* after reset, should be able to go through whole tree again */
	idx = 0;
	expect_domains(&tree, TEST_NUM_DOMAINS, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);
}

static void
test_fd_get_exp_num_domains(void **state)
{
	uint32_t result = 0;

	/* array too short for even a root node */
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN - 1, 0, &result),
			-DER_INVAL);

	/* not enough room in array for ranks */
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN, 1, &result),
			-DER_INVAL);

	/* remaining array isn't a multiple of the domain tuple length */
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 2 + 1, 0, &result),
			-DER_INVAL);
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 2 + 1, 2, &result),
			-DER_INVAL);

	/* success */
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN, 0, &result), 0);
	assert_int_equal(result, 1);

	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 2, 0, &result), 0);
	assert_int_equal(result, 2);

	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN + 5, 5, &result), 0);
	assert_int_equal(result, 1);

	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 4, DOM_LEN, &result),
			0);
	assert_int_equal(result, 3);
}

static void
test_fd_node_is_group(void **state)
{
	struct d_fd_node	dom_node = {0};
	struct d_fd_node	rank_node = {0};
	struct d_fault_domain	dom = {0};

	/* setup domain node */
	dom_node.fdn_type = D_FD_NODE_TYPE_DOMAIN;
	dom_node.fdn_val.dom = &dom;

	/* setup rank node */
	rank_node.fdn_type = D_FD_NODE_TYPE_RANK;

	/* null input */
	assert_false(d_fd_node_is_group(NULL));

	/* rank node */
	assert_false(d_fd_node_is_group(&rank_node));

	/* group level */
	dom.fd_level = D_FD_GROUP_DOMAIN_LEVEL;
	assert_true(d_fd_node_is_group(&dom_node));

	/* level higher than group */
	dom.fd_level = D_FD_GROUP_DOMAIN_LEVEL + 1;
	assert_false(d_fd_node_is_group(&dom_node));

	/* level lower than group */
	dom.fd_level = D_FD_GROUP_DOMAIN_LEVEL - 1;
	assert_false(d_fd_node_is_group(&dom_node));
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_fd_tree_init),
		cmocka_unit_test(test_fd_tree_next_bad_input),
		cmocka_unit_test(test_fd_tree_next),
		cmocka_unit_test(test_fd_tree_next_trunc_ranks),
		cmocka_unit_test(test_fd_tree_next_trunc_domains),
		cmocka_unit_test(test_fd_tree_next_trunc_domain_in_tuple),
		cmocka_unit_test(test_fd_tree_next_len_bigger_than_tree),
		cmocka_unit_test(test_fd_tree_reset),
		cmocka_unit_test(test_fd_get_exp_num_domains),
		cmocka_unit_test(test_fd_node_is_group),
	};

	return cmocka_run_group_tests_name("common_fault_domain",
					   tests, NULL, NULL);
}
