/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of daos
 *
 * tests/suite/daos_test
 */
#define D_LOGFAC	DD_FAC(tests)
#include <getopt.h>
#include "daos_test.h"

/** All tests in default order (tests that kill nodes must be last) */
static const char *all_tests = "mpceiACoROdr";
static const char *all_tests_defined = "mpceixACoROdr";

d_rank_t ranks_to_kill[MAX_KILLS];

bool
test_runable(test_arg_t *arg, unsigned int required_tgts)
{
	int		 i;
	static bool	 runable = true;

	if (arg->myrank == 0) {
		daos_epoch_state_t	epoch_state;
		int			rc;

		if (arg->srv_ntgts - arg->srv_disabled_ntgts < required_tgts) {
			if (arg->myrank == 0)
				print_message("No enough targets, skipping "
					      "(%d/%d)\n",
					      arg->srv_ntgts,
					      arg->srv_disabled_ntgts);
			runable = false;
		}

		for (i = 0; i < MAX_KILLS; i++)
			ranks_to_kill[i] = arg->srv_ntgts -
					   arg->srv_disabled_ntgts - i - 1;

		rc = daos_epoch_query(arg->coh, &epoch_state, NULL);
		assert_int_equal(rc, 0);
		arg->hce = epoch_state.es_hce;
	}

	MPI_Bcast(&runable, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
	return runable;
}

int
test_pool_get_info(test_arg_t *arg, daos_pool_info_t *pinfo)
{
	bool	   connect_pool = false;
	int	   rc;

	if (daos_handle_is_inval(arg->pool.poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, pinfo,
				       NULL /* ev */);
		if (rc) {
			print_message("pool_connect "DF_UUIDF
				      " failed, rc: %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
			return rc;
		}
		connect_pool = true;
	}

	rc = daos_pool_query(arg->pool.poh, NULL, pinfo, NULL);
	if (rc != 0)
		print_message("pool query failed %d\n", rc);

	if (connect_pool) {
		rc = daos_pool_disconnect(arg->pool.poh, NULL);
		if (rc)
			print_message("disconnect failed: %d\n",
				      rc);
		arg->pool.poh = DAOS_HDL_INVAL;
	}

	return rc;
}

static bool
rebuild_pool_wait(test_arg_t *arg)
{
	daos_pool_info_t	   pinfo = { 0 };
	struct daos_rebuild_status *rst;
	int			   rc;
	bool			   done = false;

	rc = test_pool_get_info(arg, &pinfo);
	rst = &pinfo.pi_rebuild_st;
	if (rst->rs_done || rc != 0) {
		print_message("Rebuild "DF_UUIDF" (ver=%d) is done %d/%d, "
			      "obj="DF_U64", rec="DF_U64".\n",
			       DP_UUID(arg->pool.pool_uuid), rst->rs_version,
			       rc, rst->rs_errno, rst->rs_obj_nr,
			       rst->rs_rec_nr);
		done = true;
	} else {
		print_message("wait for rebuild pool "DF_UUIDF"(ver=%u), "
			      "to-be-rebuilt obj="DF_U64", already rebuilt obj="
			      DF_U64", rec="DF_U64"\n",
			      DP_UUID(arg->pool.pool_uuid), rst->rs_version,
			      rst->rs_toberb_obj_nr, rst->rs_obj_nr,
			      rst->rs_rec_nr);
	}

	return done;
}

int
test_get_leader(test_arg_t *arg, d_rank_t *rank)
{
	daos_pool_info_t	pinfo = { 0 };
	int			rc;

	rc = test_pool_get_info(arg, &pinfo);
	if (rc)
		return rc;

	*rank = pinfo.pi_leader;
	return 0;
}

bool
test_rebuild_query(test_arg_t **args, int args_cnt)
{
	bool all_done = true;
	int i;

	for (i = 0; i < args_cnt; i++) {
		bool done;

		done = rebuild_pool_wait(args[i]);
		if (!done)
			all_done = false;
	}
	return all_done;
}

void
test_rebuild_wait(test_arg_t **args, int args_cnt)
{
	while (!test_rebuild_query(args, args_cnt))
		sleep(2);
}

static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDAOS TESTS\n=============================\n");
	print_message("Use one of these options(s) for specific test\n");
	print_message("daos_test -m|--mgmt\n");
	print_message("daos_test -p|--daos_pool_tests\n");
	print_message("daos_test -c|--daos_container_tests\n");
	print_message("daos_test -C|--capa\n");
	print_message("daos_test -i|--daos_io_tests\n");
	print_message("daos_test -x|--epoch_io\n");
	print_message("daos_test -A|--array\n");
	print_message("daos_test -d|--degraded\n");
	print_message("daos_test -e|--daos_epoch_tests\n");
	print_message("daos_test -o|--daos_epoch_recovery_tests\n");
	print_message("daos_test -O|--oid_alloc\n");
	print_message("daos_test -r|--rebuild\n");
	print_message("daos_test -a|--daos_all_tests\n");
	print_message("daos_test -g|--group GROUP\n");
	print_message("daos_test -s|--svcn NSVCREPLICAS\n");
	print_message("daos_test -E|--exclude TESTS\n");
	print_message("daos_test -h|--help\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
}

int
run_daos_sub_tests(const struct CMUnitTest *tests, int tests_size,
		   daos_size_t pool_size, int *sub_tests,
		   int sub_tests_size, test_setup_cb_t setup_cb,
		   test_teardown_cb_t teardown_cb)
{
	void *state = NULL;
	int i;
	int rc;

	D_ASSERT(pool_size > 0);
	rc = test_setup(&state, SETUP_CONT_CONNECT, true, pool_size, NULL);
	if (rc)
		return rc;

	if (setup_cb != NULL) {
		rc = setup_cb(&state);
		if (rc)
			return rc;
	}

	for (i = 0; i < sub_tests_size; i++) {
		int idx = sub_tests ? sub_tests[i] : i;
		test_arg_t	*arg = state;

		if (idx >= tests_size) {
			print_message("No test %d\n", idx);
			continue;
		}

		arg->index = idx;
		print_message("%s\n", tests[idx].name);
		if (tests[idx].setup_func)
			tests[idx].setup_func(&state);

		tests[idx].test_func(&state);
		if (tests[idx].teardown_func)
			tests[idx].teardown_func(&state);
	}

	if (teardown_cb != NULL) {
		rc = teardown_cb(&state);
		if (rc)
			return rc;
	} else {
		test_teardown(&state);
	}

	return 0;
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
			nr_failed = run_daos_mgmt_test(rank, size);
			break;
		case 'p':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS pool tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_daos_pool_test(rank, size);
			break;
		case 'c':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS container tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_cont_test(rank, size);
			break;
		case 'C':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS capability tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_capa_test(rank, size);
			break;
		case 'i':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS IO test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_io_test(rank, size, sub_tests,
						      sub_tests_size);
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
			daos_test_print(rank, "DAOS Array test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_array_test(rank, size);
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

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Barrier(MPI_COMM_WORLD);

	static struct option long_options[] = {
		{"all",		no_argument,		NULL,	'a'},
		{"mgmt",	no_argument,		NULL,	'm'},
		{"pool",	no_argument,		NULL,	'p'},
		{"cont",	no_argument,		NULL,	'c'},
		{"capa",	no_argument,		NULL,	'C'},
		{"io",		no_argument,		NULL,	'i'},
		{"epoch_io",	no_argument,		NULL,	'x'},
		{"array",	no_argument,		NULL,	'A'},
		{"epoch",	no_argument,		NULL,	'e'},
		{"erecov",	no_argument,		NULL,	'o'},
		{"mdr",		no_argument,		NULL,	'R'},
		{"oid_alloc",	no_argument,		NULL,	'O'},
		{"degraded",	no_argument,		NULL,	'd'},
		{"rebuild",	no_argument,		NULL,	'r'},
		{"group",	required_argument,	NULL,	'g'},
		{"svcn",	required_argument,	NULL,	's'},
		{"subtests",	required_argument,	NULL,	'u'},
		{"exclude",	required_argument,	NULL,	'E'},
		{"work_dir",	required_argument,	NULL,	'W'},
		{"workload_file", required_argument,	NULL,	'w'},
		{"help",	no_argument,		NULL,	'h'}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	memset(tests, 0, sizeof(tests));

	while ((opt = getopt_long(argc, argv, "ampcCdixAeoROg:s:u:E:w:W:hr",
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
		case 'w':
			test_io_conf = optarg;
			break;
		case 'W':
			strncpy(test_io_dir, optarg, PATH_MAX);
			break;
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

	if (strlen(tests) == 0) {
		strcpy(tests , all_tests);
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
	MPI_Allreduce(&nr_failed, &nr_total_failed, 1, MPI_INT, MPI_SUM,
		      MPI_COMM_WORLD);

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

	MPI_Finalize();

	return nr_failed;
}
