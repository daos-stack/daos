/*
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
/**
 * \file
 *
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
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>
#include <gurt/list.h>
#include "drpc_internal.h"
#include "srv_internal.h"

/**
 * DAOS server threading model:
 * 1) a set of "target XS (xstream) set" per server (dss_tgt_nr)
 * There is a "-c" option of daos_server to set the number.
 * For DAOS pool, one target XS set per VOS target to avoid extra lock when
 * accessing VOS file.
 * With in each target XS set, there is one "main XS":
 * 1.1) The tasks for main XS:
 *	RPC server of IO request handler,
 *	ULT server for:
 *		rebuild scanner/puller
 *		rebalance,
 *		aggregation,
 *		data scrubbing,
 *		pool service (tgt connect/disconnect etc),
 *		container open/close.
 *
 * And a set of "offload XS" (dss_tgt_offload_xs_nr)
 * Now dss_tgt_offload_xs_nr can be [0, 2].
 * 1.2) The tasks for offload XS:
 *	ULT server for:
 *		IO request dispatch (TX coordinator, on 1st offload XS),
 *		Acceleration of EC/checksum/compress (on 2nd offload XS if
 *		dss_tgt_offload_xs_nr is 2, or on 1st offload XS).
 *
 * 2) one "system XS set" per server (dss_sys_xs_nr)
 * The system XS set (now only one - the XS 0) is for some system level tasks:
 *	RPC server for:
 *		drpc listener,
 *		RDB request and meta-data service,
 *		management request for mgmt module,
 *		pool request,
 *		container request (including the OID allocate),
 *		rebuild request such as REBUILD_OBJECTS_SCAN/REBUILD_OBJECTS,
 *		rebuild status checker,
 *		rebalance request,
 *		IV, bcast, and SWIM message handling.
 *
 * Two helper functions:
 * 1) daos_rpc_tag() to query the target tag (context ID) of specific RPC
 *    request,
 * 2) dss_ult_xs() to query the XS id of the xstream for specific ULT task.
 */

/** Number of dRPC xstreams */
#define	DRPC_XS_NR	(1)
/** Number of offload XS per target [0, 2] */
unsigned int	dss_tgt_offload_xs_nr = 2;
/** number of target (XS set) per server */
unsigned int	dss_tgt_nr;
/** number of system XS */
unsigned int	dss_sys_xs_nr = DAOS_TGT0_OFFSET + DRPC_XS_NR;

unsigned int
dss_ctx_nr_get(void)
{
	return DSS_CTX_NR_TOTAL;
}

static void dss_gc_ult(void *args);

#define FIRST_DEFAULT_SCHEDULE_RATIO	80
#define REBUILD_DEFAULT_SCHEDULE_RATIO	30
unsigned int	dss_rebuild_res_percentage = REBUILD_DEFAULT_SCHEDULE_RATIO;
unsigned int	dss_first_res_percentage = FIRST_DEFAULT_SCHEDULE_RATIO;
bool		dss_agg_disabled;

bool
dss_aggregation_disabled(void)
{
	return dss_agg_disabled;
}

#define DSS_SYS_XS_NAME_FMT	"daos_sys_%d"
#define DSS_TGT_XS_NAME_FMT	"daos_tgt_%d_xs_%d"

struct dss_xstream_data {
	/** Initializing step, it is for cleanup of global states */
	int			  xd_init_step;
	int			  xd_ult_init_rc;
	bool			  xd_ult_signal;
	/** total number of XS including system XS, main XS and offload XS */
	int			  xd_xs_nr;
	/** created XS pointer array */
	struct dss_xstream	**xd_xs_ptrs;
	/** serialize initialization of ULTs */
	ABT_cond		  xd_ult_init;
	/** barrier for all ULTs to enter handling loop */
	ABT_cond		  xd_ult_barrier;
	ABT_mutex		  xd_mutex;
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

bool
dss_xstream_exiting(struct dss_xstream *dxs)
{
	ABT_bool state;
	int	 rc;

	rc = ABT_future_test(dxs->dx_shutdown, &state);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	return state == ABT_TRUE;
}

static ABT_unit
unit_pop(ABT_pool *pools, int pool_idx, ABT_pool *pool)
{
	ABT_unit unit;

	ABT_pool_pop(pools[pool_idx], &unit);
	if (unit != ABT_UNIT_NULL) {
		*pool = pools[pool_idx];
		return unit;
	}

	return ABT_UNIT_NULL;
}

static ABT_unit
normal_unit_pop(ABT_pool *pools, ABT_pool *pool)
{
	ABT_unit unit;

	/* Let's pop I/O request ULT first */
	unit = unit_pop(pools, DSS_POOL_PRIV, pool);
	if (unit != ABT_UNIT_NULL)
		return unit;

	/* Other request and collective ULT or created ULT */
	unit = unit_pop(pools, DSS_POOL_SHARE, pool);
	if (unit != ABT_UNIT_NULL)
		return unit;

	return ABT_UNIT_NULL;
}

/**
 * Choose ULT from the pool.
 * Firstly with dss_first_res_percentage to schedule the task in
 * DSS_POOL_URGENT, then with dss_rebuild_res_percentage (of left) to
 * schedule rebuild task.
 */
static ABT_unit
dss_sched_unit_pop(ABT_pool *pools, ABT_pool *pool)
{
	size_t		cnt;
	int		rc;

	/* pop highest priority pool first */
	rc = ABT_pool_get_size(pools[DSS_POOL_URGENT], &cnt);
	if (rc != ABT_SUCCESS)
		return ABT_UNIT_NULL;
	if (cnt != 0 && rand() % 100 <= dss_first_res_percentage)
		return unit_pop(pools, DSS_POOL_URGENT, pool);

	/* then pop other pools */
	rc = ABT_pool_get_size(pools[DSS_POOL_REBUILD], &cnt);
	if (rc != ABT_SUCCESS)
		return ABT_UNIT_NULL;

	if (cnt == 0 || rand() % 100 >= dss_rebuild_res_percentage)
		return normal_unit_pop(pools, pool);
	else
		return unit_pop(pools, DSS_POOL_REBUILD, pool);

	return ABT_UNIT_NULL;
}

static struct dss_xstream *
dss_xstream_get(int stream_id)
{
	if (stream_id == DSS_XS_SELF)
		return dss_get_module_info()->dmi_xstream;

	D_ASSERTF(stream_id >= 0 && stream_id < xstream_data.xd_xs_nr,
		  "invalid stream id %d (xstream_data.xd_xs_nr %d).\n",
		  stream_id, xstream_data.xd_xs_nr);

	return xstream_data.xd_xs_ptrs[stream_id];
}

/* Add to the sorted(by expire time) list */
static void
add_sleep_list(struct dss_xstream *dx, struct dss_sleep_ult *new)
{
	struct dss_sleep_ult	*dsu;

	d_list_for_each_entry(dsu, &dx->dx_sleep_ult_list, dsu_list) {
		if (dsu->dsu_expire_time > new->dsu_expire_time) {
			d_list_add_tail(&new->dsu_list, &dsu->dsu_list);
			return;
		}
	}

	d_list_add_tail(&new->dsu_list, &dx->dx_sleep_ult_list);
}

struct dss_sleep_ult
*dss_sleep_ult_create(void)
{
	struct dss_sleep_ult *dsu;
	ABT_thread	     self;

	D_ALLOC_PTR(dsu);
	if (dsu == NULL)
		return NULL;

	ABT_thread_self(&self);
	dsu->dsu_expire_time = 0;
	dsu->dsu_thread = self;
	D_INIT_LIST_HEAD(&dsu->dsu_list);

	return dsu;
}

void
dss_sleep_ult_destroy(struct dss_sleep_ult *dsu)
{
	D_ASSERT(d_list_empty(&dsu->dsu_list));
	D_FREE_PTR(dsu);
}

/* Reset the expire to force the ult to exit now */
void
dss_ult_wakeup(struct dss_sleep_ult *dsu)
{
	ABT_thread thread;

	ABT_thread_self(&thread);
	/* Only others can force the ULT to exit */
	D_ASSERT(thread != dsu->dsu_thread);
	d_list_del_init(&dsu->dsu_list);
	dsu->dsu_expire_time = 0;
	ABT_thread_resume(dsu->dsu_thread);
}

/* Schedule the ULT(dtu->ult) and reschedule in @expire_secs seconds */
void
dss_ult_sleep(struct dss_sleep_ult *dsu, uint64_t expire_secs)
{
	struct dss_xstream	*dx = dss_xstream_get(DSS_XS_SELF);
	ABT_thread		thread;
	uint64_t		now = 0;

	ABT_thread_self(&thread);
	D_ASSERT(thread == dsu->dsu_thread);

	D_ASSERT(d_list_empty(&dsu->dsu_list));
	daos_gettime_coarse(&now);
	dsu->dsu_expire_time = now + expire_secs;
	D_DEBUG(DB_TRACE, "dsu %p expire in "DF_U64" secs\n", dsu, expire_secs);
	add_sleep_list(dx, dsu);
	ABT_self_suspend();
}

static void
check_sleep_list()
{
	struct dss_xstream	*dx;
	uint64_t		now = 0;
	bool			shutdown = false;
	struct dss_sleep_ult	*dsu;
	struct dss_sleep_ult	*tmp;

	dx = dss_xstream_get(DSS_XS_SELF);
	if (dss_xstream_exiting(dx))
		shutdown = true;

	daos_gettime_coarse(&now);
	d_list_for_each_entry_safe(dsu, tmp, &dx->dx_sleep_ult_list,
				   dsu_list) {
		if (dsu->dsu_expire_time <= now || shutdown)
			dss_ult_wakeup(dsu);
		else
			break;
	}
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
			ABT_bool stop;

			ABT_sched_has_to_stop(sched, &stop);
			if (stop == ABT_TRUE) {
				D_DEBUG(DB_TRACE, "ABT_sched_has_to_stop!\n");
				break;
			}
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
	D_FREE(p_data);

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
	ret = ABT_sched_config_create(&config, cv_event_freq, 512,
				      ABT_sched_config_var_end);
	if (ret != ABT_SUCCESS)
		return dss_abterr2der(ret);

	ret = ABT_sched_create(&sched_def, pool_num, pools, config,
			       new_sched);
	ABT_sched_config_free(&config);

	return dss_abterr2der(ret);
}

/**
 * Process the rpc received, let's create a ABT thread for each request.
 */
int
dss_process_rpc(crt_context_t *ctx, crt_rpc_t *rpc,
		void (*real_rpc_hdlr)(void *), void *arg)
{
	unsigned int	mod_id = opc_get_mod_id(rpc->cr_opc);
	struct dss_module *module = dss_module_get(mod_id);
	ABT_pool	*pools = arg;
	ABT_pool	pool;
	int		rc;

	/* For RPC originally from CART might still come here, and its mod_id
	 * is 0xfe, and module would be NULL.
	 */
	if (module != NULL && module->sm_mod_ops != NULL &&
	    module->sm_mod_ops->dms_abt_pool_choose_cb)
		pool = module->sm_mod_ops->dms_abt_pool_choose_cb(rpc, pools);
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
	bool				 signal_caller = true;

	/** set affinity */
	rc = hwloc_set_cpubind(dss_topo, dx->dx_cpuset, HWLOC_CPUBIND_THREAD);
	if (rc) {
		D_ERROR("failed to set affinity: %d\n", errno);
		goto signal;
	}

	/* initialize xstream-local storage */
	dtc = dss_tls_init(DAOS_SERVER_TAG);
	if (dtc == NULL) {
		D_ERROR("failed to initialize TLS\n");
		goto signal;
	}

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);
	dmi->dmi_xs_id	= dx->dx_xs_id;
	dmi->dmi_tgt_id	= dx->dx_tgt_id;
	dmi->dmi_ctx_id	= -1;
	D_INIT_LIST_HEAD(&dmi->dmi_dtx_batched_list);

	if (dx->dx_comm) {
		/* create private transport context */
		rc = crt_context_create(&dmi->dmi_ctx);
		if (rc != 0) {
			D_ERROR("failed to create crt ctxt: %d\n", rc);
			goto tls_fini;
		}

		rc = crt_context_register_rpc_task(dmi->dmi_ctx,
						   dss_process_rpc,
						   dx->dx_pools);
		if (rc != 0) {
			D_ERROR("failed to register process cb %d\n", rc);
			goto crt_destroy;
		}

		/** Get context index from cart */
		rc = crt_context_idx(dmi->dmi_ctx, &dmi->dmi_ctx_id);
		if (rc != 0) {
			D_ERROR("failed to get xtream index: rc %d\n", rc);
			goto crt_destroy;
		}
		dx->dx_ctx_id = dmi->dmi_ctx_id;
		/** verify CART assigned the ctx_id ascendantly start from 0 */
		if (dx->dx_xs_id < dss_sys_xs_nr) {
			D_ASSERT(dx->dx_ctx_id == dx->dx_xs_id);
		} else {
			if (dx->dx_main_xs)
				D_ASSERTF(dx->dx_ctx_id ==
					  dx->dx_tgt_id + dss_sys_xs_nr -
					  DRPC_XS_NR,
					  "incorrect ctx_id %d for xs_id %d\n",
					  dx->dx_ctx_id, dx->dx_xs_id);
			else
				D_ASSERTF(dx->dx_ctx_id ==
					  (dss_sys_xs_nr + dss_tgt_nr +
					   dx->dx_tgt_id - DRPC_XS_NR),
					  "incorrect ctx_id %d for xs_id %d\n",
					  dx->dx_ctx_id, dx->dx_xs_id);
		}
	}

	/* Prepare the scheduler for DSC (Server call client API) */
	rc = tse_sched_init(&dx->dx_sched_dsc, NULL, dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("failed to init the scheduler\n");
		goto crt_destroy;
	}

	if (dx->dx_main_xs) {
		/* Initialize NVMe context for main XS which accesses NVME */
		rc = bio_xsctxt_alloc(&dmi->dmi_nvme_ctxt, dmi->dmi_tgt_id);
		if (rc != 0) {
			D_ERROR("failed to init spdk context for xstream(%d) "
				"rc:%d\n", dmi->dmi_xs_id, rc);
			D_GOTO(tse_fini, rc);
		}

		rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE],
				       dss_gc_ult, NULL,
				       ABT_THREAD_ATTR_NULL, NULL);
		if (rc != ABT_SUCCESS) {
			D_ERROR("create GC ULT failed: %d\n", rc);
			D_GOTO(nvme_fini, rc = dss_abterr2der(rc));
		}
	}

	dmi->dmi_xstream = dx;
	ABT_mutex_lock(xstream_data.xd_mutex);
	/* initialized everything for the ULT, notify the creater */
	D_ASSERT(!xstream_data.xd_ult_signal);
	xstream_data.xd_ult_signal = true;
	xstream_data.xd_ult_init_rc = 0;
	ABT_cond_signal(xstream_data.xd_ult_init);

	/* wait until all xstreams are ready, otherwise it is not safe
	 * to run lock-free dss_collective, althought this race is not
	 * realistically possible in the DAOS stack.
	 */
	ABT_cond_wait(xstream_data.xd_ult_barrier, xstream_data.xd_mutex);
	ABT_mutex_unlock(xstream_data.xd_mutex);

	signal_caller = false;
	/* main service progress loop */
	for (;;) {
		if (dx->dx_comm) {
			rc = crt_progress(dmi->dmi_ctx, 0 /* no wait */, NULL,
					  NULL);
			if (rc != 0 && rc != -DER_TIMEDOUT) {
				D_ERROR("failed to progress CART context: %d\n",
					rc);
				/* XXX Sometimes the failure might be just
				 * temporary, Let's keep progressing for now.
				 */
			}
		}

		if (dx->dx_main_xs)
			bio_nvme_poll(dmi->dmi_nvme_ctxt);

		if (dss_xstream_exiting(dx)) {
			check_sleep_list();
			break;
		}

		check_sleep_list();
		ABT_thread_yield();
	}

	D_ASSERT(d_list_empty(&dx->dx_sleep_ult_list));
	/* Let's wait until all of queue ULTs has been executed, in case dmi_ctx
	 * might be used by some other ULTs.
	 */
	while (1) {
		size_t total_size = 0;
		int i;

		for (i = 0; i < DSS_POOL_CNT; i++) {
			size_t pool_size;

			rc = ABT_pool_get_total_size(dx->dx_pools[i],
						     &pool_size);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
			total_size += pool_size;
		}
		if (total_size == 0)
			break;

		ABT_thread_yield();
	}
nvme_fini:
	if (dx->dx_main_xs)
		bio_xsctxt_free(dmi->dmi_nvme_ctxt);
tse_fini:
	tse_sched_fini(&dx->dx_sched_dsc);
crt_destroy:
	if (dx->dx_comm)
		crt_context_destroy(dmi->dmi_ctx, true);
tls_fini:
	dss_tls_fini(dtc);
signal:
	if (signal_caller) {
		ABT_mutex_lock(xstream_data.xd_mutex);
		/* initialized everything for the ULT, notify the creater */
		D_ASSERT(!xstream_data.xd_ult_signal);
		xstream_data.xd_ult_signal = true;
		xstream_data.xd_ult_init_rc = rc;
		ABT_cond_signal(xstream_data.xd_ult_init);
		ABT_mutex_unlock(xstream_data.xd_mutex);
	}
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

	return dx;

err_future:
	ABT_future_free(&dx->dx_shutdown);
err_free:
	D_FREE(dx);
	return NULL;
}

static inline void
dss_xstream_free(struct dss_xstream *dx)
{
	hwloc_bitmap_free(dx->dx_cpuset);
	D_FREE(dx);
}

/**
 * Start one xstream.
 *
 * \param[in] cpus	the cpuset to bind the xstream
 * \param[in] xs_id	the xs_id of xstream (start from 0)
 *
 * \retval	= 0 if starting succeeds.
 * \retval	negative errno if starting fails.
 */
static int
dss_start_one_xstream(hwloc_cpuset_t cpus, int xs_id)
{
	struct dss_xstream	*dx;
	ABT_thread_attr		attr = ABT_THREAD_ATTR_NULL;
	int			rc = 0;
	bool			comm; /* true to create cart ctx for RPC */
	int			xs_offset;
	int			i;

	/** allocate & init xstream configuration data */
	dx = dss_xstream_alloc(cpus);
	if (dx == NULL)
		return -DER_NOMEM;

	/** create pools */
	for (i = 0; i < DSS_POOL_CNT; i++) {
		ABT_pool_access access;

		/* for DSS_POOL_URGENT, now the only usage is for dtx_resync,
		 * that creates ULT in DSS_XS_SELF. So ABT_POOL_ACCESS_PRIV
		 * is fine.
		 */
		access = (i == DSS_POOL_SHARE || i == DSS_POOL_REBUILD ||
			  i == DSS_POOL_URGENT) ?
			 ABT_POOL_ACCESS_MPSC : ABT_POOL_ACCESS_PRIV;

		rc = ABT_pool_create_basic(ABT_POOL_FIFO, access, ABT_TRUE,
					   &dx->dx_pools[i]);
		if (rc != ABT_SUCCESS)
			D_GOTO(out_pool, rc = dss_abterr2der(rc));
	}

	/* Partial XS need the RPC communication ability - system XS, each
	 * main XS and its first offload XS (for IO dispatch).
	 * The 2nd offload XS(if exists) does not need RPC communication
	 * as it is only for EC/checksum/compress offloading.
	 */
	xs_offset = xs_id < dss_sys_xs_nr ? -1 : DSS_XS_OFFSET_IN_TGT(xs_id);
	comm = (xs_id == 0) || xs_offset == 0 || xs_offset == 1;
	dx->dx_tgt_id	= dss_xs2tgt(xs_id);
	if (xs_id < dss_sys_xs_nr) {
		snprintf(dx->dx_name, DSS_XS_NAME_LEN, DSS_SYS_XS_NAME_FMT,
			 xs_id);
	} else {
		snprintf(dx->dx_name, DSS_XS_NAME_LEN, DSS_TGT_XS_NAME_FMT,
			 dx->dx_tgt_id, xs_offset + 1);
	}
	dx->dx_xs_id	= xs_id;
	dx->dx_ctx_id	= -1;
	dx->dx_comm	= comm;
	dx->dx_main_xs	= xs_id >= dss_sys_xs_nr && xs_offset == 0;
	dx->dx_dsc_started = false;
	D_INIT_LIST_HEAD(&dx->dx_sleep_ult_list);

	rc = dss_sched_create(dx->dx_pools, DSS_POOL_CNT, &dx->dx_sched);
	if (rc != 0) {
		D_ERROR("create scheduler fails: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	/** start XS, ABT rank 0 is reserved for the primary xstream */
	rc = ABT_xstream_create_with_rank(dx->dx_sched, xs_id + 1,
					  &dx->dx_xstream);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create xstream fails %d\n", rc);
		D_GOTO(out_sched, rc = dss_abterr2der(rc));
	}

	rc = ABT_thread_attr_create(&attr);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_thread_attr_create fails %d\n", rc);
		D_GOTO(out_xstream, rc = dss_abterr2der(rc));
	}

	rc = ABT_thread_attr_set_stacksize(attr, 65536);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_thread_attr_set_stacksize fails %d\n", rc);
		D_GOTO(out_xstream, rc = dss_abterr2der(rc));
	}

	/** start progress ULT */
	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE],
			       dss_srv_handler, dx, attr,
			       &dx->dx_progress);
	if (rc != ABT_SUCCESS) {
		D_ERROR("create progress ULT failed: %d\n", rc);
		D_GOTO(out_xstream, rc = dss_abterr2der(rc));
	}

	ABT_mutex_lock(xstream_data.xd_mutex);

	if (!xstream_data.xd_ult_signal)
		ABT_cond_wait(xstream_data.xd_ult_init, xstream_data.xd_mutex);
	xstream_data.xd_ult_signal = false;
	rc = xstream_data.xd_ult_init_rc;
	if (rc != 0) {
		ABT_mutex_unlock(xstream_data.xd_mutex);
		goto out_xstream;
	}
	xstream_data.xd_xs_ptrs[xs_id] = dx;
	ABT_mutex_unlock(xstream_data.xd_mutex);
	ABT_thread_attr_free(&attr);

	D_DEBUG(DB_TRACE, "created xstream name(%s)xs_id(%d)/tgt_id(%d)/"
		"ctx_id(%d)/comm(%d)/is_main_xs(%d).\n",
		dx->dx_name, dx->dx_xs_id, dx->dx_tgt_id, dx->dx_ctx_id,
		dx->dx_comm, dx->dx_main_xs);

	return 0;
out_xstream:
	if (attr != ABT_THREAD_ATTR_NULL)
		ABT_thread_attr_free(&attr);
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
	int			 i;
	int			 rc;

	D_DEBUG(DB_TRACE, "Stopping execution streams\n");

	/** Stop & free progress ULTs */
	for (i = 0; i < xstream_data.xd_xs_nr; i++) {
		dx = xstream_data.xd_xs_ptrs[i];
		if (dx == NULL)
			continue;
		ABT_future_set(dx->dx_shutdown, dx);
	}
	for (i = 0; i < xstream_data.xd_xs_nr; i++) {
		dx = xstream_data.xd_xs_ptrs[i];
		if (dx == NULL)
			continue;
		ABT_thread_join(dx->dx_progress);
		ABT_thread_free(&dx->dx_progress);
		ABT_future_free(&dx->dx_shutdown);
	}

	/** Wait for each execution stream to complete */
	for (i = 0; i < xstream_data.xd_xs_nr; i++) {
		dx = xstream_data.xd_xs_ptrs[i];
		if (dx == NULL)
			continue;
		ABT_xstream_join(dx->dx_xstream);
		ABT_xstream_free(&dx->dx_xstream);
	}

	/** housekeeping ... */
	for (i = 0; i < xstream_data.xd_xs_nr; i++) {
		dx = xstream_data.xd_xs_ptrs[i];
		if (dx == NULL)
			continue;
		ABT_sched_free(&dx->dx_sched);
		dss_xstream_free(dx);
		xstream_data.xd_xs_ptrs[i] = NULL;
	}

	/* All other xstreams have terminated. */
	xstream_data.xd_xs_nr = 0;
	dss_tgt_nr = 0;

	/* release local storage */
	rc = pthread_key_delete(dss_tls_key);
	if (rc)
		D_ERROR("failed to delete dtc: %d\n", rc);

	D_DEBUG(DB_TRACE, "Execution streams stopped\n");
}

void
dss_xstreams_open_barrier(void)
{
	ABT_mutex_lock(xstream_data.xd_mutex);
	ABT_cond_broadcast(xstream_data.xd_ult_barrier);
	ABT_mutex_unlock(xstream_data.xd_mutex);
}

static bool
dss_xstreams_empty(void)
{
	return xstream_data.xd_xs_nr == 0;
}

static int
dss_start_xs_id(int xs_id)
{
	hwloc_obj_t	obj;
	int		rc;
	int		xs_core_offset;
	unsigned	idx;
	char		*cpuset;

	D_DEBUG(DB_TRACE, "start xs_id called for %d.  ", xs_id);
	/* if we are NUMA aware, use the NUMA information */
	if (numa_obj) {
		idx = hwloc_bitmap_first(core_allocation_bitmap);
		if (idx == -1) {
			D_DEBUG(DB_TRACE,
				"No core available for XS: %d", xs_id);
			return -DER_INVAL;
		}
		D_DEBUG(DB_TRACE,
			"Choosing next available core index %d.", idx);
		hwloc_bitmap_clr(core_allocation_bitmap, idx);

		obj = hwloc_get_obj_by_depth(dss_topo, dss_core_depth, idx);
		if (obj == NULL) {
			D_PRINT("Null core returned by hwloc\n");
			return -DER_INVAL;
		}

		hwloc_bitmap_asprintf(&cpuset, obj->allowed_cpuset);
		D_DEBUG(DB_TRACE, "Using CPU set %s\n", cpuset);
		free(cpuset);
	} else {
		D_DEBUG(DB_TRACE, "Using non-NUMA aware core allocation\n");
		/*
		* System XS all use the first core
		*/
		if (xs_id < dss_sys_xs_nr)
			xs_core_offset = 0;
		else
			xs_core_offset = xs_id - (dss_sys_xs_nr - DRPC_XS_NR);

		obj = hwloc_get_obj_by_depth(dss_topo, dss_core_depth,
					     (xs_core_offset + dss_core_offset)
					     % dss_core_nr);
		if (obj == NULL) {
			D_ERROR("Null core returned by hwloc for XS %d\n",
				xs_id);
			return -DER_INVAL;
		}
	}

	rc = dss_start_one_xstream(obj->allowed_cpuset, xs_id);
	if (rc)
		return rc;

	return 0;
}

static int
dss_xstreams_init(void)
{
	int	rc;
	int	i, xs_id;

	D_ASSERT(dss_tgt_nr >= 1);
	D_ASSERT(dss_tgt_offload_xs_nr == 0 || dss_tgt_offload_xs_nr == 1 ||
		 dss_tgt_offload_xs_nr == 2);

	/* initialize xstream-local storage */
	rc = pthread_key_create(&dss_tls_key, NULL);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the execution streams */
	D_DEBUG(DB_TRACE,
		"%d cores total detected "
		"starting %d main xstreams\n",
		dss_core_nr, dss_tgt_nr);

	if (dss_numa_node != -1) {
		D_DEBUG(DB_TRACE,
			"Detected %d cores on NUMA node %d\n",
			dss_num_cores_numa_node, dss_numa_node);
	}

	xstream_data.xd_xs_nr = DSS_XS_NR_TOTAL;
	/* start system service XS */
	for (i = 0; i < dss_sys_xs_nr; i++) {
		xs_id = i;
		rc = dss_start_xs_id(xs_id);
		if (rc)
			D_GOTO(out, rc);
	}

	/* start main IO service XS */
	for (i = 0; i < dss_tgt_nr; i++) {
		xs_id = DSS_MAIN_XS_ID(i);
		rc = dss_start_xs_id(xs_id);
		if (rc)
			D_GOTO(out, rc);
	}

	/* start offload XS if any */
	if (dss_tgt_offload_xs_nr == 0)
		D_GOTO(out, rc);
	for (i = 0; i < dss_tgt_nr; i++) {
		int j;

		for (j = 0; j < dss_tgt_offload_xs_nr; j++) {
			xs_id = DSS_MAIN_XS_ID(i) + j + 1;
			rc = dss_start_xs_id(xs_id);
			if (rc)
				D_GOTO(out, rc);
		}
	}

	D_DEBUG(DB_TRACE, "%d execution streams successfully started "
		"(first core %d)\n", dss_tgt_nr, dss_core_offset);
out:
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

	D_FREE(info);
}

struct dss_module_key daos_srv_modkey = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dss_srv_tls_init,
	.dmk_fini = dss_srv_tls_fini,
};

/**
 * Create a ULT to execute \a func(\a arg). If \a ult is not NULL, the caller
 * is responsible for freeing the ULT handle with ABT_thread_free().
 *
 * \param[in]	func	function to execute
 * \param[in]	arg	argument for \a func
 * \param[in]	stream_id on which xstream to create the ULT.
 * \param[in]	stack_size stacksize of the ULT, if it is 0, then create
 *			default size of ULT.
 * \param[in]	pool	ULT pool type indicates where the ULT is created.
 *
 * \param[out]	ult	ULT handle if not NULL
 */
static int
dss_ult_pool_create(void (*func)(void *), void *arg, int stream_id,
		    size_t stack_size, ABT_thread *ult, int pool)
{
	ABT_thread_attr		attr;
	struct dss_xstream	*dx;
	int			rc;
	int			rc1;

	dx = dss_xstream_get(stream_id);
	if (dx == NULL)
		return -DER_NONEXIST;

	if (stack_size > 0) {
		rc = ABT_thread_attr_create(&attr);
		if (rc != ABT_SUCCESS)
			return dss_abterr2der(rc);

		rc = ABT_thread_attr_set_stacksize(attr, stack_size);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		D_DEBUG(DB_TRACE, "Create ult stacksize is %zd\n", stack_size);
	} else {
		attr = ABT_THREAD_ATTR_NULL;
	}

	rc = ABT_thread_create(dx->dx_pools[pool], func, arg,
			       attr, ult);

free:
	if (attr != ABT_THREAD_ATTR_NULL) {
		rc1 = ABT_thread_attr_free(&attr);
		if (rc == ABT_SUCCESS)
			rc = rc1;
	}

	return dss_abterr2der(rc);
}

/**
 * Create the ult, will internally select the pool and XS based on ult_type
 * and tgt_idx.
 */
int
dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_idx,
	       size_t stack_size, ABT_thread *ult)
{
	return dss_ult_pool_create(func, arg, dss_ult_xs(ult_type, tgt_idx),
				   stack_size, ult, dss_ult_pool(ult_type));
}

/**
 * Create an ULT on each server xtream to execute a \a func(\a arg)
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] main	only create ULT on main XS or not.
 * \return		Success or negative error code
 *			0
 *			-DER_NOMEM
 *			-DER_INVAL
 */
int
dss_ult_create_all(void (*func)(void *), void *arg, bool main)
{
	struct dss_xstream      *dx;
	int			 i;
	int			 rc = 0;

	for (i = 0; i < xstream_data.xd_xs_nr; i++) {
		dx = xstream_data.xd_xs_ptrs[i];
		if (main && !dx->dx_main_xs)
			continue;

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
	void				(*at_reduce)(void *a_args,
						     void *s_args);
	int				at_rc;
	int				at_xs_nr;
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
 * \param[in]	func		function to execute
 * \param[in]	arg		argument for \a func
 * \param[in]	user_cb		user call back (mandatory for async mode)
 * \param[in]	arg		argument for \a user callback
 * \param[in]	ult_type	type of ULT
 * \param[in]	tgt_id		target index
 * \param[out]			error code.
 *
 */
int
dss_ult_create_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		       void *cb_args, int ult_type, int tgt_id,
		       size_t stack_size)
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

	rc = dss_ult_create(dss_ult_create_execute_cb, &future_arg,
			    ult_type, tgt_id, stack_size, NULL);
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

	for (i = 1; i < aggregator->at_xs_nr + 1; i++) {
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
			       struct dss_coll_args *args, bool create_ult,
			       int flag)
{
	struct collective_arg		carg;
	struct dss_coll_stream_args	*stream_args;
	struct dss_stream_arg_type	*stream;
	struct aggregator_arg_type	aggregator;
	struct dss_xstream		*dx;
	ABT_future			future;
	int				xs_nr;
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

	if (dss_tgt_nr == 0) {
		/* May happen when the server is shutting down. */
		D_DEBUG(DB_TRACE, "no xstreams\n");
		return -DER_CANCELED;
	}

	xs_nr = dss_tgt_nr;
	stream_args = &args->ca_stream_args;
	D_ALLOC_ARRAY(stream_args->csa_streams, xs_nr);
	if (stream_args->csa_streams == NULL)
		return -DER_NOMEM;

	/*
	 * Use the first, extra element of the value array to store the number
	 * of failed tasks.
	 */
	rc = ABT_future_create(xs_nr + 1, collective_reduce, &future);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_streams, rc = dss_abterr2der(rc));

	carg.ca_future.dfa_future = future;
	carg.ca_future.dfa_func	= ops->co_func;
	carg.ca_future.dfa_arg	= args->ca_func_args;
	carg.ca_future.dfa_status = 0;

	memset(&aggregator, 0, sizeof(aggregator));
	aggregator.at_xs_nr = xs_nr;
	if (ops->co_reduce) {
		aggregator.at_args.st_arg = args->ca_aggregator;
		aggregator.at_reduce	  = ops->co_reduce;
	}

	if (ops->co_reduce_arg_alloc)
		for (tid = 0; tid < xs_nr; tid++) {
			stream = &stream_args->csa_streams[tid];
			rc = ops->co_reduce_arg_alloc(stream,
						     aggregator.at_args.st_arg);
			if (rc)
				D_GOTO(out_future, rc);
		}

	rc = ABT_future_set(future, (void *)&aggregator);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);

	for (tid = 0; tid < xs_nr; tid++) {
		stream			= &stream_args->csa_streams[tid];
		stream->st_coll_args	= &carg;

		if (args->ca_exclude_tgts_cnt) {
			int i;

			for (i = 0; i < args->ca_exclude_tgts_cnt; i++)
				if (args->ca_exclude_tgts[i] == tid)
					break;

			if (i < args->ca_exclude_tgts_cnt) {
				D_DEBUG(DB_TRACE, "Skip tgt %d\n", tid);
				rc = ABT_future_set(future, (void *)stream);
				D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
				continue;
			}
		}

		dx = dss_xstream_get(DSS_MAIN_XS_ID(tid));
		if (create_ult)
			rc = ABT_thread_create(dx->dx_pools[DSS_POOL_SHARE],
					       collective_func, stream,
					       ABT_THREAD_ATTR_NULL, NULL);
		else
			rc = ABT_task_create(dx->dx_pools[DSS_POOL_SHARE],
					     collective_func, stream, NULL);

		if (rc != ABT_SUCCESS) {
			stream->st_rc = dss_abterr2der(rc);
			rc = ABT_future_set(future, (void *)stream);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
		}
	}

	ABT_future_wait(future);

	rc = aggregator.at_rc;

out_future:
	ABT_future_free(&future);

	if (ops->co_reduce_arg_free)
		for (tid = 0; tid < xs_nr; tid++)
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
 * \param[in] flag		collective flag, reserved for future usage.
 *
 * \return			number of failed xstreams or error code
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *args, int flag)
{
	return dss_collective_reduce_internal(ops, args, false, flag);
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
 * \param[in] flag		collective flag, reserved for future usage.
 *
 * \return			number of failed xstreams or error code
 */
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *args, int flag)
{
	return dss_collective_reduce_internal(ops, args, true, flag);
}

static int
dss_collective_internal(int (*func)(void *), void *arg, bool thread, int flag)
{
	int				rc;
	struct dss_coll_ops		coll_ops = { 0 };
	struct dss_coll_args		coll_args = { 0 };

	coll_ops.co_func	= func;
	coll_args.ca_func_args	= arg;

	if (thread)
		rc = dss_thread_collective_reduce(&coll_ops, &coll_args, flag);
	else
		rc = dss_task_collective_reduce(&coll_ops, &coll_args, flag);

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
	tid = dss_get_module_info()->dmi_tgt_id;
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
				DSS_ULT_CHECKSUM, tid,
				0);
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
 * \param[in] flag	collective flag, reserved for future usage.
 *
 * \return		number of failed xstreams or error code
 */
int
dss_task_collective(int (*func)(void *), void *arg, int flag)
{
	return dss_collective_internal(func, arg, false, flag);
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] flag	collective flag, reserved for future usage.
 *
 * \return		number of failed xstreams or error code
 */

int
dss_thread_collective(int (*func)(void *), void *arg, int flag)
{
	return dss_collective_internal(func, arg, true, flag);
}

/*
 * Set parameters on the server.
 *
 * param key_id [IN]		key id
 * param value [IN]		the value of the key.
 * param value_extra [IN]	the extra value of the key.
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
	case DSS_KEY_FAIL_VALUE:
		daos_fail_value_set(value);
		break;
	case DSS_KEY_FAIL_NUM:
		daos_fail_num_set(value);
	case DSS_REBUILD_RES_PERCENTAGE:
		if (value >= 100) {
			D_ERROR("invalid value "DF_U64"\n", value);
			rc = -DER_INVAL;
			break;
		}
		D_WARN("set rebuild percentage to "DF_U64"\n", value);
		dss_rebuild_res_percentage = value;
		break;
	case DSS_DISABLE_AGGREGATION:
		dss_agg_disabled = (value != 0);
		D_WARN("online aggregation is %s\n",
		       value != 0 ? "disabled" : "enabled");
		break;
	default:
		D_ERROR("invalid key_id %d\n", key_id);
		rc = -DER_INVAL;
	}

	return rc;
}

/** initializing steps */
enum {
	XD_INIT_NONE,
	XD_INIT_MUTEX,
	XD_INIT_ULT_INIT,
	XD_INIT_ULT_BARRIER,
	XD_INIT_REG_KEY,
	XD_INIT_NVME,
	XD_INIT_XSTREAMS,
	XD_INIT_DRPC,
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
	case XD_INIT_DRPC:
		drpc_listener_fini();
		/* fall through */
	case XD_INIT_XSTREAMS:
		dss_xstreams_fini(force);
		/* fall through */
	case XD_INIT_NVME:
		bio_nvme_fini();
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
		if (xstream_data.xd_xs_ptrs != NULL)
			D_FREE(xstream_data.xd_xs_ptrs);
		D_DEBUG(DB_TRACE, "Finalized everything\n");
	}
	return 0;
}

int
dss_srv_init()
{
	int	rc;

	xstream_data.xd_init_step  = XD_INIT_NONE;
	xstream_data.xd_ult_signal = false;

	D_ALLOC_ARRAY(xstream_data.xd_xs_ptrs, DSS_XS_NR_TOTAL);
	if (xstream_data.xd_xs_ptrs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);
	xstream_data.xd_xs_nr = 0;

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

	rc = bio_nvme_init(dss_storage_path, dss_nvme_conf, dss_nvme_shm_id,
		dss_nvme_mem_size);
	if (rc != 0)
		D_GOTO(failed, rc);
	xstream_data.xd_init_step = XD_INIT_NVME;

	/* start xstreams */
	rc = dss_xstreams_init();
	if (!dss_xstreams_empty()) /* cleanup if we started something */
		xstream_data.xd_init_step = XD_INIT_XSTREAMS;

	if (rc != 0)
		D_GOTO(failed, rc);

	/* start up drpc listener */
	rc = drpc_listener_init();
	if (rc != 0)
		D_GOTO(failed, rc);
	xstream_data.xd_init_step = XD_INIT_DRPC;

	return 0;
failed:
	dss_srv_fini(true);
	return rc;
}

void
dss_dump_ABT_state()
{
	int			rc, num_pools, i, idx;
	struct dss_xstream	*dx;
	ABT_sched		sched;
	ABT_pool		pools[DSS_POOL_CNT];

	rc = ABT_info_print_all_xstreams(stderr);
	if (rc != ABT_SUCCESS)
		D_ERROR("ABT_info_print_all_xstreams() error, rc = %d\n", rc);

	ABT_mutex_lock(xstream_data.xd_mutex);
	for (idx = 0; idx < xstream_data.xd_xs_nr; idx++) {
		dx = xstream_data.xd_xs_ptrs[idx];
		rc = ABT_info_print_xstream(stderr, dx->dx_xstream);
		if (rc != ABT_SUCCESS)
			D_ERROR("ABT_info_print_xstream() error, rc = %d, for "
				"DAOS xstream %p, ABT xstream %p\n", rc, dx,
				dx->dx_xstream);
		/* one progress ULT per xstream */
		if (dx->dx_progress != ABT_THREAD_NULL) {
			rc = ABT_info_print_thread(stderr, dx->dx_progress);
			if (rc != ABT_SUCCESS)
				D_ERROR("ABT_info_print_thread() error, "
					"rc = %d, for DAOS xstream %p, ABT "
					"xstream %p, progress ULT %p\n", rc, dx,
					dx->dx_xstream, dx->dx_progress);
		}
		/* only one sched per xstream */
		rc = ABT_xstream_get_main_sched(dx->dx_xstream, &sched);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_xstream_get_main_sched() error, rc = %d, "
				"for DAOS xstream %p, ABT xstream %p\n", rc, dx,
				dx->dx_xstream);
		} else if (sched != dx->dx_sched) {
			/* it's unexpected, unless DAOS will use stacked
			 * schedulers at some point of time, but try to
			 * continue anyway instead to abort
			 */
			D_WARN("DAOS xstream main sched %p differs from ABT "
			       "registered one %p, dumping both\n",
			       dx->dx_sched, sched);
			rc = ABT_info_print_sched(stderr, sched);
			if (rc != ABT_SUCCESS)
				D_ERROR("ABT_info_print_sched() error, rc = "
					"%d, for DAOS xstream %p, ABT xstream "
					"%p, sched %p\n", rc, dx,
					dx->dx_xstream, sched);
		}
		rc = ABT_info_print_sched(stderr, dx->dx_sched);
		if (rc != ABT_SUCCESS)
			D_ERROR("ABT_info_print_sched() error, rc = %d, for "
				"DAOS xstream %p, ABT xstream %p, sched %p\n",
				rc, dx, dx->dx_xstream, dx->dx_sched);

		/* only DSS_POOL_CNT (DSS_POOL_PRIV/DSS_POOL_SHARE/
		 * DSS_POOL_REBUILD) per sched/xstream
		 */
		rc = ABT_sched_get_num_pools(dx->dx_sched, &num_pools);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_sched_get_num_pools() error, rc = %d, for "
				"DAOS xstream %p, ABT xstream %p, sched %p\n",
				rc, dx, dx->dx_xstream, dx->dx_sched);
			continue;
		}
		if (num_pools != DSS_POOL_CNT)
			D_WARN("DAOS xstream %p, ABT xstream %p, sched %p "
				"number of pools %d != %d\n", dx,
				dx->dx_xstream, dx->dx_sched, num_pools,
				DSS_POOL_CNT);
		rc = ABT_sched_get_pools(dx->dx_sched, num_pools, 0, pools);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_sched_get_pools() error, rc = %d, for "
				"DAOS xstream %p, ABT xstream %p, sched %p\n",
				rc, dx, dx->dx_xstream, dx->dx_sched);
			continue;
		}
		for (i = 0; i < num_pools; i++) {
			if (pools[i] == ABT_POOL_NULL) {
				D_WARN("DAOS xstream %p, ABT xstream %p, "
				       "sched %p, no pool[%d]\n", dx,
				       dx->dx_xstream, dx->dx_sched, i);
				continue;
			}
			if (pools[i] != dx->dx_pools[i]) {
				D_WARN("DAOS xstream pool[%d]=%p differs from "
				       "ABT registered one %p for sched %p\n",
				       i, dx->dx_pools[i], pools[i],
				       dx->dx_sched);
			}
			rc = ABT_info_print_pool(stderr, pools[i]);
			if (rc != ABT_SUCCESS)
				D_ERROR("ABT_info_print_pool() error, rc = %d, "
					"for DAOS xstream %p, ABT xstream %p, "
					"sched %p, pool[%d]\n", rc, dx,
					dx->dx_xstream, dx->dx_sched, i);
		}
		/* XXX last, each pool's ULTs infos (and stacks?!) will need to
		 * be also dumped, when a new pool method will be available to
		 * list all ULTs in a pool (ABT issue #12)
		 */
	}
	ABT_mutex_unlock(xstream_data.xd_mutex);
}

void
dss_gc_run(daos_handle_t poh, int credits)
{
	struct dss_xstream *dxs	 = dss_get_xstream();
	int		    total = 0;

	while (1) {
		int	creds = DSS_GC_CREDS;
		int	rc;

		if (credits > 0 && (credits - total) < creds)
			creds = credits - total;

		total += creds;
		if (daos_handle_is_inval(poh))
			rc = vos_gc_run(&creds);
		else
			rc = vos_gc_pool(poh, &creds);

		if (rc) {
			D_ERROR("GC run failed: %s\n", d_errstr(rc));
			break;
		}
		total -= creds; /* subtract the remainded credits */
		if (creds != 0)
			break; /* reclaimed everything */

		if (credits > 0 && total >= credits)
			break; /* consumed all credits */

		if (dss_xstream_exiting(dxs))
			break;

		ABT_thread_yield();
	}

	if (total != 0) /* did something */
		D_DEBUG(DB_TRACE, "GC consumed %d credits\n", total);
}

static void
dss_gc_ult(void *args)
{
	 struct dss_xstream *dxs  = dss_get_xstream();

	 while (!dss_xstream_exiting(dxs)) {
		/* -1 means GC will run until there is nothing to do */
		dss_gc_run(DAOS_HDL_INVAL, -1);
		ABT_thread_yield();
	 }
}
