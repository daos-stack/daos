/*
 * <tested_file_path>src/metadata/metadata_collision.c</tested_file_path>
 * <tested_function>ocf_metadata_hash_func</tested_function>
 * <functions_to_leave>
 * 	ocf_metadata_get_hash
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
#include "metadata.h"
#include "../utils/utils_cache_line.h"

#include "metadata/metadata_collision.c/metadata_collision_generated_wraps.c"

static void metadata_hash_func_test01(void **state)
{
	struct ocf_cache *cache;
	bool wrap = false;
	ocf_cache_line_t i;
	ocf_cache_line_t hash_cur, hash_next;
	unsigned c;
	ocf_core_id_t core_ids[] = {0, 1, 2, 100, OCF_CORE_MAX};
	ocf_core_id_t core_id;

	print_test_description("Verify that hash function increments by 1 and generates"
				"collision after 'hash_table_entries' successive core lines");

	cache = test_malloc(sizeof(*cache));
	cache->device = test_malloc(sizeof(*cache->device));
	cache->device->hash_table_entries = 10;

	for (c = 0; c < sizeof(core_ids) / sizeof(core_ids[0]); c++) {
		core_id = core_ids[c];
		for (i = 0; i < cache->device->hash_table_entries + 1; i++) {
			hash_cur = ocf_metadata_hash_func(cache, i, core_id);
			hash_next = ocf_metadata_hash_func(cache, i + 1, core_id);
			/* make sure hash value is within expected range */
			assert(hash_cur < cache->device->hash_table_entries);
			assert(hash_next < cache->device->hash_table_entries);
			/* hash should either increment by 1 or overflow to 0 */
			assert(hash_next == (hash_cur + 1) % cache->device->hash_table_entries);
		}
	}

	test_free(cache->device);
	test_free(cache);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(metadata_hash_func_test01)
	};

	print_message("Unit test of src/metadata/metadata_collision.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
