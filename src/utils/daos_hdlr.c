/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

/* daos_hdlr.c - resource and operation-specific handler functions
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)

#include <stdio.h>
#include <dirent.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/debug.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"

#include "daos_hdlr.h"

#define DUNS_XATTR_NAME		"user.daos"
#define DUNS_MAX_XATTR_LEN	170
#define DUNS_XATTR_FMT		"DAOS.%s://%36s/%36s/%s/%zu"

/* TODO: implement these pool op functions
 * int pool_list_cont_hdlr(struct cmd_args_s *ap);
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 * int pool_get_prop_hdlr(struct cmd_args_s *ap);
 * int pool_get_attr_hdlr(struct cmd_args_s *ap);
 * int pool_list_attrs_hdlr(struct cmd_args_s *ap);
 */

int
pool_query_hdlr(struct cmd_args_s *ap)
{
	daos_pool_info_t		 pinfo;
	struct daos_pool_space		*ps = &pinfo.pi_space;
	struct daos_rebuild_status	*rstat = &pinfo.pi_rebuild_st;
	int				 i;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_QUERY);

	rc = daos_pool_connect(ap->p_uuid, ap->group,
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pinfo.pi_bits = DPI_ALL;
	rc = daos_pool_query(ap->pool, NULL, &pinfo, NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool query failed: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}
	D_PRINT("Pool "DF_UUIDF", ntarget=%u, disabled=%u\n",
		DP_UUID(pinfo.pi_uuid), pinfo.pi_ntargets,
		pinfo.pi_ndisabled);

	D_PRINT("Pool space info:\n");
	D_PRINT("- Target(VOS) count:%d\n", ps->ps_ntargets);
	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		D_PRINT("- %s:\n",
			i == DAOS_MEDIA_SCM ? "SCM" : "NVMe");
		D_PRINT("  Total size: "DF_U64"\n",
			ps->ps_space.s_total[i]);
		D_PRINT("  Free: "DF_U64", min:"DF_U64", max:"DF_U64", "
			"mean:"DF_U64"\n", ps->ps_space.s_free[i],
			ps->ps_free_min[i], ps->ps_free_max[i],
			ps->ps_free_mean[i]);
	}

	if (rstat->rs_errno == 0) {
		char	*sstr;

		if (rstat->rs_version == 0)
			sstr = "idle";
		else if (rstat->rs_done)
			sstr = "done";
		else
			sstr = "busy";

		D_PRINT("Rebuild %s, "DF_U64" objs, "DF_U64" recs\n",
			sstr, rstat->rs_obj_nr, rstat->rs_rec_nr);
	} else {
		D_PRINT("Rebuild failed, rc=%d, status=%d\n",
			rc, rstat->rs_errno);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

/* TODO implement the following container op functions
 * all with signatures similar to this:
 * int cont_FN_hdlr(struct cmd_args_s *ap)
 *
 * cont_list_objs_hdlr()
 * int cont_query_hdlr()
 * int cont_stat_hdlr()
 * int cont_get_prop_hdlr()
 * int cont_set_prop_hdlr()
 * int cont_list_attrs_hdlr()
 * int cont_del_attr_hdlr()
 * int cont_get_attr_hdlr()
 * int cont_set_attr_hdlr()
 * int cont_create_snap_hdlr()
 * int cont_list_snaps_hdlr()
 * int cont_destroy_snap_hdlr()
 * int cont_rollback_hdlr()
 */

/* cont_create_hdlr() - create container by UUID */
int
cont_create_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	rc = daos_cont_create(ap->pool, ap->c_uuid, NULL, NULL);
	if (rc != 0)
		fprintf(stderr, "failed to create container: %d\n", rc);
	else
		fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
				DP_UUID(ap->c_uuid));

	return rc;
}

/* cont_create_uns_hdlr() - create container and link to
 * POSIX filesystem directory or HDF 5 file.
 */
int
cont_create_uns_hdlr(struct cmd_args_s *ap)
{
	char			pool[37], cont[37];
	char			oclass[10], type[10];
	char			str[DUNS_MAX_XATTR_LEN];
	int			len;
	int			rc;
	const int		RC_PRINT_HELP = 2;

	/* Path also requires container type be specified (for create) */
	ARGS_VERIFY_PATH_CREATE(ap, err_rc, rc = RC_PRINT_HELP);

	if (ap->type == DAOS_PROP_CO_LAYOUT_HDF5) {
		/** create a new file if HDF5 container */
		int fd;

		fd = open(ap->path, O_CREAT | O_EXCL,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (fd == -1) {
			D_ERROR("Failed to create file %s: %s\n",
				ap->path, strerror(errno));
			/** TODO - convert errno to rc */
			return -DER_INVAL;
		}
		close(fd);
	} else if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
		/** create a new directory if POSIX/MPI-IO container */
		rc = mkdir(ap->path,
			   S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH);
		if (rc == -1) {
			D_ERROR("Failed to create dir %s: %s\n",
				ap->path, strerror(errno));
			/** TODO - convert errno to rc */
			return -DER_INVAL;
		}
		D_INFO("created directory: %s\n", ap->path);
	} else {
		D_ERROR("Invalid container layout.\n");
		return -DER_INVAL;
	}

	uuid_unparse(ap->p_uuid, pool);
	daos_unparse_oclass(ap->oclass, oclass);
	daos_unparse_ctype(ap->type, type);

	D_INFO("unparsed pool: %s\n", pool);
	D_INFO("unparsed oclass: %s\n", oclass);
	D_INFO("unparsed ctype: %s\n", type);

	/** generate a random container uuid and try to create it */
	do {
		uuid_generate(ap->c_uuid);
		uuid_unparse(ap->c_uuid, cont);

		/** store the daos attributes in the path xattr */
		len = sprintf(str, DUNS_XATTR_FMT, type, pool, cont, oclass,
			      ap->chunk_size);
		if (len < 90) {
			D_ERROR("Failed to create xattr value\n");
			D_GOTO(err_link, rc = -DER_INVAL);
		}

		D_INFO("lxsetattr(path=%s, xattr_name=%s, str=%s, %d, 0)\n",
			ap->path, DUNS_XATTR_NAME, str, len + 1);

		rc = lsetxattr(ap->path, DUNS_XATTR_NAME, str,
			       len + 1, 0);
		if (rc) {
			D_ERROR("Failed to set DAOS xattr (rc = %d).\n", rc);
			D_GOTO(err_link, rc = -DER_INVAL);
		}

		rc = daos_cont_create(ap->pool, ap->c_uuid, NULL, NULL);
	} while (rc == -DER_EXIST);
	if (rc) {
		D_ERROR("Failed to create container (%d)\n", rc);
		D_GOTO(err_link, rc);
	}

	/*
	 * TODO: Add a container attribute to store the inode number (or Lustre
	 * FID) of the file.
	 */

	/** If this is a POSIX container, test create a DFS mount */
	if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
		daos_cont_info_t	co_info;
		dfs_t			*dfs;

		rc = daos_cont_open(ap->pool, ap->c_uuid, DAOS_COO_RW,
				    &ap->cont, &co_info, NULL);
		if (rc) {
			D_ERROR("Failed to open container (%d)\n", rc);
			D_GOTO(err_cont, rc = 1);
		}

		rc = dfs_mount(ap->pool, ap->cont, O_RDWR, &dfs);
		dfs_umount(dfs);
		daos_cont_close(ap->cont, NULL);
		if (rc) {
			D_ERROR("dfs_mount failed (%d)\n", rc);
			D_GOTO(err_cont, rc = 1);
		}
	}

	fprintf(stdout, "Successfully created %s container "DF_UUIDF
			" linked to %s\n", type, DP_UUID(ap->c_uuid),
			ap->path);

	return 0;

err_cont:
	daos_cont_destroy(ap->pool, ap->c_uuid, 1, NULL);
err_link:
	if (ap->type == DAOS_PROP_CO_LAYOUT_HDF5)
		unlink(ap->path);
	else if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX)
		rmdir(ap->path);
err_rc:
	return rc;
}

int
cont_query_hdlr(struct cmd_args_s *ap)
{
	char			oclass[10], type[10];
	int	rc = 0;

	if (ap->path != NULL) {
		/* cont_op_hdlr() already did resolve_by_path()
		 * all resulting fields should be populated
		 */
		assert(ap->type != DAOS_PROP_CO_LAYOUT_UNKOWN);
		assert(ap->oclass != DAOS_OC_UNKNOWN);
		assert(ap->chunk_size != 0);

		printf("DAOS Unified Namespace Attributes on path %s:\n",
			ap->path);
		daos_unparse_ctype(ap->type, type);
		daos_unparse_oclass(ap->oclass, oclass);
		printf("Container Type:\t%s\n", type);
		printf("Pool UUID:\t"DF_UUIDF"\n", DP_UUID(ap->p_uuid));
		printf("Container UUID:\t"DF_UUIDF"\n", DP_UUID(ap->c_uuid));
		printf("Object Class:\t%s\n", oclass);
		printf("Chunk Size:\t%zu\n", ap->chunk_size);
	}

	/* TODO: add container query API call and print */

	return rc;
}

int
cont_destroy_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	assert(ap->c_op == CONT_DESTROY);

	rc = daos_cont_destroy(ap->pool, ap->c_uuid, 1, NULL);
	if (rc != 0)
		fprintf(stderr, "failed to destroy container: %d\n", rc);
	else
		fprintf(stdout, "Successfully destroyed container "
				DF_UUIDF"\n", DP_UUID(ap->c_uuid));

	return rc;
}
