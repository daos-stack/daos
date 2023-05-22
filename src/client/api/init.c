/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS Client initialization/shutdown routines
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/agent.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/sys_debug.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/task.h>
#include <daos/array.h>
#include <daos/kv.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos/placement.h>
#include <daos/job.h>
#if BUILD_PIPELINE
#include <daos/pipeline.h>
#endif
#include "task_internal.h"
#include <pthread.h>

/** protect against concurrent daos_init/fini calls */
static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;

/** refcount on how many times daos_init has been called */
static int		module_initialized;

const struct daos_task_api dc_funcs[] = {
	/** Management */
	{dc_deprecated, 0},
	{dc_deprecated, 0},
	{dc_deprecated, 0},
	{dc_debug_set_params, sizeof(daos_set_params_t)},
	{dc_mgmt_get_bs_state, sizeof(daos_mgmt_get_bs_state_t)},

	/** Pool */
	{dc_pool_connect, sizeof(daos_pool_connect_t)},
	{dc_pool_disconnect, sizeof(daos_pool_disconnect_t)},
	{dc_pool_exclude, sizeof(daos_pool_update_t)},
	{dc_pool_exclude_out, sizeof(daos_pool_update_t)},
	{dc_pool_reint, sizeof(daos_pool_update_t)},
	{dc_pool_query, sizeof(daos_pool_query_t)},
	{dc_pool_query_target, sizeof(daos_pool_query_target_t)},
	{dc_pool_list_attr, sizeof(daos_pool_list_attr_t)},
	{dc_pool_get_attr, sizeof(daos_pool_get_attr_t)},
	{dc_pool_set_attr, sizeof(daos_pool_set_attr_t)},
	{dc_pool_del_attr, sizeof(daos_pool_del_attr_t)},
	{dc_pool_stop_svc, sizeof(daos_pool_stop_svc_t)},
	{dc_pool_list_cont, sizeof(daos_pool_list_cont_t)},

	/** Container */
	{dc_cont_create, sizeof(daos_cont_create_t)},
	{dc_cont_open, sizeof(daos_cont_open_t)},
	{dc_cont_close, sizeof(daos_cont_close_t)},
	{dc_cont_destroy, sizeof(daos_cont_destroy_t)},
	{dc_cont_query, sizeof(daos_cont_query_t)},
	{dc_cont_set_prop, sizeof(daos_cont_set_prop_t)},
	{dc_cont_update_acl, sizeof(daos_cont_update_acl_t)},
	{dc_cont_delete_acl, sizeof(daos_cont_delete_acl_t)},
	{dc_cont_aggregate, sizeof(daos_cont_aggregate_t)},
	{dc_cont_rollback, sizeof(daos_cont_rollback_t)},
	{dc_cont_subscribe, sizeof(daos_cont_subscribe_t)},
	{dc_cont_list_attr, sizeof(daos_cont_list_attr_t)},
	{dc_cont_get_attr, sizeof(daos_cont_get_attr_t)},
	{dc_cont_set_attr, sizeof(daos_cont_set_attr_t)},
	{dc_cont_del_attr, sizeof(daos_cont_del_attr_t)},
	{dc_cont_alloc_oids, sizeof(daos_cont_alloc_oids_t)},
	{dc_cont_list_snap, sizeof(daos_cont_list_snap_t)},
	{dc_cont_create_snap, sizeof(daos_cont_create_snap_t)},
	{dc_cont_destroy_snap, sizeof(daos_cont_destroy_snap_t)},

	/** Transaction */
	{dc_tx_open, sizeof(daos_tx_open_t)},
	{dc_tx_commit, sizeof(daos_tx_commit_t)},
	{dc_tx_abort, sizeof(daos_tx_abort_t)},
	{dc_tx_open_snap, sizeof(daos_tx_open_snap_t)},
	{dc_tx_close, sizeof(daos_tx_close_t)},
	{dc_tx_restart, sizeof(daos_tx_restart_t)},

	/** Object */
	{dc_obj_register_class, sizeof(struct daos_obj_register_class_t)},
	{dc_obj_query_class, sizeof(daos_obj_query_class_t)},
	{dc_obj_list_class, sizeof(daos_obj_list_class_t)},
	{dc_obj_open, sizeof(daos_obj_open_t)},
	{dc_obj_close, sizeof(daos_obj_close_t)},
	{dc_obj_punch_task, sizeof(daos_obj_punch_t)},
	{dc_obj_punch_dkeys_task, sizeof(daos_obj_punch_t)},
	{dc_obj_punch_akeys_task, sizeof(daos_obj_punch_t)},
	{dc_obj_query, sizeof(daos_obj_query_t)},
	{dc_obj_query_key, sizeof(daos_obj_query_key_t)},
	{dc_obj_sync, sizeof(struct daos_obj_sync_args)},
	{dc_obj_fetch_task, sizeof(daos_obj_fetch_t)},
	{dc_obj_update_task, sizeof(daos_obj_update_t)},
	{dc_obj_list_dkey, sizeof(daos_obj_list_dkey_t)},
	{dc_obj_list_akey, sizeof(daos_obj_list_akey_t)},
	{dc_obj_list_rec, sizeof(daos_obj_list_recx_t)},
	{dc_obj_list_obj, sizeof(daos_obj_list_obj_t)},

	/** Array */
	{dc_array_create, sizeof(daos_array_create_t)},
	{dc_array_open, sizeof(daos_array_open_t)},
	{dc_array_close, sizeof(daos_array_close_t)},
	{dc_array_destroy, sizeof(daos_array_destroy_t)},
	{dc_array_read, sizeof(daos_array_io_t)},
	{dc_array_write, sizeof(daos_array_io_t)},
	{dc_array_punch, sizeof(daos_array_io_t)},
	{dc_array_get_size, sizeof(daos_array_get_size_t)},
	{dc_array_set_size, sizeof(daos_array_set_size_t)},
	{dc_array_stat, sizeof(daos_array_stat_t)},

	/** Key-Value Store */
	{dc_kv_open, sizeof(daos_kv_open_t)},
	{dc_kv_close, sizeof(daos_kv_close_t)},
	{dc_kv_destroy, sizeof(daos_kv_destroy_t)},
	{dc_kv_get, sizeof(daos_kv_get_t)},
	{dc_kv_put, sizeof(daos_kv_put_t)},
	{dc_kv_remove, sizeof(daos_kv_remove_t)},
	{dc_kv_list, sizeof(daos_kv_list_t)},

	{dc_pool_filter_cont, sizeof(daos_pool_filter_cont_t)},
	{dc_obj_key2anchor, sizeof(daos_obj_key2anchor_t)},
	{dc_cont_snap_oit_create, sizeof(daos_cont_snap_oit_create_t)},
	{dc_cont_snap_oit_destroy, sizeof(daos_cont_snap_oit_destroy_t)},

#if BUILD_PIPELINE
	/** Pipeline */
	{dc_pipeline_run, sizeof(daos_pipeline_run_t)},
#endif
};

/**
 * Initialize DAOS client library.
 */
int
daos_init(void)
{
	struct d_fault_attr_t *d_fault_init;
	struct d_fault_attr_t *d_fault_mem = NULL;
	struct d_fault_attr_t d_fault_mem_saved;
	int rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized > 0) {
		/** already initialized, report success */
		module_initialized++;
		D_GOTO(unlock, rc = 0);
	}

	rc = daos_debug_init(NULL);
	if (rc != 0)
		D_GOTO(unlock, rc);

	d_fault_init = d_fault_attr_lookup(101);

	/* If fault injection 101 is set then turn off fault injection 0 for the rest of this
	 * function.  This allows us to only test daos_init() under fault injection for one
	 * test only, then avoid replicating the same effort for other fault injection tests.
	 */
	if (D_SHOULD_FAIL(d_fault_init)) {
		struct d_fault_attr_t blank = {};

		d_fault_mem = d_fault_attr_lookup(0);

		if (d_fault_mem) {
			d_fault_mem_saved = *d_fault_mem;
			d_fault_attr_set(0, blank);
		}
	}

	/**
	 * Set up handle hash-table, use RW lock instead of spinlock
	 * improves multiple threads performance significantly.
	 */
	rc = daos_hhash_init_feats(D_HASH_FT_RWLOCK);
	if (rc != 0)
		D_GOTO(out_debug, rc);

	/** set up agent */
	rc = dc_agent_init();
	if (rc != 0)
		D_GOTO(out_hhash, rc);

	/** set up job info */
	rc = dc_job_init();
	if (rc != 0)
		D_GOTO(out_agent, rc);

	/**
	 * get CaRT configuration (see mgmtModule.handleGetAttachInfo for the
	 * handling of NULL system names)
	 */
	rc = dc_mgmt_net_cfg(NULL);
	if (rc != 0)
		D_GOTO(out_job, rc);

	/** set up event queue */
	rc = daos_eq_lib_init();
	if (rc != 0) {
		D_ERROR("failed to initialize eq_lib: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_job, rc);
	}

	/**
	 * daos_eq_lib_init() might change net cfg, check it.
	 */
	rc = dc_mgmt_net_cfg_check(NULL);
	if (rc != 0)
		D_GOTO(out_eq, rc);

	/** set up placement */
	rc = pl_init();
	if (rc != 0)
		D_GOTO(out_eq, rc);

	/** set up management interface */
	rc = dc_mgmt_init();
	if (rc != 0)
		D_GOTO(out_pl, rc);

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

#if BUILD_PIPELINE
	/** set up pipeline */
	rc = dc_pipeline_init();
	if (rc != 0)
		D_GOTO(out_obj, rc);
#endif
	module_initialized++;
	D_GOTO(unlock, rc = 0);

#if BUILD_PIPELINE
out_obj:
	dc_obj_fini();
#endif
out_co:
	dc_cont_fini();
out_pool:
	dc_pool_fini();
out_mgmt:
	dc_mgmt_fini();
out_pl:
	pl_fini();
out_eq:
	daos_eq_lib_fini();
out_job:
	dc_job_fini();
out_agent:
	dc_agent_fini();
out_hhash:
	daos_hhash_fini();
out_debug:
	daos_debug_fini();
unlock:
	D_MUTEX_UNLOCK(&module_lock);

	if (d_fault_mem)
		d_fault_attr_set(0, d_fault_mem_saved);

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
	if (module_initialized == 0) {
		/** calling fini without init, report an error */
		D_GOTO(unlock, rc = -DER_UNINIT);
	} else if (module_initialized > 1) {
		/**
		 * DAOS was initialized multiple times.
		 * Can happen when using multiple DAOS-aware middleware.
		 */
		module_initialized--;
		D_GOTO(unlock, rc = 0);
	}

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: "DF_RC"\n", DP_RC(rc));
		D_GOTO(unlock, rc);
	}

#if BUILD_PIPELINE
	dc_pipeline_fini();
#endif
	dc_obj_fini();
	dc_cont_fini();
	dc_pool_fini();
	dc_mgmt_fini();

	rc = dc_mgmt_notify_exit();
	if (rc != 0)
		D_ERROR("failed to disconnect some resources may leak, "
			DF_RC"\n", DP_RC(rc));

	dc_agent_fini();
	dc_job_fini();

	pl_fini();
	daos_hhash_fini();
	daos_debug_fini();
	module_initialized = 0;
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}
