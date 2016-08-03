/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the DAOS service
 * including:
 * - network setup
 * - start/stop service threads
 * - bind service threads to core/NUMA node
 */

#include <pthread.h>
#include <sched.h>

#include <abt.h>
#include <daos_errno.h>
#include <daos/list.h>
#include "dss_internal.h"

/** Number of started threads or cores used */
unsigned int	dss_nthreads;

/** Per-thread configuration data */
struct dss_thread {
	pthread_t	dt_id;
	int		dt_idx;
	pthread_mutex_t	dt_lock;
	daos_list_t	dt_list;
	hwloc_cpuset_t	dt_cpuset;
	ABT_xstream	dt_xstream;
	ABT_pool	dt_pool;
	ABT_sched	dt_sched;
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

struct sched_data {
    uint32_t event_freq;
};

static int
dss_sched_init(ABT_sched sched, ABT_sched_config config)
{
	struct sched_data *p_data;
	int ret;

	D_ALLOC_PTR(p_data);
	if (p_data == NULL)
		return ABT_ERR_MEM;

	/* Set the variables from the config */
	ret = ABT_sched_config_read(config, 1, &p_data->event_freq);
	if (ret != ABT_SUCCESS)
		return ret;

	ret = ABT_sched_set_data(sched, (void *)p_data);

	return ret;
}

static void
dss_sched_run(ABT_sched sched)
{
	uint32_t work_count = 0;
	struct sched_data *p_data;
	ABT_pool pool;
	ABT_unit unit;
	int ret;

	ABT_sched_get_data(sched, (void **)&p_data);

	/* Only one pool for now */
	ret = ABT_sched_get_pools(sched, 1, 0, &pool);
	if (ret != ABT_SUCCESS) {
		D_ERROR("ABT_sched_get_pools");
		return;
	}
	while (1) {
		/* Execute one work unit from the scheduler's pool */
		ABT_pool_pop(pool, &unit);
		if (unit != ABT_UNIT_NULL)
			ABT_xstream_run_unit(unit, pool);

		if (++work_count >= p_data->event_freq) {
			ABT_bool stop;

			ret = ABT_sched_has_to_stop(sched, &stop);
			if (ret != ABT_SUCCESS) {
				D_ERROR("ABT_sched_has_to_stop fails %d\n",
					ret);
				break;
			}
			if (stop == ABT_TRUE)
				break;
			work_count = 0;
			ABT_xstream_check_events(sched);
		}
	}
}

static int
dss_sched_free(ABT_sched sched)
{
	struct sched_data *p_data;

	ABT_sched_get_data(sched, (void **)&p_data);
	D_FREE_PTR(p_data);

	return ABT_SUCCESS;
}

/**
 * Create scheduler
 */
static int
dss_sched_create(ABT_pool *pools, int pool_num, ABT_sched *new_sched)
{
	ABT_sched_config config;
	ABT_sched_config_var cv_event_freq = {
		.idx = 0,
		.type = ABT_SCHED_CONFIG_INT
	};

	ABT_sched_def sched_def = {
		.type = ABT_SCHED_TYPE_ULT,
		.init = dss_sched_init,
		.run = dss_sched_run,
		.free = dss_sched_free,
		.get_migr_pool = NULL
	};
	int ret;

	/* Create a scheduler config */
	ret = ABT_sched_config_create(&config, cv_event_freq, 10,
				      ABT_sched_config_var_end);
	if (ret != ABT_SUCCESS)
		return ret;

	ret = ABT_sched_create(&sched_def, pool_num, pools, config,
			       new_sched);
	ABT_sched_config_free(&config);

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
static void
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
		D_GOTO(err_out, rc = daos_errno2der(errno));
	}

	/* initialize thread-local storage for this thread */
	dtc = dss_tls_init(DAOS_SERVER_TAG);
	if (dtc == NULL)
		D_GOTO(err_out, rc = -DER_NOMEM);

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	/* create private transport context */
	rc = dtp_context_create(&dthread->dt_pool, &dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("Can not create dtp ctxt: rc = %d\n", rc);
		D_GOTO(err_out, rc);
	}

	/** report thread index */
	dmi->dmi_tid = dthread->dt_idx;

	/* register clean-up routine called on cancellation point */
	pthread_cleanup_push(dss_srv_handler_cleanup, (void *)dtc);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	dthread->dt_id = pthread_self();

	/* wake up caller */
	pthread_mutex_unlock(&dthread->dt_lock);

	/* main service loop processing incoming request */
	rc = dtp_progress(dmi->dmi_ctx, -1, dss_progress_cb, NULL);
	D_ERROR("service thread exited from progress with %d\n", rc);

	pthread_cleanup_pop(0);
	dtp_context_destroy(dmi->dmi_ctx, true);
	return;

err_out:
	dthread->dt_rc = rc;
	pthread_mutex_unlock(&dthread->dt_lock);
	return;


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

	rc = pthread_mutex_init(&dthread->dt_lock, NULL);
	if (rc) {
		D_ERROR("failed to init pthread mutex: %d\n", rc);
		D_GOTO(err_free, rc = daos_errno2der(errno));
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
err_free:
	D_FREE_PTR(dthread);
	return NULL;
}

static inline void
dss_thread_free(struct dss_thread *dthread)
{
	hwloc_bitmap_free(dthread->dt_cpuset);
	pthread_mutex_destroy(&dthread->dt_lock);
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
	int			 rc = 0;

	/** allocate & init thread configuration data */
	dthread = dss_thread_alloc(idx, cpus);
	if (dthread == NULL)
		return -DER_NOMEM;

	rc = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
				   ABT_TRUE, &dthread->dt_pool);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_dthread, rc);

	rc = dss_sched_create(&dthread->dt_pool, 1, &dthread->dt_sched);
	if (rc != 0) {
		D_ERROR("create scheduler fails: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	rc = ABT_xstream_create(dthread->dt_sched, &dthread->dt_xstream);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream fails %d\n", rc);
		D_GOTO(out_sched, rc = -DER_INVAL);
	}

	rc = ABT_thread_create(dthread->dt_pool, dss_srv_handler,
			       dthread, ABT_THREAD_ATTR_NULL, NULL);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create thread failed: %d\n", rc);
		D_GOTO(out_xthream, rc = -DER_INVAL);
	}

	/** add to the list of started thread */
	daos_list_add_tail(&dthread->dt_list, &dss_thread_list);

	return 0;
out_xthream:
	ABT_xstream_join(dthread->dt_xstream);
	ABT_xstream_free(&dthread->dt_xstream);
	dss_thread_free(dthread);
	return rc;
out_sched:
	ABT_sched_free(&dthread->dt_sched);
out_pool:
	ABT_pool_free(&dthread->dt_pool);
out_dthread:
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

	/* wait for each thread to complete */
	daos_list_for_each_entry_safe(dthread, tmp, &dss_thread_list, dt_list) {
		daos_list_del(&dthread->dt_list);
		ABT_xstream_join(&dthread->dt_xstream);
		ABT_xstream_free(&dthread->dt_xstream);
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
	int	ncores;

	depth = hwloc_get_type_depth(dss_topo, HWLOC_OBJ_CORE);
	/** number of physical core, w/o hyperthreading */
	ncores = hwloc_get_nbobjs_by_type(dss_topo, HWLOC_OBJ_CORE);

	if (nr == 0)
		/* start one thread per core by default */
		dss_nthreads = ncores;
	else
		dss_nthreads = nr;

	/* initialize thread-local storage */
	rc = pthread_key_create(&dss_tls_key, dss_tls_fini);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the service threads */
	D_DEBUG(DF_SERVER, "%d cores detected, starting %d service threads\n",
		ncores, dss_nthreads);
	for (i = 0; i < dss_nthreads; i++) {
		hwloc_obj_t	obj;

		obj = hwloc_get_obj_by_depth(dss_topo, depth, i % ncores);
		if (obj == NULL) {
			D_ERROR("Null core returned by hwloc\n");
			return -DER_INVAL;
		}

		rc = dss_start_one_thread(obj->allowed_cpuset, i);
		if (rc)
			return rc;
	}
	D_DEBUG(DF_SERVER, "%d service threads successfully started\n",
		dss_nthreads);

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
 * Collective operations among all server threads
 */

struct collective_arg {
	ABT_future	ca_future;
	int	      (*ca_func)(void *);
	void	       *ca_arg;
};

static void
collective_func(void *varg)
{
	struct collective_arg  *carg = varg;
	int			rc;

	rc = carg->ca_func(carg->ca_arg);

	rc = ABT_future_set(carg->ca_future, (void *)(intptr_t)rc);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
}

/* Reduce the return codes into the first element. */
static void
collective_reduce(void **arg)
{
	int    *nfailed = arg[0];
	int	i;

	for (i = 1; i < dss_nthreads + 1; i++)
		if ((int)(intptr_t)arg[i] != 0)
			(*nfailed)++;
}

/**
 * Execute \a func(\a arg) collectively on all server threads. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \return		number of failed threads or error code
 */
int
dss_collective(int (*func)(void *), void *arg)
{
	ABT_future		future;
	struct collective_arg	carg;
	struct dss_thread      *dthread;
	int			nfailed = 0;
	int			rc;

	/*
	 * Use the first, extra element of the value array to store the number
	 * of failed tasks.
	 */
	rc = ABT_future_create(dss_nthreads + 1, collective_reduce, &future);
	if (rc != ABT_SUCCESS)
		return -DER_NOMEM;
	rc = ABT_future_set(future, &nfailed);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);

	carg.ca_future = future;
	carg.ca_func = func;
	carg.ca_arg = arg;

	/*
	 * Create tasklets and store return codes in the value array as
	 * "void *" pointers.
	 */
	daos_list_for_each_entry(dthread, &dss_thread_list, dt_list) {
		rc = ABT_task_create(dthread->dt_pool, collective_func, &carg,
				     NULL /* task */);
		if (rc != ABT_SUCCESS) {
			rc = ABT_future_set(future, (void *)-DER_NOMEM);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
		}
	}

	ABT_future_wait(future);
	ABT_future_free(&future);
	return nfailed;
}

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
