/*
 * <tested_file_path>src/ocf_lru.c</tested_file_path>
 * <tested_function>_lru_init</tested_function>
 * <functions_to_leave>
 * 	update_lru_head
 * 	update_lru_tail
 * 	update_lru_head_tail
 *      _lru_init
 * 	add_lru_head_nobalance
 * 	remove_lru_list_nobalance
 * 	remove_update_list
 * 	remove_update_ptrs
 * 	balance_lru_list
 * 	balance_update_last_hot
 * 	ocf_get_lru
 * </functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

#include "ocf_space.h"
#include "ocf_lru.h"
#include "ops.h"
#include "../utils/utils_cleaner.h"
#include "../utils/utils_cache_line.h"
#include "../concurrency/ocf_concurrency.h"
#include "../mngt/ocf_mngt_common.h"
#include "../engine/engine_zero.h"
#include "../ocf_request.h"

#include "ocf_lru.c/lru_generated_wraps.c"

#define META_COUNT 128

static struct ocf_lru_meta meta[META_COUNT];

struct ocf_cache_line_concurrency *__wrap_ocf_cache_line_concurrency(ocf_cache_t cache)
{
	return NULL;
}

struct ocf_lru_meta*
__wrap_ocf_metadata_get_lru(ocf_cache_t cache, ocf_cache_line_t line)
{
	assert (line < META_COUNT);
	return &meta[line];
}

static const unsigned end_marker = -1;

static void _lru_init_test01(void **state)
{
	struct ocf_lru_list l;

	print_test_description("test init\n");

	_lru_init(&l, true);

	assert_int_equal(l.num_hot, 0);
	assert_int_equal(l.num_nodes, 0);
	assert_int_equal(l.head, end_marker);
	assert_int_equal(l.tail, end_marker);
	assert_int_equal(l.last_hot, end_marker);

	assert_int_equal(1,1);
}

static void check_hot_elems(struct ocf_lru_list *l)
{
	unsigned i;
	unsigned curr = l->head;

	for (i = 0; i < l->num_hot; i++) {
		assert_int_equal(meta[curr].hot, 1);
		curr = meta[curr].next;
	}
	for (i = l->num_hot; i < l->num_nodes; i++) {
		assert_int_equal(meta[curr].hot, 0);
		curr = meta[curr].next;
	}
}

static void _lru_init_test02(void **state)
{
	struct ocf_lru_list l;
	unsigned i;

	memset(meta, 0, sizeof(meta));

	print_test_description("test add\n");

	_lru_init(&l, true);

	for (i = 1; i <= 8; i++)
	{
		add_lru_head_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
		assert_int_equal(l.num_hot, i / 2);
		assert_int_equal(l.num_nodes, i);
		assert_int_equal(l.head, i);
		assert_int_equal(l.tail, 1);
		assert_int_equal(l.last_hot, i < 2 ? end_marker :
				i - i / 2 + 1);
		check_hot_elems(&l);
	}
}

static void _lru_init_test03(void **state)
{
	struct ocf_lru_list l;
	unsigned i;

	memset(meta, 0, sizeof(meta));

	print_test_description("remove head\n");

	_lru_init(&l, true);

	for (i = 1; i <= 8; i++) {
		add_lru_head_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	for (i = 8; i >= 1; i--) {
		assert_int_equal(l.num_hot, i / 2);
		assert_int_equal(l.num_nodes, i);
		assert_int_equal(l.head, i);
		assert_int_equal(l.tail, 1);
		assert_int_equal(l.last_hot, i < 2 ? end_marker :
				i - i / 2 + 1);
		check_hot_elems(&l);

		remove_lru_list_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	assert_int_equal(l.num_hot, 0);
	assert_int_equal(l.num_nodes, 0);
	assert_int_equal(l.head, end_marker);
	assert_int_equal(l.tail, end_marker);
	assert_int_equal(l.last_hot, end_marker);
}

static void _lru_init_test04(void **state)
{
	struct ocf_lru_list l;
	unsigned i;

	memset(meta, 0, sizeof(meta));

	print_test_description("remove tail\n");

	_lru_init(&l, true);

	for (i = 1; i <= 8; i++) {
		add_lru_head_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	for (i = 8; i >= 1; i--) {
		assert_int_equal(l.num_hot, i / 2);
		assert_int_equal(l.num_nodes, i);
		assert_int_equal(l.head, 8);
		assert_int_equal(l.tail, 9 - i);
		assert_int_equal(l.last_hot, i < 2 ? end_marker :
				8 - i / 2 + 1);
		check_hot_elems(&l);

		remove_lru_list_nobalance(NULL, &l, 9 - i, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	assert_int_equal(l.num_hot, 0);
	assert_int_equal(l.num_nodes, 0);
	assert_int_equal(l.head, end_marker);
	assert_int_equal(l.tail, end_marker);
	assert_int_equal(l.last_hot, end_marker);
}

static void _lru_init_test05(void **state)
{
	struct ocf_lru_list l;
	unsigned i, j;
	bool present[9];
	unsigned count;

	memset(meta, 0, sizeof(meta));

	print_test_description("remove last hot\n");

	_lru_init(&l, true);

	for (i = 1; i <= 8; i++) {
		add_lru_head_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
		present[i] = true;
	}

	for (i = 8; i >= 3; i--) {
		assert_int_equal(l.num_hot, i / 2);
		assert_int_equal(l.num_nodes, i);
		assert_int_equal(l.head, 8);
		assert_int_equal(l.tail, 1);

		count = 0;
		j = 8;
		while (count < i / 2) {
			if (present[j])
				++count;
			--j;
		}

		assert_int_equal(l.last_hot, j + 1);
		check_hot_elems(&l);

		present[l.last_hot] = false;
		remove_lru_list_nobalance(NULL, &l, l.last_hot, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	assert_int_equal(l.num_hot, 1);
	assert_int_equal(l.num_nodes, 2);
	assert_int_equal(l.head, 2);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 2);
}

static void _lru_init_test06(void **state)
{
	struct ocf_lru_list l;
	unsigned i;
	unsigned count;

	memset(meta, 0, sizeof(meta));

	print_test_description("remove middle hot\n");

	_lru_init(&l, true);

	for (i = 1; i <= 8; i++) {
		add_lru_head_nobalance(NULL, &l, i, NULL);
		balance_lru_list(NULL, &l, NULL);
	}

	count = 8;

	remove_lru_list_nobalance(NULL, &l, 7, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 8);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 5);

	remove_lru_list_nobalance(NULL, &l, 6, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 8);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 4);

	remove_lru_list_nobalance(NULL, &l, 5, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 8);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 4);

	remove_lru_list_nobalance(NULL, &l, 4, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 8);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 3);

	remove_lru_list_nobalance(NULL, &l, 3, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 8);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 8);

	remove_lru_list_nobalance(NULL, &l, 8, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 2);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, 2);

	remove_lru_list_nobalance(NULL, &l, 2, NULL);
	balance_lru_list(NULL, &l, NULL);
	--count;
	assert_int_equal(l.num_hot, count / 2);
	assert_int_equal(l.num_nodes, count);
	assert_int_equal(l.head, 1);
	assert_int_equal(l.tail, 1);
	assert_int_equal(l.last_hot, end_marker);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(_lru_init_test01),
		cmocka_unit_test(_lru_init_test02),
		cmocka_unit_test(_lru_init_test03),
		cmocka_unit_test(_lru_init_test04),
		cmocka_unit_test(_lru_init_test05),
		cmocka_unit_test(_lru_init_test06)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
