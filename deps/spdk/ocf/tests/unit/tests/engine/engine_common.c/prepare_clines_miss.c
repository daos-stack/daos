/*
 * <tested_file_path>src/engine/engine_common.c</tested_file_path>
 * <tested_function>ocf_prepare_clines_miss</tested_function>
 * <functions_to_leave>
 *    ocf_prepare_clines_evict
 *    ocf_engine_remap
 *    ocf_req_set_mapping_error
 *    ocf_req_test_mapping_error
 *    ocf_req_set_part_evict
 *    ocf_req_part_evict
 * </functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

#include "ocf/ocf.h"
#include "../ocf_priv.h"
#include "../ocf_cache_priv.h"
#include "../ocf_queue_priv.h"
#include "engine_common.h"
#include "engine_debug.h"
#include "../utils/utils_cache_line.h"
#include "../ocf_request.h"
#include "../utils/utils_cleaner.h"
#include "../utils/utils_user_part.h"
#include "../metadata/metadata.h"
#include "../ocf_space.h"
#include "../promotion/promotion.h"
#include "../concurrency/ocf_concurrency.h"

#include "engine/engine_common.c/prepare_clines_miss_generated_wraps.c"

struct ocf_cache_line_concurrency *__wrap_ocf_cache_line_concurrency(ocf_cache_t cache)
{
	return NULL;
}

void __wrap_ocf_req_hash_lock_upgrade(struct ocf_request *req)
{
}

void __wrap_ocf_req_hash_unlock_wr(struct ocf_request *req)
{
}

uint32_t __wrap_ocf_user_part_has_space(struct ocf_request *req)
{
	return mock();
}

int __wrap_lock_clines(struct ocf_request *req,
		const struct ocf_engine_callbacks *engine_cbs)
{
	function_called();
	return mock();
}

void __wrap_ocf_metadata_start_exclusive_access(
		struct ocf_metadata_lock *metadata_lock)
{
}

void __wrap_ocf_metadata_end_exclusive_access(
		struct ocf_metadata_lock *metadata_lock)
{
}

int __wrap_ocf_space_managment_remap_do(struct ocf_request *req)
{
	function_called();
	return mock();
}

static void ocf_prepare_clines_miss_test01(void **state)
{
	struct ocf_cache cache;
	struct ocf_request req = {.cache = &cache };

	print_test_description("Target part doesn't have enough space.\n");
	print_test_description("\tEviction success\n");

	will_return_always(__wrap_ocf_user_part_has_space, false);
	expect_function_call(__wrap_ocf_space_managment_remap_do);
	will_return_always(__wrap_ocf_space_managment_remap_do, LOOKUP_REMAPPED);

	ocf_prepare_clines_miss(&req);
	assert(!ocf_req_test_mapping_error(&req));
	assert(ocf_req_part_evict(&req));
}

static void ocf_prepare_clines_miss_test02(void **state)
{
	struct ocf_cache cache;
	struct ocf_request req = {.cache = &cache };

	print_test_description("Target part doesn't have enough space.\n");
	print_test_description("\tEviction failed\n");

	will_return_always(__wrap_ocf_user_part_has_space, false);

	expect_function_call(__wrap_ocf_space_managment_remap_do);
	will_return(__wrap_ocf_space_managment_remap_do, LOOKUP_MISS);

	ocf_prepare_clines_miss(&req);
	assert(ocf_req_test_mapping_error(&req));
	assert(ocf_req_part_evict(&req));
}

static void ocf_prepare_clines_miss_test03(void **state)
{
	struct ocf_cache cache;
	struct ocf_request req = {.cache = &cache };

	print_test_description("Target part has enough space.\n");
	print_test_description("\tEviction success\n");

	will_return_always(__wrap_ocf_user_part_has_space, true);
	expect_function_call(__wrap_ocf_space_managment_remap_do);
	will_return_always(__wrap_ocf_space_managment_remap_do, LOOKUP_REMAPPED);

	ocf_prepare_clines_miss(&req);
	assert(!ocf_req_test_mapping_error(&req));
	assert(!ocf_req_part_evict(&req));
}

static void ocf_prepare_clines_miss_test04(void **state)
{
	struct ocf_cache cache;
	struct ocf_request req = {.cache = &cache };

	print_test_description("Target part has enough space.\n");
	print_test_description("\tEviction failed\n");

	will_return_always(__wrap_ocf_user_part_has_space, true);

	expect_function_call(__wrap_ocf_space_managment_remap_do);
	will_return(__wrap_ocf_space_managment_remap_do, LOOKUP_MISS);

	ocf_prepare_clines_miss(&req);
	assert(ocf_req_test_mapping_error(&req));
	assert(!ocf_req_part_evict(&req));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_prepare_clines_miss_test01),
		cmocka_unit_test(ocf_prepare_clines_miss_test02),
		cmocka_unit_test(ocf_prepare_clines_miss_test03),
		cmocka_unit_test(ocf_prepare_clines_miss_test04),
	};

	print_message("Unit test for ocf_prepare_clines_miss\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
