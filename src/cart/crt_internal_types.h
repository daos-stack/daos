/*
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the data types internally used by
 * CaRT and not in other specific header files.
 */

#ifndef __CRT_INTERNAL_TYPES_H__
#define __CRT_INTERNAL_TYPES_H__

#define CRT_CONTEXT_NULL         (NULL)

#ifndef CRT_SRV_CONTEXT_NUM
#define CRT_SRV_CONTEXT_NUM (128) /* Maximum number of contexts */
#endif


#include <arpa/inet.h>
#include <ifaddrs.h>

#include <gurt/list.h>
#include <gurt/hash.h>
#include <gurt/heap.h>
#include <gurt/atomic.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

struct crt_hg_gdata;
struct crt_grp_gdata;

struct crt_na_config {
	int32_t		 noc_port;
	int		 noc_iface_total;
	int               noc_domain_total;
	char		*noc_interface;
	char		*noc_domain;
	char		*noc_auth_key;
	char		**noc_iface_str; /* Array of interfaces */
	char            **noc_domain_str; /* Array of domains */
};

#define CRT_TRAFFIC_CLASSES                                                                        \
	X(CRT_TC_UNSPEC, "unspec")           /* Leave it upon plugin to choose */                  \
	X(CRT_TC_BEST_EFFORT, "best_effort") /* Best effort */                                     \
	X(CRT_TC_LOW_LATENCY, "low_latency") /* Low latency */                                     \
	X(CRT_TC_BULK_DATA, "bulk_data")     /* Bulk data */                                       \
	X(CRT_TC_UNKNOWN, "unknown")         /* Unknown */

#define X(a, b) a,
enum crt_traffic_class { CRT_TRAFFIC_CLASSES };
#undef X

struct crt_prov_gdata {
	/** NA plugin type */
	int                  cpg_provider;

	struct crt_na_config cpg_na_config;
	/** Context0 URI */
	char                 cpg_addr[CRT_ADDR_STR_MAX_LEN];

	/** CaRT contexts list */
	d_list_t             cpg_ctx_list;
	/** actual number of items in CaRT contexts list */
	int                  cpg_ctx_num;
	/** maximum number of contexts user wants to create */
	uint32_t             cpg_ctx_max_num;

	/** free-list of indices */
	bool                 cpg_used_idx[CRT_SRV_CONTEXT_NUM];

	/** Hints to mercury/ofi for max expected/unexp sizes */
	uint32_t             cpg_max_exp_size;
	uint32_t             cpg_max_unexp_size;

	/** Number of remote tags */
	uint32_t             cpg_num_remote_tags;
	uint32_t             cpg_last_remote_tag;

	/** Set of flags */
	bool                 cpg_sep_mode;
	bool                 cpg_primary;
	bool                 cpg_contig_ports;
	bool                 cpg_inited;
	bool                 cpg_progress_busy;

	/** Mutext to protect fields above */
	pthread_mutex_t      cpg_mutex;
};

#define MAX_NUM_SECONDARY_PROVS 2

/* CaRT global data */
struct crt_gdata {
	/** Providers iinitialized at crt_init() time */
	int			cg_primary_prov;
	int			cg_num_secondary_provs;
	int			*cg_secondary_provs;

	/** Provider specific data */
	struct crt_prov_gdata	cg_prov_gdata_primary;

	/** Placeholder for secondary provider data */
	struct crt_prov_gdata	*cg_prov_gdata_secondary;

	/** Hints to mercury for request post init (ignored for clients) */
	uint32_t                 cg_post_init;
	uint32_t                 cg_post_incr;
	unsigned int             cg_mrecv_buf;
	unsigned int             cg_mrecv_buf_copy;

	/** global timeout value (second) for all RPCs */
	uint32_t		cg_timeout;

	/** cart context index used by SWIM */
	int32_t                  cg_swim_ctx_idx;

	/** traffic class used by SWIM */
	enum crt_traffic_class   cg_swim_tc;

	/** credits limitation for #in-flight RPCs per target EP CTX */
	uint32_t		cg_credit_ep_ctx;

	uint32_t		cg_iv_inline_limit;
	/** the global opcode map */
	struct crt_opc_map	*cg_opc_map;
	/** HG level global data */
	struct crt_hg_gdata	*cg_hg;

	/** Points to default group */
	struct crt_grp_gdata	*cg_grp;

	/** refcount to protect crt_init/crt_finalize */
	volatile unsigned int	cg_refcount;

	/** flags to keep track of states */
	unsigned int             cg_inited              : 1;
	unsigned int             cg_grp_inited          : 1;
	unsigned int             cg_swim_inited         : 1;
	unsigned int             cg_auto_swim_disable   : 1;

	/** whether it is a client or server */
	unsigned int             cg_server              : 1;
	/** whether metrics are used */
	unsigned int             cg_use_sensors         : 1;
	/** whether we are on a primary provider */
	unsigned int             cg_provider_is_primary : 1;

	/** use single thread to access context */
	bool                     cg_thread_mode_single;

	ATOMIC uint64_t		cg_rpcid; /* rpc id */

	/* protects crt_gdata (see the lock order comment on crp_mutex) */
	pthread_rwlock_t	cg_rwlock;

	/** Global statistics (when cg_use_sensors = true) */
	/**
	 * Total number of successfully served URI lookup for self,
	 * of type counter
	 */
	struct d_tm_node_t	*cg_uri_self;
	/**
	 * Total number of successfully served (from cache) URI lookup for
	 * others, of type counter
	 */
	struct d_tm_node_t	*cg_uri_other;
	/** Number of cores on a system */
	long			 cg_num_cores;
	/** Inflight rpc quota limit */
	uint32_t                 cg_rpc_quota;
	/** bulk quota limit */
	uint32_t                 cg_bulk_quota;
	/** Retry count of HG_Init_opt2() on failure when using CXI provider */
	uint32_t                 cg_hg_init_retry_cnt;
};

extern struct crt_gdata		crt_gdata;

struct crt_event_cb_priv {
	crt_event_cb		 cecp_func;
	void			*cecp_args;
};

#ifndef CRT_PROGRESS_NUM
#define CRT_CALLBACKS_NUM		(4)	/* start number of CBs */
#endif

/*
 * List of environment variables to read at CaRT library load time.
 * for integer envs use ENV()
 * for string ones ENV_STR() or ENV_STR_NO_PRINT()
 **/
#define CRT_ENV_LIST                                                                               \
	ENV_STR(CRT_ATTACH_INFO_PATH)                                                              \
	ENV(CRT_CREDIT_EP_CTX)                                                                     \
	ENV(CRT_CTX_NUM)                                                                           \
	ENV(CRT_CXI_INIT_RETRY)                                                                    \
	ENV(CRT_ENABLE_MEM_PIN)                                                                    \
	ENV_STR(CRT_L_GRP_CFG)                                                                     \
	ENV(CRT_L_RANK)                                                                            \
	ENV(CRT_MRC_ENABLE)                                                                        \
	ENV(CRT_SECONDARY_PROVIDER)                                                                \
	ENV(CRT_TIMEOUT)                                                                           \
	ENV(DAOS_RPC_SIZE_LIMIT)                                                                   \
	ENV(DAOS_SIGNAL_REGISTER)                                                                  \
	ENV_STR(DAOS_TEST_SHARED_DIR)                                                              \
	ENV_STR(DD_MASK)                                                                           \
	ENV_STR(DD_STDERR)                                                                         \
	ENV_STR(DD_SUBSYS)                                                                         \
	ENV_STR(D_CLIENT_METRICS_DUMP_DIR)                                                         \
	ENV(D_CLIENT_METRICS_ENABLE)                                                               \
	ENV(D_CLIENT_METRICS_RETAIN)                                                               \
	ENV_STR(D_DOMAIN)                                                                          \
	ENV_STR(D_FI_CONFIG)                                                                       \
	ENV_STR(D_INTERFACE)                                                                       \
	ENV_STR(D_LOG_FILE)                                                                        \
	ENV_STR(D_LOG_FILE_APPEND_PID)                                                             \
	ENV_STR(D_LOG_FILE_APPEND_RANK)                                                            \
	ENV_STR(D_LOG_FLUSH)                                                                       \
	ENV_STR(D_LOG_MASK)                                                                        \
	ENV_STR(D_LOG_SIZE)                                                                        \
	ENV(D_LOG_STDERR_IN_LOG)                                                                   \
	ENV(D_POLL_TIMEOUT)                                                                        \
	ENV_STR(D_PORT)                                                                            \
	ENV(D_PORT_AUTO_ADJUST)                                                                    \
	ENV(D_THREAD_MODE_SINGLE)                                                                  \
	ENV(D_PROGRESS_BUSY)                                                                       \
	ENV(D_POST_INCR)                                                                           \
	ENV(D_POST_INIT)                                                                           \
	ENV(D_MRECV_BUF)                                                                           \
	ENV(D_MRECV_BUF_COPY)                                                                      \
	ENV_STR(D_PROVIDER)                                                                        \
	ENV_STR_NO_PRINT(D_PROVIDER_AUTH_KEY)                                                      \
	ENV(D_QUOTA_RPCS)                                                                          \
	ENV(D_QUOTA_BULKS)                                                                         \
	ENV(FI_OFI_RXM_USE_SRX)                                                                    \
	ENV(FI_UNIVERSE_SIZE)                                                                      \
	ENV(SWIM_PING_TIMEOUT)                                                                     \
	ENV(SWIM_PROTOCOL_PERIOD_LEN)                                                              \
	ENV(SWIM_SUBGROUP_SIZE)                                                                    \
	ENV(SWIM_SUSPECT_TIMEOUT)                                                                  \
	ENV_STR(SWIM_TRAFFIC_CLASS)                                                                \
	ENV_STR(UCX_IB_FORK_INIT)

/* uint env */
#define ENV(x)                                                                                     \
	unsigned int _##x;                                                                         \
	int          _rc_##x;                                                                      \
	int          _no_print_##x;

/* char* env */
#define ENV_STR(x)                                                                                 \
	char *_##x;                                                                                \
	int   _rc_##x;                                                                             \
	int   _no_print_##x;

#define ENV_STR_NO_PRINT(x) ENV_STR(x)

struct crt_envs {
	CRT_ENV_LIST;
	bool inited;
};

#undef ENV
#undef ENV_STR
#undef ENV_STR_NO_PRINT

extern struct crt_envs crt_genvs;

static inline void
crt_env_fini(void);

static inline void
crt_env_init(void)
{
	/* release strings if already inited previously */
	if (crt_genvs.inited)
		crt_env_fini();

#define ENV(x)                                                                                     \
	do {                                                                                       \
		crt_genvs._rc_##x       = d_getenv_uint(#x, &crt_genvs._##x);                      \
		crt_genvs._no_print_##x = 0;                                                       \
	} while (0);

#define ENV_STR(x)                                                                                 \
	do {                                                                                       \
		crt_genvs._rc_##x       = d_agetenv_str(&crt_genvs._##x, #x);                      \
		crt_genvs._no_print_##x = 0;                                                       \
	} while (0);

#define ENV_STR_NO_PRINT(x)                                                                        \
	do {                                                                                       \
		crt_genvs._rc_##x       = d_agetenv_str(&crt_genvs._##x, #x);                      \
		crt_genvs._no_print_##x = 1;                                                       \
	} while (0);

	CRT_ENV_LIST;
#undef ENV
#undef ENV_STR
#undef ENV_STR_NO_PRINT

	crt_genvs.inited = true;
}

static inline void
crt_env_fini(void)
{
#define ENV(x)           (void)
#define ENV_STR(x)       d_freeenv_str(&crt_genvs._##x);
#define ENV_STR_NO_PRINT ENV_STR

	CRT_ENV_LIST

#undef ENV
#undef ENV_STR
#undef ENV_STR_NO_PRINT

	crt_genvs.inited = false;
}

/* Returns value if env was present at load time */
#define crt_env_get(name, val)                                                                     \
	do {                                                                                       \
		D_ASSERT(crt_genvs.inited);                                                        \
		if (crt_genvs._rc_##name == 0)                                                     \
			*val = crt_genvs._##name;                                                  \
	} while (0)

static inline void
crt_env_dump(void)
{
	D_INFO("--- ENV ---\n");

	/* Only dump envariables that were set */
#define ENV(x)                                                                                     \
	if (!crt_genvs._rc_##x && crt_genvs._no_print_##x == 0)                                    \
		D_INFO("%s = %d\n", #x, crt_genvs._##x);

#define ENV_STR(x)                                                                                 \
	if (!crt_genvs._rc_##x)                                                                    \
		D_INFO("%s = %s\n", #x, crt_genvs._no_print_##x ? "****" : crt_genvs._##x);

#define ENV_STR_NO_PRINT ENV_STR

	CRT_ENV_LIST;

#undef ENV
#undef ENV_STR
#undef ENV_STR_NO_PRINT
}

/* structure of global fault tolerance data */
struct crt_plugin_gdata {
	/* list of event notification callbacks */
	size_t				 cpg_event_size;
	struct crt_event_cb_priv	*cpg_event_cbs;
	struct crt_event_cb_priv	*cpg_event_cbs_old;
	uint32_t			 cpg_inited:1;
	/* hlc error event callback */
	crt_hlc_error_cb                 hlc_error_cb;
	void                            *hlc_error_cb_arg;
	/* mutex to protect all callbacks change only */
	pthread_mutex_t			 cpg_mutex;
};

extern struct crt_plugin_gdata		crt_plugin_gdata;

/* (1 << CRT_EPI_TABLE_BITS) is the number of buckets of epi hash table */
#define CRT_EPI_TABLE_BITS		(3)
#define CRT_DEFAULT_CREDITS_PER_EP_CTX	(32)
#define CRT_MAX_CREDITS_PER_EP_CTX	(256)

struct crt_quotas {
	int			limit[CRT_QUOTA_COUNT];
	ATOMIC uint32_t		current[CRT_QUOTA_COUNT];
	bool			enabled[CRT_QUOTA_COUNT];
	pthread_mutex_t		mutex;
	d_list_t		rpc_waitq;
	/** Stats gauge of wait queue depth */
	struct d_tm_node_t     *rpc_waitq_depth;
	/** Counter for exceeded quota */
	struct d_tm_node_t     *rpc_quota_exceeded;
};

/*
 * crt_bulk - wrapper struct for crt_bulk_t type
 *
 * Local structure for representing mercury bulk handle.
 * Allows deferred allocations of mercury bulk handles by postponing
 * them until RPC encode time, right before sending onto the wire (HG_Forward())
 * See crt_proc_crt_bulk_t() for more details
 *
 * During deferred allocations hg_bulk_hdl will be set to HG_BULK_NULL (null),
 * deferred flag set to true and other fields populated based on the original
 * bulk info provided.
 *
 * Deferred allocation is only supported on clients through D_QUOTA_BULKS env
 */
struct crt_bulk {
	hg_bulk_t       hg_bulk_hdl; /** mercury bulk handle */
	bool            deferred;    /** whether handle allocation was deferred */
	crt_context_t   crt_ctx;     /** context on which bulk is to be created  */
	bool            bound;       /** whether crt_bulk_bind() was used on it */
	d_sg_list_t     sgl;         /** original sgl */
	crt_bulk_perm_t bulk_perm;   /** bulk permissions */
};

/* crt_context */
struct crt_context {
	d_list_t		 cc_link;	/** link to gdata.cg_ctx_list */
	int			 cc_idx;	/** context index */
	struct crt_hg_context	 cc_hg_ctx;	/** HG context */
	bool			 cc_primary;	/** primary provider flag */

	/* callbacks */
	void			*cc_rpc_cb_arg;
	crt_rpc_task_t		 cc_rpc_cb;	/** rpc callback */
	crt_rpc_task_t		 cc_iv_resp_cb;

	/* progress callback */
	void                    *cc_prog_cb_arg;
	crt_progress_cb          cc_prog_cb;

	/** RPC tracking */
	/** in-flight endpoint tracking hash table */
	struct d_hash_table	 cc_epi_table;
	/** binheap for in-flight RPC timeout tracking */
	struct d_binheap	 cc_bh_timeout;
	/**
	 * mutex to protect cc_epi_table and timeout binheap (see the lock
	 * order comment on crp_mutex)
	 */
	pthread_mutex_t		 cc_mutex;

	/** timeout per-context */
	uint32_t                 cc_timeout_sec;

	/** Per-context statistics (server-side only) */
	/** Total number of timed out requests, of type counter */
	struct d_tm_node_t	*cc_timedout;
	/** Total number of timed out URI lookup requests, of type counter */
	struct d_tm_node_t	*cc_timedout_uri;
	/** Total number of failed address resolution, of type counter */
	struct d_tm_node_t	*cc_failed_addr;
	/** Counter for number of network glitches */
	struct d_tm_node_t      *cc_net_glitches;
	/** Stats gauge of reported SWIM delays */
	struct d_tm_node_t      *cc_swim_delay;

	/** Stores self uri for the current context */
	char			 cc_self_uri[CRT_ADDR_STR_MAX_LEN];

	/** Stores quotas */
	struct crt_quotas	cc_quotas;
};

/* in-flight RPC req list, be tracked per endpoint for every crt_context */
struct crt_ep_inflight {
	/* link to crt_context::cc_epi_table */
	d_list_t		 epi_link;
	/* endpoint address */
	crt_endpoint_t		 epi_ep;
	struct crt_context	*epi_ctx;

	/* in-flight RPC req queue */
	d_list_t		 epi_req_q;
	/* (ei_req_num - ei_reply_num) is the number of in-flight req */
	int64_t			 epi_req_num; /* total number of req send */
	int64_t			 epi_reply_num; /* total number of reply recv */
	/* RPC req wait queue */
	d_list_t		 epi_req_waitq;
	int64_t			 epi_req_wait_num;

	unsigned int		 epi_ref;
	unsigned int		 epi_initialized:1;

	/*
	 * mutex to protect ei_req_q and some counters (see the lock order
	 * comment on crp_mutex)
	 */
	pthread_mutex_t		 epi_mutex;
};

#define CRT_UNLOCK			(0)
#define CRT_LOCKED			(1)

/* highest protocol version allowed */
#define CRT_PROTO_MAX_VER	(0xFFUL)
/* max member RPC count allowed in one protocol  */
#define CRT_PROTO_MAX_COUNT	(0xFFFFUL)
#define CRT_PROTO_BASEOPC_MASK	(0xFF000000UL)
#define CRT_PROTO_BASEOPC_SHIFT         24
#define CRT_PROTO_VER_MASK	(0x00FF0000UL)
#define CRT_PROTO_COUNT_MASK	(0x0000FFFFUL)

struct crt_opc_info {
	d_list_t		 coi_link;
	crt_opcode_t		 coi_opc;
	unsigned int		 coi_inited:1,
				 coi_proc_init:1,
				 coi_rpccb_init:1,
				 coi_coops_init:1,
				 coi_no_reply:1, /* flag of one-way RPC */
				 coi_queue_front:1, /* add to front of queue */
				 coi_reset_timer:1; /* reset timer on timeout */

	crt_rpc_cb_t		 coi_rpc_cb;
	struct crt_corpc_ops	*coi_co_ops;

	/* Sizes/offset used when buffers are part of the same allocation
	 * as the rpc descriptor.
	 */
	size_t			 coi_rpc_size;
	off_t			 coi_input_offset;
	off_t			 coi_output_offset;
	struct crt_req_format	*coi_crf;
};

/* opcode map (three-level array) */
struct crt_opc_map_L3 {
	unsigned int		 L3_num_slots_total;
	unsigned int		 L3_num_slots_used;
	struct crt_opc_info	*L3_map;
};

struct crt_opc_map_L2 {
	unsigned int		 L2_num_slots_total;
	unsigned int		 L2_num_slots_used;
	struct crt_opc_map_L3	*L2_map;
};

struct crt_opc_queried {
	uint32_t		coq_version;
	crt_opcode_t		coq_base;
	d_list_t		coq_list;
};

struct crt_opc_map {
	pthread_rwlock_t	com_rwlock;
	unsigned int		com_num_slots_total;
	d_list_t		com_coq_list;
	struct crt_opc_map_L2	*com_map;
};

void
crt_na_config_fini(bool primary, crt_provider_t provider);

#endif /* __CRT_INTERNAL_TYPES_H__ */
