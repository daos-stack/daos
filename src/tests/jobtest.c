/*
 * (C) Copyright 2020-2021 Intel Corporation.
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
#include <strings.h>
#include <daos.h>
#include <daos_mgmt.h>
#include <daos/common.h>
#include <daos/pool.h>

static char *progname;

void print_usage(void)
{
	fprintf(stderr, "Usage: %s -p pool_str [-s nsecs] [-x] "
			"[-h handles_per_pool]\n", progname);
}

void cleanup_handles(uuid_t *pool_uuids, int num_pools,
		     daos_handle_t **pool_handles, int handles_per_pool)
{
	int i, rc;

	for (i = 0; i < num_pools; i++) {
		int j;

		if (uuid_is_null(pool_uuids[i])) {
			continue;
		}
		for (j = 0; j < handles_per_pool; j++) {
			if (daos_handle_is_inval(pool_handles[i][j])) {
				continue;
			}
			rc = daos_pool_disconnect(pool_handles[i][j], NULL);
			if (rc) {
				struct dc_pool	*pool;
				char		pool_str[64];
				char		hdl_str[64];

				pool = dc_hdl2pool(pool_handles[i][j]);
				uuid_unparse_lower(pool->dp_pool, pool_str);
				uuid_unparse_lower(pool->dp_pool, hdl_str);
				printf("disconnect handle %s from pool %s "
					"failed\n", hdl_str, pool_str);
			}
		}
		free(pool_handles[i]);
		pool_handles[i] = NULL;
	}
}

/*
 * Takes in a comma separated list of UUIDs and turns them into a list of
 * UUID structures. It will return the number of entries on success and -1 on
 * failure
 */
int parse_pool_handles(char *hndl_str, uuid_t **handles)
{
	char		*idx, *pool_str;
	const char	delim[2] = ",";
	int		hdl_count = 1 /* we have at least 1 */;
	int		i = 0;
	int		rc;
	uuid_t		*hndls = NULL;

	/* Figure out how many handles are in the string */
	idx = strchr(hndl_str, ',');
	while (idx != NULL) {
		hdl_count++;
		idx = strchr(idx + 1, ',');
	}

	hndls = calloc(hdl_count, sizeof(uuid_t));
	if (!hndls) {
		perror("Unable to allocate space for pool handle list");
		return -1;
	}

	/* Parse the UUIDS into their structures */
	pool_str = strtok(hndl_str, delim);
	while (pool_str != NULL) {
		if (uuid_parse(pool_str, hndls[i]) != 0) {
			printf("Invalid pool uuid: %s\n", pool_str);
			rc = -1;
			goto out_err;
		}
		i++;
		pool_str = strtok(NULL, delim);
	}
	rc = hdl_count;
	*handles = hndls;
	goto out;

out_err:
	free(hndls);
out:
	return rc;
}

int
main(int argc, char **argv)
{
	int		rc, opt, i, num_pools;
	int		sleep_seconds = 5; /* time to sleep before cleanup */
	int		abnormal_exit = 0; /* whether to kill the process*/
	int		handles_per_pool = 5; /* Number of pool connections*/
	int		well_behaved = 0; /* Whether to disconnect at the end*/
	char		*pool_str = NULL;
	uuid_t		*pool_uuids = NULL;
	daos_handle_t	**pool_handles = NULL;

	progname = argv[0];
	while ((opt = getopt(argc, argv, "xs:h:wp:")) != -1) {
		switch (opt) {
		case 'x':
			abnormal_exit = 1;
			break;
		case 's':
			sleep_seconds = atoi(optarg);
			break;
		case 'h':
			handles_per_pool = atoi(optarg);
			break;
		case 'p':
			pool_str = strdup(optarg);
			break;
		case 'w':
			well_behaved = 1;
			break;
		default: /* '?' */
			print_usage();
			exit(-1);
		}
	}

	/** initialize the local DAOS stack */
	rc = daos_init();
	if (rc != 0) {
		printf("daos_init failed with %d\n", rc);
		exit(-1);
	}

	if (!pool_str) {
		print_usage();
		exit(-1);
	}


	num_pools = parse_pool_handles(pool_str, &pool_uuids);
	if (num_pools <= 0) {
		perror("Unable to parse pool handle string");
		exit(-1);
	}

	pool_handles = calloc(num_pools, sizeof(daos_handle_t *));
	if (pool_handles == NULL) {
		perror("Unable to allocate space for pool handles");
		exit(-1);
	}

	for (i = 0; i < num_pools; i++) {
		pool_handles[i] = calloc(handles_per_pool,
					sizeof(daos_handle_t));
		if (pool_handles[i] == NULL) {
			perror("Unable to allocate space for pool handles");
			exit(-1);
		}
	}

	/* Make our connections */
	for (i = 0; i < num_pools; i++) {
		int j;

		for (j = 0; j < handles_per_pool; j++) {
			rc = daos_pool_connect(pool_uuids[i], NULL, DAOS_PC_RW,
					       &pool_handles[i][j], NULL, NULL);
			if (rc != 0) {
				char		uuid_str[64];

				uuid_unparse(pool_uuids[i], uuid_str);
				printf("Unable to connect to %s rc: %d",
				       uuid_str, rc);
				/* Force well behaved cleanup since we're
				 * erroring out
				 */
				well_behaved = 1;
				goto cleanup;
			}
		}
	}

	/** Give a sleep grace period then exit based on -x switch */
	sleep(sleep_seconds);

	/* User our handles */
	for (i = 0; i < num_pools; i++) {
		int j;

		for (j = 0; j < handles_per_pool; j++) {
			uuid_t c_uuid;

			uuid_generate(c_uuid);
			printf("Creating container using handle %" PRIu64 "\n",
			       pool_handles[i][j].cookie);
			rc = daos_cont_create(pool_handles[i][j], c_uuid, NULL,
					      NULL);
			if (rc != 0) {
				printf("Unable to create container using handle"
				       " %" PRIu64 " rc: %d\n",
				       pool_handles[i][j].cookie, rc);
			}
		}
	}

	if (abnormal_exit)
		exit(-1);

cleanup:
	if (well_behaved) {
		cleanup_handles(pool_uuids, num_pools,
				pool_handles, handles_per_pool);
	}

	/** shutdown the local DAOS stack */
	rc = daos_fini();
	if (rc != 0) {
		printf("daos_fini failed with %d", rc);
		exit(-1);
	}

	return rc;
}
