/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This provides a simple example for how to get started with a DFS container.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>

#define FAIL(fmt, ...)						\
do {								\
	fprintf(stderr, fmt " aborting\n", ## __VA_ARGS__);	\
	exit(1);						\
} while (0)

#define	ASSERT(cond, ...)					\
do {								\
	if (!(cond))						\
		FAIL(__VA_ARGS__);				\
} while (0)

#define NUM_ENTRIES	1024
#define NR_ENUM		64

int
main(int argc, char **argv)
{
	dfs_t		*dfs;
	dfs_obj_t	*dir1, *f1;
	int		i;
	time_t		ts = 0;
	int		rc;

	if (argc != 4) {
		fprintf(stderr, "usage: ./exec pool cont dirname\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = dfs_init();
	ASSERT(rc == 0, "dfs_init failed with %d", rc);

	rc = dfs_connect(argv[1], NULL, argv[2], O_RDWR, NULL, &dfs);
	ASSERT(rc == 0, "dfs_connect failed with %d", rc);

	mode_t	create_mode = S_IWUSR | S_IRUSR;
	int	create_flags = O_RDWR | O_CREAT | O_EXCL;

	/*
	 * Create & open /dir1 - need to close later. NULL for parent, means
	 * create at root.
	 */
	rc = dfs_open(dfs, NULL, argv[3], create_mode | S_IFDIR,
		      create_flags, 0, 0, NULL, &dir1);
	ASSERT(rc == 0, "create /dir1 failed\n");

	for (i = 0; i < NUM_ENTRIES; i++) {
		char name[16];

		/* create 1 dir for every 100 files */
		if (i % 100 == 0) {
			sprintf(name, "dir.%d", i);
			rc = dfs_mkdir(dfs, dir1, name, create_mode | S_IFDIR, 0);
			ASSERT(rc == 0, "create /dir1/%s failed\n", name);
		} else {
			daos_obj_id_t oid;

			sprintf(name, "file.%d", i);
			rc = dfs_open(dfs, dir1, name, create_mode | S_IFREG, create_flags, 0, 0,
				      NULL, &f1);
			ASSERT(rc == 0, "create /dir1/%s failed\n", name);

			dfs_obj2id(f1, &oid);
			/* printf("File %s \t OID: %"PRIu64".%"PRIu64"\n", name, oid.hi, oid.lo); */

			rc = dfs_release(f1);
			ASSERT(rc == 0, "dfs_release failed\n");
		}

		if (i == NUM_ENTRIES / 2) {
			sleep(1);
			ts = time(NULL);
			sleep(1);
		}
	}

	dfs_predicate_t pred = {0};
	dfs_pipeline_t *dpipe = NULL;

	strcpy(pred.dp_name, "%.6%");
	pred.dp_newer = ts;
	rc = dfs_pipeline_create(dfs, pred, DFS_FILTER_NAME | DFS_FILTER_NEWER, &dpipe);
	ASSERT(rc == 0, "dfs_pipeline_create failed with %d\n", rc);


	uint32_t num_split = 0, j;

	rc = dfs_obj_anchor_split(dir1, &num_split, NULL);
	ASSERT(rc == 0, "dfs_obj_anchor_split failed with %d\n", rc);
	printf("Anchor split in %u parts\n", num_split);

	daos_anchor_t *anchors;
	struct dirent *dents = NULL;
	daos_obj_id_t *oids = NULL;
	daos_size_t *csizes = NULL;

	anchors = malloc(sizeof(daos_anchor_t) * num_split);
	dents = malloc (sizeof(struct dirent) * NR_ENUM);
	oids = calloc(NR_ENUM, sizeof(daos_obj_id_t));
	csizes = calloc(NR_ENUM, sizeof(daos_size_t));

	uint64_t nr_total = 0, nr_matched = 0, nr_scanned;

	for (j = 0; j < num_split; j++) {
		daos_anchor_t *anchor = &anchors[j];
		uint32_t nr;

		memset(anchor, 0, sizeof(daos_anchor_t));

		rc = dfs_obj_anchor_set(dir1, j, anchor);
		ASSERT(rc == 0, "dfs_obj_anchor_set failed with %d\n", rc);

		while (!daos_anchor_is_eof(anchor)) {
			nr = NR_ENUM;
			rc = dfs_readdir_with_filter(dfs, dir1, dpipe, anchor, &nr, dents, oids,
						     csizes, &nr_scanned);
			ASSERT(rc == 0, "dfs_readdir_with_filter failed with %d\n", rc);

			nr_total += nr_scanned;
			nr_matched += nr;

			for (i = 0; i < nr; i++) {
				printf("Name: %s\t", dents[i].d_name);
				printf("OID: %"PRIu64".%"PRIu64"\t", oids[i].hi, oids[i].lo);
				printf("CSIZE = %zu\n", csizes[i]);
				if (dents[i].d_type == DT_DIR)
					printf("Type: DIR\n");
				else if (dents[i].d_type == DT_REG)
					printf("Type: FILE\n");
				else
					ASSERT(0, "INVALID dentry type\n");
			}
		}
	}

	printf("total entries scanned = %"PRIu64"\n", nr_total);
	printf("total entries matched = %"PRIu64"\n", nr_matched);

	free(dents);
	free(anchors);
	free(oids);
	free(csizes);
	rc = dfs_pipeline_destroy(dpipe);
	ASSERT(rc == 0, "dfs_release failed\n");
	/** close / finalize */
	rc = dfs_release(dir1);
	ASSERT(rc == 0, "dfs_release failed\n");
	rc = dfs_remove(dfs, NULL, argv[3], true, NULL);
	ASSERT(rc == 0, "dfs_remove failed\n");
	rc = dfs_disconnect(dfs);
	ASSERT(rc == 0, "dfs_disconnect failed");
	rc = dfs_fini();
	ASSERT(rc == 0, "dfs_fini failed with %d", rc);

	return rc;
}
