/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * <tested_file_path>src/utils/utils_rbtree.c</tested_file_path>
 * <tested_function>ocf_rb_tree_insert</tested_function>
 * <functions_to_leave>
 *  ocf_rb_tree_init
 *  ocf_rb_tree_update_parent
 *  ocf_rb_tree_update_children
 *  ocf_rb_tree_rotate_left
 *  ocf_rb_tree_rotate_right
 *  ocf_rb_tree_fix_violation
 *  ocf_rb_tree_fix_violation
 *  ocf_rb_tree_insert
 *  ocf_rb_tree_swap
 *  ocf_rb_tree_successor
 *  ocf_rb_tree_predecessor
 *  ocf_rb_tree_bst_replacement
 *  ocf_rb_tree_sibling
 *  ocf_rb_tree_fix_double_black
 *  ocf_rb_tree_remove
 *  ocf_rb_tree_can_update
 *  ocf_rb_tree_find
 * </functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

#include "utils_rbtree.h"

#include "utils/utils_rbtree.c/utils_rbtree_generated_wraps.c"

struct test_node {
	int val;
	struct ocf_rb_node tree;
};

static int test_cmp(struct ocf_rb_node *n1,
		struct ocf_rb_node *n2)
{
	struct test_node *t1 = container_of(n1,
			struct test_node, tree);
	struct test_node *t2 = container_of(n2,
			struct test_node, tree);

	if (t1->val > t2->val)
		return 1;

	if (t1->val < t2->val)
		return -1;

	return 0;
}

struct test_node nodes[] = {{.val=50}, {.val=25}, {.val=12}, {.val=6},
	{.val=3},  {.val=1},  {.val=37}, {.val=42},
	{.val=45}, {.val=47}, {.val=75}, {.val=87},
	{.val=92}, {.val=97}, {.val=99}, {.val=67},
	{.val=62}, {.val=57}, {.val=55}, {.val=299}};
size_t nodes_number = sizeof(nodes)/sizeof(nodes[0]);

#define get_node(__ptr) container_of(__ptr, struct test_node, tree)

void prepare(struct ocf_rb_tree *tree)
{
	int i;

	ocf_rb_tree_init(tree, test_cmp, NULL);

	for (i = 0; i < nodes_number; i++)
		ocf_rb_tree_insert(tree, &nodes[i].tree);
}

static void ocf_rb_tree_test01(void **state)
{
	struct ocf_rb_tree tree;
	int i;

	print_test_description("Find existing values in tree");

	prepare(&tree);

	for (i = 0; i < nodes_number; i++) {
		struct test_node *tmp_node = ocf_rb_tree_find(&tree, &nodes[i].tree);
		assert_int_equal(nodes[i].val, get_node(tmp_node)->val);
		assert_ptr_equal(&nodes[i], get_node(tmp_node));
	}
}

static void ocf_rb_tree_test02(void **state)
{
	struct ocf_rb_tree tree;
	int i;

	struct test_node n_a_nodes[] = {{.val=250}, {.val=-1}, {.val=130},
		{.val=330},  {.val=123},
		{.val=420}, {.val=456}};
	size_t n_a_nodes_number = sizeof(n_a_nodes)/sizeof(n_a_nodes[0]);

	print_test_description("Lookup for non existing values");

	prepare(&tree);

	for (i = 0; i < n_a_nodes_number; i++) {
		assert_null(ocf_rb_tree_find(&tree, &n_a_nodes[i].tree));
	}
}

static void ocf_rb_tree_test03(void **state)
{
	struct ocf_rb_tree tree;
	struct test_node *node_to_remove = &nodes[5];
	struct test_node *tmp_node;

	print_test_description("Check if value is in tree, remove it, and recheck");

	prepare(&tree);

	tmp_node = ocf_rb_tree_find(&tree, &node_to_remove->tree);

	assert_int_equal(node_to_remove->val, get_node(tmp_node)->val);
	assert_ptr_equal(node_to_remove, get_node(tmp_node));

	ocf_rb_tree_remove(&tree, &node_to_remove->tree);

	tmp_node = ocf_rb_tree_find(&tree, &node_to_remove->tree);
	assert_null(tmp_node);
}

static void ocf_rb_tree_test04(void **state)
{
	struct ocf_rb_tree tree;
	struct test_node *node_to_update;
	struct test_node new_node = {.val=49};

	print_test_description("Check if node can be updated without "
			"changing the tree");

	prepare(&tree);

	node_to_update = &nodes[9]; //.val=47
	new_node.val = 49;
	assert_true(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[1]; //.val=25
	new_node.val = 30;
	assert_true(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[1]; //.val=25
	new_node.val = 14;
	assert_true(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[10]; //.val=75
	new_node.val = 70;
	assert_true(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);
}

static void ocf_rb_tree_test05(void **state)
{
	struct ocf_rb_tree tree;
	struct test_node *node_to_update;
	struct test_node new_node = {.val=49};

	print_test_description("Verify if node can't be updated without "
			"changing the tree");

	prepare(&tree);

	node_to_update = &nodes[3]; //.val=6
	new_node.val = 13;
	assert_false(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[19]; //.val=299
	new_node.val = 2;
	assert_false(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[16]; //.val=62
	new_node.val = 50;
	assert_false(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);

	node_to_update = &nodes[5]; //.val=1
	new_node.val = 50;
	assert_false(
			ocf_rb_tree_can_update(&tree, &node_to_update->tree, &new_node.tree)
			);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_rb_tree_test01),
		cmocka_unit_test(ocf_rb_tree_test02),
		cmocka_unit_test(ocf_rb_tree_test03),
		cmocka_unit_test(ocf_rb_tree_test04),
		cmocka_unit_test(ocf_rb_tree_test05)
	};

	print_message("Unit tests for rb tree\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
