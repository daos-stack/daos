/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	d_list_t		 dsp_link;
	struct dtx_id		 dsp_xid;
	daos_unit_oid_t		 dsp_oid;
	daos_epoch_t		 dsp_epoch;
	uint64_t		 dsp_dkey_hash;
	int			 dsp_status;
	uint32_t		 dsp_version;
	uint32_t		 dsp_inline_mbs:1;
	struct dtx_memberships	*dsp_mbs;
};

/** A single record for tracking object IDs from more than one container. */
struct dtx_local_oid_record {
	/** The container the object belongs to. */
	struct vos_container *dor_cont;
	/** Object ID */
	daos_unit_oid_t       dor_oid;
};

/**
 * DAOS two-phase commit transaction handle in DRAM.
 *
 * There may be many instances of this particular structure at runtime in DRAM.
 * So its size has to be well looked after.
 *
 * Please limit the amount of necessary padding by ordering the fields in
 * the most optimal way (packed). Please make sure that all necessary padding
 * is explicit so it could be used in the future.
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
	union {
		/** The container handle or pool handle (local transactions only) */
		daos_handle_t dth_coh;
		daos_handle_t dth_poh;
	};
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

	uint32_t                         dth_sync : 1, /* commit synchronously. */
	    /* Pin the DTX entry in DRAM. */
	    dth_pinned                            : 1,
	    /* DTXs in CoS list are committed. */
	    dth_cos_done                          : 1,
	    /* Only one participator in the DTX. */
	    dth_solo                              : 1,
	    /* Do not keep committed entry. */
	    dth_drop_cmt                          : 1,
	    /* Modified shared items: object/key */
	    dth_modify_shared                     : 1,
	    /* The DTX entry is in active table. */
	    dth_active                            : 1,
	    /* Leader oid is touched. */
	    dth_touched_leader_oid                : 1,
	    /* Local TX is started. */
	    dth_local_tx_started                  : 1,
	    /* The DTX share lists are inited. */
	    dth_shares_inited                     : 1,
	    /* Distributed transaction. */
	    dth_dist                              : 1,
	    /* For data migration. */
	    dth_for_migration                     : 1,
	    /* Has prepared locally, for resend. */
	    dth_prepared                          : 1,
	    /* The DTX handle is aborted. */
	    dth_aborted                           : 1,
	    /* The modification is done by others. */
	    dth_already                           : 1,
	    /* Need validation on leader before commit/committable. */
	    dth_need_validation                   : 1,
	    /* Ignore other uncommitted DTXs. */
	    dth_ignore_uncommitted                : 1,
	    /* Local transaction */
	    dth_local                             : 1,
	    /* Locally generate the epoch. */
	    dth_epoch_owner			  : 1,
	    /* Flag to commit the local transaction */
	    dth_local_complete : 1, padding1 : 12;

	/* The count the DTXs in the dth_dti_cos array. */
	uint32_t			 dth_dti_cos_count;
	/* The array of the DTXs for Commit on Share (conflict). */
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

	uint16_t			 dth_deferred_used_cnt;
	uint16_t                         padding2;

	union {
		struct {
			/** The count of objects that are modified by this DTX. */
			uint16_t         dth_oid_cnt;
			/** The total slots in the dth_oid_array. */
			uint16_t         dth_oid_cap;
			uint32_t         padding3;
			/** If more than one objects are modified, the IDs are reocrded here. */
			daos_unit_oid_t *dth_oid_array;
		};
		struct {
			/** The count of objects stored in dth_local_oid_array. */
			uint16_t                     dth_local_oid_cnt;
			/** The total slots in the dth_local_oid_array. */
			uint16_t                     dth_local_oid_cap;
			uint32_t                     padding4;
			/** The record of all objects touched by the local transaction. */
			struct dtx_local_oid_record *dth_local_oid_array;
		};
	};

	/* Hash of the dkey to be modified if applicable. Per modification. */
	uint64_t			 dth_dkey_hash;

	struct dtx_rsrvd_uint		 dth_rsrvd_inline;
	struct dtx_rsrvd_uint		*dth_rsrvds;
	void				**dth_deferred;
	void				*dth_local_stub;
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
	int                               dth_share_tbd_count;
	uint32_t                          padding5;
};

/* Each sub transaction handle to manage each sub thandle */
struct dtx_sub_status {
	struct daos_shard_tgt		dss_tgt;
	int				dss_result;
	uint32_t			dss_version;
	uint32_t			dss_comp:1;
	void				*dss_data;
};

struct dtx_coll_entry {
	struct dtx_id			 dce_xid;
	uint32_t			 dce_ver;
	uint32_t                         dce_min_rank;
	uint32_t                         dce_max_rank;
	uint32_t			 dce_refs;
	d_rank_list_t			*dce_ranks;
	uint8_t				*dce_hints;
	uint8_t				*dce_bitmap;
	uint32_t			 dce_hint_sz;
	uint32_t			 dce_bitmap_sz;
};

struct dtx_leader_handle;
typedef int (*dtx_agg_cb_t)(struct dtx_leader_handle *dlh, void *arg);

/* Transaction handle on the leader node to manage the transaction */
struct dtx_leader_handle {
	/* The dtx handle on the leader node */
	struct dtx_handle		dlh_handle;
	/* result for the distribute transaction */
	int				dlh_result;
	/* The known latest pool map version from remote targets. */
	uint32_t			dlh_rmt_ver;
	/* For 64-bits alignment. */
	uint32_t			dlh_padding;
	/* The array of the DTX COS entries */
	uint32_t			dlh_dti_cos_count;
	struct dtx_id			*dlh_dti_cos;

	/* The future to wait for sub requests to finish. */
	ABT_future			dlh_future;

	int32_t				dlh_allow_failure;
					/* Normal sub requests have been processed. */
	uint32_t			dlh_normal_sub_done:1,
					dlh_need_agg:1,
					dlh_agg_done:1,
					/* For collective DTX. */
					dlh_coll:1,
					/* Only forward RPC, but neither commit nor abort DTX. */
					dlh_relay:1,
					/* Drop conditional flags when forward RPC. */
					dlh_drop_cond:1;
	/* Elements for collective DTX. */
	struct dtx_coll_entry		*dlh_coll_entry;
	/* How many normal sub request. */
	int32_t                          dlh_normal_sub_cnt;
	/* How many delay forward sub request. */
	int32_t                          dlh_delay_sub_cnt;
	/* The index of the first target that forward sub-request to. */
	int32_t                          dlh_forward_idx;
	/* The count of the targets that forward sub-request to. */
	int32_t                          dlh_forward_cnt;
	/* Sub transaction handle to manage the dtx leader */
	struct dtx_sub_status		*dlh_subs;
};

_Static_assert(sizeof(struct dtx_leader_handle) == 360,
	       "The size of this structure may be tracked by other modules e.g. telemetry");

struct dtx_stat {
	uint32_t	dtx_committable_count;
	uint32_t	dtx_committable_coll_count;
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
	/* The epoch for the most new DTX entry that is aggregated. */
	uint64_t	dtx_newest_aggregated;
};

enum dtx_flags {
	/** Single operand. */
	DTX_SOLO = (1 << 0),
	/** Sync mode transaction. */
	DTX_SYNC = (1 << 1),
	/** Distributed transaction.  */
	DTX_DIST = (1 << 2),
	/** For data migration. */
	DTX_FOR_MIGRATION = (1 << 3),
	/** Ignore other uncommitted DTXs. */
	DTX_IGNORE_UNCOMMITTED = (1 << 4),
	/** Resent request. Out-of-date. */
	DTX_RESEND = (1 << 5),
	/** Force DTX refresh if hit non-committed DTX on non-leader. Out-of-date DAOS-7878. */
	DTX_FORCE_REFRESH = (1 << 6),
	/** Transaction has been prepared locally. */
	DTX_PREPARED = (1 << 7),
	/** Do not keep committed entry. */
	DTX_DROP_CMT = (1 << 8),
	/* The non-leader targets are collective. */
	DTX_TGT_COLL = (1 << 9),
	/* Not real DTX leader, Only forward IO to others, but neither commit nor abort DTX. */
	DTX_RELAY = (1 << 10),
	/** Local transaction */
	DTX_LOCAL = (1 << 11),
	/** Locally generate the epoch. */
	DTX_EPOCH_OWNER = (1 << 12),
};

void
dtx_renew_epoch(struct dtx_epoch *epoch, struct dtx_handle *dth);
int
dtx_sub_init(struct dtx_handle *dth, daos_unit_oid_t *oid, uint64_t dkey_hash);
int
dtx_leader_begin(daos_handle_t coh, struct dtx_id *dti, struct dtx_epoch *epoch,
		 uint16_t sub_modification_cnt, uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt, struct daos_shard_tgt *tgts, int tgt_cnt,
		 uint32_t flags, struct dtx_memberships *mbs, struct dtx_coll_entry *dce,
		 struct dtx_leader_handle **p_dlh);
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont, int result);

typedef void (*dtx_sub_comp_cb_t)(struct dtx_leader_handle *dlh, int idx,
				  int rc);
typedef int (*dtx_sub_func_t)(struct dtx_leader_handle *dlh, void *arg, int idx,
			      dtx_sub_comp_cb_t comp_cb);

int
dtx_begin(daos_handle_t xoh, struct dtx_id *dti, struct dtx_epoch *epoch,
	  uint16_t sub_modification_cnt, uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt, uint32_t flags, struct dtx_memberships *mbs,
	  struct dtx_handle **p_dth);
int
dtx_end(struct dtx_handle *dth, struct ds_cont_child *cont, int result);
int
dtx_cos_get_piggyback(struct ds_cont_child *cont, daos_unit_oid_t *oid, uint64_t dkey_hash,
		      int max, struct dtx_id **dtis);
void
dtx_cos_put_piggyback(struct ds_cont_child *cont, daos_unit_oid_t *oid, uint64_t dkey_hash,
		      struct dtx_id xid[], uint32_t count, bool rm);
int
dtx_leader_exec_ops(struct dtx_leader_handle *dlh, dtx_sub_func_t func,
		    dtx_agg_cb_t agg_cb, int allow_failure, void *func_arg);

int dtx_cont_open(struct ds_cont_child *cont);

void dtx_cont_close(struct ds_cont_child *cont, bool force);

int dtx_cont_register(struct ds_cont_child *cont);

void dtx_cont_deregister(struct ds_cont_child *cont);

void stop_dtx_reindex_ult(struct ds_cont_child *cont, bool force);

int dtx_obj_sync(struct ds_cont_child *cont, daos_unit_oid_t *oid,
		 daos_epoch_t epoch);

int dtx_commit(struct ds_cont_child *cont, struct dtx_entry **dtes,
	       struct dtx_cos_key *dcks, int count, bool has_cos);

int dtx_abort(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch);

int dtx_refresh(struct dtx_handle *dth, struct ds_cont_child *cont);

int
dtx_coll_commit(struct ds_cont_child *cont, struct dtx_coll_entry *dce, struct dtx_cos_key *dck,
		bool has_cos);

int
dtx_coll_abort(struct ds_cont_child *cont, struct dtx_coll_entry *dce, daos_epoch_t epoch);

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

static inline struct dtx_coll_entry *
dtx_coll_entry_get(struct dtx_coll_entry *dce)
{
	dce->dce_refs++;
	return dce;
}

static inline void
dtx_coll_entry_put(struct dtx_coll_entry *dce)
{
	if (dce != NULL && --(dce->dce_refs) == 0) {
		d_rank_list_free(dce->dce_ranks);
		D_FREE(dce->dce_bitmap);
		D_FREE(dce->dce_hints);
		D_FREE(dce);
	}
}

static inline void
dtx_dsp_free(struct dtx_share_peer *dsp)
{
	if (dsp->dsp_inline_mbs == 0)
		D_FREE(dsp->dsp_mbs);

	D_FREE(dsp);
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

/** Return true if it's a real dtx (valid and not a local transaction) */
static inline bool
dtx_is_real_handle(const struct dtx_handle *dth)
{
	return dth != NULL && !daos_is_zero_dti(&dth->dth_xid) && !dth->dth_local;
}

struct dtx_scan_args {
	uuid_t		pool_uuid;
	uint32_t	version;
};

int dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid, uint32_t ver, bool block);
void dtx_resync_ult(void *arg);

#endif /* __DAOS_DTX_SRV_H__ */
