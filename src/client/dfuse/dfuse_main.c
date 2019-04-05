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

#include <cart/api.h>
#include <gurt/common.h>
#include <signal.h>

/* Add a NO-OP signal handler.  This doesn't do anything other than
 * interrupt the fuse leader thread if it's not already awake to
 * reap the other fuse threads.  Initially this was tried with a
 * no-op function however that didn't appear to work so perhaps the
 * compiler was optimising it away.
 */
static int
signal_word;

static void
dfuse_signal_poke(int signal)
{
	signal_word++;
}

static void
*ll_loop_fn(void *args)
{
	int			ret;
	struct fs_info		*info = args;
	const struct sigaction	act = {.sa_handler = dfuse_signal_poke};

	D_MUTEX_LOCK(&info->fsi_lock);
	info->fsi_running = true;
	D_MUTEX_UNLOCK(&info->fsi_lock);

	sigaction(SIGUSR1, &act, NULL);

	/*Blocking*/
	if (info->fsi_mt) {
		struct fuse_loop_config config = {.max_idle_threads = 10};

		ret = fuse_session_loop_mt(info->fsi_session, &config);
	} else {
		ret = fuse_session_loop(info->fsi_session);
	}
	if (ret != 0)
		DFUSE_LOG_ERROR("Fuse loop exited with return code: %d", ret);

	DFUSE_LOG_DEBUG("%p fuse loop completed %d", info, ret);

	D_MUTEX_LOCK(&info->fsi_lock);
	info->fsi_running = false;
	D_MUTEX_UNLOCK(&info->fsi_lock);
	return (void *)(uintptr_t)ret;
}

/*
 * Creates a fuse filesystem for any plugin that needs one.
 *
 * Should be called from the post_start plugin callback and creates
 * a filesystem.
 * Returns 0 on success, or non-zero on error.
 */
bool
cnss_register_fuse(struct cnss_info *cnss_info,
		   struct fuse_lowlevel_ops *flo,
		   struct fuse_args *args,
		   const char *mnt,
		   bool threaded,
		   void *private_data,
		   struct fuse_session **sessionp)
{
	struct fs_info	*info = &cnss_info->ci_fsinfo;
	int		rc;

	errno = 0;
	rc = mkdir(mnt, 0755);
	if (rc != 0 && errno != EEXIST) {
		return false;
	}

	info->fsi_mt = threaded;

	/* TODO: The plugin should provide the sub-directory only, not the
	 * entire mount point and this function should add the cnss_prefix
	 */
	D_STRNDUP(info->fsi_mnt, mnt, 1024);
	if (!info->fsi_mnt)
		goto cleanup_no_mutex;

	rc = D_MUTEX_INIT(&info->fsi_lock, NULL);
	if (rc != -DER_SUCCESS) {
		goto cleanup_no_mutex;
	}

	info->fsi_handle = private_data;

	info->fsi_session = fuse_session_new(args,
					     flo,
					     sizeof(*flo),
					     private_data);
	if (!info->fsi_session)
		goto cleanup;

	rc = fuse_session_mount(info->fsi_session, info->fsi_mnt);
	if (rc != 0) {
		goto cleanup;
	}
	*sessionp = info->fsi_session;

	fuse_opt_free_args(args);

	rc = pthread_create(&info->fsi_thread, NULL,
			    ll_loop_fn, info);
	if (rc) {
		fuse_session_unmount(info->fsi_session);
		goto cleanup;
	}

	return true;
cleanup:
	pthread_mutex_destroy(&info->fsi_lock);
cleanup_no_mutex:
	D_FREE(info->fsi_mnt);
	D_FREE(info);

	return false;
}

static int
cnss_stop_fuse(struct fs_info *info)
{
	struct timespec	wait_time;
	void		*rcp = NULL;
	int		rc;

	D_MUTEX_LOCK(&info->fsi_lock);

	/* Add a short delay to allow the flush time to work, by sleeping
	 * here it allows time for the forget calls to work through from
	 * the kernel.
	 *
	 * A better approach would be to add counters for open inodes and
	 * check that here instead.
	 */
	sleep(1);

	if (info->fsi_running) {

		/*
		 * If the FUSE thread is in the filesystem servicing requests
		 * then set the exit flag and send it a dummy operation to wake
		 * it up.  Drop the mutext before calling setxattr() as that
		 * will cause I/O activity and loop_fn() to deadlock with this
		 * function.
		 */
		fuse_session_exit(info->fsi_session);
		fuse_session_unmount(info->fsi_session);
	}

	D_MUTEX_UNLOCK(&info->fsi_lock);

	clock_gettime(CLOCK_REALTIME, &wait_time);

	do {

		wait_time.tv_sec++;

		rc = pthread_timedjoin_np(info->fsi_thread, &rcp, &wait_time);

		if (rc == ETIMEDOUT) {
			if (!fuse_session_exited(info->fsi_session))
				DFUSE_TRA_INFO(info, "Session still running");

			pthread_kill(info->fsi_thread, SIGUSR1);
		}

	} while (rc == ETIMEDOUT);

	if (rc)
		DFUSE_TRA_ERROR(info, "Final join returned %d:%s",
				rc, strerror(rc));

	rc = pthread_mutex_destroy(&info->fsi_lock);
	if (rc != 0)
		DFUSE_TRA_ERROR(info,
				"Failed to destroy lock %d:%s",
				rc, strerror(rc));

	rc = (uintptr_t)rcp;

	{
		int rcf = dfuse_deregister_fuse(info->fsi_handle);

		if (rcf)
			rc = rcf;
	}

	fuse_session_destroy(info->fsi_session);
	DFUSE_TRA_INFO(info, "session destroyed");

	return rc;
}

static void
show_help(const char *prog)
{
	printf("I/O Forwarding Compute Node System Services\n");
	printf("\n");
	printf("Usage: %s [OPTION] ...\n", prog);
	printf("\n");
	printf("\t-h, --help\tThis help text\n");
	printf("\t-v, --version\tShow version\n");
	printf("\t-p, --prefix\tPath to the CNSS Working directory.\n"
		"\t\t\tThis may also be set via the CNSS_PREFIX"
		" environment variable.\n"
		"\n");
}

int
main(int argc, char **argv)
{
	char			*cnss = "CNSS";
	const char		*prefix = NULL;
	struct cnss_info	*cnss_info;
	int			ret;
	int			rc;

	rc = daos_debug_init(NULL);
	if (rc != -DER_SUCCESS) {
		D_GOTO(out, ret = rc);
	}

	while (1) {
		static struct option long_options[] = {
			{"help", no_argument, 0, 'h'},
			{"prefix", required_argument, 0, 'p'},
			{0, 0, 0, 0}
		};
		char c = getopt_long(argc, argv, "hvp:", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'h':
			show_help(argv[0]);
			exit(0);
			break;

		case 'p':
			prefix = optarg;
			break;
		case '?':
			exit(1);
			break;
		}
	}

	if (prefix == NULL) {
		prefix = getenv("CNSS_PREFIX");
	}
	if (prefix == NULL) {
		DFUSE_LOG_ERROR("CNSS prefix is required");
		D_GOTO(out, ret = -DER_INVAL);
	}

	/* chdir to the cnss_prefix, as that allows all future I/O access
	 * to use relative paths.
	 */
	ret = chdir(prefix);
	if (ret != 0) {
		DFUSE_LOG_ERROR("Could not chdir to CNSS_PREFIX");
		D_GOTO(out, ret = -DER_INVAL);
	}

	D_ALLOC_PTR(cnss_info);
	if (!cnss_info)
		D_GOTO(shutdown_log, ret = -DER_NOMEM);

	DFUSE_TRA_ROOT(cnss_info, "cnss_info");

	cnss_info->dfuse_state = dfuse_plugin_init();

	/*initialize CaRT*/
	ret = crt_init(cnss, 0);
	if (ret) {
		DFUSE_TRA_ERROR(cnss_info,
				"crt_init failed with ret = %d", ret);
		D_GOTO(shutdown_ctrl_fs, 0);
	}

	/* Call start for each plugin which should perform node-local
	 * operations only.  Plugins can choose to disable themselves
	 * at this point.
	 */
	dfuse_reg(cnss_info->dfuse_state, cnss_info);

	dfuse_post_start(cnss_info->dfuse_state);

	dfuse_flush_fuse(cnss_info->ci_fsinfo.fsi_handle);

	ret = 0;

	rc = cnss_stop_fuse(&cnss_info->ci_fsinfo);
	if (rc)
		ret = 1;

	dfuse_finish(cnss_info->dfuse_state);

	rc = crt_finalize();
	if (rc != -DER_SUCCESS)
		ret = 1;

	DFUSE_TRA_INFO(cnss_info, "Exiting with status %d", ret);

	DFUSE_TRA_DOWN(cnss_info);

	D_FREE(cnss_info);

	return ret;

shutdown_ctrl_fs:

	dfuse_finish(cnss_info->dfuse_state);

shutdown_log:

	DFUSE_TRA_DOWN(cnss_info);
	DFUSE_LOG_INFO("Exiting with status %d", ret);
	D_FREE(cnss_info);

out:
	/* Convert CaRT error numbers to something that can be returned to the
	 * user.  This needs to be less than 256 so only works for CaRT, not
	 * DAOS error numbers.
	 */
	return -(ret + DER_ERR_GURT_BASE);
}
