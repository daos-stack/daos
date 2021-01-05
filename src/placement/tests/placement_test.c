/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <daos.h>
#include "place_obj_common.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>
#include <daos/tests_lib.h>


const char *s_opts = "he:f:v";
static int idx;
static struct option l_opts[] = {
	{"exclude", required_argument, NULL, 'e'},
	{"filter",  required_argument, NULL, 'f'},
	{"help",    no_argument,       NULL, 'h'},
	{"verbose", no_argument,       NULL, 'v'},
};

static bool
show_help(int argc, char *argv[])
{
	bool result = false;
	int t_optind = optind;
	int opt;

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case '?': /** If invalid option, show help */
		case 'h':
			result = true;
			break;
		default:
			break;
		}
	}
	optind = t_optind;

	return result;
}

static void
print_usage(char *name)
{
	print_message(
		"\n\nCOMMON TESTS\n==========================\n");
	print_message("%s -e|--exclude <TESTS>\n", name);
	print_message("%s -f|--filter <TESTS>\n", name);
	print_message("%s -h|--help\n", name);
	print_message("%s -v|--verbose\n", name);
}

int placement_tests_run(bool verbose);

int main(int argc, char *argv[])
{
	int	opt;
	char	filter[1024];
	bool	verbose = false;

	assert_success(daos_debug_init(DAOS_LOG_DEFAULT));

	if (show_help(argc, argv)) {
		print_usage(argv[0]);
		return 0;
	}

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			/** already handled */
			break;
		case 'v':
			verbose = true;
			break;
		case 'e':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
			cmocka_set_skip_filter(optarg);
#else
			D_PRINT("filter not enabled");
#endif
			break;
		case 'f':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */

		{
			/** Add wildcards for easier filtering */
			sprintf(filter, "*%s*", optarg);
			cmocka_set_test_filter(filter);
		}
#else
			D_PRINT("filter not enabled. %s not applied", filter);
#endif
			break;
		default:
			break;
		}
	}
	placement_tests_run(verbose);

	daos_debug_fini();

	return 0;
}
