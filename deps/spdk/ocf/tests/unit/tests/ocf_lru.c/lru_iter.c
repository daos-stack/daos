/*
 * <tested_file_path>src/ocf_lru.c</tested_file_path>
 * <tested_function>lru_iter_next</tested_function>
 * <functions_to_leave>
 *	INSERT HERE LIST OF FUNCTIONS YOU WANT TO LEAVE
 *	ONE FUNCTION PER LINE
 *  lru_iter_init
 *  lru_iter_cleaning_init
 *  _lru_next_lru
 *  _lru_lru_is_empty
 *  _lru_lru_set_empty
 *  _lru_lru_all_empty
 *  ocf_rotate_right
 *  ocf_get_lru
 *  lru_iter_eviction_next
 *  lru_iter_cleaning_next
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
#include "../utils/utils_cleaner.h"
#include "../utils/utils_cache_line.h"
#include "../concurrency/ocf_concurrency.h"
#include "../mngt/ocf_mngt_common.h"
#include "../engine/engine_zero.h"
#include "../ocf_request.h"

#include "ocf_lru.c/lru_iter_generated_wraps.c"

// #define DEBUG

struct ocf_cache_line_concurrency *__wrap_ocf_cache_line_concurrency(ocf_cache_t cache)
{
	return NULL;
}

ocf_cache_line_t test_cases[10 * OCF_NUM_LRU_LISTS][OCF_NUM_LRU_LISTS][20];
unsigned num_cases = 20;

void write_test_case_description(void)
{
	unsigned i, j, l;
	unsigned test_case = 0;

	// case 0 - all lists empty
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		test_cases[0][i][test_case] = -1;
	}

	// case 1 - all lists with single element
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		test_cases[0][i][test_case] = 10 * i;
		test_cases[1][i][test_case] = -1;
	}

	// case 2 - all lists have between 1 and 5 elements, increasingly
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = 1 + i / (OCF_NUM_LRU_LISTS / 4);

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 10 * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 3 - all lists have between 1 and 5 elements, modulo index
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = 1 + (i % 5);

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 10 * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 4 - all lists have between 0 and 4 elements, increasingly
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = i / (OCF_NUM_LRU_LISTS / 4);

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 10 * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 5 - all lists have between 0 and 4 elements, modulo index
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = (i % 5);

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 10 * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 6 - list length increasing by 1 from 0
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = i;

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = OCF_NUM_LRU_LISTS * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 7 - list length increasing by 1 from 1
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = i + 1;

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 2 * OCF_NUM_LRU_LISTS * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 8 - list length increasing by 4 from 0
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = 4 * i;

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 4 * OCF_NUM_LRU_LISTS * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// case 9 - list length increasing by 4 from 1
	test_case++;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
		unsigned num_elements = 4 * i + 1;

		for (j = 0; j < num_elements; j++)
			test_cases[j][i][test_case] = 5 * OCF_NUM_LRU_LISTS * i + j;
		test_cases[j][i][test_case] = -1;
	}

	// cases 10-19: cases 0-9 rotated right by 4
	l = test_case;
	test_case++;
	while(test_case < 2 * (l + 1)) {
		unsigned matching_case = test_case - l - 1;

		for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
			unsigned curr_list = (i + 4) % OCF_NUM_LRU_LISTS;
			j = 0;
			while(test_cases[j][i][matching_case] != -1) {
				test_cases[j][curr_list][test_case] =
						test_cases[j][i][matching_case];
				j++;
			}
			test_cases[j][curr_list][test_case] = -1;
		}
		test_case++;
	}

	/* transform cacheline numbers so that they remain unique but have
	 * assignment to list modulo OCF_NUM_LRU_LISTS */
	for (test_case = 0; test_case < num_cases; test_case++) {
		for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
			j = 0;
			while (test_cases[j][i][test_case] != -1) {
				test_cases[j][i][test_case] = test_cases[j][i][test_case] *
						OCF_NUM_LRU_LISTS + i;
				j++;
			}
		}
	}

#ifdef DEBUG
	static bool desc_printed = false;

	if (desc_printed)
		return;
	desc_printed = true;

	for (test_case = 0; test_case < num_cases; test_case++) {
		print_message("test case no %d\n", test_case);
		for (i = 0; i < OCF_NUM_LRU_LISTS; i++) {
			print_message("list %02u: ", i);
			j = 0;
			while (test_cases[j][i][test_case] != -1) {
				print_message("%u ", test_cases[j][i][test_case]);
				j++;
			}
			print_message("<EOL>\n");
		}
		print_message("========\n\n");
	}
#endif
}

unsigned current_case;

struct ocf_lru_list list;

struct ocf_lru_list *__wrap_ocf_lru_get_list(struct ocf_user_part *user_part,
		uint32_t lru, bool clean)
{
	unsigned i = 0;

	while (test_cases[i][lru][current_case] != -1)
		i++;

	if (i == 0) {
		list.head = -1;
		list.tail = -1;
		list.num_nodes = 0;
	} else {
		list.head = test_cases[0][lru][current_case];
		list.tail = test_cases[i - 1][lru][current_case];
		list.num_nodes = i;
	}

#ifdef DEBUG
	print_message("list for case %u lru %u: head: 0x%x tail 0x%x elems 0x%x\n",
		current_case, lru, list.head, list.tail, list.num_nodes);
#endif

	return &list;
}

inline struct ocf_lru_list *__wrap_lru_get_cline_list(ocf_cache_t cache,
		ocf_cache_line_t cline)
{
	return __wrap_ocf_lru_get_list(NULL, cline % OCF_NUM_LRU_LISTS, true);
}


struct ocf_lru_meta g_lru_meta;

struct ocf_lru_meta *__wrap_ocf_metadata_get_lru(
  struct ocf_cache *cache, ocf_cache_line_t line)
{
	unsigned i, j;

	for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
	{
		j = 0;

		while (test_cases[j][i][current_case] != -1) {
			if (test_cases[j][i][current_case] == line) {
				if (j == 0) {
					g_lru_meta.prev = -1;
				} else {
					g_lru_meta.prev =
						test_cases[j - 1][i][current_case];
				}

				g_lru_meta.next = test_cases[j + 1][i][current_case];
#ifdef DEBUG
				print_message("[%u] next 0x%x prev 0x%x\n",
						line, g_lru_meta.next,
						g_lru_meta.prev);
#endif
				return &g_lru_meta;
			}
			j++;
		}

	}

	print_message("use case %d cache line %d not found\n",
			current_case, line);
	assert(false);
}


void __wrap_add_lru_head(ocf_cache_t cache,
		struct ocf_lru_list *list,
		unsigned int collision_index)
{
	unsigned list_head = list->head;
	unsigned i, j = collision_index % OCF_NUM_LRU_LISTS;

	i = 1;
	while (test_cases[i][j][current_case] != -1)
		i++;

	test_cases[i+1][j][current_case] = -1;

	while (i--)
		test_cases[i + 1][j][current_case] = test_cases[i][j][current_case];

	test_cases[0][j][current_case] = collision_index;

#ifdef DEBUG
	print_message("case %u lru %u  head set to  %u\n", current_case, j, collision_index);
#endif
}


void __wrap_remove_lru_list(ocf_cache_t cache,
		struct ocf_lru_list *list,
		unsigned int collision_index)
{
	bool found;
	unsigned i, j;

	found = false;
	for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
	{
		j = 0;

		while (test_cases[j][i][current_case] != -1) {
			if (!found && test_cases[j][i][current_case] == collision_index) {
				assert_int_equal(test_cases[0][i][current_case], list->head);
				found = true;
			}
			if (found)
				test_cases[j][i][current_case] = test_cases[j+1][i][current_case];
			j++;
		}

		if (found)
			break;
	}

	assert(found);

#ifdef DEBUG
	print_message("case %u removed  %u from lru  %u\n", current_case, collision_index, i);
#endif
}

bool __wrap__lru_lock(struct ocf_lru_iter *iter,
		ocf_cache_line_t cache_line,
		ocf_core_id_t *core_id, uint64_t *core_line)
{
	return true;
}

bool __wrap_ocf_cache_line_try_lock_rd(struct ocf_cache_line_concurrency *c,
		ocf_cache_line_t line)
{
	return true;
}

bool __wrap_ocf_cache_line_try_lock_wr(struct ocf_cache_line_concurrency *c,
		ocf_cache_line_t line)
{
	return false;
}
static void _lru_run_test(unsigned test_case)
{
	unsigned start_pos;
	current_case = test_case;

	for (start_pos = 0; start_pos < OCF_NUM_LRU_LISTS; start_pos++)
	{
		struct ocf_lru_iter iter;
		ocf_cache_line_t cache_line, expected_cache_line;
		unsigned curr_lru = start_pos;
		unsigned pos[OCF_NUM_LRU_LISTS];
		unsigned i;

		write_test_case_description();

		for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
		{
			pos[i] = -1;
			while(test_cases[pos[i] + 1][i][test_case] != -1)
				pos[i]++;
		}

		lru_iter_cleaning_init(&iter, NULL, NULL, start_pos);

		do {
			/* check what is expected to be returned from iterator */
			if (pos[curr_lru] == -1) {
				i = 1;
				while (i < OCF_NUM_LRU_LISTS &&
					pos[(curr_lru + i) % OCF_NUM_LRU_LISTS]
						== -1) {
					i++;
				}
				if (i == OCF_NUM_LRU_LISTS) {
					/* reached end of lists */
					expected_cache_line = -1;
				} else {
					curr_lru = (curr_lru + i) % OCF_NUM_LRU_LISTS;
					expected_cache_line = test_cases[pos[curr_lru]]
							[curr_lru][test_case];
					pos[curr_lru]--;
				}
			} else {
				expected_cache_line = test_cases[pos[curr_lru]]
						[curr_lru][test_case];
				pos[curr_lru]--;
			}

			/* get cacheline from iterator */
			cache_line = lru_iter_cleaning_next(&iter);

#ifdef DEBUG
			if (cache_line == expected_cache_line) {
				print_message("case %u cline 0x%x ok\n",
						test_case, cache_line);
			} else {
				print_message("case %u cline 0x%x NOK expected 0x%x\n",
						test_case, cache_line, expected_cache_line);
			}
#endif
			assert_int_equal(cache_line, expected_cache_line);

			curr_lru = (curr_lru + 1) % OCF_NUM_LRU_LISTS;
		} while (cache_line != -1);

		/* make sure all cachelines are visited */
		for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
		{
			assert_int_equal((unsigned)-1, pos[i]);
		}
	}
}

static void lru_iter_next_test00(void **state)
{
	print_test_description("lru iter test case 00\n");
	_lru_run_test(0);
	return;
}

static void lru_iter_next_test01(void **state)
{
	print_test_description("lru iter test case 01\n");
	_lru_run_test(1);
	return;
}

static void lru_iter_next_test02(void **state)
{
	print_test_description("lru iter test case 02\n");
	_lru_run_test(2);
	return;
}

static void lru_iter_next_test03(void **state)
{
	print_test_description("lru iter test case 03\n");
	_lru_run_test(3);
	return;
}

static void lru_iter_next_test04(void **state)
{
	print_test_description("lru iter test case 04\n");
	_lru_run_test(4);
	return;
}

static void lru_iter_next_test05(void **state)
{

	print_test_description("lru iter test case 05\n");
	_lru_run_test(5);
	return;
}

static void lru_iter_next_test06(void **state)
{
	print_test_description("lru iter test case 06\n");
	_lru_run_test(6);
	return;
}

static void lru_iter_next_test07(void **state)
{
	print_test_description("lru iter test case 07\n");
	_lru_run_test(7);
	return;
}

static void lru_iter_next_test08(void **state)
{
	print_test_description("lru iter test case 08\n");
	_lru_run_test(8);
	return;
}

static void lru_iter_next_test09(void **state)
{
	print_test_description("lru iter test case 09\n");
	_lru_run_test(9);
	return;
}

static void lru_iter_next_test10(void **state)
{
	print_test_description("lru iter test case 00\n");
	_lru_run_test(10);
	return;
}

static void lru_iter_next_test11(void **state)
{
	print_test_description("lru iter test case 11\n");
	_lru_run_test(11);
	return;
}

static void lru_iter_next_test12(void **state)
{
	print_test_description("lru iter test case 12\n");
	_lru_run_test(12);
	return;
}

static void lru_iter_next_test13(void **state)
{
	print_test_description("lru iter test case 13\n");
	_lru_run_test(13);
	return;
}

static void lru_iter_next_test14(void **state)
{
	print_test_description("lru iter test case 14\n");
	_lru_run_test(14);
	return;
}

static void lru_iter_next_test15(void **state)
{
	print_test_description("lru iter test case 15\n");
	_lru_run_test(15);
	return;
}

static void lru_iter_next_test16(void **state)
{
	print_test_description("lru iter test case 16\n");
	_lru_run_test(16);
	return;
}

static void lru_iter_next_test17(void **state)
{
	print_test_description("lru iter test case 17\n");
	_lru_run_test(17);
	return;
}

static void lru_iter_next_test18(void **state)
{
	print_test_description("lru iter test case 18\n");
	_lru_run_test(18);
	return;
}

static void lru_iter_next_test19(void **state)
{
	print_test_description("lru iter test case 19\n");
	_lru_run_test(19);
	return;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(lru_iter_next_test00),
		cmocka_unit_test(lru_iter_next_test01),
		cmocka_unit_test(lru_iter_next_test02),
		cmocka_unit_test(lru_iter_next_test03),
		cmocka_unit_test(lru_iter_next_test04),
		cmocka_unit_test(lru_iter_next_test05),
		cmocka_unit_test(lru_iter_next_test06),
		cmocka_unit_test(lru_iter_next_test07),
		cmocka_unit_test(lru_iter_next_test08),
		cmocka_unit_test(lru_iter_next_test09),
		cmocka_unit_test(lru_iter_next_test10),
		cmocka_unit_test(lru_iter_next_test11),
		cmocka_unit_test(lru_iter_next_test12),
		cmocka_unit_test(lru_iter_next_test13),
		cmocka_unit_test(lru_iter_next_test14),
		cmocka_unit_test(lru_iter_next_test15),
		cmocka_unit_test(lru_iter_next_test16),
		cmocka_unit_test(lru_iter_next_test17),
		cmocka_unit_test(lru_iter_next_test18),
		cmocka_unit_test(lru_iter_next_test19)
	};

	print_message("Unit test for lru_iter_next\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
