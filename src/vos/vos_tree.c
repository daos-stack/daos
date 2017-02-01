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
 * This file is part of daos
 *
 * vos/vos_tree.c
 */
#define DD_SUBSYS	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"

/** hash seed for murmur hash */
#define VOS_BTR_MUR_SEED	0xC0FFEE

/**
 * VOS Btree attributes, for tree registration and tree creation.
 */
struct vos_btr_attr {
	/** tree class ID */
	int		 ta_class;
	/** default tree order */
	int		 ta_order;
	/** feature bits */
	uint64_t	 ta_feats;
	/** name of tree type */
	char		*ta_name;
	/** customized tree functions */
	btr_ops_t	*ta_ops;
};

static struct vos_btr_attr *vos_obj_sub_tree_attr(unsigned tree_class);

static struct vos_key_bundle *
vos_iov2key_bundle(daos_iov_t *key_iov)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct vos_key_bundle));
	return (struct vos_key_bundle *)key_iov->iov_buf;
}

static struct vos_rec_bundle *
vos_iov2rec_bundle(daos_iov_t *val_iov)
{
	D_ASSERT(val_iov->iov_len == sizeof(struct vos_rec_bundle));
	return (struct vos_rec_bundle *)val_iov->iov_buf;
}

/**
 * @defgroup vos_key_btree vos key-btree
 * @{
 */

/**
 * hashed key for the key-btree, it is stored in btr_record::rec_hkey
 */
struct key_btr_hkey {
	/** murmur64 hash */
	uint64_t	hk_hash1;
	/** reserved: the second hash to avoid hash collison of murmur64 */
	uint64_t	hk_hash2;
};

/**
 * Copy a key and its checksum into vos_krec
 */
static int
kbtr_rec_fetch_in(struct btr_instance *tins, struct btr_record *rec,
		  struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec	*krec	= vos_rec2krec(tins, rec);
	daos_iov_t	*iov	= rbund->rb_iov;
	daos_csum_buf_t	*csum	= rbund->rb_csum;

	krec->kr_cs_size = csum->cs_len;
	if (krec->kr_cs_size != 0) {
		if (csum->cs_csum != NULL) {
			memcpy(vos_krec2csum(krec), csum->cs_csum,
			       csum->cs_len);
		} else {
			/* Return the address for rdma? But it is too hard to
			 * handle rdma failure.
			 */
			csum->cs_csum = vos_krec2csum(krec);
		}
		krec->kr_cs_type = csum->cs_type;
	}

	/* XXX only dkey for the time being */
	D_ASSERT(iov->iov_buf == kbund->kb_key->iov_buf);
	if (iov->iov_buf != NULL) {
		memcpy(vos_krec2key(krec), iov->iov_buf, iov->iov_len);
	} else {
		/* Return the address for rdma? But it is too hard to handle
		 * rdma failure.
		 */
		iov->iov_buf = vos_krec2key(krec);
	}
	krec->kr_size = iov->iov_len;
	return 0;
}

/**
 * Return memory address of key and checksum if BTR_FETCH_ADDR is set in
 * \a options, otherwise copy key and its checksum stored in \a rec into
 * external buffer.
 */
static int
kbtr_rec_fetch_out(struct btr_instance *tins, struct btr_record *rec,
		   struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec	*krec	= vos_rec2krec(tins, rec);
	daos_iov_t	*iov	= rbund->rb_iov;
	daos_csum_buf_t	*csum	= rbund->rb_csum;

	iov->iov_len = krec->kr_size;
	if (iov->iov_buf == NULL) {
		iov->iov_buf = vos_krec2key(krec);
		iov->iov_buf_len = krec->kr_size;

	} else if (iov->iov_buf_len >= iov->iov_len) {
		memcpy(iov->iov_buf, vos_krec2key(krec), iov->iov_len);
	}

	csum->cs_len  = krec->kr_cs_size;
	csum->cs_type = krec->kr_cs_type;
	if (csum->cs_csum == NULL)
		csum->cs_csum = vos_krec2csum(krec);
	else if (csum->cs_buf_len > csum->cs_len)
		memcpy(csum->cs_csum, vos_krec2csum(krec), csum->cs_len);

	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
kbtr_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct key_btr_hkey);
}

/** generate hkey */
static void
kbtr_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct key_btr_hkey	*khkey = (struct key_btr_hkey *)hkey;
	daos_key_t		*key;

	key = vos_iov2key_bundle(key_iov)->kb_key;

	khkey->hk_hash2 = 0; /* XXX add the second hash function */
	khkey->hk_hash1 = daos_hash_murmur64(key->iov_buf, key->iov_len,
						VOS_BTR_MUR_SEED);
}

/** compare the hashed key */
static int
kbtr_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct key_btr_hkey *khkey1 = (struct key_btr_hkey *)&rec->rec_hkey[0];
	struct key_btr_hkey *khkey2 = (struct key_btr_hkey *)hkey;

	D_ASSERT(khkey1->hk_hash2 == 0 && khkey2->hk_hash2 == 0);

	if (khkey1->hk_hash1 < khkey2->hk_hash1)
		return -1;

	if (khkey1->hk_hash1 > khkey2->hk_hash1)
		return 1;

	return 0;
}

/** compare the real key */
static int
kbtr_key_cmp(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov)
{
	daos_iov_t		*key;
	struct vos_krec		*krec;
	struct vos_key_bundle	*kbund;

	kbund = vos_iov2key_bundle(key_iov);
	key   = kbund->kb_key;

	krec = vos_rec2krec(tins, rec);
	if (krec->kr_size > key->iov_len)
		return 1;

	if (krec->kr_size < key->iov_len)
		return -1;

	return memcmp(vos_krec2key(krec), key->iov_buf, key->iov_len);
}

/** create a new key-record, or install an externally allocated key-record */
static int
kbtr_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec		*krec;
	struct vos_btr_attr	*ta;
	struct umem_attr	 uma;
	daos_handle_t		 toh;
	int			 rc;

	kbund = vos_iov2key_bundle(key_iov);
	rbund = vos_iov2rec_bundle(val_iov);

	if (UMMID_IS_NULL(rbund->rb_mmid)) {
		rec->rec_mmid = umem_alloc(&tins->ti_umm,
					   vos_krec_size(rbund));
		if (UMMID_IS_NULL(rec->rec_mmid))
			return -DER_NOMEM;

		kbtr_rec_fetch_in(tins, rec, kbund, rbund);
	} else {
		/* Huh, can't assume upper layer is using valid record format,
		 * need to do sanity check.
		 */
		rec->rec_mmid = rbund->rb_mmid;
	}

	krec = vos_rec2krec(tins, rec);
	memset(&krec->kr_btr, 0, sizeof(krec->kr_btr));

	/* find the sub-tree attributes */
	ta = vos_obj_sub_tree_attr(tins->ti_root->tr_class);
	D_ASSERT(ta != NULL);

	D_DEBUG(DF_VOS2, "Create subtree %s\n", ta->ta_name);

	umem_attr_get(&tins->ti_umm, &uma);
	rc = dbtree_create_inplace(ta->ta_class, ta->ta_feats,
				   ta->ta_order, &uma, &krec->kr_btr, &toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to create subtree: %d\n", rc);
		return rc;
	}

	rbund->rb_btr = &krec->kr_btr;
	dbtree_close(toh);
	return rc;
}

static int
kbtr_rec_free(struct btr_instance *tins, struct btr_record *rec,
	      void *args)
{
	struct vos_krec		*krec;
	int			 rc = 0;

	if (UMMID_IS_NULL(rec->rec_mmid))
		return 0;

	krec = vos_rec2krec(tins, rec);

	/* has subtree? */
	if (krec->kr_btr.tr_class != 0) {
		struct umem_attr uma;
		daos_handle_t	 toh;

		umem_attr_get(&tins->ti_umm, &uma);
		rc = dbtree_open_inplace(&krec->kr_btr, &uma, &toh);
		if (rc != 0)
			D_ERROR("Failed to open subtree: %d\b", rc);
		else
			dbtree_destroy(toh);
	}
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return rc;
}

static int
kbtr_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_krec		*krec = vos_rec2krec(tins, rec);
	struct vos_rec_bundle	*rbund;

	rbund = vos_iov2rec_bundle(val_iov);
	rbund->rb_btr = &krec->kr_btr;
	if (key_iov != NULL) {
		struct vos_key_bundle	*kbund;

		kbund = vos_iov2key_bundle(key_iov);
		kbtr_rec_fetch_out(tins, rec, kbund, rbund);
	}
	return 0;
}

static int
kbtr_rec_update(struct btr_instance *tins, struct btr_record *rec,
		daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_rec_bundle	*rbund;
	struct vos_krec		*krec;

	/* NB: do nothing at here except return the sub-tree root, because
	 * the real update happens in the sub-tree (index & epoch tree).
	 */
	krec = vos_rec2krec(tins, rec);
	rbund = vos_iov2rec_bundle(val_iov);
	rbund->rb_btr = &krec->kr_btr;
	return 0;
}

static btr_ops_t vos_key_btr_ops = {
	.to_hkey_size		= kbtr_hkey_size,
	.to_hkey_gen		= kbtr_hkey_gen,
	.to_hkey_cmp		= kbtr_hkey_cmp,
	.to_key_cmp		= kbtr_key_cmp,
	.to_rec_alloc		= kbtr_rec_alloc,
	.to_rec_free		= kbtr_rec_free,
	.to_rec_fetch		= kbtr_rec_fetch,
	.to_rec_update		= kbtr_rec_update,
};

/**
 * @} vos_key_btree
 */

/**
 * @defgroup vos_idx_btree vos index and epoch btree
 * @{
 */

struct idx_btr_key {
	/** reserved, record index */
	uint64_t	ih_index;
	/** */
	uint64_t	ih_epoch;
	/** cookie ID tag for this update */
	uuid_t		ih_cookie;
};

/**
 * Set size for the record and returns write buffer address of the record,
 * so caller can copy/rdma data into it.
 */
static int
irec_update(struct btr_instance *tins, struct btr_record *rec,
	    struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_irec		*irec	= vos_rec2irec(tins, rec);
	daos_csum_buf_t		*csum	= rbund->rb_csum;
	daos_iov_t		*iov	= rbund->rb_iov;
	struct idx_btr_key	*ihkey;

	if (iov->iov_len != rbund->rb_recx->rx_rsize)
		return -DER_IO_INVAL;

	ihkey = (struct idx_btr_key *)&rec->rec_hkey[0];
	/** Updating the cookie for this update */
	uuid_copy(ihkey->ih_cookie, rbund->rb_cookie);

	/** XXX: fix this after CSUM is added to iterator */
	irec->ir_cs_size = csum->cs_len;
	irec->ir_cs_type = csum->cs_type;
	irec->ir_size	 = iov->iov_len;

	if (irec->ir_size == 0) { /* it is a punch */
		csum->cs_csum = NULL;
		iov->iov_buf = NULL;
		return 0;
	}

	csum->cs_csum = vos_irec2csum(irec);
	iov->iov_buf = vos_irec2data(irec);
	return 0;
}

/**
 * Return memory address of data and checksum of this record.
 */
static int
irec_fetch(struct btr_instance *tins, struct btr_record *rec,
	   struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct idx_btr_key *ihkey = (struct idx_btr_key *)&rec->rec_hkey[0];
	struct vos_irec	   *irec  = vos_rec2irec(tins, rec);
	daos_csum_buf_t	   *csum  = rbund->rb_csum;
	daos_iov_t	   *iov   = rbund->rb_iov;
	daos_recx_t	   *recx  = rbund->rb_recx;

	if (kbund != NULL) { /* called from iterator */
		kbund->kb_epr->epr_lo	= ihkey->ih_epoch;
		kbund->kb_epr->epr_hi	= DAOS_EPOCH_MAX;
	}
	uuid_copy(rbund->rb_cookie, ihkey->ih_cookie);

	/* NB: return record address, caller should copy/rma data for it */
	iov->iov_len = iov->iov_buf_len = irec->ir_size;
	if (irec->ir_size != 0) {
		iov->iov_buf	= vos_irec2data(irec);
		csum->cs_len	= irec->ir_cs_size;
		csum->cs_type	= irec->ir_cs_type;
		csum->cs_csum	= vos_irec2csum(irec);
	}
	recx->rx_idx	= ihkey->ih_index;
	recx->rx_rsize	= irec->ir_size;
	recx->rx_nr	= 1;

	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
ibtr_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct idx_btr_key);
}

/** generate hkey */
static void
ibtr_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct idx_btr_key	*ihkey = (struct idx_btr_key *)hkey;
	struct vos_key_bundle	*kbund;

	kbund = vos_iov2key_bundle(key_iov);
	ihkey->ih_index = kbund->kb_idx;
	ihkey->ih_epoch = kbund->kb_epr->epr_lo;
}

/** compare the hashed key */
static int
ibtr_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct idx_btr_key *ihkey1 = (struct idx_btr_key *)&rec->rec_hkey[0];
	struct idx_btr_key *ihkey2 = (struct idx_btr_key *)hkey;

	if (ihkey1->ih_index < ihkey2->ih_index)
		return -1;

	if (ihkey1->ih_index > ihkey2->ih_index)
		return 1;

	if (ihkey1->ih_epoch < ihkey2->ih_epoch)
		return -1;

	if (ihkey1->ih_epoch > ihkey2->ih_epoch)
		return 1;

	return 0;
}

/** allocate a new record and fetch data */
static int
ibtr_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_rec_bundle	*rbund;
	struct vos_key_bundle	*kbund;
	int			 rc = 0;

	kbund = vos_iov2key_bundle(key_iov);
	rbund = vos_iov2rec_bundle(val_iov);

	if (UMMID_IS_NULL(rbund->rb_mmid)) {
		rec->rec_mmid = umem_alloc(&tins->ti_umm,
					   vos_irec_size(rbund));
		if (UMMID_IS_NULL(rec->rec_mmid))
			return -DER_NOMEM;
	} else {
		rec->rec_mmid = rbund->rb_mmid;
		rbund->rb_mmid = UMMID_NULL; /* taken over by btree */
	}

	rc = irec_update(tins, rec, kbund, rbund);
	return rc;
}

static int
ibtr_rec_free(struct btr_instance *tins, struct btr_record *rec,
	      void *args)
{

	if (UMMID_IS_NULL(rec->rec_mmid))
		return 0;

	if (args != NULL) {
		*(umem_id_t *)args = rec->rec_mmid;
		rec->rec_mmid = UMMID_NULL; /** taken over by user */

	} else {
		umem_free(&tins->ti_umm, rec->rec_mmid);
	}
	return 0;
}

static int
ibtr_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_key_bundle	*kbund = NULL;
	struct vos_rec_bundle	*rbund;

	rbund = vos_iov2rec_bundle(val_iov);
	if (key_iov != NULL)
		kbund = vos_iov2key_bundle(key_iov);

	irec_fetch(tins, rec, kbund, rbund);
	return 0;
}

static int
ibtr_rec_update(struct btr_instance *tins, struct btr_record *rec,
		daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct idx_btr_key	*ihkey;
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;

	kbund = vos_iov2key_bundle(key_iov);
	rbund = vos_iov2rec_bundle(val_iov);

	if (!UMMID_IS_NULL(rbund->rb_mmid) ||
	    !vos_irec_size_equal(vos_rec2irec(tins, rec), rbund)) {
		/* This function should return -DER_NO_PERM to dbtree if:
		 * - it is a rdma, the original record should be replaced.
		 * - the new record size cannot match the original one, so we
		 *   need to realloc and copyin data to the new space.
		 *
		 * So dbtree can release the original record and install the
		 * rdma-ed record, or just allocate a new one.
		 */
		return -DER_NO_PERM;
	}

	ihkey = (struct idx_btr_key *)&rec->rec_hkey[0];
	D_DEBUG(DF_VOS2, "Overwrite idx "DF_U64", epoch "DF_U64"\n",
		ihkey->ih_index, ihkey->ih_epoch);

	umem_tx_add(&tins->ti_umm, rec->rec_mmid, vos_irec_size(rbund));
	return irec_update(tins, rec, kbund, rbund);
}

static btr_ops_t vos_idx_btr_ops = {
	.to_hkey_size		= ibtr_hkey_size,
	.to_hkey_gen		= ibtr_hkey_gen,
	.to_hkey_cmp		= ibtr_hkey_cmp,
	.to_rec_alloc		= ibtr_rec_alloc,
	.to_rec_free		= ibtr_rec_free,
	.to_rec_fetch		= ibtr_rec_fetch,
	.to_rec_update		= ibtr_rec_update,
};

/**
 * @} vos_idx_btree
 */
static struct vos_btr_attr vos_btr_attrs[] = {
	{
		.ta_class	= VOS_BTR_DKEY,
		.ta_order	= 16,
		.ta_feats	= 0,
		.ta_name	= "vos_dkey",
		.ta_ops		= &vos_key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_AKEY,
		.ta_order	= 16,
		.ta_feats	= 0,
		.ta_name	= "vos_akey",
		.ta_ops		= &vos_key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_IDX,
		.ta_order	= 16,
		.ta_feats	= 0,
		.ta_name	= "vos_idx",
		.ta_ops		= &vos_idx_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_END,
		.ta_name	= "null",
	},
};

/**
 * Common vos tree functions.
 */

/** initialize tree for an object */
int
vos_obj_tree_init(struct vos_obj_ref *oref)
{
	struct vos_btr_attr *ta = &vos_btr_attrs[0];
	struct vos_obj	    *obj;
	int		     rc;

	if (!daos_handle_is_inval(oref->or_toh))
		return 0;

	obj = oref->or_obj;
	if (vos_obj_is_new(obj)) {
		D_DEBUG(DF_VOS2, "Create btree for object\n");
		rc = dbtree_create_inplace(ta->ta_class, ta->ta_feats,
					   ta->ta_order, vos_oref2uma(oref),
					   &obj->vo_tree, &oref->or_toh);
	} else {
		D_DEBUG(DF_VOS2, "Open btree for object\n");
		rc = dbtree_open_inplace(&obj->vo_tree, vos_oref2uma(oref),
					 &oref->or_toh);
	}
	return rc;
}

/** close btree for an object */
int
vos_obj_tree_fini(struct vos_obj_ref *oref)
{
	/* NB: tree is created inplace, so don't need to destroy */
	return daos_handle_is_inval(oref->or_toh) ?
	       0 : dbtree_close(oref->or_toh);
}

/** register all tree classes for VOS. */
int
vos_obj_tree_register(void)
{
	struct vos_btr_attr *ta;
	int		     rc = 0;

	for (ta = &vos_btr_attrs[0]; ta->ta_class != VOS_BTR_END; ta++) {
		rc = dbtree_class_register(ta->ta_class, ta->ta_feats,
					   ta->ta_ops);
		if (rc != 0) {
			D_ERROR("Failed to register %s: %d\n", ta->ta_name, rc);
			break;
		}
		D_DEBUG(DF_VOS2, "Register tree type %s\n", ta->ta_name);
	}
	return rc;
}

/** find the attributes of the subtree of @tree_class */
static struct vos_btr_attr *
vos_obj_sub_tree_attr(unsigned tree_class)
{
	int	i;

	switch (tree_class) {
	default:
	case VOS_BTR_IDX:
		return NULL;

	case VOS_BTR_AKEY:
		tree_class = VOS_BTR_IDX;
		break;

	case VOS_BTR_DKEY:
		/* TODO: change it to VOS_BTR_AKEY while adding akey support */
		tree_class = VOS_BTR_AKEY;
		break;
	}

	for (i = 0;; i++) {
		struct vos_btr_attr *ta = &vos_btr_attrs[i];

		D_DEBUG(DF_VOS2, "ta->ta_class: %d, tree_class: %d\n",
			ta->ta_class, tree_class);

		if (ta->ta_class == tree_class)
			return ta;

		if (ta->ta_class == VOS_BTR_END)
			return NULL;
	}
}
