/*
 * test.c
 *
 *  Created on: Jan 9, 2020
 *      Author: jiafu
 */
#include <gurt/common.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <dirent.h>

#include <libgen.h>
#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>
#include <fcntl.h>
#include <daos_obj_class.h>

int main(int argc, char *argv[])
{
	if (argc != 6) {
		printf("need arguments of server group, pool UUID," \
		    "container UUID, file name and file length\n");
		return 1;
	}
	char *server_group = argv[1];
	char *pool_str = argv[2];
	char *cont_str = argv[3];
	char *file_name = argv[4];
	int file_len = atoi(argv[5]);
	int rc = daos_init();

	if (rc) {
		printf("daos_init() failed with rc = %d\n", rc);
		return rc;
	}
	// connect to pool
	uuid_t pool_uuid;

	uuid_parse(pool_str, pool_uuid);
	d_rank_list_t *svcl = daos_rank_list_parse("0", ":");
	daos_handle_t poh = {0};

	rc = daos_pool_connect(pool_uuid, server_group, svcl,
					2, /* read write */
					&poh /* returned pool handle */,
					NULL /* returned pool info */,
					NULL /* event */);

	if (rc) {
		printf("Failed to connect to pool (%s), rc is %d\n",
		    pool_str, rc);
		return rc;
	}
	// connect to container
	uuid_t cont_uuid;

	uuid_parse(cont_str, cont_uuid);
	daos_cont_info_t co_info;
	daos_handle_t coh = {0};

	rc = daos_cont_open(poh, cont_uuid, 2, &coh, &co_info, NULL);
	if (rc) {
		printf("Failed to connect to container (%s)\n", cont_str);
		goto quit;
	}
	// mount FS
	dfs_t *dfs = NULL;

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if(rc) {
		printf("Failed to mount fs\n");
		goto quit;
	}
	// create file
	dfs_obj_t *file = NULL;

	rc = dfs_open(dfs, NULL, file_name, S_IFREG | 0755,
	    O_CREAT | 02, OC_SX, 8192, NULL, &file);
	if (rc) {
		printf("Failed to create file (%s)\n", file_name);
		goto quit;
	}
	// write to file
	char *buf = (char *)malloc(file_len);
	int j;

	for (j = 0; j < file_len; j++) {
		buf[j] = 1;
	}
	d_iov_t sg_iov = {0};
	d_sg_list_t sgl = {
		.sg_nr = 1,
		.sg_nr_out = 0,
		.sg_iovs = &sg_iov
	};

	d_iov_set(&sg_iov, buf, file_len);
	printf("write %d bytes to file\n", file_len);
	rc = dfs_write(dfs, file, &sgl, 0, NULL);
	if (rc) {
		printf("Failed to write %d bytes to file (%s)\n",
		    file_len, file_name);
		goto quit;
	}
	daos_size_t size;

	rc = dfs_get_size(dfs, file, &size);
	if (rc) {
		printf("Failed to get file length, (%s)\n", file_name);
		goto quit;
	}
	printf("file length is %ld\n", size);
	// read file
	int more_len = file_len + 100;
	char *read_buf = (char *)malloc(more_len);
	d_iov_t sg_iov2 = {0};
	d_sg_list_t sgl2 = {
		.sg_nr = 1,
		.sg_nr_out = 0,
		.sg_iovs = &sg_iov2
	};

	d_iov_set(&sg_iov2, read_buf, more_len);
	daos_size_t size2 = 0;

	printf("read %d bytes from file\n", more_len);
	rc = dfs_read(dfs, file, &sgl2, 0, &size2, NULL);
	if (rc) {
		printf("Failed to read from file (%s)\n", file_name);
		goto quit;
	}
	printf("expected read size %d, actual read size %ld\n",
	    file_len, size2);
quit:
	if (poh.cookie != 0) {
		daos_pool_disconnect(poh, NULL);
	}
	if (coh.cookie != 0) {
		daos_cont_close(coh, NULL);
	}
	if (dfs) {
		dfs_umount(dfs);
	}
	if (file) {
		dfs_release(file);
	}
	if (buf) {
		free(buf);
	}
	if (read_buf) {
		free(read_buf);
	}

	daos_fini();
}

