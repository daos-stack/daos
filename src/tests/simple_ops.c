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
#include <syscall.h>
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

struct thread_args {
	size_t low;
	size_t high;
	dfs_t *dfs;
	dfs_obj_t *dir;
};

static inline pid_t gettid() {
	return syscall(SYS_gettid);
}

void *dummy_func(void *data) {
	struct thread_args *arg = data;
	printf("thread %d will handle '%ld' files\n", gettid(), arg->high - arg->low);
	return NULL;
}

void *create_func(void *data) {
	struct thread_args *arg = data;
	int rc = 0;
	char fn[1024];

	mode_t create_mode = S_IWUSR | S_IRUSR;
	int create_flags = O_RDWR | O_CREAT;

	const size_t wsz = 1048576;
	char *wbuf = malloc(wsz);

	d_sg_list_t sgl;
	d_iov_t iov;

	d_iov_set(&iov, wbuf, wsz);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	for (int i = arg->low; i < arg->high; i++) {
		dfs_obj_t *f;

		sprintf(fn, "%d", i);
		rc = dfs_open(arg->dfs, arg->dir, fn, create_mode | S_IFREG, create_flags, 0, 0, NULL, &f);
		ASSERT(rc == 0, "create /dir/%s failed\n", fn);

		rc = dfs_write(arg->dfs, f, &sgl, 0, NULL);
		ASSERT(rc == 0 || rc == 2, "dfs_remove failed with %d", rc);

		dfs_release(f);
	}
	free(wbuf);
	return NULL;
}

void *remove_func(void *data) {
	struct thread_args *arg = data;
	int rc = 0;
	char buf[1024];

	printf("thread %d will delete '%ld' files\n", gettid(), arg->high - arg->low);
	for (int i = arg->low; i < arg->high; i++) {
		sprintf(buf, "%d", i);
		rc = dfs_remove(arg->dfs, arg->dir, buf, false, NULL);
		ASSERT(rc == 0 || rc == 2, "dfs_remove failed with %d", rc);
	}
	printf("thread %d done\n", gettid());
	return NULL;
}

int
main(int argc, char **argv)
{
	size_t pool_count = 4;
	size_t thread_count = 32;
	size_t file_count = 1000;
	const char *pool = "tank";
	const char *cont = "cont";

	struct {
		const char *op;
		void *(*cb)(void *);
	} cbs[] = {
		{ "dummy", dummy_func },
		{ "create", create_func },
		{ "delete", remove_func },
		{ "", NULL }
	};

	void *(*func)(void *) = dummy_func;

	int opt;
	while ((opt = getopt(argc, argv, "p:c:n:t:f:o:")) != -1) {
		switch (opt) {
		case 'p':
			pool = optarg;
			break;
		case 'c':
			cont = optarg;
			break;
		case 'n':
			pool_count = atoi(optarg);
			break;
		case 't':
			thread_count = atoi(optarg);
			break;
		case 'f':
			file_count = atoi(optarg);
			break;
		case 'o':
			for (int i = 0; i < ARRAY_SIZE(cbs); i++) {
				if (cbs[i].cb == NULL) {
					fprintf(stderr, "unknown ops '%s'.\n", optarg);
					exit(EXIT_FAILURE);
				}
				if (strcmp(optarg, cbs[i].op) == 0) {
					func = cbs[i].cb;
					break;
				}
			}
			break;
		default:
			fprintf(stderr, "unknown option: '%c'\n", opt);
			fprintf(stderr, "Usage: %s [-p pool_name] [-c container] [-n pool_handle_count] [-t thread_count] [-f file_count]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	/** initialize the local DAOS stack */
	int rc = dfs_init();
	ASSERT(rc == 0, "dfs_init failed with %d", rc);

	struct thread_args *args = alloca(sizeof(*args) * thread_count);

	for (int i = 0; i < pool_count; i++) {
		dfs_t		*dfs;
		dfs_obj_t	*dir;

		/** this creates and mounts the POSIX container */
		rc = dfs_connect(pool, NULL, cont, O_CREAT | O_RDWR, NULL, &dfs);
		ASSERT(rc == 0, "dfs_connect failed with %d", rc);

		/** Create & open /dir - need to close later. NULL for parent, means create at root. */
		rc = dfs_open(dfs, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR,  O_RDWR | O_CREAT, 0, 0, NULL, &dir);
		ASSERT(rc == 0, "create /dir failed\n");

		for (int j = i; j < thread_count; j += pool_count) {
			args[j].dfs = dfs;
			args[j].dir = dir;
		}
	}

	pthread_t *workers = alloca(sizeof(*workers) * thread_count);
	size_t group_count = file_count / thread_count;
	size_t rem = file_count - group_count * thread_count;
	for (int i = 0, j = 0; i < thread_count; i++) {
		struct thread_args *arg = &args[i];
		arg->low = j;
		arg->high = j + group_count + !!rem;
		rc = pthread_create(&workers[i], NULL, func, arg);
		ASSERT(rc == 0, "thread create failed\n");

		j = arg->high;
		if (rem > 0) rem--;
	}

	for (int i = 0; i < thread_count; i++) {
		rc = pthread_join(workers[i], NULL);
		ASSERT(rc == 0, "thread join failed\n");
	}

	for (int i = 0; i < pool_count; i++) {
		dfs_release(args[i].dir);
		rc = dfs_disconnect(args[i].dfs);
		ASSERT(rc == 0, "disconnect failed");
	}

	rc = dfs_fini();
	ASSERT(rc == 0, "dfs_fini failed with %d", rc);
	return rc;
}
