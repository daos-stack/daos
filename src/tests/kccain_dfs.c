/**
 * (C) Copyright 2020-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

#define FAIL(fmt, ...)                                                                             \
	do {                                                                                       \
		fprintf(stderr, fmt " aborting\n", ##__VA_ARGS__);                                 \
		exit(1);                                                                           \
	} while (0)

#define ASSERT(cond, ...)                                                                          \
	do {                                                                                       \
		if (!(cond))                                                                       \
			FAIL(__VA_ARGS__);                                                         \
	} while (0)

int
main(int argc, char **argv)
{
	dfs_t     *dfs;
	dfs_obj_t *topdir;
	int        dn;                    /* directory number count */
	int        gfn               = 1; /* global file number count */
	const int  NUM_DIRS          = 10;
	const int  NUM_FILES_PER_DIR = 20;
	int        rc;

	if (argc != 3) {
		fprintf(stderr, "usage: ./exec pool cont\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = dfs_init();
	ASSERT(rc == 0, "dfs_init failed with %d", rc);

	/** this mounts the POSIX container */
	printf("Mounting DFS for pool:%s, cont:%s\n", argv[1], argv[2]);
	rc = dfs_connect(argv[1], NULL, argv[2], O_RDWR, NULL, &dfs);
	ASSERT(rc == 0, "dfs_connect failed with %d", rc);

	mode_t d_create_mode = S_IRWXU;
	mode_t create_mode   = S_IWUSR | S_IRUSR;
	int    create_flags  = O_RDWR | O_CREAT | O_EXCL;

	/** Create & open /top - need to close later. NULL for parent, means create at root. */
	printf("Create and open dir: top\n");
	rc = dfs_open(dfs, NULL, "top", d_create_mode | S_IFDIR, create_flags, 0, 0, NULL, &topdir);
	ASSERT(rc == 0, "create /dir1 failed\n");

	for (dn = 1; dn <= NUM_DIRS; dn++) {
		char       dname[128];
		char       name[128];
		dfs_obj_t *dir;
		int        fn;

		/** Create & open /top/dir<d> - need to close later. */
		snprintf(dname, 128, "dir%d", dn);
		printf("Create and open dir: top/%s\n", dname);
		rc = dfs_open(dfs, topdir, dname, d_create_mode | S_IFDIR, create_flags, 0, 0, NULL,
			      &dir);
		ASSERT(rc == 0, "create directory %s failed\n", dname);

		/** mkdir /top/dir<d>/empty. The directory here is not open though, no release
		 * required. */
		snprintf(name, 128, "empty");
		printf("mkdir top/%s/%s\n", dname, name);
		rc = dfs_mkdir(dfs, dir, name, d_create_mode | S_IFDIR, 0);
		ASSERT(rc == 0, "create directory %s failed\n", name);

		for (fn = 0; fn < NUM_FILES_PER_DIR; fn++, gfn++) {
			dfs_obj_t  *f;
			char        wbuf[256];
			d_sg_list_t sgl;
			d_iov_t     iov;
			daos_size_t read_size;
			char        rbuf[256];

			/** create & open /top/dir<d>/file<gf> */
			snprintf(name, 128, "file%d", gfn);
			printf("Create and open file: top/%s/%s\n", dname, name);
			rc = dfs_open(dfs, dir, name, create_mode | S_IFREG, create_flags, 0, 0,
				      NULL, &f);
			ASSERT(rc == 0, "create file %s failed\n", name);

			/** write a "hello world!" string to the file at offset 0 */
			/** setup iovec (sgl in DAOS terms) for write buffer */
			snprintf(wbuf, 256, "Hello, world! This is file %d\n", gfn);
			d_iov_set(&iov, wbuf, strlen(wbuf) + 1);
			sgl.sg_nr   = 1;
			sgl.sg_iovs = &iov;
			printf("Write to open file top/%s/%s data:%s\n", dname, name, wbuf);
			rc = dfs_write(dfs, f, &sgl, 0 /** offset */, NULL);
			ASSERT(rc == 0, "dfs_write() failed\n");

			/** reset iovec for read buffer */
			d_iov_set(&iov, rbuf, sizeof(rbuf));
			rc = dfs_read(dfs, f, &sgl, 0 /** offset */, &read_size, NULL);
			ASSERT(rc == 0, "dfs_read() failed\n");
			ASSERT(read_size == strlen(wbuf) + 1, "not enough data read\n");
			printf("Read from open file top/%s/%s data:%s\n", dname, name, rbuf);

			dfs_release(f);
		}
		dfs_release(dir);
	}

	/** close / finalize */
	printf("Release top dir\n");
	dfs_release(topdir);
	rc = dfs_disconnect(dfs);
	ASSERT(rc == 0, "disconnect failed");
	rc = dfs_fini();
	ASSERT(rc == 0, "dfs_fini failed with %d", rc);
	return rc;
}
