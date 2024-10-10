/*
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cart/api.h>
#include <gurt/debug.h>

#include <stdio.h>
#include <stdlib.h>

#define NWIDTH 20

/*
 * Mimics hg_info utility to return list of protocols but uses crt APIs.
 */

static void
print_info(const char *info_string)
{
	struct crt_protocol_info *protocol_infos = NULL, *protocol_info;
	int                       rc;

	rc = crt_protocol_info_get(info_string, &protocol_infos);
	if (rc != DER_SUCCESS) {
		DL_ERROR(rc, "crt_protocol_info_get() failed");
		goto out;
	}
	if (protocol_infos == NULL) {
		D_ERROR("No protocol found for \"%s\"\n", info_string);
		rc = -DER_NOTSUPPORTED;
		goto out;
	}

	printf("--------------------------------------------------\n");
	printf("%-*s%*s%*s\n", 10, "Class", NWIDTH, "Protocol", NWIDTH, "Device");
	printf("--------------------------------------------------\n");
	for (protocol_info = protocol_infos; protocol_info != NULL;
	     protocol_info = protocol_info->next)
		printf("%-*s%*s%*s\n", 10, protocol_info->class_name, NWIDTH,
		       protocol_info->protocol_name, NWIDTH, protocol_info->device_name);

	crt_protocol_info_free(protocol_infos);

out:
	assert_true(rc == DER_SUCCESS);
}

static void
test_all(void **state)
{
	print_info(NULL);
}

static void
test_tcp(void **state)
{
	print_info("tcp");
}

static void
test_ofi_tcp(void **state)
{
	print_info("ofi+tcp");
}

static int
init_tests(void **state)
{
	return d_log_init();
}

static int
fini_tests(void **state)
{
	d_log_fini();

	return 0;
}

int
main(int argc, char *argv[])
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_all),
	    cmocka_unit_test(test_tcp),
	    cmocka_unit_test(test_ofi_tcp),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_protocol", tests, init_tests, fini_tests);
}
