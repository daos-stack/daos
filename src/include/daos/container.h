/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * dc_cont: Container Client API
 */

#ifndef __DD_CONT_H__
#define __DD_CONT_H__

#include <daos/common.h>
#include <daos/pool_map.h>
#include <daos/tse.h>
#include <daos_types.h>
#include "checksum.h"

int dc_cont_init(void);
void dc_cont_fini(void);

int dc_cont_tgt_idx2ptr(daos_handle_t coh, uint32_t tgt_idx,
			struct pool_target **tgt);
int dc_cont_node_id2ptr(daos_handle_t coh, uint32_t node_id,
			struct pool_domain **dom);
int dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *con_uuid);
daos_handle_t dc_cont_hdl2pool_hdl(daos_handle_t coh);
struct daos_csummer *dc_cont_hdl2csummer(daos_handle_t coh);
struct cont_props dc_cont_hdl2props(daos_handle_t coh);

int dc_cont_local2global(daos_handle_t coh, d_iov_t *glob);
int dc_cont_global2local(daos_handle_t poh, d_iov_t glob,
			 daos_handle_t *coh);

int dc_cont_create(tse_task_t *task);
int dc_cont_open(tse_task_t *task);
int dc_cont_close(tse_task_t *task);
int dc_cont_destroy(tse_task_t *task);
int dc_cont_query(tse_task_t *task);
int dc_cont_set_prop(tse_task_t *task);
int dc_cont_update_acl(tse_task_t *task);
int dc_cont_delete_acl(tse_task_t *task);
int dc_cont_aggregate(tse_task_t *task);
int dc_cont_rollback(tse_task_t *task);
int dc_cont_subscribe(tse_task_t *task);
int dc_cont_list_attr(tse_task_t *task);
int dc_cont_get_attr(tse_task_t *task);
int dc_cont_set_attr(tse_task_t *task);
int dc_cont_del_attr(tse_task_t *task);
int dc_cont_alloc_oids(tse_task_t *task);
int dc_cont_list_snap(tse_task_t *task);
int dc_cont_create_snap(tse_task_t *task);
int dc_cont_destroy_snap(tse_task_t *task);

#endif /* __DD_CONT_H__ */
