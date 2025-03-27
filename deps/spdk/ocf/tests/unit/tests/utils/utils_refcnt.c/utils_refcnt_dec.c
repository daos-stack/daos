/*
 * <tested_file_path>src/utils/utils_refcnt.c</tested_file_path>
 * <tested_function>ocf_refcnt_dec</tested_function>
 * <functions_to_leave>
 * ocf_refcnt_init
 * ocf_refcnt_inc
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

#include "utils/utils_refcnt.c/utils_refcnt_dec_generated_wraps.c"

static void ocf_refcnt_dec_test01(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Decrement subtracts 1 and returns proper value");

	ocf_refcnt_init(&rc);

	ocf_refcnt_inc(&rc);
	ocf_refcnt_inc(&rc);
	ocf_refcnt_inc(&rc);

	val = ocf_refcnt_dec(&rc);
	assert_int_equal(2, val);
	val2 = env_atomic_read(&rc.counter);
	assert_int_equal(2, val2);

	val = ocf_refcnt_dec(&rc);
	assert_int_equal(1, val);
	val2 = env_atomic_read(&rc.counter);
	assert_int_equal(1, val2);

	val = ocf_refcnt_dec(&rc);
	assert_int_equal(0, val);
	val2 = env_atomic_read(&rc.counter);
	assert_int_equal(0, val2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_refcnt_dec_test01)
	};

	print_message("Unit test of src/utils/utils_refcnt.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
