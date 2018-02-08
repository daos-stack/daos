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

#define DAOS_TASK_MAGIC			0xbabeface

struct daos_task_args {
	uint32_t			ta_magic;
	uint32_t			ta_padding;
	union {
		daos_svc_rip_t		svc_rip;
		daos_pool_create_t	pool_create;
		daos_pool_destroy_t	pool_destory;
		daos_pool_extend_t	pool_extend;
		daos_pool_evict_t	pool_evict;
		daos_pool_connect_t	pool_connect;
		daos_pool_disconnect_t	pool_disconnect;
		daos_pool_update_t	pool_update;
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
		daos_array_create_t	array_create;
		daos_array_open_t	array_open;
		daos_array_close_t	array_close;
		daos_array_io_t		array_io;
		daos_array_get_size_t	array_get_size;
		daos_array_set_size_t	array_set_size;
		daos_kv_get_t		obj_get;
		daos_kv_put_t		obj_put;
		daos_kv_remove_t	obj_remove;
		daos_obj_multi_io_t	obj_fetch_multi;
		daos_obj_multi_io_t	obj_update_multi;
	}		 ta_u;
	daos_event_t	*ta_ev;
};

struct daos_task_api {
	tse_task_func_t		task_func;
	daos_size_t		arg_size;
};

extern const struct daos_task_api dc_funcs[DAOS_OPC_MAX];

#endif /* __DAOS_TASK_INTERNAL_H__ */
