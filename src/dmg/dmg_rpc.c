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
 * DMG RPC Protocol Serialization Functions
 */
#include <daos/rpc.h>
#include "dmg_rpc.h"

struct dtp_msg_field *dmg_pool_create_in_fields[] = {
	&DMF_UUID,		/* pc_uuid */
	&DMF_STRING,		/* pc_grp */
	&DMF_STRING,		/* pc_tgt_dev */
	&DMF_UINT32,		/* pc_mode */
	&DMF_RANK_LIST,		/* pc_tgts */
	&DMF_DAOS_SIZE,		/* pc_tgt_size */
	&DMF_RANK_LIST		/* pc_svc */
};

struct dtp_msg_field *dmg_pool_create_out_fields[] = {
	&DMF_INT,		/* pc_rc */
	&DMF_RANK_LIST		/* pc_svc */
};

struct dtp_msg_field *dmg_tgt_create_in_fields[] = {
	&DMF_UUID,		/* tc_pool_uuid */
	&DMF_STRING,		/* tc_tgt_dev */
	&DMF_DAOS_SIZE		/* tc_tgt_size */
};

struct dtp_msg_field *dmg_tgt_create_out_fields[] = {
	&DMF_INT		/* tc_rc */
};

struct dtp_msg_field *dmg_tgt_destroy_in_fields[] = {
	&DMF_UUID		/* td_pool_uuid */
};

struct dtp_msg_field *dmg_tgt_destroy_out_fields[] = {
	&DMF_INT		/* td_rc */
};

struct dtp_req_format DQF_DMG_POOL_CREATE =
	DEFINE_DTP_REQ_FMT("DMG_POOL_CREATE", dmg_pool_create_in_fields,
			   dmg_pool_create_out_fields);

struct dtp_req_format DQF_DMG_TGT_CREATE =
	DEFINE_DTP_REQ_FMT("DMG_TGT_CREATE", dmg_tgt_create_in_fields,
			   dmg_tgt_create_out_fields);

struct dtp_req_format DQF_DMG_TGT_DESTROY =
	DEFINE_DTP_REQ_FMT("DMG_TGT_DESTROY", dmg_tgt_destroy_in_fields,
			   dmg_tgt_destroy_out_fields);

struct daos_rpc dmg_rpcs[] = {
	{
		.dr_name	= "DMG_POOL_CREATE",
		.dr_opc		= DMG_POOL_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DMG_POOL_CREATE,
	}, {
		.dr_name	= "DMG_TGT_CREATE",
		.dr_opc		= DMG_TGT_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DMG_TGT_CREATE,
	}, {
		.dr_name	= "DMG_TGT_DESTROY",
		.dr_opc		= DMG_TGT_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DMG_TGT_DESTROY,
	}, {
		.dr_opc		= 0
	}
};
