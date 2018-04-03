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
#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos_errno.h>
#include <gurt/list.h>
#include "srv_internal.h"

/** Number of started xstreams or cores used */
unsigned int	dss_nxstreams;

unsigned int	dss_rebuild_res_percentage = 30;

/** Per-xstream configuration data */
struct dss_xstream {
	ABT_future	dx_shutdown;
	d_list_t	dx_list;
	hwloc_cpuset_t	dx_cpuset;
	ABT_xstream	dx_xstream;
	ABT_pool	dx_pools[DSS_POOL_CNT];
	ABT_sched	dx_sched;
	ABT_thread	dx_progress;
	unsigned int	dx_idx;
};

struct dss_xstream_data {
	/** List of running execution streams */
	d_list_t	xd_list;
	/** Initializing step, it is for cleanup of global states */
	int		xd_init_step;
	bool		xd_ult_signal;
	/** serialize initialization of ULTs */
	ABT_cond	xd_ult_init;
	/** barrier for all ULTs to enter handling loop */
	ABT_cond	xd_ult_barrier;
	ABT_mutex	xd_mutex;
};

static struct dss_xstream_data	xstream_data;

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

static ABT_unit
normal_unit_pop(ABT_pool *pools, ABT_pool *pool)
{
	ABT_unit unit;

	/* Let's pop I/O request ULT first */
	ABT_pool_pop(pools[DSS_POOL_PRIV], &unit);
	if (unit != ABT_UNIT_NULL) {
		*pool = pools[DSS_POOL_PRIV];
		return unit;
	}

	/* Other request and ollective ULT or created ULT */
	ABT_pool_pop(pools[DSS_POOL_SHARE], &unit);
	if (unit != ABT_UNIT_NULL) {
		*pool = pools[DSS_POOL_SHARE];
		return unit;
	}

	return ABT_UNIT_NULL;
}

static ABT_unit
rebuild_unit_pop(ABT_pool *pools, ABT_pool *pool)
{
	ABT_unit unit;

	ABT_pool_pop(pools[DSS_POOL_REBUILD], &unit);
	if (unit != ABT_UNIT_NULL) {
		*pool = pools[DSS_POOL_REBUILD];
		return unit;
	}

	return ABT_UNIT_NULL;
}

/**
 * Choose ULT from the pool. Note: the rebuild ULT will be
 * be choosen by dss_rebuild_res_percentage.
 *
 * XXX we may change the sequence later once we have more cases.
 */
static ABT_unit
dss_sched_unit_pop(ABT_pool *pools, ABT_pool *pool)
{
	size_t	 rebuild_cnt;
	int	 rc;

	rc = ABT_pool_get_total_size(pools[DSS_POOL_REBUILD],
				     &rebuild_cnt);
	if (rc != ABT_SUCCESS)
		return ABT_UNIT_NULL;

	if (rebuild_cnt == 0 ||
	    rand() % 100 > dss_rebuild_res_percentage)
		return normal_unit_pop(pools, pool);
	else
		return rebuild_unit_pop(pools, pool);

	return ABT_UNIT_NULL;
}

static void
dss_sched_run(ABT_sched sched)
{
	uint32_t		work_count = 0;
	struct sched_data	*p_data;
	ABT_pool		pools[DSS_POOL_CNT];
	ABT_pool		pool = ABT_POOL_NULL;
	ABT_unit		unit;
	int			ret;

	ABT_sched_get_data(sched, (void **)&p_data);

	ret = ABT_sched_get_pools(sched, DSS_POOL_CNT, 0, pools);
	if (ret != ABT_SUCCESS) {
		D_ERROR("ABT_sched_get_pools");
		return;
	}

	while (1) {
		/* Execute one work unit from the scheduler's pool */
		unit = dss_sched_unit_pop(pools, &pool);
		if (unit != ABT_UNIT_NULL && pool != ABT_UNIT_NULL)
			ABT_xstream_run_unit(unit, pool);

		if (++work_count >= p_data->event_freq) {
			ABT_xstream_check_events(sched);
			break;
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


static dss_abt_pool_choose_cb_t abt_pool_choose_cbs[DAOS_MAX_MODULE];

/**
 * Register abt choose pool callback for each module, so the module
 * can choose the pools by itself.
 *
 * \param mod_id [IN]	module ID.
 * \param cb [IN]	callback.
 *
 * \return		0 if succes, otherwise negative errno.
 */
void
dss_abt_pool_choose_cb_register(unsigned int mod_id,
				dss_abt_pool_choose_cb_t cb)
{
	D_ASSERT(abt_pool_choose_cbs[mod_id] == NULL);
	abt_pool_choose_cbs[mod_id] = cb;
}

/**
 * Process the rpc received, let's create a ABT thread for each request.
 */
int
dss_process_rpc(crt_context_t *ctx, crt_rpc_t *rpc,
		void (*real_rpc_hdlr)(void *), void *arg)
{
	unsigned int	mod_id = opc_get_mod_id(rpc->cr_opc);
	ABT_pool	*pools = arg;
	ABT_pool	pool;
	int		rc;

	if (abt_pool_choose_cbs[mod_id] != NULL)
		pool = abt_pool_choose_cbs[mod_id](rpc, pools);
	else
		pool = pools[DSS_POOL_SHARE];

	rc = ABT_thread_create(pool, real_rpc_hdlr, rpc,
			       ABT_THREAD_ATTR_NULL, NULL);
	if (rc != ABT_SUCCESS)
		rc = dss_abterr2der(rc);
	return rc;
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

	/* create private transport context */
	rc = crt_context_create(&dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("failed to create crt ctxt: %d\n", rc);
		return;
	}

	rc = crt_context_register_rpc_task(dmi->dmi_ctx,
					   dss_process_rpc,
					   dx->dx_pools);
	if (rc != 0) {
		D_ERROR("failed to register process cb %d\n", rc);
		D_GOTO(destroy, rc);
	}

	/** Get xtream index from cart */
	rc = crt_context_idx(dmi->dmi_ctx, &dmi->dmi_tid);
	if (rc != 0) {
		D_ERROR("failed to get xtream index: rc %d\n", rc);
		D_GOTO(destroy, rc);
	}

	/* Prepare the scheduler */
	rc = tse_sched_init(&dmi->dmi_sched, NULL, dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("failed to init the scheduler\n");
		D_GOTO(destroy, rc);
	}

	dx->dx_idx = dmi->dmi_tid;
	dmi->dmi_xstream = dx;
	ABT_mutex_lock(xstream_data.xd_mutex);
	/* initialized everything for the ULT, notify the creater */
	D_ASSERT(!xstream_data.xd_ult_signal);
	xstream_data.xd_ult_signal = true;
	ABT_cond_signal(xstream_data.xd_ult_init);

	/* wait until all xstreams are ready, otherwise it is not safe
	 * to run lock-free dss_collective, althought this race is not
	 * realistically possible in the DAOS stack.
	 */
	ABT_cond_wait(xstream_data.xd_ult_barrier, xstream_data.xd_mutex);
	ABT_mutex_unlock(xstream_data.xd_mutex);

	/* main service progress loop */
	for (;;) {
		ABT_bool state;

		rc = crt_progress(dmi->dmi_ctx, 0 /* no wait */, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("failed to progress network context: %d\n",
				rc);
			break;
		}

		rc = ABT_future_test(dx->dx_shutdown, &state);
		D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
		if (state == ABT_TRUE)
			break;

		ABT_thread_yield();
	}

	tse_sched_fini(&dmi->dmi_sched);
destroy:
	crt_context_destroy(dmi->dmi_ctx, true);
	dss_tls_fini(dtc);
}

static inline struct dss_xstream *
dss_xstream_alloc(hwloc_cpuset_t cpus)
{
	struct dss_xstream	*dx;
	int			i;
	int			rc = 0;

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

	for (i = 0; i < DSS_POOL_CNT; i++)
		dx->dx_pools[i] = ABT_POOL_NULL;

	dx->dx_xstream	= ABT_XSTREAM_NULL;
	dx->dx_sched	= ABT_SCHED_NULL;
	dx->dx_progress	= ABT_THREAD_NULL;
	D_INIT_LIST_HEAD(&dx->dx_list);

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
	int			rc = 0;
	int			i;

	/** allocate & init xstream configuration data */
	dx = dss_xstream_alloc(cpus);
	if (dx == NULL)
		return -DER_NOMEM;

	/** create pools */
	for (i = 0; i < DSS_POOL_CNT; i++) {
		ABT_pool_access access;

		access = i == DSS_POOL_SHARE ?
			 ABT_POOL_ACCESS_MPSC : ABT_POOL_ACCESS_PRIV;

		rc = ABT_pool_create_basic(ABT_POOL_FIFO, access, ABT_TRUE,
					   &dx->dx_pools[i]);
		if (rc != ABT_SUCCESS)
			D_GOTO(out_pool, rc = dss_abterr2der(rc));
	}

	rc = dss_sched_create(dx->dx_pools, DSS_POOL_CNT, &dx->dx_sched);
	if (rc != 0) {
		D_ERROR("create scheduler fails: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	/** start execution stream, rank must be non-null */
	rc = ABT_xstream_create_with_rank(dx->dx_sched, idx, &dx->dx_xstream);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream fails %d\n", rc);
		D_GOTO(out_sched, rc = dss_abterr2der(rc));
	}

	/** start progress ULT */
	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE],
			       dss_srv_handler, dx, ABT_THREAD_ATTR_NULL,
			       &dx->dx_progress);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream failed: %d\n", rc);
		D_GOTO(out_xstream, rc = dss_abterr2der(rc));
	}

	ABT_mutex_lock(xstream_data.xd_mutex);

	if (!xstream_data.xd_ult_signal)
		ABT_cond_wait(xstream_data.xd_ult_init, xstream_data.xd_mutex);
	xstream_data.xd_ult_signal = false;

	/** add to the list of execution streams */
	d_list_add_tail(&dx->dx_list, &xstream_data.xd_list);
	ABT_mutex_unlock(xstream_data.xd_mutex);

	return 0;
out_xstream:
	ABT_xstream_join(dx->dx_xstream);
	ABT_xstream_free(&dx->dx_xstream);
	dss_xstream_free(dx);
	return rc;
out_sched:
	ABT_sched_free(&dx->dx_sched);
out_pool:
	for (i = 0; i < DSS_POOL_CNT; i++) {
		if (dx->dx_pools[i] != ABT_POOL_NULL)
			ABT_pool_free(&dx->dx_pools[i]);
	}
	dss_xstream_free(dx);
	return rc;
}

static void
dss_xstreams_fini(bool force)
{
	struct dss_xstream	*dx;
	struct dss_xstream	*tmp;
	int			 rc;

	D_DEBUG(DB_TRACE, "Stopping execution streams\n");

	/** Stop & free progress ULTs */
	d_list_for_each_entry(dx, &xstream_data.xd_list, dx_list)
		ABT_future_set(dx->dx_shutdown, dx);
	d_list_for_each_entry(dx, &xstream_data.xd_list, dx_list) {
		ABT_thread_join(dx->dx_progress);
		ABT_thread_free(&dx->dx_progress);
		ABT_future_free(&dx->dx_shutdown);
	}

	/** Wait for each execution stream to complete */
	d_list_for_each_entry(dx, &xstream_data.xd_list, dx_list) {
		ABT_xstream_join(dx->dx_xstream);
		ABT_xstream_free(&dx->dx_xstream);
	}

	/** housekeeping ... */
	d_list_for_each_entry_safe(dx, tmp, &xstream_data.xd_list, dx_list) {
		d_list_del_init(&dx->dx_list);
		ABT_sched_free(&dx->dx_sched);
		dss_xstream_free(dx);
	}

	/* All other xstreams have terminated. */
	dss_nxstreams = 0;

	/* release local storage */
	rc = pthread_key_delete(dss_tls_key);
	if (rc)
		D_ERROR("failed to delete dtc: %d\n", rc);

	D_DEBUG(DB_TRACE, "Execution streams stopped\n");
}

static void
dss_xstreams_open_barrier(void)
{
	ABT_mutex_lock(xstream_data.xd_mutex);
	ABT_cond_broadcast(xstream_data.xd_ult_barrier);
	ABT_mutex_unlock(xstream_data.xd_mutex);
}

static bool
dss_xstreams_empty(void)
{
	return d_list_empty(&xstream_data.xd_list);
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

	if (!nr || nr > ncores)
		D_PRINT("(%d/%d) cores requested; use default (%d) cores\n",
			 nr, ncores, ncores);
	/** default: one xstream per core (ncores) */
	dss_nxstreams = (nr > 0 && nr <= ncores) ? nr : ncores;

	/* initialize xstream-local storage */
	rc = pthread_key_create(&dss_tls_key, NULL);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the execution streams */
	D_DEBUG(DB_TRACE, "%d cores detected, starting %d execution streams\n",
		ncores, dss_nxstreams);
	for (i = 1; i <= dss_nxstreams; i++) {
		hwloc_obj_t	obj;

		obj = hwloc_get_obj_by_depth(dss_topo, depth, i % ncores);
		if (obj == NULL) {
			D_ERROR("Null core returned by hwloc\n");
			D_GOTO(failed, rc = -DER_INVAL);
		}

		/** ABT rank 0 is reserved for the primary xstream */
		rc = dss_start_one_xstream(obj->allowed_cpuset, i);
		if (rc)
			D_GOTO(failed, rc);
	}
	D_DEBUG(DB_TRACE, "%d execution streams successfully started\n",
		dss_nxstreams);
failed:
	dss_xstreams_open_barrier();
	if (dss_xstreams_empty()) /* started nothing */
		pthread_key_delete(dss_tls_key);

	return rc;
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

static struct dss_xstream *
dss_xstream_get(int stream_id)
{
	struct dss_xstream *dx = NULL;
	struct dss_xstream *tmp;

	if (stream_id == -1)
		return dss_get_module_info()->dmi_xstream;

	d_list_for_each_entry(tmp, &xstream_data.xd_list, dx_list) {
		if (tmp->dx_idx == stream_id) {
			dx = tmp;
			break;
		}
	}
	return dx;
}

/**
 * Create a ULT to execute \a func(\a arg). If \a ult is not NULL, the caller
 * is responsible for freeing the ULT handle with ABT_thread_free().
 *
 * \param[in]	func	function to execute
 * \param[in]	arg	argument for \a func
 * \param[out]	ult	ULT handle if not NULL
 */
int
dss_ult_create(void (*func)(void *), void *arg, int stream_id, ABT_thread *ult)
{
	struct dss_xstream	*dx;
	int			rc;

	dx = dss_xstream_get(stream_id);
	if (dx == NULL)
		return -DER_NONEXIST;

	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE], func, arg,
			       ABT_THREAD_ATTR_NULL, ult);

	return dss_abterr2der(rc);
}

/**
 * Create an ULT on each server xtream to execute a \a func(\a arg)
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \return		Success or negative error code
 *			0
 *			-DER_NOMEM
 *			-DER_INVAL
 */
int
dss_ult_create_all(void (*func)(void *), void *arg)
{
	struct dss_xstream      *dx;
	int			rc = 0;

	/*
	 * Create ULT for each stream in the target
	 */
	d_list_for_each_entry(dx, &xstream_data.xd_list, dx_list) {
		rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE], func, arg,
				       ABT_THREAD_ATTR_NULL,
				       NULL /* new thread */);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			break;
		}
	}
	return rc;
}

struct aggregator_arg_type {
	struct dss_stream_arg_type	at_args;
	int				at_rc;
	void				(*at_reduce)(void *a_args,
						     void *s_args);
};

/**
 * Collective operations among all server xstreams
 */
struct dss_future_arg {
	ABT_future	dfa_future;
	int		(*dfa_func)(void *);
	void		*dfa_arg;
	/** User callback for asynchronous mode */
	void		(*dfa_comp_cb)(void *);
	/** Argument for the user callback */
	void		*dfa_comp_arg;
	int		dfa_status;
	bool		dfa_async;
};

static void
dss_ult_create_execute_cb(void *data)
{
	struct dss_future_arg	*arg = data;
	int			rc;

	rc = arg->dfa_func(arg->dfa_arg);
	arg->dfa_status = rc;

	if (!arg->dfa_async)
		ABT_future_set(arg->dfa_future, (void *)(intptr_t)rc);
	else
		arg->dfa_comp_cb(arg->dfa_comp_arg);
}

/**
 * Create an ULT in synchornous or asynchronous mode
 * Sync: wait until it has been executed.
 * Async: return and call user callback from ULT.
 *
 * Note: This is
 * normally used when it needs to create an ULT on other xstream.
 *
 * \param[in]	func	function to execute
 * \param[in]	arg	argument for \a func
 * \param[in]	user_cb	user call back (mandatory for async mode)
 * \param[in]	arg	argument for \a user callback
 * \param[in]	stream_id indicate which xtream the ULT is executed.
 * \param[out]		error code.
 *
 */
int
dss_ult_create_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		       void *cb_args, int stream_id)
{
	struct dss_future_arg	future_arg;
	ABT_future		future;
	int			rc;

	memset(&future_arg, 0, sizeof(future_arg));
	future_arg.dfa_func = func;
	future_arg.dfa_arg = arg;
	future_arg.dfa_status = 0;

	if (user_cb == NULL) {
		rc = ABT_future_create(1, NULL, &future);
		if (rc != ABT_SUCCESS)
			return dss_abterr2der(rc);
		future_arg.dfa_future = future;
		future_arg.dfa_async  = false;
	} else {
		future_arg.dfa_comp_cb	= user_cb;
		future_arg.dfa_comp_arg = cb_args;
		future_arg.dfa_async	= true;
	}

	rc = dss_ult_create(dss_ult_create_execute_cb, &future_arg, stream_id,
			    NULL);
	if (rc)
		D_GOTO(free, rc);

	if (!future_arg.dfa_async)
		ABT_future_wait(future);
free:
	if (rc == 0)
		rc = future_arg.dfa_status;

	if (!future_arg.dfa_async)
		ABT_future_free(&future);

	return rc;
}

struct collective_arg {
	struct dss_future_arg		ca_future;
};

static void
collective_func(void *varg)
{
	struct dss_stream_arg_type	*a_args	= varg;
	struct collective_arg		*carg	= a_args->st_coll_args;
	struct dss_future_arg		*f_arg	= &carg->ca_future;
	int				rc;

	/** Update just the rc value */
	a_args->st_rc = f_arg->dfa_func(f_arg->dfa_arg);

	rc = ABT_future_set(f_arg->dfa_future, (void *)a_args);
	if (rc != ABT_SUCCESS)
		D_ERROR("future set failure %d\n", rc);
}

/* Reduce the return codes into the first element. */
static void
collective_reduce(void **arg)
{
	struct aggregator_arg_type	*aggregator;
	struct dss_stream_arg_type	*stream;
	int				*nfailed;
	int				 i;

	aggregator = (struct aggregator_arg_type *)arg[0];
	nfailed = &aggregator->at_args.st_rc;

	for (i = 1; i < dss_nxstreams + 1; i++) {
		stream = (struct dss_stream_arg_type *)arg[i];
		if (stream->st_rc != 0) {
			if (aggregator->at_rc == 0)
				aggregator->at_rc = stream->st_rc;
			(*nfailed)++;
		}

		/** optional custom aggregator call provided across streams */
		if (aggregator->at_reduce)
			aggregator->at_reduce(aggregator->at_args.st_arg,
					      stream->st_arg);
	}
}

static int
dss_collective_reduce_internal(struct dss_coll_ops *ops,
			       struct dss_coll_args *args, bool create_ult)
{
	struct collective_arg		carg;
	struct dss_coll_stream_args	*stream_args;
	struct dss_stream_arg_type	*stream;
	struct aggregator_arg_type	aggregator;
	struct dss_xstream		*dx;
	ABT_future			future;
	int				rc;
	int				tid;

	if (ops == NULL || args == NULL || ops->co_func == NULL) {
		D_DEBUG(DB_MD, "mandatory args mising dss_collective_reduce");
		return -DER_INVAL;
	}

	if (ops->co_reduce_arg_alloc != NULL &&
	    ops->co_reduce_arg_free == NULL) {
		D_DEBUG(DB_MD, "Free callback missing for reduce args\n");
		return -DER_INVAL;
	}

	if (dss_nxstreams == 0) {
		/* May happen when the server is shutting down. */
		D_DEBUG(DB_TRACE, "no xstreams\n");
		return -DER_CANCELED;
	}

	stream_args = &args->ca_stream_args;
	D_ALLOC(stream_args->csa_streams,
		(dss_nxstreams) * sizeof(struct dss_stream_arg_type));
	if (stream_args->csa_streams == NULL)
		return -DER_NOMEM;

	/*
	 * Use the first, extra element of the value array to store the number
	 * of failed tasks.
	 */
	rc = ABT_future_create(dss_nxstreams + 1, collective_reduce,
			       &future);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_streams, rc = dss_abterr2der(rc));

	carg.ca_future.dfa_future = future;
	carg.ca_future.dfa_func	= ops->co_func;
	carg.ca_future.dfa_arg	= args->ca_func_args;
	carg.ca_future.dfa_status = 0;

	memset(&aggregator, 0, sizeof(aggregator));
	if (ops->co_reduce) {
		aggregator.at_args.st_arg = args->ca_aggregator;
		aggregator.at_reduce	  = ops->co_reduce;
	}

	if (ops->co_reduce_arg_alloc)
		for (tid = 0; tid < dss_nxstreams; tid++) {
			stream = &stream_args->csa_streams[tid];
			ops->co_reduce_arg_alloc(stream,
						 aggregator.at_args.st_arg);
		}

	rc = ABT_future_set(future, (void *)&aggregator);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);

	tid = 0;
	d_list_for_each_entry(dx, &xstream_data.xd_list, dx_list) {
		stream			= &stream_args->csa_streams[tid];
		stream->st_coll_args	= &carg;

		if (create_ult)
			rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE],
					       collective_func, stream,
					       ABT_THREAD_ATTR_NULL, NULL);
		else
			rc = ABT_task_create(dx->dx_pools[DSS_POOL_SHARE],
					     collective_func, stream, NULL);

		if (rc != ABT_SUCCESS) {
			aggregator.at_args.st_rc = dss_abterr2der(rc);
			rc = ABT_future_set(future,
					    (void *)&aggregator);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
		}
		tid++;
	}

	ABT_future_wait(future);

	rc = aggregator.at_rc;

	ABT_future_free(&future);

	if (ops->co_reduce_arg_free)
		for (tid = 0; tid < dss_nxstreams; tid++)
			ops->co_reduce_arg_free(&stream_args->csa_streams[tid]);

out_streams:
	D_FREE(args->ca_stream_args.csa_streams);

	return rc;
}

/**
 * General case:
 * Execute \a task(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions. User specified
 * reduction functions for aggregation after collective
 *
 * \param[in] ops		All dss_collective ops to work on streams
 *				include \a func(\a arg) for collective on all
 *				server xstreams.
 * \param[in] args		All arguments required for dss_collective
 *				including func args.
 *
 * \return			number of failed xstreams or error code
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *args)
{
	return dss_collective_reduce_internal(ops, args, false);
}

/**
 * General case:
 * Execute \a ULT(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions. User specified
 * reduction functions for aggregation after collective
 *
 * \param[in] ops		All dss_collective ops to work on streams
 *				include \a func(\a arg) for collective on all
 *				server xstreams.
 * \param[in] args		All arguments required for dss_collective
 *				including func args.
 *
 * \return			number of failed xstreams or error code
 */
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *args)
{
	return dss_collective_reduce_internal(ops, args, true);
}

static int
dss_collective_internal(int (*func)(void *), void *arg, bool thread)
{
	int				rc;
	struct dss_coll_ops		coll_ops;
	struct dss_coll_args		coll_args;


	memset(&coll_ops, 0, sizeof(coll_ops));
	memset(&coll_args, 0, sizeof(coll_args));

	coll_ops.co_func	= func;
	coll_args.ca_func_args	= arg;

	if (thread)
		rc = dss_thread_collective_reduce(&coll_ops, &coll_args);
	else
		rc = dss_task_collective_reduce(&coll_ops, &coll_args);

	return rc;
}

/** TODO: use daos checksum library to offload checksum calculation */
static int
compute_checksum_ult(void *args)
{
	return 0;
}

/** TODO: use OFI calls to calculate checksum on FPGA */
static int
compute_checksum_acc(void *args)
{
	return 0;
}

/**
 * Generic offload call - abstraction for accelaration with
 *
 * \param[in] at_args	accelaration tasks with both ULT and FPGA
 */
int
dss_acc_offload(struct dss_acc_task *at_args)
{

	int		rc = 0;
	int		tid;


	/**
	 * Currently just launching it in this stream,
	 * ideally will move to a separate exclusive xstream
	 */
	tid = dss_get_module_info()->dmi_tid;
	if (at_args == NULL) {
		D_ERROR("missing arguments for acc_offload\n");
		return -DER_INVAL;
	}

	if (at_args->at_offload_type <= DSS_OFFLOAD_MIN ||
	    at_args->at_offload_type >= DSS_OFFLOAD_MAX) {
		D_ERROR("Unknown type of offload\n");
		return -DER_INVAL;
	}

	switch (at_args->at_offload_type) {
	case DSS_OFFLOAD_ULT:
		rc = dss_ult_create_execute(compute_checksum_ult,
					    at_args->at_params,
					    NULL /* user-cb */,
					    NULL /* user-cb args */,
					    tid);
		break;
	case DSS_OFFLOAD_ACC:
		/** calls to offload to FPGA*/
		rc = compute_checksum_acc(at_args->at_params);
		break;
	}

	return rc;
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
dss_task_collective(int (*func)(void *), void *arg)
{
	return dss_collective_internal(func, arg, false);
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
dss_thread_collective(int (*func)(void *), void *arg)
{
	return dss_collective_internal(func, arg, true);
}

static void
dss_tse_progress_ult(void *arg)
{
	struct dss_module_info *dmi = arg;

	while (true) {
		tse_sched_progress(&dmi->dmi_sched);
		/* XXX Check if this needs to be stopped */
		ABT_thread_yield();
	}
}

static int
generate_task_progress_ult(unsigned int type)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	int rc;

	if (dmi->dmi_tse_ult_created)
		return 0;

	D_ASSERT(type < DSS_POOL_CNT);
	rc = ABT_thread_create(dmi->dmi_xstream->dx_pools[type],
			       dss_tse_progress_ult, dmi,
			       ABT_THREAD_ATTR_NULL, NULL);
	if (rc)
		return rc;

	dmi->dmi_tse_ult_created = 1;
	return 0;
}

static int
dss_task_comp_cb(tse_task_t *task, void *arg)
{
	ABT_eventual *eventual = arg;

	ABT_eventual_set(*eventual, &task->dt_result, sizeof(task->dt_result));
	return 0;
}

/**
 * Call client side API on the server side asynchronously.
 */
int
dss_task_run(tse_task_t *task, unsigned int type, tse_task_cb_t cb, void *arg)
{
	ABT_eventual	eventual;
	int		*status;
	int		rc;

	/* Generate the progress task */
	rc = generate_task_progress_ult(type);
	if (rc)
		return rc;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	rc = dc_task_reg_comp_cb(task, dss_task_comp_cb, &eventual,
				 sizeof(eventual));
	if (rc != 0)
		D_GOTO(free_eventual, rc = -DER_NOMEM);

	if (cb != NULL) {
		rc = dc_task_reg_comp_cb(task, cb, arg, sizeof(arg));
		if (rc)
			D_GOTO(free_eventual, rc);
	}

	/* task will be freed inside scheduler */
	rc = dc_task_schedule(task, true);
	if (rc != 0)
		D_GOTO(free_eventual, rc = -DER_NOMEM);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(free_eventual, rc = dss_abterr2der(rc));

	rc = *status;

free_eventual:
	ABT_eventual_free(&eventual);
	return rc;
}

/*
 * Set parameters on the server.
 *
 * param key_id [IN]	key id
 * param value [IN]	the value of the key.
 *
 * return	0 if setting succeeds.
 *              negative errno if fails.
 */
int
dss_parameters_set(unsigned int key_id, uint64_t value)
{
	int rc = 0;

	switch (key_id) {
	case DSS_KEY_FAIL_LOC:
		daos_fail_loc_set(value);
		break;
	case DSS_REBUILD_RES_PERCENTAGE:
		if (value >= 100 || value == 0) {
			D_ERROR("invalid value "DF_U64"\n", value);
			rc = -DER_INVAL;
			break;
		}
		dss_rebuild_res_percentage = value;
		break;
	default:
		D_ERROR("invalid key_id %d\n", key_id);
		rc = -DER_INVAL;
	}

	return rc;
}

/**
 * Get nthreads number.
 */
unsigned int
dss_get_threads_number(void)
{
	return dss_nxstreams;
}

/** initializing steps */
enum {
	XD_INIT_NONE,
	XD_INIT_MUTEX,
	XD_INIT_ULT_INIT,
	XD_INIT_ULT_BARRIER,
	XD_INIT_REG_KEY,
	XD_INIT_XSTREAMS,
};

/**
 * Entry point to start up and shutdown the service
 */
int
dss_srv_fini(bool force)
{
	switch (xstream_data.xd_init_step) {
	default:
		D_ASSERT(0);
	case XD_INIT_XSTREAMS:
		dss_xstreams_fini(force);
		/* fall through */
	case XD_INIT_REG_KEY:
		dss_unregister_key(&daos_srv_modkey);
		/* fall through */
	case XD_INIT_ULT_BARRIER:
		ABT_cond_free(&xstream_data.xd_ult_barrier);
		/* fall through */
	case XD_INIT_ULT_INIT:
		ABT_cond_free(&xstream_data.xd_ult_init);
		/* fall through */
	case XD_INIT_MUTEX:
		ABT_mutex_free(&xstream_data.xd_mutex);
		/* fall through */
	case XD_INIT_NONE:
		D_DEBUG(DB_TRACE, "Finalized everything\n");
	}
	return 0;
}

int
dss_srv_init(int nr)
{
	int	rc;

	xstream_data.xd_init_step  = XD_INIT_NONE;
	xstream_data.xd_ult_signal = false;

	D_INIT_LIST_HEAD(&xstream_data.xd_list);
	rc = ABT_mutex_create(&xstream_data.xd_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(failed, rc);
	}
	xstream_data.xd_init_step = XD_INIT_MUTEX;

	rc = ABT_cond_create(&xstream_data.xd_ult_init);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(failed, rc);
	}
	xstream_data.xd_init_step = XD_INIT_ULT_INIT;

	rc = ABT_cond_create(&xstream_data.xd_ult_barrier);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(failed, rc);
	}
	xstream_data.xd_init_step = XD_INIT_ULT_BARRIER;

	/** register global tls accessible to all modules */
	dss_register_key(&daos_srv_modkey);
	xstream_data.xd_init_step = XD_INIT_REG_KEY;

	/* start xstreams */
	rc = dss_xstreams_init(nr);
	if (!dss_xstreams_empty()) /* cleanup if we started something */
		xstream_data.xd_init_step = XD_INIT_XSTREAMS;

	if (rc != 0)
		D_GOTO(failed, rc);

	return 0;
failed:
	dss_srv_fini(true);
	return rc;
}
