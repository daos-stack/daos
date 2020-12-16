/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * The purpose of this program is to provide a DAOS client which can be
 * triggered to terminate either correctly or illgally and after a given number
 * of seconds. This will be used in functional testing to be able to trigger
 * log messages for comparison.
 */

#include <stdio.h>
#include <string.h>
#include <daos.h>

int
main(int argc, char **argv)
{
	int	rc;
	int	opt;
	int	sleep_seconds = 5;
	int	abnormal_exit = 0;

	while ((opt = getopt(argc, argv, "xs:")) != -1) {
		switch (opt) {
		case 'x':
			abnormal_exit = 1;
			break;
		case 's':
			sleep_seconds = atoi(optarg);
			break;
		default: /* '?' */
			fprintf(stderr, "Usage: %s [-s nsecs] [-x]\n", argv[0]);
			exit(-1);
		}
	}

	/** initialize the local DAOS stack */
	rc = daos_init();
	if (rc != 0) {
		printf("daos_init failed with %d\n", rc);
		exit(-1);
	}

	/** Give a sleep grace period then exit based on -x switch */
	sleep(sleep_seconds);

	if (abnormal_exit)
		exit(-1);

	/** shutdown the local DAOS stack */
	rc = daos_fini();
	if (rc != 0) {
		printf("daos_fini failed with %d", rc);
		exit(-1);
	}

	return rc;
}
