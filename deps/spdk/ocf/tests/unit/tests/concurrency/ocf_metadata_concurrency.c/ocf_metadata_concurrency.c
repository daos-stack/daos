/*
 * <tested_file_path>src/concurrency/ocf_metadata_concurrency.c</tested_file_path>
 * <tested_function>ocf_hb_req_prot_lock_rd</tested_function>
 * <functions_to_leave>
 *	INSERT HERE LIST OF FUNCTIONS YOU WANT TO LEAVE
 *	ONE FUNCTION PER LINE
 * </functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

#include "ocf_metadata_concurrency.h"
#include "../metadata/metadata_misc.h"

#include "concurrency/ocf_metadata_concurrency.c/ocf_metadata_concurrency_generated_wraps.c"

void __wrap_ocf_hb_id_naked_lock(struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash, int rw)
{
	check_expected(hash);
	function_called();
}

#define MAP_SIZE 16

static struct ocf_request *alloc_req()
{
	struct ocf_request *req;
	struct ocf_cache *cache = test_malloc(sizeof(*cache));

	req = test_malloc(sizeof(*req) + MAP_SIZE * sizeof(req->map[0]));
	req->map = req->__map;
	req->cache = cache;

	return req;
}

static void free_req(struct ocf_request *req)
{
	test_free(req->cache);
	test_free(req);
}

static void _test_lock_order(struct ocf_request* req,
		unsigned hash[], unsigned hash_count,
		unsigned expected_call[], unsigned expected_call_count)
{
	unsigned i;

	req->core_line_count = hash_count;

	for (i = 0; i < hash_count; i++)
		req->map[i].hash = hash[i];

	for (i = 0; i < expected_call_count; i++) {
		expect_function_call(__wrap_ocf_hb_id_naked_lock);
		expect_value(__wrap_ocf_hb_id_naked_lock, hash, expected_call[i]);
	}

	 ocf_hb_req_prot_lock_rd(req);

}

static void ocf_hb_req_prot_lock_rd_test01(void **state)
{
	struct ocf_request *req = alloc_req();
	struct {
		struct {
			unsigned val[MAP_SIZE];
			unsigned count;
		} hash, expected_call;
	} test_cases[] = {
		{
			.hash = {.val = {2}, .count = 1},
			.expected_call = {.val = {2}, .count = 1}
		},
		{
			.hash = {.val = {2, 3, 4}, .count = 3},
			.expected_call = {.val = {2, 3, 4}, .count = 3}
		},
		{
			.hash = {.val = {2, 3, 4, 0}, .count = 4},
			.expected_call = {.val = {0, 2, 3, 4}, .count = 4}
		},
		{
			.hash = {.val = {2, 3, 4, 0, 1, 2, 3, 4, 0, 1}, .count = 10},
			.expected_call = {.val = {0, 1, 2, 3, 4}, .count = 5}
		},
		{
			.hash = {.val = {4, 0}, .count = 2},
			.expected_call = {.val = {0, 4}, .count = 2}
		},
		{
			.hash = {.val = {0, 1, 2, 3, 4, 0, 1}, .count = 7},
			.expected_call = {.val = {0, 1, 2, 3, 4}, .count = 5}
		},
			{
			.hash = {.val = {1, 2, 3, 4, 0, 1}, .count = 6},
			.expected_call = {.val = {0, 1, 2, 3, 4}, .count = 5}
		},
};
	const unsigned test_case_count = sizeof(test_cases) / sizeof(test_cases[0]);
	unsigned i;

	req->cache->metadata.lock.num_hash_entries = 5;

	print_test_description("Verify hash locking order\n");

	for (i = 0; i < test_case_count; i++) {
		_test_lock_order(req, test_cases[i].hash.val, test_cases[i].hash.count,
				 test_cases[i].expected_call.val, test_cases[i].expected_call.count);
	}

	free_req(req);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_hb_req_prot_lock_rd_test01)
	};

	print_message("Unit test for ocf_hb_req_prot_lock_rd\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
