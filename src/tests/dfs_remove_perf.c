/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This provides a simple example for how to get started with a DFS container.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mpi.h>
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

enum handleType {
        POOL_HANDLE,
        CONT_HANDLE,
	DFS_HANDLE
};

static dfs_t *dfs;
static daos_handle_t poh, coh;
static int rank, rankn;

/* Distribute process 0's pool or container handle to others. */
static int
HandleDistribute(enum handleType type)
{
        d_iov_t global;
        int        rc;

        global.iov_buf = NULL;
        global.iov_buf_len = 0;
        global.iov_len = 0;

        assert(type == POOL_HANDLE || type == CONT_HANDLE || type == DFS_HANDLE);
        if (rank == 0) {
                /* Get the global handle size. */
                if (type == POOL_HANDLE)
                        rc = daos_pool_local2global(poh, &global);
                else if (type == CONT_HANDLE)
                        rc = daos_cont_local2global(coh, &global);
                else
                        rc = dfs_local2global(dfs, &global);
		ASSERT(rc == 0, "daos_pool_local2global failed %d", rc);
        }

        MPI_Bcast(&global.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

	global.iov_len = global.iov_buf_len;
        global.iov_buf = malloc(global.iov_buf_len);
        if (global.iov_buf == NULL)
		exit(1);

        if (rank == 0) {
                if (type == POOL_HANDLE)
                        rc = daos_pool_local2global(poh, &global);
                else if (type == CONT_HANDLE)
                        rc = daos_cont_local2global(coh, &global);
                else
                        rc = dfs_local2global(dfs, &global);
		ASSERT(rc == 0, "daos_pool_local2global failed  %d", rc);
        }

        MPI_Bcast(global.iov_buf, global.iov_buf_len, MPI_BYTE, 0, MPI_COMM_WORLD);

        if (rank != 0) {
                if (type == POOL_HANDLE)
                        rc = daos_pool_global2local(global, &poh);
                else if (type == CONT_HANDLE)
                        rc = daos_cont_global2local(poh, global, &coh);
                else
                        rc = dfs_global2local(poh, coh, 0, global, &dfs);
		ASSERT(rc == 0, "daos_cont_global2local failed %d", rc);
        }

        if (global.iov_buf)
                free(global.iov_buf);
        return rc;
}

int
main(int argc, char **argv)
{
	dfs_obj_t	*dir1, *f1;
	double		start, end, total, max;
	int		i, files_per_proc;
	int		rc;

	if (argc != 3) {
		fprintf(stderr, "usage: ./exec pool num_files_per_proc\n");
		exit(1);
	}

	files_per_proc = atoi(argv[2]);

	rc = MPI_Init(&argc, &argv);
	ASSERT(rc == MPI_SUCCESS, "MPI_Init failed with %d", rc);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rankn);

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	if (rank == 0) {
		rc = daos_pool_connect(argv[1], NULL, DAOS_PC_RW, &poh, NULL, NULL);
		ASSERT(rc == 0, "pool connect failed with %d", rc);

		rc = dfs_cont_create_with_label(poh, "mycont", NULL, NULL, &coh, &dfs);
		ASSERT(rc == 0, "DFS cont create failed with %d", rc);
	}

        HandleDistribute(POOL_HANDLE);
        HandleDistribute(CONT_HANDLE);
        HandleDistribute(DFS_HANDLE);

	mode_t	create_mode = S_IWUSR | S_IRUSR | S_IXUSR;
	int	create_flags = O_RDWR | O_CREAT | O_EXCL;

	/*
	 * Create & open /dir1 - need to close later. NULL for parent, means
	 * create at root.
	 */
	if (rank == 0) {
		printf("Setup Complete, creating dir ...\n");
		rc = dfs_mkdir(dfs, NULL, "dir1", create_mode, OC_SX);
		ASSERT(rc == 0, "create /dir1 failed\n");
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_open(dfs, NULL, "dir1", S_IFDIR, O_RDWR, 0, 0, NULL, &dir1);
	ASSERT(rc == 0, "open /dir1 failed %d\n", rc);

	if (rank == 0)
		printf("Creating Files ...\n");
	start = MPI_Wtime();
	for (i = 0; i < files_per_proc; i++) {
		char filename[16];

		sprintf(filename, "%s.%d.%d", "file", rank, i);
		rc = dfs_open(dfs, dir1, filename, create_mode | S_IFREG, create_flags,
			      OC_S1, 0, NULL, &f1);
		ASSERT(rc == 0, "create file %s failed\n", filename);

		/** close / finalize */
		dfs_release(f1);
	}
	end = MPI_Wtime();
	total = end - start;
	MPI_Reduce(&total, &max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	if (rank == 0)
		printf("create time = %f\n", max);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("Stating Files ...\n");
	start = MPI_Wtime();
	for (i = 0; i < files_per_proc; i++) {
		char filename[16];
		struct stat stbuf;

		sprintf(filename, "%s.%d.%d", "file", rank, i);
		rc = dfs_stat(dfs, dir1, filename, &stbuf);
		ASSERT(rc == 0, "stat file %s failed\n", filename);
	}
	end = MPI_Wtime();
	total = end - start;
	MPI_Reduce(&total, &max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	if (rank == 0)
		printf("stat time = %f\n", max);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("Removing Files ...\n");
	start = MPI_Wtime();
	for (i = 0; i < files_per_proc; i++) {
		char filename[16];

		sprintf(filename, "%s.%d.%d", "file", rank, i);
		rc = dfs_remove(dfs, dir1, filename, 0, NULL);
		ASSERT(rc == 0, "remove file %s failed\n", filename);
	}
	end = MPI_Wtime();
	total = end - start;
	MPI_Reduce(&total, &max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	if (rank == 0)
		printf("file remove time = %f\n", max);

	dfs_release(dir1);
	MPI_Barrier(MPI_COMM_WORLD);

	if (rank == 0) {
		printf("Removing Parent dir ...\n");
		start = MPI_Wtime();
		rc = dfs_remove(dfs, NULL, "dir1", 0, NULL);
		ASSERT(rc == 0, "remove dir failed\n");
		end = MPI_Wtime();
		printf("Parent dir remove time = %f\n", end-start);
	}

	rc = dfs_umount(dfs);
	ASSERT(rc == 0, "dfs_umount failed");
	rc = daos_cont_close(coh, NULL);
	ASSERT(rc == 0, "cont close failed");

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		printf("Destroying Container ...\n");
		rc = daos_cont_destroy(poh, "mycont", 0, NULL);
		ASSERT(rc == 0, "cont destroy failed\n");
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_pool_disconnect(poh, NULL);
	ASSERT(rc == 0, "disconnect failed");
	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	MPI_Finalize();
	return rc;
}
