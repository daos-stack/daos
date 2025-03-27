/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

//<tested_file_path>src/mngt/ocf_mngt_cache.c</tested_file_path>
//<tested_function>_cache_mngt_set_cache_mode</tested_function>

/*
<functions_to_leave>
ocf_mngt_cache_mode_has_lazy_write
</functions_to_leave>
*/

#undef static
#undef inline

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

/*
 * Headers from tested target.
 */
#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "../ocf_core_priv.h"
#include "../ocf_queue_priv.h"
#include "../metadata/metadata.h"
#include "../engine/cache_engine.h"
#include "../utils/utils_user_part.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_io.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_pipeline.h"
#include "../concurrency/ocf_concurrency.h"
#include "../ocf_lru.h"
#include "../ocf_ctx_priv.h"
#include "../cleaning/cleaning.h"

#include "mngt/ocf_mngt_cache.c/_cache_mngt_set_cache_mode_test_generated_wraps.c"
/*
 * Mocked functions
 */
bool __wrap_ocf_cache_mode_is_valid(ocf_cache_mode_t mode)
{
	function_called();
	return mock();
}

ocf_ctx_t __wrap_ocf_cache_get_ctx(ocf_cache_t cache)
{
	return cache->owner;
}

int __wrap_ocf_log_raw(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, ...)
{
	function_called();
	return mock();
}

int __wrap_ocf_mngt_cache_flush(ocf_cache_t cache, bool interruption)
{
	function_called();
	return mock();
}

bool __wrap_env_bit_test(int nr, const volatile unsigned long *addr)
{
}

void __wrap_env_atomic_set(env_atomic *a, int i)
{
	function_called();
}

int __wrap_env_atomic_read(const env_atomic *a)
{
	function_called();
	return mock();
}

int __wrap_ocf_mngt_cache_reset_fallback_pt_error_counter(ocf_cache_t cache)
{
	function_called();
	return mock();
}

void __wrap__cache_mngt_update_initial_dirty_clines(ocf_cache_t cache)
{
	function_called();
}

static void _cache_mngt_set_cache_mode_test01(void **state)
{
	ocf_cache_mode_t mode_old = -20;
	ocf_cache_mode_t mode_new = ocf_cache_mode_none;
	struct ocf_ctx ctx = {
		.logger = 0x1, /* Just not NULL, we don't care. */
	};
	struct ocf_superblock_config sb_config = {
		.cache_mode = mode_old,
	};
	struct ocf_cache *cache;
	int result;

	print_test_description("Invalid new mode produces appropirate error code");

	cache = test_malloc(sizeof(*cache));
	cache->owner = &ctx;
	cache->conf_meta = &sb_config;

	expect_function_call(__wrap_ocf_cache_mode_is_valid);
	will_return(__wrap_ocf_cache_mode_is_valid, 0);

	result = _cache_mngt_set_cache_mode(cache, mode_new);

	assert_int_equal(result, -OCF_ERR_INVAL);
	assert_int_equal(cache->conf_meta->cache_mode, mode_old);

	test_free(cache);
}

static void _cache_mngt_set_cache_mode_test02(void **state)
{
	ocf_cache_mode_t mode_old = ocf_cache_mode_wt;
	ocf_cache_mode_t mode_new = ocf_cache_mode_wt;
	struct ocf_ctx ctx = {
		.logger = 0x1, /* Just not NULL, we don't care. */
	};
	struct ocf_superblock_config sb_config = {
		.cache_mode = mode_old,
	};
	struct ocf_cache *cache;
	uint8_t flush = 0;
	int result;

	print_test_description("Attempt to set mode the same as previous");

	cache = test_malloc(sizeof(*cache));
	cache->owner = &ctx;
	cache->conf_meta = &sb_config;

	expect_function_call(__wrap_ocf_cache_mode_is_valid);
	will_return(__wrap_ocf_cache_mode_is_valid, 1);

	expect_function_call(__wrap_ocf_log_raw);
	will_return(__wrap_ocf_log_raw, 0);

	result = _cache_mngt_set_cache_mode(cache, mode_new);

	assert_int_equal(result, 0);
	assert_int_equal(cache->conf_meta->cache_mode, mode_old);

	test_free(cache);
}

static void _cache_mngt_set_cache_mode_test03(void **state)
{
	ocf_cache_mode_t mode_old = ocf_cache_mode_wb;
	ocf_cache_mode_t mode_new = ocf_cache_mode_wa;
	struct ocf_ctx ctx = {
		.logger = 0x1, /* Just not NULL, we don't care. */
	};
	struct ocf_superblock_config sb_config = {
		.cache_mode = mode_old,
	};
	struct ocf_cache *cache;
	int result;
	int i;

	print_test_description("Old cache mode is write back. "
		       "Setting new cache mode is succesfull");

	cache = test_malloc(sizeof(*cache));
	cache->owner = &ctx;
	cache->conf_meta = &sb_config;

	expect_function_call(__wrap_ocf_cache_mode_is_valid);
	will_return(__wrap_ocf_cache_mode_is_valid, 1);

	expect_function_call(__wrap__cache_mngt_update_initial_dirty_clines);

	expect_function_call(__wrap_ocf_log_raw);
	will_return(__wrap_ocf_log_raw, 0);

	result = _cache_mngt_set_cache_mode(cache, mode_new);

	assert_int_equal(result, 0);
	assert_int_equal(cache->conf_meta->cache_mode, mode_new);

	test_free(cache);
}

static void _cache_mngt_set_cache_mode_test04(void **state)
{
	ocf_cache_mode_t mode_old = ocf_cache_mode_wt;
	ocf_cache_mode_t mode_new = ocf_cache_mode_wa;
	struct ocf_ctx ctx = {
		.logger = 0x1, /* Just not NULL, we don't care. */
	};
	struct ocf_superblock_config sb_config = {
		.cache_mode = mode_old,
	};
	struct ocf_cache *cache;
	int result;
	int i;

	print_test_description("Mode changed successfully");

	cache = test_malloc(sizeof(*cache));
	cache->owner = &ctx;
	cache->conf_meta = &sb_config;

	expect_function_call(__wrap_ocf_cache_mode_is_valid);
	will_return(__wrap_ocf_cache_mode_is_valid, 1);

	expect_function_call(__wrap_ocf_log_raw);
	will_return(__wrap_ocf_log_raw, 0);

	result = _cache_mngt_set_cache_mode(cache, mode_new);

	assert_int_equal(result, 0);
	assert_int_equal(cache->conf_meta->cache_mode, mode_new);

	test_free(cache);
}

/*
 * Main function. It runs tests.
 */
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(_cache_mngt_set_cache_mode_test01),
		cmocka_unit_test(_cache_mngt_set_cache_mode_test02),
		cmocka_unit_test(_cache_mngt_set_cache_mode_test03),
		cmocka_unit_test(_cache_mngt_set_cache_mode_test04),
	};

	print_message("Unit test of _cache_mngt_set_cache_mode\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
