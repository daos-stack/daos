/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_test
 */
#define D_LOGFAC	DD_FAC(tests)
#include <getopt.h>
#include "daos_test.h"

/**
 * Tests can be run by specifying the appropriate argument for a test or
 * all will be run if no test is specified. Tests will be run in order
 * so tests that kill nodes must be last.
 */
#define TESTS "mpcetTViADKCoRvSXbOzZUdrNbBIPG"

/**
 * These tests will only be run if explicitly specified. They don't get
 * run if no test is specified.
 */
#define EXPLICIT_TESTS "x"
static const char *all_tests = TESTS;
static const char *all_tests_defined = TESTS EXPLICIT_TESTS;

enum {
	CHECKSUM_ARG_VAL_TYPE		= 0x2713,
	CHECKSUM_ARG_VAL_CHUNKSIZE	= 0x2714,
	CHECKSUM_ARG_VAL_SERVERVERIFY	= 0x2715,
};


static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDAOS TESTS\n=============================\n");
	print_message("Tests: Use one of these arg(s) for specific test\n");
	print_message("daos_test -m|--mgmt\n");
	print_message("daos_test -p|--pool\n");
	print_message("daos_test -c|--cont\n");
	print_message("daos_test -C|--capa\n");
	print_message("daos_test -U|--dedup\n");
	print_message("daos_test -z|--checksum\n");
	print_message("daos_test -Z|--agg_ec\n");
	print_message("daos_test -t|--base_dtx\n");
	print_message("daos_test -T|--dist_dtx\n");
	print_message("daos_test -i|--io\n");
	print_message("daos_test -I|--ec_io\n");
	print_message("daos_test -x|--epoch_io\n");
	print_message("daos_test -A|--obj_array\n");
	print_message("daos_test -D|--array\n");
	print_message("daos_test -K|--daos_kv\n");
	print_message("daos_test -d|--degraded\n");
	print_message("daos_test -e|--epoch\n");
	print_message("daos_test -o|--erecov\n");
	print_message("daos_test -V|--verify\n");
	print_message("daos_test -R|--mdr\n");
	print_message("daos_test -O|--oid_alloc\n");
	print_message("daos_test -r|--rebuild\n");
	print_message("daos_test -v|--rebuild_simple\n");
	print_message("daos_test -S|--rebuild_ec\n");
	print_message("daos_test -X|--degrade_ec\n");
	print_message("daos_test -b|--drain_simple\n");
	print_message("daos_test -B|--extend_simple\n");
	print_message("daos_test -N|--nvme_recovery\n");
	print_message("daos_test -P|--daos_pipeline\n");
	print_message("daos_test -G|--upgrade\n");
	print_message("daos_test -a|--all\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
	print_message("Options: Use one of these arg(s) to modify the "
			"tests that are run\n");
	print_message("daos_test -g|--group GROUP\n");
	print_message("daos_test -s|--svcn NSVCREPLICAS\n");
	print_message("daos_test -E|--exclude TESTS\n");
	print_message("daos_test -f|--filter TESTS\n");
	print_message("daos_test -h|--help\n");
	print_message("daos_test -u|--subtests\n");
	print_message("daos_test -n|--dmg_config\n");
	print_message("daos_test --csum_type CSUM_TYPE\n");
	print_message("daos_test --csum_cs CHUNKSIZE\n");
	print_message("daos_test --csum_sv\n");
	print_message("\n=============================\n");
}

static int
run_specified_tests(const char *tests, int rank, int size,
		    int *sub_tests, int sub_tests_size)
{
	int nr_failed = 0;

	if (strlen(tests) == 0)
		tests = all_tests;

	while (*tests != '\0') {
		switch (*tests) {
		case 'm':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS management tests..");
			daos_test_print(rank, "=====================");
			nr_failed = run_daos_mgmt_test(rank, size, sub_tests,
						       sub_tests_size);
			break;
		case 'p':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS pool tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_daos_pool_test(rank, size, sub_tests, sub_tests_size);
			break;
		case 'c':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS container tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_cont_test(rank, size,
							   sub_tests,
							   sub_tests_size);
			break;
		case 'C':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS capability tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_capa_test(rank, size);
			break;
		case 't':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "Single RDG TX test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_base_tx_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'T':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "Distributed TX tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_dist_tx_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'i':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS IO test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_io_test(rank, size, sub_tests,
						      sub_tests_size);
			break;
		case 'I':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS EC IO test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_ec_io_test(rank, size, sub_tests,
						      sub_tests_size);
			break;
		case 'z':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS checksum tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_checksum_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'Z':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS EC aggregation tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_aggregation_ec_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'U':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS dedup tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_dedup_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'x':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Epoch IO test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_epoch_io_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'A':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Object Array test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_obj_array_test(rank, size);
			break;
		case 'D':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS 1-D Array test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_array_test(rank, size, sub_tests, sub_tests_size);
			break;
		case 'K':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Flat KV test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_kv_test(rank, size);
			break;
		case 'e':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Epoch tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_epoch_test(rank, size);
			break;
		case 'o':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Epoch recovery tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_epoch_recovery_test(rank, size);
			break;
		case 'V':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS verify consistency..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_vc_test(rank, size, sub_tests,
						      sub_tests_size);
			break;
		case 'R':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS MD replication tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_md_replication_test(rank, size);
			break;
		case 'O':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS OID Allocator tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_oid_alloc_test(rank, size);
			break;
		case 'd':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS degraded-mode tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_degraded_test(rank, size);
			break;
		case 'r':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS rebuild tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_rebuild_test(rank, size,
							   sub_tests,
							   sub_tests_size);
			break;
		case 'N':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS NVMe recovery tests..");
			daos_test_print(rank, "==================");
			nr_failed += run_daos_nvme_recov_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'v':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS rebuild simple tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_rebuild_simple_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'b':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS drain simple tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_drain_simple_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'B':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS extend simple tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_extend_simple_test(rank, size,
						sub_tests, sub_tests_size);
			break;
		case 'S':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS rebuild ec tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_rebuild_simple_ec_test(rank, size,
								     sub_tests,
								sub_tests_size);
			break;
		case 'X':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS degrade ec tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_degrade_simple_ec_test(rank, size,
								     sub_tests,
								sub_tests_size);
			break;
		case 'P':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Pipeline tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_pipeline_test(rank, size);
			break;
		case 'G':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS upgrade tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_upgrade_test(rank, size, sub_tests, sub_tests_size);
			break;
		default:
			D_ASSERT(0);
		}

		tests++;
	}

	return nr_failed;
}

int
main(int argc, char **argv)
{
	test_arg_t	*arg;
	char		 tests[64];
	char		*sub_tests_str = NULL;
	char		*exclude_str = NULL;
	int		 sub_tests[1024];
	int		 sub_tests_idx = 0;
	int		 ntests = 0;
	int		 nr_failed = 0;
	int		 nr_total_failed = 0;
	int		 opt = 0, index = 0;
	int		 rank;
	int		 size;
	int		 rc;
#if CMOCKA_FILTER_SUPPORTED == 1 /** for cmocka filter(requires cmocka 1.1.5) */
	char		 filter[1024];
#endif

	d_register_alt_assert(mock_assert);

	par_init(&argc, &argv);

	par_rank(PAR_COMM_WORLD, &rank);
	par_size(PAR_COMM_WORLD, &size);
	par_barrier(PAR_COMM_WORLD);

	static struct option long_options[] = {
		{"all",		no_argument,		NULL,	'a'},
		{"mgmt",	no_argument,		NULL,	'm'},
		{"pool",	no_argument,		NULL,	'p'},
		{"cont",	no_argument,		NULL,	'c'},
		{"capa",	no_argument,		NULL,	'C'},
		{"base_dtx",	no_argument,		NULL,	't'},
		{"dist_dtx",	no_argument,		NULL,	'T'},
		{"verify",	no_argument,		NULL,	'V'},
		{"io",		no_argument,		NULL,	'i'},
		{"ec_io",	no_argument,		NULL,	'I'},
		{"checksum",	no_argument,		NULL,	'z'},
		{"agg_ec",	no_argument,		NULL,	'Z'},
		{"dedup",	no_argument,		NULL,	'U'},
		{"epoch_io",	no_argument,		NULL,	'x'},
		{"obj_array",	no_argument,		NULL,	'A'},
		{"array",	no_argument,		NULL,	'D'},
		{"daos_kv",	no_argument,		NULL,	'K'},
		{"epoch",	no_argument,		NULL,	'e'},
		{"erecov",	no_argument,		NULL,	'o'},
		{"mdr",		no_argument,		NULL,	'R'},
		{"oid_alloc",	no_argument,		NULL,	'O'},
		{"degraded",	no_argument,		NULL,	'd'},
		{"rebuild",	no_argument,		NULL,	'r'},
		{"rebuild_simple",	no_argument,	NULL,	'v'},
		{"rebuild_ec",	no_argument,		NULL,	'S'},
		{"degrade_ec",	no_argument,		NULL,	'X'},
		{"drain_simple",	no_argument,	NULL,	'b'},
		{"nvme_recovery",	no_argument,	NULL,	'N'},
		{"pipeline",	no_argument,	NULL,	'P'},
		{"group",	required_argument,	NULL,	'g'},
		{"csum_type",	required_argument,	NULL,
						CHECKSUM_ARG_VAL_TYPE},
		{"csum_cs",	required_argument,	NULL,
						CHECKSUM_ARG_VAL_CHUNKSIZE},
		{"csum_sv",	no_argument,		NULL,
						CHECKSUM_ARG_VAL_SERVERVERIFY},
		{"dmg_config",	required_argument,	NULL,	'n'},
		{"svcn",	required_argument,	NULL,	's'},
		{"subtests",	required_argument,	NULL,	'u'},
		{"exclude",	required_argument,	NULL,	'E'},
		{"filter",	required_argument,	NULL,	'f'},
		{"work_dir",	required_argument,	NULL,	'W'},
		{"workload_file", required_argument,	NULL,	'w'},
		{"obj_class",	required_argument,	NULL,	'l'},
		{"help",	no_argument,		NULL,	'h'},
		{NULL,		0,			NULL,	0}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	memset(tests, 0, sizeof(tests));

	while ((opt =
		getopt_long(argc, argv,
			    "ampcCdtTViIzUZxADKeoROg:n:s:u:E:f:w:W:hrNvbBSXl:GP",
			     long_options, &index)) != -1) {
		if (strchr(all_tests_defined, opt) != NULL) {
			tests[ntests] = opt;
			ntests++;
			continue;
		}
		switch (opt) {
		case 'a':
			break;
		case 'g':
			server_group = optarg;
			break;
		case 'n':
			dmg_config_file = optarg;
			break;
		case 'h':
			print_usage(rank);
			goto exit;
		case 's':
			svc_nreplicas = atoi(optarg);
			break;
		case 'u':
			sub_tests_str = optarg;
			break;
		case 'E':
			exclude_str = optarg;
			break;
		case 'f':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
		{
			/** Add wildcards for easier filtering */
			sprintf(filter, "*%s*", optarg);
			cmocka_set_test_filter(filter);
		}
#else
			D_PRINT("filter not enabled");
#endif
			break;
		case 'w':
			test_io_conf = optarg;
			break;
		case 'W':
			D_FREE(test_io_dir);
			D_STRNDUP(test_io_dir, optarg, PATH_MAX);
			if (test_io_dir == NULL)
				return -1;
			break;
		case 'l':
			dt_obj_class = daos_oclass_name2id(optarg);
			if (dt_obj_class == OC_UNKNOWN)
				return -1;
			break;
		case CHECKSUM_ARG_VAL_TYPE:
			dt_csum_type = daos_checksum_test_arg2type(optarg);
			break;
		case CHECKSUM_ARG_VAL_CHUNKSIZE:
			dt_csum_chunksize = atoi(optarg);
			break;
		case CHECKSUM_ARG_VAL_SERVERVERIFY:
			dt_csum_server_verify = true;
			break;
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

	if (strlen(tests) == 0) {
		strncpy(tests, all_tests, sizeof(TESTS));
	}

	if (svc_nreplicas > ARRAY_SIZE(arg->pool.ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->pool.ranks));
		return -1;
	}

	if (sub_tests_str != NULL) {
		/* sub_tests="1,2,3" sub_tests="2-8" */
		char *ptr = sub_tests_str;
		char *tmp;
		int start = -1;
		int end = -1;

		while (*ptr) {
			int number = -1;

			while (!isdigit(*ptr) && *ptr)
				ptr++;

			if (!*ptr)
				break;

			tmp = ptr;
			while (isdigit(*ptr))
				ptr++;

			/* get the current number */
			number = atoi(tmp);
			if (*ptr == '-') {
				if (start != -1) {
					print_message("str is %s\n",
						      sub_tests_str);
					return -1;
				}
				start = number;
				continue;
			} else {
				if (start != -1)
					end = number;
				else
					start = number;
			}

			if (start != -1 || end != -1) {
				if (end != -1) {
					int i;

					for (i = start; i <= end; i++) {
						sub_tests[sub_tests_idx] = i;
						sub_tests_idx++;
					}
				} else {
					sub_tests[sub_tests_idx] = start;
					sub_tests_idx++;
				}
				start = -1;
				end = -1;
			}
		}
	}

	/*Exclude tests mentioned in exclude list*/
	/* Example: daos_test -E mpc */
	if(exclude_str != NULL){
		int old_idx , new_idx=0;
		printf("\n==============");
		printf("\n Excluding tests %s" , exclude_str);
		printf("\n==============");
		for (old_idx=0;tests[old_idx]!=0;old_idx++){
			if (!strchr(exclude_str , tests[old_idx])){
				tests[new_idx]=tests[old_idx];
				new_idx++;
			}
		}
		tests[new_idx]='\0';
	}

	nr_failed = run_specified_tests(tests, rank, size,
					sub_tests_idx > 0 ? sub_tests : NULL,
					sub_tests_idx);

exit:
	par_allreduce(PAR_COMM_WORLD, &nr_failed, &nr_total_failed, 1, PAR_INT, PAR_SUM);

	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);

	if (!rank) {
		print_message("\n============ Summary %s\n", __FILE__);
		if (nr_total_failed == 0)
			print_message("OK - NO TEST FAILURES\n");
		else
			print_message("ERROR, %i TEST(S) FAILED\n",
				      nr_total_failed);
	}

	par_fini();

	D_FREE(test_io_dir);

	return nr_failed;
}
