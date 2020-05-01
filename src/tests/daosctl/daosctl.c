/**
 * (C) Copyright 2018-2020 Intel Corporation.
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

#ifdef USE_MPI
#include <mpi.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <argp.h>

#include "daosctl.h"

#include <daos.h>
#include <daos_api.h>
#include <daos_mgmt.h>
#include <daos/common.h>

const char *program_bug_address = "scott.kirvan@intel.com";
const char *program_version = "daosctl version 0.1";

const char daosctl_usage_string[] =
	"daosctl [--version] [--help] [--list-cmds] COMMAND [ARGS]";
const char daosctl_more_info_string[] =
	"See 'daosctl COMMAND --help' for more info on a specific command.\n"
	" daosctl --list-cmds to see all available commands";
const char daosctl_summary_string[] =
	"daosctl handles basic management of DAOS";
const char daosctl_option_string[] =
	"\t-h --help     prints this message\n"
	"\t--usage       prints a short usage message\n"
	"\t--list-cmds   lists the available daosctl commands";

static struct cmd_struct commands[] = {
	{ "create-container", cmd_create_container },
	{ "create-pool", cmd_create_pool },
	{ "destroy-container", cmd_destroy_container },
	{ "destroy-pool", cmd_destroy_pool },
	{ "connect-pool", cmd_connect_pool },
	{ "evict-pool", cmd_evict_pool },
	{ "exclude-target", cmd_exclude_target },
	{ "kill-server", cmd_kill_server },
	{ "query-container", cmd_query_container },
	{ "query-pool-status", cmd_query_pool_status },
	{ "test-create-pool", cmd_test_create_pool },
	{ "test-connect-pool", cmd_test_connect_pool },
	{ "kill-leader", cmd_kill_pool_leader },
	{ "test-evict-pool", cmd_test_evict_pool },
	{ "test-query-pool", cmd_test_query_pool },
	{ "write-pattern", cmd_write_pattern },
	{ "verify-pattern", cmd_verify_pattern },
	{ "help", cmd_help }
};
int command_count = ARRAY_SIZE(commands);

int
print_help(void)
{
	printf("\n usage: %s\n", daosctl_usage_string);
	printf("\n %s\n", daosctl_summary_string);
	printf("\n %s\n", daosctl_option_string);
	printf("\n %s\n\n", daosctl_more_info_string);
	return 0;
}

/**
 * Initializes daos, specifically the MPI stuff.  Just pass the
 * argc/argv that came from main.
 */
int
setup(int argc, char **argv)
{
#ifdef USE_MPI
	int my_client_rank = 0;
	int rank_size = 1;

	/* setup the MPI stuff */
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_client_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rank_size);
	MPI_Barrier(MPI_COMM_WORLD);
#endif
	return daos_init();
}

/**
 * The counterpart to setup above, release resources.
 */
int
done()
{
	int rc = daos_fini();
#ifdef USE_MPI
	MPI_Finalize();
#endif
	return rc;
}

/**
 * The parsing of arguments is done in 2 phases, the 1st phase is
 * arguments to daosctl itself which are handled here.  The second
 * part is the options passed to each command which are handled by
 * the logic that processes each command.
 */
int
handle_information_options(const char ***argv, int *argc)
{
	if (*argc > 1) {
		const char *cmd = (*argv)[1];

		if (cmd[0] != '-')
			return 0;

		if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
			print_help();
			exit(0);
		}

		if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version")) {
			printf("\n%s\n", program_version);
			exit(0);
		}

		if (!strcmp(cmd, "-V") || !strcmp(cmd, "--usage")) {
			printf("\n%s\n", program_version);
			exit(0);
		}

		if (!strcmp(cmd, "--list-cmds")) {
			int i;

			printf("daosctl available commands:\n\n");
			for (i = 0; i < command_count; i++) {
				struct cmd_struct *p = commands+i;

				printf("\t%s\n", p->cmd);
			}
			exit(0);
		} else {
			D_PRINT("Unknown option: %s\n", cmd);
			D_PRINT("\n Usage: %s\n", daosctl_usage_string);
			exit(129);
		}
	} else {
		D_PRINT("No options or commands.\n");
		D_PRINT("\n Usage: %s\n", daosctl_usage_string);
		exit(129);
	}
}

/**
 * Uses the function table to call the right code for the detected
 * command.
 */
static int
process_cmd(int argc, const char **argv)
{
	int rc = EINVAL;
	int i;

	for (i = 0; i < command_count; i++) {
		if (!strcmp(commands[i].cmd, argv[1])) {
			rc = commands[i].fn(argc, argv, NULL);
			break;
		}
	}

	if (rc == EINVAL) {
		D_PRINT("Unknown command or missing argument: %s\n\n", argv[1]);
		print_help();
	}

	return rc;
}

int
main(int argc, const char **argv)
{
	/* doesn't return if there were informational options */
	handle_information_options(&argv, &argc);

	/* setup daos if that isn't possible no point continuing */
	int test_rc = setup(argc, (char **)argv);

	if (test_rc) {
		D_PRINT("Couldn't initialize DAOS.\n");
		return 1;
	}

	/* there is a real command to process */
	test_rc = process_cmd(argc, argv);

	/* shutdown daos */
	done();
	return test_rc;
}
