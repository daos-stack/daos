/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Single-rank, single-context echo server for the RPC reconnect test.
 * Launch through crt_launch to supply CRT_L_RANK and CRT_L_GRP_CFG. Use the same
 * group name and crt_launch port on restart: the surviving client deliberately
 * retains its original attached group and cached endpoint.
 *
 * --pid_file optionally publishes the server PID for targeted interruption.
 * The first instance is terminated externally; the restarted instance exits
 * through the shutdown RPC after the client has verified recovery.
 */
#include <getopt.h>

#include "test_rpc_reconnect_common.h"

static void
test_run(char *group_name, const char *pid_file)
{
	crt_group_t  *group = NULL;
	crt_context_t context;
	pthread_t     progress_thread;
	uint32_t      group_size;
	int           rc;

	rc = crtu_srv_start_basic(group_name, &context, &progress_thread, &group, &group_size, NULL,
				  &rpc_reconnect_proto_fmt);
	D_ASSERTF(rc == 0, "crtu_srv_start_basic() failed; rc=%d\n", rc);
	D_ASSERTF(group_size == 1, "Expected one server rank, got %u\n", group_size);

	/* Publish the PID before the attach file signals readiness to the controller. */
	if (pid_file != NULL) {
		FILE *file = fopen(pid_file, "w");

		D_ASSERTF(file != NULL, "Failed to open PID file %s\n", pid_file);
		D_ASSERTF(fprintf(file, "%d\n", getpid()) > 0, "Failed to write PID file\n");
		D_ASSERTF(fclose(file) == 0, "Failed to close PID file\n");
	}

	if (crtu_get_opts()->self_rank == 0) {
		rc = crt_group_config_save(group, true);
		D_ASSERTF(rc == 0, "crt_group_config_save() failed; rc=%d\n", rc);
	}

	DBG_PRINT("RPC reconnect test server started\n");

	/* Only the final, RPC-driven shutdown reaches normal cleanup after this join. */
	rc = pthread_join(progress_thread, NULL);
	D_ASSERTF(rc == 0, "pthread_join() failed; rc=%d\n", rc);

	if (crtu_get_opts()->self_rank == 0) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0, "crt_group_config_remove() failed; rc=%d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed; rc=%d\n", rc);
	d_log_fini();
}

int
main(int argc, char **argv)
{
	static struct option long_options[] = {
	    {"name", required_argument, NULL, 'n'},
	    {"pid_file", required_argument, NULL, 'p'},
	    {0, 0, 0, 0},
	};
	char *group_name = NULL;
	char *pid_file   = NULL;
	int   option;

	while ((option = getopt_long(argc, argv, "n:p:", long_options, NULL)) != -1) {
		if (option == 'n')
			group_name = optarg;
		else if (option == 'p')
			pid_file = optarg;
		else
			return 1;
	}
	if (optind != argc || group_name == NULL) {
		fprintf(stderr, "Usage: %s --name <group_name> [--pid_file <path>]\n", argv[0]);
		return 1;
	}

	crtu_test_init(0, 40, true, true);
	test_run(group_name, pid_file);

	return 0;
}
