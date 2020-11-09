/**
 * (C) Copyright 2020 Intel Corporation.
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

#include <pthread.h>

#include <fuse3/fuse_lowlevel.h>
#define D_LOGFAC DD_FAC(dfuse)
#include "dfuse.h"

struct dfuse_thread {
	d_list_t	dt_threads;
	pthread_t	dt_id;
	struct fuse_buf dt_fbuf;
	struct dfuse_tm	*dt_tm;
};

struct dfuse_tm {
	d_list_t		tm_threads;
	pthread_mutex_t		tm_lock;
	struct fuse_session	*tm_se;
	sem_t			tm_finish;
	bool			tm_exit;
	int			tm_error;
};

static int
start_one(struct dfuse_tm *mt);

static void
*dfuse_do_work(void *arg)
{
	struct dfuse_thread	*dt = arg;
	struct dfuse_tm		*dtm = dt->dt_tm;
	int rc;

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

		DFUSE_TRA_INFO(dt, "Received %d bytes", rc);

		D_MUTEX_LOCK(&dtm->tm_lock);
		if (dtm->tm_exit) {
			D_MUTEX_UNLOCK(&dtm->tm_lock);
			return NULL;
		}
		D_MUTEX_UNLOCK(&dtm->tm_lock);

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
	struct dfuse_thread	*dt;
	sigset_t		oldset;
	sigset_t		newset;
	int rc;

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

	pthread_setname_np(dt->dt_id, "dfuse_worker");

	d_list_add(&dt->dt_threads, &dtm->tm_threads);

out:
	return rc;
}

int
dfuse_loop(struct dfuse_info *dfuse_info)
{
	struct fuse_session	*se;
	struct dfuse_tm		*dtm;
	struct dfuse_thread	*dt, *next;
	int			i;
	int			rc = 0;

	se = dfuse_info->di_session;
	D_ALLOC_PTR(dtm);
	if (dtm == NULL)
		D_GOTO(out, rc = ENOMEM);

	DFUSE_TRA_UP(dtm, dfuse_info, "thread_manager");

	D_INIT_LIST_HEAD(&dtm->tm_threads);
	dtm->tm_se = se;
	dtm->tm_error = 0;

	sem_init(&dtm->tm_finish, 0, 0);
	D_MUTEX_INIT(&dtm->tm_lock, NULL);

	D_MUTEX_LOCK(&dtm->tm_lock);
	for (i = 0 ; i < 4 ; i++) {
		rc = start_one(dtm);
		if (rc != 0) {
			fuse_session_exit(se);
			break;
		}
	}
	D_MUTEX_UNLOCK(&dtm->tm_lock);

	/* sem_wait() is interruptible */
	while (!fuse_session_exited(se))
		sem_wait(&dtm->tm_finish);

	D_MUTEX_LOCK(&dtm->tm_lock);
	d_list_for_each_entry(dt, &dtm->tm_threads, dt_threads)
		pthread_cancel(dt->dt_id);
	dtm->tm_exit = true;
	D_MUTEX_UNLOCK(&dtm->tm_lock);

	d_list_for_each_entry_safe(dt, next, &dtm->tm_threads, dt_threads) {
		pthread_join(dt->dt_id, NULL);
		D_MUTEX_LOCK(&dtm->tm_lock);
		d_list_del(&dt->dt_threads);
		D_MUTEX_UNLOCK(&dtm->tm_lock);
		free(dt->dt_fbuf.mem);
		D_FREE(dt);
	}

	rc = dtm->tm_error;

	pthread_mutex_destroy(&dtm->tm_lock);
	sem_destroy(&dtm->tm_finish);
	fuse_session_reset(se);
	D_FREE(dtm);
out:
	return rc;
}

