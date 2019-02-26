/**
 * (C) Copyright 2019 Intel Corporation.
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

#ifndef __DAOS_DTX_SRV_H__
#define __DAOS_DTX_SRV_H__

#include <daos/mem.h>
#include <daos/dtx.h>
#include <daos/placement.h>

#define DTX_THRESHOLD_COUNT			512
#define DTX_COMMIT_THRESHOLD_TIME		60ULL
#define DTX_AGGREGATION_THRESHOLD_COUNT		(1 << 27)
#define DTX_AGGREGATION_THRESHOLD_TIME		3600ULL
#define DTX_AGGREGATION_YIELD_INTERVAL		DTX_THRESHOLD_COUNT

struct daos_tx_entry {
	/** The identifier of the DTX */
	struct daos_tx_id	dte_xid;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t		dte_oid;
};

enum daos_tx_flags {
	/* Abort related DTX by force on conflict. */
	DTX_F_AOC		= 1,
};

enum dtx_cos_list_types {
	DCLT_UPDATE		= (1 << 0),
	DCLT_PUNCH		= (1 << 1),
};

/**
 * DAOS two-phase commit transaction handle in DRAM.
 */
struct daos_tx_handle {
	/** The identifier of the DTX */
	struct daos_tx_id	 dth_xid;
	/** The identifier of the object (shard) to be modified. */
	daos_unit_oid_t		 dth_oid;
	/* The dkey to be modified if applicable */
	daos_key_t		*dth_dkey;
	/** The container handle */
	daos_handle_t		 dth_coh;
	/** The epoch# for the DTX. */
	daos_epoch_t		 dth_epoch;
	/** The {obj/dkey/akey}-tree records that are created
	 * by other DTXs, but not ready for commit yet.
	 */
	d_list_t		 dth_shares;
	/** Pool map version. */
	uint32_t		 dth_ver;
	/** The intent of related modification. */
	uint32_t		 dth_intent;
	/* Flags for related modification, see daos_tx_flags. */
	uint32_t		 dth_flags;
	uint32_t		 dth_sync:1, /* commit DTX synchronously. */
				 dth_leader:1, /* leader replica or not. */
				 dth_non_rep:1; /* non-replicated object. */
	/* The identifier of the DTX that conflict with current one. */
	struct daos_tx_id	 dth_conflict;
	/** The address of the DTX entry in SCM. */
	umem_id_t		 dth_ent;
};

/**
 * DAOS two-phase commit transaction status.
 */
enum dtx_status {
	/**  Initialized, but local modification has not completed. */
	DTX_ST_INIT		= 1,
	/** Local participant has done the modification. */
	DTX_ST_PREPARED		= 2,
	/** The DTX has been committed. */
	DTX_ST_COMMITTED	= 3,
};

/**
 * Some actions to be done for DTX control.
 */
enum dtx_actions {
	/** Need to aggregate some old DTXs. */
	DTX_ACT_AGGREGATE	= 1,
	/** Need to commit some old DTXs asychronously. */
	DTX_ACT_COMMIT_ASYNC	= 2,
	/** Commit current DTX sychronously. */
	DTX_ACT_COMMIT_SYNC	= 3,
};

int dtx_commit(uuid_t po_uuid, uuid_t co_uuid,
	       struct daos_tx_entry *dtes, int count, uint32_t version);
int dtx_abort(uuid_t po_uuid, uuid_t co_uuid,
	      struct daos_tx_entry *dtes, int count, uint32_t version);
int dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid,
	       uint32_t version, bool collective);

#endif /* __DAOS_DTX_SRV_H__ */
