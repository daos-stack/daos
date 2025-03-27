/*
 * <tested_file_path>src/utils/utils_refcnt.c</tested_file_path>
 * <tested_function>ocf_refcnt_init</tested_function>
 * <functions_to_leave>
 *	INSERT HERE LIST OF FUNCTIONS YOU WANT TO LEAVE
 *	ONE FUNCTION PER LINE
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

#include "utils/utils_refcnt.c/utils_refcnt_init_generated_wraps.c"

static void ocf_refcnt_init_test(void **state)
{
	struct ocf_refcnt rc;

	print_test_description("Reference counter is properly initialized");

	env_atomic_set(&rc.counter, 1);
	env_atomic_set(&rc.freeze, 1);
	env_atomic_set(&rc.callback, 1);

	ocf_refcnt_init(&rc);

	assert_int_equal(0, env_atomic_read(&rc.counter));
	assert_int_equal(0, env_atomic_read(&rc.freeze));
	assert_int_equal(0, env_atomic_read(&rc.cb));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_refcnt_init_test)
	};

	print_message("Unit test of src/utils/utils_refcnt.c");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
