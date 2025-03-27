/*
 * Copyright (c) 2022 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Regression test for https://github.com/ofiwg/libfabric/pull/7605:
 * "prov/shm: Properly chain the original signal handlers".
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <rdma/fabric.h>
#include <signal.h>
#include "shared.h"

int main(int argc, char **argv)
{
	int child;
	int status;
	int op;
	opts = INIT_OPTS;

	if ((child = fork())) {
		usleep(500000); /* give child time to finish initialization */
		kill(child, SIGINT);
		usleep(5000000); /* give child time to handle the signal */
		kill(child, SIGKILL);

		waitpid(child, &status, 0);
		if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) {
			printf("Pass: child caught SIGINT and exited as expected\n");
			exit(0);
		}
		printf("Fail: child killed by SIGKILL or exited with error\n");
		exit(EXIT_FAILURE);
	} else {
		hints = fi_allocinfo();
		if (!hints)
			exit(EXIT_FAILURE);

		while ((op = getopt(argc, argv, "p:h")) != -1) {
			switch (op) {
			case 'p':
				hints->fabric_attr->prov_name = strdup(optarg);
				break;
			case '?':
			case 'h':
				FT_PRINT_OPTS_USAGE("-p <provider>", "specific provider name eg shm, efa");
				return EXIT_FAILURE;
			}
		}
		hints->caps = FI_MSG;
		hints->mode = FI_CONTEXT;
		if (ft_init_fabric()) {
			ft_freehints(hints);
			exit(EXIT_FAILURE);
		}

		while (1)
			;
	}
}
