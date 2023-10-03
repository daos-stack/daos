/*
 * (C) Copyright 2016-2023 Intel Corporation.
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
#define CRT_SRV_CONTEXT_NUM (64)	/* Maximum number of contexts */
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
	char		*noc_interface;
	char		*noc_domain;
	char		*noc_auth_key;
	/* IP addr str for the noc_interface */
	char		 noc_ip_str[INET_ADDRSTRLEN];
};

struct crt_prov_gdata {
	/** NA plugin type */
	int			cpg_provider;

	struct crt_na_config	cpg_na_config;
	/** Context0 URI */
	char			cpg_addr[CRT_ADDR_STR_MAX_LEN];

	/** CaRT contexts list */
	d_list_t		cpg_ctx_list;
	/** actual number of items in CaRT contexts list */
	int			cpg_ctx_num;
	/** maximum number of contexts user wants to create */
	uint32_t		cpg_ctx_max_num;

	/** free-list of indices */
	bool			cpg_used_idx[CRT_SRV_CONTEXT_NUM];

	/** Hints to mercury/ofi for max expected/unexp sizes */
	uint32_t		cpg_max_exp_size;
	uint32_t		cpg_max_unexp_size;

	/** Number of remote tags */
	uint32_t		cpg_num_remote_tags;
	uint32_t		cpg_last_remote_tag;

	/** Set of flags */
	unsigned int		cpg_sep_mode		: 1,
				cpg_primary		: 1,
				cpg_contig_ports	: 1,
				cpg_inited		: 1;

	/** Mutext to protect fields above */
	pthread_mutex_t		cpg_mutex;
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

	/** */
	struct crt_prov_gdata	*cg_prov_gdata_secondary;

	/** global timeout value (second) for all RPCs */
	uint32_t		cg_timeout;

	/** global swim index for all servers */
	int32_t			cg_swim_crt_idx;

	/** credits limitation for #in-flight RPCs per target EP CTX */
	uint32_t		cg_credit_ep_ctx;

	/** the global opcode map */
	struct crt_opc_map	*cg_opc_map;
	/** HG level global data */
	struct crt_hg_gdata	*cg_hg;

	struct crt_grp_gdata	*cg_grp;

	/** refcount to protect crt_init/crt_finalize */
	volatile unsigned int	cg_refcount;

	/** flags to keep track of states */
	unsigned int		cg_inited		: 1,
				cg_grp_inited		: 1,
				cg_swim_inited		: 1,
				cg_auto_swim_disable	: 1,
				/** whether it is a client or server */
				cg_server		: 1,
				/** whether scalable endpoint is enabled */
				cg_use_sensors		: 1,
				/** whether we are on a primary provider */
				cg_provider_is_primary	: 1;

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
};

extern struct crt_gdata		crt_gdata;

struct crt_prog_cb_priv {
	crt_progress_cb		 cpcp_func;
	void			*cpcp_args;
};

struct crt_event_cb_priv {
	crt_event_cb		 cecp_func;
	void			*cecp_args;
};

#ifndef CRT_PROGRESS_NUM
#define CRT_CALLBACKS_NUM		(4)	/* start number of CBs */
#endif

/* structure of global fault tolerance data */
struct crt_plugin_gdata {
	/* list of progress callbacks */
	size_t				 cpg_prog_size[CRT_SRV_CONTEXT_NUM];
	struct crt_prog_cb_priv		*cpg_prog_cbs[CRT_SRV_CONTEXT_NUM];
	struct crt_prog_cb_priv		*cpg_prog_cbs_old[CRT_SRV_CONTEXT_NUM];
	/* list of event notification callbacks */
	size_t				 cpg_event_size;
	struct crt_event_cb_priv	*cpg_event_cbs;
	struct crt_event_cb_priv	*cpg_event_cbs_old;
	uint32_t			 cpg_inited:1;
	/* hlc error event callback */
	crt_hlc_error_cb		 hlc_error_cb;
	void				*hlc_error_cb_arg;

	/* mutex to protect all callbacks change only */
	pthread_mutex_t			 cpg_mutex;
};

extern struct crt_plugin_gdata		crt_plugin_gdata;

/* (1 << CRT_EPI_TABLE_BITS) is the number of buckets of epi hash table */
#define CRT_EPI_TABLE_BITS		(3)
#define CRT_DEFAULT_CREDITS_PER_EP_CTX	(32)
#define CRT_MAX_CREDITS_PER_EP_CTX	(256)

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
	uint32_t		 cc_timeout_sec;
	/** HLC time of last received RPC */
	uint64_t		 cc_last_unpack_hlc;

	/** Per-context statistics (server-side only) */
	/** Total number of timed out requests, of type counter */
	struct d_tm_node_t	*cc_timedout;
	/** Total number of timed out URI lookup requests, of type counter */
	struct d_tm_node_t	*cc_timedout_uri;
	/** Total number of failed address resolution, of type counter */
	struct d_tm_node_t	*cc_failed_addr;

	/** Stores self uri for the current context */
	char			 cc_self_uri[CRT_ADDR_STR_MAX_LEN];
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
