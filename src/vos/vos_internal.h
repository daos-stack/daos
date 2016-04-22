/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
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

#include "vos_layout.h"

/**
 * Reference of a cached object.
 *
 * NB: DRAM data structure.
 */
struct vos_obj_ref {
	/** TODO: link it to object cache lru and hash table */

	/** Key for searching, container uuid */
	uuid_t				 or_co_uuid;
	/** Key for searching, object ID within a container */
	daos_unit_oid_t			 or_oid;
	/** btree open handle of the object */
	daos_handle_t			 or_toh;
	/** btree iterator handle */
	daos_handle_t			 or_ih;
	/** Persistent memory ID for the object */
	struct vos_obj			*or_obj;
};

/**
 * percpu object cache. It can include a hash table and a LRU for
 * cached objects.
 *
 * This structure is not exported (move to TLS).
 */
struct vos_obj_cache;

/**
 * Find an object in the cache \a occ and take its reference. If the object is
 * not in cache, this function will load it from PMEM pool or create it, then
 * add it to the cache.
 *
 * \param occ	[IN]	Object cache, it could be a percpu data structure.
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	VOS object ID.
 * \param oref_p [OUT]	Returned object cache reference.
 */
int  vos_obj_ref_hold(struct vos_obj_cache *occ, daos_handle_t coh,
		      daos_unit_oid_t oid, struct vos_obj_ref **oref_p);

/**
 * Release the object cache reference.
 *
 * \param oref	[IN]	Reference to be released.
 */
void vos_obj_ref_release(struct vos_obj_cache *occ, struct vos_obj_ref *oref);

/**
 * Create an object cache.
 *
 * \param occ_p	[OUT]	Newly created cache.
 */
int  vos_obj_cache_create(struct vos_obj_cache **occ_p);

/**
 * Destroy an object cache, and release all cached object references.
 *
 * \param occ	[IN]	Cache to be destroyed.
 */
void vos_obj_cache_destroy(struct vos_obj_cache *occ);

/**
 * Return object cache for the current thread.
 */
struct vos_obj_cache *vos_obj_cache_current(void);

/**
 * VOS pool handle (DRAM)
 */
struct vp_hdl {
	/* handle hash link for the vos pool */
	struct daos_hlink	vp_hlink;
	/* Pointer to PMEMobjpool */
	PMEMobjpool		*vp_ph;
	/* Path to PMEM file */
	char			*vp_fpath;
};

/**
 * VOS container handle (DRAM)
 */
struct vc_hdl {
	/* VOS container handle hash link */
	struct daos_hlink	vc_hlink;
	/* VOS PMEMobjpool pointer */
	PMEMobjpool		*vc_ph;
	/* Unique UID of VOS container */
	uuid_t			vc_id;
	/* Direct pointer to VOS object index
	 * within container
	 */
	struct vos_object_index	*vc_obj_table;
	/* Direct pointer to VOS epoch index
	 * within container
	 */
	struct vos_epoch_index	*vc_epoch_table;
};

/**
 * Global Handle Hash
 * Across all VOS handles
 */
struct daos_hhash	*daos_vos_hhash;

/**
 * Create a handle hash
 * Created once across all threads all
 * handles
 */
int
vos_create_hhash(void);

/**
 * Lookup VOS pool handle
 *
 * \param poh	[IN]	VOS pool handle
 *
 * \return		vos_pool handle of type
 *			struct vp_hdl or NULL
 *
 */
struct vp_hdl*
vos_pool_lookup_handle(daos_handle_t poh);

/**
 * Decrement reference count
 *
 * \param vpool	[IN]	VOS pool handle
 */
void
vos_pool_putref_handle(struct vp_hdl *vpool);

/**
 * Lookup VOS container handle
 *
 * \param coh	[IN]	VOS container handle
 *
 * TODO: Not yet implemented
 */
struct vos_pool*
vos_co_lookup_handle(daos_handle_t poh);

PMEMobjpool *vos_coh2pop(daos_handle_t coh);

/**
 * Data structure which carries the keys, epoch ranges to the multi-nested
 * btree.
 */
struct vos_key_bundle {
	/** daos key for the I/O operation */
	daos_dkey_t		*kb_key;
	/** record index for the I/O operation */
	daos_recx_t		*kb_rex;
	/** epoch for the I/O operation */
	daos_epoch_range_t	*kb_epr;
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
	daos_iov_t	*dkey;
	uint64_t	 size;

	dkey = rbund->rb_iov; /* XXX dkey only */
	size = vos_size_round(rbund->rb_csum->cs_len);

	return size + dkey->iov_len + sizeof(struct vos_krec);
}

static inline char *
vos_krec2csum(struct vos_krec *krec)
{
	return krec->kr_cs_size == 0 ? NULL : &krec->kr_body[0];
}

static inline char *
vos_krec2dkey(struct vos_krec *krec)
{
	return &krec->kr_body[vos_size_round(krec->kr_cs_size)];
}

static inline uint64_t
vos_irec_size(struct vos_rec_bundle *rbund)
{
	uint64_t size;

	size = vos_size_round(rbund->rb_csum->cs_len);
	return size + rbund->rb_iov->iov_len + sizeof(struct vos_irec);
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
	/** key tree */
	VOS_BTR_KEY		= (VOS_BTR_BEGIN + 0),
	/** index + epoch tree */
	VOS_BTR_IDX		= (VOS_BTR_BEGIN + 1),
	/** the last reserved tree class */
	VOS_BTR_END,
};

int vos_obj_tree_init(struct vos_obj_ref *oref);
int vos_obj_tree_fini(struct vos_obj_ref *oref);
int vos_obj_tree_register(PMEMobjpool *pop);

struct umem_attr vos_uma;

#endif /* __VOS_INTERNAL_H__ */
