/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include <daos_srv/daos_server.h>
#include <daos_srv/bio.h>
#include <vos_layout.h>
#include <vos_ilog.h>
#include <vos_obj.h>

#define VOS_CONT_ORDER		20	/* Order of container tree */
#define VOS_OBJ_ORDER		20	/* Order of object tree */
#define VOS_KTR_ORDER		23	/* order of d/a-key tree */
#define VOS_SVT_ORDER		5	/* order of single value tree */
#define VOS_EVT_ORDER		23	/* evtree order */
#define DTX_BTREE_ORDER		23	/* Order for DTX tree */



#define DAOS_VOS_VERSION 1

extern struct dss_module_key vos_module_key;

#define VOS_POOL_HHASH_BITS 10 /* Upto 1024 pools */
#define VOS_CONT_HHASH_BITS 20 /* Upto 1048576 containers */

#define VOS_BLK_SHIFT		12	/* 4k */
#define VOS_BLK_SZ		(1UL << VOS_BLK_SHIFT) /* bytes */
#define VOS_BLOB_HDR_BLKS	1	/* block */

/** hash seed for murmur hash */
#define VOS_BTR_MUR_SEED	0xC0FFEE
/*
 * When aggregate merge window reaches this size threshold, it will stop
 * growing and trigger window flush immediately.
 */
#define VOS_MW_FLUSH_THRESH	(1UL << 23)	/* 8MB */

/* Force aggregation/discard ULT yield on certain amount of tight loops */
#define VOS_AGG_CREDITS_MAX	256

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

/**
 * VOS pool (DRAM)
 */
struct vos_pool {
	/** VOS uuid hash-link with refcnt */
	struct d_ulink		vp_hlink;
	/** number of openers */
	int			vp_opened:30;
	int			vp_dying:1;
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
	/** address of durable-format pool in SCM */
	struct vos_pool_df	*vp_pool_df;
	/** I/O context */
	struct bio_io_context	*vp_io_ctxt;
	/** In-memory free space tracking for NVMe device */
	struct vea_space_info	*vp_vea_info;
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
	/* The handle for active DTX table */
	daos_handle_t		vc_dtx_active_hdl;
	/* The handle for committed DTX table */
	daos_handle_t		vc_dtx_committed_hdl;
	/* The objects with committable DTXs in DRAM. */
	daos_handle_t		vc_dtx_cos_hdl;
	/** The root of the B+ tree for ative DTXs. */
	struct btr_root		vc_dtx_active_btr;
	/** The root of the B+ tree for committed DTXs. */
	struct btr_root		vc_dtx_committed_btr;
	/* The DTX COS-btree. */
	struct btr_root		vc_dtx_cos_btr;
	/* The global list for committable DTXs. */
	d_list_t		vc_dtx_committable_list;
	/* The global list for committed DTXs. */
	d_list_t		vc_dtx_committed_list;
	/* The temporary list for committed DTXs during re-index. */
	d_list_t		vc_dtx_committed_tmp_list;
	/* The count of committable DTXs. */
	uint32_t		vc_dtx_committable_count;
	/* The count of committed DTXs. */
	uint32_t		vc_dtx_committed_count;
	/* The items count in vc_dtx_committed_tmp_list. */
	uint32_t		vc_dtx_committed_tmp_count;
	/** Direct pointer to the VOS container */
	struct vos_cont_df	*vc_cont_df;
	/**
	 * Corresponding in-memory block allocator hints for the
	 * durable hints in vos_cont_df
	 */
	struct vea_hint_context	*vc_hint_ctxt[VOS_IOS_CNT];
	/* Various flags */
	unsigned int		vc_in_aggregation:1,
				vc_abort_aggregation:1,
				vc_reindex_cmt_dtx:1;
	unsigned int		vc_open_count;
	uint64_t		vc_dtx_resync_gen;
};

struct vos_dtx_act_ent {
	struct vos_dtx_act_ent_df	 dae_base;
	umem_off_t			 dae_df_off;
	struct vos_dtx_blob_df		*dae_dbd;
	/* More DTX records if out of the inlined buffer. */
	struct vos_dtx_record_df	*dae_records;
	/* The capacity of dae_records, NOT including the inlined buffer. */
	int				 dae_rec_cap;
};

#define DAE_XID(dae)		((dae)->dae_base.dae_xid)
#define DAE_OID(dae)		((dae)->dae_base.dae_oid)
#define DAE_DKEY_HASH(dae)	((dae)->dae_base.dae_dkey_hash)
#define DAE_EPOCH(dae)		((dae)->dae_base.dae_epoch)
#define DAE_SRV_GEN(dae)	((dae)->dae_base.dae_srv_gen)
#define DAE_LAYOUT_GEN(dae)	((dae)->dae_base.dae_layout_gen)
#define DAE_INTENT(dae)		((dae)->dae_base.dae_intent)
#define DAE_INDEX(dae)		((dae)->dae_base.dae_index)
#define DAE_REC_INLINE(dae)	((dae)->dae_base.dae_rec_inline)
#define DAE_FLAGS(dae)		((dae)->dae_base.dae_flags)
#define DAE_REC_CNT(dae)	((dae)->dae_base.dae_rec_cnt)
#define DAE_REC_OFF(dae)	((dae)->dae_base.dae_rec_off)

struct vos_dtx_cmt_ent {
	/* Link into vos_conter::vc_dtx_committed_list */
	d_list_t			 dce_committed_link;
	struct vos_dtx_cmt_ent_df	 dce_base;
	uint32_t			 dce_reindex:1,
					 dce_exist:1;
};

#define DCE_XID(dce)		((dce)->dce_base.dce_xid)
#define DCE_EPOCH(dce)		((dce)->dce_base.dce_epoch)

struct vos_imem_strts {
	/**
	 * In-memory object cache for the PMEM
	 * object table
	 */
	struct daos_lru_cache	*vis_ocache;
	/** Hash table to refcount VOS handles */
	/** (container/pool, etc.,) */
	struct d_hash_table	*vis_pool_hhash;
	struct d_hash_table	*vis_cont_hhash;
};
/* in-memory structures standalone instance */
struct bio_xs_context		*vsa_xsctxt_inst;
extern int vos_evt_feats;

static inline struct bio_xs_context *
vos_xsctxt_get(void)
{
#ifdef VOS_STANDALONE
	return vsa_xsctxt_inst;
#else
	return dss_get_module_info()->dmi_nvme_ctxt;
#endif
}

enum {
	VOS_KEY_CMP_UINT64	= (1ULL << 63),
	VOS_KEY_CMP_LEXICAL	= (1ULL << 62),
	VOS_KEY_CMP_ANY		= (VOS_KEY_CMP_UINT64 | VOS_KEY_CMP_LEXICAL),
};

#define VOS_KEY_CMP_UINT64_SET	(VOS_KEY_CMP_UINT64  | BTR_FEAT_DIRECT_KEY)
#define VOS_KEY_CMP_LEXICAL_SET	(VOS_KEY_CMP_LEXICAL | BTR_FEAT_DIRECT_KEY)
#define VOS_OFEAT_SHIFT		48
#define VOS_OFEAT_MASK		(0x0ffULL   << VOS_OFEAT_SHIFT)
#define VOS_OFEAT_BITS		(0x0ffffULL << VOS_OFEAT_SHIFT)

/** Iterator ops for objects and OIDs */
extern struct vos_iter_ops vos_oi_iter_ops;
extern struct vos_iter_ops vos_obj_iter_ops;
extern struct vos_iter_ops vos_cont_iter_ops;
extern struct vos_iter_ops vos_dtx_iter_ops;

/** VOS thread local storage structure */
struct vos_tls {
	/* in-memory structures TLS instance */
	/* TODO: move those members to vos_tls, nosense to have another
	 * data structure for it.
	 */
	struct vos_imem_strts		 vtl_imems_inst;
	/** pools registered for GC */
	d_list_t			 vtl_gc_pools;
	/* PMDK transaction stage callback data */
	struct umem_tx_stage_data	 vtl_txd;
	/** XXX: The DTX handle.
	 *
	 *	 Transferring DTX handle via TLS can avoid much changing
	 *	 of existing functions' interfaces, and avoid the corner
	 *	 cases that someone may miss to set the DTX handle when
	 *	 operate related tree.
	 *
	 *	 But honestly, it is some hack to pass the DTX handle via
	 *	 the TLS. It requires that there is no CPU yield during the
	 *	 processing. Otherwise, the vtl_dth may be changed by other
	 *	 ULTs. The user needs to guarantee that by itself.
	 */
	struct dtx_handle		*vtl_dth;
};

struct vos_tls *
vos_tls_get();

static inline struct d_hash_table *
vos_pool_hhash_get(void)
{
	return vos_tls_get()->vtl_imems_inst.vis_pool_hhash;
}

static inline struct d_hash_table *
vos_cont_hhash_get(void)
{
	return vos_tls_get()->vtl_imems_inst.vis_cont_hhash;
}

static inline struct umem_tx_stage_data *
vos_txd_get(void)
{
	return &vos_tls_get()->vtl_txd;
}

static inline struct dtx_handle *
vos_dth_get(void)
{
	return vos_tls_get()->vtl_dth;
}

static inline void
vos_dth_set(struct dtx_handle *dth)
{
	D_ASSERT(dth == NULL || vos_tls_get()->vtl_dth == NULL);

	vos_tls_get()->vtl_dth = dth;
}

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
struct daos_lru_cache *vos_get_obj_cache(void);

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
 * \param cont_df	[IN]	Pointer to the on-disk VOS containter.
 */
void
vos_dtx_table_destroy(struct umem_instance *umm, struct vos_cont_df *cont_df);

/**
 * Register dbtree class for DTX table, it is called within vos_init().
 *
 * \return		0 on success and negative on failure
 */
int
vos_dtx_table_register(void);

/**
 * Check whether the record (to be accessible) is available to outside or not.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param coh		[IN]	The container open handle.
 * \param entry		[IN]	Address (offset) of the DTX to be checked.
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
vos_dtx_check_availability(struct umem_instance *umm, daos_handle_t coh,
			   umem_off_t entry, uint32_t intent, uint32_t type);

/**
 * Register the record (to be modified) to the DTX entry.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param record	[IN]	Address (offset) of the record (in SCM)
 *				to associate witht the transaction.
 * \param type		[IN]	The record type, see vos_dtx_record_types.
 * \param dtx		[OUT]	tx_id is returned.  Caller is responsible
 *				to save it in the record.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_register_record(struct umem_instance *umm, umem_off_t record,
			uint32_t type, umem_off_t *tx_id);

/**
 * Cleanup DTX handle (in DRAM things) when related PMDK transaction failed.
 */
void
vos_dtx_cleanup_dth(struct dtx_handle *dth);

/** Return the already active dtx id, if any */
umem_off_t
vos_dtx_get(void);

/**
 * Deregister the record from the DTX entry.
 *
 * \param umm		[IN]	Instance of an unified memory class.
 * \param coh		[IN]	The container open handle.
 * \param entry		[IN]	The DTX entry address (offset).
 * \param record	[IN]	Address (offset) of the record to be
 *				deregistered.
 */
void
vos_dtx_deregister_record(struct umem_instance *umm, daos_handle_t coh,
			  umem_off_t entry, umem_off_t record);

/**
 * Mark the DTX as prepared locally.
 *
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_prepared(struct dtx_handle *dth);

int
vos_dtx_commit_internal(struct vos_container *cont, struct dtx_id *dtis,
			int counti, daos_epoch_t epoch);

/**
 * Register dbtree class for DTX CoS, it is called within vos_init().
 *
 * \return		0 on success and negative on failure.
 */
int
vos_dtx_cos_register(void);

/**
 * Remove the DTX from the CoS cache.
 *
 * \param cont		[IN]	Pointer to the container.
 * \param oid		[IN]	Pointer to the object ID.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param punch		[IN]	For punch DTX or not.
 *
 * \return		Zero on success.
 * \return		Other negative value if error.
 */
int
vos_dtx_del_cos(struct vos_container *cont, daos_unit_oid_t *oid,
		struct dtx_id *xid, uint64_t dkey_hash, bool punch);

/**
 * Query the oldest DTX's timestamp in the CoS cache.
 *
 * \param cont	[IN]	Pointer to the container.
 *
 * \return		The oldest DTX's timestamp in the CoS cache.
 *			Zero if the CoS cache is empty.
 */
uint64_t
vos_dtx_cos_oldest(struct vos_container *cont);

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
	/** DAOS two-phase commit transation table (active) */
	VOS_BTR_DTX_ACT_TABLE	= (VOS_BTR_BEGIN + 5),
	/** DAOS two-phase commit transation table (committed) */
	VOS_BTR_DTX_CMT_TABLE	= (VOS_BTR_BEGIN + 6),
	/** The objects with committable DTXs in DRAM */
	VOS_BTR_DTX_COS		= (VOS_BTR_BEGIN + 7),
	/** The VOS incarnation log tree */
	VOS_BTR_ILOG		= (VOS_BTR_BEGIN + 8),
	/** the last reserved tree class */
	VOS_BTR_END,
};

int obj_tree_init(struct vos_object *obj);
int obj_tree_fini(struct vos_object *obj);
int obj_tree_register(void);

/**
 * Data structure which carries the keys, epoch ranges to the multi-nested
 * btree.
 */
struct vos_key_bundle {
	/** key for the current tree, could be @kb_dkey or @kb_akey */
	daos_key_t		*kb_key;
	/** epoch of the I/O */
	daos_epoch_t		 kb_epoch;
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
	bool			 rb_flat;
};

/**
 * Inline data structure for embedding the key bundle and key into an anchor
 * for serialization.
 */
#define	EMBEDDED_KEY_MAX	96
struct vos_embedded_key {
	/** Inlined iov kbund references */
	d_iov_t		ek_kiov;
	/** Inlined buffer the kiov references*/
	unsigned char	ek_key[EMBEDDED_KEY_MAX];
};
D_CASSERT(sizeof(struct vos_embedded_key) == DAOS_ANCHOR_BUF_MAX);

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
	/** iterator has valide cursor (user can call next/probe) */
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
	struct vos_iter_ops	*it_ops;
	struct vos_iterator	*it_parent; /* parent iterator */
	vos_iter_type_t		 it_type;
	enum vos_iter_state	 it_state;
	uint32_t		 it_ref_cnt;
	uint32_t		 it_from_parent:1,
				 it_for_purge:1,
				 it_for_rebuild:1;
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
	daos_epoch_range_t	 ii_epr;
	/** highest epoch where parent obj/key was punched */
	daos_epoch_t		 ii_punched;
	/** epoch logic expression for the iterator. */
	vos_it_epc_expr_t	 ii_epc_expr;
	/** iterator flags */
	uint32_t		 ii_flags;

};

/** function table for vos iterator */
struct vos_iter_ops {
	/** prepare a new iterator with the specified type and parameters */
	int	(*iop_prepare)(vos_iter_type_t type, vos_iter_param_t *param,
			       struct vos_iterator **iter_pp);
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
	/** finalise a iterator */
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
	daos_epoch_t		 it_punched;
	/** condition of the iterator: attribute key */
	daos_key_t		 it_akey;
	/* reference on the object */
	struct vos_object	*it_obj;
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
 * into dbtree operations as a compound key.
 */
static inline void
tree_key_bundle2iov(struct vos_key_bundle *kbund, d_iov_t *iov)
{
	memset(kbund, 0, sizeof(*kbund));
	d_iov_set(iov, kbund, sizeof(*kbund));
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

int
vos_bio_addr_free(struct vos_pool *pool, bio_addr_t *addr, daos_size_t nob);

void
vos_evt_desc_cbs_init(struct evt_desc_cbs *cbs, struct vos_pool *pool,
		      daos_handle_t coh);

/* vos_obj.c */
int
key_tree_prepare(struct vos_object *obj, daos_handle_t toh,
		 enum vos_tree_class tclass, daos_key_t *key, int flags,
		 uint32_t intent, struct vos_krec_df **krecp,
		 daos_handle_t *sub_toh);
void
key_tree_release(daos_handle_t toh, bool is_array);
int
key_tree_punch(struct vos_object *obj, daos_handle_t toh, daos_epoch_t epoch,
	       d_iov_t *key_iov, d_iov_t *val_iov, int flags);

/* vos_io.c */
uint16_t
vos_media_select(struct vos_container *cont, daos_iod_type_t type,
		 daos_size_t size);
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
	if (iter->it_for_rebuild)
		return DAOS_INTENT_REBUILD;
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
gc_add_item(struct vos_pool *pool, enum vos_gc_type type, umem_off_t item_off,
	    uint64_t args);

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

#endif /* __VOS_INTERNAL_H__ */
