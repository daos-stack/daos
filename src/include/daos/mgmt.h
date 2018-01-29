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
 * dc_mgmt: Management Client API
 */

#ifndef __DC_MGMT_H__
#define __DC_MGMT_H__

#include <daos_types.h>
#include <daos/tse.h>

int dc_mgmt_init(void);

void dc_mgmt_fini(void);

int dc_mgmt_svc_rip(tse_task_t *task);
int dc_pool_create(tse_task_t *task);
int dc_pool_destroy(tse_task_t *task);
int dc_pool_evict(tse_task_t *task);
int dc_pool_extend(tse_task_t *task);
int dc_mgmt_params_set(tse_task_t *task);

/**
 * object layout information.
 **/
struct daos_obj_shard {
	uint32_t	os_replica_nr;
	uint32_t	os_ranks[0];
};

struct daos_obj_layout {
	uint32_t	ol_ver;
	uint32_t	ol_class;
	uint32_t	ol_nr;
	struct daos_obj_shard	*ol_shards[0];
};

int
daos_obj_layout_get(daos_handle_t coh, daos_obj_id_t oid,
		    struct daos_obj_layout **layout);

int
daos_obj_layout_free(struct daos_obj_layout *layout);

int
daos_obj_layout_alloc(struct daos_obj_layout **layout, uint32_t grp_nr,
		      uint32_t grp_size);
#endif
