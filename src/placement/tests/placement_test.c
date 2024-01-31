/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 */

#include <daos.h>
#include "place_obj_common.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>
#include <daos/tests_lib.h>


const char *s_opts = "he:f:vpmdn:l:o";
static int idx;
static struct option l_opts[] = {
	{"exclude", required_argument, NULL, 'e'},
	{"filter",  required_argument, NULL, 'f'},
	{"nlvl",    no_argument,	NULL, 'o'},
	{"help",    no_argument,       NULL, 'h'},
	{"verbose", no_argument,       NULL, 'v'},
	{"pda",     no_argument,       NULL, 'p'},
	{"pda_layout", no_argument,      NULL, 'm'},
	{"distribute", no_argument,    NULL, 'd'},
	{"num_objs", required_argument, NULL, 'n'},
	{"obj_class", required_argument, NULL, 'l'},
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
	print_message("%s -p|--pda <TESTS>\n", name);
	print_message("%s -m|--pda_layout <TESTS>\n", name);
	print_message("%s -d|--distribut [-n num_objs] [-l obj_class] <TESTS>\n", name);
	print_message("%s -o|--nlvl failure domain as node, engine by default\n", name);
	print_message("%s -h|--help\n", name);
	print_message("%s -v|--verbose\n", name);
}

int main(int argc, char *argv[])
{
	int		opt;
	char		filter[1024];
	bool		pda_test = false;
	bool		pda_layout = false;
	bool		dist_test = false;
	bool		verbose = false;
	uint32_t	num_objs = 0;
	int		obj_class = 0;

	assert_success(daos_debug_init(DAOS_LOG_DEFAULT));

	if (show_help(argc, argv)) {
		print_usage(argv[0]);
		return 0;
	}

	fail_domain_node = false;
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
			sprintf(filter, "*%s*", optarg);
			D_PRINT("filter not enabled. %s not applied", filter);
#endif
			break;
		case 'p':
			pda_test = true;
			break;
		case 'm':
			pda_layout = true;
			break;
		case 'd':
			dist_test = true;
			break;
		case 'n':
			num_objs = atoi(optarg);
			break;
		case 'l':
			obj_class = daos_oclass_name2id(optarg);
			if (obj_class == OC_UNKNOWN) {
				D_ERROR("invalid obj class %s\n", optarg);
				return -1;
			}
		case 'o':
			fail_domain_node = true;
			D_PRINT("run test as node failure domain");
			break;
		default:
			break;
		}
	}
	if (pda_layout)
		pda_layout_run(verbose);
	else if (pda_test)
		pda_tests_run(verbose);
	else if (dist_test)
		dist_tests_run(verbose, num_objs, obj_class);
	else
		placement_tests_run(verbose);

	daos_debug_fini();

	return 0;
}
