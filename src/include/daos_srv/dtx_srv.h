/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DTX_SRV_H__
#define __DAOS_DTX_SRV_H__

#include <daos/mem.h>
#include <daos/dtx.h>
#include <daos/placement.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>

#define DTX_REFRESH_MAX 4

struct dtx_share_peer {
	d_list_t		dsp_link;
	struct dtx_id		dsp_xid;
	daos_unit_oid_t		dsp_oid;
	daos_epoch_t		dsp_epoch;
	uint64_t		dsp_dkey_hash;
	struct dtx_memberships	dsp_mbs;
};

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
					 /* Pin the DTX entry in DRAM. */
					 dth_pinned:1,
					 /* DTXs in CoS list are committed. */
					 dth_cos_done:1,
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
					 dth_local_retry:1,
					 /* The DTX share lists are inited. */
					 dth_shares_inited:1,
					 /* Distributed transaction. */
					 dth_dist:1,
					 /* For data migration. */
					 dth_for_migration:1,
					 /* Force refresh for non-committed */
					 dth_force_refresh:1,
					 /* Has prepared locally, for resend. */
					 dth_prepared:1,
					 /* Ignore other uncommitted DTXs. */
					 dth_ignore_uncommitted:1;

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
	d_list_t			 dth_deferred_nvme;
	/* Committed or comittable DTX list */
	d_list_t			 dth_share_cmt_list;
	/* Aborted DTX list */
	d_list_t			 dth_share_abt_list;
	/* Active DTX list */
	d_list_t			 dth_share_act_list;
	/* DTX list to be checked */
	d_list_t			 dth_share_tbd_list;
	int				 dth_share_tbd_count;
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
	uint64_t	dtx_oldest_active_time;
	/* The epoch for the oldest entry in the 1st committed blob. */
	uint64_t	dtx_first_cmt_blob_time_up;
	/* The epoch for the newest entry in the 1st committed blob. */
	uint64_t	dtx_first_cmt_blob_time_lo;
	/* container-based committed DTX entries count. */
	uint32_t	dtx_cont_cmt_count;
	/* pool-based committed DTX entries count. */
	uint32_t	dtx_pool_cmt_count;
};

enum dtx_flags {
	/** Single operand. */
	DTX_SOLO		= (1 << 0),
	/** Sync mode transaction. */
	DTX_SYNC		= (1 << 1),
	/** Distributed transaction.  */
	DTX_DIST		= (1 << 2),
	/** For data migration. */
	DTX_FOR_MIGRATION	= (1 << 3),
	/** Ignore other uncommitted DTXs. */
	DTX_IGNORE_UNCOMMITTED	= (1 << 4),
	/** Resent request. */
	DTX_RESEND		= (1 << 5),
	/** Force DTX refresh if hit non-committed DTX on non-leader. */
	DTX_FORCE_REFRESH	= (1 << 6),
	/** Transaction has been prepared locally. */
	DTX_PREPARED		= (1 << 7),
};

int
dtx_sub_init(struct dtx_handle *dth, daos_unit_oid_t *oid, uint64_t dkey_hash);
int
dtx_leader_begin(daos_handle_t coh, struct dtx_id *dti,
		 struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
		 uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt,
		 struct daos_shard_tgt *tgts, int tgt_cnt, uint32_t flags,
		 struct dtx_memberships *mbs, struct dtx_leader_handle *dlh);
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont,
	       int result);

typedef void (*dtx_sub_comp_cb_t)(struct dtx_leader_handle *dlh, int idx,
				  int rc);
typedef int (*dtx_sub_func_t)(struct dtx_leader_handle *dlh, void *arg, int idx,
			      dtx_sub_comp_cb_t comp_cb);

int
dtx_begin(daos_handle_t coh, struct dtx_id *dti,
	  struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
	  uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt, uint32_t flags,
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

int dtx_obj_sync(struct ds_cont_child *cont, daos_unit_oid_t *oid,
		 daos_epoch_t epoch);

int dtx_abort(struct ds_cont_child *cont, daos_epoch_t epoch,
	      struct dtx_entry **dtes, int count);
int dtx_refresh(struct dtx_handle *dth, struct ds_cont_child *cont);

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
 *			-DER_DATA_LOSS	means that related DTX is marked as
 *					'corrupted', not sure whether former
 *					sent has even succeed or not.
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
dtx_is_valid_handle(const struct dtx_handle *dth)
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
