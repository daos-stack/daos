/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include <daos/mem.h>
#include <daos_srv/vos.h>
#include <daos_api.h> /* For ofeat bits */
#include "vos_internal.h"

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

static struct vos_btr_attr *obj_tree_find_attr(unsigned tree_class);

static struct vos_key_bundle *
iov2key_bundle(daos_iov_t *key_iov)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct vos_key_bundle));
	return (struct vos_key_bundle *)key_iov->iov_buf;
}

static struct vos_rec_bundle *
iov2rec_bundle(daos_iov_t *val_iov)
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
struct ktr_hkey {
	/** murmur64 hash */
	uint64_t		kh_hash[2];
	/** epoch when this key is punched. */
	uint64_t		kh_epoch;
	/** cacheline alignment */
	uint64_t		kh_pad_64;
};

/**
 * Store a key and its checksum as a durable struct.
 */
static int
ktr_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;
	char		   *kbuf;

	krec->kr_cs_size = csum->cs_len;
	if (krec->kr_cs_size != 0) {
		D_ASSERT(csum->cs_csum);
		krec->kr_cs_type = csum->cs_type;
		memcpy(vos_krec2csum(krec), csum->cs_csum, csum->cs_len);
	}
	kbuf = vos_krec2key(krec);

	if (iov->iov_buf != NULL) {
		D_ASSERT(iov->iov_buf == kbund->kb_key->iov_buf);
		memcpy(kbuf, iov->iov_buf, iov->iov_len);
	} else {
		/* return it for RDMA */
		iov->iov_buf = kbuf;
	}
	krec->kr_size = iov->iov_len;
	return 0;
}

/**
 * Copy key and its checksum stored in \a rec into external buffer if it's
 * provided, otherwise return memory address of key and checksum.
 */
static int
ktr_rec_load(struct btr_instance *tins, struct btr_record *rec,
	     struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df *krec = vos_rec2krec(tins, rec);
	daos_iov_t	   *iov	 = rbund->rb_iov;
	daos_csum_buf_t	   *csum = rbund->rb_csum;
	char		   *kbuf;

	kbuf = vos_krec2key(krec);
	iov->iov_len = krec->kr_size;

	if (kbund != NULL) {
		if (kbund->kb_key != NULL) {
			daos_iov_set(kbund->kb_key, kbuf, krec->kr_size);
		} else {
			kbund->kb_key = iov;
		}
	}

	if (iov->iov_buf == NULL) {
		iov->iov_buf = kbuf;
		iov->iov_buf_len = krec->kr_size;

	} else if (iov->iov_buf_len >= iov->iov_len) {
		memcpy(iov->iov_buf, kbuf, iov->iov_len);
	}

	csum->cs_len  = krec->kr_cs_size;
	csum->cs_type = krec->kr_cs_type;
	if (csum->cs_csum == NULL)
		csum->cs_csum = (uint8_t *) vos_krec2csum(krec);
	else if (csum->cs_buf_len > csum->cs_len)
		memcpy(csum->cs_csum, vos_krec2csum(krec), csum->cs_len);

	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
ktr_hkey_size(void)
{
	return sizeof(struct ktr_hkey);
}

static int
ktr_rec_msize(int alloc_overhead)
{
	/* So this actually isn't currently the same for DKEY and AKEY but it
	 * will be shortly so didn't want to complicate the interface by
	 * passing the class.  Will need an update to support checksums but
	 * for now, this is a step.
	 */
	return alloc_overhead + sizeof(struct vos_krec_df);
}

/** generate hkey */
static void
ktr_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct ktr_hkey		*kkey  = (struct ktr_hkey *)hkey;
	struct vos_key_bundle	*kbund = iov2key_bundle(key_iov);
	daos_key_t		*key   = kbund->kb_key;

	kkey->kh_hash[0] = d_hash_murmur64(key->iov_buf, key->iov_len,
					   VOS_BTR_MUR_SEED);
	kkey->kh_hash[1] = d_hash_string_u32(key->iov_buf, key->iov_len);
	kkey->kh_epoch	 = kbund->kb_epoch;
}

/** compare the hashed key */
static int
ktr_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct ktr_hkey *k1 = (struct ktr_hkey *)&rec->rec_hkey[0];
	struct ktr_hkey *k2 = (struct ktr_hkey *)hkey;

	if (k1->kh_hash[0] < k2->kh_hash[0])
		return BTR_CMP_LT;

	if (k1->kh_hash[0] > k2->kh_hash[0])
		return BTR_CMP_GT;

	if (k1->kh_hash[1] < k2->kh_hash[1])
		return BTR_CMP_LT;

	if (k1->kh_hash[1] > k2->kh_hash[1])
		return BTR_CMP_GT;

	if (k1->kh_epoch > k2->kh_epoch)
		return BTR_CMP_GT | BTR_CMP_MATCHED;

	if (k1->kh_epoch < k2->kh_epoch)
		return BTR_CMP_LT | BTR_CMP_MATCHED;

	return BTR_CMP_EQ;
}

static int
ktr_key_cmp_lexical(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	int cmp;

	/* First, compare the bytes */
	cmp = memcmp(vos_krec2key(krec), (char *)kiov->iov_buf,
		     min(krec->kr_size, kiov->iov_len));
	if (cmp)
		return dbtree_key_cmp_rc(cmp);

	/* Second, fallback to the length */
	if (krec->kr_size > kiov->iov_len)
		return BTR_CMP_GT;
	else if (krec->kr_size < kiov->iov_len)
		return BTR_CMP_LT;

	return BTR_CMP_EQ;
}

static int
ktr_key_cmp_uint64(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	uint64_t k1, k2;

	if (krec->kr_size != kiov->iov_len ||
	    krec->kr_size != sizeof(uint64_t)) {
		D_ERROR("invalid kr_size %d.\n", krec->kr_size);
		return BTR_CMP_ERR;
	}

	k1 = *(uint64_t *)vos_krec2key(krec);
	k2 = *(uint64_t *)kiov->iov_buf;

	return (k1 > k2) ? BTR_CMP_GT :
			   ((k1 < k2) ? BTR_CMP_LT : BTR_CMP_EQ);
}

static int
ktr_key_cmp_default(struct vos_krec_df *krec, daos_iov_t *kiov)
{
	/* This only gets called if hash comparison matches. */
	if (krec->kr_size > kiov->iov_len)
		return BTR_CMP_GT;

	if (krec->kr_size < kiov->iov_len)
		return BTR_CMP_LT;

	return dbtree_key_cmp_rc(
		memcmp(vos_krec2key(krec), kiov->iov_buf, kiov->iov_len));
}

/** compare the real key */
static int
ktr_key_cmp(struct btr_instance *tins, struct btr_record *rec,
	    daos_iov_t *key_iov)
{
	daos_iov_t		*kiov;
	struct vos_krec_df	*krec;
	struct vos_key_bundle	*kbund;
	uint64_t		 feats = tins->ti_root->tr_feats;
	daos_epoch_t		 punched = DAOS_EPOCH_MAX;
	int			 cmp = 0;

	krec  = vos_rec2krec(tins, rec);
	kbund = iov2key_bundle(key_iov);
	kiov  = kbund->kb_key;

	if (feats & VOS_KEY_CMP_UINT64)
		cmp = ktr_key_cmp_uint64(krec, kiov);
	else if (feats & VOS_KEY_CMP_LEXICAL)
		cmp = ktr_key_cmp_lexical(krec, kiov);
	else
		cmp = ktr_key_cmp_default(krec, kiov);

	if (cmp != BTR_CMP_EQ)
		return cmp;

	if (krec->kr_bmap & KREC_BF_PUNCHED)
		punched = krec->kr_latest;
	if (punched > kbund->kb_epoch)
		return BTR_CMP_GT | BTR_CMP_MATCHED;

	if (punched < kbund->kb_epoch)
		return BTR_CMP_LT | BTR_CMP_MATCHED;

	return BTR_CMP_EQ;
}

static void
ktr_key_encode(struct btr_instance *tins, daos_iov_t *key,
	       daos_anchor_t *anchor)
{
	struct vos_key_bundle *kbund = iov2key_bundle(key);

	if (kbund->kb_key) {
		struct vos_embedded_key *embedded =
			(struct vos_embedded_key *)anchor->da_buf;
		D_ASSERT(kbund->kb_key->iov_len <= sizeof(embedded->ek_key));

		memcpy(embedded->ek_key, kbund->kb_key->iov_buf,
		       kbund->kb_key->iov_len);
		embedded->ek_kbund.kb_epoch = kbund->kb_epoch;
		/** Pointers will have to be set on decode. */
		embedded->ek_kiov.iov_len = kbund->kb_key->iov_len;
		embedded->ek_kiov.iov_buf_len = sizeof(embedded->ek_key);
	}
}

static void
ktr_key_decode(struct btr_instance *tins, daos_iov_t *key,
	       daos_anchor_t *anchor)
{
	struct vos_embedded_key *embedded =
		(struct vos_embedded_key *) anchor->da_buf;

	embedded->ek_kiov.iov_buf = &embedded->ek_key[0];
	embedded->ek_kbund.kb_key = &embedded->ek_kiov;

	key->iov_buf = &embedded->ek_kbund;
	key->iov_len = key->iov_buf_len = sizeof(embedded->ek_kbund);
}

/** create a new key-record, or install an externally allocated key-record */
static int
ktr_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	int			 rc = 0;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

	rec->rec_off = umem_zalloc_off(&tins->ti_umm, vos_krec_size(rbund));
	if (UMOFF_IS_NULL(rec->rec_off))
		return -DER_NOMEM;

	krec = vos_rec2krec(tins, rec);
	if (kbund->kb_epoch == DAOS_EPOCH_MAX) {
		/* These will be updated on first update */
		krec->kr_earliest = DAOS_EPOCH_MAX;
		krec->kr_latest = 0;
	} else {
		/* NB: if it's the first time punch, kr_earliest will be updated
		 * to timestamp of punched tree.  If it's a replay, we know it
		 * will never be greater than the punched epoch so it's a
		 * reasonable starting boundary.
		 */
		krec->kr_earliest = krec->kr_latest = kbund->kb_epoch;
		krec->kr_bmap |= KREC_BF_PUNCHED;
	}
	rbund->rb_krec = krec;

	/** Subtree will be created later */

	rc = vos_dtx_register_record(&tins->ti_umm, rec->rec_off,
				     DTX_RT_KEY, 0);
	if (rc == 0)
		ktr_rec_store(tins, rec, kbund, rbund);

	return rc;
}

static int
ktr_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct vos_krec_df *krec;
	struct umem_attr    uma;
	daos_handle_t	    toh;
	int		    rc = 0;

	if (UMOFF_IS_NULL(rec->rec_off))
		return 0;

	krec = vos_rec2krec(tins, rec);
	umem_attr_get(&tins->ti_umm, &uma);

	vos_dtx_deregister_record(&tins->ti_umm, krec->kr_dtx, rec->rec_off,
				  DTX_RT_KEY);
	if (krec->kr_dtx_shares > 0) {
		D_ERROR("There are some unknown DTXs (%d) share the key rec\n",
			krec->kr_dtx_shares);
		return -DER_BUSY;
	}

	/* has subtree? */
	if (krec->kr_bmap & KREC_BF_EVT) {
		if (krec->kr_evt.tr_order == 0)
			goto exit; /* No subtree */

		rc = evt_open(&krec->kr_evt, &uma, tins->ti_coh,
			      tins->ti_blks_info, &toh);
		if (rc != 0)
			D_ERROR("Failed to open evtree: %d\n", rc);
		else
			evt_destroy(toh);
	} else if (krec->kr_bmap & KREC_BF_BTR) {
		D_ASSERT(krec->kr_bmap & KREC_BF_BTR);
		if (krec->kr_btr.tr_order == 0)
			goto exit; /* No subtree */
		rc = dbtree_open_inplace_ex(&krec->kr_btr, &uma, tins->ti_coh,
					    tins->ti_blks_info, &toh);
		if (rc != 0)
			D_ERROR("Failed to open btree: %d\n", rc);
		else
			dbtree_destroy(toh);
	} /* It's possible that neither tree is created in case of punch only */
exit:
	umem_free_off(&tins->ti_umm, rec->rec_off);
	return rc;
}

static int
ktr_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);
	struct vos_key_bundle	*kbund = NULL;

	rbund->rb_krec = krec;

	if (key_iov != NULL) {
		kbund = iov2key_bundle(key_iov);
		/* NB: For now, this preserves previous fetch semantics but
		 * need to investigate further if it's needed
		 */
		if (krec->kr_bmap & KREC_BF_PUNCHED)
			kbund->kb_epoch = krec->kr_latest;
		else
			kbund->kb_epoch = DAOS_EPOCH_MAX;

		ktr_rec_load(tins, rec, kbund, rbund);
	}
	return 0;
}

static int
ktr_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);

	rbund->rb_krec = vos_rec2krec(tins, rec);
	/* NB: do nothing at here except return the sub-tree root,
	 * because the real update happens in the sub-tree (index &
	 * epoch tree).
	 */
	return 0;
}

static int
ktr_check_availability(struct btr_instance *tins, struct btr_record *rec,
		       uint32_t intent)
{
	struct vos_krec_df	*key;

	key = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	return vos_dtx_check_availability(&tins->ti_umm, tins->ti_coh,
				  key->kr_dtx, rec->rec_off, intent,
				  DTX_RT_KEY);
}

static btr_ops_t key_btr_ops = {
	.to_rec_msize		= ktr_rec_msize,
	.to_hkey_size		= ktr_hkey_size,
	.to_hkey_gen		= ktr_hkey_gen,
	.to_hkey_cmp		= ktr_hkey_cmp,
	.to_key_cmp		= ktr_key_cmp,
	.to_key_encode		= ktr_key_encode,
	.to_key_decode		= ktr_key_decode,
	.to_rec_alloc		= ktr_rec_alloc,
	.to_rec_free		= ktr_rec_free,
	.to_rec_fetch		= ktr_rec_fetch,
	.to_rec_update		= ktr_rec_update,
	.to_check_availability	= ktr_check_availability,
};

/**
 * @} vos_key_btree
 */

/**
 * @defgroup vos_singv_btr vos single value btree
 * @{
 */

struct svt_hkey {
	/** */
	uint64_t	sv_epoch;
};

/**
 * Set size for the record and returns write buffer address of the record,
 * so caller can copy/rdma data into it.
 */
static int
svt_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct vos_irec_df	*irec	= vos_rec2irec(tins, rec);
	daos_csum_buf_t		*csum	= rbund->rb_csum;
	struct bio_iov		*biov	= rbund->rb_biov;

	if (biov->bi_data_len != rbund->rb_rsize)
		return -DER_IO_INVAL;

	irec->ir_cs_size = csum->cs_len;
	irec->ir_cs_type = csum->cs_type;
	irec->ir_size	 = biov->bi_data_len;
	irec->ir_ex_addr = biov->bi_addr;
	irec->ir_ver	 = rbund->rb_ver;

	if (irec->ir_size == 0) { /* it is a punch */
		csum->cs_csum = NULL;
		return 0;
	}
	/** at this point, it's assumed that enough was allocated for the irec
	 *  to hold a checksum of length csum->cs_len
	 */
	memcpy(vos_irec2csum(irec), csum->cs_csum, csum->cs_len);

	return 0;
}

/**
 * Return memory address of data and checksum of this record.
 */
static int
svt_rec_load(struct btr_instance *tins, struct btr_record *rec,
	     struct vos_key_bundle *kbund, struct vos_rec_bundle *rbund)
{
	struct svt_hkey    *skey = (struct svt_hkey *)&rec->rec_hkey[0];
	struct vos_irec_df *irec = vos_rec2irec(tins, rec);
	daos_csum_buf_t    *csum = rbund->rb_csum;
	struct bio_iov     *biov = rbund->rb_biov;

	if (kbund != NULL) /* called from iterator */
		kbund->kb_epoch = skey->sv_epoch;

	/* NB: return record address, caller should copy/rma data for it */
	biov->bi_data_len = irec->ir_size;
	biov->bi_addr = irec->ir_ex_addr;
	biov->bi_buf = NULL;

	if (irec->ir_size != 0 && csum) {
		csum->cs_len		= irec->ir_cs_size;
		csum->cs_buf_len	= irec->ir_cs_size;
		csum->cs_type		= irec->ir_cs_type;
		if (csum->cs_csum)
			memcpy(csum->cs_csum,
			       vos_irec2csum(irec), csum->cs_len);
		else
			csum->cs_csum = (uint8_t *) vos_irec2csum(irec);
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
svt_hkey_size(void)
{
	return sizeof(struct svt_hkey);
}

static int
svt_rec_msize(int alloc_overhead)
{
	/* Doesn't presently include checksum so the interface will need to
	 * change slightly for that.
	 */
	return alloc_overhead + sizeof(struct vos_irec_df);
}

/** generate hkey */
static void
svt_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	struct svt_hkey		*skey = (struct svt_hkey *)hkey;
	struct vos_key_bundle	*kbund;

	kbund = iov2key_bundle(key_iov);
	skey->sv_epoch = kbund->kb_epoch;
}

/** compare the hashed key */
static int
svt_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct svt_hkey *skey1 = (struct svt_hkey *)&rec->rec_hkey[0];
	struct svt_hkey *skey2 = (struct svt_hkey *)hkey;

	if (skey1->sv_epoch < skey2->sv_epoch)
		return BTR_CMP_LT;

	if (skey1->sv_epoch > skey2->sv_epoch)
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

/** allocate a new record and fetch data */
static int
svt_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_rec_bundle	*rbund;
	struct vos_key_bundle	*kbund;
	int			 rc = 0;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

	if (UMMID_IS_NULL(rbund->rb_mmid)) {
		rec->rec_off = umem_alloc_off(&tins->ti_umm,
					   vos_irec_size(rbund));
		if (UMOFF_IS_NULL(rec->rec_off))
			return -DER_NOMEM;
	} else {
		umem_tx_add(&tins->ti_umm, rbund->rb_mmid,
			    vos_irec_msize(rbund));
		rec->rec_off = umem_id2off(&tins->ti_umm, rbund->rb_mmid);
		rbund->rb_mmid = UMMID_NULL; /* taken over by btree */
	}

	rc = vos_dtx_register_record(&tins->ti_umm, rec->rec_off,
				     DTX_RT_SVT, 0);
	if (rc != 0)
		/* It is unnecessary to free the PMEM that will be dropped
		 * automatically when the PMDK transaction is aborted.
		 */
		return rc;

	rc = svt_rec_store(tins, rec, kbund, rbund);
	return rc;
}

static int
svt_rec_free(struct btr_instance *tins, struct btr_record *rec,
	      void *args)
{
	struct vos_irec_df *irec = vos_rec2irec(tins, rec);
	bio_addr_t *addr = &irec->ir_ex_addr;

	if (UMOFF_IS_NULL(rec->rec_off))
		return 0;

	vos_dtx_deregister_record(&tins->ti_umm, irec->ir_dtx, rec->rec_off,
				  DTX_RT_SVT);
	if (args != NULL) {
		*(umem_id_t *)args = umem_off2id(&tins->ti_umm, rec->rec_off);
		rec->rec_off = UMOFF_NULL; /** taken over by user */
		return 0;
	}

	if (addr->ba_type == DAOS_MEDIA_NVME && !bio_addr_is_hole(addr)) {
		struct vea_space_info *vsi = tins->ti_blks_info;
		uint64_t blk_off;
		uint32_t blk_cnt;
		int rc;

		D_ASSERT(vsi != NULL);

		blk_off = vos_byte2blkoff(addr->ba_off);
		blk_cnt = vos_byte2blkcnt(irec->ir_size);

		rc = vea_free(vsi, blk_off, blk_cnt);
		if (rc)
			D_ERROR("Error on block free. %d\n", rc);
	}

	umem_free_off(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
svt_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_key_bundle	*kbund = NULL;
	struct vos_rec_bundle	*rbund;

	rbund = iov2rec_bundle(val_iov);
	if (key_iov != NULL)
		kbund = iov2key_bundle(key_iov);

	svt_rec_load(tins, rec, kbund, rbund);
	return 0;
}

static int
svt_rec_update(struct btr_instance *tins, struct btr_record *rec,
		daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct svt_hkey		*skey;
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;

	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);

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

	skey = (struct svt_hkey *)&rec->rec_hkey[0];
	D_DEBUG(DB_IO, "Overwrite epoch "DF_U64"\n", skey->sv_epoch);

	umem_tx_add_off(&tins->ti_umm, rec->rec_off, vos_irec_size(rbund));
	return svt_rec_store(tins, rec, kbund, rbund);
}

static int
svt_check_availability(struct btr_instance *tins, struct btr_record *rec,
		       uint32_t intent)
{
	struct vos_irec_df	*svt;

	svt = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	return vos_dtx_check_availability(&tins->ti_umm, tins->ti_coh,
					  svt->ir_dtx, UMOFF_NULL, intent,
					  DTX_RT_SVT);
}

static btr_ops_t singv_btr_ops = {
	.to_rec_msize		= svt_rec_msize,
	.to_hkey_size		= svt_hkey_size,
	.to_hkey_gen		= svt_hkey_gen,
	.to_hkey_cmp		= svt_hkey_cmp,
	.to_rec_alloc		= svt_rec_alloc,
	.to_rec_free		= svt_rec_free,
	.to_rec_fetch		= svt_rec_fetch,
	.to_rec_update		= svt_rec_update,
	.to_check_availability	= svt_check_availability,
};

/**
 * @} vos_singv_btr
 */
static struct vos_btr_attr vos_btr_attrs[] = {
	{
		.ta_class	= VOS_BTR_DKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= VOS_OFEAT_BITS | BTR_FEAT_DIRECT_KEY,
		.ta_name	= "vos_dkey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_AKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= VOS_OFEAT_BITS | BTR_FEAT_DIRECT_KEY,
		.ta_name	= "vos_akey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_SINGV,
		.ta_order	= VOS_SVT_ORDER,
		.ta_feats	= 0,
		.ta_name	= "singv",
		.ta_ops		= &singv_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_END,
		.ta_name	= "null",
	},
};

static int
tree_open_create(struct vos_object *obj, enum vos_tree_class tclass, int flags,
		 struct vos_krec_df *krec, daos_handle_t *sub_toh)
{
	struct umem_attr	*uma = vos_obj2uma(obj);
	struct vea_space_info	*info = obj->obj_cont->vc_pool->vp_vea_info;
	daos_handle_t		 coh = vos_cont2hdl(obj->obj_cont);
	int			 expected_flag;
	int			 unexpected_flag;
	int			 rc = 0;

	if (flags & SUBTR_EVT) {
		expected_flag = KREC_BF_EVT;
		unexpected_flag = KREC_BF_BTR;
	} else {
		expected_flag = KREC_BF_BTR;
		unexpected_flag = KREC_BF_EVT;
	}

	if (krec->kr_bmap & unexpected_flag) {
		if (flags & SUBTR_CREATE) {
			D_ERROR("Mixing single value and array not allowed\n");
			rc = -DER_NO_PERM;
			goto out;
		}
		D_DEBUG(DB_TRACE, "Attempt to fetch wrong value type\n");
		rc = -DER_NONEXIST;
		goto out;
	}

	if (krec->kr_bmap & expected_flag) {
		if (flags & SUBTR_EVT) {
			rc = evt_open(&krec->kr_evt, uma, coh, info, sub_toh);
		} else {
			rc = dbtree_open_inplace_ex(&krec->kr_btr, uma, coh,
						    info, sub_toh);
		}
		if (rc != 0)
			D_ERROR("Failed to open tree: %d\n", rc);

		goto out;
	}

	D_ASSERT(flags & SUBTR_CREATE);

	if (flags & SUBTR_EVT) {
		rc = evt_create(EVT_FEAT_DEFAULT, VOS_EVT_ORDER, uma,
				&krec->kr_evt, coh, sub_toh);
		if (rc != 0) {
			D_ERROR("Failed to create evtree: %d\n", rc);
			goto out;
		}
	} else {
		struct vos_btr_attr	*ta;
		uint64_t		 tree_feats = 0;

		/* Step-1: find the btree attributes and create btree */
		if (tclass == VOS_BTR_DKEY) {
			uint64_t	obj_feats;

			/* Check and setup the akey key compare bits */
			obj_feats = daos_obj_id2feat(obj->obj_df->vo_id.id_pub);
			tree_feats = (uint64_t)obj_feats << VOS_OFEAT_SHIFT;
			if (obj_feats & DAOS_OF_AKEY_UINT64)
				tree_feats |= VOS_KEY_CMP_UINT64_SET;
			else if (obj_feats & DAOS_OF_AKEY_LEXICAL)
				tree_feats |= VOS_KEY_CMP_LEXICAL_SET;
		}


		ta = obj_tree_find_attr(tclass);

		D_DEBUG(DB_TRACE, "Create dbtree %s feats 0x"DF_X64"\n",
			ta->ta_name, tree_feats);

		rc = dbtree_create_inplace_ex(ta->ta_class, tree_feats,
					      ta->ta_order, uma, &krec->kr_btr,
					      coh, sub_toh);
		if (rc != 0) {
			D_ERROR("Failed to create btree: %d\n", rc);
			goto out;
		}
	}

	/* NB: Only happens on create so krec will be in the transaction log
	 * already.
	 */
	krec->kr_bmap |= expected_flag;

out:
	return rc;
}

/**
 * Load the subtree roots embedded in the parent tree record.
 *
 * akey tree	: all akeys under the same dkey
 * recx tree	: all record extents under the same akey, this function will
 *		  load both btree and evtree root.
 */
int
key_tree_prepare(struct vos_object *obj, daos_epoch_t epoch,
		 daos_handle_t toh, enum vos_tree_class tclass,
		 daos_key_t *key, int flags, uint32_t intent,
		 struct vos_krec_df **krecp, daos_handle_t *sub_toh)
{
	struct vos_krec_df	*krec;
	daos_csum_buf_t		 csum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	if (krecp != NULL)
		*krecp = NULL;

	D_DEBUG(DB_IO, "prepare tree, flags=%x, tclass=%d\n", flags, tclass);
	if (tclass != VOS_BTR_AKEY && (flags & SUBTR_EVT))
		D_GOTO(out, rc = -DER_INVAL);

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= key;
	kbund.kb_epoch	= epoch;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_mmid	= UMMID_NULL;
	rbund.rb_csum	= &csum;
	rbund.rb_tclass	= tclass;
	memset(&csum, 0, sizeof(csum));

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested, instead:
	 *
	 * - In the case of fetch, we load the subtree root stored in the
	 *   parent tree leaf.
	 * - In the case of update/insert, we call dbtree_update() which may
	 *   create the root for the subtree, or just return it if it's already
	 *   there.
	 */
	rc = dbtree_fetch(toh, BTR_PROBE_GE | BTR_PROBE_MATCHED, intent, &kiov,
			  NULL, &riov);
	switch (rc) {
	default:
		D_ERROR("fetch failed: %d\n", rc);
		goto out;

	case -DER_NONEXIST:
		if (!(flags & SUBTR_CREATE))
			goto out;

		kbund.kb_epoch	= DAOS_EPOCH_MAX;
		rbund.rb_iov	= key;
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, intent, &kiov, &riov);
		if (rc) {
			D_ERROR("Failed to upsert: %d\n", rc);
			goto out;
		}
		krec = rbund.rb_krec;
		break;
	case 0:
		krec = rbund.rb_krec;
		if (krec->kr_bmap & KREC_BF_PUNCHED &&
		    krec->kr_latest == epoch) {
			/* already punched in this epoch */
			rc = -DER_NONEXIST;
			goto out;
		}
		break;
	}

	/* For updates, we need to be able to modify the epoch range */
	if (krecp != NULL)
		*krecp = krec;

	rc = tree_open_create(obj, tclass, flags, krec, sub_toh);
 out:
	return rc;
}

/** Close the opened trees */
void
key_tree_release(daos_handle_t toh, bool is_array)
{
	int	rc;

	if (is_array)
		rc = evt_close(toh);
	else
		rc = dbtree_close(toh);

	D_ASSERT(rc == 0 || rc == -DER_NO_HDL);
}

/**
 * Punch a key in its parent tree.
 */
int
key_tree_punch(struct vos_object *obj, daos_handle_t toh, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, int flags)
{
	struct vos_key_bundle	*kbund;
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	umem_off_t		 umoff;
	int			 rc;
	bool			 replay = (flags & VOS_OF_REPLAY_PC);

	rc = dbtree_fetch(toh, BTR_PROBE_GE | BTR_PROBE_MATCHED,
			  DAOS_INTENT_PUNCH, key_iov, NULL, val_iov);
	if (rc != 0) {
		if (rc == -DER_INPROGRESS)
			return rc;

		D_ASSERT(rc == -DER_NONEXIST);
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, DAOS_INTENT_PUNCH,
				   key_iov, val_iov);
		if (rc)
			D_ERROR("Failed to add new punch, rc=%d\n", rc);

		return rc;
	}

	/* found a match */
	kbund = iov2key_bundle(key_iov);
	rbund = iov2rec_bundle(val_iov);
	krec = rbund->rb_krec;
	umoff = umem_ptr2off(vos_obj2umm(obj), krec);

	if (krec->kr_bmap & KREC_BF_PUNCHED &&
	    krec->kr_latest == kbund->kb_epoch) {
		D_DEBUG(DB_TRACE, "epoch "DF_U64" already punched\n",
			krec->kr_latest);
		return 0; /* nothing to do */
	}

	if (krec->kr_latest >= kbund->kb_epoch && !replay) {
		D_CRIT("Underwrite is only allowed for punch replay: "
		       DF_U64" >="DF_U64"\n", krec->kr_latest, kbund->kb_epoch);
		return -DER_NO_PERM;
	}

	/* PROBE_EQ == insert in this case */
	rc = dbtree_upsert(toh, BTR_PROBE_EQ, DAOS_INTENT_PUNCH, key_iov,
			   val_iov);
	if (rc != 0 || replay)
		return rc;

	if (vos_dth_get() == NULL) { /* delete the max epoch */
		struct umem_instance	*umm = vos_obj2umm(obj);
		struct vos_krec_df	*krec2;
		struct vos_key_bundle	 kbund2;
		daos_iov_t	         tmp;

		krec2 = rbund->rb_krec;
		if (krec->kr_bmap & KREC_BF_BTR) {
			krec2->kr_btr = krec->kr_btr;
			umem_tx_add_ptr(umm, &krec->kr_btr,
					sizeof(krec->kr_btr));
			memset(&krec->kr_btr, 0, sizeof(krec->kr_btr));
		} else {
			D_ASSERT(krec->kr_bmap & KREC_BF_EVT);
			krec2->kr_evt = krec->kr_evt;
			umem_tx_add_ptr(umm, &krec->kr_evt,
					sizeof(krec->kr_evt));
			memset(&krec->kr_evt, 0, sizeof(krec->kr_evt));
		}
		/* Capture the earliest modification time of the removed key */
		krec2->kr_earliest = krec->kr_earliest;
		krec2->kr_bmap = krec->kr_bmap | KREC_BF_PUNCHED;

		tree_key_bundle2iov(&kbund2, &tmp);
		kbund2.kb_key	= kbund->kb_key;
		kbund2.kb_epoch	= DAOS_EPOCH_MAX;

		rc = dbtree_delete(toh, &tmp, NULL);
		if (rc)
			D_ERROR("Failed to delete: %d\n", rc);
	} else {
		rc = vos_dtx_register_record(btr_hdl2umm(toh), umoff,
					     DTX_RT_KEY, DTX_RF_EXCHANGE_SRC);
	}
	return rc;
}

/** initialize tree for an object */
int
obj_tree_init(struct vos_object *obj)
{
	struct vos_btr_attr *ta	= &vos_btr_attrs[0];
	int			rc;

	if (!daos_handle_is_inval(obj->obj_toh))
		return 0;

	D_ASSERT(obj->obj_df);
	if (obj->obj_df->vo_tree.tr_class == 0) {
		uint64_t	tree_feats	= 0;
		daos_ofeat_t	obj_feats;

		D_DEBUG(DB_DF, "Create btree for object\n");

		obj_feats = daos_obj_id2feat(obj->obj_df->vo_id.id_pub);
		/* Use hashed key if feature bits aren't set for object */
		tree_feats = (uint64_t)obj_feats << VOS_OFEAT_SHIFT;
		if (obj_feats & DAOS_OF_DKEY_UINT64)
			tree_feats |= VOS_KEY_CMP_UINT64_SET;
		else if (obj_feats & DAOS_OF_DKEY_LEXICAL)
			tree_feats |= VOS_KEY_CMP_LEXICAL_SET;

		rc = dbtree_create_inplace_ex(ta->ta_class, tree_feats,
					      ta->ta_order, vos_obj2uma(obj),
					      &obj->obj_df->vo_tree,
					      vos_cont2hdl(obj->obj_cont),
					      &obj->obj_toh);
	} else {
		D_DEBUG(DB_DF, "Open btree for object\n");
		rc = dbtree_open_inplace_ex(&obj->obj_df->vo_tree,
					    vos_obj2uma(obj),
					    vos_cont2hdl(obj->obj_cont),
					    NULL, &obj->obj_toh);
	}
	return rc;
}

/** close btree for an object */
int
obj_tree_fini(struct vos_object *obj)
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
obj_tree_register(void)
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
obj_tree_find_attr(unsigned tree_class)
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
