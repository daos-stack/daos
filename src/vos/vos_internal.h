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

/**
 * VOS pool handle (DRAM)
 */
struct vp_hdl {
	/** VOS uuid hash-link with refcnt */
	struct daos_ulink	vp_uhlink;
	/** UUID of vos pool */
	uuid_t			vp_id;
	/** Pointer to PMEMobjpool **/
	PMEMobjpool		*vp_ph;
	/** Path to PMEM file **/
	char			*vp_fpath;
	/** Btree attribute for pool instance **/
	struct umem_attr	vp_uma;
	/** pmem allocation outside btree **/
	struct umem_instance	vp_umm;
	/** btr handle for container tree */
	daos_handle_t		vp_ct_hdl;
};

/**
 * VOS container handle (DRAM)
 */
struct vc_hdl {
	/* VOS uuid hash with refcnt */
	struct daos_ulink	vc_uhlink;
	/* VOS PMEMobjpool pointer */
	struct vp_hdl		*vc_phdl;
	/* Unique UID of VOS container */
	uuid_t			vc_id;
	/* DAOS handle for object index btree */
	daos_handle_t		vc_btr_hdl;
	/* Direct pointer to VOS object index
	 * within container
	 */
	struct vos_object_index	*vc_obj_table;
	/* Direct pointer to VOS epoch index
	 * within container
	 */
	struct vos_epoch_index	*vc_epoch_table;
	/** Diret pointer to the VOS container */
	struct vos_container	*vc_co;
};

struct vos_imem_strts {
	/**
	 * In-memory object cache for the PMEM
	 * object table
	 */
	struct daos_lru_cache	*vis_ocache;
	/**
	 * Hash table to refcount VOS handles
	 * (container/pool, etc.,)
	 */
	struct dhash_table	*vis_hr_hash;
};

/* in-memory structures standalone instance */
struct vos_imem_strts	*vsa_imems_inst;

/**
 * VOS thread local storage structure
 */
struct vos_tls {
	/* in-memory structures TLS instance */
	struct vos_imem_strts	vtl_imems_inst;
};

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


static inline struct vos_tls *
vos_tls_get()
{
	struct vos_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct vos_tls *)dss_module_key_get(dtc, &vos_module_key);
	return tls;
}

static inline struct vos_pool_root *
vos_pool2root(struct vp_hdl *vp)
{
	TOID(struct vos_pool_root)  proot;

	proot = POBJ_ROOT(vp->vp_ph, struct vos_pool_root);
	return D_RW(proot);
}

static inline struct vos_container_index*
vos_pool2coi_table(struct vp_hdl *vp)
{
	struct vos_container_index *coi;

	coi =  D_RW(vos_pool2root(vp)->vpr_ci_table);

	D_ASSERT(coi != NULL);
	return coi;
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
 * \param po_hdl	[IN]	Pool Handle
 * \param obj_index	[IN]	vos object index
 *				(pmem direct pointer)
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_create(struct vp_hdl *po_hdl,
	      struct vos_object_index *obj_index);

/**
 * VOS object index destroy
 * Destroy the object index and all its objects
 * Called from vos_container_destroy
 *
 * \param po_hdl	[IN]	Pool Handle
 * \param obj_index	[IN]	vos object index
 *				(pmem direct pointer)
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_destroy(struct vp_hdl *po_hdl,
	       struct vos_object_index *obj_index);

/**
 * Data structure which carries the keys, epoch ranges to the multi-nested
 * btree.
 */
struct vos_key_bundle {
	/** daos key for the I/O operation */
	daos_key_t		*kb_dkey;
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
	/** Optional, externally allocated d-key record mmid (rdma vos_krec) */
	umem_id_t		 rb_mmid;
	/** returned subtree root */
	struct btr_root		*rb_btr;
	/** returned size and nr of recx */
	daos_recx_t		*rb_recx;
};

#define VOS_SIZE_ROUND		8

/* size round up */
static inline uint64_t
vos_size_round(uint64_t size)
{
	return (size + VOS_SIZE_ROUND - 1) & ~(VOS_SIZE_ROUND - 1);
}

static inline struct vos_krec *
vos_rec2krec(struct btr_instance *tins, struct btr_record *rec)
{
	return (struct vos_krec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
}

static inline struct vos_irec *
vos_rec2irec(struct btr_instance *tins, struct btr_record *rec)
{
	return (struct vos_irec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
}

static inline uint64_t
vos_krec_size(struct vos_rec_bundle *rbund)
{
	daos_iov_t	*key;
	uint64_t	 size;

	key = rbund->rb_iov;
	size = vos_size_round(rbund->rb_csum->cs_len);

	return size + key->iov_len + sizeof(struct vos_krec);
}

static inline char *
vos_krec2csum(struct vos_krec *krec)
{
	return krec->kr_cs_size == 0 ? NULL : &krec->kr_body[0];
}

static inline char *
vos_krec2key(struct vos_krec *krec)
{
	return &krec->kr_body[vos_size_round(krec->kr_cs_size)];
}

static inline uint64_t
vos_irec_size(struct vos_rec_bundle *rbund)
{
	uint64_t size = 0;

	if (rbund->rb_csum != NULL)
		size = vos_size_round(rbund->rb_csum->cs_len);
	return size + sizeof(struct vos_irec) +
	       rbund->rb_recx->rx_rsize * rbund->rb_recx->rx_nr;
}

static inline bool
vos_irec_size_equal(struct vos_irec *irec, struct vos_rec_bundle *rbund)
{
	if (irec->ir_size != rbund->rb_recx->rx_rsize * rbund->rb_recx->rx_nr)
		return false;

	if (irec->ir_cs_size != rbund->rb_csum->cs_len)
		return false;

	return true;
}

static inline char *
vos_irec2csum(struct vos_irec *irec)
{
	return irec->ir_cs_size == 0 ? NULL : &irec->ir_body[0];
}

static inline char *
vos_irec2data(struct vos_irec *irec)
{
	return &irec->ir_body[vos_size_round(irec->ir_cs_size)];
}

static inline bool
vos_obj_is_new(struct vos_obj *obj)
{
	return obj->vo_tree.tr_class == 0;
}

static inline bool vos_obj_is_zombie(struct vos_obj *obj)
{
	/* TODO */
	return false;
}

enum {
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
	/** the last reserved tree class */
	VOS_BTR_END,
};

int vos_obj_tree_init(struct vos_obj_ref *oref);
int vos_obj_tree_fini(struct vos_obj_ref *oref);
int vos_obj_tree_register(void);

static inline PMEMobjpool *
vos_oref2pop(struct vos_obj_ref *oref)
{
	return oref->or_co->vc_phdl->vp_ph;
}

static inline struct umem_attr *
vos_oref2uma(struct vos_obj_ref *oref)
{
	return &oref->or_co->vc_phdl->vp_uma;
}

static inline struct umem_instance *
vos_oref2umm(struct vos_obj_ref *oref)
{
	return &oref->or_co->vc_phdl->vp_umm;
}

static inline daos_handle_t
vos_pool2hdl(struct vp_hdl *pool)
{
	daos_handle_t poh;

	poh.cookie = (uint64_t)pool;
	return poh;
}

static inline struct vp_hdl*
vos_hdl2pool(daos_handle_t poh)
{
	return (struct vp_hdl *)(poh.cookie);
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
};

void vos_pmemobj_close(PMEMobjpool *pop);

#endif /* __VOS_INTERNAL_H__ */
