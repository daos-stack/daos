/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
 * client stack task API definition.
 */
#ifndef __DAOS_CLI_TASK_H__
#define __DAOS_CLI_TASK_H__

#include <daos_types.h>
#include <daos_api.h>
#include <daos/tse.h>
#include <daos/common.h>
#include <daos_task.h>

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
dc_obj_sync_task_create(daos_handle_t oh, daos_epoch_t epoch,
			daos_epoch_t **epochs_p, int *nr, daos_event_t *ev,
			tse_sched_t *tse, tse_task_t **task);
int
dc_obj_fetch_shard_task_create(daos_handle_t oh, daos_handle_t th,
			       unsigned int flags, unsigned int shard,
			       daos_key_t *dkey, unsigned int nr,
			       daos_iod_t *iods, d_sg_list_t *sgls,
			       daos_iom_t *maps, daos_event_t *ev,
			       tse_sched_t *tse, tse_task_t **task);

int
dc_obj_fetch_task_create(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			 daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
			 d_sg_list_t *sgls, void *extra_args,
			 daos_iom_t *maps, daos_event_t *ev,
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

void *
dc_task_get_args(tse_task_t *task);
#endif
