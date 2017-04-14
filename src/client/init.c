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
 * DAOS Client initialization/shutdown routines
 */
#define DD_SUBSYS	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/tier.h>
#include "task_internal.h"
#include <pthread.h>

/* Shared by pool, container, and object handles. */
struct daos_hhash *daos_client_hhash;

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static bool		module_initialized;

const struct daos_task_api dc_funcs[] = {
	{dc_mgmt_svc_rip, sizeof(daos_svc_rip_t)},
	{dc_pool_create, sizeof(daos_pool_create_t)},
	{dc_pool_destroy, sizeof(daos_pool_destroy_t)},
	{dc_pool_extend, sizeof(daos_pool_extend_t)},
	{dc_pool_evict, sizeof(daos_pool_evict_t)},
	{dc_pool_connect, sizeof(daos_pool_connect_t)},
	{dc_pool_disconnect, sizeof(daos_pool_disconnect_t)},
	{dc_pool_exclude, sizeof(daos_pool_update_t)},
	{dc_pool_exclude_out, sizeof(daos_pool_update_t)},
	{dc_pool_add, sizeof(daos_pool_update_t)},
	{dc_pool_query, sizeof(daos_pool_query_t)},
	{dc_pool_target_query, sizeof(daos_pool_target_query_t)},
	{dc_cont_create, sizeof(daos_cont_create_t)},
	{dc_cont_open, sizeof(daos_cont_open_t)},
	{dc_cont_close, sizeof(daos_cont_close_t)},
	{dc_cont_destroy, sizeof(daos_cont_destroy_t)},
	{dc_cont_query, sizeof(daos_cont_query_t)},
	{dc_cont_attr_list, sizeof(daos_cont_attr_list_t)},
	{dc_cont_attr_get, sizeof(daos_cont_attr_get_t)},
	{dc_cont_attr_set, sizeof(daos_cont_attr_set_t)},
	{dc_epoch_flush, sizeof(daos_epoch_flush_t)},
	{dc_epoch_discard, sizeof(daos_epoch_discard_t)},
	{dc_epoch_query, sizeof(daos_epoch_query_t)},
	{dc_epoch_hold, sizeof(daos_epoch_hold_t)},
	{dc_epoch_slip, sizeof(daos_epoch_slip_t)},
	{dc_epoch_commit, sizeof(daos_epoch_commit_t)},
	{dc_epoch_wait, sizeof(daos_epoch_wait_t)},
	{dc_snap_list, sizeof(daos_snap_list_t)},
	{dc_snap_create, sizeof(daos_snap_create_t)},
	{dc_snap_destroy, sizeof(daos_snap_destroy_t)},
	{dc_obj_class_register, sizeof(daos_obj_class_register_t)},
	{dc_obj_class_query, sizeof(daos_obj_class_query_t)},
	{dc_obj_class_list, sizeof(daos_obj_class_list_t)},
	{dc_obj_declare, sizeof(daos_obj_declare_t)},
	{dc_obj_open, sizeof(daos_obj_open_t)},
	{dc_obj_close, sizeof(daos_obj_close_t)},
	{dc_obj_punch, sizeof(daos_obj_punch_t)},
	{dc_obj_query, sizeof(daos_obj_query_t)},
	{dc_obj_fetch, sizeof(daos_obj_fetch_t)},
	{dc_obj_update, sizeof(daos_obj_update_t)},
	{dc_obj_list_dkey, sizeof(daos_obj_list_dkey_t)},
	{dc_obj_list_akey, sizeof(daos_obj_list_akey_t)},
	{dc_obj_list_rec, sizeof(daos_obj_list_recx_t)},
	{dc_obj_single_shard_list_dkey, sizeof(daos_obj_list_dkey_t)}
};

/**
 * Initialize DAOS client library.
 */
int
daos_init(void)
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		D_GOTO(unlock, rc);

	rc = daos_hhash_create(DAOS_HHASH_BITS, &daos_client_hhash);
	if (rc != 0) {
		D_ERROR("failed to create handle hash table: %d\n", rc);
		D_GOTO(out_debug, rc);
	}

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

	/** set up tiering */
	rc = dc_tier_init();
	if (rc != 0)
		D_GOTO(out_obj, rc);

	module_initialized = true;
	D_GOTO(unlock, rc = 0);

out_obj:
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
	daos_hhash_destroy(daos_client_hhash);
out_debug:
	daos_debug_fini();
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}

/**
 * Turn down DAOS client library
 */
int
daos_fini(void)
{
	int	rc;

	pthread_mutex_lock(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	dc_tier_fini();
	dc_obj_fini();
	dc_cont_fini();
	dc_pool_fini();
	dc_mgmt_fini();

	daos_hhash_destroy(daos_client_hhash);
	daos_client_hhash = NULL;

	daos_debug_fini();
	module_initialized = false;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}
