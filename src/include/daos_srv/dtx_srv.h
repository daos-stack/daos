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

#ifndef __DAOS_DTX_SRV_H__
#define __DAOS_DTX_SRV_H__

#include <daos/mem.h>
#include <daos/dtx.h>
#include <daos/placement.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>

struct dtx_rdg_unit {
	/* The ID for one object that is in this redundancy group (RDG). */
	daos_obj_id_t		dru_oid;
	/* The count of shards within this RDG that participate in the DTX. */
	uint16_t		dru_shard_cnt;
	/* 32-bits alignment. */
	uint16_t		dru_padding;
	/*
	 * The array of shards index. Cover 3-way replicas,
	 * or partial EC obj update (1 shard + 2 parities).
	 */
	uint32_t		dru_shards[3];
};

struct dtx_entry {
	struct dtx_id		 dte_xid;
	uint32_t		 dte_rdg_size;
	uint16_t		 dte_rdg_cnt;
	struct dtx_rdg_unit	*dte_rdgs;
};

enum dtx_flags {
	/* The DTX is the leader */
	DF_LEADER		= (1 << 0),
	/* The DTX entry is invalid. */
	DF_INVALID		= (1 << 1),
	/*
	 * Compounded DTX that may touch multiple dkeys
	 * (in spite of whether in the same RDG or not).
	 */
	DF_CPD			= (1 << 2),
	/* The RDG information is embedded inside DTX entry. */
	DF_INLINE_RDG		= (1 << 3),
};

/**
 * DAOS two-phase commit transaction handle in DRAM.
 */
struct dtx_handle {
	/** Holds DTX identifier and related RDG information. */
	struct dtx_entry		 dth_entry;
	/** The container handle */
	daos_handle_t			 dth_coh;
	/** The epoch# for the DTX. */
	daos_epoch_t			 dth_epoch;
	/** Pool map version. */
	uint32_t			 dth_ver;

	uint32_t			 dth_sync:1, /* commit synchronously. */
					 dth_leader:1, /* leader replica. */
					 /* Only one participator in the DTX. */
					 dth_solo:1,
					 /* The DTX may cross multiple dkeys. */
					 dth_compounded:1,
					 /* dti_cos has been committed. */
					 dth_dti_cos_done:1,
					 /* May touch shared target. */
					 dth_touch_shared:1,
					 /* epoch conflict, need to renew. */
					 dth_renew:1,
					 /* The DTX entry is in active table. */
					 dth_actived:1;

	/* The generation when the DTX is handled on the server. */
	uint64_t			 dth_gen;
	/** Pointer to the DTX entry in DRAM. */
	void				*dth_ent;

	/* The array of the DTXs for Commit on Share (conflcit). */
	struct dtx_id			*dth_dti_cos;
	/* The count the DTXs in the dth_dti_cos array. */
	uint32_t			 dth_dti_cos_cnt;

	/* The following fields are per modification based. */

	/** The intent of related modification. */
	uint16_t			 dth_intent;
	/* Operation sequence starts from 1 instead of 0. */
	uint16_t			 dth_op_seq;
	/* The hash of the dkey to be modified if applicable */
	uint64_t			 dth_dkey_hash;
	/** The identifier of the shard to be modified. */
	daos_unit_oid_t			 dth_oid;
};

/* Each sub transaction handle to manage each sub thandle */
struct dtx_sub_status {
	struct daos_shard_tgt		dss_tgt;
	int				dss_result;
};

/* Transaction handle on the leader node to manage the transaction */
struct dtx_leader_handle {
	/* The dtx handle on the leader node */
	struct dtx_handle		dlh_handle;
	/* result for the distribute transaction */
	int				dlh_result;

	/* The array of the DTX COS entries */
	uint32_t			dlh_dti_cos_count;
	struct dtx_id			*dlh_dti_cos;

	/* The future to wait for all sub handle to finish */
	ABT_future			dlh_future;

	/* How many sub leader transaction */
	uint32_t			dlh_sub_cnt;
	/* Sub transaction handle to manage the dtx leader */
	struct dtx_sub_status		*dlh_subs;
};

struct dtx_stat {
	uint64_t	dtx_committable_count;
	uint64_t	dtx_oldest_committable_time;
	uint64_t	dtx_committed_count;
	uint64_t	dtx_oldest_committed_time;
};

/**
 * DAOS two-phase commit transaction status.
 */
enum dtx_status {
	/** Local participant has done the modification. */
	DTX_ST_PREPARED		= 1,
	/** The DTX has been committed. */
	DTX_ST_COMMITTED	= 2,
};

void
dtx_sub_init(struct dtx_handle *dth, daos_unit_oid_t *oid,
	     uint64_t dkey_hash, uint16_t intent);
int
dtx_leader_begin(struct dtx_id *dti, daos_handle_t coh, daos_epoch_t epoch,
		 uint32_t pm_ver, uint32_t rdg_size, uint16_t rdg_cnt,
		 struct dtx_rdg_unit *rdgs, struct daos_shard_tgt *tgts,
		 uint32_t tgt_cnt, uint32_t dti_cos_cnt,
		 struct dtx_id *dti_cos, struct dtx_leader_handle *dlh);
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont,
	       int result);

typedef void (*dtx_sub_comp_cb_t)(struct dtx_leader_handle *dlh, int idx,
				  int rc);
typedef int (*dtx_sub_func_t)(struct dtx_leader_handle *dlh, void *arg, int idx,
			      dtx_sub_comp_cb_t comp_cb);

int dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid,
	       uint32_t ver, bool block);
int
dtx_begin(struct dtx_id *dti, daos_handle_t coh, daos_epoch_t epoch,
	  uint32_t pm_ver, uint32_t rdg_size, uint16_t rdg_cnt,
	  struct dtx_rdg_unit *rdgs, uint32_t dti_cos_cnt,
	  struct dtx_id *dti_cos, struct dtx_handle *dth);
int
dtx_end(struct dtx_handle *dth, struct ds_cont_hdl *cont_hdl,
	struct ds_cont_child *cont, int result);

int dtx_leader_exec_ops(struct dtx_leader_handle *dth, dtx_sub_func_t exec_func,
			void *func_arg);

int dtx_batched_commit_register(struct ds_cont_child *cont);

void dtx_batched_commit_deregister(struct ds_cont_child *cont);

int dtx_obj_sync(uuid_t po_uuid, uuid_t co_uuid, daos_handle_t coh,
		 daos_unit_oid_t oid, daos_epoch_t epoch, uint32_t map_ver);

/**
 * Check whether the given DTX is resent one or not.
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	Pointer to the object ID.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param epoch		[IN,OUT] Pointer to current epoch, if it is zero and
 *				 if the DTX exists, then the DTX's epoch will
 *				 be saved in it.
 *
 * \return		0		means that the DTX has been 'prepared',
 *					so the local modification has been done
 *					on related replica(s).
 *			-DER_ALREADY	means the DTX has been committed or is
 *					committable.
 *			-DER_MISMATCH	means that the DTX has ever been
 *					processed with different epoch.
 *			Other negative value if error.
 */
int dtx_handle_resend(daos_handle_t coh, daos_obj_id_t *oid,
		      struct dtx_id *dti, uint64_t dkey_hash,
		      daos_epoch_t *epoch);

/* XXX: The higher 48 bits of HLC is the wall clock, the lower bits are for
 *	logic clock that will be hidden when divided by NSEC_PER_SEC.
 */
static inline uint64_t
dtx_hlc_age2sec(uint64_t hlc)
{
	return (crt_hlc_get() - hlc) / NSEC_PER_SEC;
}

struct dtx_resync_arg {
	uuid_t		pool_uuid;
	uint32_t	version;
};

/* resync all dtx inside the pool */
void
dtx_resync_ult(void *arg);
#endif /* __DAOS_DTX_SRV_H__ */
