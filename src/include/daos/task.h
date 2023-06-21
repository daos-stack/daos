/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * client stack task API definition.
 */
#ifndef __DAOS_CLI_TASK_H__
#define __DAOS_CLI_TASK_H__

#include <daos_types.h>
#include <daos_api.h>
#include <daos/tse.h>
#include <daos/common.h>
#include <daos_task.h>

/* size of daos_task_args should within limitation of TSE_TASK_ARG_LEN */
struct daos_task_args {
	uint32_t			ta_magic;
	uint32_t			ta_opc;
	union {
		/** Management */
		daos_set_params_t	mgmt_set_params;
		daos_pool_replicas_t	pool_add_replicas;
		daos_pool_replicas_t	pool_remove_replicas;
		daos_mgmt_get_bs_state_t mgmt_get_bs_state;

		/** Pool */
		daos_pool_connect_t	pool_connect;
		daos_pool_disconnect_t	pool_disconnect;
		daos_pool_update_t	pool_update;
		daos_pool_query_t	pool_query;
		daos_pool_query_target_t pool_query_info;
		daos_pool_list_attr_t	pool_list_attr;
		daos_pool_get_attr_t	pool_get_attr;
		daos_pool_set_attr_t	pool_set_attr;
		daos_pool_stop_svc_t	pool_stop_svc;
		daos_pool_list_cont_t	pool_list_cont;

		/** Container */
		daos_cont_create_t	cont_create;
		daos_cont_open_t	cont_open;
		daos_cont_close_t	cont_close;
		daos_cont_destroy_t	cont_destroy;
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
		daos_cont_snap_oit_oid_get_t cont_get_oit_oid;
		daos_cont_snap_oit_create_t cont_snap_oit_create;
		daos_cont_snap_oit_destroy_t cont_snap_oit_destroy;

		/** Transaction */
		daos_tx_open_t		tx_open;
		daos_tx_commit_t	tx_commit;
		daos_tx_abort_t		tx_abort;
		daos_tx_close_t		tx_close;
		daos_tx_restart_t	tx_restart;

		/** Object */
		struct daos_obj_register_class_t obj_reg_class;
		daos_obj_query_class_t	obj_query_class;
		daos_obj_list_class_t	obj_list_class;
		daos_obj_open_t		obj_open;
		daos_obj_close_t	obj_close;
		daos_obj_punch_t	obj_punch;
		daos_obj_query_t	obj_query;
		daos_obj_query_key_t	obj_query_key;
		struct daos_obj_sync_args obj_sync;
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

		/** KV */
		daos_kv_open_t		kv_open;
		daos_kv_close_t		kv_close;
		daos_kv_destroy_t	kv_destroy;
		daos_kv_get_t		kv_get;
		daos_kv_put_t		kv_put;
		daos_kv_remove_t	kv_remove;
		daos_kv_list_t		kv_list;

		/** Pipeline */
		daos_pipeline_run_t	pipeline_run;
	}		 ta_u;
	daos_event_t	*ta_ev;
};

/**
 * Push to task stack space. This API only reserves space on the task stack, no
 * data copy involved.
 *
 * \param task [in] task to push the buffer.
 * \param size [in] buffer size.
 *
 * \return	pointer to the pushed buffer in task stack.
 */
void *
tse_task_stack_push(tse_task_t *task, uint32_t size);

/**
 * Pop from task stack space. This API only reserves space on the task stack, no
 * data copy involved.
 *
 * \param task [in] task to pop the buffer.
 * \param size [in] buffer size.
 *
 * \return	pointer to the popped buffer in task stack.
 */
void *
tse_task_stack_pop(tse_task_t *task, uint32_t size);

/**
 * Push data to task stack space, will copy the data to stack.
 *
 * \param task [in]	task to push the buffer.
 * \param data [in]	pointer of data to push
 * \param len  [in]	length of data
 */
void
tse_task_stack_push_data(tse_task_t *task, void *data, uint32_t len);

/**
 * Pop data from task stack space, will copy the data from stack.
 *
 * \param task [in]	task to push the buffer.
 * \param data [in/out]	pointer of value to store the popped data
 * \param len  [in]	length of data
 */
void
tse_task_stack_pop_data(tse_task_t *task, void *data, uint32_t len);

/**
 * Return the internal private data of the task.
 */
void *
tse_task_get_priv_internal(tse_task_t *task);

/**
 * Set or change the internal private data of the task. The original internal
 * private data will be returned.
 */
void *
tse_task_set_priv_internal(tse_task_t *task, void *priv);

struct daos_task_api {
	tse_task_func_t		task_func;
	daos_size_t		arg_size;
};

extern const struct daos_task_api dc_funcs[DAOS_OPC_MAX];
#define DAOS_API_ARG_ASSERT(args, name)					\
do {									\
	int __opc = DAOS_OPC_##name;					\
	D_ASSERTF(sizeof(args) == dc_funcs[__opc].arg_size,		\
		  "Argument size %zu != predefined arg size %zu\n",	\
		  sizeof(args), dc_funcs[__opc].arg_size);		\
} while (0)

int
dc_obj_open_task_create(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
			daos_handle_t *oh, daos_event_t *ev, tse_sched_t *tse,
			tse_task_t **task);
int
dc_obj_close_task_create(daos_handle_t oh, daos_event_t *ev,
			 tse_sched_t *tse, tse_task_t **task);
int
dc_obj_punch_task_create(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			 daos_event_t *ev, tse_sched_t *tse,
			 tse_task_t **task);
int
dc_obj_punch_dkeys_task_create(daos_handle_t oh, daos_handle_t th,
			       uint64_t flags, unsigned int nr,
			       daos_key_t *dkeys, daos_event_t *ev,
			       tse_sched_t *tse, tse_task_t **task);
int
dc_obj_punch_akeys_task_create(daos_handle_t oh, daos_handle_t th,
			       uint64_t flags, daos_key_t *dkey,
			       unsigned int nr, daos_key_t *akeys,
			       daos_event_t *ev, tse_sched_t *tse,
			       tse_task_t **task);
int
dc_obj_query_key_task_create(daos_handle_t oh, daos_handle_t th,
			     uint64_t flags, daos_key_t *dkey, daos_key_t *akey,
			     daos_recx_t *recx, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task);
int
dc_obj_query_max_epoch_task_create(daos_handle_t oh, daos_handle_t th, daos_epoch_t *epoch,
				   daos_event_t *ev, tse_sched_t *tse, tse_task_t **task);
int
dc_obj_sync_task_create(daos_handle_t oh, daos_epoch_t epoch,
			daos_epoch_t **epochs_p, int *nr, daos_event_t *ev,
			tse_sched_t *tse, tse_task_t **task);
int
dc_obj_fetch_task_create(daos_handle_t oh, daos_handle_t th, uint64_t api_flags,
			 daos_key_t *dkey, uint32_t nr, uint32_t extra_flags,
			 daos_iod_t *iods, d_sg_list_t *sgls, daos_iom_t *ioms,
			 void *extra_arg, d_iov_t *csum, daos_event_t *ev,
			 tse_sched_t *tse, tse_task_t **task);
int
dc_obj_update_task_create(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			  daos_key_t *dkey, unsigned int nr,
			  daos_iod_t *iods, d_sg_list_t *sgls,
			  daos_event_t *ev, tse_sched_t *tse,
			  tse_task_t **task);

int
dc_obj_list_dkey_task_create(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
			     daos_key_desc_t *kds, d_sg_list_t *sgl,
			     daos_anchor_t *anchor, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task);
int
dc_obj_list_akey_task_create(daos_handle_t oh, daos_handle_t th,
			     daos_key_t *dkey, uint32_t *nr,
			     daos_key_desc_t *kds, d_sg_list_t *sgl,
			     daos_anchor_t *anchor, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task);
int
dc_obj_list_recx_task_create(daos_handle_t oh, daos_handle_t th,
			     daos_key_t *dkey, daos_key_t *akey,
			     daos_iod_type_t type, daos_size_t *size,
			     uint32_t *nr, daos_recx_t *recx,
			     daos_epoch_range_t *eprs, daos_anchor_t *anchor,
			     bool incr_order, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task);
int
dc_obj_list_obj_task_create(daos_handle_t oh, daos_handle_t th,
			    daos_epoch_range_t *epr, daos_key_t *dkey,
			    daos_key_t *akey, daos_size_t *size,
			    uint32_t *nr, daos_key_desc_t *kds,
			    d_sg_list_t *sgl, daos_anchor_t *anchor,
			    daos_anchor_t *dkey_anchor,
			    daos_anchor_t *akey_anchor, bool incr_order,
			    daos_event_t *ev, tse_sched_t *tse,
			    d_iov_t *csum, tse_task_t **task);
int
dc_obj_key2anchor_task_create(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
			      daos_key_t *akey, daos_anchor_t *anchor, daos_event_t *ev,
			      tse_sched_t *tse, tse_task_t **task);

int
dc_pipeline_run_task_create(daos_handle_t coh, daos_handle_t oh, daos_handle_t th,
			    daos_pipeline_t *pipeline, uint64_t flags, daos_key_t *dkey,
			    uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
			    uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
			    d_sg_list_t *sgl_recx, daos_size_t *recx_size, d_sg_list_t *sgl_agg,
			    daos_pipeline_stats_t *stats, daos_event_t *ev, tse_sched_t *tse,
			    tse_task_t **task);

void *
dc_task_get_args(tse_task_t *task);
#endif
