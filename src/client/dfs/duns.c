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
#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"

#define DUNS_XATTR_NAME		"user.daos"
#define DUNS_MAX_XATTR_LEN	150
#define DUNS_XATTR_FMT		"DAOS.%s://%36s/%36s/%s/"

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
		fprintf(stderr, "Invalid DAOS xattr format (%s).\n", str);
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
	daos_parse_oclass(t, &attr->da_oclass);

	return 0;
}

int
duns_link_path(const char *path, struct duns_attr_t attr)
{
	char			*svcl_str, *group;
	d_rank_list_t		*svcl = NULL;
	daos_handle_t		poh;
	daos_pool_info_t	pool_info;
	char			pool[37], cont[37];
	char			oclass[10], type[10];
	char			str[DUNS_MAX_XATTR_LEN];
	int			len;
	int			rc;

	if (path == NULL) {
		D_ERROR("Invalid path\n");
		return -DER_INVAL;
	}

	/*
	 * MSC - for now SVCL and group need to be passed through env
	 * variable. they shouldn't be required later.
	 */
	svcl_str = getenv("DAOS_SVCL");
	if (svcl_str == NULL) {
		fprintf(stderr, "missing pool service rank list\n");
		return -DER_INVAL;
	}

	svcl = daos_rank_list_parse(svcl_str, ":");
	if (svcl == NULL) {
		fprintf(stderr, "Invalid pool service rank list\n");
		return -DER_INVAL;
	}

	group = getenv("DAOS_GROUP");

	if (attr.da_type == DAOS_PROP_CO_LAYOUT_HDF5) {
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
	} else if (attr.da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
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
	rc = daos_pool_connect(attr.da_puuid, group, svcl, DAOS_PC_RW, &poh,
			       &pool_info, NULL);
	if (rc) {
		D_ERROR("Failed to connect to pool (%d)\n", rc);
		D_GOTO(err_link, rc);
	}

	/** generate a random container uuid and try to create it */
	do {
		uuid_generate(attr.da_cuuid);
		rc = daos_cont_create(poh, attr.da_cuuid, NULL, NULL);
	} while (rc == -DER_EXIST);
	if (rc) {
		D_ERROR("Failed to create container (%d)\n", rc);
		D_GOTO(err_pool, rc);
	}

	/** If this is a POSIX container, create DFS mount */
	if (attr.da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		daos_handle_t		coh;
		daos_cont_info_t	co_info;
		dfs_t			*dfs;

		rc = daos_cont_open(poh, attr.da_cuuid, DAOS_COO_RW, &coh,
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

	/** store the daos attributes in the path xattr */
	uuid_unparse(attr.da_puuid, pool);
	uuid_unparse(attr.da_cuuid, cont);
	daos_unparse_oclass(attr.da_oclass, oclass);
	daos_unparse_ctype(attr.da_type, type);

	len = sprintf(str, DUNS_XATTR_FMT, type, pool, cont, oclass);
	if (len < 90) {
		D_ERROR("Failed to create xattr value\n");
		D_GOTO(err_cont, rc = -DER_INVAL);
	}

	rc = lsetxattr(path, DUNS_XATTR_NAME, str, len + 1, 0);
	if (rc) {
		D_ERROR("Failed to set DAOS xattr (rc = %d).\n", rc);
		D_GOTO(err_cont, rc = -DER_INVAL);
	}

	daos_pool_disconnect(poh, NULL);
	return rc;

err_cont:
	daos_cont_destroy(poh, attr.da_cuuid, 1, NULL);
err_pool:
	daos_pool_disconnect(poh, NULL);
err_link:
	if (attr.da_type == DAOS_PROP_CO_LAYOUT_HDF5)
		unlink(path);
	else if (attr.da_type == DAOS_PROP_CO_LAYOUT_POSIX)
		rmdir(path);
	return rc;
}
