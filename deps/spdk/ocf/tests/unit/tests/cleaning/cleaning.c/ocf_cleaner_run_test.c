/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * This headers must be in test source file. It's important that cmocka.h is
 * last.
 */

#undef static
#undef inline

//<tested_file_path>src/cleaning/cleaning.c</tested_file_path>
//<tested_function>ocf_cleaner_run</tested_function>
//<functions_to_leave>
//ocf_cleaner_set_cmpl
//</functions_to_leave>


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

/*
 * Headers from tested target.
 */
#include "cleaning.h"
#include "alru.h"
#include "acp.h"
#include "../ocf_cache_priv.h"
#include "../ocf_ctx_priv.h"
#include "../mngt/ocf_mngt_common.h"
#include "../metadata/metadata.h"

#include "cleaning/cleaning.c/ocf_cleaner_run_test_generated_wraps.c"

/*
 * Mocked functions. Here we must deliver functions definitions which are not
 * in tested source file.
 */


int __wrap_ocf_cleaning_perform_cleaning(struct ocf_cache *cache, ocf_cleaner_end_t cmpl)
{
	function_called();
	return mock();
}


ocf_cache_t __wrap_ocf_cleaner_get_cache(ocf_cleaner_t c)
{
	function_called();
	return mock_ptr_type(struct ocf_cache*);
}

bool __wrap_ocf_mngt_cache_is_locked(ocf_cache_t cache)
{
	function_called();
	return mock();
}


int __wrap__ocf_cleaner_run_check_dirty_inactive(struct ocf_cache *cache)
{
	function_called();
	return mock();
}

void __wrap_ocf_cleaner_run_complete(ocf_cleaner_t cleaner, uint32_t interval)
{
	function_called();
}

int __wrap_env_bit_test(int nr, const void *addr)
{
	function_called();
	return mock();
}

int __wrap_ocf_mngt_cache_trylock(env_rwsem *s)
{
	function_called();
	return mock();
}

void __wrap_ocf_mngt_cache_unlock(env_rwsem *s)
{
	function_called();
}

static void cleaner_complete(ocf_cleaner_t cleaner, uint32_t interval)
{
	function_called();
}

/*
 * Tests of functions. Every test name must be written to tests array in main().
 * Declarations always look the same: static void test_name(void **state);
 */

static void ocf_cleaner_run_test01(void **state)
{
	struct ocf_cache *cache;
	ocf_part_id_t part_id;
	uint32_t io_queue;
	int result;

	print_test_description("Parts are ready for cleaning - should perform cleaning"
			" for each part");

	//Initialize needed structures.
	cache = test_malloc(sizeof(*cache));
	cache->conf_meta = test_malloc(sizeof(struct ocf_superblock_config));
	cache->conf_meta->cleaning_policy_type = ocf_cleaning_alru;


	expect_function_call(__wrap_ocf_cleaner_get_cache);
	will_return(__wrap_ocf_cleaner_get_cache, cache);

	expect_function_call(__wrap_env_bit_test);
	will_return(__wrap_env_bit_test, 1);

	expect_function_call(__wrap_ocf_mngt_cache_is_locked);
	will_return(__wrap_ocf_mngt_cache_is_locked, 0);

	expect_function_call(__wrap_ocf_mngt_cache_trylock);
	will_return(__wrap_ocf_mngt_cache_trylock, 0);

	expect_function_call(__wrap__ocf_cleaner_run_check_dirty_inactive);
	will_return(__wrap__ocf_cleaner_run_check_dirty_inactive, 0);

	expect_function_call(__wrap_ocf_cleaning_perform_cleaning);
	will_return(__wrap_ocf_cleaning_perform_cleaning, 0);

	ocf_cleaner_set_cmpl(&cache->cleaner, cleaner_complete);

	ocf_cleaner_run(&cache->cleaner, 0xdeadbeef);

	/* Release allocated memory if allocated with test_* functions */

	test_free(cache->conf_meta);
	test_free(cache);
}

/*
 * Main function. It runs tests.
 */
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_cleaner_run_test01)
	};

	print_message("Unit test of cleaning.c\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
