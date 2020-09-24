/*
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
#include <daos_mgmt.h>
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
 * Helper function:
 * daos_rpc_tag() to query the target tag (context ID) of specific RPC request.
 */

/** Number of dRPC xstreams */
#define DRPC_XS_NR	(1)
/** Number of offload XS */
unsigned int	dss_tgt_offload_xs_nr;
/** Number of target (XS set) per server */
unsigned int	dss_tgt_nr;
/** Number of system XS */
unsigned int	dss_sys_xs_nr = DAOS_TGT0_OFFSET + DRPC_XS_NR;
/**
 * Flag of helper XS as a pool.
 * false - the helper XS is near its main IO service XS. When there is one or
 *         2 helper XS for each VOS target (dss_tgt_offload_xs_nr % dss_tgt_nr
 *         == 0), we create each VOS target's IO service XS and then its helper
 *         XS, and each VOS has its own helper XS.
 * true  - When there is no enough cores/XS to create one or two helpers for
 *         VOS target (dss_tgt_offload_xs_nr % dss_tgt_nr != 0), we firstly
 *         create all VOS targets' IO service XS, and then all helper XS that
 *         are shared used by all VOS targets.
 */
bool		dss_helper_pool;

unsigned int
dss_ctx_nr_get(void)
{
	return DSS_CTX_NR_TOTAL;
}

#define DSS_SYS_XS_NAME_FMT	"daos_sys_%d"
#define DSS_IO_XS_NAME_FMT	"daos_io_%d"
#define DSS_OFFLOAD_XS_NAME_FMT	"daos_off_%d"

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

bool
dss_xstream_exiting(struct dss_xstream *dxs)
{
	ABT_bool state;
	int	 rc;

	rc = ABT_future_test(dxs->dx_shutdown, &state);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	return state == ABT_TRUE;
}

int
dss_xstream_cnt(void)
{
	return xstream_data.xd_xs_nr;
}

struct dss_xstream *
dss_get_xstream(int stream_id)
{
	if (stream_id == DSS_XS_SELF)
		return dss_current_xstream();

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

	/* Wakeup the thread if it was put in the sleep list */
	if (!d_list_empty(&dsu->dsu_list)) {
		ABT_thread_self(&thread);
		/* Only others can force the ULT to exit */
		D_ASSERT(thread != dsu->dsu_thread);
		d_list_del_init(&dsu->dsu_list);
		dsu->dsu_expire_time = 0;
		ABT_thread_resume(dsu->dsu_thread);
	}
}

/* Schedule the ULT(dtu->ult) and reschedule in @expire_secs nano seconds */
void
dss_ult_sleep(struct dss_sleep_ult *dsu, uint64_t expire_nsecs)
{
	struct dss_xstream	*dx = dss_current_xstream();
	ABT_thread		thread;
	uint64_t		now = 0;

	D_ASSERT(dsu != NULL);
	ABT_thread_self(&thread);
	D_ASSERT(thread == dsu->dsu_thread);

	D_ASSERT(d_list_empty(&dsu->dsu_list));
	now = daos_getntime_coarse();
	dsu->dsu_expire_time = now + expire_nsecs;
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

	dx = dss_current_xstream();
	if (dss_xstream_exiting(dx))
		shutdown = true;

	if (d_list_empty(&dx->dx_sleep_ult_list))
		return;

	now = daos_getntime_coarse();
	d_list_for_each_entry_safe(dsu, tmp, &dx->dx_sleep_ult_list, dsu_list) {
		if (dsu->dsu_expire_time <= now || shutdown)
			dss_ult_wakeup(dsu);
		else
			break;
	}
}

/**
 * sleep micro seconds, then being rescheduled.
 * \param[in]	us	milli seconds to sleep for
 */
int
dss_sleep(uint64_t sleep_msec)
{
	struct dss_sleep_ult *dsu;

	dsu = dss_sleep_ult_create();
	if (dsu == NULL)
		return -DER_NOMEM;

	dss_ult_sleep(dsu, sleep_msec * 1000000);
	dss_sleep_ult_destroy(dsu);
	return 0;
}

struct dss_rpc_cntr *
dss_rpc_cntr_get(enum dss_rpc_cntr_id id)
{
	struct dss_xstream  *dx = dss_current_xstream();

	D_ASSERT(id < DSS_RC_MAX);
	return &dx->dx_rpc_cntrs[id];
}

/** increase the active and total counters for the RPC type */
void
dss_rpc_cntr_enter(enum dss_rpc_cntr_id id)
{
	struct dss_rpc_cntr *cntr = dss_rpc_cntr_get(id);

	daos_gettime_coarse(&cntr->rc_active_time);
	cntr->rc_active++;
	cntr->rc_total++;

	/* TODO: add interface to calculate average workload and reset stime */
	if (cntr->rc_stime == 0)
		cntr->rc_stime = cntr->rc_active_time;
}

/**
 * Decrease the active counter for the RPC type, also increase error counter
 * if @failed is true.
 */
void
dss_rpc_cntr_exit(enum dss_rpc_cntr_id id, bool error)
{
	struct dss_rpc_cntr *cntr = dss_rpc_cntr_get(id);

	D_ASSERT(cntr->rc_active > 0);
	cntr->rc_active--;
	if (error)
		cntr->rc_errors++;
}

static int
dss_iv_resp_hdlr(crt_context_t *ctx, void *hdlr_arg,
		 void (*real_rpc_hdlr)(void *), void *arg)
{
	struct dss_xstream	*dx = (struct dss_xstream *)arg;
	int			 rc;

	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_IO], real_rpc_hdlr,
			       hdlr_arg, ABT_THREAD_ATTR_NULL, NULL);
	return dss_abterr2der(rc);
}

static int
dss_rpc_hdlr(crt_context_t *ctx, void *hdlr_arg,
	     void (*real_rpc_hdlr)(void *), void *arg)
{
	struct dss_xstream	*dx = (struct dss_xstream *)arg;
	crt_rpc_t		*rpc = (crt_rpc_t *)hdlr_arg;
	unsigned int		 mod_id = opc_get_mod_id(rpc->cr_opc);
	struct dss_module	*module = dss_module_get(mod_id);
	struct sched_req_attr	 attr = { 0 };
	int			 rc = -DER_NOSYS;

	if (DAOS_FAIL_CHECK(DAOS_FAIL_LOST_REQ))
		return 0;
	/*
	 * The mod_id for the RPC originated from CART is 0xfe, and 'module'
	 * will be NULL for this case.
	 */
	if (module != NULL && module->sm_mod_ops != NULL &&
	    module->sm_mod_ops->dms_get_req_attr != NULL)
		rc = module->sm_mod_ops->dms_get_req_attr(rpc, &attr);

	if (rc == 0)
		return sched_req_enqueue(dx, &attr, real_rpc_hdlr, rpc);

	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_IO], real_rpc_hdlr, rpc,
			       ABT_THREAD_ATTR_NULL, NULL);
	return dss_abterr2der(rc);
}

static void
dss_nvme_poll_ult(void *args)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct dss_xstream	*dx = dss_current_xstream();

	D_ASSERT(dx->dx_main_xs);
	while (!dss_xstream_exiting(dx)) {
		bio_nvme_poll(dmi->dmi_nvme_ctxt);
		ABT_thread_yield();
	}
}

/*
 * Wait all other ULTs exited before the srv handler ULT dss_srv_handler()
 * exits, since the per-xstream TLS, comm context, NVMe context, etc. will
 * be destroyed on server handler ULT exiting.
 */
static void
wait_all_exited(struct dss_xstream *dx)
{
	D_DEBUG(DB_TRACE, "XS(%d) draining ULTs.\n", dx->dx_xs_id);

	sched_stop(dx);

	while (1) {
		size_t	total_size = 0;
		int	i;

		for (i = 0; i < DSS_POOL_CNT; i++) {
			size_t	pool_size;
			int	rc;
			rc = ABT_pool_get_total_size(dx->dx_pools[i],
						     &pool_size);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
			total_size += pool_size;
		}
		/*
		 * Current running srv handler ULT is popped, so it's not
		 * counted in pool size by argobots.
		 */
		if (total_size == 0)
			break;

		ABT_thread_yield();
	}
	D_DEBUG(DB_TRACE, "XS(%d) drained ULTs.\n", dx->dx_xs_id);
}

/*
 * The server handler ULT first sets CPU affinity, initialize the per-xstream
 * TLS, CRT(comm) context, NVMe context, creates the long-run ULTs (GC & NVMe
 * poll), then it starts to poll the network requests in a loop until service
 * shutdown.
 */
static void
dss_srv_handler(void *arg)
{
	struct dss_xstream		*dx = (struct dss_xstream *)arg;
	struct dss_thread_local_storage	*dtc;
	struct dss_module_info		*dmi;
	int				 rc;
	bool				 signal_caller = true;

	/**
	 * Set cpu affinity
	 */
	rc = hwloc_set_cpubind(dss_topo, dx->dx_cpuset, HWLOC_CPUBIND_THREAD);
	if (rc) {
		D_ERROR("failed to set cpu affinity: %d\n", errno);
		goto signal;
	}

	/**
	 * Set memory affinity, but fail silently if it does not work since some
	 * systems return ENOSYS.
	 */
	rc = hwloc_set_membind(dss_topo, dx->dx_cpuset, HWLOC_MEMBIND_BIND,
			       HWLOC_MEMBIND_THREAD);
	if (rc)
		D_DEBUG(DB_TRACE, "failed to set memory affinity: %d\n", errno);

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

	(void)pthread_setname_np(pthread_self(), dx->dx_name);

	if (dx->dx_comm) {
		/* create private transport context */
		rc = crt_context_create(&dmi->dmi_ctx);
		if (rc != 0) {
			D_ERROR("failed to create crt ctxt: "DF_RC"\n",
				DP_RC(rc));
			goto tls_fini;
		}

		rc = crt_context_register_rpc_task(dmi->dmi_ctx, dss_rpc_hdlr,
						   dss_iv_resp_hdlr, dx);
		if (rc != 0) {
			D_ERROR("failed to register process cb "DF_RC"\n",
				DP_RC(rc));
			goto crt_destroy;
		}

		/** Get context index from cart */
		rc = crt_context_idx(dmi->dmi_ctx, &dmi->dmi_ctx_id);
		if (rc != 0) {
			D_ERROR("failed to get xtream index: rc "DF_RC"\n",
				DP_RC(rc));
			goto crt_destroy;
		}
		dx->dx_ctx_id = dmi->dmi_ctx_id;
		/** verify CART assigned the ctx_id ascendantly start from 0 */
		if (dx->dx_xs_id < dss_sys_xs_nr) {
			D_ASSERT(dx->dx_ctx_id == dx->dx_xs_id);
		} else {
			if (dx->dx_main_xs) {
				D_ASSERTF(dx->dx_ctx_id ==
					  dx->dx_tgt_id + dss_sys_xs_nr -
					  DRPC_XS_NR,
					  "incorrect ctx_id %d for xs_id %d\n",
					  dx->dx_ctx_id, dx->dx_xs_id);
			} else {
				if (dss_helper_pool)
					D_ASSERTF(dx->dx_ctx_id ==
						  (dx->dx_xs_id - DRPC_XS_NR),
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

		rc = ABT_thread_create(dx->dx_pools[DSS_POOL_NVME_POLL],
				       dss_nvme_poll_ult, NULL,
				       ABT_THREAD_ATTR_NULL, NULL);
		if (rc != ABT_SUCCESS) {
			D_ERROR("create NVMe poll ULT failed: %d\n", rc);
			ABT_future_set(dx->dx_shutdown, dx);
			wait_all_exited(dx);
			D_GOTO(nvme_fini, rc = dss_abterr2der(rc));
		}
	}

	dmi->dmi_xstream = dx;
	ABT_mutex_lock(xstream_data.xd_mutex);
	/* initialized everything for the ULT, notify the creator */
	D_ASSERT(!xstream_data.xd_ult_signal);
	xstream_data.xd_ult_signal = true;
	xstream_data.xd_ult_init_rc = 0;
	ABT_cond_signal(xstream_data.xd_ult_init);

	/* wait until all xstreams are ready, otherwise it is not safe
	 * to run lock-free dss_collective, although this race is not
	 * realistically possible in the DAOS stack.
	 */
	ABT_cond_wait(xstream_data.xd_ult_barrier, xstream_data.xd_mutex);
	ABT_mutex_unlock(xstream_data.xd_mutex);

	signal_caller = false;
	/* main service progress loop */
	for (;;) {
		if (dx->dx_comm) {
			rc = crt_progress(dmi->dmi_ctx, 0 /* no wait */);
			if (rc != 0 && rc != -DER_TIMEDOUT) {
				D_ERROR("failed to progress CART context: %d\n",
					rc);
				/* XXX Sometimes the failure might be just
				 * temporary, Let's keep progressing for now.
				 */
			}
		}

		check_sleep_list();

		if (dss_xstream_exiting(dx))
			break;

		ABT_thread_yield();
	}
	D_ASSERT(d_list_empty(&dx->dx_sleep_ult_list));

	wait_all_exited(dx);
	if (dmi->dmi_dp) {
		daos_profile_destroy(dmi->dmi_dp);
		dmi->dmi_dp = NULL;
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
		/* initialized everything for the ULT, notify the creator */
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
	int			xs_offset = 0;

	/** allocate & init xstream configuration data */
	dx = dss_xstream_alloc(cpus);
	if (dx == NULL)
		return -DER_NOMEM;

	/* Partial XS need the RPC communication ability - system XS, each
	 * main XS and its first offload XS (for IO dispatch).
	 * The 2nd offload XS(if exists) does not need RPC communication
	 * as it is only for EC/checksum/compress offloading.
	 */
	if (dss_helper_pool) {
		comm = (xs_id == 0) || (xs_id >= dss_sys_xs_nr &&
				xs_id < (dss_sys_xs_nr + 2 * dss_tgt_nr));
	} else {
		int	helper_per_tgt;

		helper_per_tgt = dss_tgt_offload_xs_nr / dss_tgt_nr;
		D_ASSERT(helper_per_tgt == 0 || helper_per_tgt == 1 ||
			 helper_per_tgt == 2);
		xs_offset = xs_id < dss_sys_xs_nr ? -1 :
				(((xs_id) - dss_sys_xs_nr) %
				 (helper_per_tgt + 1));
		comm = (xs_id == 0) || xs_offset == 0 || xs_offset == 1;
	}
	dx->dx_xs_id	= xs_id;
	dx->dx_ctx_id	= -1;
	dx->dx_comm	= comm;
	if (dss_helper_pool) {
		dx->dx_main_xs	= xs_id >= dss_sys_xs_nr &&
				  xs_id < (dss_sys_xs_nr + dss_tgt_nr);
	} else {
		dx->dx_main_xs	= xs_id >= dss_sys_xs_nr && xs_offset == 0;
	}
	dx->dx_dsc_started = false;
	D_INIT_LIST_HEAD(&dx->dx_sleep_ult_list);

	/**
	 * Generate name for each xstreams so that they can be easily identified
	 * and monitored independently (e.g. via ps(1))
	 */
	dx->dx_tgt_id	= dss_xs2tgt(xs_id);
	if (xs_id < dss_sys_xs_nr) {
		/** system xtreams are named daos_sys_$num */
		snprintf(dx->dx_name, DSS_XS_NAME_LEN, DSS_SYS_XS_NAME_FMT,
			 xs_id);
	} else if (dx->dx_main_xs) {
		/** primary I/O xstreams are named daos_io_$tgtid */
		snprintf(dx->dx_name, DSS_XS_NAME_LEN, DSS_IO_XS_NAME_FMT,
			 dx->dx_tgt_id);
	} else {
		/** offload xstreams are named daos_off_$num */
		snprintf(dx->dx_name, DSS_XS_NAME_LEN, DSS_OFFLOAD_XS_NAME_FMT,
			 xs_id);
	}

	/** create ABT scheduler in charge of this xstream */
	rc = dss_sched_init(dx);
	if (rc != 0) {
		D_ERROR("create scheduler fails: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_dx, rc);
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
	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_NET_POLL],
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
out_sched:
	dss_sched_fini(dx);
out_dx:
	dss_xstream_free(dx);
	return rc;
}

static void
dss_xstreams_fini(bool force)
{
	struct dss_xstream	*dx;
	int			 i;
	int			 rc;
	bool			 started = false;

	D_DEBUG(DB_TRACE, "Stopping execution streams\n");
	dss_xstreams_open_barrier();
	rc = bio_nvme_ctl(BIO_CTL_NOTIFY_STARTED, &started);
	D_ASSERT(rc == 0);

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
		dss_sched_fini(dx);
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

bool
dss_xstream_is_busy(void)
{
	struct dss_rpc_cntr	*cntr = dss_rpc_cntr_get(DSS_RC_OBJ);
	uint64_t		 cur_sec = 0;

	daos_gettime_coarse(&cur_sec);
	/* No IO requests for more than 5 seconds */
	return cur_sec < (cntr->rc_active_time + 5);
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
			D_ERROR("No core available for XS: %d", xs_id);
			return -DER_INVAL;
		}
		D_DEBUG(DB_TRACE,
			"Choosing next available core index %d.", idx);
		/* the 2nd system XS (drpc XS) will reuse the first XS' core */
		if (xs_id != 0)
			hwloc_bitmap_clr(core_allocation_bitmap, idx);

		obj = hwloc_get_obj_by_depth(dss_topo, dss_core_depth, idx);
		if (obj == NULL) {
			D_PRINT("Null core returned by hwloc\n");
			return -DER_INVAL;
		}

		hwloc_bitmap_asprintf(&cpuset, obj->cpuset);
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

	rc = dss_start_one_xstream(obj->cpuset, xs_id);
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
	if (dss_helper_pool) {
		for (i = 0; i < dss_tgt_offload_xs_nr; i++) {
			xs_id = dss_sys_xs_nr + dss_tgt_nr + i;
			rc = dss_start_xs_id(xs_id);
			if (rc)
				D_GOTO(out, rc);
		}
	} else {
		D_ASSERTF(dss_tgt_offload_xs_nr % dss_tgt_nr == 0,
			  "bad dss_tgt_offload_xs_nr %d, dss_tgt_nr %d\n",
			  dss_tgt_offload_xs_nr, dss_tgt_nr);
		for (i = 0; i < dss_tgt_nr; i++) {
			int j;

			for (j = 0; j < dss_tgt_offload_xs_nr / dss_tgt_nr;
			     j++) {
				xs_id = DSS_MAIN_XS_ID(i) + j + 1;
				rc = dss_start_xs_id(xs_id);
				if (rc)
					D_GOTO(out, rc);
			}
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
 * Generic offload call - abstraction for acceleration with
 *
 * \param[in] at_args	acceleration tasks with both ULT and FPGA
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
		rc = dss_ult_execute(compute_checksum_ult,
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
	case DMG_KEY_FAIL_LOC:
		daos_fail_loc_set(value);
		break;
	case DMG_KEY_FAIL_VALUE:
		daos_fail_value_set(value);
		break;
	case DMG_KEY_FAIL_NUM:
		daos_fail_num_set(value);
	case DMG_KEY_REBUILD_THROTTLING:
		if (value >= 100) {
			D_ERROR("invalid value "DF_U64"\n", value);
			rc = -DER_INVAL;
			break;
		}
		D_WARN("set rebuild percentage to "DF_U64"\n", value);
		rc = sched_set_throttle(SCHED_REQ_MIGRATE, value);
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
	bool	started = true;

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

	rc = bio_nvme_ctl(BIO_CTL_NOTIFY_STARTED, &started);
	D_ASSERT(rc == 0);

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
