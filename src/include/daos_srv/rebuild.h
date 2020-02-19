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
 * rebuild Server API
 */

#ifndef __DAOS_SRV_REBUILD_H__
#define __DAOS_SRV_REBUILD_H__

#include <daos_types.h>

#define REBUILD_ENV            "DAOS_REBUILD"
#define REBUILD_ENV_DISABLED   "no"

bool is_rebuild_container(uuid_t pool_uuid, uuid_t coh_uuid);
bool is_rebuild_pool(uuid_t pool_uuid, uuid_t poh_uuid);

int ds_rebuild_schedule(const uuid_t uuid, uint32_t map_ver,
			struct pool_target_id_list *tgts_failed);
int ds_rebuild_query(uuid_t pool_uuid,
		     struct daos_rebuild_status *status);
int ds_rebuild_regenerate_task(struct ds_pool *pool);
int ds_rebuild_pool_map_update(struct ds_pool *pool);
void ds_rebuild_leader_stop_all(void);
void ds_rebuild_leader_stop(const uuid_t pool_uuid, unsigned int version);
#endif
