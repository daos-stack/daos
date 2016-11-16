/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include <crt_util/heap.h>

#include <crt_hg.h>

struct crt_hg_gdata;
struct crt_grp_gdata;

/* CaRT global data */
struct crt_gdata {
	crt_phy_addr_t		cg_addr;
	uint32_t		cg_addr_len;

	bool			cg_server;
	bool			cg_singleton; /* true for singleton client */
	bool			cg_verbs; /* CCI verbs transport flag */
	/* multiple NA addr flag, true for server when using CCI plugin */
	bool			cg_multi_na;

	/* CaRT contexts list */
	crt_list_t		cg_ctx_list;
	/* actual number of items in CaRT contexts list */
	int			cg_ctx_num;
	/* the global opcode map */
	struct crt_opc_map	*cg_opc_map;
	/* HG level global data */
	struct crt_hg_gdata	*cg_hg;

	struct crt_grp_gdata	*cg_grp;

	/* refcount to protect crt_init/crt_finalize */
	volatile unsigned int	cg_refcount;
	volatile unsigned int	cg_inited:1,
				cg_grp_inited:1; /* group initialized */

	/* protects crt_gdata */
	pthread_rwlock_t	cg_rwlock;
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
	crt_list_t		 cc_link; /* link to gdata.cg_ctx_list */
	int			 cc_idx; /* context index */
	struct crt_hg_context	 cc_hg_ctx; /* HG context */
	void			*cc_pool; /* pool for ES on server stack */
	/* in-flight endpoint tracking hash table */
	struct chash_table	 cc_epi_table;
	/* binheap for inflight RPC timeout tracking */
	struct crt_binheap	 cc_bh_timeout;
	/* mutex to protect cc_epi_table */
	pthread_mutex_t		 cc_mutex;
};

/* in-flight RPC req list, be tracked per endpoint for every crt_context */
struct crt_ep_inflight {
	/* link to crt_context::cc_epi_table */
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
	pthread_rwlock_t	com_rwlock;
	unsigned int		com_lock_init:1;
	unsigned int		com_pid;
	unsigned int		com_bits;
	crt_list_t		*com_hash;
};

struct crt_opc_info {
	crt_list_t		coi_link;
	crt_opcode_t		coi_opc;
	unsigned int		coi_proc_init:1,
				coi_rpccb_init:1,
				coi_coops_init:1;

	crt_rpc_cb_t		coi_rpc_cb;
	struct crt_corpc_ops	*coi_co_ops;
	crt_size_t		coi_input_size;
	crt_size_t		coi_output_size;
	struct crt_req_format	*coi_crf;
};

#endif /* __CRT_INTERNAL_TYPES_H__ */
