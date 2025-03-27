/*
 * <tested_file_path>src/utils/utils_refcnt.c</tested_file_path>
 * <tested_function>ocf_refcnt_unfreeze</tested_function>
 * <functions_to_leave>
 * ocf_refcnt_init
 * ocf_refcnt_inc
 * ocf_refcnt_dec
 * ocf_refcnt_freeze
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

#include "utils/utils_refcnt.c/utils_refcnt_unfreeze_generated_wraps.c"

static void ocf_refcnt_unfreeze_test01(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Unfreeze decrements freeze counter");

	ocf_refcnt_init(&rc);

	ocf_refcnt_freeze(&rc);
	ocf_refcnt_freeze(&rc);
	val = env_atomic_read(&rc.freeze);

	ocf_refcnt_unfreeze(&rc);
	val2 = env_atomic_read(&rc.freeze);
	assert_int_equal(val2, val - 1);

	ocf_refcnt_unfreeze(&rc);
	val2 = env_atomic_read(&rc.freeze);
	assert_int_equal(val2, val - 2);

}

static void ocf_refcnt_unfreeze_test02(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Unfreezed counter can be incremented");

	ocf_refcnt_init(&rc);

	val = ocf_refcnt_inc(&rc);
	ocf_refcnt_freeze(&rc);
	ocf_refcnt_unfreeze(&rc);
	val2 = ocf_refcnt_inc(&rc);

	assert_int_equal(val2, val + 1);
}

static void ocf_refcnt_unfreeze_test03(void **state)
{
	struct ocf_refcnt rc;
	int val, val2;

	print_test_description("Two freezes require two unfreezes");

	ocf_refcnt_init(&rc);

	val = ocf_refcnt_inc(&rc);
	ocf_refcnt_freeze(&rc);
	ocf_refcnt_freeze(&rc);
	ocf_refcnt_unfreeze(&rc);
	val2 = ocf_refcnt_inc(&rc);

	assert_int_equal(0, val2);

	ocf_refcnt_unfreeze(&rc);
	val2 = ocf_refcnt_inc(&rc);

	assert_int_equal(val2, val + 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_refcnt_unfreeze_test01),
		cmocka_unit_test(ocf_refcnt_unfreeze_test02),
		cmocka_unit_test(ocf_refcnt_unfreeze_test03),
	};

	print_message("Unit test of src/utils/utils_refcnt.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
