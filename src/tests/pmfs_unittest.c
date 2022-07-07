/*-
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include <pmfs/pmfs.h>
#include <pmfs/vos_target_fs.h>
#include <pmfs/vos_tasks.h>
#include "perf_internal.h"
#include <spdk/crc32.h>
#include <daos_srv/vos_types.h>
#include <daos_obj.h>
#include <daos_types.h>

static void
pmfs_buffer_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

static inline int
pmfs_sgl_init(d_sg_list_t *sgl, unsigned int nr)
{
	sgl->sg_nr_out = 0;
	sgl->sg_nr = nr;

	if (unlikely(nr == 0)) {
		sgl->sg_iovs = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(sgl->sg_iovs, nr);

	return sgl->sg_iovs == NULL ? -DER_NOMEM : 0;
}

static int
prepare_sgl(d_sg_list_t *sgl, daos_size_t size)
{
	char *buf;

	D_ALLOC(buf, size);
	if (buf == NULL) {
		return -DER_NOMEM;
	}
	pmfs_sgl_init(sgl, 1);
	pmfs_buffer_render(buf, size);

	sgl->sg_iovs[0].iov_buf = buf;
	sgl->sg_iovs[0].iov_len = size;

	return 0;
}

static int
app_send_thread_test_pmfs_cmds_in_pool(struct pmfs *pmfs)
{
	int rc;
	uint32_t i;

	rc = pmfs_mkdir_start(pmfs, NULL, "pmfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir start failed\r\n");
		goto end;
	}

	rc = pmfs_mkdir_start(pmfs, NULL, "dfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir start failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs mkdir done---------------\r\n");
	/* start listdir thread */
	uint32_t nr;

	rc = pmfs_listdir_start(pmfs, NULL, &nr);
	if (rc != 0) {
		D_PRINT("pmfs listdir start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs list %d directories done---\r\n", nr);
	D_PRINT("---------------pmfs listdir done---------------\r\n");
	/* start open obj start */
	struct pmfs_obj *obj = NULL;

	rc = pmfs_open_start(pmfs, NULL, "pmfs", S_IFDIR, O_RDWR | O_CREAT,
			     1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open start failed\r\n");
		goto end;
	}
	rc = pmfs_mkdir_start(pmfs, obj, "nfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir nfs in pmfs start failed\r\n");
		goto end;
	}
	rc = pmfs_mkdir_start(pmfs, obj, "tfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir tfs in pmfs start failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs open folder pmfs done---------------\r\n");
	/* start readdir obj start */
	struct dirent tmp_dirs = { 0 };

	rc = pmfs_readdir_start(pmfs, obj, &nr, &tmp_dirs);
	if (rc != 0) {
		D_PRINT("readdir failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs readdir %s--------------\r\n",
		tmp_dirs.d_name);
	D_PRINT("---------------pmfs readdir done---------------\r\n");
	/* start lookup start */
	struct pmfs_obj *tmp_obj = NULL;
	mode_t mode;

	rc = pmfs_lookup_start(pmfs, "/pmfs", 1, &tmp_obj, &mode, NULL);
	if (rc != 0) {
		D_PRINT("pmfs lookup start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs lookup done---------------\r\n");
	/* start pmfs remove */
	daos_obj_id_t oid;

	rc = pmfs_remove_start(pmfs, obj, "tfs", true, &oid);
	if (rc != 0) {
		D_PRINT("pmfs remove start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs remove done---------------\r\n");
	/* start pmfs stat */
	struct stat stbuf;

	rc = pmfs_stat_start(pmfs, obj, "nfs", &stbuf);
	if (rc != 0) {
		D_PRINT("pmfs stat start failed\r\n");
		goto end;
	}
	D_PRINT("total size =%ld", stbuf.st_size);
	D_PRINT("\t  File type and mode  =%x \r\n", stbuf.st_mode);
	D_PRINT("---------------pmfs stat done---------------\r\n");
	/* start release tmp obj start */

	rc = pmfs_release_start(tmp_obj);
	if (rc != 0) {
		D_PRINT("pmfs release tmp_obj start failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs release tmp_obj done---------------\r\n");
	/* start release obj start */
	rc = pmfs_release_start(obj);
	if (rc != 0) {
		D_PRINT("pmfs release start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs release done---------------\r\n");
	/* start create a file and open */
	D_PRINT("---------------pmfs open a file-----------------\r\n");
	rc = pmfs_open_start(pmfs, NULL, "pmfs.c", S_IFREG, O_RDWR | O_CREAT,
			     1024, "sssss", &obj);
	if (rc != 0 || !obj) {
		D_PRINT("pmfs open file start failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs open a file done -----------------\r\n");
	/*start truncate a file */
	D_PRINT("---------------pmfs start truncate  a file done -------\r\n");
	rc = pmfs_truncate_start(pmfs, obj, 4096);
	if (rc != 0) {
		D_PRINT("pmfs truncate start failed\r\n");
		goto end;
	}

	if (obj->file_size != 4096) {
		D_PRINT("pmfs_truncate file failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs truncate a file done -----------------\r\n");
	pmfs_release(obj);
	int record;

	rc = pmfs_listdir_start(pmfs, NULL, &nr);
	if (rc != 0) {
		D_PRINT("pmfs listdir start failed\r\n");
		goto end;
	}
	record = nr;
	D_PRINT("-----pmfs list %d files before rename---\r\n", nr);
	rc = pmfs_lookup_start(pmfs, "/", O_RDONLY, &tmp_obj, &mode, NULL);
	if (rc != 0 || !tmp_obj) {
		D_PRINT("pmfs lookup start renamed file failed\r\n");
		goto end;
	}
	struct dirent tmp_dir1 = {};
	struct dirent *tmp_dir2 = &tmp_dir1;

	rc = pmfs_readdir_start(pmfs, tmp_obj, &nr, tmp_dir2);
	if (rc != 0) {
		D_PRINT("pmfs readdir failed\r\n");
		goto end;
	}
	D_PRINT("\n--------------list--------------------------\n");
	for (i = 0; i < nr; i++) {
		D_PRINT("%s \t", tmp_dir2[i].d_name);
	}
	D_PRINT("\n");
	D_PRINT("---------------pmfs rename  a file start -----------------\r\n");
	rc =  pmfs_rename_start(pmfs, tmp_obj, "pmfs.c", "spdk.c");
	if (rc != 0) {
		D_PRINT("pmfs rename file pmfs.c to spdk.c is failed");
		goto end;
	}

	rc = pmfs_listdir_start(pmfs, NULL, &nr);
	if (rc != 0) {
		D_PRINT("pmfs listdir start failed\r\n");
		goto end;
	}

	rc = pmfs_lookup_start(pmfs, "/", O_RDONLY, &tmp_obj, &mode, NULL);
	if (rc != 0 || !tmp_obj) {
		D_PRINT("pmfs lookup start renamed file failed\r\n");
		goto end;
	}

	rc = pmfs_readdir_start(pmfs, tmp_obj, &nr, tmp_dir2);
	if (rc != 0) {
		D_PRINT("pmfs readdir failed\r\n");
		goto end;
	}

	D_PRINT("\n--------------list--------------------------\n");
	bool existed = false;

	for (i = 0; i < nr; i++) {
		if (strcmp(tmp_dir2[i].d_name, "pmfs.c") == 0)
			existed = true;
		 D_PRINT("%s \t", tmp_dir2[i].d_name);
	}
	D_PRINT("\n");
	D_PRINT("-----pmfs list %d files after rename---\r\n", nr);
	if (nr != record || existed == true) {
		D_PRINT("pmfs listdir after renamed failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs rename  a file done -----------------\r\n");
	D_PRINT("---------------pmfs open a file to write-----------------\r\n");
	rc = pmfs_open_start(pmfs, NULL, "spdk.c", S_IFREG, O_RDWR,
			     1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open file start failed\r\n");
		goto end;
	}
	D_PRINT("---------------pmfs start write  a file --------------\r\n");
	d_sg_list_t	user_sgl;
	daos_size_t	write_size = 2048;
	uint64_t crc32_tmp1 = 0xFFFFFFFFUL, crc32_tmp2 = 0xFFFFFFFFUL;

	/* using pmfs buffer render */
	rc = prepare_sgl(&user_sgl, write_size);
	if (rc != 0) {
		D_PRINT("Preaparing pmfs write sgl failed\r\n");
		goto end;
	}

	printf("--------------------------start write\r\n");
	rc = pmfs_write_start(pmfs, obj, &user_sgl, 10, &write_size);
	if (rc != 0) {
		D_PRINT("pmfs write file start failed\r\n");
		goto end;
	}

	for (i = 0; i < user_sgl.sg_nr; i++) {
		crc32_tmp1 = spdk_crc32c_update(user_sgl.sg_iovs[i].iov_buf,
						user_sgl.sg_iovs[i].iov_len,
						crc32_tmp1);
	}
	crc32_tmp1 =  crc32_tmp1 ^ 0xFFFFFFFFUL;
	D_PRINT("---------------pmfs write CRC=%lx, sg_nr = %d---------\r\n",
		crc32_tmp1, user_sgl.sg_nr);
	D_PRINT("---------------pmfs write file done -----------------\r\n");
	/* start to read file start offset and len */
	D_PRINT("---------------pmfs start read a file -----------------\r\n");

	daos_size_t read_size = 2048;

	rc = prepare_sgl(&user_sgl, read_size);

	rc = pmfs_read_start(pmfs, obj, &user_sgl, 10, &read_size);
	if (rc != 0) {
		D_PRINT("pmfs read file failed\r\n");
		goto end;
	}

	for (i = 0; i < user_sgl.sg_nr; i++) {
		crc32_tmp2 = spdk_crc32c_update(user_sgl.sg_iovs[i].iov_buf,
						user_sgl.sg_iovs[i].iov_len,
						crc32_tmp2);
	}
	crc32_tmp2 = crc32_tmp2 ^ 0xFFFFFFFFUL;

	D_PRINT("---------------pmfs read CRC=%lx-------------------\r\n",
		crc32_tmp2);
	if (crc32_tmp2 != crc32_tmp1) {
		D_PRINT("CRC check failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs read file done-----------------\r\n");

	D_PRINT("---------------pmfs start punch a file -------------\r\n");
	/* start to punch a file start offset and len */
	rc = pmfs_punch_start(pmfs, obj, 1000, 24);
	if (rc != 0) {
		D_PRINT("pmfs punch file failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs punch file done-----------------\r\n");

	/* start open a symbolic link */
	D_PRINT("---------------pmfs open a symbolic-----------------\r\n");
	rc = pmfs_open_start(pmfs, NULL, "pmfs.c", /*O_RDWR |*/ S_IFLNK,
			     O_RDWR |  O_CREAT, 1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open file start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs open a symbolic link done ------\r\n");
	/* start umount thread */
	rc = pmfs_umount_start(pmfs);
	if (rc != 0) {
		D_PRINT("pmfs umount start failed\r\n");
		goto end;
	}

	D_PRINT("---------------pmfs umount done---------------------\r\n");
	D_PRINT("test app thread start function ok\r\n");
end:
	return rc;
}

int
main(int argc, char **argv)
{
	int rc;
	struct pmfs_pool *pmfs_pool;
	struct pmfs *pmfs;

	daos_debug_init(DAOS_LOG_DEFAULT);

	/* Open this config with NVME and SCM size for user */
	pmfs_pool = pmfs_init_target_env(8ULL << 30, 2ULL << 30);
	if (!pmfs_pool) {
		D_ERROR("PMFS environment init failed\r\n");
		return -1;
	}

	pmfs_prepare_mounted_env_in_pool(pmfs_pool, &pmfs);

	/* Start to process cmds */
	rc = app_send_thread_test_pmfs_cmds_in_pool(pmfs);
	if (rc != 0) {
		D_PRINT("PMFS test failed\r\n");
	} else {
		D_PRINT("PMFS test success\r\n");
	}

	pmfs_fini_target_env();
	return 0;
}
