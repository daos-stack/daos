/**
 * (C) Copyright 2016-2021 Intel Corporation.
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
#include "vos_tls.h"
#include "vos_layout.h"
#include "vos_ilog.h"
#include "vos_obj.h"

#define VOS_MINOR_EPC_MAX (VOS_SUB_OP_MAX + 1)
D_CASSERT(VOS_MINOR_EPC_MAX == EVT_MINOR_EPC_MAX);

#define VOS_TX_LOG_FAIL(rc, ...)			\
	do {						\
		bool	__is_err = true;		\
							\
		if (rc >= 0)				\
			break;				\
		switch (rc) {				\
		case -DER_TX_RESTART:			\
		case -DER_INPROGRESS:			\
		case -DER_EXIST:			\
		case -DER_NONEXIST:			\
			__is_err = false;		\
			break;				\
		}					\
		D_CDEBUG(__is_err, DLOG_ERR, DB_IO,	\
			 __VA_ARGS__);			\
	} while (0)

#define VOS_TX_TRACE_FAIL(rc, ...)			\
	do {						\
		bool	__is_err = true;		\
							\
		if (rc >= 0)				\
			break;				\
		switch (rc) {				\
		case -DER_TX_RESTART:			\
		case -DER_INPROGRESS:			\
		case -DER_EXIST:			\
		case -DER_NONEXIST:			\
			__is_err = false;		\
			break;				\
		}					\
		D_CDEBUG(__is_err, DLOG_ERR, DB_TRACE,	\
			 __VA_ARGS__);			\
	} while (0)

#define VOS_CONT_ORDER		20	/* Order of container tree */
#define VOS_OBJ_ORDER		20	/* Order of object tree */
#define VOS_KTR_ORDER		23	/* order of d/a-key tree */
#define VOS_SVT_ORDER		5	/* order of single value tree */
#define VOS_EVT_ORDER		23	/* evtree order */
#define DTX_BTREE_ORDER	23	/* Order for DTX tree */
#define VEA_TREE_ODR		20	/* Order of a VEA tree */

extern struct dss_module_key vos_module_key;

#define VOS_POOL_HHASH_BITS 10 /* Up to 1024 pools */
#define VOS_CONT_HHASH_BITS 20 /* Up to 1048576 containers */

#define VOS_BLK_SHIFT		12	/* 4k */
#define VOS_BLK_SZ		(1UL << VOS_BLK_SHIFT) /* bytes */
#define VOS_BLOB_HDR_BLKS	1	/* block */

/** Up to 1 million lid entries split into 16 expansion slots */
#define DTX_ARRAY_LEN		(1 << 20) /* Total array slots for DTX lid */
#define DTX_ARRAY_NR		(1 << 4)  /* Number of expansion arrays */

enum {
	/** Used for marking an in-tree record committed */
	DTX_LID_COMMITTED = 0,
	/** Used for marking an in-tree record aborted */
	DTX_LID_ABORTED,
	/** Reserved local ids */
	DTX_LID_RESERVED,
};

/*
 * When aggregate merge window reaches this size threshold, it will stop
 * growing and trigger window flush immediately.
 */
#define VOS_MW_FLUSH_THRESH	(1UL << 23)	/* 8MB */

/* Force aggregation/discard ULT yield on certain amount of tight loops */
#define VOS_AGG_CREDITS_MAX	32

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

/**
 * VOS pool (DRAM)
 */
struct vos_pool {
	/** VOS uuid hash-link with refcnt */
	struct d_ulink		vp_hlink;
	/** number of openers */
	int			vp_opened:30;
	int			vp_dying:1;
	/** exclusive handle (see VOS_POF_EXCL) */
	int			vp_excl:1;
	/** caller specifies pool is small (for sys space reservation) */
	bool			vp_small;
	/** UUID of vos pool */
	uuid_t			vp_id;
	/** memory attribute of the @vp_umm */
	struct umem_attr	vp_uma;
	/** memory class instance of the pool */
	struct umem_instance	vp_umm;
	/** btr handle for the container table */
	daos_handle_t		vp_cont_th;
	/** GC statistics of this pool */
	struct vos_gc_stat	vp_gc_stat;
	/** link chain on vos_tls::vtl_gc_pools */
	d_list_t		vp_gc_link;
	/** List of open containers with objects in gc pool */
	d_list_t		vp_gc_cont;
	/** address of durable-format pool in SCM */
	struct vos_pool_df	*vp_pool_df;
	/** I/O context */
	struct bio_io_context	*vp_io_ctxt;
	/** In-memory free space tracking for NVMe device */
	struct vea_space_info	*vp_vea_info;
	/** Reserved sys space (for space reclaim, rebuild, etc.) in bytes */
	daos_size_t		vp_space_sys[DAOS_MEDIA_MAX];
	/** Held space by inflight updates. In bytes */
	daos_size_t		vp_space_held[DAOS_MEDIA_MAX];
	/** Dedup hash */
	struct d_hash_table	*vp_dedup_hash;
	/* The count of committed DTXs for the whole pool. */
	uint32_t		 vp_dtx_committed_count;
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
	/* Various flags */
	unsigned int		vc_in_aggregation:1,
				vc_in_discard:1,
				vc_reindex_cmt_dtx:1;
	unsigned int		vc_open_count;
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
	 * If the single object is differet from 'dae_base::dae_oid',
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
	daos_epoch_t			 dae_start_time;
	/* Link into container::vc_dtx_act_list. */
	d_list_t			 dae_link;

	unsigned int			 dae_committable:1,
					 dae_committed:1,
					 dae_aborted:1,
					 dae_maybe_shared:1,
					 dae_prepared:1,
					 dae_resent:1;
};

#ifdef VOS_STANDALONE
#define VOS_TIME_START(start, op)		\
do {						\
	if (vos_tls_get()->vtl_dp == NULL)	\
		break;				\
	start = daos_get_ntime();		\
} while (0)

#define VOS_TIME_END(start, op)			\
do {						\
	struct daos_profile *dp;		\
	int time_msec;				\
						\
	dp = vos_tls_get()->vtl_dp;		\
	if ((dp) == NULL || start == 0)		\
		break;				\
	time_msec = (daos_get_ntime() - start)/1000; \
	daos_profile_count(dp, op, time_msec);	\
} while (0)

#else

#define VOS_TIME_START(start, op) D_TIME_START(start, op)
#define VOS_TIME_END(start, op) D_TIME_END(start, op)

#endif

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
					 dce_invalid:1,
					 dce_resent:1;
};

#define DCE_XID(dce)		((dce)->dce_base.dce_xid)
#define DCE_EPOCH(dce)		((dce)->dce_base.dce_epoch)

extern int vos_evt_feats;

#define VOS_KEY_CMP_LEXICAL	(1ULL << 63)

#define VOS_KEY_CMP_UINT64_SET	(BTR_FEAT_UINT_KEY)
#define VOS_KEY_CMP_LEXICAL_SET	(VOS_KEY_CMP_LEXICAL | BTR_FEAT_DIRECT_KEY)
#define VOS_OFEAT_SHIFT		48
#define VOS_OFEAT_MASK		(0x0ffULL   << VOS_OFEAT_SHIFT)
#define VOS_OFEAT_BITS		(0x0ffffULL << VOS_OFEAT_SHIFT)

/** Iterator ops for objects and OIDs */
extern struct vos_iter_ops vos_oi_iter_ops;
extern struct vos_iter_ops vos_obj_iter_ops;
extern struct vos_iter_ops vos_cont_iter_ops;
extern struct vos_iter_ops vos_dtx_iter_ops;

static inline void
vos_pool_addref(struct vos_pool *pool)
{
	d_uhash_link_addref(vos_pool_hhash_get(), &pool->vp_hlink);
}

static inline void
vos_pool_decref(struct vos_pool *pool)
{
	d_uhash_link_putref(vos_pool_hhash_get(), &pool->vp_hlink);
}

static inline void
vos_pool_hash_del(struct vos_pool *pool)
{
	d_uhash_link_delete(vos_pool_hhash_get(), &pool->vp_hlink);
}

/**
 * Getting object cache
 * Wrapper for TLS and standalone mode
 */
static inline struct daos_lru_cache *
vos_get_obj_cache(void)
{
	return vos_tls_get()->vtl_ocache;
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
			   daos_epoch_t epoch, uint32_t intent, uint32_t type);

/**
 * Get local entry DTX state. Only used by VOS aggregation.
 *
 * \param entry		[IN]	DTX local id
 *
 * \return		DTX_ST_COMMITTED, DTX_ST_PREPARED or
 *			DTX_ST_ABORTED.
 */
static inline unsigned int
vos_dtx_ent_state(uint32_t entry)
{
	switch (entry) {
	case DTX_LID_COMMITTED:
		return DTX_ST_COMMITTED;
	case DTX_LID_ABORTED:
		return DTX_ST_ABORTED;
	default:
		return DTX_ST_PREPARED;
	}
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
vos_dtx_get(void);

/**
 * Deregister the record from the DTX entry.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param coh		[IN]	The container open handle.
 * \param entry		[IN]	The local DTX id.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param record	[IN]	Address (offset) of the record to be
 *				deregistered.
 */
void
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
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int count, daos_epoch_t epoch,
			bool resent, bool *rm_cos,
			struct vos_dtx_act_ent **daes,
			struct vos_dtx_cmt_ent **dces);
void
vos_dtx_post_handle(struct vos_container *cont,
		    struct vos_dtx_act_ent **daes,
		    struct vos_dtx_cmt_ent **dces,
		    int count, bool abort, bool rollback);

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

/** Size of metadata without user payload */
static inline uint64_t
vos_irec_msize(struct vos_rec_bundle *rbund)
{
	uint64_t size = 0;

	if (rbund->rb_csum != NULL)
		size = vos_size_round(rbund->rb_csum->cs_len);
	return size + sizeof(struct vos_irec_df);
}

static inline uint64_t
vos_irec_size(struct vos_rec_bundle *rbund)
{
	return vos_irec_msize(rbund) + rbund->rb_rsize;
}

static inline bool
vos_irec_size_equal(struct vos_irec_df *irec, struct vos_rec_bundle *rbund)
{
	if (irec->ir_size != rbund->rb_rsize)
		return false;

	if (vos_irec2csum_size(irec) != rbund->rb_csum->cs_len)
		return false;

	return true;
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

/**
 * operation code for VOS iterator.
 */
enum vos_iter_opc {
	IT_OPC_NOOP,
	IT_OPC_FIRST,
	IT_OPC_LAST,
	IT_OPC_PROBE,
	IT_OPC_NEXT,
};

struct vos_iter_ops;

/** the common part of vos iterators */
struct vos_iterator {
	struct dtx_handle	*it_dth;
	struct vos_iter_ops	*it_ops;
	struct vos_iterator	*it_parent; /* parent iterator */
	struct vos_ts_set	*it_ts_set;
	daos_epoch_t		 it_bound;
	vos_iter_type_t		 it_type;
	enum vos_iter_state	 it_state;
	uint32_t		 it_ref_cnt;
	uint32_t		 it_from_parent:1,
				 it_for_purge:1,
				 it_for_migration:1,
				 it_cleanup_stale_dtx:1,
				 it_ignore_uncommitted:1;
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
		/* oid to hold */
		daos_unit_oid_t	 ii_oid;
	};
	/* needed to open nested tree */
	struct umem_attr	*ii_uma;
	/* needed to open nested tree */
	struct vea_space_info	*ii_vea_info;
	/* Reference to vos object, set in iop_tree_prepare. */
	struct vos_object	*ii_obj;
	d_iov_t			*ii_akey; /* conditional akey */
	/** address range (RECX); rx_nr == 0 means entire range (0:~0ULL) */
	daos_recx_t              ii_recx;
	daos_epoch_range_t	 ii_epr;
	/** highest epoch where parent obj/key was punched */
	struct vos_punch_record	 ii_punched;
	/** epoch logic expression for the iterator. */
	vos_it_epc_expr_t	 ii_epc_expr;
	/** iterator flags */
	uint32_t		 ii_flags;

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
			     daos_anchor_t *anchor);
	/** move forward the iterating cursor */
	int	(*iop_next)(struct vos_iterator *iter);
	/** fetch the record that the cursor points to */
	int	(*iop_fetch)(struct vos_iterator *iter,
			     vos_iter_entry_t *it_entry,
			     daos_anchor_t *anchor);
	/** copy out the record data */
	int	(*iop_copy)(struct vos_iterator *iter,
			    vos_iter_entry_t *it_entry, d_iov_t *iov_out);
	/** Delete the record that the cursor points to */
	int	(*iop_delete)(struct vos_iterator *iter,
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

/** iterator for dkey/akey/recx */
struct vos_obj_iter {
	/* public part of the iterator */
	struct vos_iterator	 it_iter;
	/** Incarnation log entries for current iterator */
	struct vos_ilog_info	 it_ilog_info;
	/** handle of iterator */
	daos_handle_t		 it_hdl;
	/** condition of the iterator: epoch logic expression */
	vos_it_epc_expr_t	 it_epc_expr;
	/** iterator flags */
	uint32_t		 it_flags;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	 it_epr;
	/** highest epoch where parent obj/key was punched */
	struct vos_punch_record	 it_punched;
	/** condition of the iterator: attribute key */
	daos_key_t		 it_akey;
	/* reference on the object */
	struct vos_object	*it_obj;
	/** condition of the iterator: extent range */
	daos_recx_t              it_recx;
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
	SUBTR_CREATE	= (1 << 0),	/**< may create the subtree */
	SUBTR_EVT	= (1 << 1),	/**< subtree is evtree */
};

/* vos_common.c */
int
vos_bio_addr_free(struct vos_pool *pool, bio_addr_t *addr, daos_size_t nob);

void
vos_evt_desc_cbs_init(struct evt_desc_cbs *cbs, struct vos_pool *pool,
		      daos_handle_t coh);

/* Reserve SCM through umem_reserve() for a PMDK transaction */
struct vos_rsrvd_scm {
	unsigned int		rs_actv_cnt;
	unsigned int		rs_actv_at;
	struct pobj_action	rs_actv[0];
};

int
vos_tx_begin(struct dtx_handle *dth, struct umem_instance *umm);

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
 * \param[in]	err		the error code
 *
 * \return	err if non-zero, otherwise 0 or appropriate error
 */
int
vos_tx_end(struct vos_container *cont, struct dtx_handle *dth_in,
	   struct vos_rsrvd_scm **rsrvd_scmp, d_list_t *nvme_exts, bool started,
	   int err);

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
	       uint64_t flags, struct vos_ts_set *ts_set,
	       struct vos_ilog_info *parent, struct vos_ilog_info *info);
int
key_tree_delete(struct vos_object *obj, daos_handle_t toh, d_iov_t *key_iov);

/* vos_io.c */
daos_size_t
vos_recx2irec_size(daos_size_t rsize, struct dcs_csum_info *csum);

/*
 * A simple media selection policy embedded in VOS, which select media by
 * akey type and record size.
 */
static inline uint16_t
vos_media_select(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size)
{
	if (pool->vp_vea_info == NULL)
		return DAOS_MEDIA_SCM;

	return (size >= VOS_BLK_SZ) ? DAOS_MEDIA_NVME : DAOS_MEDIA_SCM;
}

int
vos_dedup_init(struct vos_pool *pool);
void
vos_dedup_fini(struct vos_pool *pool);
void
vos_dedup_invalidate(struct vos_pool *pool);

umem_off_t
vos_reserve_scm(struct vos_container *cont, struct vos_rsrvd_scm *rsrvd_scm,
		daos_size_t size);
int
vos_publish_scm(struct vos_container *cont, struct vos_rsrvd_scm *rsrvd_scm,
		bool publish);
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
	    enum vos_gc_type type, umem_off_t item_off, uint64_t args);
int
vos_gc_pool_tight(daos_handle_t poh, int *credits);
void
gc_reserve_space(daos_size_t *rsrvd);


/**
 * Aggregate the creation/punch records in the current entry of the object
 * iterator
 *
 * \param ih[IN]	Iterator handle
 * \param discard[IN]	Discard all entries (within the iterator epoch range)
 *
 * \return		Zero on Success
 *			1 if a reprobe is needed (entry is removed or not
 *			visible)
 *			negative value otherwise
 */
int
oi_iter_aggregate(daos_handle_t ih, bool discard);

/**
 * Aggregate the creation/punch records in the current entry of the key
 * iterator
 *
 * \param ih[IN]	Iterator handle
 * \param discard[IN]	Discard all entries (within the iterator epoch range)
 *
 * \return		Zero on Success
 *			1 if a reprobe is needed (entry is removed or not
 *			visible)
 *			negative value otherwise
 */
int
vos_obj_iter_aggregate(daos_handle_t ih, bool discard);

/** Internal bit for initializing iterator from open tree handle */
#define VOS_IT_KEY_TREE	(1 << 31)
/** Ensure there is no overlap with public iterator flags (defined in
 *  src/include/daos_srv/vos_types.h).
 */
D_CASSERT((VOS_IT_KEY_TREE & VOS_IT_MASK) == 0);

/** Internal vos iterator API for iterating through keys using an
 *  open tree handle to initialize the iterator
 *
 *  \param obj[IN]			VOS object
 *  \param toh[IN]			Open key tree handle
 *  \param type[IN]			Iterator type (VOS_ITER_AKEY/DKEY only)
 *  \param epr[IN]			Valid epoch range for iteration
 *  \param ignore_inprogress[IN]	Fail if there are uncommitted entries
 *  \param cb[IN]			Callback for key
 *  \param arg[IN]			argument to pass to callback
 *  \param dth[IN]			dtx handle
 */
int
vos_iterate_key(struct vos_object *obj, daos_handle_t toh, vos_iter_type_t type,
		const daos_epoch_range_t *epr, bool ignore_inprogress,
		vos_iter_cb_t cb, void *arg, struct dtx_handle *dth);

/** Start epoch of vos */
extern daos_epoch_t	vos_start_epoch;

/* Slab allocation */
enum {
	VOS_SLAB_OBJ_NODE	= 0,
	VOS_SLAB_KEY_NODE	= 1,
	VOS_SLAB_SV_NODE	= 2,
	VOS_SLAB_EVT_NODE	= 3,
	VOS_SLAB_EVT_DESC	= 4,
	VOS_SLAB_OBJ_DF		= 5,
	VOS_SLAB_EVT_NODE_SM	= 6,
	VOS_SLAB_MAX		= 7
};
D_CASSERT(VOS_SLAB_MAX <= UMM_SLABS_CNT);

static inline umem_off_t
vos_slab_alloc(struct umem_instance *umm, int size, int slab_id)
{
	/* evtree unit tests may skip slab register in vos_pool_open() */
	D_ASSERTF(!umem_slab_registered(umm, slab_id) ||
		  size == umem_slab_usize(umm, slab_id),
		  "registered: %d, id: %d, size: %d != %zu\n",
		  umem_slab_registered(umm, slab_id),
		  slab_id, size, umem_slab_usize(umm, slab_id));

	return umem_alloc_verb(umm, umem_slab_flags(umm, slab_id) |
					POBJ_FLAG_ZERO, size);
}

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
vos_dtx_hit_inprogress(void)
{
	struct dtx_handle	*dth = vos_dth_get();

	return dth != NULL && dth->dth_share_tbd_count > 0;
}

static inline bool
vos_dtx_continue_detect(int rc)
{
	struct dtx_handle	*dth = vos_dth_get();

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

int
vos_pool_settings_init(void);

#endif /* __VOS_INTERNAL_H__ */
