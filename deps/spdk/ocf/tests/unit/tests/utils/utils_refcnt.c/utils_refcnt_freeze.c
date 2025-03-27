/*
 * <tested_file_path>src/utils/utils_refcnt.c</tested_file_path>
 * <tested_function>ocf_refcnt_freeze</tested_function>
 * <functions_to_leave>
 * ocf_refcnt_init
 * ocf_refcnt_inc
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

#include "utils/utils_refcnt.c/utils_refcnt_freeze_generated_wraps.c"

static void ocf_refcnt_freeze_test01(void **state)
{
	struct ocf_refcnt rc;
	int val;

	print_test_description("Freeze increments freeze counter");

	ocf_refcnt_init(&rc);

	ocf_refcnt_freeze(&rc);
	assert_int_equal(1, env_atomic_read(&rc.freeze));

	ocf_refcnt_freeze(&rc);
	assert_int_equal(2, env_atomic_read(&rc.freeze));
}

static void ocf_refcnt_freeze_test02(void **state)
{
	struct ocf_refcnt rc;
	int val;

	print_test_description("Increment returns 0 for frozen counter");

	ocf_refcnt_init(&rc);

	ocf_refcnt_inc(&rc);
	ocf_refcnt_inc(&rc);
	ocf_refcnt_inc(&rc);

	ocf_refcnt_freeze(&rc);

	val = ocf_refcnt_inc(&rc);

	assert_int_equal(0, val);
}

static void ocf_refcnt_freeze_test03(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Freeze bocks increment");

	ocf_refcnt_init(&rc);

	val = ocf_refcnt_inc(&rc);
	val = ocf_refcnt_inc(&rc);
	val = ocf_refcnt_inc(&rc);

	ocf_refcnt_freeze(&rc);

	ocf_refcnt_inc(&rc);

	val2 = env_atomic_read(&rc.counter);

	assert_int_equal(val, val2);
}

static void ocf_refcnt_freeze_test04(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Freeze allows decrement");

	ocf_refcnt_init(&rc);

	val = ocf_refcnt_inc(&rc);
	val = ocf_refcnt_inc(&rc);
	val = ocf_refcnt_inc(&rc);

	ocf_refcnt_freeze(&rc);

	val2 = ocf_refcnt_dec(&rc);
	assert_int_equal(val2, val - 1);

	val2 = ocf_refcnt_dec(&rc);
	assert_int_equal(val2, val - 2);
}
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_refcnt_freeze_test01),
		cmocka_unit_test(ocf_refcnt_freeze_test02),
		cmocka_unit_test(ocf_refcnt_freeze_test03),
		cmocka_unit_test(ocf_refcnt_freeze_test04),
	};

	print_message("Unit test of src/utils/utils_refcnt.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
