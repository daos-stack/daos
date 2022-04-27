/*
 * (C) Copyright 2020-2022 Intel Corporation.
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
#include <daos/container.h>

static char *progname;

void print_usage(void)
{
	fprintf(stderr, "Usage: %s -p pool_str [-s nsecs] [-xwi] "
			"[-h handles_per_pool]\n", progname);
}

int pause_for_keypress(void)
{
	char ch;

	printf("Press any key to continue.\n");
	return scanf("%c", &ch);
}

void cleanup_handles(char **pool_ids, int num_pools,
		     daos_handle_t **pool_handles, int handles_per_pool,
		     daos_handle_t **cont_handles)
{
	int i, rc;

	for (i = 0; i < num_pools; i++) {
		int j;

		if (pool_ids[i] == NULL) {
			continue;
		}
		for (j = 0; j < handles_per_pool; j++) {
			if (daos_handle_is_inval(pool_handles[i][j]))
				continue;

			if (!daos_handle_is_inval(cont_handles[i][j])) {
				rc = daos_cont_close(cont_handles[i][j], NULL);
				if (rc) {
					uuid_t		cont;
					uuid_t		hdl;
					char		cont_str[DAOS_UUID_STR_SIZE];
					char		hdl_str[DAOS_UUID_STR_SIZE];

					dc_cont_hdl2uuid(cont_handles[i][j], &hdl, &cont);
					uuid_unparse_lower(cont, cont_str);
					uuid_unparse_lower(hdl, hdl_str);
					printf("disconnect handle %s from container %s "
						"failed: %d\n", hdl_str, cont_str, rc);
				}
			}

			rc = daos_pool_disconnect(pool_handles[i][j], NULL);
			if (rc) {
				struct dc_pool	*pool;
				char		pool_str[DAOS_UUID_STR_SIZE];
				char		hdl_str[DAOS_UUID_STR_SIZE];

				pool = dc_hdl2pool(pool_handles[i][j]);
				if (pool) {
					uuid_unparse_lower(pool->dp_pool, pool_str);
					uuid_unparse_lower(pool->dp_pool, hdl_str);
					printf("disconnect handle %s from pool %s "
						"failed\n", hdl_str, pool_str);
					dc_pool_put(pool);
				}
			}
		}
		free(pool_ids[i]);
		free(pool_handles[i]);
		free(cont_handles[i]);
		pool_ids[i] = NULL;
		pool_handles[i] = NULL;
		cont_handles[i] = NULL;
	}
}

/*
 * Takes in a comma separated list of pool IDs and turns them into a list of
 * pool ID strings. It will return the number of entries on success and -1 on
 * failure
 */
int parse_pool_ids(char *pools_str, char ***pool_ids)
{
	char		*idx, *pool_str;
	const char	delim[2] = ",";
	int		pool_count = 1 /* we have at least 1 */;
	int		i = 0;
	int		rc;
	char		**ids = NULL;

	/* Figure out how many handles are in the string */
	idx = strchr(pools_str, ',');
	while (idx != NULL) {
		pool_count++;
		idx = strchr(idx + 1, ',');
	}

	ids = calloc(pool_count, sizeof(char *));
	if (!ids) {
		perror("Unable to allocate space for pool ID list");
		return -1;
	}

	/* Parse the IDs */
	pool_str = strtok(pools_str, delim);
	while (pool_str != NULL) {
		ids[i] = strdup(pool_str);
		if (!ids[i]) {
			perror("Unable to allocate space for pool ID");
			rc = -1;
			goto out_err;
		}
		i++;
		pool_str = strtok(NULL, delim);
	}
	rc = pool_count;
	*pool_ids = ids;
	goto out;

out_err:
	for (i = 0; i < pool_count; i++) {
		if (ids[i])
			free(ids[i]);
	}
	free(ids);
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
	int		use_handles = 0; /* Whether to use handles */
	int		interactive = 0; /* Program needs manual advancement*/
	char		*pools_str = NULL;
	char		**pool_ids = NULL;
	daos_handle_t	**pool_handles = NULL;
	daos_handle_t	**cont_handles = NULL;

	progname = argv[0];
	while ((opt = getopt(argc, argv, "uxs:h:wip:")) != -1) {
		switch (opt) {
		case 'u':
			use_handles = 1;
			break;
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
			pools_str = strdup(optarg);
			break;
		case 'w':
			well_behaved = 1;
			break;
		case 'i':
			interactive = 1;
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

	if (!pools_str) {
		print_usage();
		exit(-1);
	}

	num_pools = parse_pool_ids(pools_str, &pool_ids);
	if (num_pools <= 0) {
		perror("Unable to parse pool handle string");
		exit(-1);
	}

	pool_handles = calloc(num_pools, sizeof(daos_handle_t *));
	if (pool_handles == NULL) {
		perror("Unable to allocate space for pool handles");
		exit(-1);
	}

	cont_handles = calloc(num_pools, sizeof(daos_handle_t *));
	if (cont_handles == NULL) {
		perror("Unable to allocate space for container handles");
		exit(-1);
	}

	for (i = 0; i < num_pools; i++) {
		pool_handles[i] = calloc(handles_per_pool,
					sizeof(daos_handle_t));
		if (pool_handles[i] == NULL) {
			perror("Unable to allocate space for pool handles");
			exit(-1);
		}
		cont_handles[i] = calloc(handles_per_pool,
					sizeof(daos_handle_t));
		if (cont_handles[i] == NULL) {
			perror("Unable to allocate container handles");
			exit(-1);
		}
	}

	/* Make our connections */
	for (i = 0; i < num_pools; i++) {
		int j;

		printf("Making %d connections to pool %s\n",
		       handles_per_pool, pool_ids[i]);

		for (j = 0; j < handles_per_pool; j++) {
			rc = daos_pool_connect(pool_ids[i], NULL, DAOS_PC_RW,
					       &pool_handles[i][j], NULL, NULL);
			if (rc != 0) {
				printf("Unable to connect to %s rc: %d",
				       pool_ids[i], rc);
				/* Force well behaved cleanup since we're
				 * erroring out
				 */
				well_behaved = 1;
				goto cleanup;
			}
		}
	}

	/* Use our handles */
	if (use_handles) {
		printf("\n");
		for (i = 0; i < num_pools; i++) {
			int j;

			for (j = 0; j < handles_per_pool; j++) {
				uuid_t c_uuid;
				char cstr[DAOS_UUID_STR_SIZE];

				uuid_generate(c_uuid);
				printf("Creating container using handle %" PRIu64 "\n",
				       pool_handles[i][j].cookie);
				rc = daos_cont_create(pool_handles[i][j], c_uuid, NULL, NULL);
				if (rc != 0) {
					printf("Unable to create container using handle"
					       " %" PRIu64 " rc: %d\n",
					       pool_handles[i][j].cookie, rc);
				}

				uuid_unparse(c_uuid, cstr);
				printf("Opening container %s\n", cstr);
				rc = daos_cont_open(pool_handles[i][j], cstr,
						    DAOS_COO_RW, &cont_handles[i][j],
						    NULL, NULL);
				if (rc != 0) {
					printf("Unable to open container %s rc: %d",
					       pool_ids[i], rc);
				}
			}
		}
	}

	if (interactive) {
		rc = pause_for_keypress();
	} else {
		/** Give a sleep grace period then exit based on -x switch */
		printf("\nSleeping for %d seconds...\n", sleep_seconds);
		sleep(sleep_seconds);
	}

	printf("\n");
	if (abnormal_exit) {
		printf("Simulating application crash!\n");
		fflush(stdout);
		exit(-1);
	}

cleanup:
	if (well_behaved) {
		printf("Cleaning up %d pool/cont handles\n",
		       num_pools * handles_per_pool);
		cleanup_handles(pool_ids, num_pools,
				pool_handles, handles_per_pool,
				cont_handles);
	} else {
		printf("Not cleaning up pool/cont handles prior to exit\n");
	}

	/** shutdown the local DAOS stack */
	rc = daos_fini();
	if (rc != 0) {
		printf("daos_fini failed with %d", rc);
		exit(-1);
	}

	return rc;
}
