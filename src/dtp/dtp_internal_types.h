/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport. It gives out the main data type
 * definitions of dtp.
 */

#ifndef __DTP_INTERNAL_TYPES_H__
#define __DTP_INTERNAL_TYPES_H__

#define DTP_CONTEXT_MAX_NUM      (32)
#define DTP_CONTEXT_NULL         (NULL)
#define DTP_BULK_NULL            (NULL)

#include <pthread.h>

#include <daos/list.h>
#include <process_set.h>

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
	/* TODO refine grp_id things together with dtp_group_create() */
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

#define DTP_RPC_MAGIC			(0xAB0C01EC)
#define DTP_RPC_VERSION			(0x00000001)

/*
 * TODO need to consider more later together with dtp_group_create()
 * Temporarily just use a global server group ID and a global client group ID.
 */
#define DTP_GLOBAL_SRV_GRPID_STR	"da03c1e7-1618-8899-6699-aabbccddeeff"
#define DTP_GLOBAL_CLI_GRPID_STR	"da033e4e-1618-8899-6699-aabbccddeeff"

/* TODO may use a RPC to query server-side context number */
#ifndef DTP_SRV_CONTEX_NUM
# define DTP_SRV_CONTEX_NUM	(4)
#endif

/* dtp layer common header */
struct dtp_common_hdr {
	uint32_t	dch_magic;
	uint32_t	dch_version;
	uint32_t	dch_opc;
	uint32_t	dch_cksum;
	uint32_t	dch_flags;
	/* gid and rank identify the rpc request sender */
	dtp_group_id_t	dch_grp_id; /* uuid_t 16 bytes */
	daos_rank_t	dch_rank; /* uint32_t */
	uint32_t	dch_padding[2];
};

/* TODO: cannot know the state of RPC_REQ_SENT from mercury */
typedef enum {
	RPC_INITED = 0x36,
	RPC_REQ_SENT,
	RPC_REPLY_RECVED,
	RPC_COMPLETED,
	RPC_CANCELING,
} dtp_rpc_state_t;

#define DTP_OPC_MAP_BITS	(12)

#define DTP_UNLOCK		(0)
#define DTP_LOCKED		(1)

/* TODO export the group name to user? and multiple client groups? */
#define DTP_GLOBAL_SRV_GROUP_NAME	"dtp_global_srv_group"
#define DTP_CLI_GROUP_NAME		"dtp_cli_group"
#define DTP_GROUP_NAME_MAX_LEN		(64)
#define DTP_ADDR_STR_MAX_LEN		(128)

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
				doi_rpc_init:1;

	dtp_rpc_cb_t		doi_rpc_cb;
	daos_size_t             doi_input_size;
	daos_size_t             doi_output_size;
	struct dtp_req_format	*doi_drf;
};

#endif /* __DTP_INTERNAL_TYPES_H__ */
