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
#ifndef __DAOS_SRV_INTERNAL__
#define __DAOS_SRV_INTERNAL__

#include <daos_srv/daos_server.h>

/**
 * Argobots ULT pools for different tasks, NET_POLL & NVME_POLL
 * must be the top two items.
 *
 * DSS_POOL_NET_POLL	Network poll ULT
 * DSS_POOL_NVME_POLL	NVMe poll ULT
 * DSS_POOL_IO		Update/Fetch/Punch, enumeration RPC handler ULTs
 * DSS_POOL_REBUILD	Rebuild/Reint scan & pull ULTs
 * DSS_POOL_GC		Space reclaiming ULTs like GC or aggregation
 * DSS_POOL_SCRUB	Scrub checksums to discover silent data corruption
 */
enum {
	DSS_POOL_NET_POLL	= 0,
	DSS_POOL_NVME_POLL,
	DSS_POOL_IO,
	DSS_POOL_REBUILD,
	DSS_POOL_GC,
	DSS_POOL_SCRUB,
	DSS_POOL_CNT,
};

struct sched_info {
	uint64_t		 si_cur_ts;	/* Current timestamp */
	d_list_t		 si_idle_list;	/* All unused requests */
	d_list_t		 si_sleep_list;	/* All sleeping requests */
	d_list_t		 si_fifo_list;	/* All IO requests in FIFO */
	d_list_t		 si_purge_list;	/* Stale sched_pool_info */
	struct d_hash_table	*si_pool_hash;	/* All sched_pool_info */
	uint32_t		 si_req_cnt;	/* Total inuse request count */
	unsigned int		 si_stop:1;
};

/** Per-xstream configuration data */
struct dss_xstream {
	char			dx_name[DSS_XS_NAME_LEN];
	ABT_future		dx_shutdown;
	hwloc_cpuset_t		dx_cpuset;
	ABT_xstream		dx_xstream;
	ABT_pool		dx_pools[DSS_POOL_CNT];
	ABT_sched		dx_sched;
	ABT_thread		dx_progress;
	struct sched_info	dx_sched_info;
	d_list_t		dx_sleep_ult_list;
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
	bool			dx_main_xs;	/* true for main XS */
	bool			dx_comm;	/* true with cart context */
	bool			dx_dsc_started;	/* DSC progress ULT started */
};

/** Server node topology */
extern hwloc_topology_t	dss_topo;
/** core depth of the topology */
extern int		dss_core_depth;
/** number of physical cores, w/o hyper-threading */
extern int		dss_core_nr;
/** start offset index of the first core for service XS */
extern int		dss_core_offset;
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

/* module.c */
int dss_module_init(void);
int dss_module_fini(bool force);
int dss_module_load(const char *modname);
int dss_module_init_all(uint64_t *mod_fac);
int dss_module_unload(const char *modname);
void dss_module_unload_all(void);
int dss_module_setup_all(void);
int dss_module_cleanup_all(void);

/* srv.c */
int dss_srv_init(void);
int dss_srv_fini(bool force);
void dss_dump_ABT_state(void);
void dss_xstreams_open_barrier(void);
struct dss_xstream *dss_get_xstream(int stream_id);
int dss_xstream_cnt(void);

/* sched.c */
void dss_sched_fini(struct dss_xstream *dx);
int dss_sched_init(struct dss_xstream *dx);
int sched_set_throttle(unsigned int type, unsigned int percent);
int sched_req_enqueue(struct dss_xstream *dx, struct sched_req_attr *attr,
		      void (*func)(void *), void *arg);
void sched_stop(struct dss_xstream *dx);

/* tls.c */
void dss_tls_fini(struct dss_thread_local_storage *dtls);
struct dss_thread_local_storage *dss_tls_init(int tag);

/* server_iv.c */
void ds_iv_init(void);
void ds_iv_fini(void);

/** To schedule ULT on caller's self XS */
#define DSS_XS_SELF		(-1)
/** Total number of XS */
#define DSS_XS_NR_TOTAL						\
	(dss_sys_xs_nr + dss_tgt_nr + dss_tgt_offload_xs_nr)
/** Total number of cart contexts created */
#define DSS_CTX_NR_TOTAL					\
	(DAOS_TGT0_OFFSET + dss_tgt_nr +			\
	 (dss_tgt_offload_xs_nr > dss_tgt_nr ? dss_tgt_nr :	\
	  dss_tgt_offload_xs_nr))
/** main XS id of (vos) tgt_id */
#define DSS_MAIN_XS_ID(tgt_id)						\
	(dss_helper_pool ? ((tgt_id) + dss_sys_xs_nr) :			\
			   ((tgt_id) * ((dss_tgt_offload_xs_nr /	\
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

#endif /* __DAOS_SRV_INTERNAL__ */
