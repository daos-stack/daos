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
 * - start/stop execution streams
 * - bind execution streams to core/NUMA node
 */

#include <abt.h>
#include <daos_errno.h>
#include <daos/list.h>
#include "srv_internal.h"

/** Number of started xstreams or cores used */
unsigned int	dss_nxstreams;

/** Per-xstream configuration data */
struct dss_xstream {
	ABT_future	dx_shutdown;
	daos_list_t	dx_list;
	hwloc_cpuset_t	dx_cpuset;
	ABT_xstream	dx_xstream;
	ABT_pool	dx_pool;
	ABT_sched	dx_sched;
	ABT_xstream	dx_progress;
};

/** List of running execution streams */
static daos_list_t dss_xstream_list;

struct sched_data {
    uint32_t event_freq;
};

static int
dss_sched_init(ABT_sched sched, ABT_sched_config config)
{
	struct sched_data	*p_data;
	int			 ret;

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
	uint32_t		 work_count = 0;
	struct sched_data	*p_data;
	ABT_pool		 pool;
	ABT_unit		 unit;
	int			 ret;

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
	int			ret;
	ABT_sched_config	config;
	ABT_sched_config_var	cv_event_freq = {
		.idx	= 0,
		.type	= ABT_SCHED_CONFIG_INT
	};

	ABT_sched_def		sched_def = {
		.type	= ABT_SCHED_TYPE_ULT,
		.init	= dss_sched_init,
		.run	= dss_sched_run,
		.free	= dss_sched_free,
		.get_migr_pool = NULL
	};

	/* Create a scheduler config */
	ret = ABT_sched_config_create(&config, cv_event_freq, 10,
				      ABT_sched_config_var_end);
	if (ret != ABT_SUCCESS)
		return dss_abterr2der(ret);

	ret = ABT_sched_create(&sched_def, pool_num, pools, config,
			       new_sched);
	ABT_sched_config_free(&config);

	return dss_abterr2der(ret);
}

int
dss_progress_cb(void *arg)
{
	ABT_future	*shutdown = (ABT_future *)arg;
	ABT_bool	 state;
	int		 rc;

	rc = ABT_future_test(*shutdown, &state);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	return state == ABT_TRUE;
}

/**
 *
 * The handling process would like
 *
 * 1. The execution stream creates a private CRT context
 *
 * 2. Then polls the request from CRT context
 */
static void
dss_srv_handler(void *arg)
{
	struct dss_xstream		*dx = (struct dss_xstream *)arg;
	struct dss_thread_local_storage	*dtc;
	struct dss_module_info		*dmi;
	int				 rc;

	/** set affinity */
	rc = hwloc_set_cpubind(dss_topo, dx->dx_cpuset, HWLOC_CPUBIND_THREAD);
	if (rc) {
		D_ERROR("failed to set affinity: %d\n", errno);
		return;
	}

	/* initialize xstream-local storage */
	dtc = dss_tls_init(DAOS_SERVER_TAG);
	if (dtc == NULL) {
		D_ERROR("failed to initialize TLS\n");
		return;
	}

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	/** report xstream index */
	rc = ABT_xstream_self_rank(&dmi->dmi_tid);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to retrieve xstream rank %d\n", rc);
		return;
	}

	/* create private transport context */
	rc = crt_context_create(&dx->dx_pool, &dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("failed to create crt ctxt: %d\n", rc);
		return;
	}

	/* main service loop processing incoming request */
	rc = crt_progress(dmi->dmi_ctx, -1, dss_progress_cb, &dx->dx_shutdown);
	if (rc != 0)
		D_ERROR("failed to progress network context: %d\n", rc);
	crt_context_destroy(dmi->dmi_ctx, true);
}

static inline struct dss_xstream *
dss_xstream_alloc(hwloc_cpuset_t cpus)
{
	struct dss_xstream	*dx;
	int			 rc = 0;

	D_ALLOC_PTR(dx);
	if (dx == NULL) {
		D_ERROR("Can not allocate execution stream.\n");
		return NULL;
	}

	rc = ABT_future_create(1, NULL, &dx->dx_shutdown);
	if (rc != 0) {
		D_ERROR("failed to allocate future\n");
		D_GOTO(err_free, rc = dss_abterr2der(rc));
	}

	dx->dx_cpuset = hwloc_bitmap_dup(cpus);
	if (dx->dx_cpuset == NULL) {
		D_ERROR("failed to allocate cpuset\n");
		D_GOTO(err_future, rc = -DER_NOMEM);
	}

	dx->dx_xstream	= ABT_XSTREAM_NULL;
	dx->dx_pool	= ABT_POOL_NULL;
	dx->dx_sched	= ABT_SCHED_NULL;
	dx->dx_progress	= ABT_THREAD_NULL;
	DAOS_INIT_LIST_HEAD(&dx->dx_list);

	return dx;

err_future:
	ABT_future_free(&dx->dx_shutdown);
err_free:
	D_FREE_PTR(dx);
	return NULL;
}

static inline void
dss_xstream_free(struct dss_xstream *dx)
{
	hwloc_bitmap_free(dx->dx_cpuset);
	D_FREE_PTR(dx);
}

/**
 * Start \a nr xstreams, which will evenly distributed all
 * of cores.
 *
 * \param[in] nr	number of xstreams to be created.
 *
 * \retval	= 0 if starting succeeds.
 * \retval	negative errno if starting fails.
 */
static int
dss_start_one_xstream(hwloc_cpuset_t cpus, int idx)
{
	struct dss_xstream	*dx;
	int			 rc = 0;

	/** allocate & init xstream configuration data */
	dx = dss_xstream_alloc(cpus);
	if (dx == NULL)
		return -DER_NOMEM;

	/** create pool */
	rc = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
				   ABT_TRUE, &dx->dx_pool);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_dxstream, rc = dss_abterr2der(rc));

	rc = dss_sched_create(&dx->dx_pool, 1, &dx->dx_sched);
	if (rc != 0) {
		D_ERROR("create scheduler fails: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	/** start execution stream, rank must be non-null */
	rc = ABT_xstream_create_with_rank(dx->dx_sched, idx,
					  &dx->dx_xstream);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream fails %d\n", rc);
		D_GOTO(out_sched, rc = dss_abterr2der(rc));
	}

	/** start progress ULT */
	rc = ABT_thread_create(dx->dx_pool, dss_srv_handler,
			       dx, ABT_THREAD_ATTR_NULL,
			       &dx->dx_progress);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream failed: %d\n", rc);
		D_GOTO(out_xstream, rc = dss_abterr2der(rc));
	}

	/** add to the list of execution streams */
	daos_list_add_tail(&dx->dx_list, &dss_xstream_list);

	return 0;
out_xstream:
	ABT_xstream_join(dx->dx_xstream);
	ABT_xstream_free(&dx->dx_xstream);
	dss_xstream_free(dx);
	return rc;
out_sched:
	ABT_sched_free(&dx->dx_sched);
out_pool:
	ABT_pool_free(&dx->dx_pool);
out_dxstream:
	dss_xstream_free(dx);
	return rc;
}

static void
dss_xstreams_fini(bool force)
{
	struct dss_xstream	*dx;
	struct dss_xstream	*tmp;
	int			 rc;

	D_DEBUG(DF_SERVER, "Stopping execution streams\n");

	/** Stop & free progress xstreams */
	daos_list_for_each_entry(dx, &dss_xstream_list, dx_list)
		ABT_future_set(dx->dx_shutdown, dx);
	daos_list_for_each_entry(dx, &dss_xstream_list, dx_list) {
		ABT_thread_join(dx->dx_progress);
		ABT_thread_free(&dx->dx_progress);
		ABT_future_free(&dx->dx_shutdown);
	}

	/** Wait for each execution stream to complete */
	daos_list_for_each_entry(dx, &dss_xstream_list, dx_list) {
		ABT_xstream_join(dx->dx_xstream);
		ABT_xstream_free(&dx->dx_xstream);
	}

	/** housekeeping ... */
	daos_list_for_each_entry_safe(dx, tmp, &dss_xstream_list, dx_list) {
		daos_list_del_init(&dx->dx_list);
		ABT_sched_free(&dx->dx_sched);
		dss_xstream_free(dx);
	}

	/* release local storage */
	rc = pthread_key_delete(dss_tls_key);
	if (rc)
		D_ERROR("failed to delete dtc: %d\n", rc);

	D_DEBUG(DF_SERVER, "Execution streams stopped\n");
}

static int
dss_xstreams_init(int nr)
{
	int	rc;
	int	i;
	int	depth;
	int	ncores;

	depth = hwloc_get_type_depth(dss_topo, HWLOC_OBJ_CORE);
	/** number of physical core, w/o hyperthreading */
	ncores = hwloc_get_nbobjs_by_type(dss_topo, HWLOC_OBJ_CORE);

	if (nr == 0)
		/* start one xstream per core by default */
		dss_nxstreams = ncores;
	else
		dss_nxstreams = nr;

	/* initialize xstream-local storage */
	rc = pthread_key_create(&dss_tls_key, dss_tls_fini);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the execution streams */
	D_DEBUG(DF_SERVER, "%d cores detected, starting %d execution streams\n",
		ncores, dss_nxstreams);
	for (i = 1; i <= dss_nxstreams; i++) {
		hwloc_obj_t	obj;

		obj = hwloc_get_obj_by_depth(dss_topo, depth, i % ncores);
		if (obj == NULL) {
			D_ERROR("Null core returned by hwloc\n");
			return -DER_INVAL;
		}

		/** ABT rank 0 is reserved for the primary xstream */
		rc = dss_start_one_xstream(obj->allowed_cpuset, i);
		if (rc)
			return rc;
	}
	D_DEBUG(DF_SERVER, "%d execution streams successfully started\n",
		dss_nxstreams);

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
 * Collective operations among all server xstreams
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

	for (i = 1; i < dss_nxstreams + 1; i++)
		if ((int)(intptr_t)arg[i] != 0)
			(*nfailed)++;
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \return		number of failed xstreams or error code
 */
int
dss_collective(int (*func)(void *), void *arg)
{
	ABT_future		future;
	struct collective_arg	carg;
	struct dss_xstream      *dx;
	int			nfailed = 0;
	int			rc;

	/*
	 * Use the first, extra element of the value array to store the number
	 * of failed tasks.
	 */
	rc = ABT_future_create(dss_nxstreams + 1, collective_reduce, &future);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = ABT_future_set(future, &nfailed);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);

	carg.ca_future = future;
	carg.ca_func = func;
	carg.ca_arg = arg;

	/*
	 * Create tasklets and store return codes in the value array as
	 * "void *" pointers.
	 */
	daos_list_for_each_entry(dx, &dss_xstream_list, dx_list) {
		rc = ABT_task_create(dx->dx_pool, collective_func, &carg,
				     NULL /* task */);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			rc = ABT_future_set(future, (void *)(intptr_t)rc);
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
dss_srv_fini(bool force)
{
	dss_xstreams_fini(force);

	dss_unregister_key(&daos_srv_modkey);

	return 0;
}

int
dss_srv_init(int nr)
{
	int	rc;

	DAOS_INIT_LIST_HEAD(&dss_xstream_list);

	/** register global tls accessible to all modules */
	dss_register_key(&daos_srv_modkey);

	/* start xstreams */
	rc = dss_xstreams_init(nr);
	if (rc != 0)
		dss_srv_fini(true);

	return rc;
}
