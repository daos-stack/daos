/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_errno.h>
#include <daos/common.h>
#include "ddb.h"

int
ddb_vos_tests_run(void);

struct ddb_test_driver_arguments {
	bool dtda_create_vos_file;
};

static int
ddb_test_driver_arguments_parse(uint32_t argc, char **argv, struct ddb_test_driver_arguments *args)
{
	struct option program_options[] = {{"create_vos", optional_argument, NULL, 'c'}, {NULL}};
	int           index             = 0, opt;

	memset(args, 0, sizeof(*args));

	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "c", program_options, &index)) != -1) {
		switch (opt) {
		case 'c':
			args->dtda_create_vos_file = true;
			break;
		case '?':
			printf("'%c' is unknown\n", optopt);
			return -DER_INVAL;
		default:
			return -DER_INVAL;
		}
	}

	return 0;
}

static bool
char_in_tests(char a, char *str, uint32_t str_len)
{
	int i;

	if (strlen(str) == 0) /* if there is no filter, always return true */
		return true;
	for (i = 0; i < str_len; i++) {
		if (a == str[i])
			return true;
	}

	return false;
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
int
main(int argc, char *argv[])
{
	struct ddb_test_driver_arguments args = {0};
	int                              rc;

	rc = ddb_init();
	if (rc != 0)
		return -rc;

	ddb_test_driver_arguments_parse(argc, argv, &args);

	assert_false(args.dtda_create_vos_file);

#define RUN_TEST_SUIT(c, func)                                                                     \
	do {                                                                                       \
		if (char_in_tests(c, test_suites, ARRAY_SIZE(test_suites)))                        \
			rc += func();                                                              \
	} while (0)

	/* filtering suites and tests */
	char test_suites[] = "";
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
	cmocka_set_test_filter("*dtx_act_discard_invalid*");
#endif
	RUN_TEST_SUIT('c', ddb_vos_tests_run);

	ddb_fini();
	if (rc > 0)
		printf("%d test(s) failed!\n", rc);
	else
		printf("All tests successful!\n");
	return rc;
}
