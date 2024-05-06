/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

#include <fused/fuse_lowlevel.h>
#define D_LOGFAC DD_FAC(dfuse)
#include "dfuse.h"

struct dfuse_thread {
	d_list_t         dt_threads;
	pthread_t        dt_id;
	struct fuse_buf  dt_fbuf;
	struct dfuse_tm *dt_tm;
};

struct dfuse_tm {
	d_list_t             tm_threads;
	struct fuse_session *tm_se;
	sem_t                tm_finish;
	ATOMIC bool          tm_exit;
	int                  tm_error;
};

static int
start_one(struct dfuse_tm *mt);

static void *
dfuse_do_work(void *arg)
{
	struct dfuse_thread *dt  = arg;
	struct dfuse_tm     *dtm = dt->dt_tm;
	int                  rc;

	while (!fuse_session_exited(dtm->tm_se)) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		rc = fuse_session_receive_buf(dtm->tm_se, &dt->dt_fbuf);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		if (rc == -EINTR)
			continue;
		if (rc <= 0) {
			if (rc < 0) {
				fuse_session_exit(dtm->tm_se);
				dtm->tm_error = -rc;
			}
			break;
		}

		if (atomic_load_relaxed(&dtm->tm_exit))
			return NULL;

		fuse_session_process_buf(dtm->tm_se, &dt->dt_fbuf);
	}

	sem_post(&dtm->tm_finish);

	return NULL;
}

/* Start a new worker thread, either an initial one, or a new
 * one.
 * Called with lock held.
 */
static int
start_one(struct dfuse_tm *dtm)
{
	struct dfuse_thread *dt;
	sigset_t             oldset;
	sigset_t             newset;
	int                  rc;

	D_ALLOC_PTR(dt);
	if (dt == NULL)
		D_GOTO(out, rc = ENOMEM);

	DFUSE_TRA_UP(dt, dtm, "thread");

	dt->dt_tm = dtm;

	sigemptyset(&newset);
	sigaddset(&newset, SIGTERM);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGHUP);
	sigaddset(&newset, SIGQUIT);
	pthread_sigmask(SIG_BLOCK, &newset, &oldset);
	rc = pthread_create(&dt->dt_id, NULL, dfuse_do_work, dt);
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	if (rc != 0) {
		D_FREE(dt);
		D_GOTO(out, rc);
	}

	pthread_setname_np(dt->dt_id, "dfuse worker");

	d_list_add(&dt->dt_threads, &dtm->tm_threads);

out:
	return rc;
}

int
dfuse_loop(struct dfuse_info *dfuse_info)
{
	struct dfuse_tm     *dtm;
	struct dfuse_thread *dt, *next;
	int                  rc;

	D_ALLOC_PTR(dtm);
	if (dtm == NULL)
		D_GOTO(out, rc = ENOMEM);

	DFUSE_TRA_UP(dtm, dfuse_info, "thread_manager");

	D_INIT_LIST_HEAD(&dtm->tm_threads);
	dtm->tm_se    = dfuse_info->di_session;
	dtm->tm_error = 0;

	rc = sem_init(&dtm->tm_finish, 0, 0);
	if (rc != 0)
		D_GOTO(out, rc = errno);

	for (int i = 0; i < dfuse_info->di_thread_count; i++) {
		rc = start_one(dtm);
		if (rc != 0) {
			fuse_session_exit(dfuse_info->di_session);
			break;
		}
	}

	/* sem_wait() is interruptible */
	while (!fuse_session_exited(dfuse_info->di_session))
		sem_wait(&dtm->tm_finish);

	DFUSE_TRA_INFO(dtm, "Session has completed, commencing shutdown");

	atomic_store_relaxed(&dtm->tm_exit, true);

	d_list_for_each_entry(dt, &dtm->tm_threads, dt_threads)
		pthread_cancel(dt->dt_id);

	d_list_for_each_entry_safe(dt, next, &dtm->tm_threads, dt_threads) {
		pthread_join(dt->dt_id, NULL);
		d_list_del(&dt->dt_threads);
		free(dt->dt_fbuf.mem);
		D_FREE(dt);
	}

	rc = dtm->tm_error;

	fuse_session_reset(dfuse_info->di_session);
	sem_destroy(&dtm->tm_finish);
out:
	D_FREE(dtm);
	return rc;
}
