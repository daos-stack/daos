/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/*
<tested_file_path>src/cleaning/alru.c</tested_file_path>
<tested_function>cleaning_policy_alru_initialize_part</tested_function>
<functions_to_leave>
</functions_to_leave>
*/

#undef static
#undef inline
/*
 * This headers must be in test source file. It's important that cmocka.h is
 * last.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

/*
 * Headers from tested target.
 */
#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "cleaning.h"
#include "alru.h"
#include "../metadata/metadata.h"
#include "../utils/utils_cleaner.h"
#include "../utils/utils_user_part.h"
#include "../utils/utils_realloc.h"
#include "../concurrency/ocf_cache_line_concurrency.h"
#include "../ocf_def_priv.h"

#include "cleaning/alru.c/cleaning_policy_alru_initialize_part_test_generated_wraps.c"


static void cleaning_policy_alru_initialize_test01(void **state)
{
	int result;
	struct ocf_cache *cache;
	ocf_part_id_t part_id = 0;

	int collision_table_entries = 900729;

	print_test_description("Check if all variables are set correctly");

	cache = test_malloc(sizeof(*cache));
	cache->user_parts[part_id].part.runtime = test_malloc(sizeof(struct ocf_part_runtime));
	cache->user_parts[part_id].clean_pol = test_malloc(sizeof(*cache->user_parts[part_id].clean_pol));
	cache->user_parts[part_id].part.id = part_id;
	cache->device = test_malloc(sizeof(struct ocf_cache_device));
	cache->device->runtime_meta = test_malloc(sizeof(struct ocf_superblock_runtime));

	cache->device->collision_table_entries = collision_table_entries;

	result = cleaning_policy_alru_initialize_part(cache, &cache->user_parts[part_id], 1, 1);

	assert_int_equal(result, 0);

	assert_int_equal(env_atomic_read(&cache->user_parts[part_id].clean_pol->policy.alru.size), 0);
	assert_int_equal(cache->user_parts[part_id].clean_pol->policy.alru.lru_head, collision_table_entries);
	assert_int_equal(cache->user_parts[part_id].clean_pol->policy.alru.lru_tail, collision_table_entries);

	assert_int_equal(cache->device->runtime_meta->cleaning_thread_access, 0);

	test_free(cache->device->runtime_meta);
	test_free(cache->device);
	test_free(cache->user_parts[part_id].clean_pol);
	test_free(cache->user_parts[part_id].part.runtime);
	test_free(cache);
}

static void cleaning_policy_alru_initialize_test02(void **state)
{
	int result;
	struct ocf_cache *cache;
	ocf_part_id_t part_id = 0;

	uint32_t collision_table_entries = 900729;

	print_test_description("Check if only appropirate variables are changed");

	cache = test_malloc(sizeof(*cache));
	cache->user_parts[part_id].part.runtime = test_malloc(sizeof(struct ocf_part_runtime));
	cache->user_parts[part_id].clean_pol = test_malloc(sizeof(*cache->user_parts[part_id].clean_pol));
	cache->device = test_malloc(sizeof(struct ocf_cache_device));
	cache->device->runtime_meta = test_malloc(sizeof(struct ocf_superblock_runtime));

	env_atomic_set(&cache->user_parts[part_id].clean_pol->policy.alru.size, 1);
	cache->user_parts[part_id].clean_pol->policy.alru.lru_head = -collision_table_entries;
	cache->user_parts[part_id].clean_pol->policy.alru.lru_tail = -collision_table_entries;

	result = cleaning_policy_alru_initialize_part(cache, &cache->user_parts[part_id], 0, 0);

	assert_int_equal(result, 0);

	assert_int_equal(env_atomic_read(&cache->user_parts[part_id].clean_pol->policy.alru.size), 1);
	assert_int_equal(cache->user_parts[part_id].clean_pol->policy.alru.lru_head, -collision_table_entries);
	assert_int_equal(cache->user_parts[part_id].clean_pol->policy.alru.lru_tail, -collision_table_entries);

	assert_int_equal(cache->device->runtime_meta->cleaning_thread_access, 0);

	test_free(cache->device->runtime_meta);
	test_free(cache->device);
	test_free(cache->user_parts[part_id].clean_pol);
	test_free(cache->user_parts[part_id].part.runtime);
	test_free(cache);
}

/*
 * Main function. It runs tests.
 */
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(cleaning_policy_alru_initialize_test01),
		cmocka_unit_test(cleaning_policy_alru_initialize_test02)
	};

	print_message("Unit test of alru.c\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}



