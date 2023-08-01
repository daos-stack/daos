/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_SRV_INTERNAL__
#define __DAOS_SRV_INTERNAL__

#include <daos_srv/daos_engine.h>
#include <daos/stack_mmap.h>
#include <gurt/telemetry_common.h>

/**
 * Argobots ULT pools for different tasks, NET_POLL & NVME_POLL
 * must be the top two items.
 *
 * DSS_POOL_NET_POLL	Network poll ULT
 * DSS_POOL_NVME_POLL	NVMe poll ULT
 * DSS_POOL_GENERIC	All other ULTS
 */
enum {
	DSS_POOL_NET_POLL	= 0,
	DSS_POOL_NVME_POLL,
	DSS_POOL_GENERIC,
	DSS_POOL_CNT,
};

struct sched_stats {
	struct d_tm_node_t	*ss_total_time;		/* Total CPU time (ms) */
	struct d_tm_node_t	*ss_relax_time;		/* CPU relax time (ms) */
	struct d_tm_node_t	*ss_wq_len;		/* Wait queue length */
	struct d_tm_node_t	*ss_sq_len;		/* Sleep queue length */
	struct d_tm_node_t	*ss_cycle_duration;	/* Cycle duration (ms) */
	struct d_tm_node_t	*ss_cycle_size;		/* Total ULTs in a cycle */
	uint64_t		 ss_busy_ts;		/* Last busy timestamp (ms) */
	uint64_t		 ss_watchdog_ts;	/* Last watchdog print ts (ms) */
	void			*ss_last_unit;		/* Last executed unit */
};

struct sched_info {
	uint64_t		 si_cur_ts;	/* Current timestamp (ms) */
	uint64_t		 si_cur_seq;	/* Current schedule sequence */
	uint64_t		 si_ult_start;	/* Start time of last executed unit */
	void			*si_ult_func;	/* Function addr of last executed unit */
	struct sched_stats	 si_stats;	/* Sched stats */
	d_list_t		 si_idle_list;	/* All unused requests */
	d_list_t		 si_sleep_list;	/* All sleeping requests */
	d_list_t		 si_fifo_list;	/* All IO requests in FIFO */
	d_list_t		 si_purge_list;	/* Stale sched_pool_info */
	struct d_hash_table	*si_pool_hash;	/* All sched_pool_info */
	uint32_t		 si_req_cnt;	/* Total inuse request count */
	int			 si_sleep_cnt;	/* Sleeping request count */
	int			 si_wait_cnt;	/* Long wait request count */
	unsigned int		 si_stop:1;
};

/** Per-xstream configuration data */
struct dss_xstream {
	char			dx_name[DSS_XS_NAME_LEN];
	ABT_future		dx_shutdown;
	ABT_future		dx_stopping;
	hwloc_cpuset_t		dx_cpuset;
	ABT_xstream		dx_xstream;
	ABT_pool		dx_pools[DSS_POOL_CNT];
	ABT_sched		dx_sched;
	ABT_thread		dx_progress;
	struct sched_info	dx_sched_info;
	tse_sched_t		dx_sched_dsc;
	struct dss_rpc_cntr	dx_rpc_cntrs[DSS_RC_MAX];
	/* xstream id, [0, DSS_XS_NR_TOTAL - 1] */
	int			dx_xs_id;
	/* VOS target id, [0, dss_tgt_nr - 1]. Invalid (-1) for system XS.
	 * For offload XS it is same value as its main XS.
	 */
	int			dx_tgt_id;
	/* CART context id, invalid (-1) for the offload XS w/o CART context */
	int			dx_ctx_id;
	/* Cart progress timeout in micro-seconds */
	unsigned int		dx_timeout;
	bool			dx_main_xs;	/* true for main XS */
	bool			dx_comm;	/* true with cart context */
	bool			dx_dsc_started;	/* DSC progress ULT started */
#ifdef ULT_MMAP_STACK
	/* per-xstream pool/list of free stacks */
	struct stack_pool	*dx_sp;
#endif
	bool			dx_progress_started;	/* Network poll started */
	int                     dx_tag;                 /** tag for xstream */
};

/** Engine module's metrics */
struct engine_metrics {
	struct d_tm_node_t	*started_time;
	struct d_tm_node_t	*ready_time;
	struct d_tm_node_t	*rank_id;
	struct d_tm_node_t	*dead_rank_events;
	struct d_tm_node_t	*last_event_time;
};

extern struct engine_metrics dss_engine_metrics;

#define DSS_HOSTNAME_MAX_LEN	255

/** Server node hostname */
extern char		dss_hostname[];
/** Server node topology */
extern hwloc_topology_t	dss_topo;
/** core depth of the topology */
extern int		dss_core_depth;
/** number of physical cores, w/o hyper-threading */
extern int		dss_core_nr;
/** start offset index of the first core for service XS */
extern unsigned int	dss_core_offset;
/** NUMA node to bind to */
extern int		dss_numa_node;
/** bitmap describing core allocation */
extern hwloc_bitmap_t	core_allocation_bitmap;
/** a copy of the NUMA node object in the topology */
extern hwloc_obj_t	numa_obj;
/** number of cores in the given NUMA node */
extern int		dss_num_cores_numa_node;
/** Number of offload XS */
extern unsigned int	dss_tgt_offload_xs_nr;
/** number of system XS */
extern unsigned int	dss_sys_xs_nr;
/** Flag of helper XS as a pool */
extern bool		dss_helper_pool;

/** Shadow dss_get_module_info */
struct dss_module_info *get_module_info(void);

/* init.c */
d_rank_t dss_self_rank(void);

/* module.c */
int dss_module_init(void);
int dss_module_fini(bool force);
int dss_module_load(const char *modname);
int dss_module_init_all(uint64_t *mod_fac);
int dss_module_unload(const char *modname);
void dss_module_unload_all(void);
int dss_module_cleanup_all(void);

/* srv.c */
extern struct dss_module_key daos_srv_modkey;
int dss_srv_init(void);
int dss_srv_fini(bool force);
void dss_srv_set_shutting_down(void);
void dss_dump_ABT_state(FILE *fp);
void dss_xstreams_open_barrier(void);
struct dss_xstream *dss_get_xstream(int stream_id);
int dss_xstream_cnt(void);

/* srv_metrics.c */
int dss_engine_metrics_init(void);
int dss_engine_metrics_fini(void);

/* sched.c */
#define SCHED_RELAX_INTVL_MAX		100 /* msec */
#define SCHED_RELAX_INTVL_DEFAULT	1 /* msec */

enum sched_cpu_relax_mode {
	SCHED_RELAX_MODE_NET		= 0,
	SCHED_RELAX_MODE_SLEEP,
	SCHED_RELAX_MODE_DISABLED,
	SCHED_RELAX_MODE_INVALID,
};

static inline char *
sched_relax_mode2str(enum sched_cpu_relax_mode mode)
{
	switch (mode) {
	case SCHED_RELAX_MODE_NET:
		return "net";
	case SCHED_RELAX_MODE_SLEEP:
		return "sleep";
	case SCHED_RELAX_MODE_DISABLED:
		return "disabled";
	default:
		return "invalid";
	}
}

static inline enum sched_cpu_relax_mode
sched_relax_str2mode(char *str)
{
	if (strcasecmp(str, "sleep") == 0)
		return SCHED_RELAX_MODE_SLEEP;
	else if (strcasecmp(str, "net") == 0)
		return SCHED_RELAX_MODE_NET;
	else if (strcasecmp(str, "disabled") == 0)
		return SCHED_RELAX_MODE_DISABLED;
	else
		return SCHED_RELAX_MODE_INVALID;
}

extern bool sched_prio_disabled;
extern unsigned int sched_stats_intvl;
extern unsigned int sched_relax_intvl;
extern unsigned int sched_relax_mode;
extern unsigned int sched_unit_runtime_max;
extern bool sched_watchdog_all;

void dss_sched_fini(struct dss_xstream *dx);
int dss_sched_init(struct dss_xstream *dx);
int sched_req_enqueue(struct dss_xstream *dx, struct sched_req_attr *attr,
		      void (*func)(void *), void *arg);
void sched_stop(struct dss_xstream *dx);


static inline bool
sched_xstream_stopping(void)
{
	struct dss_xstream	*dx;
	ABT_bool		 state;
	int			 rc;

	/* ULT creation from main thread which doesn't have dss_xstream */
	if (dss_tls_get() == NULL)
		return false;

	dx = dss_current_xstream();
	rc = ABT_future_test(dx->dx_stopping, &state);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	return state == ABT_TRUE;
}

static inline int
sched_create_task(struct dss_xstream *dx, void (*func)(void *), void *arg,
		  ABT_task *task, unsigned int flags)
{
	ABT_pool		 abt_pool = dx->dx_pools[DSS_POOL_GENERIC];
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;

	if (sched_xstream_stopping())
		return -DER_SHUTDOWN;

	/* Avoid bumping busy ts for internal periodically created tasks */
	if (!(flags & DSS_ULT_FL_PERIODIC))
		/* Atomic integer assignment from different xstream */
		info->si_stats.ss_busy_ts = info->si_cur_ts;

	rc = ABT_task_create(abt_pool, func, arg, task);
	return dss_abterr2der(rc);
}

#ifdef ULT_MMAP_STACK
/* callback to ensure stack will be freed in exiting-ULT/current-XStream pool */
static inline void
dss_free_stack_cb(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;
	/* main thread doesn't have TLS and XS */
	struct dss_xstream *dx = dss_tls_get() ? dss_current_xstream() : NULL;

	/* ensure pool where to free stack is from current-XStream/ULT-exiting */
	if (dx != NULL)
		desc->sp = dx->dx_sp;

}
#else
#define dss_free_stack_cb NULL
#endif

static inline int
sched_create_thread(struct dss_xstream *dx, void (*func)(void *), void *arg,
		    ABT_thread_attr t_attr, ABT_thread *thread,
		    unsigned int flags)
{
	ABT_pool		 abt_pool = dx->dx_pools[DSS_POOL_GENERIC];
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;
#ifdef ULT_MMAP_STACK
	bool			 tls_set = dss_tls_get() ? true : false;
	struct dss_xstream	*cur_dx = NULL;

	if (tls_set)
		cur_dx = dss_current_xstream();

	/* if possible,stack should be allocated from launching XStream pool */
	if (cur_dx == NULL)
		cur_dx = dx;
#endif

	if (sched_xstream_stopping())
		return -DER_SHUTDOWN;

	/* Avoid bumping busy ts for internal periodically created ULTs */
	if (!(flags & DSS_ULT_FL_PERIODIC))
		/* Atomic integer assignment from different xstream */
		info->si_stats.ss_busy_ts = info->si_cur_ts;

	rc = daos_abt_thread_create(cur_dx->dx_sp, dss_free_stack_cb, abt_pool, func, arg, t_attr, thread);
	return dss_abterr2der(rc);
}

/* tls.c */
void dss_tls_fini(struct dss_thread_local_storage *dtls);
struct dss_thread_local_storage *dss_tls_init(int tag, int xs_id, int tgt_id);

/* server_iv.c */
void ds_iv_init(void);
void ds_iv_fini(void);

/** Total number of XS */
#define DSS_XS_NR_TOTAL						\
	(dss_sys_xs_nr + dss_tgt_nr + dss_tgt_offload_xs_nr)
/** Total number of cart contexts created */
#define DSS_CTX_NR_TOTAL					\
	(DAOS_TGT0_OFFSET + dss_tgt_nr +			\
	 (dss_tgt_offload_xs_nr > dss_tgt_nr ? dss_tgt_nr :	\
	  dss_tgt_offload_xs_nr))
/** main XS id of (vos) tgt_id */
#define DSS_MAIN_XS_ID(tgt_id)					\
	(dss_helper_pool ? ((tgt_id) + dss_sys_xs_nr) :		\
			   ((tgt_id) * ((dss_tgt_offload_xs_nr /\
			      dss_tgt_nr) + 1) + dss_sys_xs_nr))


/**
 * get the VOS target ID of xstream.
 *
 * \param[in]	xs_id	xstream ID
 *
 * \return		VOS target ID (-1 for system XS).
 */
static inline int
dss_xs2tgt(int xs_id)
{
	D_ASSERTF(xs_id >= 0 && xs_id < DSS_XS_NR_TOTAL,
		  "invalid xs_id %d, dss_tgt_nr %d, "
		  "dss_tgt_offload_xs_nr %d.\n",
		  xs_id, dss_tgt_nr, dss_tgt_offload_xs_nr);
	if (dss_helper_pool) {
		if (xs_id < dss_sys_xs_nr ||
		    xs_id >= dss_sys_xs_nr + dss_tgt_nr)
			return -1;
		return xs_id - dss_sys_xs_nr;
	}

	if (xs_id < dss_sys_xs_nr)
		return -1;
	return (xs_id - dss_sys_xs_nr) /
	       (dss_tgt_offload_xs_nr / dss_tgt_nr + 1);
}

static inline bool
dss_xstream_has_nvme(struct dss_xstream *dx)
{

	if (dx->dx_main_xs != 0)
		return true;
	if (bio_nvme_configured(SMD_DEV_TYPE_META) && dx->dx_xs_id == 0)
		return true;

	return false;
}

#endif /* __DAOS_SRV_INTERNAL__ */
