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

/**
 * DAOS two-phase commit transaction handle in DRAM.
 */
struct dtx_handle {
	union {
		struct dtx_entry		 dth_dte;
		struct {
			/** The identifier of the DTX. */
			struct dtx_id		 dth_xid;
			/** Pool map version. */
			uint32_t		 dth_ver;
			/** Match dtx_entry::dte_refs. */
			uint32_t		 dth_refs;
			/** The DTX participants information. */
			struct dtx_memberships	*dth_mbs;
		};
	};
	/** The container handle */
	daos_handle_t			 dth_coh;
	/** The epoch# for the DTX. */
	daos_epoch_t			 dth_epoch;
	/**
	 * The upper bound of the epoch uncertainty. dth_epoch_bound ==
	 * dth_epoch means that dth_epoch has no uncertainty.
	 */
	daos_epoch_t			 dth_epoch_bound;
	/**
	 * The object ID is used to elect the DTX leader,
	 * mainly used for CoS (for single RDG case) and DTX recovery.
	 */
	daos_unit_oid_t			 dth_leader_oid;

	uint32_t			 dth_sync:1, /* commit synchronously. */
					 dth_resent:1, /* For resent case. */
					 /* Only one participator in the DTX. */
					 dth_solo:1,
					 /* Modified shared items: object/key */
					 dth_modify_shared:1,
					 /* The DTX entry is in active table. */
					 dth_active:1,
					 /* Leader oid is touched. */
					 dth_touched_leader_oid:1,
					 /* Local TX is started. */
					 dth_local_tx_started:1,
					 /* Retry with this server. */
					 dth_local_retry:1;

	/* The count the DTXs in the dth_dti_cos array. */
	uint32_t			 dth_dti_cos_count;
	/* The array of the DTXs for Commit on Share (conflcit). */
	struct dtx_id			*dth_dti_cos;
	/** Pointer to the DTX entry in DRAM. */
	void				*dth_ent;
	/** The flags, see dtx_entry_flags. */
	uint32_t			 dth_flags;
	/** The count of reserved items in the dth_rsrvds array. */
	uint16_t			 dth_rsrvd_cnt;
	uint16_t			 dth_deferred_cnt;
	/** The total sub modifications count. */
	uint16_t			 dth_modification_cnt;
	/** Modification sequence in the distributed transaction. */
	uint16_t			 dth_op_seq;

	/** The count of objects that are modified by this DTX. */
	uint16_t			 dth_oid_cnt;
	/** The total slots in the dth_oid_array. */
	uint16_t			 dth_oid_cap;
	/** If more than one objects are modified, the IDs are reocrded here. */
	daos_unit_oid_t			*dth_oid_array;

	/* Hash of the dkey to be modified if applicable. Per modification. */
	uint64_t			 dth_dkey_hash;

	struct dtx_rsrvd_uint		 dth_rsrvd_inline;
	struct dtx_rsrvd_uint		*dth_rsrvds;
	void				**dth_deferred;
	/* NVME extents to release */
	d_list_t			dth_deferred_nvme;
};

/* Each sub transaction handle to manage each sub thandle */
struct dtx_sub_status {
	struct daos_shard_tgt		dss_tgt;
	int				dss_result;
};

struct dtx_leader_handle;
typedef int (*dtx_agg_cb_t)(struct dtx_leader_handle *dlh, void *arg);
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
	dtx_agg_cb_t			dlh_agg_cb;
	void				*dlh_agg_cb_arg;
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
dtx_sub_init(struct dtx_handle *dth, daos_unit_oid_t *oid, uint64_t dkey_hash);
int
dtx_leader_begin(struct ds_cont_child *cont, struct dtx_id *dti,
		 struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
		 uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt,
		 struct daos_shard_tgt *tgts, int tgt_cnt, bool solo, bool sync,
		 struct dtx_memberships *mbs, struct dtx_leader_handle *dlh);
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont,
	       int result);

typedef void (*dtx_sub_comp_cb_t)(struct dtx_leader_handle *dlh, int idx,
				  int rc);
typedef int (*dtx_sub_func_t)(struct dtx_leader_handle *dlh, void *arg, int idx,
			      dtx_sub_comp_cb_t comp_cb);

int
dtx_begin(struct ds_cont_child *cont, struct dtx_id *dti,
	  struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
	  uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt,
	  struct dtx_memberships *mbs, struct dtx_handle *dth);
int
dtx_end(struct dtx_handle *dth, struct ds_cont_child *cont, int result);
int
dtx_list_cos(struct ds_cont_child *cont, daos_unit_oid_t *oid,
	     uint64_t dkey_hash, int max, struct dtx_id **dtis);
int
dtx_leader_exec_ops(struct dtx_leader_handle *dlh, dtx_sub_func_t func,
		    dtx_agg_cb_t agg_cb, void *agg_cb_arg, void *func_arg);

int dtx_batched_commit_register(struct ds_cont_child *cont);

void dtx_batched_commit_deregister(struct ds_cont_child *cont);

int dtx_obj_sync(uuid_t po_uuid, uuid_t co_uuid, struct ds_cont_child *cont,
		 daos_unit_oid_t *oid, daos_epoch_t epoch);

/**
 * Check whether the given DTX is resent one or not.
 *
 * \param coh		[IN]	Container open handle.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param epoch		[IN,OUT] Pointer to current epoch, if it is zero and
 *				 if the DTX exists, then the DTX's epoch will
 *				 be saved in it.
 * \param mp_ver	[OUT]	Hold the DTX pool map version.
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
int dtx_handle_resend(daos_handle_t coh, struct dtx_id *dti,
		      daos_epoch_t *epoch, uint32_t *pm_ver);

static inline uint64_t
dtx_hlc_age2sec(uint64_t hlc)
{
	uint64_t now = crt_hlc_get();

	if (now <= hlc)
		return 0;

	return crt_hlc2sec(now - hlc);
}

static inline struct dtx_entry *
dtx_entry_get(struct dtx_entry *dte)
{
	dte->dte_refs++;
	return dte;
}

static inline void
dtx_entry_put(struct dtx_entry *dte)
{
	if (--(dte->dte_refs) == 0)
		D_FREE(dte);
}

static inline bool
dtx_is_valid_handle(struct dtx_handle *dth)
{
	return dth != NULL && !daos_is_zero_dti(&dth->dth_xid);
}

struct dtx_scan_args {
	uuid_t		pool_uuid;
	uint32_t	version;
};

int dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid,
	       uint32_t ver, bool block, bool resync_all);
void dtx_resync_ult(void *arg);

#endif /* __DAOS_DTX_SRV_H__ */
