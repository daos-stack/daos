/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT. It gives out the data types internally used by
 * CaRT and not in other specific header files.
 */

#ifndef __CRT_INTERNAL_TYPES_H__
#define __CRT_INTERNAL_TYPES_H__

#define CRT_CONTEXT_NULL         (NULL)

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <gurt/list.h>
#include <gurt/hash.h>
#include <gurt/heap.h>
#include <gurt/atomic.h>

struct crt_hg_gdata;
struct crt_grp_gdata;

/* CaRT global data */
struct crt_gdata {
	/*
	 * TODO: Temporary storage for context0 URI, used during group
	 * attach info file population for self address. Per-provider
	 * context0 URIs need to be stored for multi-provider support
	 */
	char			cg_addr[CRT_ADDR_STR_MAX_LEN];

	/* Flag indicating whether it is a client or server */
	bool			cg_server;
	/* Flag indicating whether scalable endpoint mode is enabled */
	bool			cg_sep_mode;
	int			cg_na_plugin; /* NA plugin type */

	/* global timeout value (second) for all RPCs */
	uint32_t		cg_timeout;
	/* credits limitation for #inflight RPCs per target EP CTX */
	uint32_t		cg_credit_ep_ctx;

	/* CaRT contexts list */
	d_list_t		cg_ctx_list;
	/* actual number of items in CaRT contexts list */
	int			cg_ctx_num;
	/* maximum number of contexts user wants to create */
	uint32_t		cg_ctx_max_num;
	/* the global opcode map */
	struct crt_opc_map	*cg_opc_map;
	/* HG level global data */
	struct crt_hg_gdata	*cg_hg;

	struct crt_grp_gdata	*cg_grp;

	/* refcount to protect crt_init/crt_finalize */
	volatile unsigned int	cg_refcount;

	/* flags to keep track of states */
	volatile unsigned int	cg_inited		: 1,
				cg_grp_inited		: 1,
				cg_swim_inited		: 1,
				cg_auto_swim_disable	: 1;

	ATOMIC uint64_t		cg_rpcid; /* rpc id */

	/* protects crt_gdata */
	pthread_rwlock_t	cg_rwlock;
};

extern struct crt_gdata		crt_gdata;

struct crt_prog_cb_priv {
	d_list_t		 cpcp_link;
	crt_progress_cb		 cpcp_func;
	void			*cpcp_args;
};

struct crt_timeout_cb_priv {
	d_list_t		 ctcp_link;
	crt_timeout_cb		 ctcp_func;
	void			*ctcp_args;
};

struct crt_event_cb_priv {
	d_list_t		 cecp_link;
	crt_event_cb		 cecp_func;
	void			*cecp_args;
};

/* TODO: remove the three structs above, use the following one for all */
struct crt_plugin_cb_priv {
	d_list_t			 cp_link;
	union {
		crt_progress_cb		 cp_prog_cb;
		crt_timeout_cb		 cp_timeout_cb;
		crt_event_cb		 cp_event_cb;
		crt_eviction_cb		 cp_eviction_cb;
	};
	void				*cp_args;
};

/* TODO may use a RPC to query server-side context number */
#ifndef CRT_SRV_CONTEXT_NUM
# define CRT_SRV_CONTEXT_NUM		(256)
#endif

/* structure of global fault tolerance data */
struct crt_plugin_gdata {
	/* list of progress callbacks */
	d_list_t		cpg_prog_cbs[CRT_SRV_CONTEXT_NUM];
	/* list of rpc timeout callbacks */
	d_list_t		cpg_timeout_cbs;
	/* list of event notification callbacks */
	d_list_t		cpg_event_cbs;
	uint32_t		cpg_inited:1;
	pthread_rwlock_t	cpg_prog_rwlock[CRT_SRV_CONTEXT_NUM];
	pthread_rwlock_t	cpg_timeout_rwlock;
	pthread_rwlock_t	cpg_event_rwlock;
};

extern struct crt_plugin_gdata		crt_plugin_gdata;

/* (1 << CRT_EPI_TABLE_BITS) is the number of buckets of epi hash table */
#define CRT_EPI_TABLE_BITS		(3)
#define CRT_DEFAULT_CREDITS_PER_EP_CTX	(32)
#define CRT_MAX_CREDITS_PER_EP_CTX	(256)

/* crt_context */
struct crt_context {
	d_list_t		 cc_link; /* link to gdata.cg_ctx_list */
	int			 cc_idx; /* context index */
	struct crt_hg_context	 cc_hg_ctx; /* HG context */
	void			*cc_rpc_cb_arg;
	crt_rpc_task_t		 cc_rpc_cb; /* rpc callback */
	crt_rpc_task_t		 cc_iv_resp_cb;
	/* in-flight endpoint tracking hash table */
	struct d_hash_table	 cc_epi_table;
	/* binheap for inflight RPC timeout tracking */
	struct d_binheap	 cc_bh_timeout;
	/* mutex to protect cc_epi_table and timeout binheap */
	pthread_mutex_t		 cc_mutex;
	/* timeout per-context */
	uint32_t		 cc_timeout_sec;
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
	/* (ei_req_num - ei_reply_num) is the number of inflight req */
	int64_t			 epi_req_num; /* total number of req send */
	int64_t			 epi_reply_num; /* total number of reply recv */
	/* RPC req wait queue */
	d_list_t		 epi_req_waitq;
	int64_t			 epi_req_wait_num;

	unsigned int		 epi_ref;
	unsigned int		 epi_initialized:1;

	/* mutex to protect ei_req_q and some counters */
	pthread_mutex_t		 epi_mutex;
};

#define CRT_UNLOCK			(0)
#define CRT_LOCKED			(1)

#define CRT_OPC_MAP_BITS	8
#define CRT_OPC_MAP_BITS_LEGACY	12

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

struct crt_opc_map {
	pthread_rwlock_t	 com_rwlock;
	unsigned int		 com_lock_init:1;
	unsigned int		 com_pid;
	unsigned int		 com_bits;
	unsigned int		 com_num_slots_total;
	struct crt_opc_map_L2	*com_map;
};

struct na_ofi_config {
	int32_t		 noc_port;
	char		*noc_interface;
	char		*noc_domain;
	/* IP addr str for the noc_interface */
	char		 noc_ip_str[INET_ADDRSTRLEN];
};

int crt_na_ofi_config_init(void);
void crt_na_ofi_config_fini(void);

extern struct na_ofi_config crt_na_ofi_conf;

#endif /* __CRT_INTERNAL_TYPES_H__ */
