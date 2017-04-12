/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <daos/list.h>
#include <daos/hash.h>
#include <daos/btree.h>
#include <daos/common.h>
#include <daos/lru.h>
#include <daos_srv/daos_server.h>
#include <vos_layout.h>
#include <vos_obj.h>

extern struct dss_module_key vos_module_key;

#define VOS_POOL_HHASH_BITS 10 /* Upto 1024 pools */
#define VOS_CONT_HHASH_BITS 20 /* Upto 1048576 containers */

/**
 * VOS cookie index table
 * In-memory BTR index to hold all cookies and max epoch updated
 */
struct vos_cookie_itab {
	struct btr_root		cit_btr;
};

/**
 * VOS pool (DRAM)
 */
struct vos_pool {
	/** VOS uuid hash-link with refcnt */
	struct daos_ulink	vp_hlink;
	/** number of openers */
	int			vp_opened;
	/** UUID of vos pool */
	uuid_t			vp_id;
	/** memory attribute of the @vp_umm */
	struct umem_attr	vp_uma;
	/** memory class instance of the pool */
	struct umem_instance	vp_umm;
	/** btr handle for the container index table */
	daos_handle_t		vp_cont_ith;
	/** cookie index table (DRAM only) */
	struct vos_cookie_itab	vp_cookie_itab;
	/** btr handle for the cookie index table */
	daos_handle_t		vp_cookie_ith;
};

/**
 * VOS container handle (DRAM)
 */
struct vc_hdl {
	/* VOS uuid hash with refcnt */
	struct daos_ulink	vc_uhlink;
	/* VOS PMEMobjpool pointer */
	struct vos_pool		*vc_pool;
	/* Unique UID of VOS container */
	uuid_t			vc_id;
	/* DAOS handle for object index btree */
	daos_handle_t		vc_btr_hdl;
	/* Direct pointer to VOS object index
	 * within container
	 */
	struct vos_object_index	*vc_obj_table;
	/** Diret pointer to the VOS container */
	struct vos_container	*vc_co;
};

struct vos_imem_strts {
	/**
	 * In-memory object cache for the PMEM
	 * object table
	 */
	struct daos_lru_cache	*vis_ocache;
	/** Hash table to refcount VOS handles */
	/** (container/pool, etc.,) */
	struct dhash_table	*vis_pool_hhash;
	struct dhash_table	*vis_cont_hhash;
};

/* in-memory structures standalone instance */
struct vos_imem_strts	*vsa_imems_inst;

/**
 * Reference of a cached object.
 * NB: DRAM data structure.
 */
struct vos_obj_ref {
	/** llink for daos lru cache */
	struct daos_llink		or_llink;
	/** Key for searching, object ID within a container */
	daos_unit_oid_t			or_oid;
	/** VOS object reference Key size */
	unsigned int			or_ksize;
	/** dkey tree open handle of the object */
	daos_handle_t			or_toh;
	/** btree iterator handle */
	daos_handle_t			or_ih;
	/** Persistent memory ID for the object */
	struct vos_obj			*or_obj;
	/** Container Handle - Convenience */
	struct vc_hdl			*or_co;
};

/** Iterator ops for objects and OIDs */
extern struct vos_iter_ops vos_obj_iter_ops;
extern struct vos_iter_ops vos_oid_iter_ops;
extern struct vos_iter_ops vos_co_iter_ops;

/** VOS thread local storage structure */
struct vos_tls {
	/* in-memory structures TLS instance */
	struct vos_imem_strts	vtl_imems_inst;
};

static inline struct vos_tls *
vos_tls_get()
{
	struct vos_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct vos_tls *)dss_module_key_get(dtc, &vos_module_key);
	return tls;
}

static inline struct dhash_table *
vos_pool_hhash_get(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_pool_hhash;
#else
	return vos_tls_get()->vtl_imems_inst.vis_pool_hhash;
#endif
}

static inline struct dhash_table *
vos_cont_hhash_get(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_cont_hhash;
#else
	return vos_tls_get()->vtl_imems_inst.vis_cont_hhash;
#endif
}

extern pthread_mutex_t vos_pmemobj_lock;

static inline PMEMobjpool *
vos_pmemobj_create(const char *path, const char *layout, size_t poolsize,
		   mode_t mode)
{
	PMEMobjpool *pop;

	pthread_mutex_lock(&vos_pmemobj_lock);
	pop = pmemobj_create(path, layout, poolsize, mode);
	pthread_mutex_unlock(&vos_pmemobj_lock);
	return pop;
}

static inline PMEMobjpool *
vos_pmemobj_open(const char *path, const char *layout)
{
	PMEMobjpool *pop;

	pthread_mutex_lock(&vos_pmemobj_lock);
	pop = pmemobj_open(path, layout);
	pthread_mutex_unlock(&vos_pmemobj_lock);
	return pop;
}

static inline void
vos_pmemobj_close(PMEMobjpool *pop)
{
	pthread_mutex_lock(&vos_pmemobj_lock);
	pmemobj_close(pop);
	pthread_mutex_unlock(&vos_pmemobj_lock);
}

static inline struct vos_pool_df *
vos_pool_pop2df(PMEMobjpool *pop)
{
	TOID(struct vos_pool_df) pool_df;

	pool_df = POBJ_ROOT(pop, struct vos_pool_df);
	return D_RW(pool_df);
}

static inline PMEMobjpool *
vos_pool_ptr2pop(struct vos_pool *pool)
{
	return pool->vp_uma.uma_u.pmem_pool;
}

static inline struct vos_pool_df *
vos_pool_ptr2df(struct vos_pool *pool)
{
	return vos_pool_pop2df(vos_pool_ptr2pop(pool));
}

static inline void
vos_pool_addref(struct vos_pool *pool)
{
	daos_uhash_link_addref(vos_pool_hhash_get(), &pool->vp_hlink);
}

static inline void
vos_pool_decref(struct vos_pool *pool)
{
	daos_uhash_link_decref(vos_pool_hhash_get(), &pool->vp_hlink);
}

/**
 * Generate CRC64 hash for any key
 *
 * \param key	[IN]	Key for generating hash
 * \param size	[IN]	Size of the key
 *
 * \return		64-bit Hash value for the
 *			key
*/
uint64_t
vos_generate_crc64(void *key, uint64_t size);

/**
 * Generate Jump Consistent Hash for a key
 *
 * \param key		[IN]	64-bit hash of a key
 * \param num_buckets	[IN]	number of buckets
 *
 * \return			Bucket id
 */
int32_t
vos_generate_jch(uint64_t key, uint32_t num_buckets);

PMEMobjpool *vos_coh2pop(daos_handle_t coh);

/**
 * Getting object cache
 * Wrapper for TLS and standalone mode
 */
struct daos_lru_cache *vos_get_obj_cache(void);

/**
 * VOS container index class register for btree
 * to be called withing vos_init()
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_ci_init();

/**
 * VOS Container index create
 * Create a new B-tree for empty container index
 * Called from vos_pool_create.
 *
 * \param p_umem_attr	[IN]	Pool umem attributes
 * \param co_index	[IN]	vos container index
 *				(pmem direct pointer)
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_ci_create(struct umem_attr *p_umem_attr,
	      struct vos_container_index *co_index);


/**
 * VOS cookie index class register for btree
 * to be called withing vos_init()
 *
 * \return		0 on success and negative on
 *			failure
 */

int
vos_cookie_itab_init();

/**
 * create a VOS Cookie index table.
 *
 * \param uma		[IN]	universal memory attributes
 * \param cookie_index	[IN]	vos cookie index
 * \param cookie_handle [OUT]	cookie_btree handle
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_cookie_itab_create(struct umem_attr *uma, struct vos_cookie_itab *itab,
		       daos_handle_t *cookie_handle);


/**
 * Destroy the cookie index table
 *
 * \param cih	[IN]	cookie index handle
 */
int
vos_cookie_itab_destroy(daos_handle_t cih);

/**
 * VOS cookie update
 *
 * \param cih		[IN]	cookie index handle
 * \param cookie	[IN]	cookie
 * \param epoch		[IN]	epoch
 *
 * \return		0 on success -DER_NONEXIST when
 *			not found and -DER_INVAL if
 *			invalid handle
 */
int
vos_cookie_update(daos_handle_t cih, uuid_t cookie, daos_epoch_t epoch);

/**
 * VOS cookie find and update
 * Find cookie if it exists update the
 * max_epoch, if not found add the entry
 * if less than max_epoch do nothing
 *
 * \param cih		[IN]	cookie index handle
 * \param cookie	[IN]	cookie
 * \param epoch		[IN]	epoch to update
 * \param update_flag	[IN]	flag to update/lookup
 * \param epoch_ret	[OUT]	max_epoch returned
 */
int
vos_cookie_find_update(daos_handle_t cih, uuid_t cookie, daos_epoch_t epoch,
		       bool update_flag, daos_epoch_t *epoch_ret);

/**
 * VOS object index class register for btree
 * Called with vos_init()
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_init();

/**
 * VOS object index create
 * Create a new B-tree if empty object index and adds the first
 * oid
 * Called from vos_container_create.
 *
 * \param pool		[IN]	vos pool
 * \param obj_index	[IN]	vos object index
 *				(pmem direct pointer)
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_create(struct vos_pool *pool, struct vos_object_index *obj_index);

/**
 * VOS object index destroy
 * Destroy the object index and all its objects
 * Called from vos_container_destroy
 *
 * \param pool		[IN]	vos pool
 * \param obj_index	[IN]	vos object index
 *				(pmem direct pointer)
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_destroy(struct vos_pool *pool, struct vos_object_index *obj_index);

enum vos_tree_class {
	/** the first reserved tree class */
	VOS_BTR_BEGIN		= DBTREE_VOS_BEGIN,
	/** distribution key tree */
	VOS_BTR_DKEY		= (VOS_BTR_BEGIN + 0),
	/** attribute key tree */
	VOS_BTR_AKEY		= (VOS_BTR_BEGIN + 1),
	/** index + epoch tree */
	VOS_BTR_IDX		= (VOS_BTR_BEGIN + 2),
	/** object index table */
	VOS_BTR_OIT		= (VOS_BTR_BEGIN + 3),
	/** container index table */
	VOS_BTR_CIT		= (VOS_BTR_BEGIN + 4),
	/** tree type for cookie index table */
	VOS_BTR_COOKIE		= (VOS_BTR_BEGIN + 5),
	/** the last reserved tree class */
	VOS_BTR_END,
};

int vos_obj_tree_init(struct vos_obj_ref *oref);
int vos_obj_tree_fini(struct vos_obj_ref *oref);
int vos_obj_tree_register(void);

/**
 * Data structure which carries the keys, epoch ranges to the multi-nested
 * btree.
 */
struct vos_key_bundle {
	enum vos_tree_class	 kb_tclass;
	/** daos d-key for the I/O operation */
	daos_key_t		*kb_dkey;
	/** daos a-key for the I/O operation */
	daos_key_t		*kb_akey;
	/** key for the current tree, could be @kb_dkey or @kb_akey */
	daos_key_t		*kb_key;
	/** epoch for the I/O operation */
	daos_epoch_range_t	*kb_epr;
	/** index of recx */
	uint64_t		 kb_idx;
};

/**
 * Data structure which carries the value buffers, checksums and memory IDs
 * to the multi-nested btree.
 */
struct vos_rec_bundle {
	/** checksum buffer for the daos key */
	daos_csum_buf_t		*rb_csum;
	/**
	 * Input  : value buffer (non-rdma data)
	 *	    TODO also support scatter/gather list input.
	 * Output : parameter to return value address.
	 */
	daos_iov_t		*rb_iov;
	/** Optional, externally allocated buffer mmid */
	umem_id_t		 rb_mmid;
	/** returned btree root */
	struct btr_root		*rb_btr;
	/** returned evtree root */
	struct evt_root		*rb_evt;
	/** returned record extent */
	daos_recx_t		*rb_recx;
	/**
	 * update : input record size
	 * fetch  : return size of records
	 */
	daos_size_t		 rb_rsize;
	/** update cookie of this recx (input for update, output for fetch) */
	uuid_t			 rb_cookie;
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
	return (struct vos_krec_df *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
}

static inline struct vos_irec_df *
vos_rec2irec(struct btr_instance *tins, struct btr_record *rec)
{
	return (struct vos_irec_df *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
}

static inline uint64_t
vos_krec_size(enum vos_tree_class tclass, struct vos_rec_bundle *rbund)
{
	daos_iov_t	*key;
	uint64_t	 size;
	bool		 has_evt = (tclass == VOS_BTR_AKEY);

	key = rbund->rb_iov;
	size = vos_size_round(rbund->rb_csum->cs_len) + key->iov_len;
	return size + offsetof(struct vos_krec_df, kr_evt[has_evt]);
}

static inline void *
vos_krec2payload(struct vos_krec_df *krec)
{
	return (void *)&krec->kr_evt[!!(krec->kr_bmap & KREC_BF_EVT)];
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

static inline uint64_t
vos_irec_size(struct vos_rec_bundle *rbund)
{
	uint64_t size = 0;

	if (rbund->rb_csum != NULL)
		size = vos_size_round(rbund->rb_csum->cs_len);
	return size + sizeof(struct vos_irec_df) +
	       rbund->rb_rsize * rbund->rb_recx->rx_nr;
}

static inline bool
vos_irec_size_equal(struct vos_irec_df *irec, struct vos_rec_bundle *rbund)
{
	if (irec->ir_size != rbund->rb_rsize * rbund->rb_recx->rx_nr)
		return false;

	if (irec->ir_cs_size != rbund->rb_csum->cs_len)
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
	return &irec->ir_body[vos_size_round(irec->ir_cs_size)];
}

static inline bool
vos_obj_is_new(struct vos_obj *obj)
{
	return obj->vo_tree.tr_class == 0;
}

static inline bool
vos_subtree_is_empty(daos_handle_t toh)
{
	return dbtree_is_empty(toh) == 1;
}

static inline bool vos_obj_is_zombie(struct vos_obj *obj)
{
	/* TODO */
	return false;
}

static inline bool vos_recx_is_equal(daos_recx_t *recx1, daos_recx_t *recx2)
{
	return !(memcmp(recx1, recx2, sizeof(daos_recx_t)));
}

static inline PMEMobjpool *
vos_co2pop(struct vc_hdl *co_hdl)
{
	return vos_pool_ptr2pop(co_hdl->vc_pool);
}

static inline PMEMobjpool *
vos_oref2pop(struct vos_obj_ref *oref)
{
	return vos_co2pop(oref->or_co);
}

static inline daos_handle_t
vos_oref2cookie_hdl(struct vos_obj_ref *oref)
{
	return oref->or_co->vc_pool->vp_cookie_ith;
}

static inline struct umem_attr *
vos_oref2uma(struct vos_obj_ref *oref)
{
	return &oref->or_co->vc_pool->vp_uma;
}

static inline struct umem_instance *
vos_oref2umm(struct vos_obj_ref *oref)
{
	return &oref->or_co->vc_pool->vp_umm;
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
vos_co2hdl(struct vc_hdl *co)
{
	daos_handle_t coh;

	coh.cookie = (uint64_t)co;
	return coh;
}

static inline struct vc_hdl*
vos_hdl2co(daos_handle_t coh)
{
	return (struct vc_hdl *)(coh.cookie);
}

static inline daos_handle_t
vos_coh2cih(daos_handle_t coh)
{
	struct vc_hdl *chdl = vos_hdl2co(coh);

	return chdl->vc_pool->vp_cookie_ith;
}

static inline void
vos_co_set_purged_epoch(daos_handle_t coh, daos_epoch_t update_epoch)
{
	struct vc_hdl		*co_hdl;
	struct vos_container	*vc_co;

	co_hdl	= vos_hdl2co(coh);
	vc_co	= co_hdl->vc_co;
	vc_co->vc_info.pci_purged_epoch = update_epoch;
}

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

struct vos_iter_ops;

/** the common part of vos iterators */
struct vos_iterator {
	vos_iter_type_t		 it_type;
	enum vos_iter_state	 it_state;
	struct vos_iter_ops	*it_ops;
};

/** function table for vos iterator */
struct vos_iter_ops {
	/** prepare a new iterator with the specified type and parameters */
	int	(*iop_prepare)(vos_iter_type_t type, vos_iter_param_t *param,
			       struct vos_iterator **iter_pp);
	/** finalise a iterator */
	int	(*iop_finish)(struct vos_iterator *iter);
	/** Set the iterating cursor to the provided @anchor */
	int	(*iop_probe)(struct vos_iterator *iter,
			     daos_hash_out_t *anchor);
	/** move forward the iterating cursor */
	int	(*iop_next)(struct vos_iterator *iter);
	/** fetch the record that the cursor points to */
	int	(*iop_fetch)(struct vos_iterator *iter,
			     vos_iter_entry_t *it_entry,
			     daos_hash_out_t *anchor);
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

static  inline struct vos_iterator *
vos_hdl2iter(daos_handle_t hdl)
{
	return (struct vos_iterator *)hdl.cookie;
}

/** Pack a key bundle into an iovec */
void
tree_key_bundle2iov(struct vos_key_bundle *kbund, daos_iov_t *iov);

struct vos_obj_iter*
vos_hdl2oiter(daos_handle_t hdl);

struct vos_oid_iter*
vos_hdl2oid_iter(daos_handle_t hdl);

#endif /* __VOS_INTERNAL_H__ */
