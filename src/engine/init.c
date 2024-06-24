/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * This file is part of the DAOS server. It implements the startup/shutdown
 * routines for the daos_server.
 */

#define D_LOGFAC	DD_FAC(server)

#include <signal.h>
#include <abt.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <execinfo.h>

#include <daos/btree_class.h>
#include <daos/common.h>
#include <daos/placement.h>
#include <daos/tls.h>
#include <daos/ult_stack_mmap.h>
#include "srv_internal.h"
#include "drpc_internal.h"
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

#define MAX_MODULE_OPTIONS	64
#if BUILD_PIPELINE
#define MODULE_LIST	"vos,rdb,rsvc,security,mgmt,dtx,pool,cont,obj,rebuild,pipeline"
#else
#define MODULE_LIST	"vos,rdb,rsvc,security,mgmt,dtx,pool,cont,obj,rebuild"
#endif
#define MODS_LIST_CHK	"vos,rdb,rsvc,security,mgmt,dtx,pool,cont,obj,rebuild,chk"

/** List of modules to load */
static char		modules[MAX_MODULE_OPTIONS + 1];

/**
 * Number of target threads the user would like to start.
 */
static unsigned int	nr_threads;

/** DAOS system name (corresponds to crt group ID) */
char                   *daos_sysname = DAOS_DEFAULT_SYS_NAME;

/** Storage node hostname */
char		        dss_hostname[DSS_HOSTNAME_MAX_LEN];

/** Storage path (hack) */
const char	       *dss_storage_path = "/mnt/daos";

/** NVMe config file */
const char	       *dss_nvme_conf;

/** Socket Directory */
const char	       *dss_socket_dir = "/var/run/daos_server";

/** NVMe mem_size for SPDK memory allocation */
unsigned int		dss_nvme_mem_size = DAOS_NVME_MEM_PRIMARY;

/** NVMe hugepage_size for DPDK/SPDK memory allocation */
unsigned int		dss_nvme_hugepage_size;

/** I/O Engine instance index */
unsigned int		dss_instance_idx;

/** HW topology */
hwloc_topology_t	dss_topo;
/** core depth of the topology */
int			dss_core_depth;
/** number of physical cores, w/o hyperthreading */
int			dss_core_nr;
/** start offset index of the first core for service XS.  Init to -1 so we can
 * detect when it is explicitly set and disable multi-socket mode.
 */
unsigned int            dss_core_offset = -1;
/** NUMA node to bind to */
int			dss_numa_node = -1;
/** Forward I/O work to neighbor */
bool                    dss_forward_neighbor;
/** Cached numa information */
struct dss_numa_info   *dss_numa;
/** Number of active numa nodes, multi-socket mode only */
int                     dss_numa_nr = 1;
/** Module facility bitmask */
static uint64_t		dss_mod_facs;
/** Number of storage tiers: 2 for SCM and NVMe */
unsigned int		dss_storage_tiers = 2;

/** Flag to indicate Arbogots is initialized */
static bool dss_abt_init;

/** Start daos_engine under check mode. */
static bool dss_check_mode;

/* stream used to dump ABT infos and ULTs stacks */
static FILE *abt_infos;

bool
engine_in_check(void)
{
	return dss_check_mode;
}

d_rank_t
dss_self_rank(void)
{
	d_rank_t	rank;
	int		rc;

	rc = crt_group_rank(NULL /* grp */, &rank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	return rank;
}

struct dss_module_info *
get_module_info(void)
{
	return dss_get_module_info();
}

/* See the comment near where this function is called. */
static uint64_t
hlc_recovery_begin(void)
{
	return d_hlc_epsilon_get_bound(d_hlc_get());
}

/* See the comment near where this function is called. */
static void
hlc_recovery_end(uint64_t bound)
{
	int64_t	diff;

	diff = bound - d_hlc_get();
	if (diff > 0) {
		struct timespec	tv;

		tv.tv_sec = d_hlc2nsec(diff) / NSEC_PER_SEC;
		tv.tv_nsec = d_hlc2nsec(diff) % NSEC_PER_SEC;

		/* XXX: If the server restart so quickly as to all related
		 *	things are handled within HLC epsilon, then it is
		 *	possible that current local HLC after restart may
		 *	be older than some HLC that was generated before
		 *	server restart because of the clock drift between
		 *	servers. So here, we control the server (re)start
		 *	process to guarantee that the restart time window
		 *	will be longer than the HLC epsilon, then new HLC
		 *	generated after server restart will not rollback.
		 */
		D_INFO("nanosleep %lu:%lu before open external service.\n",
		       tv.tv_sec, tv.tv_nsec);
		nanosleep(&tv, NULL);
	}
}

/*
 * Register the dbtree classes used by native server-side modules (e.g.,
 * ds_pool, ds_cont, etc.). Unregistering is currently not supported.
 */
static int
register_dbtree_classes(void)
{
	int rc;

	rc = dbtree_class_register(DBTREE_CLASS_KV, 0 /* feats */,
				   &dbtree_kv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_KV: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_IV: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_ifv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_IFV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_NV, BTR_FEAT_DIRECT_KEY,
				   &dbtree_nv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_NV: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_UV, 0 /* feats */,
				   &dbtree_uv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_UV: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_EC,
				   BTR_FEAT_UINT_KEY /* feats */,
				   &dbtree_ec_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_EC: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	return rc;
}

static int
modules_load(void)
{
	char		*mod;
	char		*sep;
	char		*run;
	int		 rc = 0;

	D_STRNDUP(sep, modules, MAX_MODULE_OPTIONS + 1);
	if (sep == NULL)
		return -DER_NOMEM;
	run = sep;

	mod = strsep(&run, ",");
	while (mod != NULL) {
		if (strcmp(mod, "object") == 0)
			mod = "obj";
		else if (strcmp(mod, "po") == 0)
			mod = "pool";
		else if (strcmp(mod, "container") == 0 ||
			 strcmp(mod, "co") == 0)
			mod = "cont";
		else if (strcmp(mod, "management") == 0)
			mod = "mgmt";
		else if (strcmp(mod, "vos") == 0)
			mod = "vos_srv";

		rc = dss_module_load(mod);
		if (rc != 0) {
			D_ERROR("Failed to load module %s: %d\n", mod, rc);
			break;
		}

		mod = strsep(&run, ",");
	}

	D_FREE(sep);
	return rc;
}

static unsigned int
ncores_needed(unsigned int tgt_nr, unsigned int nr_helpers)
{
	return DAOS_TGT0_OFFSET + tgt_nr + nr_helpers;
}

/**
 * Check if the #targets and #nr_xs_helpers is valid to start server, the #nr_xs_helpers possibly
 * be reduced.
 */
static int
dss_tgt_nr_check(unsigned int ncores, unsigned int tgt_nr, bool oversubscribe)
{
	D_ASSERT(ncores >= 1);

	/* at most 2 helper XS per target */
	if (dss_tgt_offload_xs_nr > 2 * tgt_nr) {
		D_PRINT("#nr_xs_helpers(%d) cannot exceed 2 times #targets (2 x %d = %d).\n",
			dss_tgt_offload_xs_nr, tgt_nr, 2 * tgt_nr);
		dss_tgt_offload_xs_nr = 2 * tgt_nr;
	} else if (dss_tgt_offload_xs_nr == 0) {
		D_WARN("Suggest to config at least 1 helper XS per DAOS engine\n");
	}

	if (oversubscribe) {
		if (ncores_needed(tgt_nr, dss_tgt_offload_xs_nr) > ncores)
			D_PRINT("Force to start engine with %d targets %d xs_helpers on %d cores("
				"%d cores reserved for system service).\n",
				tgt_nr, dss_tgt_offload_xs_nr, ncores, DAOS_TGT0_OFFSET);
		goto out;
	}

	if (ncores_needed(tgt_nr, dss_tgt_offload_xs_nr) > ncores) {
		D_ERROR("cannot start engine with %d targets %d xs_helpers on %d cores, may try "
			"with DAOS_TARGET_OVERSUBSCRIBE=1 or reduce #targets/#nr_xs_helpers("
			"%d cores reserved for system service).\n",
			tgt_nr, dss_tgt_offload_xs_nr, ncores, DAOS_TGT0_OFFSET);
		return -DER_INVAL;
	}

out:
	if (dss_tgt_offload_xs_nr % tgt_nr != 0)
		dss_helper_pool = true;

	return 0;
}

static bool
dss_multi_socket_check(bool oversub, int numa_nr)
{
	/** Keep this simple and disallow some configurations */
	if (oversub) {
		D_INFO("Oversubscription requested, bypassing multi-socket mode\n");
		return false;
	}

	if (dss_numa_node != -1) {
		D_INFO("Numa node specified, running in single socket mode\n");
		return false;
	}

	if (numa_nr < 2) {
		D_INFO("No NUMA found, bypassing multi-socket mode\n");
		return false;
	}

	if ((dss_tgt_offload_xs_nr % numa_nr) != 0) {
		D_INFO("Uneven split of helpers on sockets, bypassing multi-socket mode\n");
		return false;
	}

	if ((dss_tgt_nr % numa_nr) != 0) {
		D_INFO("Uneven split of targets on sockets, bypassing multi-socket mode\n");
		return false;
	}

	return true;
}

static int
dss_legacy_mode(bool oversub)
{
	D_PRINT("Using legacy core allocation algorithm\n");
	if (dss_core_offset >= dss_core_nr) {
		D_ERROR("invalid dss_core_offset %u (set by \"-f\" option), should within "
			"range [0, %u]\n",
			dss_core_offset, dss_core_nr - 1);
		return -DER_INVAL;
	}

	return dss_tgt_nr_check(dss_core_nr, dss_tgt_nr, oversub);
}

static int
dss_topo_init(void)
{
	int		depth;
	int		numa_node_nr;
	int             num_cores_visited;
	int		k;
	int             numa_node;
	int             rc = 0;
	hwloc_obj_t     numa_obj;
	hwloc_obj_t	corenode;
	bool            tgt_oversub = false;
	bool            multi_socket = false;

	hwloc_topology_init(&dss_topo);
	hwloc_topology_load(dss_topo);

	dss_core_depth = hwloc_get_type_depth(dss_topo, HWLOC_OBJ_CORE);
	dss_core_nr = hwloc_get_nbobjs_by_type(dss_topo, HWLOC_OBJ_CORE);
	depth = hwloc_get_type_depth(dss_topo, HWLOC_OBJ_NUMANODE);
	numa_node_nr = hwloc_get_nbobjs_by_depth(dss_topo, depth);
	d_getenv_bool("DAOS_TARGET_OVERSUBSCRIBE", &tgt_oversub);
	d_getenv_bool("DAOS_FORWARD_NEIGHBOR", &dss_forward_neighbor);
	dss_tgt_nr = nr_threads;

	/** Set to -1 initially so we can detect when it's set explicitly to
	 * maintain mode consistency between engines where one sets it to 0.
	 */
	if (dss_core_offset == -1) {
		dss_core_offset = 0;
		if (dss_multi_socket_check(tgt_oversub, numa_node_nr))
			multi_socket = true;
	} else {
		D_INFO("Core offset specified, running in single socket mode\n");
	}

	/* Fall back to legacy mode if no socket was specified and
	 * multi-socket mode is not possible or NUMA data is unavailable
	 */
	if ((!multi_socket && dss_numa_node == -1) || numa_node_nr <= 0)
		return dss_legacy_mode(tgt_oversub);

	if (dss_numa_node > numa_node_nr) {
		D_ERROR("Invalid NUMA node selected. Must be no larger than %d\n", numa_node_nr);
		return -DER_INVAL;
	}

	D_ALLOC_ARRAY(dss_numa, numa_node_nr);
	if (dss_numa == NULL)
		return -DER_NOMEM;

	for (numa_node = 0; numa_node < numa_node_nr; numa_node++) {
		dss_numa[numa_node].ni_idx = numa_node;
		numa_obj                   = hwloc_get_obj_by_depth(dss_topo, depth, numa_node);
		if (numa_obj == NULL) {
			D_ERROR("NUMA node %d was not found in the topology\n", numa_node);
			D_GOTO(failed, rc = -DER_INVAL);
		}

		/* create an empty bitmap, then set each bit as we */
		/* find a core that matches */
		dss_numa[numa_node].ni_coremap = hwloc_bitmap_alloc();
		if (dss_numa[numa_node].ni_coremap == NULL) {
			D_ERROR("Unable to allocate core allocation bitmap\n");
			D_GOTO(failed, rc = -DER_INVAL);
		}

		dss_numa[numa_node].ni_core_nr = 0;
		num_cores_visited              = 0;

		for (k = 0; k < dss_core_nr; k++) {
			corenode = hwloc_get_obj_by_depth(dss_topo, dss_core_depth, k);
			if (corenode == NULL)
				continue;
			if (hwloc_bitmap_isincluded(corenode->cpuset, numa_obj->cpuset) != 0) {
				if (num_cores_visited++ >= dss_core_offset)
					hwloc_bitmap_set(dss_numa[numa_node].ni_coremap, k);
				dss_numa[numa_node].ni_core_nr++;
			}
		}
		if (multi_socket && numa_node > 0 &&
		    dss_numa[numa_node].ni_core_nr != dss_numa[numa_node - 1].ni_core_nr) {
			D_INFO("Non-uniform numa nodes, bypassing multi-socket mode\n");
			D_FREE(dss_numa);
			return dss_legacy_mode(false);
		}
	}

	if (multi_socket) {
		/** In this mode, we simply save the topology for later use but
		 * still use all of the cores.
		 */
		D_PRINT("Using Multi-socket NUMA core allocation algorithm\n");
		dss_numa_nr             = numa_node_nr;
		dss_offload_per_numa_nr = dss_tgt_offload_xs_nr / dss_numa_nr;
		dss_tgt_per_numa_nr     = dss_tgt_nr / dss_numa_nr;
		return dss_tgt_nr_check(dss_core_nr, dss_tgt_nr, tgt_oversub);
	}

	if (dss_core_offset >= dss_numa[dss_numa_node].ni_core_nr) {
		D_ERROR("invalid dss_core_offset %d (set by \"-f\" option), should within range "
			"[0, %d]\n",
			dss_core_offset, dss_numa[dss_numa_node].ni_core_nr - 1);
		return -DER_INVAL;
	}
	D_PRINT("Using NUMA core allocation algorithm\n");

	return dss_tgt_nr_check(dss_numa[dss_numa_node].ni_core_nr, dss_tgt_nr, tgt_oversub);
failed:
	D_FREE(dss_numa);
	return rc;
}

static void
dss_topo_fini(void)
{
	D_FREE(dss_numa);
}

static ABT_mutex		server_init_state_mutex;
static ABT_cond			server_init_state_cv;
static enum dss_init_state	server_init_state;

static int
server_init_state_init(void)
{
	int rc;

	rc = ABT_mutex_create(&server_init_state_mutex);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = ABT_cond_create(&server_init_state_cv);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&server_init_state_mutex);
		return dss_abterr2der(rc);
	}
	return 0;
}

static void
server_init_state_fini(void)
{
	server_init_state = DSS_INIT_STATE_INIT;
	ABT_cond_free(&server_init_state_cv);
	ABT_mutex_free(&server_init_state_mutex);
}

static void
server_init_state_wait(enum dss_init_state state)
{
	D_INFO("waiting for server init state %d\n", state);
	ABT_mutex_lock(server_init_state_mutex);
	while (server_init_state != state)
		ABT_cond_wait(server_init_state_cv, server_init_state_mutex);
	ABT_mutex_unlock(server_init_state_mutex);
}

void
dss_init_state_set(enum dss_init_state state)
{
	D_INFO("setting server init state to %d\n", state);
	ABT_mutex_lock(server_init_state_mutex);
	server_init_state = state;
	ABT_cond_broadcast(server_init_state_cv);
	ABT_mutex_unlock(server_init_state_mutex);
}

static int
abt_max_num_xstreams(void)
{
	unsigned num_xstreams = 0;

	if (d_isenv_def("ABT_MAX_NUM_XSTREAMS"))
		d_getenv_uint("ABT_MAX_NUM_XSTREAMS", &num_xstreams);
	else
		d_getenv_uint("ABT_ENV_MAX_NUM_XSTREAMS", &num_xstreams);
	D_ASSERT(num_xstreams <= INT_MAX);

	return num_xstreams;
}

static int
set_abt_max_num_xstreams(int n)
{
	char   *name = "ABT_MAX_NUM_XSTREAMS";
	char   *value;
	int	rc;

	D_ASSERTF(n > 0, "%d\n", n);
	D_ASPRINTF(value, "%d", n);
	if (value == NULL)
		return -DER_NOMEM;
	D_INFO("Setting %s to %s\n", name, value);
	rc = d_setenv(name, value, 1 /* overwrite */);
	D_FREE(value);
	if (rc != 0)
		return daos_errno2der(errno);
	return 0;
}

static int
abt_init(int argc, char *argv[])
{
	int	nrequested = abt_max_num_xstreams();
	int	nrequired = 1 /* primary xstream */ + DSS_XS_NR_TOTAL;
	int	rc;

	/*
	 * Set ABT_MAX_NUM_XSTREAMS to the larger of nrequested and nrequired.
	 * If we don't do this, Argobots may use a default or requested value
	 * less than nrequired. We may then hit Argobots assertion failures
	 * because xstream_data.xd_mutex's internal queue has fewer slots than
	 * some xstreams' rank numbers need.
	 */
	rc = set_abt_max_num_xstreams(max(nrequested, nrequired));
	if (rc != 0)
		return daos_errno2der(errno);

	/* Now, initialize Argobots. */
	rc = da_initialize(argc, argv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to init ABT: %d\n", rc);
		return dss_abterr2der(rc);
	}

	dss_abt_init = true;

	return 0;
}

static void
abt_fini(void)
{
	dss_abt_init = false;

	da_finalize();
}

static void
dss_crt_event_cb(d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
		 enum crt_event_type type, void *arg)
{
	int			 rc = 0;
	struct engine_metrics	*metrics = &dss_engine_metrics;

	/* We only care about dead ranks for now */
	if (type != CRT_EVT_DEAD) {
		D_DEBUG(DB_MGMT, "ignore: src=%d type=%d\n", src, type);
		return;
	}

	d_tm_record_timestamp(metrics->last_event_time);

	if (src == CRT_EVS_SWIM) {
		d_tm_inc_counter(metrics->dead_rank_events, 1);
		rc = ds_notify_swim_rank_dead(rank, incarnation);
		if (rc)
			D_ERROR("failed to handle %u/%u event: "DF_RC"\n",
				src, type, DP_RC(rc));
	} else if (src == CRT_EVS_GRPMOD) {
		d_rank_t self_rank = dss_self_rank();

		if (rank == dss_self_rank()) {
			D_WARN("raising SIGKILL: exclusion of this engine (rank %u) detected\n",
			       self_rank);
			/*
			 * For now, we just raise a SIGKILL to ourselves; we could
			 * inform daos_server, who would initiate a termination and
			 * decide whether to restart us.
			 */
			rc = kill(getpid(), SIGKILL);
			if (rc != 0)
				D_ERROR("failed to raise SIGKILL: %d\n", errno);
			return;
		}

	}
}

static void
dss_crt_hlc_error_cb(void *arg)
{
	/* Rank will be populated automatically */
	ds_notify_ras_eventf(RAS_ENGINE_CLOCK_DRIFT, RAS_TYPE_INFO,
			     RAS_SEV_ERROR, NULL /* hwid */,
			     NULL /* rank */, NULL /* inc */,
			     NULL /* jobid */, NULL /* pool */,
			     NULL /* cont */, NULL /* objid */,
			     NULL /* ctlop */, NULL /* data */,
			     "clock drift detected");
}

static void
server_id_cb(uint32_t *tid, uint64_t *uid)
{

	if (server_init_state != DSS_INIT_STATE_SET_UP)
		return;

	if (uid != NULL && dss_abt_init) {
		ABT_unit_type type = ABT_UNIT_TYPE_EXT;
		int rc;

		rc = ABT_self_get_type(&type);

		if (rc == 0 && (type == ABT_UNIT_TYPE_THREAD || type == ABT_UNIT_TYPE_TASK))
			ABT_self_get_thread_id(uid);
	}

	if (tid != NULL) {
		struct daos_thread_local_storage *dtc;
		struct daos_module_info          *dmi;
		int index = daos_srv_modkey.dmk_index;

		/* Avoid assertion in daos_module_key_get() */
		dtc = dss_tls_get();
		if (dtc != NULL && index >= 0 && index < DAOS_MODULE_KEYS_NR &&
		    daos_get_module_key(index) == &daos_srv_modkey) {
			dmi = dss_get_module_info();
			if (dmi != NULL)
				*tid = dmi->dmi_xs_id;
		}
	}
}

static uint64_t
metrics_region_size(int num_tgts)
{
	const uint64_t	est_std_metrics = 1024; /* high estimate to allow for pool links */
	const uint64_t	est_tgt_metrics = 128; /* high estimate */

	return (est_std_metrics + est_tgt_metrics * num_tgts) * D_TM_METRIC_SIZE;
}

static int
server_init(int argc, char *argv[])
{
	uint64_t		bound;
	unsigned int		ctx_nr;
	int			rc;
	struct engine_metrics	*metrics;

	/*
	 * Begin the HLC recovery as early as possible. Do not read the HLC
	 * before the hlc_recovery_end call below.
	 */
	bound = hlc_recovery_begin();

	gethostname(dss_hostname, DSS_HOSTNAME_MAX_LEN);

	daos_debug_set_id_cb(server_id_cb);
	rc = daos_debug_init_ex(DAOS_LOG_DEFAULT, DLOG_INFO);
	if (rc != 0)
		return rc;

	/** initialize server topology data - this is needed to set up the number of targets */
	rc = dss_topo_init();
	if (rc != 0)
		D_GOTO(exit_debug_init, rc);

	rc = d_tm_init(dss_instance_idx, metrics_region_size(dss_tgt_nr), D_TM_SERVER_PROCESS);
	if (rc != 0)
		goto exit_debug_init;

	rc = dss_engine_metrics_init();
	if (rc != 0)
		D_WARN("Unable to initialize engine metrics, " DF_RC "\n",
		       DP_RC(rc));

	metrics = &dss_engine_metrics;
	/** Report timestamp when engine was started */
	d_tm_record_timestamp(metrics->started_time);

	rc = drpc_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize dRPC: "DF_RC"\n", DP_RC(rc));
		goto exit_metrics_init;
	}

	rc = register_dbtree_classes();
	if (rc != 0)
		D_GOTO(exit_drpc_fini, rc);

	rc = abt_init(argc, argv);
	if (rc != 0)
		goto exit_drpc_fini;

	/* initialize the modular interface */
	rc = dss_module_init();
	if (rc)
		goto exit_abt_init;
	D_INFO("Module interface successfully initialized\n");

	/* initialize the network layer */
	ctx_nr = dss_ctx_nr_get();
	rc = crt_init_opt(daos_sysname,
			  CRT_FLAG_BIT_SERVER,
			  daos_crt_init_opt_get(true, ctx_nr));
	if (rc)
		D_GOTO(exit_mod_init, rc);
	D_INFO("Network successfully initialized\n");

	rc = daos_hhash_init();
	if (rc != 0) {
		D_ERROR("daos_hhash_init failed, rc: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(exit_crt, rc);
	}

	rc = pl_init();
	if (rc != 0) {
		daos_hhash_fini();
		goto exit_crt;
	}
	D_INFO("handle hash table and placement initialized\n");

	/* server-side uses D_HTYPE_PTR handle */
	d_hhash_set_ptrtype(daos_ht.dht_hhash);

	ds_iv_init();

	/* load modules. Split load and init so first call to dlopen()
	 * is from the engine to avoid DAOS-4557
	 */
	rc = modules_load();
	if (rc)
		/* Some modules may have been loaded successfully. */
		D_GOTO(exit_mod_loaded, rc);
	D_INFO("Module %s successfully loaded\n", modules);

	/*
	 * End the HLC recovery so that module init callbacks (e.g.,
	 * vos_mod_init) invoked by the dss_module_init_all call below can read
	 * the HLC.
	 */
	hlc_recovery_end(bound);
	dss_set_start_epoch();

	/* init nvme */
	rc = bio_nvme_init(dss_nvme_conf, dss_numa_node, dss_nvme_mem_size,
			   dss_nvme_hugepage_size, dss_tgt_nr, dss_nvme_bypass_health_check);
	if (rc)
		D_GOTO(exit_mod_loaded, rc);

	/* init modules */
	rc = dss_module_init_all(&dss_mod_facs);
	if (rc)
		/* Some modules may have been loaded successfully. */
		D_GOTO(exit_nvme_init, rc);
	D_INFO("Module %s successfully initialized\n", modules);

	/* initialize service */
	rc = dss_srv_init();
	if (rc)
		D_GOTO(exit_mod_loaded, rc);
	D_INFO("Service initialized\n");

	rc = server_init_state_init();
	if (rc != 0) {
		D_ERROR("failed to init server init state: "DF_RC"\n",
			DP_RC(rc));
		goto exit_srv_init;
	}

	rc = drpc_notify_ready(dss_check_mode);
	if (rc != 0) {
		D_ERROR("Failed to notify daos_server: "DF_RC"\n", DP_RC(rc));
		goto exit_init_state;
	}

	server_init_state_wait(DSS_INIT_STATE_SET_UP);

	if (!dss_check_mode) {
		rc = crt_register_event_cb(dss_crt_event_cb, NULL);
		if (rc != 0)
			D_GOTO(exit_init_state, rc);
	}

	rc = crt_register_hlc_error_cb(dss_crt_hlc_error_cb, NULL);
	if (rc)
		D_GOTO(exit_init_state, rc);

	dss_xstreams_open_barrier();
	D_INFO("Service fully up\n");

	/** Report timestamp when engine was open for business */
	d_tm_record_timestamp(metrics->ready_time);

	/** Report rank */
	d_tm_set_gauge(metrics->rank_id, dss_self_rank());

	D_PRINT("DAOS I/O Engine (v%s) process %u started on rank %u "
		"with %u target, %d helper XS, firstcore %d, host %s.\n",
		DAOS_VERSION, getpid(), dss_self_rank(), dss_tgt_nr,
		dss_tgt_offload_xs_nr, dss_core_offset, dss_hostname);

	if (dss_numa && dss_numa_node != -1)
		D_PRINT("Using NUMA node: %d", dss_numa_node);

	return 0;

exit_init_state:
	server_init_state_fini();
exit_srv_init:
	dss_srv_fini(true);
exit_nvme_init:
	bio_nvme_fini();
exit_mod_loaded:
	ds_iv_fini();
	dss_module_unload_all();
	pl_fini();
	daos_hhash_fini();
exit_crt:
	crt_finalize();
exit_mod_init:
	dss_module_fini(true);
exit_abt_init:
	abt_fini();
exit_drpc_fini:
	drpc_fini();
exit_metrics_init:
	dss_engine_metrics_fini();
	d_tm_fini();
	/* dss_topo_fini cleans itself if it fails */
exit_debug_init:
	daos_debug_fini();
	return rc;
}

static void
server_fini(bool force)
{
	D_INFO("Service is shutting down\n");
	/*
	 * The first thing to do is to inform every xstream that the engine is
	 * shutting down, so that we can avoid allocating new resources or
	 * taking new references on existing ones if necessary. Note that
	 * xstreams won't start shutting down until we call dss_srv_fini below.
	 */
	dss_srv_set_shutting_down();
	if (!dss_check_mode)
		crt_unregister_event_cb(dss_crt_event_cb, NULL);
	D_INFO("unregister event callbacks done\n");
	/*
	 * Cleaning up modules needs to create ULTs on other xstreams; must be
	 * called before shutting down the xstreams.
	 */
	dss_module_cleanup_all();
	D_INFO("dss_module_cleanup_all() done\n");
	server_init_state_fini();
	D_INFO("server_init_state_fini() done\n");
	/*
	 * All other xstreams start shutting down here. ULT/tasklet creations
	 * on them are no longer possible.
	 */
	dss_srv_fini(force);
	D_INFO("dss_srv_fini() done\n");
	bio_nvme_fini();
	D_INFO("bio_nvme_fini() done\n");
	ds_iv_fini();
	D_INFO("ds_iv_fini() done\n");
	dss_module_unload_all();
	D_INFO("dss_module_unload_all() done\n");
	/*
	 * Client stuff finalization needs be done after all ULTs drained
	 * in dss_srv_fini().
	 */
	pl_fini();
	daos_hhash_fini();
	D_INFO("daos_fini() or pl_fini() done\n");
	crt_finalize();
	D_INFO("crt_finalize() done\n");
	dss_module_fini(force);
	D_INFO("dss_module_fini() done\n");
	abt_fini();
	D_INFO("abt_fini() done\n");
	drpc_fini();
	D_INFO("drpc_fini() done\n");
	dss_engine_metrics_fini();
	D_INFO("dss_engine_metrics_fini() done\n");
	d_tm_fini();
	D_INFO("d_tm_fini() done\n");
	dss_topo_fini();
	D_INFO("dss_top_fini() done\n");
	daos_debug_fini();
	D_INFO("daos_debug_fini() done\n");
}

static void
usage(char *prog, FILE *out)
{
	fprintf(out, "\
Usage:\n\
  %s -h\n\
  %s [-m modules] [-c ncores] [-g group] [-s path]\n\
Options:\n\
  --modules=modules, -m modules\n\
      List of server modules to load (default \"%s\")\n\
  --cores=ncores, -c ncores\n\
      Number of targets to use (deprecated, please use -t instead)\n\
  --targets=ntgts, -t ntargets\n\
      Number of targets to use (use all cores by default)\n\
  --xshelpernr=nhelpers, -x helpers\n\
      Number of helper XS -per vos target (default 1)\n\
  --firstcore=firstcore, -f firstcore\n\
      index of first core for service thread (default 0)\n\
  --group=group, -g group\n\
      Server group name (default \"%s\")\n\
  --storage=path, -s path\n\
      Storage path (default \"%s\")\n\
  --socket_dir=socket_dir, -d socket_dir\n\
      Directory where daos_server sockets are located (default \"%s\")\n\
  --nvme=config, -n config\n\
      NVMe config file (default \"%s\")\n\
  --instance_idx=idx, -I idx\n\
      Identifier for this server instance (default %u)\n\
  --pinned_numa_node=numanode, -p numanode\n\
      Bind to cores within the specified NUMA node\n\
  --bypass_health_chk, -b\n\
      Boolean set to inhibit collection of NVME health data\n\
  --mem_size=mem_size, -r mem_size\n\
      Allocates mem_size MB for SPDK when using primary process mode\n\
  --hugepage_size=hugepage_size, -H hugepage_size\n\
      Passes the configured hugepage size(2MB or 1GB)\n\
  --storage_tiers=ntiers, -T ntiers\n\
      Number of storage tiers\n\
  --check, -C\n\
      Start engine with check mode, global consistency check\n\
  --help, -h\n\
      Print this description\n",
		prog, prog, modules, daos_sysname, dss_storage_path,
		dss_socket_dir, dss_nvme_conf, dss_instance_idx);
}

static int arg_strtoul(const char *str, unsigned int *value, const char *opt)
{
	char *ptr_parse_end = NULL;

	*value = strtoul(str, &ptr_parse_end, 0);
	if (ptr_parse_end && *ptr_parse_end != '\0') {
		printf("invalid numeric value: %s (set by %s)\n", str, opt);
		return -DER_INVAL;
	}

	return 0;
}

static int
parse(int argc, char **argv)
{
	struct	option opts[] = {
		{ "cores",		required_argument,	NULL,	'c' },
		{ "socket_dir",		required_argument,	NULL,	'd' },
		{ "firstcore",		required_argument,	NULL,	'f' },
		{ "group",		required_argument,	NULL,	'g' },
		{ "help",		no_argument,		NULL,	'h' },
		{ "modules",		required_argument,	NULL,	'm' },
		{ "nvme",		required_argument,	NULL,	'n' },
		{ "pinned_numa_node",	required_argument,	NULL,	'p' },
		{ "mem_size",		required_argument,	NULL,	'r' },
		{ "hugepage_size",	required_argument,	NULL,	'H' },
		{ "targets",		required_argument,	NULL,	't' },
		{ "storage",		required_argument,	NULL,	's' },
		{ "xshelpernr",		required_argument,	NULL,	'x' },
		{ "instance_idx",	required_argument,	NULL,	'I' },
		{ "bypass_health_chk",	no_argument,		NULL,	'b' },
		{ "storage_tiers",	required_argument,	NULL,	'T' },
		{ "check",		no_argument,		NULL,	'C' },
		{ NULL,			0,			NULL,	0}
	};
	int	rc = 0;
	int	c;
	bool	spec_mod = false;

	dss_check_mode = false;

	/* load all of modules by default */
	sprintf(modules, "%s", MODULE_LIST);
	while ((c = getopt_long(argc, argv, "c:d:f:g:hi:m:n:p:r:H:t:s:x:I:bT:C",
				opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (dss_check_mode) {
				printf("'-c|--modules' option is ignored under check mode\n");
				break;
			}

			if (strlen(optarg) > MAX_MODULE_OPTIONS) {
				rc = -DER_INVAL;
				usage(argv[0], stderr);
				break;
			}
			spec_mod = true;
			snprintf(modules, sizeof(modules), "%s", optarg);
			break;
		case 'c':
			printf("\"-c\" option is deprecated, please use \"-t\" "
			       "instead.\n");
		case 't':
			rc = arg_strtoul(optarg, &nr_threads, "\"-t\"");
			break;
		case 'x':
			rc = arg_strtoul(optarg, &dss_tgt_offload_xs_nr,
					 "\"-x\"");
			break;
		case 'f':
			rc = arg_strtoul(optarg, &dss_core_offset, "\"-f\"");
			break;
		case 'g': {
			if (strnlen(optarg, DAOS_SYS_NAME_MAX + 1) > DAOS_SYS_NAME_MAX) {
				printf("DAOS system name must be at most %d bytes\n",
				       DAOS_SYS_NAME_MAX);
				rc = -DER_INVAL;
				break;
			}
			daos_sysname = optarg;
			break;
		}
		case 's':
			dss_storage_path = optarg;
			break;
		case 'd':
			dss_socket_dir = optarg;
			break;
		case 'n':
			dss_nvme_conf = optarg;
			break;
		case 'p':
			dss_numa_node = atoi(optarg);
			break;
		case 'r':
			rc = arg_strtoul(optarg, &dss_nvme_mem_size, "\"-r\"");
			break;
		case 'H':
			rc = arg_strtoul(optarg, &dss_nvme_hugepage_size,
					 "\"-H\"");
			break;
		case 'h':
			usage(argv[0], stdout);
			break;
		case 'I':
			rc = arg_strtoul(optarg, &dss_instance_idx, "\"-I\"");
			break;
		case 'b':
			dss_nvme_bypass_health_check = true;
			break;
		case 'T':
			rc = arg_strtoul(optarg, &dss_storage_tiers, "\"-T\"");
			if (dss_storage_tiers < 1 || dss_storage_tiers > 4) {
				printf("Requires 1 to 4 tiers\n");
				rc = -DER_INVAL;
			}
			break;
		case 'C':
			dss_check_mode = true;
			if (spec_mod) {
				printf("'-c|--modules' option is ignored under check mode\n");
				spec_mod = false;
			}
			snprintf(modules, sizeof(modules), "%s", MODS_LIST_CHK);
			break;
		default:
			usage(argv[0], stderr);
			rc = -DER_INVAL;
		}
		if (rc < 0)
			return rc;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	sigset_t	set;
	int		sig;
	int		rc;

	/** parse command line arguments */
	rc = parse(argc, argv);
	if (rc)
		exit(EXIT_FAILURE);

	/** block all possible signals but faults */
	sigfillset(&set);
	sigdelset(&set, SIGILL);
	sigdelset(&set, SIGFPE);
	sigdelset(&set, SIGBUS);
	sigdelset(&set, SIGSEGV);
	/** also allow abort()/assert() to trigger */
	sigdelset(&set, SIGABRT);

	rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc) {
		perror("failed to mask signals");
		exit(EXIT_FAILURE);
	}

	/* register our own handler for faults and abort()/assert() */
	d_signal_stack_enable(true);
	d_signal_register();

	/** server initialization */
	rc = server_init(argc, argv);
	if (rc)
		exit(EXIT_FAILURE);

	/** wait for shutdown signal */
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);
	while (1) {
		rc = sigwait(&set, &sig);
		if (rc) {
			D_ERROR("failed to wait for signals: %d\n", rc);
			break;
		}

		/* open specific file to dump ABT infos and ULTs stacks */
		if (sig == SIGUSR1 || sig == SIGUSR2) {
			struct timeval tv;
			struct tm *tm = NULL;

			rc = gettimeofday(&tv, NULL);
			if (rc == 0)
				tm = localtime(&tv.tv_sec);
			else
				D_ERROR("failure to gettimeofday(): %s (%d)\n",
					strerror(errno), errno);

			 if (abt_infos == NULL) {
				/* filename format is
				 * "/tmp/daos_dump_<PID>_YYYYMMDD_hh_mm.txt"
				 */
				char name[50];

				if (rc != -1 && tm != NULL)
					snprintf(name, 50,
						 "/tmp/daos_dump_%d_%04d%02d%02d_%02d_%02d.txt",
						 getpid(), tm->tm_year + 1900,
						 tm->tm_mon + 1, tm->tm_mday,
						 tm->tm_hour, tm->tm_min);
				else
					snprintf(name, 50,
						 "/tmp/daos_dump_%d.txt",
						 getpid());

				abt_infos = fopen(name, "a");
				if (abt_infos == NULL) {
					D_ERROR("failed to open file to dump ABT infos and ULTs stacks: %s (%d)\n",
						strerror(errno), errno);
					abt_infos = stderr;
				}
			}

			/* print header msg with date */
			fprintf(abt_infos,
				"=== Dump of ABT infos and ULTs stacks in %s mode (",
				sig == SIGUSR1 ? "unattended" : "attended");
			if (rc == -1 || tm == NULL)
				fprintf(abt_infos, "time unavailable");
			else
				fprintf(abt_infos,
					"%04d/%02d/%02d-%02d:%02d:%02d.%02ld",
					tm->tm_year + 1900, tm->tm_mon + 1,
					tm->tm_mday, tm->tm_hour, tm->tm_min,
					tm->tm_sec,
					(long int)tv.tv_usec / 10000);
			fprintf(abt_infos, ")\n");
		}

		/* use this engine main thread's context to dump Argobots
		 * internal infos and ULTs stacks without internal synchro
		 */
		if (sig == SIGUSR1) {
			D_INFO("got SIGUSR1, dumping Argobots infos and ULTs stacks\n");
			dss_dump_ABT_state(abt_infos);
			continue;
		}

		/* trigger dump of all Argobots ULTs stacks with internal
		 * synchro (timeout of 10s)
		 */
		if (sig == SIGUSR2) {
			D_INFO("got SIGUSR2, attempting to trigger dump of all Argobots ULTs stacks\n");
			ABT_info_trigger_print_all_thread_stacks(abt_infos,
								 10.0, NULL,
								 NULL);
			continue;
		}

		/* SIGINT/SIGTERM cause server shutdown */
		break;
	}

	/** shutdown */
	server_fini(true);

	exit(EXIT_SUCCESS);
}
