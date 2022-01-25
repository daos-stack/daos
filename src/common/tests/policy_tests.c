/*
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/tests_lib.h>
#include <daos_srv/policy.h>
#include <daos/common.h>

static void
reset_policy_desc(struct policy_desc_t *desc)
{
	int i;

	desc->policy = 0;
	for (i = 0; i < DAOS_MEDIA_POLICY_PARAMS_MAX; i++)
		desc->params[i] = 0;
}

bool
are_policy_descs_equal(const struct policy_desc_t *left,
		       const struct policy_desc_t *right)
{
	int i;

	if (left->policy != right->policy)
		return false;

	for (i = 0; i < DAOS_MEDIA_POLICY_PARAMS_MAX; i++)
		if (left->params[i] != right->params[i])
			return false;

	return true;
}

static void
test_policy_positive(void **state)
{
	const char *str = "type=io_size/th1=512/th2=4096";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_IO_SIZE;
	exp_out.params[0] = 512;
	exp_out.params[1] = 4096;

	assert_true(result);
	assert_true(are_policy_descs_equal(&out, &exp_out));

}

static void
test_policy_negative(void **state)
{
	const char *str = "type=unknown/th1=512/th2=4096";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	assert_false(result);

}

static void
test_policy_type_only(void **state)
{
	const char *str = "type=write_intensivity";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_WRITE_INTENSIVITY;

	assert_true(result);
	assert_true(are_policy_descs_equal(&out, &exp_out));

}

static void
test_policy_no_type(void **state)
{
	const char *str = "th1=6";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_WRITE_INTENSIVITY;

	assert_false(result);
}

static void
test_policy_blank(void **state)
{
	const char *str = "";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_WRITE_INTENSIVITY;

	assert_false(result);

}

static void
test_policy_junk(void **state)
{
	const char *str = "dfgj=jaosdfhg/asdg=2346/wgdsh=25";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_WRITE_INTENSIVITY;

	assert_false(result);

}


static void
test_policy_too_many_params(void **state)
{
	const char *str = "type=io_size/th1=512/th2=4096/th3=666/th4=42/th5=6";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	assert_false(result);

}

static void
test_policy_bad_param(void **state)
{
	const char *str = "type=io_size/th1=asdf/th2=4096";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	assert_false(result);
}

static void
test_policy_bad_chars(void **state)
{
	const char *str = "Q$=%,*%#^*($^&RGFH";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	assert_false(result);
}

static void
test_policy_type_not_first(void **state)
{
	const char *str = "th1=512/th2=4096/type=io_size";
	struct policy_desc_t out, exp_out;
	bool result = false;

	reset_policy_desc(&out);
	reset_policy_desc(&exp_out);
	result = daos_policy_try_parse(str, &out);

	exp_out.policy = DAOS_MEDIA_POLICY_IO_SIZE;
	exp_out.params[0] = 512;
	exp_out.params[1] = 4096;

	assert_false(result);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_policy_positive),
		cmocka_unit_test(test_policy_negative),
		cmocka_unit_test(test_policy_type_only),
		cmocka_unit_test(test_policy_no_type),
		cmocka_unit_test(test_policy_blank),
		cmocka_unit_test(test_policy_junk),
		cmocka_unit_test(test_policy_too_many_params),
		cmocka_unit_test(test_policy_bad_param),
		cmocka_unit_test(test_policy_bad_chars),
		cmocka_unit_test(test_policy_type_not_first),
	};

	return cmocka_run_group_tests_name("policy_tests", tests, NULL, NULL);
}
