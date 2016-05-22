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
/*
 * dmg: RPC Protocol Definitions
 *
 * This is naturally shared by both dmgc and dmgss.
 */

#ifndef __DMG_RPC_H__
#define __DMG_RPC_H__

#include <daos/transport.h>
#include <daos/rpc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * dtp_req_create(..., opc, ...). See daos_rpc.h.
 */
enum dmg_operation {
	DMG_POOL_CREATE		= 1,
	DMG_POOL_DESTROY	= 2,
	DMG_POOL_EXTEND		= 3,
	DMG_TGT_CREATE		= 4,
	DMG_TGT_DESTROY		= 5,
	DMG_TGT_EXTEND		= 6,
};

struct dmg_pool_create_in {
	uuid_t			 pc_pool_uuid;
	dtp_string_t		 pc_grp;
	dtp_string_t		 pc_tgt_dev;
	daos_rank_list_t	*pc_tgts;
	daos_size_t		 pc_tgt_size;
	uint32_t		 pc_svc_nr;
	uint32_t		 pc_mode;
	uint32_t		 pc_uid;
	uint32_t		 pc_gid;
};

struct dmg_pool_create_out {
	daos_rank_list_t	*pc_svc;
	int			 pc_rc;
};

struct dmg_pool_destroy_in {
	uuid_t			pd_pool_uuid;
	dtp_string_t		pd_grp;
	int			pd_force;
};

struct dmg_pool_destroy_out {
	int			pd_rc;
};

struct dmg_tgt_create_in {
	uuid_t			tc_pool_uuid;
	dtp_string_t		tc_tgt_dev;
	daos_size_t		tc_tgt_size;
};

struct dmg_tgt_create_out {
	int			tc_rc;
	uuid_t			tc_tgt_uuid;
};

struct dmg_tgt_destroy_in {
	uuid_t			td_pool_uuid;
};

struct dmg_tgt_destroy_out {
	int			td_rc;
};



extern struct daos_rpc dmg_rpcs[];

#endif /* __DMG_RPC_H__ */
