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
 * This file is part of CaRT. It gives out the data types internally used by
 * CaRT and not in other specific header files.
 */

#ifndef __CRT_INTERNAL_TYPES_H__
#define __CRT_INTERNAL_TYPES_H__

#define CRT_CONTEXT_NULL         (NULL)
#define CRT_BULK_NULL            (NULL)

#include <pthread.h>

#include <crt_util/list.h>
#include <crt_util/hash.h>

#include <crt_hg.h>

struct crt_hg_gdata;
struct crt_grp_gdata;

/* CaRT global data */
struct crt_gdata {
	crt_phy_addr_t		cg_addr;
	uint32_t		cg_addr_len;

	bool			cg_server;
	bool			cg_verbs; /* CCI verbs transport flag */
	/* multiple NA addr flag, true for server when using CCI plugin */
	bool			cg_multi_na;

	/* CaRT contexts list */
	crt_list_t             cg_ctx_list;
	/* actual number of items in CaRT contexts list */
	int			cg_ctx_num;
	/* the global opcode map */
	struct crt_opc_map	*cg_opc_map;
	/* HG level global data */
	struct crt_hg_gdata	*cg_hg;

	struct crt_grp_gdata	*cg_grp;

	/* the unique global server and client group ID */
	crt_group_id_t		cg_srv_grp_id;
	crt_group_id_t		cg_cli_grp_id;

	/* protects crt_gdata */
	pthread_rwlock_t	cg_rwlock;
	/* refcount to protect crt_init/crt_finalize */
	volatile unsigned int	cg_refcount;
	volatile unsigned int	cg_inited:1,
				cg_grp_inited:1; /* group initialized */
	/* ... */
};

extern struct crt_gdata		crt_gdata;

/* TODO may use a RPC to query server-side context number */
#ifndef CRT_SRV_CONTEXT_NUM
# define CRT_SRV_CONTEXT_NUM		(256)
#endif

/* (1 << CRT_EPI_TABLE_BITS) is the number of buckets of epi hash table */
#define CRT_EPI_TABLE_BITS		(3)
#define CRT_MAX_INFLIGHT_PER_EP_CTX	(32)

/* crt_context */
struct crt_context {
	crt_list_t		 dc_link; /* link to gdata.cg_ctx_list */
	int			 dc_idx; /* context index */
	struct crt_hg_context	 dc_hg_ctx; /* HG context */
	void			*dc_pool; /* pool for ES on server stack */
	/* in-flight endpoint tracking hash table */
	struct dhash_table	 dc_epi_table;
	/* mutex to protect dc_epi_table */
	pthread_mutex_t		 dc_mutex;
};

/* in-flight RPC req list, be tracked per endpoint for every crt_context */
struct crt_ep_inflight {
	/* link to crt_context::dc_epi_table */
	crt_list_t		epi_link;
	/* endpoint address */
	crt_endpoint_t		epi_ep;
	struct crt_context	*epi_ctx;

	/* in-flight RPC req queue */
	crt_list_t		epi_req_q;
	/* (ei_req_num - ei_reply_num) is the number of inflight req */
	int64_t			epi_req_num; /* total number of req send */
	int64_t			epi_reply_num; /* total number of reply recv */
	/* RPC req wait queue */
	crt_list_t		epi_req_waitq;
	int64_t			epi_req_wait_num;

	unsigned int		epi_ref;
	unsigned int		epi_initialized:1;

	/* mutex to protect ei_req_q and some counters */
	pthread_mutex_t		epi_mutex;
};

#define CRT_UNLOCK			(0)
#define CRT_LOCKED			(1)
#define CRT_ADDR_STR_MAX_LEN		(128)

#define CRT_OPC_MAP_BITS	(12)

/* opcode map (hash list) */
struct crt_opc_map {
	pthread_rwlock_t	dom_rwlock;
	unsigned int		dom_lock_init:1;
	unsigned int		dom_pid;
	unsigned int		dom_bits;
	crt_list_t		*dom_hash;
};

struct crt_opc_info {
	crt_list_t		doi_link;
	crt_opcode_t		doi_opc;
	unsigned int		doi_proc_init:1,
				doi_rpccb_init:1,
				doi_coops_init:1;

	crt_rpc_cb_t		doi_rpc_cb;
	struct crt_corpc_ops	*doi_co_ops;
	crt_size_t		doi_input_size;
	crt_size_t		doi_output_size;
	struct crt_req_format	*doi_drf;
};

#endif /* __CRT_INTERNAL_TYPES_H__ */
