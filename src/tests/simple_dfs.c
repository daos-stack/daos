/**
 * (C) Copyright 2020-2023 Intel Corporation.
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
#include <pthread.h>

#include <daos.h>
#include <daos_fs.h>
#include <gurt/common.h>

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

#define POOL_COUNT 4
#define THREAD_COUNT 32

struct thread_args {
	size_t low;
	size_t high;
	dfs_t *dfs;
	dfs_obj_t *dir;
};

void *work_func(void *data) {
	struct thread_args *arg = data;
	int rc = 0;
	char buf[1024];

	for (int i = arg->low; i < arg->high; i++) {
		sprintf(buf, "%d", i);
		rc = dfs_remove(arg->dfs, arg->dir, buf, false, NULL);
		ASSERT(rc == 0 || rc == 2, "dfs_remove failed with %d", rc);
	}
	return NULL;
}

int
main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: ./exec pool cont file_count\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	int rc = dfs_init();
	ASSERT(rc == 0, "dfs_init failed with %d", rc);

	struct thread_args args[THREAD_COUNT];

	for (int i = 0; i < POOL_COUNT; i++) {
		dfs_t		*dfs;
		dfs_obj_t	*dir;

		/** this creates and mounts the POSIX container */
		rc = dfs_connect(argv[1], NULL, argv[2], O_CREAT | O_RDWR, NULL, &dfs);
		ASSERT(rc == 0, "dfs_connect failed with %d", rc);

		/** Create & open /dir - need to close later. NULL for parent, means create at root. */
		rc = dfs_open(dfs, NULL, "dir", S_IFDIR, O_RDWR, 0, 0, NULL, &dir);
		ASSERT(rc == 0, "create /dir failed\n");

		for (int j = i; j < THREAD_COUNT; j += POOL_COUNT) {
			args[j].dfs = dfs;
			args[j].dir = dir;
		}
	}

	const size_t file_count = atoi(argv[3]);

	pthread_t workers[THREAD_COUNT];
	size_t step_count = (file_count + THREAD_COUNT + 1) / THREAD_COUNT;
	for (int i = 0, j = 0; i < THREAD_COUNT; i++, j += step_count) {
		struct thread_args *arg = &args[i];
		arg->low = j;
		arg->high = MIN(j + step_count, file_count);
		rc = pthread_create(&workers[i], NULL, work_func, arg);
		ASSERT(rc == 0, "thread create failed\n");
	}

	for (int i = 0; i < THREAD_COUNT; i++) {
		rc = pthread_join(workers[i], NULL);
		ASSERT(rc == 0, "thread join failed\n");
	}

	for (int i = 0; i < POOL_COUNT; i++) {
		dfs_release(args[i].dir);
		rc = dfs_disconnect(args[i].dfs);
		ASSERT(rc == 0, "disconnect failed");
	}

	rc = dfs_fini();
	ASSERT(rc == 0, "dfs_fini failed with %d", rc);
	return rc;
}
