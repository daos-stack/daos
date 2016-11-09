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
/*
 * DMG RPC Protocol Serialization Functions
 */

#include <daos/rpc.h>
#include "dmg_rpc.h"

struct crt_msg_field *dmg_pool_create_in_fields[] = {
	&CMF_UUID,		/* pc_pool_uuid */
	&CMF_STRING,		/* pc_grp */
	&CMF_STRING,		/* pc_tgt_dev */
	&CMF_RANK_LIST,		/* pc_tgts */
	&DMF_DAOS_SIZE,		/* pc_tgt_size */
	&CMF_UINT32,		/* pc_svc_nr */
	&CMF_UINT32,		/* pc_mode */
	&CMF_UINT32,		/* pc_uid */
	&CMF_UINT32,		/* pc_gid */
};

struct crt_msg_field *dmg_pool_create_out_fields[] = {
	&CMF_RANK_LIST,		/* pc_svc */
	&CMF_INT,		/* pc_rc */
};

struct crt_msg_field *dmg_pool_destroy_in_fields[] = {
	&CMF_UUID,		/* pd_pool_uuid */
	&CMF_STRING,		/* pd_grp */
	&CMF_INT		/* pd_force */
};

struct crt_msg_field *dmg_pool_destroy_out_fields[] = {
	&CMF_INT		/* pd_rc */
};

struct crt_msg_field *dmg_tgt_create_in_fields[] = {
	&CMF_UUID,		/* tc_pool_uuid */
	&CMF_STRING,		/* tc_tgt_dev */
	&DMF_DAOS_SIZE		/* tc_tgt_size */
};

struct crt_msg_field *dmg_tgt_create_out_fields[] = {
	&CMF_INT,		/* tc_rc */
	&CMF_UUID,		/* tc_tgt_uuid */
};

struct crt_msg_field *dmg_tgt_destroy_in_fields[] = {
	&CMF_UUID		/* td_pool_uuid */
};

struct crt_msg_field *dmg_tgt_destroy_out_fields[] = {
	&CMF_INT		/* td_rc */
};

struct crt_req_format DQF_DMG_POOL_CREATE =
	DEFINE_CRT_REQ_FMT("DMG_POOL_CREATE", dmg_pool_create_in_fields,
			   dmg_pool_create_out_fields);

struct crt_req_format DQF_DMG_POOL_DESTROY =
	DEFINE_CRT_REQ_FMT("DMG_POOL_DESTROY", dmg_pool_destroy_in_fields,
			   dmg_pool_destroy_out_fields);

struct crt_req_format DQF_DMG_TGT_CREATE =
	DEFINE_CRT_REQ_FMT("DMG_TGT_CREATE", dmg_tgt_create_in_fields,
			   dmg_tgt_create_out_fields);

struct crt_req_format DQF_DMG_TGT_DESTROY =
	DEFINE_CRT_REQ_FMT("DMG_TGT_DESTROY", dmg_tgt_destroy_in_fields,
			   dmg_tgt_destroy_out_fields);

struct daos_rpc dmg_rpcs[] = {
	{
		.dr_name	= "DMG_POOL_CREATE",
		.dr_opc		= DMG_POOL_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DMG_POOL_CREATE,
	}, {
		.dr_name	= "DMG_POOL_DESTROY",
		.dr_opc		= DMG_POOL_DESTROY,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_DMG_POOL_DESTROY,
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
