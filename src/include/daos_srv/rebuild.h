/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#include <daos_srv/pool.h>

#define REBUILD_ENV            "DAOS_REBUILD"
#define REBUILD_ENV_DISABLED   "no"

/**
 * Enum values to indicate the rebuild operation that should be applied to the
 * associated targets
 */
typedef enum {
	RB_OP_FAIL,
	RB_OP_DRAIN,
	RB_OP_REINT,
	RB_OP_EXTEND,
} daos_rebuild_opc_t;

#define RB_OP_STR(rb_op) ((rb_op) == RB_OP_FAIL ? "RB_OP_FAIL" : \
			  (rb_op) == RB_OP_DRAIN ? "RB_OP_DRAIN" : \
			  (rb_op) == RB_OP_REINT ? "RB_OP_REINT" : \
			  (rb_op) == RB_OP_EXTEND ? "RB_OP_EXTEND" : \
			  "RB_OP_UNKNOWN")

int ds_rebuild_schedule(const uuid_t uuid, uint32_t map_ver,
			struct pool_target_id_list *tgts,
			daos_rebuild_opc_t rebuild_op);
int ds_rebuild_query(uuid_t pool_uuid,
		     struct daos_rebuild_status *status);
int ds_rebuild_regenerate_task(struct ds_pool *pool);
int ds_rebuild_pool_map_update(struct ds_pool *pool);
void ds_rebuild_leader_stop_all(void);
void ds_rebuild_leader_stop(const uuid_t pool_uuid, unsigned int version);
void ds_rebuild_abort(uuid_t pool_uuid, unsigned int version);
#endif
