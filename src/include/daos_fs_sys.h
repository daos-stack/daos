/**
 * (C) Copyright 2018-2021 Intel Corporation.
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

/* TODO evaluate includes */
#include <libgen.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/checksum.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/container.h>
#include <daos/array.h>

#include "daos.h"
#include "daos_fs.h"

#define DFS_SYS_NO_CACHE 1

/** struct holding attributes for the dfs_sys calls */
typedef struct dfs_sys dfs_sys_t;

/**
 * Mount a file system with dfs_mount and optionally initialize the hash.
 */
int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int flags, int sys_flags,
	      dfs_sys_t **_dfs_sys);

/**
 * Unmount a file system with dfs_mount and destroy the hash.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys);

int
dfs_sys_access(dfs_sys_t *dfs_sys, const char* path, int amode);

int
dfs_sys_faccessat(dfs_sys_t *dfs_sys, int dirfd, const char* path, int amode,
		  int flags);

int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char* path, mode_t mode);

int
dfs_sys_utimensat(dfs_sys_t *dfs_sys, const char *pathname,
		  const struct timespec times[2], int flags);

int
dfs_sys_lstat(dfs_sys_t *dfs_sys, const char* path, struct stat* buf);

int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char* path, struct stat* buf);

int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char* path, mode_t mode, dev_t dev);

/**
 * TODO - Many more functions:
 * mknod
 * listxattr
 * llistxattr
 * getxattr
 * lgetxattr
 * lsetxattr
 * readlink
 * symlink
 * open
 * close
 * lseek
 * read
 * pread
 * write
 * pwrite
 * truncate
 * ftruncate
 * unlink
 * mkdir
 * opendir
 * closedir
 */
