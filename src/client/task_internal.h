/**
 * (C) Copyright 2017 Intel Corporation.
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
 * Internal task function pointers
 */

#ifndef __DAOS_TASK_INTERNAL_H__
#define  __DAOS_TASK_INTERNAL_H__

#include <daos_task.h>

struct daos_task_args {
	daos_opc_t opc;
	union {
		daos_svc_rip_t		svc_rip;
		daos_pool_create_t	pool_create;
		daos_pool_destroy_t	pool_destory;
		daos_pool_extend_t	pool_extend;
		daos_pool_evict_t	pool_evict;
		daos_pool_connect_t	pool_connect;
		daos_pool_disconnect_t	pool_disconnect;
		daos_pool_exclude_t	pool_exclude;
		daos_pool_query_t	pool_query;
		daos_pool_target_query_t pool_tgt_query;
		daos_cont_create_t	cont_create;
		daos_cont_open_t	cont_open;
		daos_cont_close_t	cont_close;
		daos_cont_destroy_t	cont_destory;
		daos_cont_query_t	cont_query;
		daos_cont_attr_list_t	cont_attr_list;
		daos_cont_attr_get_t	cont_attr_get;
		daos_cont_attr_set_t	cont_attr_set;
		daos_epoch_flush_t	epoch_flush;
		daos_epoch_discard_t	epoch_discard;
		daos_epoch_query_t	epoch_query;
		daos_epoch_hold_t	epoch_hold;
		daos_epoch_slip_t	epoch_slip;
		daos_epoch_commit_t	epoch_commit;
		daos_epoch_wait_t	epoch_wait;
		daos_snap_list_t	snap_list;
		daos_snap_create_t	snap_create;
		daos_snap_destroy_t	snap_destroy;
		daos_obj_class_register_t obj_class_reg;
		daos_obj_class_query_t	obj_class_query;
		daos_obj_class_list_t	obj_class_list;
		daos_obj_declare_t	obj_declare;
		daos_obj_open_t		obj_open;
		daos_obj_close_t	obj_close;
		daos_obj_punch_t	obj_punch;
		daos_obj_query_t	obj_query;
		daos_obj_fetch_t	obj_fetch;
		daos_obj_update_t	obj_update;
		daos_obj_list_dkey_t	obj_list_dkey;
		daos_obj_list_akey_t	obj_list_akey;
		daos_obj_list_recx_t	obj_list_recx;
	} op_args;
};

struct daos_task_api {
	daos_task_func_t	task_func;
	daos_size_t		arg_size;
};

extern const struct daos_task_api dc_funcs[DAOS_OPC_MAX];

#endif /* __DAOS_TASK_INTERNAL_H__ */
