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
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"

/** hash seed for murmur hash */
#define VOS_BTR_MUR_SEED	0xC0FFEE

#define VOS_BTR_ORDER		23
#define VOS_EVT_ORDER		23

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
struct kb_hkey {
	/** murmur64 hash */
	uint64_t	kb_hash1;
	/** reserved: the second hash to avoid hash collison of murmur64 */
	uint64_t	kb_hash2;
	/** the first update epoch */
	uint64_t	kb_epc_lo;
	/** the punched epoch of this key, it's DAOS_EPOCH_MAX if no punch */
	uint64_t	kb_epc_hi;
};

/**
 * Copy a key and its checksum into vos_krec_df
 */
static int
kb_rec_copy_in(struct btr_instance *tins, struct btr_record *rec,
	       struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;

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
kb_rec_copy_out(struct btr_instance *tins, struct btr_record *rec,
		struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;

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
kb_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct kb_hkey);
}

/** generate hkey */
static void
kb_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct kb_hkey		*kkey  = (struct kb_hkey *)hkey;
	struct vos_key_bundle	*kbund = vos_iov2key_bundle(key_iov);
	daos_key_t		*key   = kbund->kb_key;

	kkey->kb_hash1  = d_hash_murmur64(key->iov_buf, key->iov_len,
					  VOS_BTR_MUR_SEED);
	kkey->kb_hash2  = d_hash_string_u32(key->iov_buf, key->iov_len);
	kkey->kb_epc_lo = kbund->kb_epr->epr_lo;
	kkey->kb_epc_hi = kbund->kb_epr->epr_hi;
}

/** compare the hashed key */
static int
kb_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct kb_hkey *kkey1 = (struct kb_hkey *)&rec->rec_hkey[0];
	struct kb_hkey *kkey2 = (struct kb_hkey *)hkey;

	if (kkey1->kb_hash1 < kkey2->kb_hash1)
		return -1;

	if (kkey1->kb_hash1 > kkey2->kb_hash1)
		return 1;

	if (kkey1->kb_hash2 < kkey2->kb_hash2)
		return -1;

	if (kkey1->kb_hash2 > kkey2->kb_hash2)
		return 1;

	/* NB: epoch checks may be wrong for underwrite, but we are not
	 * supposed to support underwrite for now.
	 */
	if (kkey1->kb_epc_lo > kkey2->kb_epc_hi)
		return 1;

	if (kkey1->kb_epc_hi < kkey2->kb_epc_lo)
		return -1;

	return 0;
}

/** compare the real key */
static int
kb_key_cmp(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov)
{
	daos_iov_t		*kiov;
	struct vos_krec_df	*krec;
	struct vos_key_bundle	*kbund;
	struct kb_hkey		*hkey;
	daos_epoch_range_t	*epr;
	int			 cmp;

	kbund = vos_iov2key_bundle(key_iov);
	kiov  = kbund->kb_key;
	epr   = kbund->kb_epr;

	krec = vos_rec2krec(tins, rec);
	if (krec->kr_size > kiov->iov_len)
		return 1;

	if (krec->kr_size < kiov->iov_len)
		return -1;

	cmp = memcmp(vos_krec2key(krec), kiov->iov_buf, kiov->iov_len);
	if (cmp)
		return cmp;

	hkey = (struct kb_hkey *)&rec->rec_hkey[0];
	if (hkey->kb_epc_lo > epr->epr_hi)
		return 1;

	if (hkey->kb_epc_hi < epr->epr_lo)
		return -1;

	return 0;
}

/** create a new key-record, or install an externally allocated key-record */
static int
kb_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	     daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	struct vos_btr_attr	*ta;
	struct umem_attr	 uma;
	daos_handle_t		 btr_oh = DAOS_HDL_INVAL;
	daos_handle_t		 evt_oh = DAOS_HDL_INVAL;
	int			 rc;

	kbund = vos_iov2key_bundle(key_iov);
	rbund = vos_iov2rec_bundle(val_iov);

	rec->rec_mmid = umem_zalloc(&tins->ti_umm,
				    vos_krec_size(rbund->rb_tclass, rbund));
	if (UMMID_IS_NULL(rec->rec_mmid))
		return -DER_NOMEM;

	krec = vos_rec2krec(tins, rec);

	/* Step-1: find the btree attributes and create btree */
	ta = vos_obj_sub_tree_attr(tins->ti_root->tr_class);
	D_ASSERT(ta != NULL);

	D_DEBUG(DB_TRACE, "Create dbtree %s\n", ta->ta_name);

	umem_attr_get(&tins->ti_umm, &uma);
	rc = dbtree_create_inplace(ta->ta_class, ta->ta_feats, ta->ta_order,
				   &uma, &krec->kr_btr, &btr_oh);
	if (rc != 0) {
		D_ERROR("Failed to create btree: %d\n", rc);
		return rc;
	}
	rbund->rb_btr = &krec->kr_btr;

	if (rbund->rb_tclass == VOS_BTR_DKEY)
		D_GOTO(copy_in, rc = 0);

	/* Step-2: find evtree for akey only */
	D_DEBUG(DB_TRACE, "Create evtree\n");

	rc = evt_create_inplace(EVT_FEAT_DEFAULT, VOS_EVT_ORDER, &uma,
				&krec->kr_evt[0], &evt_oh);
	if (rc != 0) {
		D_ERROR("Failed to create evtree: %d\n", rc);
		D_GOTO(out, rc);
	}

	rbund->rb_evt = &krec->kr_evt[0];
	krec->kr_bmap |= KREC_BF_EVT;
 copy_in:
	kb_rec_copy_in(tins, rec, kbund, rbund);
 out:
	if (!daos_handle_is_inval(btr_oh))
		dbtree_close(btr_oh);

	if (!daos_handle_is_inval(evt_oh))
		evt_close(evt_oh);

	return rc;
}

static int
kb_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct vos_krec_df *krec;
	struct umem_attr    uma;
	daos_handle_t	    toh;
	int		    rc = 0;

	if (UMMID_IS_NULL(rec->rec_mmid))
		return 0;

	krec = vos_rec2krec(tins, rec);
	umem_attr_get(&tins->ti_umm, &uma);

	/* has subtree? */
	if (krec->kr_btr.tr_order) {
		rc = dbtree_open_inplace(&krec->kr_btr, &uma, &toh);
		if (rc != 0)
			D_ERROR("Failed to open btree: %d\n", rc);
		else
			dbtree_destroy(toh);
	}

	if ((krec->kr_bmap & KREC_BF_EVT) && krec->kr_evt[0].tr_order) {
		rc = evt_open_inplace(&krec->kr_evt[0], &uma, &toh);
		if (rc != 0)
			D_ERROR("Failed to open evtree: %d\n", rc);
		else
			evt_destroy(toh);
	}
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return rc;
}

static int
kb_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	struct vos_rec_bundle	*rbund;

	rbund = vos_iov2rec_bundle(val_iov);
	rbund->rb_btr = &krec->kr_btr;
	if (krec->kr_bmap & KREC_BF_EVT)
		rbund->rb_evt = &krec->kr_evt[0];

	if (key_iov != NULL) {
		struct vos_key_bundle	*kbund;
		struct kb_hkey		*hkey;

		kbund = vos_iov2key_bundle(key_iov);
		if (kbund->kb_epr) {
			hkey = (struct kb_hkey *)&rec->rec_hkey[0];
			kbund->kb_epr->epr_lo = hkey->kb_epc_lo;
			kbund->kb_epr->epr_hi = hkey->kb_epc_hi;
		}
		kb_rec_copy_out(tins, rec, kbund, rbund);
	}
	return 0;
}

static int
kb_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_rec_bundle	*rbund = vos_iov2rec_bundle(val_iov);
	struct vos_krec_df	*krec;

	krec = vos_rec2krec(tins, rec);
	if (rbund->rb_tclass) {
		/* NB: do nothing at here except return the sub-tree root,
		 * because the real update happens in the sub-tree (index &
		 * epoch tree).
		 */
		rbund->rb_btr = &krec->kr_btr;
		if (krec->kr_bmap & KREC_BF_EVT)
			rbund->rb_evt = &krec->kr_evt[0];

	} else { /* punch */
		daos_epoch_range_t *epr = vos_iov2key_bundle(key_iov)->kb_epr;
		struct kb_hkey	   *hkey;

		hkey = (struct kb_hkey *)&rec->rec_hkey[0];

		D_ASSERT(epr->epr_lo == epr->epr_hi);
		D_ASSERT(hkey->kb_epc_hi >= epr->epr_lo &&
			 hkey->kb_epc_lo <= epr->epr_lo);

		hkey->kb_epc_hi = epr->epr_lo - 1;
	}
	return 0;
}

static btr_ops_t key_btr_ops = {
	.to_hkey_size		= kb_hkey_size,
	.to_hkey_gen		= kb_hkey_gen,
	.to_hkey_cmp		= kb_hkey_cmp,
	.to_key_cmp		= kb_key_cmp,
	.to_rec_alloc		= kb_rec_alloc,
	.to_rec_free		= kb_rec_free,
	.to_rec_fetch		= kb_rec_fetch,
	.to_rec_update		= kb_rec_update,
};

/**
 * @} vos_key_btree
 */

/**
 * @defgroup vos_singv_btr vos single value btree
 * @{
 */

struct svb_hkey {
	/** */
	uint64_t	sv_epoch;
	/** cookie ID tag for this update */
	uuid_t		sv_cookie;
};

/**
 * Set size for the record and returns write buffer address of the record,
 * so caller can copy/rdma data into it.
 */
static int
svb_rec_copy_in(struct btr_instance *tins, struct btr_record *rec,
		struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_irec_df	*irec	= vos_rec2irec(tins, rec);
	daos_csum_buf_t		*csum	= rbund->rb_csum;
	daos_iov_t		*iov	= rbund->rb_iov;
	struct svb_hkey		*skey;

	if (iov->iov_len != rbund->rb_rsize)
		return -DER_IO_INVAL;

	skey = (struct svb_hkey *)&rec->rec_hkey[0];
	/** Updating the cookie for this update */
	uuid_copy(skey->sv_cookie, rbund->rb_cookie);

	/** XXX: fix this after CSUM is added to iterator */
	irec->ir_cs_size = csum->cs_len;
	irec->ir_cs_type = csum->cs_type;
	irec->ir_size	 = iov->iov_len;
	irec->ir_ver	 = rbund->rb_ver;

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
svb_rec_copy_out(struct btr_instance *tins, struct btr_record *rec,
		 struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct svb_hkey	   *skey = (struct svb_hkey *)&rec->rec_hkey[0];
	struct vos_irec_df *irec  = vos_rec2irec(tins, rec);
	daos_csum_buf_t	   *csum  = rbund->rb_csum;
	daos_iov_t	   *iov   = rbund->rb_iov;

	if (kbund != NULL) { /* called from iterator */
		kbund->kb_epr->epr_lo	= skey->sv_epoch;
		kbund->kb_epr->epr_hi	= DAOS_EPOCH_MAX;
	}
	uuid_copy(rbund->rb_cookie, skey->sv_cookie);

	/* NB: return record address, caller should copy/rma data for it */
	iov->iov_len = iov->iov_buf_len = irec->ir_size;
	if (irec->ir_size != 0) {
		iov->iov_buf	= vos_irec2data(irec);
		csum->cs_len	= irec->ir_cs_size;
		csum->cs_type	= irec->ir_cs_type;
		csum->cs_csum	= vos_irec2csum(irec);
	}
	rbund->rb_rsize	= irec->ir_size;
	rbund->rb_ver	= irec->ir_ver;
	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
svb_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct svb_hkey);
}

/** generate hkey */
static void
svb_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct svb_hkey		*skey = (struct svb_hkey *)hkey;
	struct vos_key_bundle	*kbund;

	kbund = vos_iov2key_bundle(key_iov);
	skey->sv_epoch = kbund->kb_epr->epr_lo;
}

/** compare the hashed key */
static int
svb_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct svb_hkey *skey1 = (struct svb_hkey *)&rec->rec_hkey[0];
	struct svb_hkey *skey2 = (struct svb_hkey *)hkey;

	if (skey1->sv_epoch < skey2->sv_epoch)
		return -1;

	if (skey1->sv_epoch > skey2->sv_epoch)
		return 1;

	return 0;
}

/** allocate a new record and fetch data */
static int
svb_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
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

	rc = svb_rec_copy_in(tins, rec, kbund, rbund);
	return rc;
}

static int
svb_rec_free(struct btr_instance *tins, struct btr_record *rec,
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
svb_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_key_bundle	*kbund = NULL;
	struct vos_rec_bundle	*rbund;

	rbund = vos_iov2rec_bundle(val_iov);
	if (key_iov != NULL)
		kbund = vos_iov2key_bundle(key_iov);

	svb_rec_copy_out(tins, rec, kbund, rbund);
	return 0;
}

static int
svb_rec_update(struct btr_instance *tins, struct btr_record *rec,
		daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct svb_hkey		*skey;
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

	skey = (struct svb_hkey *)&rec->rec_hkey[0];
	D_DEBUG(DB_IO, "Overwrite epoch "DF_U64"\n", skey->sv_epoch);

	umem_tx_add(&tins->ti_umm, rec->rec_mmid, vos_irec_size(rbund));
	return svb_rec_copy_in(tins, rec, kbund, rbund);
}

static btr_ops_t singv_btr_ops = {
	.to_hkey_size		= svb_hkey_size,
	.to_hkey_gen		= svb_hkey_gen,
	.to_hkey_cmp		= svb_hkey_cmp,
	.to_rec_alloc		= svb_rec_alloc,
	.to_rec_free		= svb_rec_free,
	.to_rec_fetch		= svb_rec_fetch,
	.to_rec_update		= svb_rec_update,
};

/**
 * @} vos_singv_btr
 */
static struct vos_btr_attr vos_btr_attrs[] = {
	{
		.ta_class	= VOS_BTR_DKEY,
		.ta_order	= VOS_BTR_ORDER,
		.ta_feats	= 0,
		.ta_name	= "vos_dkey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_AKEY,
		.ta_order	= VOS_BTR_ORDER,
		.ta_feats	= 0,
		.ta_name	= "vos_akey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_SINGV,
		.ta_order	= VOS_BTR_ORDER,
		.ta_feats	= 0,
		.ta_name	= "singv",
		.ta_ops		= &singv_btr_ops,
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
vos_obj_tree_init(struct vos_object *obj)
{
	struct vos_btr_attr *ta = &vos_btr_attrs[0];
	int		     rc;

	if (!daos_handle_is_inval(obj->obj_toh))
		return 0;

	D_ASSERT(obj->obj_df);
	if (obj->obj_df->vo_tree.tr_class == 0) {
		D_DEBUG(DB_DF, "Create btree for object\n");
		rc = dbtree_create_inplace(ta->ta_class, ta->ta_feats,
					   ta->ta_order, vos_obj2uma(obj),
					   &obj->obj_df->vo_tree,
					   &obj->obj_toh);
	} else {
		D_DEBUG(DB_DF, "Open btree for object\n");
		rc = dbtree_open_inplace(&obj->obj_df->vo_tree,
					 vos_obj2uma(obj), &obj->obj_toh);
	}
	return rc;
}

/** close btree for an object */
int
vos_obj_tree_fini(struct vos_object *obj)
{
	int	rc = 0;

	/* NB: tree is created inplace, so don't need to destroy */
	if (!daos_handle_is_inval(obj->obj_toh)) {
		D_ASSERT(obj->obj_df);
		rc = dbtree_close(obj->obj_toh);
		obj->obj_toh = DAOS_HDL_INVAL;
	}
	return rc;
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
		D_DEBUG(DB_TRACE, "Register tree type %s\n", ta->ta_name);
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
	case VOS_BTR_SINGV:
		return NULL;

	case VOS_BTR_AKEY:
		tree_class = VOS_BTR_SINGV;
		break;

	case VOS_BTR_DKEY:
		/* TODO: change it to VOS_BTR_AKEY while adding akey support */
		tree_class = VOS_BTR_AKEY;
		break;
	}

	for (i = 0;; i++) {
		struct vos_btr_attr *ta = &vos_btr_attrs[i];

		D_DEBUG(DB_TRACE, "ta->ta_class: %d, tree_class: %d\n",
			ta->ta_class, tree_class);

		if (ta->ta_class == tree_class)
			return ta;

		if (ta->ta_class == VOS_BTR_END)
			return NULL;
	}
}
