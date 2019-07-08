/**
 * (C) Copyright 2019 Intel Corporation.
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
/**
 * DAOS Unified namespace functionality.
 */

#define D_LOGFAC	DD_FAC(duns)

#include <dirent.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos/common.h>
#include <daos/object.h>
#include "daos_types.h"
#include "daos.h"
#include "daos_fs.h"
#include "daos_uns.h"

#define DUNS_XATTR_NAME		"user.daos"
#define DUNS_MAX_XATTR_LEN	170
#define DUNS_XATTR_FMT		"DAOS.%s://%36s/%36s/%s/%zu"

int
duns_resolve_path(const char *path, struct duns_attr_t *attr)
{
	ssize_t	s;
	char	str[DUNS_MAX_XATTR_LEN];
	char	*saveptr, *t;
	int	rc;

	s = lgetxattr(path, DUNS_XATTR_NAME, &str, DUNS_MAX_XATTR_LEN);
	if (s < 0 || s > DUNS_MAX_XATTR_LEN) {
		if (s == ENOTSUP)
			D_ERROR("Path is not in a filesystem that supports the"
				" DAOS unified namespace\n");
		else if (s == ENODATA)
			D_ERROR("Path does not represent a DAOS link\n");
		else if (s > DUNS_MAX_XATTR_LEN)
			D_ERROR("Invalid xattr length\n");
		else
			D_ERROR("Invalid DAOS unified namespace xattr\n");
		return -DER_INVAL;
	}

	t = strtok_r(str, ".", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS xattr format (%s).\n", str);
		return -DER_INVAL;
	}

	t = strtok_r(NULL, ":", &saveptr);
	daos_parse_ctype(t, &attr->da_type);
	if (attr->da_type == DAOS_PROP_CO_LAYOUT_UNKOWN) {
		D_ERROR("Invalid DAOS xattr format: Container layout cannot be"
			" unknown\n");
		return -DER_INVAL;
	}

	t = strtok_r(NULL, "/", &saveptr);
	rc = uuid_parse(t, attr->da_puuid);
	if (rc) {
		D_ERROR("Invalid DAOS xattr format: pool UUID cannot be"
			" parsed\n");
		return -DER_INVAL;
	}

	t = strtok_r(NULL, "/", &saveptr);
	rc = uuid_parse(t, attr->da_cuuid);
	if (rc) {
		D_ERROR("Invalid DAOS xattr format: container UUID cannot be"
			" parsed\n");
		return -DER_INVAL;
	}

	t = strtok_r(NULL, "/", &saveptr);
	daos_oclass_str2id(t, &attr->da_oclass);

	t = strtok_r(NULL, "/", &saveptr);
	attr->da_chunk_size = strtoull(t, NULL, 10);

	return 0;
}

/*
 * TODO: should detect backend file system and if Lustre, use special LOV/LVM EA
 * instead of regular extended attribute.
 */
int
duns_link_path(const char *path, const char *sysname,
	       d_rank_list_t *svcl, struct duns_attr_t *attrp)
{
	daos_handle_t		poh;
	daos_pool_info_t	pool_info;
	char			pool[37], cont[37];
	char			oclass[10], type[10];
	char			str[DUNS_MAX_XATTR_LEN];
	int			len;
	int			try_multiple = 1;		/* boolean */
	int			rc;

	if (path == NULL) {
		D_ERROR("Invalid path\n");
		return -DER_INVAL;
	}

	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_HDF5) {
		/** create a new file if HDF5 container */
		int fd;

		fd = open(path, O_CREAT | O_EXCL,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (fd == -1) {
			D_ERROR("Failed to create file %s: %s\n", path,
				strerror(errno));
			/** TODO - convert errno to rc */
			return -DER_INVAL;
		}
		close(fd);
	} else if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		/** create a new directory if POSIX/MPI-IO container */
		rc = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH);
		if (rc == -1) {
			D_ERROR("Failed to create dir %s: %s\n",
				path, strerror(errno));
			/** TODO - convert errno to rc */
			return -DER_INVAL;
		}
	} else {
		D_ERROR("Invalid container layout.\n");
		return -DER_INVAL;
	}

	/** Connect to the pool. */
	rc = daos_pool_connect(attrp->da_puuid, sysname, svcl, DAOS_PC_RW,
			       &poh, &pool_info, NULL);
	if (rc) {
		D_ERROR("Failed to connect to pool (%d)\n", rc);
		D_GOTO(err_link, rc);
	}

	uuid_unparse(attrp->da_puuid, pool);
	daos_oclass_id2str(attrp->da_oclass, oclass);
	daos_unparse_ctype(attrp->da_type, type);

	/* create container with specified container uuid (try_multiple=0)
	 * or a generated random container uuid (try_multiple!=0).
	 */
	if (!uuid_is_null(attrp->da_cuuid)) {
		try_multiple = 0;
		uuid_unparse(attrp->da_cuuid, cont);
		D_INFO("try create once with provided container UUID: %36s\n",
			cont);
	}
	do {
		if (try_multiple) {
			uuid_generate(attrp->da_cuuid);
			uuid_unparse(attrp->da_cuuid, cont);
		}

		/** store the daos attributes in the path xattr */
		len = sprintf(str, DUNS_XATTR_FMT, type, pool, cont, oclass,
			      attrp->da_chunk_size);
		if (len < 90) {
			D_ERROR("Failed to create xattr value\n");
			D_GOTO(err_pool, rc = -DER_INVAL);
		}

		rc = lsetxattr(path, DUNS_XATTR_NAME, str, len + 1, 0);
		if (rc) {
			D_ERROR("Failed to set DAOS xattr (rc = %d).\n", rc);
			D_GOTO(err_pool, rc = -DER_INVAL);
		}

		rc = daos_cont_create(poh, attrp->da_cuuid, NULL, NULL);
	} while ((rc == -DER_EXIST) && try_multiple);
	if (rc) {
		D_ERROR("Failed to create container (%d)\n", rc);
		D_GOTO(err_pool, rc);
	}

	/*
	 * TODO: Add a container attribute to store the inode number (or Lustre
	 * FID) of the file.
	 */

	/** If this is a POSIX container, create DFS mount */
	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		daos_handle_t		coh;
		daos_cont_info_t	co_info;
		dfs_t			*dfs;

		rc = daos_cont_open(poh, attrp->da_cuuid, DAOS_COO_RW, &coh,
				    &co_info, NULL);
		if (rc) {
			D_ERROR("Failed to open container (%d)\n", rc);
			D_GOTO(err_cont, rc = 1);
		}

		rc = dfs_mount(poh, coh, O_RDWR, &dfs);
		dfs_umount(dfs);
		daos_cont_close(coh, NULL);
		if (rc) {
			D_ERROR("dfs_mount failed (%d)\n", rc);
			D_GOTO(err_cont, rc = 1);
		}
	}

	daos_pool_disconnect(poh, NULL);
	return rc;

err_cont:
	daos_cont_destroy(poh, attrp->da_cuuid, 1, NULL);
err_pool:
	daos_pool_disconnect(poh, NULL);
err_link:
	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_HDF5)
		unlink(path);
	else if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX)
		rmdir(path);
	return rc;
}
