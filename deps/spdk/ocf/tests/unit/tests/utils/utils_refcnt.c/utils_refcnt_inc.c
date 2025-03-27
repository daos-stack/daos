/*
 * <tested_file_path>src/utils/utils_refcnt.c</tested_file_path>
 * <tested_function>ocf_refcnt_inc</tested_function>
 * <functions_to_leave>
 * ocf_refcnt_init
 * ocf_refcnt_dec
 * </functions_to_leave>
 */

#undef static

#undef inline


#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "print_desc.h"

#include "../utils/utils_refcnt.h"

#include "utils/utils_refcnt.c/utils_refcnt_inc_generated_wraps.c"

static void ocf_refcnt_inc_test(void **state)
{
	struct ocf_refcnt rc;
	int val;

	print_test_description("Increment adds 1 and returns proper value");

	ocf_refcnt_init(&rc);

	val = ocf_refcnt_inc(&rc);
	assert_int_equal(1, val);
	assert_int_equal(1, env_atomic_read(&rc.counter));

	val = ocf_refcnt_inc(&rc);
	assert_int_equal(2, val);
	assert_int_equal(2, env_atomic_read(&rc.counter));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_refcnt_inc_test)
	};

	print_message("Unit test of src/utils/utils_refcnt.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
