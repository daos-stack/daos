/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include "daos_uns.h"

#include <gurt/common.h>

/* Signal handler for SIGCHLD, it doesn't need to do anything, but it's
 * presence makes pselect() return EINTR in the dfuse_bg() function which
 * is used to detect abnormal exit.
 */
static void
noop_handler(int arg) {
}

static int bg_fd;

/* Send a message to the foreground thread */
static int
dfuse_send_to_fg(int rc)
{
	int nfd;
	int ret;

	if (bg_fd == 0)
		return -DER_SUCCESS;

	DFUSE_LOG_INFO("Sending %d to fg", rc);

	ret = write(bg_fd, &rc, sizeof(rc));

	close(bg_fd);
	bg_fd = 0;

	if (ret != sizeof(rc))
		return -DER_MISC;

	/* If the return code is non-zero then that means there's an issue so
	 * do not perform the rest of the operations in this function.
	 */
	if (rc != 0)
		return -DER_SUCCESS;

	ret = chdir("/");

	nfd = open("/dev/null", O_RDWR);
	if (nfd == -1)
		return -DER_MISC;

	dup2(nfd, STDIN_FILENO);
	dup2(nfd, STDOUT_FILENO);
	dup2(nfd, STDERR_FILENO);
	close(nfd);

	if (ret != 0)
		return -DER_MISC;

	DFUSE_LOG_INFO("Success");

	return -DER_SUCCESS;
}

/* Optionally go into the background
 *
 * It's not possible to simply call daemon() here as if we do that after
 * daos_init() then libfabric doesn't like it, and if we do it before
 * then there are no reporting of errors.  Instead, roll our own where
 * we create a socket pair, call fork(), and then communicate on the
 * socket pair to allow the foreground process to stay around until
 * the background process has completed.  Add in a check for SIGCHLD
 * from the background in case of abnormal exit to avoid deadlocking
 * the parent in this case.
 */
static int
dfuse_bg(struct dfuse_info *dfuse_info)
{
	sigset_t pset;
	fd_set read_set = {};
	int err;
	struct sigaction sa = {};
	pid_t child_pid;
	sigset_t sset;
	int rc;
	int di_spipe[2];

	rc = pipe(&di_spipe[0]);
	if (rc)
		return 1;

	sigemptyset(&sset);
	sigaddset(&sset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sset, NULL);

	child_pid = fork();
	if (child_pid == -1)
		return 1;

	if (child_pid == 0) {
		bg_fd = di_spipe[1];
		return 0;
	}

	sa.sa_handler = noop_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	sigemptyset(&pset);

	FD_ZERO(&read_set);
	FD_SET(di_spipe[0], &read_set);

	errno = 0;
	rc = pselect(di_spipe[0] + 1, &read_set, NULL, NULL, NULL, &pset);
	err = errno;

	if (err == EINTR) {
		printf("Child process died without reporting failure\n");
		exit(2);
	}

	if (FD_ISSET(di_spipe[0], &read_set)) {
		ssize_t b;
		int child_ret;

		b = read(di_spipe[0], &child_ret, sizeof(child_ret));
		if (b != sizeof(child_ret)) {
			printf("Read incorrect data %zd\n", b);
			exit(2);
		}
		if (child_ret) {
			printf("Exiting %d %s\n", child_ret,
			       d_errstr(child_ret));
			exit(-(child_ret + DER_ERR_GURT_BASE));
		} else {
			exit(0);
		}
	}

	printf("Socket is not set\n");
	exit(2);
}

static int
ll_loop_fn(struct dfuse_info *dfuse_info)
{
	int			ret;

	/*Blocking*/
	if (dfuse_info->di_threaded) {
		struct fuse_loop_config config = {.max_idle_threads = 10};

		ret = fuse_session_loop_mt(dfuse_info->di_session,
					   &config);
	} else {
		ret = fuse_session_loop(dfuse_info->di_session);
	}
	if (ret != 0)
		DFUSE_TRA_ERROR(dfuse_info,
				"Fuse loop exited with return code: %d", ret);

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

	dfuse_info->di_handle = fs_handle;

	dfuse_info->di_session = fuse_session_new(args,
						   flo,
						   sizeof(*flo),
						   fs_handle);
	if (!dfuse_info->di_session)
		goto cleanup;

	rc = fuse_session_mount(dfuse_info->di_session,
				dfuse_info->di_mountpoint);
	if (rc != 0)
		goto cleanup;

	fuse_opt_free_args(args);

	if (dfuse_send_to_fg(0) != -DER_SUCCESS)
		goto cleanup;

	rc = ll_loop_fn(dfuse_info);
	fuse_session_unmount(dfuse_info->di_session);
	if (rc)
		goto cleanup;

	return true;
cleanup:
	return false;
}

static void
show_help(char *name)
{
	printf("usage: %s -m=PATHSTR -s=RANKS\n"
		"\n"
		"	-m --mountpoint=PATHSTR	Mount point to use\n"
		"	-s --svc=RANKS		pool service replicas like 1,2,3\n"
		"	   --pool=UUID		pool UUID\n"
		"	   --container=UUID	container UUID\n"
		"	   --sys-name=STR	DAOS system name context for servers\n"
		"	-S --singlethreaded	Single threaded\n"
		"	-f --foreground		Run in foreground\n"
		"	   --enable-caching	Enable node-local caching (experimental)\n",
		name);
}

int
main(int argc, char **argv)
{
	struct dfuse_info	*dfuse_info = NULL;
	char			*svcl = NULL;
	struct dfuse_pool	*dfp = NULL;
	struct dfuse_pool	*dfpn;
	struct dfuse_dfs	*dfs = NULL;
	struct dfuse_dfs	*dfsn;
	struct duns_attr_t	duns_attr;
	uuid_t			tmp_uuid;
	char			c;
	int			ret = -DER_SUCCESS;
	int			rc;

	/* The 'daos' command uses -m as an alias for --scv however
	 * dfuse uses -m for --mountpoint so this is inconsistent
	 * but probably better than changing the meaning of the -m
	 * option here.h
	 */
	struct option long_options[] = {
		{"pool",		required_argument, 0, 'p'},
		{"container",		required_argument, 0, 'c'},
		{"svc",			required_argument, 0, 's'},
		{"sys-name",		required_argument, 0, 'G'},
		{"mountpoint",		required_argument, 0, 'm'},
		{"singlethread",	no_argument,	   0, 'S'},
		{"enable-caching",	no_argument,	   0, 'A'},
		{"disable-direct-io",	no_argument,	   0, 'D'},
		{"foreground",		no_argument,	   0, 'f'},
		{"help",		no_argument,	   0, 'h'},
		{0, 0, 0, 0}
	};

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		D_GOTO(out, ret = rc);

	D_ALLOC_PTR(dfuse_info);
	if (!dfuse_info)
		D_GOTO(out_debug, ret = -DER_NOMEM);

	D_INIT_LIST_HEAD(&dfuse_info->di_dfp_list);
	rc = D_MUTEX_INIT(&dfuse_info->di_lock, NULL);
	if (rc != -DER_SUCCESS)
		D_GOTO(out_debug, ret = rc);

	dfuse_info->di_threaded = true;
	dfuse_info->di_direct_io = true;

	while (1) {
		c = getopt_long(argc, argv, "s:m:Sfh",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			dfuse_info->di_pool = optarg;
			break;
		case 'c':
			dfuse_info->di_cont = optarg;
			break;
		case 's':
			svcl = optarg;
			break;
		case 'G':
			dfuse_info->di_group = optarg;
			break;
		case 'A':
			dfuse_info->di_caching = true;
			break;
		case 'm':
			dfuse_info->di_mountpoint = optarg;
			break;
		case 'S':
			dfuse_info->di_threaded = false;
			break;
		case 'f':
			dfuse_info->di_foreground = true;
			break;
		case 'D':
			dfuse_info->di_direct_io = false;
			break;
		case 'h':
			show_help(argv[0]);
			exit(0);
			break;
		case '?':
			show_help(argv[0]);
			exit(1);
			break;
		}
	}

	if (dfuse_info->di_caching && !dfuse_info->di_threaded) {
		printf("Caching not compatible with single-threaded mode\n");
		exit(1);
	}

	if (!dfuse_info->di_foreground && getenv("PMIX_RANK")) {
		DFUSE_TRA_WARNING(dfuse_info,
				  "Not running in background under orterun");
		dfuse_info->di_foreground = true;
	}

	if (!dfuse_info->di_mountpoint) {
		printf("Mountpoint is required\n");
		show_help(argv[0]);
		D_GOTO(out_debug, ret = -DER_NO_HDL);
	}

	/* Is this required, or can we assume some kind of default for
	 * this.
	 */
	if (!svcl) {
		printf("Svcl is required\n");
		show_help(argv[0]);
		D_GOTO(out_debug, ret = -DER_NO_HDL);
	}

	if (dfuse_info->di_pool) {
		if (uuid_parse(dfuse_info->di_pool, tmp_uuid) < 0) {
			printf("Invalid pool uuid\n");
			exit(1);
		}

		if (dfuse_info->di_cont) {
			if (uuid_parse(dfuse_info->di_cont, tmp_uuid) < 0) {
				printf("Invalid container uuid\n");
				exit(1);
			}
		}
	}

	if (!dfuse_info->di_foreground) {
		rc = dfuse_bg(dfuse_info);
		if (rc != 0) {
			printf("Failed to background\n");
			return 2;
		}
	}

	rc = daos_init();
	if (rc != -DER_SUCCESS)
		D_GOTO(out_debug, ret = rc);

	DFUSE_TRA_ROOT(dfuse_info, "dfuse_info");

	dfuse_info->di_svcl = daos_rank_list_parse(svcl, ":");
	if (dfuse_info->di_svcl == NULL) {
		printf("Invalid pool service rank list\n");
		D_GOTO(out_dfuse, ret = -DER_INVAL);
	}

	D_ALLOC_PTR(dfp);
	if (!dfp)
		D_GOTO(out_svcl, ret = -DER_NOMEM);

	DFUSE_TRA_UP(dfp, dfuse_info, "dfp");
	D_INIT_LIST_HEAD(&dfp->dfp_dfs_list);

	d_list_add(&dfp->dfp_list, &dfuse_info->di_dfp_list);

	D_ALLOC_PTR(dfs);
	if (!dfs)
		D_GOTO(out_dfs, ret = -DER_NOMEM);

	if (dfuse_info->di_caching)
		dfs->dfs_attr_timeout = 5;

	d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);

	dfs->dfs_dfp = dfp;

	DFUSE_TRA_UP(dfs, dfp, "dfs");

	rc = duns_resolve_path(dfuse_info->di_mountpoint, &duns_attr);
	DFUSE_TRA_INFO(dfuse_info, "duns_resolve_path() returned %d %s",
		       rc, strerror(rc));
	if (rc == 0) {
		if (dfuse_info->di_pool) {
			printf("UNS configured on mount point but pool provided\n");
			D_GOTO(out_dfs, ret = -DER_INVAL);
		}
		uuid_copy(dfp->dfp_pool, duns_attr.da_puuid);
		uuid_copy(dfs->dfs_cont, duns_attr.da_cuuid);
	} else if (rc == ENODATA || rc == ENOTSUP) {
		if (dfuse_info->di_pool) {
			if (uuid_parse(dfuse_info->di_pool,
				       dfp->dfp_pool) < 0) {
				printf("Invalid pool uuid\n");
				D_GOTO(out_dfs, ret = -DER_INVAL);
			}
			if (dfuse_info->di_cont) {
				if (uuid_parse(dfuse_info->di_cont,
					       dfs->dfs_cont) < 0) {
					printf("Invalid container uuid\n");
					D_GOTO(out_dfs, ret = -DER_INVAL);
				}
			}
		}
	} else if (rc == ENOENT) {
		printf("Mount point does not exist\n");
		D_GOTO(out_dfs, ret = daos_errno2der(rc));
	} else {
		/* Other errors from DUNS, it should have logged them already */
		D_GOTO(out_dfs, ret = daos_errno2der(rc));
	}

	if (uuid_is_null(dfp->dfp_pool) == 0) {
		/** Connect to DAOS pool */
		rc = daos_pool_connect(dfp->dfp_pool, dfuse_info->di_group,
				       dfuse_info->di_svcl, DAOS_PC_RW,
				       &dfp->dfp_poh, &dfp->dfp_pool_info,
				       NULL);
		if (rc != -DER_SUCCESS) {
			printf("Failed to connect to pool (%d)\n", rc);
			D_GOTO(out_dfs, 0);
		}

		if (uuid_is_null(dfs->dfs_cont) == 0) {
			/** Try to open the DAOS container (the mountpoint) */
			rc = daos_cont_open(dfp->dfp_poh, dfs->dfs_cont,
					    DAOS_COO_RW, &dfs->dfs_coh,
					    &dfs->dfs_co_info, NULL);
			if (rc) {
				printf("Failed container open (%d)\n", rc);
				D_GOTO(out_dfs, ret = rc);
			}

			rc = dfs_mount(dfp->dfp_poh, dfs->dfs_coh, O_RDWR,
				       &dfs->dfs_ns);
			if (rc) {
				daos_cont_close(dfs->dfs_coh, NULL);
				printf("dfs_mount failed (%d)\n", rc);
				D_GOTO(out_dfs, ret = rc);
			}
			dfs->dfs_ops = &dfuse_dfs_ops;
		} else {
			dfs->dfs_ops = &dfuse_cont_ops;
		}
	} else {
		dfs->dfs_ops = &dfuse_pool_ops;
	}

	dfuse_dfs_init(dfs, NULL);

	rc = dfuse_start(dfuse_info, dfs);
	if (rc != -DER_SUCCESS)
		D_GOTO(out_dfs, ret = rc);

	/* Remove all inodes from the hash tables */
	ret = dfuse_destroy_fuse(dfuse_info->di_handle);

	fuse_session_destroy(dfuse_info->di_session);

out_dfs:

	d_list_for_each_entry_safe(dfp, dfpn, &dfuse_info->di_dfp_list,
				   dfp_list) {
		DFUSE_TRA_ERROR(dfp, "DFP left at the end");
		d_list_for_each_entry_safe(dfs, dfsn, &dfp->dfp_dfs_list,
					   dfs_list) {
			DFUSE_TRA_ERROR(dfs, "DFS left at the end");
			if (!daos_handle_is_inval(dfs->dfs_coh)) {
				rc = dfs_umount(dfs->dfs_ns);
				if (rc != 0)
					DFUSE_TRA_ERROR(dfs,
							"dfs_umount() failed (%d)",
							rc);

				rc = daos_cont_close(dfs->dfs_coh, NULL);
				if (rc != -DER_SUCCESS) {
					DFUSE_TRA_ERROR(dfs,
							"daos_cont_close() failed: (%d)",
							rc);
				}
			}
			D_MUTEX_DESTROY(&dfs->dfs_read_mutex);
			DFUSE_TRA_DOWN(dfs);
			D_FREE(dfs);
		}

		if (!daos_handle_is_inval(dfp->dfp_poh)) {
			rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
			if (rc != -DER_SUCCESS) {
				DFUSE_TRA_ERROR(dfp,
						"daos_pool_disconnect() failed: (%d)",
						rc);
			}
		}
		DFUSE_TRA_DOWN(dfp);
		D_FREE(dfp);
	}
out_svcl:
	d_rank_list_free(dfuse_info->di_svcl);
out_dfuse:
	DFUSE_TRA_DOWN(dfuse_info);
	D_MUTEX_DESTROY(&dfuse_info->di_lock);
	daos_fini();
out_debug:
	D_FREE(dfuse_info);
	DFUSE_LOG_INFO("Exiting with status %d", ret);
	daos_debug_fini();
out:
	dfuse_send_to_fg(ret);

	/* Convert CaRT error numbers to something that can be returned to the
	 * user.  This needs to be less than 256 so only works for CaRT, not
	 * DAOS error numbers.
	 */

	if (ret)
		return -(ret + DER_ERR_GURT_BASE);
	else
		return 0;
}
