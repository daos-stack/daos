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

void cleanup_handles(uuid_t *pool_uuids, int num_pools,
		     daos_handle_t **pool_handles, int handles_per_pool,
		     daos_handle_t **cont_handles)
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
			rc = daos_cont_close(cont_handles[i][j], NULL);
			if (rc) {
				uuid_t		cont;
				uuid_t		hdl;
				char		cont_str[64];
				char		hdl_str[64];

				dc_cont_hdl2uuid(cont_handles[i][j], &hdl, &cont);
				uuid_unparse_lower(cont, cont_str);
				uuid_unparse_lower(hdl, hdl_str);
				printf("disconnect handle %s from container %s "
					"failed: %d\n", hdl_str, cont_str, rc);
			}

			rc = daos_pool_disconnect(pool_handles[i][j], NULL);
			if (rc) {
				struct dc_pool	*pool;
				char		pool_str[64];
				char		hdl_str[64];

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
		free(pool_handles[i]);
		free(cont_handles[i]);
		pool_handles[i] = NULL;
		cont_handles[i] = NULL;
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
	int		interactive = 0; /* Program needs manual advancement*/
	char		*pool_str = NULL;
	uuid_t		*pool_uuids = NULL;
	daos_handle_t	**pool_handles = NULL;
	daos_handle_t	**cont_handles = NULL;

	progname = argv[0];
	while ((opt = getopt(argc, argv, "xs:h:wip:")) != -1) {
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

		for (j = 0; j < handles_per_pool; j++) {
			char str[37];

			uuid_unparse(pool_uuids[i], str);
			rc = daos_pool_connect(str, NULL, DAOS_PC_RW,
					       &pool_handles[i][j], NULL, NULL);
			if (rc != 0) {
				printf("Unable to connect to %s rc: %d", str, rc);
				/* Force well behaved cleanup since we're
				 * erroring out
				 */
				well_behaved = 1;
				goto cleanup;
			}
		}
	}

	if (interactive) {
		rc = pause_for_keypress();
	} else {
		/** Give a sleep grace period then exit based on -x switch */
		sleep(sleep_seconds);
	}

	/* User our handles by creating pools and connecting to them */
	for (i = 0; i < num_pools; i++) {
		int j;

		for (j = 0; j < handles_per_pool; j++) {
			uuid_t c_uuid;
			char cstr[64];

			printf("Creating container using handle %" PRIu64 "\n",
			       pool_handles[i][j].cookie);
			rc = daos_cont_create(pool_handles[i][j], &c_uuid, NULL,
					      NULL);
			if (rc != 0) {
				printf("Unable to create container using handle"
				       " %" PRIu64 " rc: %d\n",
				       pool_handles[i][j].cookie, rc);
			}

			uuid_unparse(c_uuid, cstr);
			printf("Opening container %s\n", cstr);
			rc = daos_cont_open(pool_handles[i][j], cstr,
						DAOS_COO_RW,
						&cont_handles[i][j],
						NULL, NULL);
			if (rc != 0) {
				char		uuid_str[DAOS_UUID_STR_SIZE];

				uuid_unparse(pool_uuids[i], uuid_str);
				printf("Unable to open container %s rc: %d",
				       uuid_str, rc);
			}
		}
	}

	if (abnormal_exit)
		exit(-1);

cleanup:
	if (well_behaved) {
		cleanup_handles(pool_uuids, num_pools,
				pool_handles, handles_per_pool,
				cont_handles);
	}

	/** shutdown the local DAOS stack */
	rc = daos_fini();
	if (rc != 0) {
		printf("daos_fini failed with %d", rc);
		exit(-1);
	}

	return rc;
}
