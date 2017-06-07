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
 * dc_rebuild: Rebuild Client API
 *
 * This consists of dc_rebuild methods that do not belong to DAOS API.
 */

#ifndef __DAOS_REBUILD_H__
#define __DAOS_REBUILD_H__

#include <daos_types.h>
#include <daos/client.h>
#include <daos/scheduler.h>
#include <daos/common.h>

int
dc_rebuild_tgt(uuid_t pool_uuid, daos_rank_list_t *failed_list,
	       struct daos_task *task);
int
dc_rebuild_tgt_fini(uuid_t pool_uuid, daos_rank_list_t *failed_list,
		    struct daos_task *task);
int
dc_rebuild_query(daos_handle_t poh, daos_rank_list_t *failed_list,
		 int *done, int *failed, unsigned int *rec_count,
		 unsigned int *obj_count,
		 struct daos_task *task);

int dc_rebuild_init(void);
void dc_rebuild_fini(void);
#endif
