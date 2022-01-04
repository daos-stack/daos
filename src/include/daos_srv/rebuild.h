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

#define RB_OP_STR(rb_op) ((rb_op) == RB_OP_FAIL ? "Rebuild" : \
			  (rb_op) == RB_OP_DRAIN ? "Drain" : \
			  (rb_op) == RB_OP_REINT ? "Reintegrate" : \
			  (rb_op) == RB_OP_EXTEND ? "Extend" : \
			  (rb_op) == RB_OP_RECLAIM ? "Reclaim" : \
			  "Unknown")

int ds_rebuild_schedule(struct ds_pool *pool, uint32_t map_ver,
			struct pool_target_id_list *tgts,
			daos_rebuild_opc_t rebuild_op, uint64_t delay_sec);
int ds_rebuild_query(uuid_t pool_uuid,
		     struct daos_rebuild_status *status);
int ds_rebuild_regenerate_task(struct ds_pool *pool, daos_prop_t *prop);
void ds_rebuild_leader_stop_all(void);
void ds_rebuild_leader_stop(const uuid_t pool_uuid, unsigned int version);
void ds_rebuild_abort(uuid_t pool_uuid, unsigned int version);
#endif
