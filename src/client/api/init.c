/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * DAOS Client initialization/shutdown routines
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/task.h>
#include <daos/addons.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include "task_internal.h"
#include <pthread.h>

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static bool		module_initialized;

const struct daos_task_api dc_funcs[] = {
	{dc_mgmt_svc_rip, sizeof(daos_svc_rip_t)},
	{dc_pool_create, sizeof(daos_pool_create_t)},
	{dc_pool_destroy, sizeof(daos_pool_destroy_t)},
	{dc_pool_extend, sizeof(daos_pool_extend_t)},
	{dc_pool_evict, sizeof(daos_pool_evict_t)},
	{dc_mgmt_set_params, sizeof(daos_set_params_t)},
	{dc_pool_add_replicas, sizeof(daos_pool_replicas_t)},
	{dc_pool_remove_replicas, sizeof(daos_pool_replicas_t)},
	{dc_pool_connect, sizeof(daos_pool_connect_t)},
	{dc_pool_disconnect, sizeof(daos_pool_disconnect_t)},
	{dc_pool_exclude, sizeof(daos_pool_update_t)},
	{dc_pool_exclude_out, sizeof(daos_pool_update_t)},
	{dc_pool_add, sizeof(daos_pool_update_t)},
	{dc_pool_query, sizeof(daos_pool_query_t)},
	{dc_pool_query_target, sizeof(daos_pool_query_target_t)},
	{dc_pool_list_attr, sizeof(daos_pool_list_attr_t)},
	{dc_pool_get_attr, sizeof(daos_pool_get_attr_t)},
	{dc_pool_set_attr, sizeof(daos_pool_set_attr_t)},
	{dc_pool_stop_svc, sizeof(daos_pool_stop_svc_t)},
	{dc_cont_create, sizeof(daos_cont_create_t)},
	{dc_cont_open, sizeof(daos_cont_open_t)},
	{dc_cont_close, sizeof(daos_cont_close_t)},
	{dc_cont_destroy, sizeof(daos_cont_destroy_t)},
	{dc_cont_query, sizeof(daos_cont_query_t)},
	{dc_cont_rollback, sizeof(daos_cont_rollback_t)},
	{dc_cont_subscribe, sizeof(daos_cont_subscribe_t)},
	{dc_cont_list_attr, sizeof(daos_cont_list_attr_t)},
	{dc_cont_get_attr, sizeof(daos_cont_get_attr_t)},
	{dc_cont_set_attr, sizeof(daos_cont_set_attr_t)},
	{dc_cont_alloc_oids, sizeof(daos_cont_alloc_oids_t)},
	{dc_cont_list_snap, sizeof(daos_cont_list_snap_t)},
	{dc_cont_create_snap, sizeof(daos_cont_create_snap_t)},
	{dc_cont_destroy_snap, sizeof(daos_cont_destroy_snap_t)},
	{dc_tx_open, sizeof(daos_tx_open_t)},
	{dc_tx_commit, sizeof(daos_tx_commit_t)},
	{dc_tx_abort, sizeof(daos_tx_abort_t)},
	{dc_tx_open_snap, sizeof(daos_tx_open_snap_t)},
	{dc_tx_close, sizeof(daos_tx_close_t)},
	{dc_obj_register_class, sizeof(daos_obj_register_class_t)},
	{dc_obj_query_class, sizeof(daos_obj_query_class_t)},
	{dc_obj_list_class, sizeof(daos_obj_list_class_t)},
	{dc_obj_open, sizeof(daos_obj_open_t)},
	{dc_obj_close, sizeof(daos_obj_close_t)},
	{dc_obj_punch, sizeof(daos_obj_punch_t)},
	{dc_obj_punch_dkeys, sizeof(daos_obj_punch_t)},
	{dc_obj_punch_akeys, sizeof(daos_obj_punch_t)},
	{dc_obj_query, sizeof(daos_obj_query_t)},
	{dc_obj_query_key, sizeof(daos_obj_query_key_t)},
	{dc_obj_fetch, sizeof(daos_obj_fetch_t)},
	{dc_obj_update, sizeof(daos_obj_update_t)},
	{dc_obj_list_dkey, sizeof(daos_obj_list_dkey_t)},
	{dc_obj_list_akey, sizeof(daos_obj_list_akey_t)},
	{dc_obj_list_rec, sizeof(daos_obj_list_recx_t)},
	{dac_array_create, sizeof(daos_array_create_t)},
	{dac_array_open, sizeof(daos_array_open_t)},
	{dac_array_close, sizeof(daos_array_close_t)},
	{dac_array_read, sizeof(daos_array_io_t)},
	{dac_array_write, sizeof(daos_array_io_t)},
	{dac_array_get_size, sizeof(daos_array_get_size_t)},
	{dac_array_set_size, sizeof(daos_array_set_size_t)},
	{dac_kv_get, sizeof(daos_kv_get_t)},
	{dac_kv_put, sizeof(daos_kv_put_t)},
	{dac_kv_remove, sizeof(daos_kv_remove_t)},
	{dac_kv_list, sizeof(daos_kv_list_t)},
	{dac_obj_fetch_multi, sizeof(daos_obj_multi_io_t)},
	{dac_obj_update_multi, sizeof(daos_obj_multi_io_t)},
};

/**
 * Initialize DAOS client library.
 */
int
daos_init(void)
{
	int rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		D_GOTO(unlock, rc);

	rc = dbtree_class_register(DBTREE_CLASS_RECX, BTR_FEAT_DIRECT_KEY,
				   &dbtree_recx_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		D_ERROR("failed to register DBTREE_CLASS_RECX: %d\n", rc);
		D_GOTO(out_debug, rc);
	}

	/** set up handle hash-table */
	rc = daos_hhash_init();
	if (rc != 0)
		D_GOTO(out_debug, rc);

	/** set up event queue */
	rc = daos_eq_lib_init();
	if (rc != 0) {
		D_ERROR("failed to initialize eq_lib: %d\n", rc);
		D_GOTO(out_hhash, rc);
	}

	/** set up management interface */
	rc = dc_mgmt_init();
	if (rc != 0)
		D_GOTO(out_eq, rc);

	/** set up pool */
	rc = dc_pool_init();
	if (rc != 0)
		D_GOTO(out_mgmt, rc);

	/** set up container */
	rc = dc_cont_init();
	if (rc != 0)
		D_GOTO(out_pool, rc);

	/** set up object */
	rc = dc_obj_init();
	if (rc != 0)
		D_GOTO(out_co, rc);

	module_initialized = true;
	D_GOTO(unlock, rc = 0);

	dc_obj_fini();
out_co:
	dc_cont_fini();
out_pool:
	dc_pool_fini();
out_mgmt:
	dc_mgmt_fini();
out_eq:
	daos_eq_lib_fini();
out_hhash:
	daos_hhash_fini();
out_debug:
	daos_debug_fini();
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}

/**
 * Turn down DAOS client library
 */
int
daos_fini(void)
{
	int	rc;

	D_MUTEX_LOCK(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	dc_obj_fini();
	dc_cont_fini();
	dc_pool_fini();
	dc_mgmt_fini();

	daos_hhash_fini();
	daos_debug_fini();
	module_initialized = false;
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}
