/*
 *<tested_file_path>src/mngt/ocf_mngt_cache.c</tested_file_path>
 *	<tested_function>ocf_mngt_cache_set_fallback_pt_error_threshold</tested_function>
 *	<functions_to_leave>
 *	INSERT HERE LIST OF FUNCTIONS YOU WANT TO LEAVE
 *	ONE FUNCTION PER LINE
 *</functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

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

#include "mngt/ocf_mngt_cache.c/ocf_mngt_cache_set_fallback_pt_error_threshold_generated_wraps.c"

int __wrap_ocf_log_raw(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, ...)
{
	function_called();
}

ocf_ctx_t __wrap_ocf_cache_get_ctx(ocf_cache_t cache)
{
	function_called();
}

int __wrap_ocf_mngt_cache_set_fallback_pt(ocf_cache_t cache)
{
	function_called();
}

static void ocf_mngt_cache_set_fallback_pt_error_threshold_test01(void **state)
{
	struct ocf_cache *cache;
	int new_threshold;
	int result;

	print_test_description("Appropriate error code on invalid threshold value");

	cache = test_malloc(sizeof(*cache));

	new_threshold = -1;

	result = ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(result, -OCF_ERR_INVAL);


	new_threshold = 10000001;

	result = ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(result, -OCF_ERR_INVAL);

	test_free(cache);
}

static void ocf_mngt_cache_set_fallback_pt_error_threshold_test02(void **state)
{
	struct ocf_cache *cache;
	int new_threshold;
	int old_threshold;

	print_test_description("Invalid new threshold value doesn't change current threshold");

	cache = test_malloc(sizeof(*cache));

	new_threshold = -1;
	old_threshold = cache->fallback_pt_error_threshold = 1000;

	ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, old_threshold);


	new_threshold = 10000001;
	old_threshold = cache->fallback_pt_error_threshold = 1000;

	ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, old_threshold);

	test_free(cache);
}

static void ocf_mngt_cache_set_fallback_pt_error_threshold_test03(void **state)
{
	struct ocf_cache *cache;
	int new_threshold, old_threshold;

	print_test_description("Setting new threshold value");

	cache = test_malloc(sizeof(*cache));

	new_threshold = 5000;
	old_threshold = cache->fallback_pt_error_threshold = 1000;

	ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, new_threshold);


	new_threshold = 1000000;
	old_threshold = cache->fallback_pt_error_threshold = 1000;

	ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, new_threshold);


	new_threshold = 0;
	old_threshold = cache->fallback_pt_error_threshold = 1000;

	ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, new_threshold);

	test_free(cache);
}

static void ocf_mngt_cache_set_fallback_pt_error_threshold_test04(void **state)
{
	struct ocf_cache *cache;
	int new_threshold;
	int result;

	print_test_description("Return appropriate value on success");

	cache = test_malloc(sizeof(*cache));

	new_threshold = 5000;

	result = ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(result, 0);


	new_threshold = 1000000;

	result = ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(cache->fallback_pt_error_threshold, new_threshold);


	new_threshold = 0;

	result = ocf_mngt_cache_set_fallback_pt_error_threshold(cache, new_threshold);

	assert_int_equal(result, 0);

	test_free(cache);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_mngt_cache_set_fallback_pt_error_threshold_test01),
		cmocka_unit_test(ocf_mngt_cache_set_fallback_pt_error_threshold_test02),
		cmocka_unit_test(ocf_mngt_cache_set_fallback_pt_error_threshold_test03),
		cmocka_unit_test(ocf_mngt_cache_set_fallback_pt_error_threshold_test04)
	};

	print_message("Unit test of src/mngt/ocf_mngt_cache.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
