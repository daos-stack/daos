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

#include <errno.h>
#include <getopt.h>
#include <dlfcn.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#define D_LOGFAC DD_FAC(dfuse)

#include "dfuse.h"

#include "daos_fs.h"
#include "daos_api.h"

#include <gurt/common.h>

static int
ll_loop_fn(struct dfuse_info *dfuse_info)
{
	int			ret;

	/*Blocking*/
	if (dfuse_info->dfi_dfd.threaded) {
		struct fuse_loop_config config = {.max_idle_threads = 10};

		ret = fuse_session_loop_mt(dfuse_info->dfi_session,
					   &config);
	} else {
		ret = fuse_session_loop(dfuse_info->dfi_session);
	}
	if (ret != 0)
		DFUSE_LOG_ERROR("Fuse loop exited with return code: %d", ret);

	return ret;
}

/*
 * Creates a fuse filesystem for any plugin that needs one.
 *
 * Should be called from the post_start plugin callback and creates
 * a filesystem.
 * Returns 0 on success, or non-zero on error.
 */
bool
dfuse_launch_fuse(struct dfuse_info *dfuse_info,
		  struct fuse_lowlevel_ops *flo,
		  struct fuse_args *args,
		  struct dfuse_projection_info *fs_handle)
{
	int rc;

	dfuse_info->dfi_handle = fs_handle;

	dfuse_info->dfi_session = fuse_session_new(args,
						   flo,
						   sizeof(*flo),
						   fs_handle);
	if (!dfuse_info->dfi_session)
		goto cleanup;

	rc = fuse_session_mount(dfuse_info->dfi_session,
				dfuse_info->dfi_dfd.mountpoint);
	if (rc != 0) {
		goto cleanup;
	}
	fs_handle->session = dfuse_info->dfi_session;

	fuse_opt_free_args(args);

	rc = ll_loop_fn(dfuse_info);
	fuse_session_unmount(dfuse_info->dfi_session);
	if (rc) {
		goto cleanup;
	}

	return true;
cleanup:
	return false;
}

int
main(int argc, char **argv)
{
	struct dfuse_info	*dfuse_info = NULL;
	struct dfuse_data	*dfuse_fs;
	d_rank_list_t		*svcl;
	uuid_t			pool_uuid;
	uuid_t			co_uuid;
	daos_pool_info_t	pool_info;
	daos_cont_info_t	co_info;
	daos_handle_t		poh;
	daos_handle_t		coh;
	dfs_t			*dfs;
	char			c;
	int			ret = -DER_SUCCESS;
	int			rc;

	struct option long_options[] = {
		{"pool",		required_argument, 0, 'p'},
		{"container",		required_argument, 0, 'c'},
		{"svcl",		required_argument, 0, 's'},
		{"group",		required_argument, 0, 'g'},
		{"mountpoint",		required_argument, 0, 'm'},
		{"singlethread",	no_argument,	   0, 'S'},
		{"help",		no_argument,	   0, 'h'},
		{"prefix",		required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	rc = daos_debug_init(NULL);
	if (rc != -DER_SUCCESS) {
		D_GOTO(out, ret = rc);
	}

	D_ALLOC_PTR(dfuse_info);
	if (!dfuse_info)
		D_GOTO(out, ret = -DER_NOMEM);

	dfuse_fs = &dfuse_info->dfi_dfd;
	dfuse_fs->threaded = true;

	while (1) {
		c = getopt_long(argc, argv, "p:c:s:g:m:Sh",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			dfuse_fs->pool = optarg;
			break;
		case 'c':
			dfuse_fs->cont = optarg;
			break;
		case 's':
			dfuse_fs->svcl = optarg;
			break;
		case 'g':
			dfuse_fs->group = optarg;
			break;
		case 'm':
			dfuse_fs->mountpoint = optarg;
			break;
		case 'S':
			dfuse_fs->threaded = false;
			break;
		case 'h':
			exit(0);
			break;
		case '?':
			exit(1);
			break;
		}
	}

	if (!dfuse_fs->pool) {
		DFUSE_LOG_ERROR("Pool is required");
		D_GOTO(out, ret = -DER_INVAL);
	}

	if (!dfuse_fs->cont) {
		DFUSE_LOG_ERROR("Container is required");
		D_GOTO(out, ret = -DER_INVAL);
	}

	if (!dfuse_fs->mountpoint) {
		DFUSE_LOG_ERROR("Mountpoint is required");
		D_GOTO(out, ret = -DER_INVAL);
	}

	DFUSE_TRA_ROOT(dfuse_info, "dfuse_info");

	if (uuid_parse(dfuse_fs->pool, pool_uuid) < 0) {
		DFUSE_LOG_ERROR("Invalid pool uuid");
		D_GOTO(out, ret = -DER_INVAL);
	}

	if (uuid_parse(dfuse_fs->cont, co_uuid) < 0) {
		DFUSE_LOG_ERROR("Invalid container uuid");
		D_GOTO(out, ret = -DER_INVAL);
	}

	svcl = daos_rank_list_parse(dfuse_fs->svcl, ":");
	if (svcl == NULL) {
		DFUSE_LOG_ERROR("Invalid pool service rank list");
		D_GOTO(out, ret = -DER_INVAL);
	}

	rc = daos_init();
	if (rc != -DER_SUCCESS) {
		D_GOTO(out, ret = rc);
	}

	/** Connect to DAOS pool */
	rc = daos_pool_connect(pool_uuid, dfuse_fs->group, svcl, DAOS_PC_RW,
			       &poh, &pool_info, NULL);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("Failed to connect to pool (%d)", rc);
		D_GOTO(out, 0);
	}

	/** Try to open the DAOS container first (the mountpoint) */
	rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, &co_info, NULL);
	/* If NOEXIST we create it */
	if (rc == -DER_NONEXIST) {
		rc = daos_cont_create(poh, co_uuid, NULL, NULL);
		if (rc == 0) {
			rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW,
					    &coh, &co_info, NULL);
		}
	}
	if (rc) {
		DFUSE_LOG_ERROR("Failed container open (%d)", rc);
		D_GOTO(out_pool, 0);
	}

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("dfs_mount failed (%d)", rc);
		D_GOTO(out_cont, 0);
	}

	rc = dfuse_start(dfuse_info, dfs);
	if (rc != -DER_SUCCESS) {
		D_GOTO(out_cont, ret = rc);
	}

	ret = dfuse_destroy_fuse(dfuse_info->dfi_handle);

	fuse_session_destroy(dfuse_info->dfi_session);

out_cont:
	daos_cont_close(coh, NULL);
out_pool:
	daos_pool_disconnect(poh, NULL);
out:

	DFUSE_TRA_DOWN(dfuse_info);
	DFUSE_LOG_INFO("Exiting with status %d", ret);
	D_FREE(dfuse_info);

	/* Convert CaRT error numbers to something that can be returned to the
	 * user.  This needs to be less than 256 so only works for CaRT, not
	 * DAOS error numbers.
	 */
	return -(ret + DER_ERR_GURT_BASE);
}
