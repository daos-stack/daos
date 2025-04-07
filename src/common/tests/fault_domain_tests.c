/*
 * (C) Copyright 2021-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

#define MD_LEN                  1 /* length of metadata */
#define DOM_LEN                 (sizeof(struct d_fault_domain) / sizeof(uint32_t))
#define MIN_LEN                 (DOM_LEN + MD_LEN)
#define START_IDX               MD_LEN /* start looking for domains after the metadata */

/* clang-format off */
static uint32_t test_compressed[] = {D_FD_TREE_HAS_PERF_DOMAIN, /* metadata */
				     3, 1, 2, /* root */
				     2, 2, 2, /* perf dom */
				     2, 3, 1, /* perf dom */
				     1, 4, 1, /* node */
				     1, 5, 2, /* node */
				     1, 6, 1, /* node */
				     /* ranks */
				     0, 1, 2, 3};
/* clang-format on */

#define TEST_NUM_DOMAINS	(6)
#define TEST_NUM_NODES          (3)
#define TEST_NUM_PERF           (TEST_NUM_DOMAINS - 1 - TEST_NUM_NODES)
#define TEST_NUM_RANKS		(4)

static void
test_fd_tree_init(void **state)
{
	uint32_t                comp[MIN_LEN] = {0, 1, 1, 0};
	struct d_fd_tree	tree = {0};
	uint32_t		i;

	/* bad inputs */
	assert_rc_equal(d_fd_tree_init(&tree, NULL, MIN_LEN), -DER_INVAL);

	assert_rc_equal(d_fd_tree_init(NULL, comp, MIN_LEN), -DER_INVAL);

	for (i = 0; i < MIN_LEN; i++) {
		assert_rc_equal(d_fd_tree_init(&tree, comp, i), -DER_INVAL);
	}

	/* success */
	assert_rc_equal(d_fd_tree_init(&tree, comp, MIN_LEN), 0);
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
expect_domains(struct d_fd_tree *tree, enum d_fd_node_type node_type, size_t num_domains,
	       size_t *next_idx)
{
	struct d_fd_node	next = {0};
	size_t			i;
	size_t			exp_idx = *next_idx;

	for (i = 0; i < num_domains; i++) {
		print_message("Checking domain %lu, node type=%s (%d)\n", i,
			      d_fd_get_node_type_str(node_type), node_type);

		assert_rc_equal(d_fd_tree_next(tree, &next), 0);
		assert_int_equal(next.fdn_type, node_type);
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
expect_root(struct d_fd_tree *tree, size_t *next_idx)
{
	expect_domains(tree, D_FD_NODE_TYPE_ROOT, 1, next_idx);
}

static void
expect_perf_doms(struct d_fd_tree *tree, size_t num_domains, size_t *next_idx)
{
	expect_domains(tree, D_FD_NODE_TYPE_PERF_DOM, num_domains, next_idx);
}

static void
expect_nodes(struct d_fd_tree *tree, size_t num_domains, size_t *next_idx)
{
	expect_domains(tree, D_FD_NODE_TYPE_NODE, num_domains, next_idx);
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
	size_t                  idx  = START_IDX;

	assert_rc_equal(d_fd_tree_init(&tree, test_compressed,
				       ARRAY_SIZE(test_compressed)), 0);

	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_NONEXIST);
}

static void
test_fd_tree_next_trunc_ranks(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t                  idx = START_IDX;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS + 1;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES, &idx);
	expect_ranks(&tree, 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_trunc_domains(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t                  idx = START_IDX;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS -
	      (sizeof(struct d_fault_domain) / sizeof(uint32_t));
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES - 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_trunc_domain_in_tuple(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t			len;
	size_t                  idx = START_IDX;

	len = ARRAY_SIZE(test_compressed) - TEST_NUM_RANKS - 1;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);
	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES - 1, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_TRUNC);
}

static void
test_fd_tree_next_len_bigger_than_tree(void **state)
{
	struct d_fd_tree	tree = {0};
	struct d_fd_node	next = {0};
	size_t                  idx  = START_IDX;
	size_t			len;

	/* We can only detect this condition if the tree is well-formed */
	len = ARRAY_SIZE(test_compressed) + 25;
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed, len), 0);

	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);

	assert_rc_equal(d_fd_tree_next(&tree, &next), -DER_NONEXIST);
}

static void
test_fd_tree_reset(void **state)
{
	struct d_fd_tree	tree = {0};
	size_t                  idx  = START_IDX;

	/* Bad input */
	assert_rc_equal(d_fd_tree_reset(NULL), -DER_INVAL);
	assert_rc_equal(d_fd_tree_reset(&tree), -DER_UNINIT);

	/* Success */
	assert_rc_equal(d_fd_tree_init(&tree, test_compressed,
				       ARRAY_SIZE(test_compressed)), 0);

	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);

	assert_rc_equal(d_fd_tree_reset(&tree), 0);

	/* after reset, should be able to go through whole tree again */
	idx = START_IDX;
	expect_root(&tree, &idx);
	expect_perf_doms(&tree, TEST_NUM_PERF, &idx);
	expect_nodes(&tree, TEST_NUM_NODES, &idx);
	expect_ranks(&tree, TEST_NUM_RANKS, &idx);
}

static void
test_fd_get_exp_num_domains(void **state)
{
	uint32_t result = 0;

	/* array too short for even a root node */
	assert_rc_equal(d_fd_get_exp_num_domains(MIN_LEN - 1, 0, &result), -DER_INVAL);

	/* not enough room in array for ranks */
	assert_rc_equal(d_fd_get_exp_num_domains(MIN_LEN, 1, &result), -DER_INVAL);

	/* remaining array after metadata isn't a multiple of the domain tuple length */
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 2, 0, &result), -DER_INVAL);
	assert_rc_equal(d_fd_get_exp_num_domains(DOM_LEN * 2 + 3, 3, &result), -DER_INVAL);

	/* success */
	assert_rc_equal(d_fd_get_exp_num_domains(MIN_LEN, 0, &result), 0);
	assert_int_equal(result, 1);

	assert_rc_equal(d_fd_get_exp_num_domains(MD_LEN + DOM_LEN * 2, 0, &result), 0);
	assert_int_equal(result, 2);

	assert_rc_equal(d_fd_get_exp_num_domains(MD_LEN + DOM_LEN + 5, 5, &result), 0);
	assert_int_equal(result, 1);

	assert_rc_equal(d_fd_get_exp_num_domains(MD_LEN + DOM_LEN * 4, DOM_LEN, &result), 0);
	assert_int_equal(result, 3);
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
	};

	return cmocka_run_group_tests_name("common_fault_domain",
					   tests, NULL, NULL);
}
