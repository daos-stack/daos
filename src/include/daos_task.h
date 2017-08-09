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
 * DAOS task-based API
 */

#ifndef __DAOS_TASK_H__
#define __DAOS_TASK_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_addons.h>
#include <daos_errno.h>
#include <daos/tse.h>

/** DAOS operation codes for task creation */
typedef enum {
	DAOS_OPC_INVALID	= -1,

	/** Managment APIs */
	DAOS_OPC_SVC_RIP = 0,
	DAOS_OPC_POOL_CREATE,
	DAOS_OPC_POOL_DESTROY,
	DAOS_OPC_POOL_EXTEND,
	DAOS_OPC_POOL_EVICT,

	/** Pool APIs */
	DAOS_OPC_POOL_CONNECT,
	DAOS_OPC_POOL_DISCONNECT,
	DAOS_OPC_POOL_EXCLUDE,
	DAOS_OPC_POOL_EXCLUDE_OUT,
	DAOS_OPC_POOL_ADD,
	DAOS_OPC_POOL_QUERY,
	DAOS_OPC_POOL_TARGET_QUERY,
	DAOS_OPC_POOL_SVC_STOP,

	/** Container APIs */
	DAOS_OPC_CONT_CREATE,
	DAOS_OPC_CONT_OPEN,
	DAOS_OPC_CONT_CLOSE,
	DAOS_OPC_CONT_DESTROY,
	DAOS_OPC_CONT_QUERY,
	DAOS_OPC_CONT_ATTR_LIST,
	DAOS_OPC_CONT_ATTR_GET,
	DAOS_OPC_CONT_ATTR_SET,

	/** Epoch APIs */
	DAOS_OPC_EPOCH_FLUSH,
	DAOS_OPC_EPOCH_DISCARD,
	DAOS_OPC_EPOCH_QUERY,
	DAOS_OPC_EPOCH_HOLD,
	DAOS_OPC_EPOCH_SLIP,
	DAOS_OPC_EPOCH_COMMIT,
	DAOS_OPC_EPOCH_WAIT,

	/** Snapshot APIs */
	DAOS_OPC_SNAP_LIST,
	DAOS_OPC_SNAP_CREATE,
	DAOS_OPC_SNAP_DESTROY,

	/** Object APIs */
	DAOS_OPC_OBJ_CLASS_REGISTER,
	DAOS_OPC_OBJ_CLASS_QUERY,
	DAOS_OPC_OBJ_CLASS_LIST,
	DAOS_OPC_OBJ_DECLARE,
	DAOS_OPC_OBJ_OPEN,
	DAOS_OPC_OBJ_CLOSE,
	DAOS_OPC_OBJ_PUNCH,
	DAOS_OPC_OBJ_QUERY,
	DAOS_OPC_OBJ_FETCH,
	DAOS_OPC_OBJ_UPDATE,
	DAOS_OPC_OBJ_LIST_DKEY,
	DAOS_OPC_OBJ_LIST_AKEY,
	DAOS_OPC_OBJ_LIST_RECX,
	DAOS_OPC_OBJ_SHARD_LIST_DKEY,

	/** Array APIs */
	DAOS_OPC_ARRAY_CREATE,
	DAOS_OPC_ARRAY_OPEN,
	DAOS_OPC_ARRAY_CLOSE,
	DAOS_OPC_ARRAY_READ,
	DAOS_OPC_ARRAY_WRITE,
	DAOS_OPC_ARRAY_GET_SIZE,
	DAOS_OPC_ARRAY_SET_SIZE,

	/** HL APIs */
	DAOS_OPC_KV_GET,
	DAOS_OPC_KV_PUT,
	DAOS_OPC_KV_REMOVE,
	DAOS_OPC_OBJ_FETCH_MULTI,
	DAOS_OPC_OBJ_UPDATE_MULTI,

	DAOS_OPC_MAX
} daos_opc_t;

typedef struct {
	const char		*grp;
	daos_rank_t		rank;
	bool			force;
} daos_svc_rip_t;

typedef struct {
	unsigned int		mode;
	unsigned int		uid;
	unsigned int		gid;
	const char		*grp;
	const daos_rank_list_t	*tgts;
	const char		*dev;
	daos_size_t		size;
	daos_rank_list_t	*svc;
	unsigned char		*uuid;
} daos_pool_create_t;

typedef struct {
	const uuid_t		uuid;
	const char		*grp;
	int			force;
} daos_pool_destroy_t;

typedef struct {
	const uuid_t		uuid;
	const char		*grp;
	daos_rank_list_t	*tgts;
	daos_rank_list_t	*failed;
} daos_pool_extend_t;

typedef struct {
	const uuid_t		uuid;
	const char		*grp;
	daos_rank_list_t	*svc;
} daos_pool_evict_t;

typedef struct {
	const uuid_t		uuid;
	const char		*grp;
	const daos_rank_list_t	*svc;
	unsigned int		flags;
	daos_handle_t		*poh;
	daos_pool_info_t	*info;
} daos_pool_connect_t;

typedef struct {
	daos_handle_t		poh;
} daos_pool_disconnect_t;

typedef struct {
	const uuid_t		uuid;
	const char		*grp;
	daos_rank_list_t	*svc;
	daos_rank_list_t	*tgts;
} daos_pool_update_t;

typedef struct {
	daos_handle_t		poh;
	daos_rank_list_t	*tgts;
	daos_pool_info_t	*info;
} daos_pool_query_t;

typedef struct {
	daos_handle_t		poh;
	daos_rank_list_t	*tgts;
	daos_rank_list_t	*failed;
	daos_target_info_t	*info_list;
} daos_pool_target_query_t;

typedef struct {
	daos_handle_t		poh;
} daos_pool_svc_stop_t;

typedef struct {
	daos_handle_t		poh;
	const uuid_t		uuid;
} daos_cont_create_t;

typedef struct {
	daos_handle_t		poh;
	const uuid_t		uuid;
	unsigned int		flags;
	daos_handle_t		*coh;
	daos_cont_info_t	*info;
} daos_cont_open_t;

typedef struct {
	daos_handle_t		coh;
} daos_cont_close_t;

typedef struct {
	daos_handle_t		poh;
	const uuid_t		uuid;
	int			force;
} daos_cont_destroy_t;

typedef struct {
	daos_handle_t		coh;
	daos_cont_info_t	*info;
} daos_cont_query_t;

typedef struct {
	daos_handle_t		coh;
	char			*buf;
	size_t			*size;
} daos_cont_attr_list_t;

typedef struct {
	daos_handle_t		coh;
	int			n;
	const char		**names;
	void			**values;
	size_t			**sizes;
} daos_cont_attr_get_t;

typedef struct {
	daos_handle_t		coh;
	int			n;
	const char		**names;
	const void		**values;
	const size_t		*sizes;
} daos_cont_attr_set_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	*state;
} daos_epoch_flush_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_state_t	*state;
} daos_epoch_query_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	*state;
} daos_epoch_discard_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		*epoch;
	daos_epoch_state_t	*state;
} daos_epoch_hold_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	*state;
} daos_epoch_slip_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	*state;
} daos_epoch_commit_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	*state;
} daos_epoch_wait_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		*buf;
	int			*n;
} daos_snap_list_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
} daos_snap_create_t;

typedef struct {
	daos_handle_t		coh;
	daos_epoch_t		epoch;
} daos_snap_destroy_t;

typedef struct {
	daos_handle_t		coh;
	daos_oclass_id_t	cid;
	daos_oclass_attr_t	*cattr;
} daos_obj_class_register_t;

typedef struct {
	daos_handle_t		coh;
	daos_oclass_id_t	cid;
	daos_oclass_attr_t	*cattr;
} daos_obj_class_query_t;

typedef struct {
	daos_handle_t		coh;
	daos_oclass_list_t	*clist;
	daos_hash_out_t		*anchor;
} daos_obj_class_list_t;

typedef struct {
	daos_handle_t		coh;
	daos_obj_id_t		oid;
	daos_epoch_t		epoch;
	daos_obj_attr_t		*oa;
} daos_obj_declare_t;

typedef struct {
	daos_handle_t		coh;
	daos_obj_id_t		oid;
	daos_epoch_t		epoch;
	unsigned int		mode;
	daos_handle_t		*oh;
} daos_obj_open_t;

typedef struct {
	daos_handle_t		oh;
} daos_obj_close_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
} daos_obj_punch_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_obj_attr_t		*oa;
	daos_rank_list_t	*ranks;
} daos_obj_query_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	unsigned int		nr;
	daos_iod_t		*iods;
	daos_sg_list_t		*sgls;
	daos_iom_t		*maps;
} daos_obj_fetch_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	unsigned int		nr;
	daos_iod_t		*iods;
	daos_sg_list_t		*sgls;
} daos_obj_update_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	uint32_t		*nr;
	daos_key_desc_t		*kds;
	daos_sg_list_t		*sgl;
	daos_hash_out_t		*anchor;
} daos_obj_list_dkey_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	uint32_t		*nr;
	daos_key_desc_t		*kds;
	daos_sg_list_t		*sgl;
	daos_hash_out_t		*anchor;
} daos_obj_list_akey_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	daos_key_t		*akey;
	daos_size_t		*size;
	daos_iod_type_t		type;
	uint32_t		*nr;
	daos_recx_t		*recxs;
	daos_epoch_range_t	*eprs;
	uuid_t			*cookies;
	daos_hash_out_t		*anchor;
	uint32_t		*versions;
	bool			incr_order;
} daos_obj_list_recx_t;

typedef struct {
	daos_handle_t	coh;
	daos_obj_id_t	oid;
	daos_epoch_t	epoch;
	daos_size_t	cell_size;
	daos_size_t	block_size;
	daos_handle_t	*oh;
} daos_array_create_t;

typedef struct {
	daos_handle_t	coh;
	daos_obj_id_t	oid;
	daos_epoch_t	epoch;
	unsigned int	mode;
	daos_size_t	*cell_size;
	daos_size_t	*block_size;
	daos_handle_t	*oh;
} daos_array_open_t;

typedef struct {
	daos_handle_t	oh;
} daos_array_close_t;

typedef struct {
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_array_ranges_t	*ranges;
	daos_sg_list_t		*sgl;
	daos_csum_buf_t		*csums;
} daos_array_io_t;

typedef struct {
	daos_handle_t           oh;
	daos_epoch_t            epoch;
	daos_size_t		*size;
} daos_array_get_size_t;

typedef struct {
	daos_handle_t           oh;
	daos_epoch_t            epoch;
	daos_size_t		size;
} daos_array_set_size_t;

typedef struct {
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	const char	*key;
	daos_size_t	*buf_size;
	void		*buf;
} daos_kv_get_t;

typedef struct {
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	const char	*key;
	daos_size_t	buf_size;
	const void	*buf;
} daos_kv_put_t;

typedef struct {
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	const char	*key;
} daos_kv_remove_t;

typedef struct {
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	unsigned int	num_dkeys;
	daos_dkey_io_t	*io_array;
} daos_obj_multi_io_t;

/**
 * Create an asynchronous task and associate it with a daos client operation.
 * For synchronous operations please use the specific API for that operation.
 * Typically this API is used for use cases where a list of daos operations need
 * to be queued into the DAOS async engines with specific dependencies for order
 * of execution between those tasks. For example, a use can create task to open
 * and object then update that object with a dependency inserted on the update
 * to the open task.
 * For a simpler workflow, users can use the event based API instead of tasks.
 *
 * \param opc	[IN]	Operation Code to identify the daos op to associate with
 *			the task,
 * \param sched	[IN]	Scheduler / Engine this task will be added to.
 * \param nr	[IN]	Number of tasks this task depends on before it gets
 *			scheduled. No tasks can be in progress.
 * \param dep_tasks [IN]
 *			Array of tasks that new task will wait on completion
 *			before it's scheduled.
 * \param ready	[IN]	Indicate whether the DAOS op can be immediately called
 *			(true) or not (false). If set to true, dep_tasks must be
 *			NULL and user can't register prep callbacks anymore.
 * \param taskp	[OUT]	Pointer to task to be created/initalized with the op.
 *
 * \return		0 if task creation succeeds.
 *			negative errno if it fails.
 */
int
daos_task_create(daos_opc_t opc, tse_sched_t *sched, void *args,
		 unsigned int num_deps, tse_task_t *dep_tasks[],
		 tse_task_t **taskp);

/**
 * Return a pointer to the DAOS task argument structure. This is called to set
 * the arguments for the task before being scheduled, typically after it's
 * created or in its prepare cb. The task must be created with
 * daos_task_create() and a valid DAOS opc.
 *
 * \param opc	[IN]	OPC that was used to create the task with.
 * \param task	[IN]	Task to retrieve the struct from.
 *
 * \return		Success: Pointer to arguments for the DAOS task
 *			FAIL: NULL, likely because the opc did not match
 */
void *
daos_task_get_args(daos_opc_t opc, tse_task_t *task);

/**
 * Return a pointer to the DAOS task private state. If no private state has
 * been set (via daos_task_get_priv()), NULL is returned.
 *
 * \param task	[IN]	Task to retrieve the private state from
 *
 * \return		Pointer to the private state
 */
void *
daos_task_get_priv(tse_task_t *task);

/**
 * Set a pointer to the DAOS task private state.
 *
 * \param task	[IN,OUT]	Task to retrieve the private state from
 * \param priv	[IN]		Pointer to the private state
 */
void
daos_task_set_priv(tse_task_t *task, void *priv);

/**
 * Make progress on the RPC context associated with the scheduler and schedule
 * tasks that are ready. Also check if the scheduler has any tasks.
 *
 * \param sched	[IN]	Scheduler to make progress on.
 * \param timeout [IN]	How long is caller going to wait (micro-second)
 *			if \a timeout > 0,
 *			it can also be DAOS_EQ_NOWAIT, DAOS_EQ_WAIT
 * \param is_empty [OUT]
 *			flag to indicate whether the scheduler is empty or not.
 *
 * \return		0 if Success, errno if failed.
 */
int
daos_progress(tse_sched_t *sched, int64_t timeout, bool *is_empty);

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_TASK_H__ */
