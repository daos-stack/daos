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
#include <daos_srv/vos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>

struct dtx_entry {
	/** The identifier of the DTX */
	struct dtx_id		dte_xid;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t		dte_oid;
};

enum dtx_cos_list_types {
	DCLT_UPDATE		= (1 << 0),
	DCLT_PUNCH		= (1 << 1),
};

/**
 * DAOS two-phase commit transaction handle in DRAM.
 */
struct dtx_handle {
	union {
		struct {
			/** The identifier of the DTX */
			struct dtx_id		dth_xid;
			/** The identifier of the shard to be modified. */
			daos_unit_oid_t		dth_oid;
		};
		struct dtx_entry		dth_dte;
	};
	/** The container handle */
	daos_handle_t			 dth_coh;
	/** The epoch# for the DTX. */
	daos_epoch_t			 dth_epoch;
	/* The generation when the DTX is handled on the server. */
	uint64_t			 dth_gen;
	/** The {obj/dkey/akey}-tree records that are created
	 * by other DTXs, but not ready for commit yet.
	 */
	d_list_t			 dth_shares;
	/* The hash of the dkey to be modified if applicable */
	uint64_t			 dth_dkey_hash;
	/** Pool map version. */
	uint32_t			 dth_ver;
	/** The intent of related modification. */
	uint32_t			 dth_intent;
	uint32_t			 dth_sync:1, /* commit synchronously. */
					 dth_leader:1, /* leader replica. */
					 /* Only one participator in the DTX. */
					 dth_single_participator:1,
					 /* dti_cos has been committed. */
					 dth_dti_cos_done:1;
	/* The count the DTXs in the dth_dti_cos array. */
	uint32_t			 dth_dti_cos_count;
	/* The array of the DTXs for Commit on Share (conflcit). */
	struct dtx_id			*dth_dti_cos;
	/* The identifier of the DTX that conflict with current one. */
	struct dtx_conflict_entry	*dth_conflict;
	/** The address of the DTX entry in SCM. */
	umem_off_t			 dth_ent;
	/** The address (offset) of the (new) object to be modified. */
	umem_off_t			 dth_obj;
};

/* Each sub transaction handle to manage each sub thandle */
struct dtx_sub_status {
	struct daos_shard_tgt		dss_tgt;
	struct dtx_conflict_entry	dss_dce;
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

struct dtx_share {
	/** Link into the dtx_handle::dth_shares */
	d_list_t		dts_link;
	/** The DTX record type. */
	uint32_t		dts_type;
	/** The record in the related tree in SCM. */
	umem_off_t		dts_record;
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

int
dtx_leader_begin(struct dtx_id *dti, daos_unit_oid_t *oid, daos_handle_t coh,
		 daos_epoch_t epoch, uint64_t dkey_hash, uint32_t pm_ver,
		 uint32_t intent, struct daos_shard_tgt *tgts, int tgts_cnt,
		 struct dtx_leader_handle *dlh);
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_hdl *cont_hdl,
	       struct ds_cont_child *cont, int result);

typedef void (*dtx_sub_comp_cb_t)(struct dtx_leader_handle *dlh, int idx,
				  int rc);
typedef int (*dtx_sub_func_t)(struct dtx_leader_handle *dlh, void *arg, int idx,
			      dtx_sub_comp_cb_t comp_cb);

int dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid,
	       uint32_t ver, bool block);
int
dtx_begin(struct dtx_id *dti, daos_unit_oid_t *oid, daos_handle_t coh,
	  daos_epoch_t epoch, uint64_t dkey_hash,
	  struct dtx_conflict_entry *conflict, struct dtx_id *dti_cos,
	  int dti_cos_cnt, uint32_t pm_ver, uint32_t intent,
	  struct dtx_handle *dth);
int
dtx_end(struct dtx_handle *dth, struct ds_cont_hdl *cont_hdl,
	struct ds_cont_child *cont, int result);

int dtx_leader_exec_ops(struct dtx_leader_handle *dth, dtx_sub_func_t exec_func,
			void *func_arg);

int dtx_batched_commit_register(struct ds_cont_hdl *hdl);

void dtx_batched_commit_deregister(struct ds_cont_hdl *hdl);

int dtx_obj_sync(uuid_t po_uuid, uuid_t co_uuid, daos_handle_t coh,
		 daos_unit_oid_t oid, daos_epoch_t epoch, uint32_t map_ver);

/**
 * Check whether the given DTX is resent one or not.
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	Pointer to the object ID.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param punch		[IN]	For punch operation or not.
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
int dtx_handle_resend(daos_handle_t coh, daos_unit_oid_t *oid,
		      struct dtx_id *dti, uint64_t dkey_hash,
		      bool punch, daos_epoch_t *epoch);

/* XXX: The higher 48 bits of HLC is the wall clock, the lower bits are for
 *	logic clock that will be hidden when divided by NSEC_PER_SEC.
 */
static inline uint64_t
dtx_hlc_age2sec(uint64_t hlc)
{
	return (crt_hlc_get() - hlc) / NSEC_PER_SEC;
}

static inline bool
dtx_is_null(umem_off_t umoff)
{
	return umoff == UMOFF_NULL;
}

#endif /* __DAOS_DTX_SRV_H__ */
