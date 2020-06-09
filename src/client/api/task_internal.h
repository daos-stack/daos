/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

/* size of daos_task_args should within limitation of TSE_TASK_ARG_LEN */
struct daos_task_args {
	uint32_t			ta_magic;
	uint32_t			ta_opc;
	union {
		/** Management */
		daos_svc_rip_t		svc_rip;
		daos_pool_create_t	pool_create;
		daos_pool_destroy_t	pool_destroy;
		daos_pool_extend_t	pool_extend;
		daos_pool_evict_t	pool_evict;
		daos_set_params_t	mgmt_set_params;
		daos_pool_replicas_t	pool_add_replicas;
		daos_pool_replicas_t	pool_remove_replicas;
		daos_mgmt_list_pools_t	mgmt_list_pools;

		/** Pool */
		daos_pool_connect_t	pool_connect;
		daos_pool_disconnect_t	pool_disconnect;
		daos_pool_update_t	pool_update;
		daos_pool_query_t	pool_query;
		daos_pool_query_target_t pool_query_tgt;
		daos_pool_list_attr_t	pool_list_attr;
		daos_pool_get_attr_t	pool_get_attr;
		daos_pool_set_attr_t	pool_set_attr;
		daos_pool_stop_svc_t	pool_stop_svc;

		/** Container */
		daos_cont_create_t	cont_create;
		daos_cont_open_t	cont_open;
		daos_cont_close_t	cont_close;
		daos_cont_destroy_t	cont_destory;
		daos_cont_query_t	cont_query;
		daos_cont_aggregate_t	cont_aggregate;
		daos_cont_rollback_t	cont_rollback;
		daos_cont_subscribe_t	cont_subscribe;
		daos_cont_list_attr_t	cont_list_attr;
		daos_cont_get_attr_t	cont_get_attr;
		daos_cont_set_attr_t	cont_set_attr;
		daos_cont_alloc_oids_t	cont_alloc_oids;
		daos_cont_list_snap_t	cont_list_snap;
		daos_cont_create_snap_t	cont_create_snap;
		daos_cont_destroy_snap_t cont_destroy_snap;

		/** Transaction */
		daos_tx_open_t		tx_open;
		daos_tx_commit_t	tx_commit;
		daos_tx_abort_t		tx_abort;
		daos_tx_close_t		tx_close;
		daos_tx_restart_t	tx_restart;

		/** Object */
		daos_obj_register_class_t obj_reg_class;
		daos_obj_query_class_t	obj_query_class;
		daos_obj_list_class_t	obj_list_class;
		daos_obj_open_t		obj_open;
		daos_obj_close_t	obj_close;
		daos_obj_punch_t	obj_punch;
		daos_obj_query_t	obj_query;
		daos_obj_query_key_t	obj_query_key;
		struct daos_obj_sync_args obj_sync;
		struct daos_obj_fetch_shard obj_fetch_shard;
		daos_obj_fetch_t	obj_fetch;
		daos_obj_update_t	obj_update;
		daos_obj_list_dkey_t	obj_list_dkey;
		daos_obj_list_akey_t	obj_list_akey;
		daos_obj_list_recx_t	obj_list_recx;
		daos_obj_list_obj_t	obj_list_obj;

		/** Array */
		daos_array_create_t	array_create;
		daos_array_open_t	array_open;
		daos_array_close_t	array_close;
		daos_array_destroy_t	array_destroy;
		daos_array_io_t		array_io;
		daos_array_get_size_t	array_get_size;
		daos_array_set_size_t	array_set_size;

		/** HL */
		daos_kv_get_t		kv_get;
		daos_kv_put_t		kv_put;
		daos_kv_remove_t	kv_remove;
		daos_kv_list_t		kv_list;
	}		 ta_u;
	daos_event_t	*ta_ev;
};

#endif /* __DAOS_TASK_INTERNAL_H__ */
