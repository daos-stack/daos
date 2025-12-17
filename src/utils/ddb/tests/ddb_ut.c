/**
 * Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
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
ddb_vos_ut_run(void);

static int
ddb_test_driver_arguments_parse(uint32_t argc, char **argv)
{
	struct option program_options[] = {
	    {"filter", required_argument, NULL, 'f'},
	};
	int           index             = 0, opt;

	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "f:", program_options, &index)) != -1) {
		switch (opt) {
		case 'f':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
			cmocka_set_test_filter(optarg);
#else
			printf("Test filtering disabled at compile time:"
			       " feature not supported with this version of cmocka.\n");
#endif
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
	int                              rc;

	rc = ddb_init();
	if (rc != 0)
		return -rc;

	ddb_test_driver_arguments_parse(argc, argv);

#define RUN_TEST_SUIT(c, func)                                                                     \
	do {                                                                                       \
		if (char_in_tests(c, test_suites, ARRAY_SIZE(test_suites)))                        \
			rc += func();                                                              \
	} while (0)

	/* filtering suites and tests */
	char test_suites[] = "";
	RUN_TEST_SUIT('a', ddb_vos_ut_run);

	ddb_fini();
	if (rc > 0)
		printf("%d test(s) failed!\n", rc);
	else
		printf("All tests successful!\n");
	return rc;
}
