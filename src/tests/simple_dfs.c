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


int
main(int argc, char **argv)
{
	dfs_t		*dfs;
	dfs_obj_t	*dir1, *f1;
	int		rc;

	if (argc != 3) {
		fprintf(stderr, "usage: ./exec pool cont\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = dfs_init();
	ASSERT(rc == 0, "dfs_init failed with %d", rc);

	/** this creates and mounts the POSIX container */
	rc = dfs_connect(argv[1], NULL, argv[2], O_CREAT | O_RDWR, NULL, &dfs);
	ASSERT(rc == 0, "dfs_connect failed with %d", rc);

	mode_t	create_mode = S_IWUSR | S_IRUSR;
	int	create_flags = O_RDWR | O_CREAT | O_EXCL;

	/** Create & open /dir1 - need to close later. NULL for parent, means create at root. */
	rc = dfs_open(dfs, NULL, "dir1", create_mode | S_IFDIR, create_flags, 0, 0, NULL, &dir1);
	ASSERT(rc == 0, "create /dir1 failed\n");

	/** mkdir /dir1/dir2. The directory here is not open though, no release required. */
	rc = dfs_mkdir(dfs, dir1, "dir2", create_mode | S_IFDIR, 0);
	ASSERT(rc == 0, "create /dir1/dir2 failed\n");

	/** create & open /dir1/file1 */
	rc = dfs_open(dfs, dir1, "file1", create_mode | S_IFREG, create_flags, 0, 0, NULL, &f1);
	ASSERT(rc == 0, "create /dir1/file failed\n");

	/** write a "hello world!" string to the file at offset 0 */

	char		*wbuf = "hello world!";
	d_sg_list_t     sgl;
	d_iov_t         iov;

	/** setup iovec (sgl in DAOS terms) for write buffer */
	d_iov_set(&iov, wbuf, strlen(wbuf) + 1);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	rc = dfs_write(dfs, f1, &sgl, 0 /** offset */, NULL);
	ASSERT(rc == 0, "dfs_write() failed\n");

	char		rbuf[1024];
	daos_size_t	read_size;

	/** reset iovec for read buffer */
	d_iov_set(&iov, rbuf, sizeof(rbuf));
	rc = dfs_read(dfs, f1, &sgl, 0 /** offset */, &read_size, NULL);
	ASSERT(rc == 0, "dfs_read() failed\n");
	ASSERT(read_size == strlen(wbuf) + 1, "not enough data read\n");
	printf("read back: %s\n", rbuf);

	/** close / finalize */
	dfs_release(f1);
	dfs_release(dir1);
	rc = dfs_disconnect(dfs);
	ASSERT(rc == 0, "disconnect failed");
	rc = dfs_fini();
	ASSERT(rc == 0, "dfs_fini failed with %d", rc);
	return rc;
}
