/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	RB_OP_RECLAIM,
} daos_rebuild_opc_t;

#define RB_OP_STR(rb_op) ((rb_op) == RB_OP_FAIL ? "RB_OP_FAIL" : \
			  (rb_op) == RB_OP_DRAIN ? "RB_OP_DRAIN" : \
			  (rb_op) == RB_OP_REINT ? "RB_OP_REINT" : \
			  (rb_op) == RB_OP_EXTEND ? "RB_OP_EXTEND" : \
			  (rb_op) == RB_OP_RECLAIM ? "RB_OP_RECLAIM" : \
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
