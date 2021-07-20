/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include "suite/daos_test.h"

/** local task information */
static char		node[128] = "unknown";
static daos_handle_t	poh;
static daos_handle_t	coh;
dfs_t			*dfs;
dfs_obj_t		*obj;
static daos_size_t	buf_size = 256;
static daos_size_t	block_size = 64 * 1024 * 1024;

#define FAIL(fmt, ...)						\
do {								\
	fprintf(stderr, "Process (%s): " fmt " aborting\n",	\
		node, ## __VA_ARGS__);				\
	exit(1);						\
} while (0)

#define	ASSERT(cond, ...)					\
do {								\
	if (!(cond))						\
		FAIL(__VA_ARGS__);				\
} while (0)

enum handleType {
        POOL_HANDLE,
        CONT_HANDLE,
	DFS_HANDLE
};

static int
dfs_test_file_gen(void)
{
	char		*buf;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		rc = 0;

	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		return -DER_NOMEM;

	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	daos_off_t offset = 0;

	while (block_size > offset) {
		dts_buf_render(buf, buf_size);

		rc = dfs_write(dfs, obj, &sgl, offset, NULL);
		ASSERT(rc == 0, "dfs open failed with %d", rc);
		offset += buf_size;
	}

	D_FREE(buf);
	return rc;
}

int dfs_test_thread_nr          = 32;
#define DFS_TEST_MAX_THREAD_NR  (64)
pthread_t dfs_test_tid[DFS_TEST_MAX_THREAD_NR];

struct dfs_test_thread_arg {
	int			thread_idx;
	pthread_barrier_t	*barrier;
};
struct dfs_test_thread_arg dfs_test_targ[DFS_TEST_MAX_THREAD_NR];

static void *
dfs_test_get_size(void *arg)
{
	struct stat stbuf;
	struct dfs_test_thread_arg	*targ = arg;
	int rc;

	pthread_barrier_wait(targ->barrier);

	rc = dfs_ostat(dfs, obj, &stbuf);
	ASSERT(rc == 0, "dfs ostat failed with %d", rc);

	if (stbuf.st_size != block_size) {
		fprintf(stderr, "DFS size verification failed (%zu)\n",
			stbuf.st_size);
		exit(1);
	}

	pthread_exit(NULL);
}

int
main(int argc, char **argv)
{
	uuid_t		pool_uuid, co_uuid;
	d_rank_list_t	*svcl = NULL;
	pthread_barrier_t barrier;
	int		i, j, rc;

	rc = gethostname(node, sizeof(node));
	ASSERT(rc == 0, "buffer for hostname too small");

	if (argc != 5) {
		fprintf(stderr, "args: pool svcl cont filename\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	printf("Connecting to pool %s\n", argv[1]);

	rc = uuid_parse(argv[1], pool_uuid);
	ASSERT(rc == 0, "Failed to parse 'Pool uuid': %s", argv[1]);

	svcl = daos_rank_list_parse(argv[2], ":");
	if (svcl == NULL)
		ASSERT(svcl != NULL, "Failed to allocate svcl");

	rc = daos_pool_connect(pool_uuid, NULL, svcl, DAOS_PC_RW, &poh,
			       NULL, NULL);
	ASSERT(rc == 0, "pool connect failed with %d", rc);
	d_rank_list_free(svcl);

	rc = uuid_parse(argv[3], co_uuid);
	ASSERT(rc == 0, "Failed to parse 'Cont uuid': %s", argv[3]);

	rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh,
			    NULL, NULL);
	ASSERT(rc == 0, "dfs cont open failed with %d", rc);

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	ASSERT(rc == 0, "dfs moumt failed with %d", rc);

	rc = dfs_open(dfs, NULL, argv[4], S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, 0, 131072, NULL, &obj);
	ASSERT(rc == 0, "dfs open failed with %d", rc);

	printf("Generating File\n");
	dfs_test_file_gen();

	printf("Verifying File Size\n");

	for (i = 0; i < 20000; i++) {
		if (i % 1000 == 0)
			printf("verified %d times\n", i);

		pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);
		for (j = 0; j < dfs_test_thread_nr; j++) {
			dfs_test_targ[j].thread_idx = j;
			dfs_test_targ[j].barrier = &barrier;
			rc = pthread_create(&dfs_test_tid[j], NULL,
					    dfs_test_get_size, &dfs_test_targ[j]);
			if (rc) {
				printf("Pthread create failed %d\n", rc);
				ASSERT(rc == 0, "pthread create failed");
			}
		}
		pthread_barrier_wait(&barrier);

		for (j = 0; j < dfs_test_thread_nr; j++) {
			rc = pthread_join(dfs_test_tid[j], NULL);
			ASSERT(rc == 0);
		}
	}

	rc = dfs_release(obj);
	ASSERT(rc == 0, "dfs release failed with %d", rc);

	rc = dfs_remove(dfs, NULL, argv[4], true, NULL);
	ASSERT(rc == 0, "dfs remove failed with %d", rc);

	rc = dfs_umount(dfs);
	ASSERT(rc == 0, "umount failed");

	rc = daos_cont_close(coh, NULL);
	ASSERT(rc == 0, "cont close failed");

	rc = daos_pool_disconnect(poh, NULL);
	ASSERT(rc == 0, "disconnect failed");

	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	return rc;
}
