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
#include <daos_srv/daos_server.h>

#include "vos_layout.h"
#include "vos_obj.h"

struct vos_tls {
	daos_handle_t	vmi_poh;
};

extern struct dss_module_key vos_module_key;

static inline struct vos_tls *
vos_tls_get()
{
	struct vos_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct vos_tls *)dss_module_key_get(dtc, &vos_module_key);
	return tls;
}

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
	/* Btree attribute this instance of
	 * pool
	 */
	struct umem_attr	vp_uma;
};

/**
 * VOS container handle (DRAM)
 */
struct vc_hdl {
	/* VOS container handle hash link */
	struct daos_hlink	vc_hlink;
	/* VOS PMEMobjpool pointer */
	struct vp_hdl		*vc_phdl;
	/* Unique UID of VOS container */
	uuid_t			vc_id;
	/* DAOS handle for object index btree */
	daos_handle_t		vc_btr_oid;
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
 * \param co_hdl	[IN] VOS container handle
 */
void
vos_pool_putref_handle(struct vp_hdl *vp_hdl);

/**
 * Lookup VOS container handle
 *
 * \param coh	[IN]	VOS container handle
 *
 * TODO: Not yet implemented
 */
struct vc_hdl*
vos_co_lookup_handle(daos_handle_t coh);

/**
 * Decrement container handle reference
 * count
 *
 * \param co_hdl [IN]	VOS container handle
 */
void
vos_co_putref_handle(struct vc_hdl *co_hdl);

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

PMEMobjpool *vos_coh2pop(daos_handle_t coh);

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
