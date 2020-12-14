/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#ifndef __DAOS_DTX_H__
#define __DAOS_DTX_H__

#include <time.h>
#include <uuid/uuid.h>

/* If the count of committable DTXs on leader exceeds this threshold,
 * it will trigger batched DTX commit globally. We will optimize the
 * threshould with considering RPC limitation, PMDK transaction, and
 * CPU schedule efficiency, and so on.
 */
#define DTX_THRESHOLD_COUNT		(1 << 9)

/* The time (in second) threshold for batched DTX commit. */
#define DTX_COMMIT_THRESHOLD_AGE	10

enum dtx_target_flags {
	/* The target only contains read-only operations for the DTX. */
	DTF_RDONLY			= (1 << 0),
};

enum dtx_grp_flags {
	/* The group only contains read-only operations for the DTX. */
	DGF_RDONLY			= (1 << 0),
};

enum dtx_mbs_flags {
	/* The targets modified via the DTX belong to replicated object
	 * within single redundancy group.
	 */
	DMF_SRDG_REP			= (1 << 0),
	/* The MBS contains the leader information, used for distributed
	 * transaction. For stand-alone modification, leader information
	 * is not stored inside MBS as optimization.
	 */
	DMF_CONTAIN_LEADER		= (1 << 1),
};

/**
 * The daos target that participates in the DTX.
 */
struct dtx_daos_target {
	/* Globally target ID, corresponding to pool_component::co_id. */
	uint32_t			ddt_id;
	/* See dtx_target_flags. */
	uint32_t			ddt_flags;
};

/**
 * The items (replica or EC shard) belong to the same redundancy group make
 * up a modification group that is subset of related DAOS redundancy group.
 *
 * These information will be used for DTX recovery as following:
 *
 * During DTX recovery, for a non-committed DTX, its new leader queries with
 * other alive participants for such DTX status. If all alive ones reply the
 * new leader with 'prepared', then before making the decision to commit the
 * DTX, we need to handle some corner cases:
 *
 * Some corrupted DTX participant may have ever refused (because of conflict
 * with other DTX that may be committed or may be not) related modification.
 * But it did not reply to the old leader before its corruption, or did but
 * the old leader crashed before abort such DTX. If the case happened on all
 * members in some modification group, then when DTX recovery, nobody knows
 * there have ever been DTX conflict. Under such case, the new leader should
 * NOT commit such DTX, otherwise, it will break DAOS transaction semantics
 * for other conflict DTXs. On the other hand, abort such DTX is also NOT a
 * safe solution, because it is possible that the corrupted DTX participant
 * may have committed such DTX before its (and the old leader) corruption.
 *
 * So once we detect some group corruption or lost during the DTX recovery,
 * we can neither commit nor abort related DTX to avoid further damage the
 * system. Instead, we can mark it with some flags and introduce more human
 * knowledge to recover it sometime later.
 */
struct dtx_redundancy_group {
	/* How many touched shards in this group. */
	uint32_t			drg_tgt_cnt;

	/* The degree of redundancy. For EC based group, it is equal to the
	 * count of parity nodes + 1. For replicated one, it is the same as
	 * the drg_tgt_cnt.
	 *
	 * If all the shards 'drg_ids[0 - drg_redundancy - 1]' are lost,
	 * then the group is regarded as unavailable.
	 */
	uint16_t			drg_redundancy;

	/* See dtx_grp_flags. */
	uint16_t			drg_flags;

	/* The shards' IDs, corresponding to pool_component::co_id. For the
	 * leader group that is the first in dtx_memberships, 'drg_index[0]'
	 * is for the leader, the other 'drg_index[1 - drg_redundancy - 1]'
	 * are the leader candidates for DTX recovery.
	 */
	uint32_t			drg_ids[0];
};

struct dtx_memberships {
	/* How many touched shards in the DTX. */
	uint32_t			dm_tgt_cnt;

	/* How many modification groups in the DTX. For standalone modification,
	 * be as optimization, we will not store modification group information
	 * inside 'dm_data'. Similarly for the distributed transaction that all
	 * the touched targets are in the same redundancy group.
	 */
	uint32_t			dm_grp_cnt;

	/* sizeof(dm_data). */
	uint32_t			dm_data_size;

	/* see dtx_mbs_flags. */
	uint16_t			dm_flags;

	union {
		/* DTX entry flags during DTX recovery. */
		uint16_t		dm_dte_flags;
		/* For alignment. */
		uint16_t		dm_padding;
	};

	/* The first 'sizeof(struct dtx_daos_target) * dm_tgt_cnt' is the
	 * dtx_daos_target array. The subsequent are modification groups.
	 */
	union {
		char			dm_data[0];
		struct dtx_daos_target	dm_tgts[0];
	};
};

/**
 * DAOS two-phase commit transaction identifier,
 * generated by client, globally unique.
 */
struct dtx_id {
	/** The uuid of the transaction */
	uuid_t			dti_uuid;
	/** The HLC timestamp (not epoch) of the transaction */
	uint64_t		dti_hlc;
};

void daos_dti_gen_unique(struct dtx_id *dti);
void daos_dti_gen(struct dtx_id *dti, bool zero);

static inline void
daos_dti_copy(struct dtx_id *des, const struct dtx_id *src)
{
	if (src != NULL)
		*des = *src;
	else
		memset(des, 0, sizeof(*des));
}

static inline bool
daos_is_zero_dti(struct dtx_id *dti)
{
	return dti->dti_hlc == 0;
}

static inline bool
daos_dti_equal(struct dtx_id *dti0, struct dtx_id *dti1)
{
	return memcmp(dti0, dti1, sizeof(*dti0)) == 0;
}

#define DF_DTI		DF_UUID"."DF_X64
#define DP_DTI(dti)	DP_UUID((dti)->dti_uuid), (dti)->dti_hlc

enum daos_ops_intent {
	DAOS_INTENT_DEFAULT		= 0, /* fetch/enumerate/query */
	DAOS_INTENT_PURGE		= 1, /* purge/aggregation */
	DAOS_INTENT_UPDATE		= 2, /* write/insert */
	DAOS_INTENT_PUNCH		= 3, /* punch/delete */
	DAOS_INTENT_MIGRATION		= 4, /* for migration related scan */
	DAOS_INTENT_CHECK		= 5, /* check aborted or not */
	DAOS_INTENT_KILL		= 6, /* delete object/key */
	DAOS_INTENT_COS			= 7, /* add something into CoS cache. */
	DAOS_INTENT_IGNORE_NONCOMMITTED	= 8, /* ignore non-committed DTX. */
};

enum daos_dtx_alb {
	/* unavailable case */
	ALB_UNAVAILABLE		= 0,
	/* available, no (or not care) pending modification */
	ALB_AVAILABLE_CLEAN	= 1,
	/* available but with dirty modification or garbage */
	ALB_AVAILABLE_DIRTY	= 2,
};

enum daos_tx_flags {
	DTF_RETRY_COMMIT	= 1, /* TX commit will be retry. */
};

/** Epoch context of a DTX */
struct dtx_epoch {
	/** epoch */
	daos_epoch_t		oe_value;
	/** first epoch chosen */
	daos_epoch_t		oe_first;
	/** such as DTX_EPOCH_UNCERTAIN, etc. */
	uint32_t		oe_flags;
	union {
		uint32_t	oe_padding;
		/** see 'obj_rpc_flags' when it is transferred on wire. */
		uint32_t	oe_rpc_flags;
	};
};

/* dtx_epoch.oe_flags */
#define DTX_EPOCH_UNCERTAIN	(1U << 0)	/**< oe_value is uncertain */

/** Does \a epoch contain a chosen TX epoch? */
static inline bool
dtx_epoch_chosen(struct dtx_epoch *epoch)
{
	return (epoch->oe_value != 0 && epoch->oe_value != DAOS_EPOCH_MAX);
}

/** Are \a and \b equal? */
static inline bool
dtx_epoch_equal(struct dtx_epoch *a, struct dtx_epoch *b)
{
	return (a->oe_value == b->oe_value && a->oe_first == b->oe_first &&
		a->oe_flags == b->oe_flags);
}

#endif /* __DAOS_DTX_H__ */
