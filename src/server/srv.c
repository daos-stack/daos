/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of the DAOS server. It implements the DAOS service
 * including:
 * - network setup
 * - start/stop service threads
 * - bind service threads to core/NUMA node
 */

#include <pthread.h>
#include <sched.h>

#include <daos_errno.h>
#include <daos/list.h>
#include "dss_internal.h"

/** Per-thread configuration data */
struct dss_thread {
	pthread_t	dt_id;
	int		dt_idx;
	pthread_cond_t	dt_start_cond;
	pthread_mutex_t	dt_lock;
	daos_list_t	dt_list;
	hwloc_cpuset_t	dt_cpuset;
	int		dt_rc;
};

/** List of running service threads */
static daos_list_t dss_thread_list;

void
dss_srv_handler_cleanup(void *param)
{
	struct dss_thread_local_storage	*dtc;
	struct dss_module_info		*dmi;
	int				rc;

	dtc = param;
	dmi = (struct dss_module_info *)
	      dss_module_key_get(dtc, &daos_srv_modkey);
	D_ASSERT(dmi != NULL);
	rc = dtp_context_destroy(dmi->dmi_ctx, true);
	if (rc)
		D_ERROR("failed to destroy context: %d\n", rc);
}

int
dss_progress_cb(void *arg)
{
	/** cancellation point */
	pthread_testcancel();

	/** no cancel happened, continue running */
	return 0;
}

/**
 *
 The handling process would like
 *
 * 1. The service thread creates a private DTP context
 *
 * 2. Then polls the request from DTP context
 **/
static void *
dss_srv_handler(void *arg)
{
	struct dss_thread	*dthread = (struct dss_thread *)arg;
	struct dss_thread_local_storage *dtc;
	struct dss_module_info	*dmi;
	int			 rc;

	/* ignore the cancel request until after initialization */
	pthread_mutex_lock(&dthread->dt_lock);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	rc = hwloc_set_cpubind(dss_topo, dthread->dt_cpuset,
			       HWLOC_CPUBIND_THREAD);
	if (rc) {
		D_ERROR("failed to set affinity: %d\n", errno);
		dthread->dt_rc = daos_errno2der();
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	/* initialize thread-local storage for this thread */
	dtc = dss_tls_init(DAOS_SERVER_TAG);
	if (dtc == NULL) {
		dthread->dt_rc = -DER_NOMEM;
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	/* create private transport context */
	rc = dtp_context_create(NULL, &dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("Can not create dtp ctxt: rc = %d\n", rc);
		dthread->dt_rc = rc;
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	/** report thread index */
	dmi->dmi_tid = dthread->dt_idx;

	/* register clean-up routine called on cancellation point */
	pthread_cleanup_push(dss_srv_handler_cleanup, (void *)dtc);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	dthread->dt_id = pthread_self();

	/* wake up caller */
	pthread_cond_signal(&dthread->dt_start_cond);
	pthread_mutex_unlock(&dthread->dt_lock);

	/* main service loop processing incoming request */
	rc = dtp_progress(dmi->dmi_ctx, -1, dss_progress_cb, NULL);
	D_ERROR("service thread exited from progress with %d\n", rc);

	pthread_cleanup_pop(0);
	dtp_context_destroy(dmi->dmi_ctx, true);
	return NULL;
}

static inline struct dss_thread *
dss_thread_alloc(int idx, hwloc_cpuset_t cpus)
{
	struct dss_thread	*dthread;
	int			 rc = 0;

	D_ALLOC_PTR(dthread);
	if (dthread == NULL) {
		D_ERROR("Can not allocate dthread.\n");
		return NULL;
	}

	errno = pthread_cond_init(&dthread->dt_start_cond, NULL);
	if (errno) {
		D_ERROR("failed to init pthread cond: %d\n", rc);
		D_GOTO(err_free, rc = daos_errno2der());
	}

	rc = pthread_mutex_init(&dthread->dt_lock, NULL);
	if (rc) {
		D_ERROR("failed to init pthread mutex: %d\n", rc);
		D_GOTO(err_cond, rc = daos_errno2der());
	}

	dthread->dt_cpuset = hwloc_bitmap_dup(cpus);
	if (dthread->dt_cpuset == NULL) {
		D_ERROR("failed to allocate cpuset\n");
		D_GOTO(err_mutex, rc = -DER_NOMEM);
	}

	dthread->dt_idx = idx;
	DAOS_INIT_LIST_HEAD(&dthread->dt_list);

	return dthread;

err_mutex:
	pthread_mutex_destroy(&dthread->dt_lock);
err_cond:
	pthread_cond_destroy(&dthread->dt_start_cond);
err_free:
	D_FREE_PTR(dthread);
	return NULL;
}

static inline void
dss_thread_free(struct dss_thread *dthread)
{
	hwloc_bitmap_free(dthread->dt_cpuset);
	pthread_mutex_destroy(&dthread->dt_lock);
	pthread_cond_destroy(&dthread->dt_start_cond);
	D_FREE_PTR(dthread);
}

/**
 * Start \a nr threads, which will evenly distributed all
 * of cores.
 *
 * \param[in] nr	number of threads to be created.
 *
 * \retval	= 0 if starting succeeds.
 * \retval	negative errno if starting fails.
 */
static int
dss_start_one_thread(hwloc_cpuset_t cpus, int idx)
{
	struct dss_thread	*dthread;
	pthread_attr_t		 attr;
	pthread_t		 pid;
	int			 rc = 0;

	/** allocate & init thread configuration data */
	dthread = dss_thread_alloc(idx, cpus);
	if (dthread == NULL)
		return -DER_NOMEM;

	/** configure per-thread attributes */
	errno = pthread_attr_init(&attr);
	if (errno) {
		D_ERROR("failed to init pthread attr: %d\n", errno);
		D_GOTO(out, rc = daos_errno2der());
	}

	errno = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	if (errno) {
		D_ERROR("failed to set sched policy: %d\n", errno);
		D_GOTO(out_attr, rc = daos_errno2der());
	}

	/** start the service thread */
	pthread_mutex_lock(&dthread->dt_lock);
	errno = pthread_create(&pid, &attr, dss_srv_handler, dthread);
	if (errno != 0) {
		D_ERROR("Can not create thread%d: %d\n", idx, errno);
		D_GOTO(out_lock, rc = daos_errno2der());
	}

	/** wait for thread to be effectively set up */
	errno = pthread_cond_wait(&dthread->dt_start_cond, &dthread->dt_lock);
	if (errno != 0) {
		D_ERROR("failed to wait for thread%d: %d\n", idx, errno);
		D_GOTO(out_lock, rc = daos_errno2der());
	}

	if (dthread->dt_id == 0) {
		D_ERROR("can not start thread%d: %d\n", idx, dthread->dt_rc);
		D_GOTO(out_lock, rc = dthread->dt_rc);
	}

	/** add to the list of started thread */
	daos_list_add_tail(&dthread->dt_list, &dss_thread_list);

out_lock:
	pthread_mutex_unlock(&dthread->dt_lock);
out_attr:
	pthread_attr_destroy(&attr);
out:
	if (rc)
		dss_thread_free(dthread);
	return rc;
}

static void
dss_threads_fini()
{
	struct dss_thread	*dthread;
	struct dss_thread	*tmp;
	int			 rc;

	D_DEBUG(DF_SERVER, "stopping service threads\n");

	/** issue cancel to all threads */
	daos_list_for_each_entry_safe(dthread, tmp, &dss_thread_list, dt_list) {
		pthread_mutex_lock(&dthread->dt_lock);
		if (dthread->dt_id != 0) {
			rc = pthread_cancel(dthread->dt_id);
			if (rc) {
				D_ERROR("Failed to kill %ld thread: rc = %d\n",
					dthread->dt_id, rc);
				dthread->dt_id = 0;
			}
		}
		pthread_mutex_unlock(&dthread->dt_lock);
	}

	/* wait for each thread to complete */
	daos_list_for_each_entry_safe(dthread, tmp, &dss_thread_list, dt_list) {
		pthread_mutex_lock(&dthread->dt_lock);
		daos_list_del(&dthread->dt_list);

		if (dthread->dt_id != 0) {
			/* We need to wait for the thread to exit */
			pthread_join(dthread->dt_id, NULL);
			dthread->dt_id = 0;
		}

		pthread_mutex_unlock(&dthread->dt_lock);

		/* housekeeping ... */
		dss_thread_free(dthread);
	}

	/* release thread-local storage */
	rc = pthread_key_delete(dss_tls_key);
	if (rc)
		D_ERROR("failed to delete dtc: %d\n", rc);

	D_DEBUG(DF_SERVER, "service threads stopped\n");
}

static int
dss_threads_init(int nr)
{
	int	rc;
	int	i;
	int	depth;

	depth = hwloc_get_type_depth(dss_topo, HWLOC_OBJ_CORE);

	if (nr == 0)
		/* start one thread per core by default */
		nr = dss_ncores;

	/* initialize thread-local storage */
	rc = pthread_key_create(&dss_tls_key, dss_tls_fini);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the service threads */
	D_DEBUG(DF_SERVER, "%d cores detected, starting %d service threads\n",
		dss_ncores, nr);
	for (i = 0; i < nr; i++) {
		hwloc_obj_t	obj;

		obj = hwloc_get_obj_by_depth(dss_topo, depth, nr % dss_ncores);
		if (obj == NULL) {
			D_ERROR("Null core returned by hwloc\n");
			return -DER_INVAL;
		}

		rc = dss_start_one_thread(obj->allowed_cpuset, i);
		if (rc)
			return rc;
	}
	D_DEBUG(DF_SERVER, "%d service threads successfully started\n", nr);

	return 0;
}

/**
 * Global TLS
 */

static void *
dss_srv_tls_init(const struct dss_thread_local_storage *dtls,
		     struct dss_module_key *key)
{
	struct dss_module_info *info;

	D_ALLOC_PTR(info);

	return info;
}

static void
dss_srv_tls_fini(const struct dss_thread_local_storage *dtls,
		     struct dss_module_key *key, void *data)
{
	struct dss_module_info *info = (struct dss_module_info *)data;

	D_FREE_PTR(info);
}

struct dss_module_key daos_srv_modkey = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dss_srv_tls_init,
	.dmk_fini = dss_srv_tls_fini,
};

/**
 * Entry point to start up and shutdown the service
 */

int
dss_srv_fini()
{
	dss_threads_fini();

	dss_unregister_key(&daos_srv_modkey);

	return 0;
}

int
dss_srv_init(int nr)
{
	int	rc;

	DAOS_INIT_LIST_HEAD(&dss_thread_list);

	/** register global tls accessible to all modules */
	dss_register_key(&daos_srv_modkey);

	/* start threads */
	rc = dss_threads_init(nr);
	if (rc != 0)
		dss_srv_fini();

	return rc;
}
