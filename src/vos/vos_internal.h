/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Layout definition for VOS root object
 * vos/vos_internal.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef __VOS_INTERNAL_H__
#define __VOS_INTERNAL_H__

#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos/common.h>
#include <daos/lru.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/bio.h>
#include <daos_srv/vos.h>
#include "vos_tls.h"
#include "vos_layout.h"
#include "vos_ilog.h"
#include "vos_obj.h"

#define VOS_MINOR_EPC_MAX EVT_MINOR_EPC_MAX

#define VOS_TX_LOG_FAIL(rc, ...)                                                                   \
	do {                                                                                       \
		bool __is_err = true;                                                              \
                                                                                                   \
		if (rc >= 0)                                                                       \
			break;                                                                     \
		switch (rc) {                                                                      \
		case -DER_TX_RESTART:                                                              \
		case -DER_INPROGRESS:                                                              \
		case -DER_UPDATE_AGAIN:                                                            \
		case -DER_BUSY:                                                                    \
		case -DER_EXIST:                                                                   \
		case -DER_NONEXIST:                                                                \
			__is_err = false;                                                          \
			break;                                                                     \
		}                                                                                  \
		D_CDEBUG(__is_err, DLOG_ERR, DB_IO, __VA_ARGS__);                                  \
	} while (0)

#define VOS_TX_TRACE_FAIL(rc, ...)                                                                 \
	do {                                                                                       \
		bool __is_err = true;                                                              \
                                                                                                   \
		if (rc >= 0)                                                                       \
			break;                                                                     \
		switch (rc) {                                                                      \
		case -DER_TX_RESTART:                                                              \
		case -DER_INPROGRESS:                                                              \
		case -DER_UPDATE_AGAIN:                                                            \
		case -DER_BUSY:                                                                    \
		case -DER_EXIST:                                                                   \
		case -DER_NONEXIST:                                                                \
			__is_err = false;                                                          \
			break;                                                                     \
		}                                                                                  \
		D_CDEBUG(__is_err, DLOG_ERR, DB_TRACE, __VA_ARGS__);                               \
	} while (0)

#define VOS_CONT_ORDER		20	/* Order of container tree */
#define VOS_OBJ_ORDER           15      /* Order of object tree */
#define VOS_KTR_ORDER           20      /* Order of d/a-key tree */
#define VOS_SVT_ORDER           5       /* Order of single value tree */
#define VOS_EVT_ORDER           15      /* Order of evtree */
#define DTX_BTREE_ORDER         23      /* Order for DTX tree */
#define VEA_TREE_ODR		20	/* Order of a VEA tree */

extern struct dss_module_key vos_module_key;

#define VOS_POOL_HHASH_BITS 10 /* Up to 1024 pools */
#define VOS_CONT_HHASH_BITS 20 /* Up to 1048576 containers */

#define VOS_BLK_SHIFT		12	/* 4k */
#define VOS_BLK_SZ		(1UL << VOS_BLK_SHIFT) /* bytes */
#define VOS_BLOB_HDR_BLKS	1	/* block */

/** Up to 1 million lid entries split into 2048 expansion slots */
#define DTX_ARRAY_LEN		(1 << 20) /* Total array slots for DTX lid */
#define DTX_ARRAY_NR		(1 << 11)  /* Number of expansion arrays */

enum {
	/** Used for marking an in-tree record committed */
	DTX_LID_COMMITTED = 0,
	/** Used for marking an in-tree record aborted */
	DTX_LID_ABORTED,
	/** Reserved local ids */
	DTX_LID_RESERVED,
};

/**
 * If the highest bit (31th) of the DTX entry offset is set, then it is for
 * solo (single modification against single replicated object) transaction.
 */
#define DTX_LID_SOLO_BITS	31
#define DTX_LID_SOLO_FLAG	(1UL << DTX_LID_SOLO_BITS)
#define DTX_LID_SOLO_MASK	(DTX_LID_SOLO_FLAG - 1)

/*
 * When aggregate merge window reaches this size threshold, it will stop
 * growing and trigger window flush immediately.
 */
#define VOS_MW_FLUSH_THRESH	(1UL << 23)	/* 8MB */

/*
 * Default size (in blocks) threshold for merging NVMe records, we choose
 * 256 blocks as default value since the default DFS chunk size is 1MB.
 */
#define VOS_MW_NVME_THRESH	256		/* 256 * VOS_BLK_SZ = 1MB */

/*
 * Aggregation/Discard ULT yield when certain amount of credits consumed.
 *
 * More credits are used in tight mode to reduce re-probe on iterating;
 * Fewer credits are used in slack mode to avoid io performance fluctuation;
 */
enum {
	/* Maximum scans for tight mode */
	AGG_CREDS_SCAN_TIGHT	= 64,
	/* Maximum scans for slack mode */
	AGG_CREDS_SCAN_SLACK	= 32,
	/* Maximum obj/key/rec deletions for tight mode */
	AGG_CREDS_DEL_TIGHT	= 16,
	/* Maximum obj/key/rec deletions for slack mode */
	AGG_CREDS_DEL_SLACK	= 4,
	/* Maximum # of mw flush for tight mode */
	AGG_CREDS_MERGE_TIGHT	= 8,
	/* Maximum # of mw flush for slack mode */
	AGG_CREDS_MERGE_SLACK	= 2,
};

/* Throttle ENOSPACE error message */
#define VOS_NOSPC_ERROR_INTVL	60	/* seconds */

extern uint32_t vos_agg_gap;

#define VOS_AGG_GAP_MIN		20 /* seconds */
#define VOS_AGG_GAP_DEF         20
#define VOS_AGG_GAP_MAX		180

extern unsigned int vos_agg_nvme_thresh;
extern bool vos_dkey_punch_propagate;
extern bool vos_skip_old_partial_dtx;

static inline uint32_t vos_byte2blkcnt(uint64_t bytes)
{
	D_ASSERT(bytes != 0);
	return (bytes + VOS_BLK_SZ - 1) >> VOS_BLK_SHIFT;
}

static inline uint64_t vos_byte2blkoff(uint64_t bytes)
{
	D_ASSERT(bytes != 0);
	D_ASSERTF((bytes >> VOS_BLK_SHIFT) > 0, ""DF_U64"\n", bytes);
	D_ASSERTF(!(bytes & ((uint64_t)VOS_BLK_SZ - 1)), ""DF_U64"\n", bytes);
	return bytes >> VOS_BLK_SHIFT;
}

static inline void
agg_reserve_space(daos_size_t *rsrvd)
{
	daos_size_t	size = VOS_MW_FLUSH_THRESH * 5;

	rsrvd[DAOS_MEDIA_SCM]	+= size;
	rsrvd[DAOS_MEDIA_NVME]	+= size;
}

enum {
	AGG_OP_SCAN = 0,	/* scanned obj/dkey/akey */
	AGG_OP_SKIP,		/* skipped obj/dkey/akey */
	AGG_OP_DEL,		/* deleted obj/dkey/akey */
	/* Not used in metrics, must be the last item */
	AGG_OP_MERGE,		/* records merge operation */
	AGG_OP_MAX,
};

struct vos_agg_metrics {
	struct d_tm_node_t	*vam_epr_dur;		/* EPR(Epoch Range) scan duration */
	struct d_tm_node_t	*vam_obj[AGG_OP_MERGE];
	struct d_tm_node_t	*vam_dkey[AGG_OP_MERGE];
	struct d_tm_node_t	*vam_akey[AGG_OP_MERGE];
	struct d_tm_node_t	*vam_uncommitted;	/* Hit uncommitted entries */
	struct d_tm_node_t	*vam_csum_errs;		/* Hit CSUM errors */
	struct d_tm_node_t	*vam_del_sv;		/* Deleted SV records */
	struct d_tm_node_t	*vam_del_ev;		/* Deleted EV records */
	struct d_tm_node_t	*vam_merge_recs;	/* Total merged EV records */
	struct d_tm_node_t	*vam_merge_size;	/* Total merged size */
	struct d_tm_node_t	*vam_fail_count;	/* Aggregation failed */
	struct d_tm_node_t      *vam_agg_blocked;       /* Aggregation waiting for discard */
	struct d_tm_node_t      *vam_discard_blocked;   /* Discard waiting for aggregation */
};

struct vos_gc_metrics {
	struct d_tm_node_t *vgm_duration;  /* Duration of each gc scan */
	struct d_tm_node_t *vgm_cont_del;  /* containers reclaimed */
	struct d_tm_node_t *vgm_obj_del;   /* objects reclaimed */
	struct d_tm_node_t *vgm_dkey_del;  /* dkeys reclaimed */
	struct d_tm_node_t *vgm_akey_del;  /* akeys reclaimed */
	struct d_tm_node_t *vgm_ev_del;    /* EV records reclaimed */
	struct d_tm_node_t *vgm_sv_del;    /* SV records reclaimed */
	struct d_tm_node_t *vgm_slack_cnt; /* Slack mode count */
	struct d_tm_node_t *vgm_tight_cnt; /* Tight mode count */
};

/*
 * VOS Pool metrics for checkpoint activity.
 */
struct vos_chkpt_metrics {
	struct d_tm_node_t	*vcm_duration;
	struct d_tm_node_t	*vcm_dirty_pages;
	struct d_tm_node_t	*vcm_dirty_chunks;
	struct d_tm_node_t	*vcm_iovs_copied;
	struct d_tm_node_t	*vcm_wal_purged;
};

void vos_chkpt_metrics_init(struct vos_chkpt_metrics *vc_metrics, const char *path, int tgt_id);
void
vos_gc_metrics_init(struct vos_gc_metrics *vc_metrics, const char *path, int tgt_id);

struct vos_space_metrics {
	struct d_tm_node_t	*vsm_scm_used;		/* SCM space used */
	struct d_tm_node_t	*vsm_nvme_used;		/* NVMe space used */
	struct d_tm_node_t      *vsm_scm_total;         /* SCM space total */
	struct d_tm_node_t      *vsm_nvme_total;        /* NVMe space total */
	uint64_t		 vsm_last_update_ts;	/* Timeout counter */
};

/* VOS Pool metrics for WAL */
struct vos_wal_metrics {
	struct d_tm_node_t *vwm_wal_sz;       /* WAL size for single tx */
	struct d_tm_node_t *vwm_wal_qd;       /* WAL transaction queue depth */
	struct d_tm_node_t *vwm_wal_waiters;  /* Waiters for WAL reclaiming */
	struct d_tm_node_t *vwm_wal_dur;      /* WAL commit duration */
	struct d_tm_node_t *vwm_replay_size;  /* WAL replay size in bytes */
	struct d_tm_node_t *vwm_replay_time;  /* WAL replay time in us */
	struct d_tm_node_t *vwm_replay_count; /* Total replay count */
	struct d_tm_node_t *vwm_replay_tx;    /* Total replayed TX count */
	struct d_tm_node_t *vwm_replay_ent;   /* Total replayed entry count */
};

void vos_wal_metrics_init(struct vos_wal_metrics *vw_metrics, const char *path, int tgt_id);

/* VOS pool metrics for umem cache */
struct vos_cache_metrics {
	struct d_tm_node_t	*vcm_pg_ne;
	struct d_tm_node_t	*vcm_pg_pinned;
	struct d_tm_node_t	*vcm_pg_free;
	struct d_tm_node_t	*vcm_pg_hit;
	struct d_tm_node_t	*vcm_pg_miss;
	struct d_tm_node_t	*vcm_pg_evict;
	struct d_tm_node_t	*vcm_pg_flush;
	struct d_tm_node_t	*vcm_pg_load;
	struct d_tm_node_t	*vcm_obj_hit;
};

void vos_cache_metrics_init(struct vos_cache_metrics *vc_metrcis, const char *path, int tgt_id);

struct vos_pool_metrics {
	void			*vp_vea_metrics;
	struct vos_agg_metrics	 vp_agg_metrics;
	struct vos_gc_metrics    vp_gc_metrics;
	struct vos_space_metrics vp_space_metrics;
	struct vos_chkpt_metrics vp_chkpt_metrics;
	struct vos_wal_metrics	 vp_wal_metrics;
	struct vos_cache_metrics vp_cache_metrics;
	/* TODO: add more metrics for VOS */
};

struct vos_gc_info {
	daos_handle_t	gi_bins_btr;
	uint32_t	gi_last_pinned;
};

/**
 * VOS pool (DRAM)
 */
struct vos_pool {
	/** VOS uuid hash-link with refcnt */
	struct d_ulink		vp_hlink;
	/** number of openers */
	uint32_t                vp_opened;
	uint32_t                vp_dying:1,
				vp_opening:1,
	/** exclusive handle (see VOS_POF_EXCL) */
				vp_excl:1;
	ABT_mutex		vp_mutex;
	ABT_cond		vp_cond;
	/* this pool is for sysdb */
	bool			vp_sysdb;
	/** this pool is for rdb */
	bool			vp_rdb;
	/** caller specifies pool is small (for sys space reservation) */
	bool			vp_small;
	/** UUID of vos pool */
	uuid_t			vp_id;
	/** memory attribute of the @vp_umm */
	struct umem_attr	vp_uma;
	/** memory class instance of the pool */
	struct umem_instance	vp_umm;
	/** Size of pool file */
	uint64_t		vp_size;
	/** Features enabled for this pool */
	uint64_t		vp_feats;
	/** btr handle for the container table */
	daos_handle_t		vp_cont_th;
	/** GC statistics of this pool */
	struct vos_gc_stat       vp_gc_stat_global;
	/** GC per slice statistics of this pool */
	struct vos_gc_stat	vp_gc_stat;
	/** link chain on vos_tls::vtl_gc_pools */
	d_list_t		vp_gc_link;
	/** List of open containers with objects in gc pool */
	d_list_t		vp_gc_cont;
	/** address of durable-format pool in SCM */
	struct vos_pool_df	*vp_pool_df;
	/** Dummy data I/O context */
	struct bio_io_context	*vp_dummy_ioctxt;
	/** In-memory free space tracking for NVMe device */
	struct vea_space_info	*vp_vea_info;
	/** Reserved sys space (for space reclaim, rebuild, etc.) in bytes */
	daos_size_t		vp_space_sys[DAOS_MEDIA_MAX];
	/** Held space by in-flight updates. In bytes */
	daos_size_t		vp_space_held[DAOS_MEDIA_MAX];
	/** Dedup hash */
	struct d_hash_table	*vp_dedup_hash;
	struct vos_pool_metrics	*vp_metrics;
	vos_chkpt_update_cb_t    vp_update_cb;
	vos_chkpt_wait_cb_t      vp_wait_cb;
	void                    *vp_chkpt_arg;
	/* The count of committed DTXs for the whole pool. */
	uint32_t		 vp_dtx_committed_count;
	/** Data threshold size */
	uint32_t		 vp_data_thresh;
	/** Space (in percentage) reserved for rebuild */
	unsigned int		 vp_space_rb;
	/* GC runtime for pool */
	struct vos_gc_info	 vp_gc_info;
};

/**
 * VOS container (DRAM)
 */
struct vos_container {
	/* VOS uuid hash with refcnt */
	struct d_ulink		vc_uhlink;
	/* VOS PMEMobjpool pointer */
	struct vos_pool		*vc_pool;
	/* Unique UID of VOS container */
	uuid_t			vc_id;
	/* DAOS handle for object index btree */
	daos_handle_t		vc_btr_hdl;
	/** Array for active DTX records */
	struct lru_array	*vc_dtx_array;
	/* The handle for active DTX table */
	daos_handle_t		vc_dtx_active_hdl;
	/* The handle for committed DTX table */
	daos_handle_t		vc_dtx_committed_hdl;
	/** The root of the B+ tree for active DTXs. */
	struct btr_root		vc_dtx_active_btr;
	/** The root of the B+ tree for committed DTXs. */
	struct btr_root		vc_dtx_committed_btr;
	/* The list for active DTXs, roughly ordered in time. */
	d_list_t		vc_dtx_act_list;
	/* The list for the active DTX entries with epoch sorted. */
	d_list_t		vc_dtx_sorted_list;
	/* The list for the active DTX entries (but not re-indexed) with epoch unsorted. */
	d_list_t		vc_dtx_unsorted_list;
	/* The list for the active DTX entries that are re-indexed when open the container. */
	d_list_t		vc_dtx_reindex_list;
	/* The largest epoch difference for re-indexed DTX entries max/min pairs. */
	uint64_t		vc_dtx_reindex_eph_diff;
	/* The latest calculated local stable epoch. */
	daos_epoch_t		vc_local_stable_epoch;
	/*
	 * The lowest epoch boundary for current acceptable modification. It cannot be lower than
	 * vc_local_stable_epoch, otherwise, it may break stable epoch semantics. Because current
	 * target reported local stable epoch may be used as global stable epoch. There is window
	 * between current target reporting the local stable epoch and related leader setting the
	 * global stable epoch. If the modification with older epoch arrives during such internal,
	 * we have to reject it to avoid potential conflict.
	 *
	 * On the other hand, it must be higher than EC/VOS aggregation up boundary. Under space
	 * pressure, the EC/VOS aggregation up boundary may be higher than vc_local_stable_epoch,
	 * then it will cause vc_mod_epoch_bound > vc_local_stable_epoch.
	 */
	daos_epoch_t		vc_mod_epoch_bound;
	/* Last timestamp when VOS reject DTX because of stale epoch. */
	uint64_t		vc_dtx_reject_ts;
	/* The count of committed DTXs. */
	uint32_t		vc_dtx_committed_count;
	/** Index for timestamp lookup */
	uint32_t		*vc_ts_idx;
	/** Direct pointer to the VOS container */
	struct vos_cont_df	*vc_cont_df;
	/** Set if container has objects to garbage collect */
	d_list_t		vc_gc_link;
	/**
	 * Corresponding in-memory block allocator hints for the
	 * durable hints in vos_cont_df
	 */
	struct vea_hint_context	*vc_hint_ctxt[VOS_IOS_CNT];
	/* Current ongoing aggregation ERR */
	daos_epoch_range_t	vc_epr_aggregation;
	/* Current ongoing discard EPR */
	daos_epoch_range_t	vc_epr_discard;
	/* Last timestamp when VOS aggregation reports -DER_TX_BUSY */
	uint64_t		vc_agg_busy_ts;
	/* Last timestamp when VOS aggregation reporting ENOSPACE */
	uint64_t		vc_agg_nospc_ts;
	/* Last timestamp when IO reporting ENOSPACE */
	uint64_t		vc_io_nospc_ts;
	/* The (next) position for committed DTX entries reindex. */
	umem_off_t		vc_cmt_dtx_reindex_pos;
	/* The epoch for the latest committed solo DTX. Any solo
	 * * transaction with older epoch must have been committed.
	 */
	daos_epoch_t		vc_solo_dtx_epoch;
	/* GC runtime for container */
	struct vos_gc_info	vc_gc_info;
	/* Various flags */
	unsigned int		vc_in_aggregation:1,
				vc_in_discard:1,
				vc_cmt_dtx_indexed:1;
	unsigned int		vc_obj_discard_count;
	unsigned int		vc_open_count;
	/* The latest pool map version that DTX resync has been done. */
	uint32_t                vc_dtx_resync_ver;
};

struct vos_dtx_act_ent {
	struct vos_dtx_act_ent_df	 dae_base;
	umem_off_t			 dae_df_off;
	struct vos_dtx_blob_df		*dae_dbd;
	/* More DTX records if out of the inlined buffer. */
	umem_off_t			*dae_records;
	/* The capacity of dae_records, NOT including the inlined buffer. */
	int				 dae_rec_cap;

	/* The count of objects that are modified by this DTX. */
	int				 dae_oid_cnt;

	/* The single object OID if it is different from 'dae_base::dae_oid'. */
	daos_unit_oid_t			 dae_oid_inline;

	/* If single object is modified and if it is the same as the
	 * 'dae_base::dae_oid', then 'dae_oids' points to 'dae_base::dae_oid'.
	 *
	 * If the single object is different from 'dae_base::dae_oid',
	 * then 'dae_oids' points to the 'dae_oid_inline'.
	 *
	 * Otherwise, 'dae_oids' points to new buffer to hold more.
	 *
	 * These information is used for EC aggregation optimization.
	 * If server restarts, then we will lose the optimization but
	 * it is not fatal.
	 */
	daos_unit_oid_t			*dae_oids;
	/* The time (hlc) when the DTX entry is created. */
	uint64_t			 dae_start_time;
	/* Link into container::vc_dtx_{sorted,unsorted,reindex}_list. */
	d_list_t			 dae_order_link;
	/* Link into container::vc_dtx_act_list. */
	d_list_t			 dae_link;
	/* Back pointer to the DTX handle. */
	struct dtx_handle		*dae_dth;

	unsigned int			 dae_committable:1,
					 dae_committing:1,
					 dae_committed:1,
					 dae_aborting:1,
					 dae_aborted:1,
					 dae_maybe_shared:1,
					 /* Need validation on leader before commit/committable. */
					 dae_need_validation:1,
					 dae_need_release:1,
					 dae_preparing:1,
					 dae_prepared:1;
};

#define DAE_XID(dae)		((dae)->dae_base.dae_xid)
#define DAE_OID(dae)		((dae)->dae_base.dae_oid)
#define DAE_DKEY_HASH(dae)	((dae)->dae_base.dae_dkey_hash)
#define DAE_EPOCH(dae)		((dae)->dae_base.dae_epoch)
#define DAE_LID(dae)		((dae)->dae_base.dae_lid)
#define DAE_FLAGS(dae)		((dae)->dae_base.dae_flags)
#define DAE_MBS_FLAGS(dae)	((dae)->dae_base.dae_mbs_flags)
#define DAE_REC_INLINE(dae)	((dae)->dae_base.dae_rec_inline)
#define DAE_REC_CNT(dae)	((dae)->dae_base.dae_rec_cnt)
#define DAE_VER(dae)		((dae)->dae_base.dae_ver)
#define DAE_REC_OFF(dae)	((dae)->dae_base.dae_rec_off)
#define DAE_TGT_CNT(dae)	((dae)->dae_base.dae_tgt_cnt)
#define DAE_GRP_CNT(dae)	((dae)->dae_base.dae_grp_cnt)
#define DAE_MBS_DSIZE(dae)	((dae)->dae_base.dae_mbs_dsize)
#define DAE_INDEX(dae)		((dae)->dae_base.dae_index)
#define DAE_MBS_INLINE(dae)	((dae)->dae_base.dae_mbs_inline)
#define DAE_MBS_OFF(dae)	((dae)->dae_base.dae_mbs_off)

struct vos_dtx_cmt_ent {
	struct vos_dtx_cmt_ent_df	 dce_base;

	uint32_t			 dce_reindex:1,
					 dce_exist:1,
					 dce_invalid:1;
};

#define DCE_XID(dce)		((dce)->dce_base.dce_xid)
#define DCE_EPOCH(dce)		((dce)->dce_base.dce_epoch)
#define DCE_CMT_TIME(dce)	((dce)->dce_base.dce_cmt_time)

#define EVT_DESC_MAGIC          0xbeefdead

extern uint64_t vos_evt_feats;

/** Flags for internal use - Bit 63 can be used for another purpose so as to
 *  match corresponding internal flags for btree
 */
#define VOS_KEY_CMP_LEXICAL	(1ULL << 63)
/** Indicates that a tree has aggregation optimization enabled */
#define VOS_TF_AGG_OPT	(1ULL << 62)
/** Indicates that the stored bits from a timestamp are from an HLC */
#define VOS_TF_AGG_HLC	(1ULL << 61)
/** Number of bits to use for timestamp (roughly 1/4 ms granularity) */
#define VOS_AGG_NR_BITS	42
/** HLC differentiation bits */
#define VOS_AGG_NR_HLC_BITS	(64 - VOS_AGG_NR_BITS)
/** Lower bits of HLC used for rounding */
#define VOS_AGG_HLC_BITS	((1ULL << VOS_AGG_NR_HLC_BITS) - 1)
/** Mask to check if epoch is HLC. */
#define VOS_AGG_HLC_MASK	(VOS_AGG_HLC_BITS << (64 - VOS_AGG_NR_HLC_BITS))
/** Mask of bits of epoch */
#define VOS_AGG_EPOCH_MASK	(~VOS_AGG_HLC_MASK << (VOS_AGG_NR_HLC_BITS))
/** Start bit of stored timestamp */
#define VOS_TF_AGG_BIT	60
/** Right shift for stored portion of epoch */
#define VOS_AGG_RSHIFT	(63 - VOS_TF_AGG_BIT)
/** Left shift for stored portion of epoch */
#define VOS_AGG_LSHIFT	(64 - VOS_AGG_NR_BITS - VOS_AGG_RSHIFT)
/** In-place mask to get epoch from feature bits */
#define VOS_AGG_TIME_MASK	(VOS_AGG_EPOCH_MASK >> VOS_AGG_RSHIFT)

D_CASSERT((VOS_TF_AGG_HLC & VOS_AGG_TIME_MASK) == 0);
D_CASSERT(VOS_AGG_TIME_MASK & (1ULL << VOS_TF_AGG_BIT));
D_CASSERT((VOS_AGG_TIME_MASK & (1ULL << (VOS_TF_AGG_BIT + 1))) == 0);
D_CASSERT((VOS_AGG_TIME_MASK & (1ULL << (VOS_TF_AGG_BIT - VOS_AGG_NR_BITS))) == 0);

#define CHECK_VOS_TREE_FLAG(flag)	\
	D_CASSERT(((flag) & (EVT_FEATS_SUPPORTED | BTR_FEAT_MASK)) == 0)
CHECK_VOS_TREE_FLAG(VOS_KEY_CMP_LEXICAL);
CHECK_VOS_TREE_FLAG(VOS_TF_AGG_OPT);
CHECK_VOS_TREE_FLAG(VOS_AGG_TIME_MASK);

/* For solo transaction (single modification against single replicated object),
 * consider efficiency, we do not generate persistent DTX entry. Then we need
 * some special mechanism to maintain the semantics: any readable data must be
 * persistently visible unless it is over-written by newer modification. That
 * is the same behavior as non-solo cases. So if the local backend TX for the
 * solo transaction is in committing, then related modification is invisible
 * until related local backend TX has been successfully committed. (NOTE: in
 * committing modification maybe lost if engine crashed before commit done.)
 *
 * For this purpose, we will reuse the DTX entry index (uint32_t) in the data
 * record (ilog/svt/evt). Originally, such 32-bits integer is used as the DTX
 * entry offset in the DTX LRU array. Up to now, we only use the lower 20 bits
 * (DTX_ARRAY_LEN). Now, the highest (31th) bit will be used as a solo flag to
 * indicate a solo DTX. On the other hand, we will trace the epoch against the
 * container for the latest committed solo DTX. Anytime, for a given solo DTX,
 * if its epoch is newer than the one for the container known latest committed
 * solo DTX, then it is in committing status; otherwise, it has been committed.
 */
static inline bool
dtx_is_committed(uint32_t tx_lid, struct vos_container *cont, daos_epoch_t epoch)
{
	if (tx_lid == DTX_LID_COMMITTED)
		return true;

	D_ASSERT(cont != NULL);

	if (tx_lid & DTX_LID_SOLO_FLAG && cont->vc_solo_dtx_epoch >= epoch)
		return true;

	return false;
}

static inline bool
dtx_is_aborted(uint32_t tx_lid)
{
	return tx_lid == DTX_LID_ABORTED;
}

static inline bool
vos_dtx_is_normal_entry(uint32_t tx_lid)
{
	return tx_lid >= DTX_LID_RESERVED && !(tx_lid & DTX_LID_SOLO_FLAG);
}

static inline void
dtx_set_committed(uint32_t *tx_lid)
{
	*tx_lid = DTX_LID_COMMITTED;
}

static inline void
dtx_set_aborted(uint32_t *tx_lid)
{
	*tx_lid = DTX_LID_ABORTED;
}

/** Get the aggregatable write timestamp within 1/4 ms granularity */
static inline bool
vos_feats_agg_time_get(uint64_t feats, daos_epoch_t *epoch)
{
	if ((feats & VOS_TF_AGG_OPT) == 0)
		return false;

	if (feats & VOS_TF_AGG_HLC)
		*epoch = ((feats & VOS_AGG_TIME_MASK) << VOS_AGG_RSHIFT);
	else
		*epoch = ((feats & VOS_AGG_TIME_MASK) >> VOS_AGG_LSHIFT);

	return true;
}

/** Update the aggregatable write timestamp within 1/4 ms granularity */
static inline void
vos_feats_agg_time_update(daos_epoch_t epoch, uint64_t *feats)
{
	uint64_t	extra_flag = 0;
	daos_epoch_t	old_epoch = 0;

	if (!vos_feats_agg_time_get(*feats, &old_epoch))
		old_epoch = 0;

	if (epoch <= old_epoch) /** Only need to save newest */
		return;

	if (epoch & VOS_AGG_HLC_MASK) {
		/** We only save the top bits so round up so the stored timestamp works */
		epoch += VOS_AGG_HLC_BITS;
		epoch &= VOS_AGG_EPOCH_MASK;
		/** Ensure the resulting epoch is not zero regardless, mostly for standalone */
		epoch = (epoch >> VOS_AGG_RSHIFT);
		extra_flag = VOS_TF_AGG_HLC;
	} else {
		epoch = (epoch << VOS_AGG_LSHIFT);
	}

	*feats &= ~VOS_AGG_TIME_MASK;
	*feats |= epoch | VOS_TF_AGG_OPT | extra_flag;
}

#define VOS_KEY_CMP_UINT64_SET	(BTR_FEAT_UINT_KEY)
#define VOS_KEY_CMP_LEXICAL_SET	(VOS_KEY_CMP_LEXICAL | BTR_FEAT_DIRECT_KEY)

/** Iterator ops for objects and OIDs */
extern struct vos_iter_ops vos_oi_iter_ops;
extern struct vos_iter_ops vos_obj_dkey_iter_ops;
extern struct vos_iter_ops vos_obj_akey_iter_ops;
extern struct vos_iter_ops vos_obj_sv_iter_ops;
extern struct vos_iter_ops vos_obj_ev_iter_ops;
extern struct vos_iter_ops vos_cont_iter_ops;
extern struct vos_iter_ops vos_dtx_iter_ops;

static inline void
vos_pool_addref(struct vos_pool *pool)
{
	d_uhash_link_addref(vos_pool_hhash_get(pool->vp_sysdb), &pool->vp_hlink);
}

static inline void
vos_pool_decref(struct vos_pool *pool)
{
	d_uhash_link_putref(vos_pool_hhash_get(pool->vp_sysdb), &pool->vp_hlink);
}

static inline void
vos_pool_hash_del(struct vos_pool *pool)
{
	d_uhash_link_delete(vos_pool_hhash_get(pool->vp_sysdb), &pool->vp_hlink);
}

/**
 * Register btree class for container table, it is called within vos_init()
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_cont_tab_register();

/**
 * VOS object index class register for btree
 * Called with vos_init()
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_obj_tab_register();

/**
 * DTX table destroy
 * Called from vos_cont_destroy
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param cont_df	[IN]	Pointer to the on-disk VOS container.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_table_destroy(struct umem_instance *umm, struct vos_cont_df *cont_df);

/**
 * Register dbtree class for DTX table, it is called within vos_init().
 *
 * \return		0 on success and negative on failure
 */
int
vos_dtx_table_register(void);

/** Cleanup the dtx handle when aborting a transaction. */
void
vos_dtx_cleanup_internal(struct dtx_handle *dth);

/**
 * Check whether the record (to be accessible) is available to outside or not.
 *
 * \param coh		[IN]	The container open handle.
 * \param entry		[IN]	DTX local id
 * \param epoch		[IN]	Epoch of update
 * \param intent	[IN]	The request intent.
 * \param type		[IN]	The record type, see vos_dtx_record_types.
 * \param retry		[IN]	Whether need to retry if hit non-committed DTX entry.
 *
 * \return	positive value	If available to outside.
 *		zero		If unavailable to outside.
 *		-DER_INPROGRESS If the target record is in some
 *				uncommitted DTX, the caller
 *				needs to retry some time later.
 *				Or the caller is not sure about whether
 *				related DTX is committable or not, need
 *				to check with leader replica.
 *		negative value	For error cases.
 */
int
vos_dtx_check_availability(daos_handle_t coh, uint32_t entry,
			   daos_epoch_t epoch, uint32_t intent, uint32_t type, bool retry);

/**
 * Get local entry DTX state. Only used by VOS aggregation.
 *
 * \param entry		[IN]	DTX local id
 * \param cont		[IN]	Pointer to the vos container.
 * \param epoch		[IN]	Epoch for the entry.
 *
 * \return		DTX_ST_COMMITTED, DTX_ST_PREPARED or
 *			DTX_ST_ABORTED.
 */
static inline unsigned int
vos_dtx_ent_state(uint32_t entry, struct vos_container *cont, daos_epoch_t epoch)
{
	if (dtx_is_committed(entry, cont, epoch))
		return DTX_ST_COMMITTED;

	if (dtx_is_aborted(entry))
		return DTX_ST_ABORTED;

	return DTX_ST_PREPARED;
}

/**
 * Register the record (to be modified) to the DTX entry.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param record	[IN]	Address (offset) of the record (in SCM)
 *				to associate with the transaction.
 * \param type		[IN]	The record type, see vos_dtx_record_types.
 * \param dtx		[OUT]	tx_id is returned.  Caller is responsible
 *				to save it in the record.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, uint32_t *tx_id);

/** Return the already active dtx id, if any */
uint32_t
vos_dtx_get(bool standalone);

/**
 * Deregister the record from the DTX entry.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param coh		[IN]	The container open handle.
 * \param entry		[IN]	The local DTX id.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param record	[IN]	Address (offset) of the record to be
 *				deregistered.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_deregister_record(struct umem_instance *umm, daos_handle_t coh,
			  uint32_t entry, daos_epoch_t epoch,
			  umem_off_t record);

/**
 * Mark the DTX as prepared locally.
 *
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_prepared(struct dtx_handle *dth, struct vos_dtx_cmt_ent **dce_p);

int
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id dtis[],
			int count, daos_epoch_t epoch, bool keep_act, bool rm_cos[],
			struct vos_dtx_act_ent **daes, struct vos_dtx_cmt_ent **dces);

int
vos_dtx_abort_internal(struct vos_container *cont, struct vos_dtx_act_ent *dae, bool force);

void
vos_dtx_post_handle(struct vos_container *cont,
		    struct vos_dtx_act_ent **daes,
		    struct vos_dtx_cmt_ent **dces,
		    int count, bool abort, bool rollback, bool keep_act);

/**
 * Establish indexed active DTX table in DRAM.
 *
 * \param cont	[IN]	Pointer to the container.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_act_reindex(struct vos_container *cont);

enum vos_tree_class {
	/** the first reserved tree class */
	VOS_BTR_BEGIN		= DBTREE_VOS_BEGIN,
	/** distribution key tree */
	VOS_BTR_DKEY		= (VOS_BTR_BEGIN + 0),
	/** attribute key tree */
	VOS_BTR_AKEY		= (VOS_BTR_BEGIN + 1),
	/** single value + epoch tree */
	VOS_BTR_SINGV		= (VOS_BTR_BEGIN + 2),
	/** object index table */
	VOS_BTR_OBJ_TABLE	= (VOS_BTR_BEGIN + 3),
	/** container index table */
	VOS_BTR_CONT_TABLE	= (VOS_BTR_BEGIN + 4),
	/** DAOS two-phase commit transaction table (active) */
	VOS_BTR_DTX_ACT_TABLE	= (VOS_BTR_BEGIN + 5),
	/** DAOS two-phase commit transaction table (committed) */
	VOS_BTR_DTX_CMT_TABLE	= (VOS_BTR_BEGIN + 6),
	/** The VOS incarnation log tree */
	VOS_BTR_ILOG		= (VOS_BTR_BEGIN + 7),
	/** the last reserved tree class */
	VOS_BTR_END,
};

int obj_tree_init(struct vos_object *obj);
int obj_tree_fini(struct vos_object *obj);
int obj_tree_register(void);

/**
 * Single value key
 */
struct vos_svt_key {
	/** Epoch of entry */
	uint64_t	sk_epoch;
	/** Minor epoch of entry */
	uint16_t	sk_minor_epc;
};

/**
 * Data structure which carries the value buffers, checksums and memory IDs
 * to the multi-nested btree.
 */
struct vos_rec_bundle {
	/** Optional, externally allocated buffer umoff */
	umem_off_t		 rb_off;
	/** checksum buffer for the daos key */
	struct dcs_csum_info	*rb_csum;
	/**
	 * Input  : value buffer (non-rdma data)
	 *	    TODO also support scatter/gather list input.
	 * Output : parameter to return value address.
	 */
	d_iov_t			*rb_iov;
	/**
	 * Single value record IOV.
	 */
	struct bio_iov		*rb_biov;
	/** Returned durable address of the btree record */
	struct vos_krec_df	*rb_krec;
	/** input record size */
	daos_size_t		 rb_rsize;
	/** global record size, needed for EC singv record */
	daos_size_t		 rb_gsize;
	/** pool map version */
	uint32_t		 rb_ver;
	/** tree class */
	enum vos_tree_class	 rb_tclass;
	/** DTX state */
	unsigned int		 rb_dtx_state;
};

#define VOS_SIZE_ROUND		8

/* size round up */
static inline uint64_t
vos_size_round(uint64_t size)
{
	return (size + VOS_SIZE_ROUND - 1) & ~(VOS_SIZE_ROUND - 1);
}

static inline struct vos_krec_df *
vos_rec2krec(struct btr_instance *tins, struct btr_record *rec)
{
	return (struct vos_krec_df *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
}

static inline struct vos_irec_df *
vos_rec2irec(struct btr_instance *tins, struct btr_record *rec)
{
	return (struct vos_irec_df *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
}

static inline uint64_t
vos_krec_size(struct vos_rec_bundle *rbund)
{
	d_iov_t	*key;
	daos_size_t	 psize;

	key = rbund->rb_iov;
	psize = vos_size_round(rbund->rb_csum->cs_len) + key->iov_len;
	return sizeof(struct vos_krec_df) + psize;
}

static inline void *
vos_krec2payload(struct vos_krec_df *krec)
{
	return (void *)&krec[1];
}

static inline char *
vos_krec2csum(struct vos_krec_df *krec)
{
	return krec->kr_cs_size ? vos_krec2payload(krec) : NULL;
}

static inline char *
vos_krec2key(struct vos_krec_df *krec)
{
	char *payload = vos_krec2payload(krec);

	return &payload[vos_size_round(krec->kr_cs_size)];
}

static inline uint16_t vos_irec2csum_size(struct vos_irec_df *irec)
{
	return irec->ir_cs_size;
}

static inline void vos_irec_init_csum(struct vos_irec_df *irec,
				      struct dcs_csum_info *csum)
{
	if (csum) {
		irec->ir_cs_size = csum->cs_len;
		irec->ir_cs_type = (uint8_t)csum->cs_type;
	} else {
		irec->ir_cs_size = 0;
		irec->ir_cs_type = 0;
	}
}

#define	VOS_GANG_SIZE_THRESH	(BIO_DMA_CHUNK_MB << 20)	/* 8MB */

static inline unsigned int
vos_irec_gang_nr(struct vos_pool *pool, daos_size_t rsize)
{
	if (pool->vp_feats & VOS_POOL_FEAT_GANG_SV) {
		if (rsize > VOS_GANG_SIZE_THRESH)
			return (rsize + VOS_GANG_SIZE_THRESH - 1) / VOS_GANG_SIZE_THRESH;
	}

	return 0;
}

/** Size of metadata without user payload */
static inline uint64_t
vos_irec_msize(struct vos_pool *pool, struct vos_rec_bundle *rbund)
{
	uint64_t size = sizeof(struct vos_irec_df);

	if (rbund->rb_csum != NULL)
		size += vos_size_round(rbund->rb_csum->cs_len);

	size += bio_gaddr_size(vos_irec_gang_nr(pool, rbund->rb_rsize));

	return size;
}

static inline char *
vos_irec2csum(struct vos_irec_df *irec)
{
	return irec->ir_cs_size == 0 ? NULL : &irec->ir_body[0];
}

static inline char *
vos_irec2data(struct vos_irec_df *irec)
{
	return &irec->ir_body[vos_size_round(vos_irec2csum_size(irec))];
}

static inline bool
vos_obj_is_empty(struct vos_object *obj)
{
	return !obj->obj_df || obj->obj_df->vo_tree.tr_class == 0;
}

static inline bool
vos_subtree_is_empty(daos_handle_t toh)
{
	return dbtree_is_empty(toh) == 1;
}

static inline bool vos_recx_is_equal(daos_recx_t *recx1, daos_recx_t *recx2)
{
	return !(memcmp(recx1, recx2, sizeof(daos_recx_t)));
}

static inline struct vos_pool *
vos_cont2pool(struct vos_container *cont)
{
	return cont->vc_pool;
}

static inline struct vos_pool *
vos_obj2pool(struct vos_object *obj)
{
	return vos_cont2pool(obj->obj_cont);
}

static inline struct umem_attr *
vos_obj2uma(struct vos_object *obj)
{
	return &obj->obj_cont->vc_pool->vp_uma;
}

static inline struct umem_instance *
vos_obj2umm(struct vos_object *obj)
{
	return &obj->obj_cont->vc_pool->vp_umm;
}

static inline daos_handle_t
vos_pool2hdl(struct vos_pool *pool)
{
	daos_handle_t poh;

	poh.cookie = (uint64_t)pool;
	return poh;
}

static inline struct vos_pool*
vos_hdl2pool(daos_handle_t poh)
{
	return (struct vos_pool *)(poh.cookie);
}

static inline daos_handle_t
vos_cont2hdl(struct vos_container *co)
{
	daos_handle_t coh;

	coh.cookie = (uint64_t)co;
	return coh;
}

static inline struct vos_container *
vos_hdl2cont(daos_handle_t coh)
{
	return (struct vos_container *)(coh.cookie);
}

void vos_cont_addref(struct vos_container *cont);
void vos_cont_decref(struct vos_container *cont);

/**
 * iterators
 */
enum vos_iter_state {
	/** iterator has no valid cursor */
	VOS_ITS_NONE,
	/** iterator has valid cursor (user can call next/probe) */
	VOS_ITS_OK,
	/** end of iteration, no more entries */
	VOS_ITS_END,
};

struct vos_iter_ops;

/** the common part of vos iterators */
struct vos_iterator {
	struct dtx_handle	*it_dth;
	struct vos_iter_ops	*it_ops;
	struct vos_iterator	*it_parent; /* parent iterator */
	struct vos_ts_set	*it_ts_set;
	vos_iter_filter_cb_t	 it_filter_cb;
	void			*it_filter_arg;
	uint64_t                 it_seq;
	struct vos_iter_anchors *it_anchors;
	daos_epoch_t		 it_bound;
	vos_iter_type_t		 it_type;
	enum vos_iter_state	 it_state;
	uint32_t		 it_ref_cnt;
	/** Note: it_for_agg is only set at object level as it's only used for
	 * mutual exclusion between aggregation and object discard.
	 */
	uint32_t it_from_parent : 1, it_for_purge : 1, it_for_discard : 1, it_for_migration : 1,
	    it_show_uncommitted : 1, it_ignore_uncommitted : 1, it_for_sysdb : 1, it_for_agg : 1;
};

/* Auxiliary structure for passing information between parent and nested
 * iterator
 */
struct vos_iter_info {
	/* retrieved container handle */
	daos_handle_t		 ii_hdl;
	union {
		/* Pointer to evtree for nested iterator */
		struct evt_root	*ii_evt;
		/* Pointer to btree for nested iterator */
		struct btr_root	*ii_btr;
		/** Open tree handle for nested iterator */
		daos_handle_t    ii_tree_hdl;
		/* oid to hold */
		daos_unit_oid_t	 ii_oid;
	};
	/* needed to open nested tree */
	struct umem_attr	*ii_uma;
	/* needed to open nested tree */
	struct vea_space_info	*ii_vea_info;
	/* Reference to vos object, set in iop_tree_prepare. */
	struct vos_object	*ii_obj;
	/** for fake akey, pass the parent ilog info */
	struct vos_ilog_info    *ii_ilog_info;
	/** address range (RECX); rx_nr == 0 means entire range (0:~0ULL) */
	daos_recx_t              ii_recx;
	daos_epoch_range_t	 ii_epr;
	/** highest epoch where parent obj/key was punched */
	struct vos_punch_record	 ii_punched;
	/** Filter callback */
	vos_iter_filter_cb_t	 ii_filter_cb;
	void			*ii_filter_arg;
	/** epoch logic expression for the iterator. */
	vos_it_epc_expr_t	 ii_epc_expr;
	/** iterator flags */
	uint32_t		 ii_flags;
	struct vos_krec_df      *ii_dkey_krec;
	/** Indicate this is a fake akey and which type */
	uint32_t                 ii_fake_akey_flag;
};

/** function table for vos iterator */
struct vos_iter_ops {
	/** prepare a new iterator with the specified type and parameters */
	int	(*iop_prepare)(vos_iter_type_t type, vos_iter_param_t *param,
			       struct vos_iterator **iter_pp,
			       struct vos_ts_set *ts_set);
	/** fetch the record that the cursor points to and open the subtree
	 *  corresponding to specified type, return info about the iterator
	 *  and nested object.   If NULL, it isn't supported for the parent
	 *  type.
	 */
	int	(*iop_nested_tree_fetch)(struct vos_iterator *iter,
					 vos_iter_type_t type,
					 struct vos_iter_info *info);
	/** prepare the nested iterator from state from parent.  Should close
	 *  the tree handle after preparing the iterator.  If NULL, it isn't
	 *  supported in the nested iterator.
	 */
	int	(*iop_nested_prepare)(vos_iter_type_t type,
				      struct vos_iter_info *info,
				      struct vos_iterator **iter_pp);
	/** finalize a iterator */
	int	(*iop_finish)(struct vos_iterator *iter);
	/** Set the iterating cursor to the provided @anchor */
	int	(*iop_probe)(struct vos_iterator *iter,
			     daos_anchor_t *anchor, uint32_t flags);
	/** move forward the iterating cursor */
	int	(*iop_next)(struct vos_iterator *iter, daos_anchor_t *anchor);
	/** fetch the record that the cursor points to */
	int	(*iop_fetch)(struct vos_iterator *iter,
			     vos_iter_entry_t *it_entry,
			     daos_anchor_t *anchor);
	/** copy out the record data */
	int	(*iop_copy)(struct vos_iterator *iter,
			    vos_iter_entry_t *it_entry, d_iov_t *iov_out);
	/** Delete the record that the cursor points to */
	int	(*iop_process)(struct vos_iterator *iter, vos_iter_proc_op_t op,
			       void *args);
	/**
	 * Optional, the iterator has no element.
	 *
	 * \return	1 empty
	 *		0 non-empty
	 *		-ve error code
	 */
	int	(*iop_empty)(struct vos_iterator *iter);
};

const char *vos_iter_type2name(vos_iter_type_t type);

static inline struct vos_iterator *
vos_hdl2iter(daos_handle_t hdl)
{
	return (struct vos_iterator *)hdl.cookie;
}

/** Internal bit for initializing iterator from open tree handle */
#define VOS_IT_KEY_TREE (1 << 31)
/** Ensure there is no overlap with public iterator flags (defined in
 *  src/include/daos_srv/vos_types.h).
 */
D_CASSERT((VOS_IT_KEY_TREE & VOS_IT_MASK) == 0);

/** Special internal marker for fake akey. If set, it_hdl will point
 *  at krec of the dkey.  We just need a struct as a placeholder
 *  to keep iterator presenting an akey to the caller.  This adds
 *  some small complication to VOS iterator but simplifies rebuild
 *  and other entities that use it. This flag must not conflict with
 *  other iterator flags.
 */
#define VOS_IT_DKEY_SV (1 << 30)
#define VOS_IT_DKEY_EV (1 << 29)
D_CASSERT((VOS_IT_DKEY_SV & VOS_IT_MASK) == 0);
D_CASSERT((VOS_IT_DKEY_EV & VOS_IT_MASK) == 0);

/** iterator for dkey/akey/recx */
struct vos_obj_iter {
	/* public part of the iterator */
	struct vos_iterator	 it_iter;
	/** Incarnation log entries for current iterator */
	struct vos_ilog_info	 it_ilog_info;
	/** For flat akey, this will open value tree handle and either
	 * VOS_IT_DKEY_SV or VOS_IT_DKEY_EV will be set.
	 */
	daos_handle_t            it_hdl;
	/** condition of the iterator: epoch logic expression */
	vos_it_epc_expr_t	 it_epc_expr;
	/** iterator flags */
	uint32_t		 it_flags;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	 it_epr;
	/** highest epoch where parent obj/key was punched */
	struct vos_punch_record  it_punched;
	/* reference on the object */
	struct vos_object	*it_obj;
	/** condition of the iterator: extent range */
	daos_recx_t              it_recx;
	/** For fake akey, save the dkey krec as well */
	struct vos_krec_df      *it_dkey_krec;
	/** Store the fake akey */
	char                     it_fake_akey;
};

static inline struct vos_obj_iter *
vos_iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_obj_iter, it_iter);
}

static inline struct vos_obj_iter *
vos_hdl2oiter(daos_handle_t hdl)
{
	return vos_iter2oiter(vos_hdl2iter(hdl));
}

static inline daos_handle_t
vos_iter2hdl(struct vos_iterator *iter)
{
	daos_handle_t	hdl;

	hdl.cookie = (uint64_t)iter;
	return hdl;
}

/**
 * store a bundle of parameters into a iovec, which is going to be passed
 * into dbtree operations as a compound value (data buffer address, or ZC
 * buffer umoff, checksum etc).
 */
static inline void
tree_rec_bundle2iov(struct vos_rec_bundle *rbund, d_iov_t *iov)
{
	memset(rbund, 0, sizeof(*rbund));
	d_iov_set(iov, rbund, sizeof(*rbund));
}

enum {
	SUBTR_CREATE  = (1 << 0), /**< may create the subtree */
	SUBTR_EVT     = (1 << 1), /**< subtree is evtree */
	SUBTR_FLAT    = (1 << 2), /**< use flat kv on create */
	SUBTR_NO_OPEN = (1 << 3), /**< Don't initialize the subtree if the key is flat */
};

/* vos_common.c */
int
vos_bio_addr_free(struct vos_pool *pool, bio_addr_t *addr, daos_size_t nob);

void
vos_evt_desc_cbs_init(struct evt_desc_cbs *cbs, struct vos_pool *pool,
		      daos_handle_t coh, struct vos_object *obj);

int
vos_tx_begin(struct dtx_handle *dth, struct umem_instance *umm, bool is_sysdb);

/** Finish the transaction and publish or cancel the reservations or
 *  return if err == 0 and it's a multi-modification transaction that
 *  isn't complete.
 *
 * \param[in]	cont		the VOS container
 * \param[in]	dth_in		The dtx handle, if applicable
 * \param[in]	rsrvd_scmp	Pointer to reserved scm, will be consumed
 * \param[in]	nvme_exts	List of resreved nvme extents
 * \param[in]	started		Only applies when dth_in is invalid,
 *				indicates if vos_tx_begin was successful
 * \param[in]	biod		bio_desc for data I/O
 * \param[in]	err		the error code
 *
 * \return	err if non-zero, otherwise 0 or appropriate error
 */
int
vos_tx_end(struct vos_container *cont, struct dtx_handle *dth_in,
	   struct umem_rsrvd_act **rsrvd_actp, d_list_t *nvme_exts, bool started,
	   struct bio_desc *biod, int err);

/* vos_obj.c */
int
key_tree_prepare(struct vos_object *obj, daos_handle_t toh,
		 enum vos_tree_class tclass, daos_key_t *key, int flags,
		 uint32_t intent, struct vos_krec_df **krecp,
		 daos_handle_t *sub_toh, struct vos_ts_set *ts_set);
void
key_tree_release(daos_handle_t toh, bool is_array);
int
key_tree_punch(struct vos_object *obj, daos_handle_t toh, daos_epoch_t epoch,
	       daos_epoch_t bound, d_iov_t *key_iov, d_iov_t *val_iov,
	       uint64_t flags, struct vos_ts_set *ts_set, umem_off_t *known_key,
	       struct vos_ilog_info *parent, struct vos_ilog_info *info);
int
key_tree_delete(struct vos_object *obj, daos_handle_t toh, d_iov_t *key_iov);

/* vos_io.c */
int
vos_dedup_init(struct vos_pool *pool);
void
vos_dedup_fini(struct vos_pool *pool);
void
vos_dedup_invalidate(struct vos_pool *pool);

umem_off_t
vos_reserve_scm(struct vos_container *cont, struct umem_rsrvd_act *rsrvd_scm,
		daos_size_t size, struct vos_object *obj);
int
vos_publish_scm(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_scm, bool publish);
int
vos_reserve_blocks(struct vos_container *cont, d_list_t *rsrvd_nvme,
		   daos_size_t size, enum vos_io_stream ios, uint64_t *off);

int
vos_publish_blocks(struct vos_container *cont, d_list_t *blk_list, bool publish,
		   enum vos_io_stream ios);

static inline struct umem_instance *
vos_pool2umm(struct vos_pool *pool)
{
	return &pool->vp_umm;
}

static inline struct umem_store *
vos_pool2store(struct vos_pool *pool)
{
	return &pool->vp_umm.umm_pool->up_store;
}

static inline struct umem_instance *
vos_cont2umm(struct vos_container *cont)
{
	return vos_pool2umm(cont->vc_pool);
}

static inline uint32_t
vos_iter_intent(struct vos_iterator *iter)
{
	if (iter->it_for_purge)
		return DAOS_INTENT_PURGE;
	if (iter->it_for_discard)
		return DAOS_INTENT_DISCARD;
	if (iter->it_ignore_uncommitted)
		return DAOS_INTENT_IGNORE_NONCOMMITTED;
	if (iter->it_for_migration)
		return DAOS_INTENT_MIGRATION;
	return DAOS_INTENT_DEFAULT;
}

void
gc_wait(void);
int
gc_add_pool(struct vos_pool *pool);
void
gc_del_pool(struct vos_pool *pool);
bool
gc_have_pool(struct vos_pool *pool);
int
gc_init_pool(struct umem_instance *umm, struct vos_pool_df *pd);
int
gc_init_cont(struct umem_instance *umm, struct vos_cont_df *cd);
void
gc_check_cont(struct vos_container *cont);
int
gc_add_item(struct vos_pool *pool, daos_handle_t coh,
	    enum vos_gc_type type, umem_off_t item_off, uint32_t *bkt_ids);
int
vos_gc_pool_tight(daos_handle_t poh, int *credits);
void
gc_reserve_space(struct vos_pool *pool, daos_size_t *rsrvd);
int
gc_open_pool(struct vos_pool *pool);
void
gc_close_pool(struct vos_pool *pool);
int
gc_open_cont(struct vos_container *cont);
void
gc_close_cont(struct vos_container *cont);

struct vos_bkt_iter {
	uint32_t	bi_bkt_tot;
	uint32_t	bi_bkt_cur;
	uint8_t		bi_skipped[0];
};

/**
 * If the object is fully punched, bypass normal aggregation and move it to container
 * discard pool.
 *
 * \param ih[IN]	Iterator handle
 *
 * \return		Zero on Success
 *			1: entry is removed
 *			Negative value otherwise
 */
int
oi_iter_check_punch(daos_handle_t ih);

/**
 * Aggregate the creation/punch records in the current entry of the object
 * iterator.
 *
 * \param ih[IN]		Iterator handle
 * \param range_discard[IN]	Discard only uncommitted ilog entries (for reintegration)
 *
 * \return		Zero on Success
 *			Positive value if a reprobe is needed
 *			(1: entry is removed; 2: entry is invisible)
 *			Negative value otherwise
 */
int
oi_iter_aggregate(daos_handle_t ih, bool range_discard);

/**
 * If the key is fully punched, bypass normal aggregation and move it to container
 * discard pool.
 *
 * \param ih[IN]	Iterator handle
 *
 * \return		Zero on Success
 *			1: entry is removed
 *			Negative value otherwise
 */
int
vos_obj_iter_check_punch(daos_handle_t ih);

/**
 * Aggregate the creation/punch records in the current entry of the key
 * iterator.  If aggregation optimization is supported, it will clear the
 * aggregation flag and set the needed flag, accordingly.
 *
 * \param ih[IN]		Iterator handle
 * \param range_discard[IN]	Discard only uncommitted ilog entries (for reintegration)
 *
 * \return		Zero on Success
 *			Positive value if a reprobe is needed
 *			(1: entry is removed; 2: entry is invisible)
 *			Negative value otherwise
 */
int
vos_obj_iter_aggregate(daos_handle_t ih, bool range_discard);

/** Internal vos iterator API for iterating through keys using an
 *  open tree handle to initialize the iterator
 *
 *  \param obj[IN]			VOS object
 *  \param toh[IN]			Open key tree handle
 *  \param type[IN]			Iterator type (VOS_ITER_AKEY/DKEY only)
 *  \param epr[IN]			Valid epoch range for iteration
 *  \param show_uncommitted[IN]		Return uncommitted entries marked as instead of failing
 *  \param cb[IN]			Callback for key
 *  \param arg[IN]			argument to pass to callback
 *  \param dth[IN]			dtx handle
 *  \param anchor[IN]			Option anchor from where to start iterator
 */
int
vos_iterate_key(struct vos_object *obj, daos_handle_t toh, vos_iter_type_t type,
		const daos_epoch_range_t *epr, bool show_uncommitted,
		vos_iter_cb_t cb, void *arg, struct dtx_handle *dth, daos_anchor_t *anchor);

/** Start epoch of vos */
extern daos_epoch_t	vos_start_epoch;

/* vos_space.c */
void
vos_space_sys_init(struct vos_pool *pool);
int
vos_space_sys_set(struct vos_pool *pool, daos_size_t *space_sys);
int
vos_space_query(struct vos_pool *pool, struct vos_pool_space *vps, bool slow);
int
vos_space_hold(struct vos_pool *pool, uint64_t flags, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods,
	       struct dcs_iod_csums *iods_csums, daos_size_t *space_hld);
void
vos_space_unhold(struct vos_pool *pool, daos_size_t *space_hld);
void
vos_space_update_metrics(struct vos_pool *pool);

static inline bool
vos_epc_punched(daos_epoch_t epc, uint16_t minor_epc,
		const struct vos_punch_record *punch)
{
	if (punch->pr_epc < epc)
		return false;

	if (punch->pr_epc > epc)
		return true;

	if (punch->pr_minor_epc >= minor_epc)
		return true;

	return false;
}

static inline bool
vos_dtx_hit_inprogress(bool standalone)
{
	struct dtx_handle	*dth;

	if (standalone)
		return false;

	dth = vos_dth_get(false);

	return dth != NULL && dth->dth_share_tbd_count > 0;
}

static inline bool
vos_dtx_continue_detect(int rc, bool standalone)
{
	struct dtx_handle	*dth;

	if (standalone)
		return false;

	dth = vos_dth_get(false);

	/* Continue to detect other potential in-prepared DTX. */
	return rc == -DER_INPROGRESS && dth != NULL &&
		dth->dth_share_tbd_count > 0 &&
		dth->dth_share_tbd_count < DTX_REFRESH_MAX;
}

static inline bool
vos_has_uncertainty(struct vos_ts_set *ts_set,
		    const struct vos_ilog_info *info, daos_epoch_t epoch,
		    daos_epoch_t bound)
{
	if (info->ii_uncertain_create)
		return true;

	return vos_ts_wcheck(ts_set, epoch, bound);
}

/** For dealing with common routines between punch and update where akeys are
 *  passed in different structures
 */
struct vos_akey_data {
	union {
		/** If ad_is_iod is true, array of iods is used for akeys */
		daos_iod_t	*ad_iods;
		/** If ad_is_iod is false, it's an array of akeys */
		daos_key_t	*ad_keys;
	};
	/** True if the the field above is an iod array */
	bool		 ad_is_iod;
};

/** Add any missing timestamps to the read set when an operation fails due to
 *  -DER_NONEXST.   This allows for fewer false conflicts on negative
 *  entries.
 *
 *  \param[in]	ts_set	The timestamp set
 *  \param[in]	dkey	Pointer to the dkey or NULL
 *  \param[in]	akey_nr	Number of akeys (or 0 if no akeys)
 *  \param[in]	ad	The actual akeys (either an array of akeys or iods)
 */
void
vos_ts_add_missing(struct vos_ts_set *ts_set, daos_key_t *dkey, int akey_nr,
		   struct vos_akey_data *ad);

/** Init VOS pool settings
 *
 *  \param	md_on_ssd[IN]	Boolean indicating if MD-on-SSD is enabled.
 *
 *  \return		Zero on Success, Error otherwise
 */
int
vos_pool_settings_init(bool md_on_ssd);

/** Raise a RAS event on incompatible durable format
 *
 * \param[in] type		Type of object with layout format
 *				incompatibility (e.g. VOS pool)
 * \param[in] version		Version of the object
 * \param[in] min_version	Minimum supported version
 * \param[in] max_version	Maximum supported version
 * \param[in] pool		(Optional) associated pool uuid
 */
void
vos_report_layout_incompat(const char *type, int version, int min_version,
			   int max_version, uuid_t *uuid);

static inline int
vos_offload_exec(int (*func)(void *), void *arg)
{
	if (dss_offload_exec != NULL)
		return dss_offload_exec(func, arg);
	else
		return func(arg);
}

static inline int
vos_exec(void (*func)(void *), void *arg)
{
	if (dss_main_exec != NULL)
		return dss_main_exec(func, arg);

	func(arg);

	return 0;
}

/* vos_csum_recalc.c */

struct csum_recalc {
	struct evt_extent	 cr_log_ext;
	struct evt_extent	*cr_phy_ext;
	struct dcs_csum_info	*cr_phy_csum;
	daos_off_t		 cr_phy_off;
};

struct csum_recalc_args {
	struct bio_sglist	*cra_bsgl;	/* read sgl */
	struct evt_entry_in	*cra_ent_in;    /* coalesced entry */
	struct csum_recalc	*cra_recalcs;   /* recalc info */
	unsigned int		 cra_seg_cnt;   /* # of read segments */
	int			 cra_rc;	/* return code */
};

int vos_csum_recalc_fn(void *recalc_args);

static inline bool
vos_dae_is_commit(struct vos_dtx_act_ent *dae)
{
	return dae->dae_committable || dae->dae_committing || dae->dae_committed;
}

static inline bool
vos_dae_is_abort(struct vos_dtx_act_ent *dae)
{
	return dae->dae_aborting || dae->dae_aborted;
}

static inline bool
vos_dae_is_prepare(struct vos_dtx_act_ent *dae)
{
	return dae->dae_preparing || dae->dae_prepared;
}

static inline bool
vos_dae_in_process(struct vos_dtx_act_ent *dae)
{
	return dae->dae_committing || dae->dae_aborting || dae->dae_preparing;
}

static inline struct dcs_csum_info *
vos_csum_at(struct dcs_iod_csums *iod_csums, unsigned int idx)
{
	/** is enabled and has csums (might not for punch) */
	if (iod_csums != NULL && iod_csums[idx].ic_nr > 0)
		return iod_csums[idx].ic_data;
	return NULL;
}

static inline struct dcs_csum_info *
recx_csum_at(struct dcs_csum_info *csums, unsigned int idx, daos_iod_t *iod)
{
	if (csums != NULL && csum_iod_is_supported(iod))
		return &csums[idx];
	return NULL;
}

static inline daos_size_t
recx_csum_len(daos_recx_t *recx, struct dcs_csum_info *csum,
	      daos_size_t rsize)
{
	if (!ci_is_valid(csum) || rsize == 0)
		return 0;
	return (daos_size_t)csum->cs_len * csum_chunk_count(csum->cs_chunksize,
			recx->rx_idx, recx->rx_idx + recx->rx_nr - 1, rsize);
}

/** Mark that the object and container need aggregation.
 *
 * \param[in] cont	VOS container
 * \param[in] dkey_root	Root of dkey tree (marked for object)
 * \param[in] obj_root	Root of object tree (marked for container)
 * \param[in] epoch	Epoch of aggregatable update
 *
 * \return 0 on success, error otherwise
 */
int
vos_mark_agg(struct vos_container *cont, struct btr_root *dkey_root, struct btr_root *obj_root,
	     daos_epoch_t epoch);

/** Mark that the key needs aggregation.
 *
 * \param[in] cont	VOS container
 * \param[in] krec	The key's record
 * \param[in] epoch	Epoch of aggregatable update
 *
 * \return 0 on success, error otherwise
 */
int
vos_key_mark_agg(struct vos_container *cont, struct vos_krec_df *krec, daos_epoch_t epoch);

/** Convenience function to return address of a bio_addr in pmem.  If it's a hole or NVMe address,
 *  it returns NULL.
 */
const void *
vos_pool_biov2addr(daos_handle_t poh, struct bio_iov *biov);

static inline bool
vos_anchor_is_zero(daos_anchor_t *anchor)
{
	return anchor == NULL || daos_anchor_is_zero(anchor);
}

static inline int
vos_media_read(struct bio_io_context *ioc, struct umem_instance *umem,
	       bio_addr_t addr, d_iov_t *iov_out)
{
	if (addr.ba_type == DAOS_MEDIA_NVME) {
		D_ASSERT(ioc != NULL);
		return bio_read(ioc, addr, iov_out);
	}

	D_ASSERT(umem != NULL);
	memcpy(iov_out->iov_buf, umem_off2ptr(umem, addr.ba_off), iov_out->iov_len);
	return 0;
}

static inline struct bio_meta_context *
vos_pool2mc(struct vos_pool *vp)
{
	D_ASSERT(vp && vp->vp_umm.umm_pool != NULL);
	return (struct bio_meta_context *)vp->vp_umm.umm_pool->up_store.stor_priv;
}

static inline struct bio_io_context *
vos_data_ioctxt(struct vos_pool *vp)
{
	struct bio_meta_context	*mc = vos_pool2mc(vp);

	if (mc != NULL && bio_mc2ioc(mc, SMD_DEV_TYPE_DATA) != NULL)
		return bio_mc2ioc(mc, SMD_DEV_TYPE_DATA);

	/* Use dummy I/O context when data blob doesn't exist */
	D_ASSERT(vp->vp_dummy_ioctxt != NULL);
	return vp->vp_dummy_ioctxt;
}

/*
 * When a local transaction includes data write to NVMe, we submit data write and WAL
 * write in parallel to reduce one NVMe I/O latency, and the WAL replay on recovery
 * relies on the data csum stored in WAL to verify the data integrity.
 *
 * To ensure the data integrity check on replay not being interfered by aggregation or
 * container destroy (which could delete/change the committed NVMe extent), we need to
 * explicitly flush WAL header before aggregation or container destroy, so that WAL
 * replay will be able to tell that the transactions (can be potentially interfered)
 * are already committed, and skip data integriy check over them.
 */
static inline int
vos_flush_wal_header(struct vos_pool *vp)
{
	struct bio_meta_context *mc = vos_pool2mc(vp);

	/* When both md-on-ssd and data blob are present */
	if (mc != NULL && bio_mc2ioc(mc, SMD_DEV_TYPE_WAL) != NULL &&
	    bio_mc2ioc(mc, SMD_DEV_TYPE_DATA) != NULL)
		return bio_wal_flush_header(mc);

	return 0;
}

/*
 * Check if the NVMe context of a VOS target is healthy.
 *
 * \param[in] coh	VOS container
 * \param[in] update	The check is for an update operation or not
 *
 * \return		0		: VOS target is healthy
 *			-DER_NVME_IO	: VOS target is faulty
 */
static inline int
vos_tgt_health_check(struct vos_container *cont, bool update)
{
	D_ASSERT(cont != NULL);
	D_ASSERT(cont->vc_pool != NULL);

	if (cont->vc_pool->vp_sysdb)
		return 0;

	return bio_xsctxt_health_check(vos_xsctxt_get(), true, update);
}

int
vos_oi_upgrade_layout_ver(struct vos_container *cont, daos_unit_oid_t oid,
			  uint32_t layout_ver);

void vos_lru_free_track(void *arg, daos_size_t size);
void vos_lru_alloc_track(void *arg, daos_size_t size);

static inline bool
vos_obj_skip_akey_supported(struct vos_container *cont, daos_unit_oid_t oid)
{
	struct vos_pool *pool = vos_cont2pool(cont);

	if ((pool->vp_feats & VOS_POOL_FEAT_FLAT_DKEY) == 0)
		return false;

	if (daos_is_array(oid.id_pub) || daos_is_kv(oid.id_pub))
		return true;

	return false;
}

/** For flat trees, we sometimes need a fake akey anchor */
static inline void
vos_fake_anchor_create(daos_anchor_t *anchor)
{
	memset(&anchor->da_buf[0], 0, sizeof(anchor->da_buf));
	anchor->da_type = DAOS_ANCHOR_TYPE_HKEY;
}

/**
 * If subtree is already created, it could have been created by an older pool
 * version so if the dkey is not flat, we need to use KREC_BF_BTR here.
 **/
static inline bool
key_tree_is_evt(int flags, enum vos_tree_class tclass, struct vos_krec_df *krec)
{
	return (flags & SUBTR_EVT && (tclass == VOS_BTR_AKEY ||
				     (krec->kr_bmap & KREC_BF_NO_AKEY)));
}

static inline bool
vos_io_scm(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size, enum vos_io_stream ios)
{
	if (pool->vp_vea_info == NULL)
		return true;

	if (pool->vp_data_thresh == 0)
		return true;

	if (size < pool->vp_data_thresh)
		return true;

	return false;
}

/**
 * Insert object ID and its parent container into the array of objects touched by the ongoing
 * local transaction.
 *
 * \param[in] dth	DTX handle for ongoing local transaction
 * \param[in] cont	VOS container
 * \param[in] oid	Object ID
 *
 * \return		0		: Success.
 *			-DER_NOMEM	: Run out of the volatile memory.
 */
int
vos_insert_oid(struct dtx_handle *dth, struct vos_container *cont, daos_unit_oid_t *oid);

static inline bool
vos_pool_is_p2(struct vos_pool *pool)
{
	struct umem_store	*store = vos_pool2store(pool);

	return store->store_type == DAOS_MD_BMEM_V2;
}

static inline bool
vos_pool_is_evictable(struct vos_pool *pool)
{
	struct umem_store	*store = vos_pool2store(pool);

	if (store->store_evictable) {
		D_ASSERT(store->store_type == DAOS_MD_BMEM_V2);
		return true;
	}

	return false;
}

static inline umem_off_t
vos_obj_alloc(struct umem_instance *umm, struct vos_object *obj, size_t size, bool zeroing)
{

	if (obj != NULL && vos_pool_is_evictable(vos_obj2pool(obj))) {
		D_ASSERT(obj->obj_bkt_alloted == 1);
		if (zeroing)
			return umem_zalloc_from_bucket(umm, size, obj->obj_bkt_ids[0]);

		return umem_alloc_from_bucket(umm, size, obj->obj_bkt_ids[0]);
	}

	if (zeroing)
		return umem_zalloc(umm, size);

	return umem_alloc(umm, size);
}

static inline umem_off_t
vos_obj_reserve(struct umem_instance *umm, struct vos_object *obj,
		struct umem_rsrvd_act *rsrvd_scm, daos_size_t size)
{
	if (obj != NULL && vos_pool_is_evictable(vos_obj2pool(obj))) {
		D_ASSERT(obj->obj_bkt_alloted == 1);
		return umem_reserve_from_bucket(umm, rsrvd_scm, size, obj->obj_bkt_ids[0]);
	}

	return umem_reserve(umm, rsrvd_scm, size);
}

/* vos_obj_cache.c */
static inline struct dtx_handle *
clear_cur_dth(struct vos_pool *pool)
{
	struct dtx_handle	*dth;

	dth = vos_dth_get(pool->vp_sysdb);
	vos_dth_set(NULL, pool->vp_sysdb);

	return dth;
}

static inline void
restore_cur_dth(struct vos_pool *pool, struct dtx_handle *dth)
{
	vos_dth_set(dth, pool->vp_sysdb);
}

static inline struct vos_cache_metrics *
store2cache_metrics(struct umem_store *store)
{
	struct vos_pool_metrics	*vpm = (struct vos_pool_metrics *)store->stor_stats;

	return vpm != NULL ? &vpm->vp_cache_metrics : NULL;
}

static inline void
update_page_stats(struct umem_store *store)
{
	struct vos_cache_metrics	*vcm = store2cache_metrics(store);
	struct umem_cache		*cache = store->cache;

	if (vcm == NULL)
		return;

	d_tm_set_gauge(vcm->vcm_pg_ne, cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE]);
	d_tm_set_gauge(vcm->vcm_pg_pinned, cache->ca_pgs_stats[UMEM_PG_STATS_PINNED]);
	d_tm_set_gauge(vcm->vcm_pg_free, cache->ca_pgs_stats[UMEM_PG_STATS_FREE]);

	d_tm_set_counter(vcm->vcm_pg_hit, cache->ca_cache_stats[UMEM_CACHE_STATS_HIT]);
	d_tm_set_counter(vcm->vcm_pg_miss, cache->ca_cache_stats[UMEM_CACHE_STATS_MISS]);
	d_tm_set_counter(vcm->vcm_pg_evict, cache->ca_cache_stats[UMEM_CACHE_STATS_EVICT]);
	d_tm_set_counter(vcm->vcm_pg_flush, cache->ca_cache_stats[UMEM_CACHE_STATS_FLUSH]);
	d_tm_set_counter(vcm->vcm_pg_load, cache->ca_cache_stats[UMEM_CACHE_STATS_LOAD]);
}

static inline int
vos_cache_pin(struct vos_pool *pool, struct umem_cache_range *ranges, int range_nr,
	      bool for_sys, struct umem_pin_handle **pin_handle)
{
	struct umem_store	*store = vos_pool2store(pool);
	struct dtx_handle	*cur_dth;
	int			 rc;

	cur_dth = clear_cur_dth(pool);
	rc = umem_cache_pin(store, ranges, range_nr, for_sys, pin_handle);
	restore_cur_dth(pool, cur_dth);

	update_page_stats(store);

	return rc;
}

int vos_obj_acquire(struct vos_container *cont, daos_unit_oid_t oid, bool pin,
		    struct vos_object **obj_p);

#define	VOS_BKTS_INLINE_MAX	4
struct vos_bkt_array {
	uint32_t	 vba_tot;
	uint32_t	 vba_cnt;
	uint32_t	 vba_inline_bkts[VOS_BKTS_INLINE_MAX];
	uint32_t	*vba_bkts;
};

static inline void
vos_bkt_array_fini(struct vos_bkt_array *bkts)
{
	if (bkts->vba_tot > VOS_BKTS_INLINE_MAX)
		D_FREE(bkts->vba_bkts);
}

static inline void
vos_bkt_array_init(struct vos_bkt_array *bkts)
{
	bkts->vba_tot	= VOS_BKTS_INLINE_MAX;
	bkts->vba_cnt	= 0;
	bkts->vba_bkts	= &bkts->vba_inline_bkts[0];
}

bool vos_bkt_array_subset(struct vos_bkt_array *super, struct vos_bkt_array *sub);
int vos_bkt_array_add(struct vos_bkt_array *bkts, uint32_t bkt_id);
int vos_bkt_array_pin(struct vos_pool *pool, struct vos_bkt_array *bkts,
		      struct umem_pin_handle **pin_hdl);

/** Validate the provided svt.
 *
 * Note: It is designed for catastrophic recovery. Not to perform at run-time.
 *
 * \param svt[in]
 * \param dtx_lid[in]	local id of the DTX entry the evt is supposed to belong to
 *
 * \return true if svt is valid.
 **/
bool
vos_irec_is_valid(const struct vos_irec_df *svt, uint32_t dtx_lid);

enum {
	DTX_UMOFF_ILOG = (1 << 0),
	DTX_UMOFF_SVT  = (1 << 1),
	DTX_UMOFF_EVT  = (1 << 2),
};

static inline void
dtx_type2umoff_flag(umem_off_t *rec, uint32_t type)
{
	uint8_t flag = 0;

	switch (type) {
	case DTX_RT_ILOG:
		flag = DTX_UMOFF_ILOG;
		break;
	case DTX_RT_SVT:
		flag = DTX_UMOFF_SVT;
		break;
	case DTX_RT_EVT:
		flag = DTX_UMOFF_EVT;
		break;
	default:
		D_ASSERT(0);
	}

	umem_off_set_flags(rec, flag);
}

#endif /* __VOS_INTERNAL_H__ */
