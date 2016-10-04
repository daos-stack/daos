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
 * This file is part of daos_transport. It gives out the data types internally
 * used by dtp and not in other specific header files.
 */

#ifndef __DTP_INTERNAL_TYPES_H__
#define __DTP_INTERNAL_TYPES_H__

#define DTP_CONTEXT_NULL         (NULL)
#define DTP_BULK_NULL            (NULL)

#include <pthread.h>

#include <daos/list.h>
#include <daos/hash.h>
#include <process_set.h>

#include <dtp_hg.h>

struct dtp_hg_gdata;

/* dtp global data */
struct dtp_gdata {
	dtp_phy_addr_t		dg_addr;
	uint32_t		dg_addr_len;

	bool			dg_server;
	bool			dg_verbs; /* CCI verbs transport flag */
	/* multiple NA addr flag, true for server when using CCI plugin */
	bool			dg_multi_na;

	/* dtp contexts list */
	daos_list_t             dg_ctx_list;
	/* actual number of items in dtp contexts list */
	int			dg_ctx_num;
	/* the global opcode map */
	struct dtp_opc_map	*dg_opc_map;
	/* HG level global data */
	struct dtp_hg_gdata	*dg_hg;

	struct mcl_state	*dg_mcl_state;
	/* service process set */
	struct mcl_set		*dg_mcl_srv_set;
	/* client process set */
	struct mcl_set		*dg_mcl_cli_set;

	/* the unique global server and client group ID */
	dtp_group_id_t		dg_srv_grp_id;
	dtp_group_id_t		dg_cli_grp_id;

	/* protects dtp_gdata */
	pthread_rwlock_t	dg_rwlock;
	/* refcount to protect dtp_init/dtp_finalize */
	volatile unsigned int	dg_refcount;
	volatile unsigned int	dg_inited;
	/* ... */
};

extern struct dtp_gdata		dtp_gdata;

enum {
	DTP_GRP_CREATING = 0x66,
	DTP_GRP_NORMAL,
	DTP_GRP_DESTROYING,
};

struct dtp_grp_priv {
	daos_list_t		 gp_link; /* link to dtp_grp_list */
	dtp_group_t		 gp_pub; /* public grp handle */
	dtp_rank_list_t	*gp_membs; /* member ranks in global group */
	/* the priv pointer user passed in for dtp_group_create */
	void			*gp_priv;
	/* dtp context only for sending grp create/destroy RPCs */
	dtp_context_t		 gp_ctx;
	int			 gp_status; /* group status */

	/* TODO: reuse dtp_corpc_info here */
	/*
	 * Some temporary info used for group creating/destroying, valid when
	 * gp_status is DTP_GRP_CREATING or DTP_GRP_DESTROYING.
	 */
	struct dtp_rpc_priv	*gp_parent_rpc; /* parent RPC, NULL on root */
	daos_list_t		 gp_child_rpcs; /* child RPCs list */
	uint32_t		 gp_child_num;
	uint32_t		 gp_child_ack_num;
	int			 gp_rc; /* temporary recoded return code */
	dtp_rank_list_t	*gp_failed_ranks; /* failed ranks */

	dtp_grp_create_cb_t	 gp_create_cb; /* grp create completion cb */
	dtp_grp_destroy_cb_t	 gp_destroy_cb; /* grp destroy completion cb */
	void			*gp_destroy_cb_arg;

	pthread_mutex_t		 gp_mutex; /* protect all fields above */
};

/* TODO may use a RPC to query server-side context number */
#ifndef DTP_SRV_CONTEX_NUM
# define DTP_SRV_CONTEX_NUM		(256)
#endif

/* (1 << DTP_EPI_TABLE_BITS) is the number of buckets of epi hash table */
#define DTP_EPI_TABLE_BITS		(3)
#define DTP_MAX_INFLIGHT_PER_EP_CTX	(32)

/* dtp_context */
struct dtp_context {
	daos_list_t		 dc_link; /* link to gdata.dg_ctx_list */
	int			 dc_idx; /* context index */
	struct dtp_hg_context	 dc_hg_ctx; /* HG context */
	void			*dc_pool; /* pool for ES on server stack */
	/* in-flight endpoint tracking hash table */
	struct dhash_table	 dc_epi_table;
	/* mutex to protect dc_epi_table */
	pthread_mutex_t		 dc_mutex;
};

/* in-flight RPC req list, be tracked per endpoint for every dtp_context */
struct dtp_ep_inflight {
	/* link to dtp_context::dc_epi_table */
	daos_list_t		epi_link;
	/* endpoint address */
	dtp_endpoint_t		epi_ep;
	struct dtp_context	*epi_ctx;

	/* in-flight RPC req queue */
	daos_list_t		epi_req_q;
	/* (ei_req_num - ei_reply_num) is the number of inflight req */
	int64_t			epi_req_num; /* total number of req send */
	int64_t			epi_reply_num; /* total number of reply recv */
	/* RPC req wait queue */
	daos_list_t		epi_req_waitq;
	int64_t			epi_req_wait_num;

	unsigned int		epi_ref;
	unsigned int		epi_initialized:1;

	/* mutex to protect ei_req_q and some counters */
	pthread_mutex_t		epi_mutex;
};

#define DTP_UNLOCK		(0)
#define DTP_LOCKED		(1)

/* TODO export the group name to user? and multiple client groups? */
#define DTP_GLOBAL_SRV_GROUP_NAME	"dtp_global_srv_group"
#define DTP_CLI_GROUP_NAME		"dtp_cli_group"
#define DTP_ADDR_STR_MAX_LEN		(128)

#define DTP_OPC_MAP_BITS	(12)

/* opcode map (hash list) */
struct dtp_opc_map {
	pthread_rwlock_t	dom_rwlock;
	unsigned int		dom_lock_init:1;
	unsigned int		dom_pid;
	unsigned int		dom_bits;
	daos_list_t		*dom_hash;
};

struct dtp_opc_info {
	daos_list_t		doi_link;
	dtp_opcode_t		doi_opc;
	unsigned int		doi_proc_init:1,
				doi_rpccb_init:1,
				doi_coops_init:1;

	dtp_rpc_cb_t		doi_rpc_cb;
	struct dtp_corpc_ops	*doi_co_ops;
	dtp_size_t		doi_input_size;
	dtp_size_t		doi_output_size;
	struct dtp_req_format	*doi_drf;
};

#endif /* __DTP_INTERNAL_TYPES_H__ */
