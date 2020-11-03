/*
 * (C) Copyright 2020 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
