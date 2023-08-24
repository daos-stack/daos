/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * vos/vos_tree.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos/mem.h>
#include <daos/object.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"

uint64_t vos_evt_feats = EVT_FEAT_SORT_DIST;

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

static struct vos_svt_key *
iov2svt_key(d_iov_t *key_iov)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct vos_svt_key));
	return (struct vos_svt_key *)key_iov->iov_buf;
}

static struct vos_rec_bundle *
iov2rec_bundle(d_iov_t *val_iov)
{
	D_ASSERT(val_iov->iov_len == sizeof(struct vos_rec_bundle));
	return (struct vos_rec_bundle *)val_iov->iov_buf;
}

/**
 * @defgroup vos_key_btree vos key-btree
 * @{
 */
/**
 * Store a key and its checksum as a durable struct.
 */
static int
ktr_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key_iov, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	d_iov_t			*iov  = rbund->rb_iov;
	struct dcs_csum_info	*csum = rbund->rb_csum;
	char			*kbuf;

	krec->kr_cs_size = csum->cs_len;
	if (krec->kr_cs_size != 0) {
		D_ASSERT(csum->cs_csum);
		krec->kr_cs_type = csum->cs_type;
		memcpy(vos_krec2csum(krec), csum->cs_csum, csum->cs_len);
	}
	kbuf = vos_krec2key(krec);

	if (iov->iov_buf != NULL) {
		D_ASSERT(iov->iov_buf == key_iov->iov_buf);
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
	     d_iov_t *key, struct vos_rec_bundle *rbund)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	d_iov_t			*iov  = rbund->rb_iov;
	struct dcs_csum_info	*csum = rbund->rb_csum;
	char			*kbuf;

	kbuf = vos_krec2key(krec);
	iov->iov_len = krec->kr_size;

	if (key != NULL)
		d_iov_set(key, kbuf, krec->kr_size);

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
ktr_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	struct ktr_hkey		*kkey = (struct ktr_hkey *)hkey;
	struct umem_pool        *umm_pool = tins->ti_umm.umm_pool;

	hkey_common_gen(key_iov, hkey);

	if (key_iov->iov_len > KH_INLINE_MAX)
		vos_kh_set(kkey->kh_murmur64, umm_pool->up_store.store_standalone);
}

/** compare the hashed key */
static int
ktr_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct ktr_hkey *k1 = (struct ktr_hkey *)&rec->rec_hkey[0];
	struct ktr_hkey *k2 = (struct ktr_hkey *)hkey;

	return hkey_common_cmp(k1, k2);
}

static int
ktr_key_cmp_lexical(struct vos_krec_df *krec, d_iov_t *kiov)
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
ktr_key_cmp_default(struct vos_krec_df *krec, d_iov_t *kiov)
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
	    d_iov_t *key_iov)
{
	struct vos_krec_df	*krec;
	uint64_t		 feats = tins->ti_root->tr_feats;
	int			 cmp = 0;

	krec  = vos_rec2krec(tins, rec);

	if (feats & VOS_KEY_CMP_LEXICAL)
		cmp = ktr_key_cmp_lexical(krec, key_iov);
	else
		cmp = ktr_key_cmp_default(krec, key_iov);

	if (cmp != BTR_CMP_EQ)
		return cmp;

	return BTR_CMP_EQ;
}

static void
ktr_key_encode(struct btr_instance *tins, d_iov_t *key,
	       daos_anchor_t *anchor)
{
	if (key)
		embedded_key_encode(key, anchor);
}

static void
ktr_key_decode(struct btr_instance *tins, d_iov_t *key,
	       daos_anchor_t *anchor)
{
	embedded_key_decode(key, anchor);
}

/** create a new key-record, or install an externally allocated key-record */
static int
ktr_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	      d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct vos_rec_bundle	*rbund;
	struct vos_krec_df	*krec;
	int			 rc = 0;

	rbund = iov2rec_bundle(val_iov);

	rec->rec_off = umem_zalloc(&tins->ti_umm, vos_krec_size(rbund));
	if (UMOFF_IS_NULL(rec->rec_off))
		return -DER_NOSPACE;

	krec = vos_rec2krec(tins, rec);
	rc = ilog_create(&tins->ti_umm, &krec->kr_ilog);
	if (rc != 0) {
		D_ERROR("Failure to create incarnation log\n");
		return rc;
	}

	if (rbund->rb_tclass == VOS_BTR_DKEY)
		krec->kr_bmap |= KREC_BF_DKEY;

	rbund->rb_krec = krec;

	ktr_rec_store(tins, rec, key_iov, rbund);

	return rc;
}

static int
ktr_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct vos_krec_df	*krec;
	struct umem_attr	 uma;
	struct ilog_desc_cbs	 cbs;
	daos_handle_t		 coh;
	int			 gc;
	int			 rc;
	struct vos_pool		*pool;

	if (UMOFF_IS_NULL(rec->rec_off))
		return 0;

	krec = vos_rec2krec(tins, rec);
	umem_attr_get(&tins->ti_umm, &uma);

	vos_ilog_desc_cbs_init(&cbs, tins->ti_coh);
	rc = ilog_destroy(&tins->ti_umm, &cbs, &krec->kr_ilog);
	if (rc != 0)
		return rc;

	pool = (struct vos_pool *)tins->ti_priv;
	vos_ilog_ts_evict(&krec->kr_ilog, (krec->kr_bmap & KREC_BF_DKEY) ?
			  VOS_TS_TYPE_DKEY : VOS_TS_TYPE_AKEY, pool->vp_sysdb);

	D_ASSERT(tins->ti_priv);
	gc = (krec->kr_bmap & KREC_BF_DKEY) ? GC_DKEY : GC_AKEY;
	coh = vos_cont2hdl(args);
	return gc_add_item(pool, coh, gc, rec->rec_off, 0);
}

static int
ktr_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_krec_df	*krec = vos_rec2krec(tins, rec);
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);

	rbund->rb_krec = krec;

	if (key_iov != NULL)
		ktr_rec_load(tins, rec, key_iov, rbund);

	return 0;
}

static int
ktr_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov, d_iov_t *val_out)
{
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);

	rbund->rb_krec = vos_rec2krec(tins, rec);
	/* NB: do nothing at here except return the sub-tree root,
	 * because the real update happens in the sub-tree (index &
	 * epoch tree).
	 */
	return 0;
}

static umem_off_t
ktr_node_alloc(struct btr_instance *tins, int size)
{
	return umem_zalloc(&tins->ti_umm, size);
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
	.to_node_alloc		= ktr_node_alloc,
};

/**
 * @} vos_key_btree
 */

/**
 * @defgroup vos_singv_btr vos single value btree
 * @{
 */

static int
svt_rec_store(struct btr_instance *tins, struct btr_record *rec,
	      struct vos_svt_key *skey, struct vos_rec_bundle *rbund)
{
	struct vos_container	*cont = vos_hdl2cont(tins->ti_coh);
	struct dtx_handle	*dth	= vos_dth_get(cont->vc_pool->vp_sysdb);
	struct vos_irec_df	*irec	= vos_rec2irec(tins, rec);
	struct dcs_csum_info	*csum	= rbund->rb_csum;
	struct bio_iov		*biov	= rbund->rb_biov;

	if (bio_iov2len(biov) != rbund->rb_rsize)
		return -DER_IO_INVAL;

	irec->ir_cs_size	= csum->cs_len;
	irec->ir_cs_type	= csum->cs_type;
	irec->ir_size		= bio_iov2len(biov);
	irec->ir_gsize		= rbund->rb_gsize;
	irec->ir_ex_addr	= biov->bi_addr;
	irec->ir_ver		= rbund->rb_ver;
	irec->ir_minor_epc	= skey->sk_minor_epc;

	if (irec->ir_size == 0) { /* it is a punch */
		csum->cs_csum = NULL;
		return 0;
	}
	/** at this point, it's assumed that enough was allocated for the irec
	 *  to hold a checksum of length csum->cs_len
	 */
	if (dtx_is_valid_handle(dth) && dth->dth_flags & DTE_LEADER &&
	    irec->ir_ex_addr.ba_type == DAOS_MEDIA_SCM &&
	    DAOS_FAIL_CHECK(DAOS_VC_DIFF_REC)) {
		void	*addr;

		irec->ir_cs_size = 0;
		irec->ir_cs_type = 0;
		addr = vos_irec2data(irec);
		*((int *)addr) = rand();
	} else {
		memcpy(vos_irec2csum(irec), csum->cs_csum, csum->cs_len);
	}

	return 0;
}

/**
 * Return memory address of data and checksum of this record.
 */
static int
svt_rec_load(struct btr_instance *tins, struct btr_record *rec,
	     struct vos_svt_key *key, struct vos_rec_bundle *rbund)
{
	daos_epoch_t		*epc  = (daos_epoch_t *)&rec->rec_hkey[0];
	struct vos_irec_df	*irec = vos_rec2irec(tins, rec);
	struct dcs_csum_info	*csum = rbund->rb_csum;
	struct bio_iov		*biov = rbund->rb_biov;

	if (key != NULL) {/* called from iterator */
		key->sk_epoch = *epc;
		key->sk_minor_epc = irec->ir_minor_epc;
	}

	/* NB: return record address, caller should copy/rma data for it */
	bio_iov_set_len(biov, irec->ir_size);
	biov->bi_addr = irec->ir_ex_addr;
	biov->bi_buf = NULL;

	if (irec->ir_size != 0 && csum) {
		csum->cs_len		= irec->ir_cs_size;
		csum->cs_buf_len	= irec->ir_cs_size;
		csum->cs_type		= irec->ir_cs_type;
		csum->cs_nr		= 1; /** sv only has 1 csum */
		csum->cs_chunksize	= CSUM_NO_CHUNK;
		if (csum->cs_csum)
			memcpy(csum->cs_csum,
			       vos_irec2csum(irec), csum->cs_len);
		else
			csum->cs_csum = (uint8_t *) vos_irec2csum(irec);
	}

	rbund->rb_rsize	= irec->ir_size;
	rbund->rb_gsize	= irec->ir_gsize;
	rbund->rb_ver	= irec->ir_ver;
	rbund->rb_dtx_state = vos_dtx_ent_state(irec->ir_dtx, vos_hdl2cont(tins->ti_coh), *epc);
	rbund->rb_off = rec->rec_off;
	return 0;
}

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
svt_hkey_size(void)
{
	return sizeof(daos_epoch_t);
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
svt_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	daos_epoch_t		*epc = hkey;
	struct vos_svt_key	*skey_in;

	D_ASSERT(key_iov->iov_len == sizeof(struct vos_svt_key));
	D_ASSERT(key_iov->iov_buf != NULL);

	skey_in = (struct vos_svt_key *)key_iov->iov_buf;
	*epc = skey_in->sk_epoch;
}

/** compare the hashed key */
static int
svt_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	daos_epoch_t		*epc1 = (daos_epoch_t *)&rec->rec_hkey[0];
	daos_epoch_t		*epc2 = hkey;

	if (*epc1 < *epc2)
		return BTR_CMP_LT;

	if (*epc1 > *epc2)
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

/** Single value is always pre-allocated so rbund->rb_off should be pointer to
 * it.   Just assert this condition to simplify the code a bit.
 */
static int
svt_rec_alloc_common(struct btr_instance *tins, struct btr_record *rec,
		     struct vos_svt_key *skey, struct vos_rec_bundle *rbund)
{
	struct vos_irec_df	*irec;
	int			 rc;

	D_ASSERT(!UMOFF_IS_NULL(rbund->rb_off));
	rc = umem_tx_xadd(&tins->ti_umm, rbund->rb_off, vos_irec_msize(rbund),
			  UMEM_XADD_NO_SNAPSHOT);
	if (rc != 0)
		return rc;

	rec->rec_off = rbund->rb_off;
	rbund->rb_off = UMOFF_NULL; /* taken over by btree */

	irec	= vos_rec2irec(tins, rec);
	rc = vos_dtx_register_record(&tins->ti_umm, rec->rec_off,
				     DTX_RT_SVT, &irec->ir_dtx);
	if (rc != 0)
		/* It is unnecessary to free the PMEM that will be dropped
		 * automatically when the PMDK transaction is aborted.
		 */
		return rc;

	rc = svt_rec_store(tins, rec, skey, rbund);
	return rc;
}

/** allocate a new record and fetch data */
static int
svt_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	       d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct vos_svt_key	*skey = key_iov->iov_buf;
	struct vos_rec_bundle	*rbund;

	rbund = iov2rec_bundle(val_iov);

	return svt_rec_alloc_common(tins, rec, skey, rbund);
}

/** Find the nvme extent in reserved list and move it to deferred cancel list */
static void
cancel_nvme_exts(bio_addr_t *addr, struct dtx_handle *dth)
{
	struct vea_resrvd_ext	*ext;
	struct dtx_rsrvd_uint	*dru;
	int			 i;
	uint64_t		 blk_off;

	if (addr->ba_type != DAOS_MEDIA_NVME)
		return;

	blk_off = vos_byte2blkoff(addr->ba_off);

	/** Find the allocation and move it to the deferred list */
	for (i = 0; i < dth->dth_rsrvd_cnt; i++) {
		dru = &dth->dth_rsrvds[i];

		d_list_for_each_entry(ext, &dru->dru_nvme, vre_link) {
			if (ext->vre_blk_off == blk_off) {
				d_list_del(&ext->vre_link);
				d_list_add_tail(&ext->vre_link,
						&dth->dth_deferred_nvme);
				return;
			}
		}
	}

	D_ASSERT(0);
}

static int
svt_rec_free_internal(struct btr_instance *tins, struct btr_record *rec,
		      bool overwrite)
{
	daos_epoch_t		*epc = (daos_epoch_t *)&rec->rec_hkey[0];
	struct vos_irec_df	*irec = vos_rec2irec(tins, rec);
	bio_addr_t		*addr = &irec->ir_ex_addr;
	struct dtx_handle	*dth = NULL;
	struct umem_rsrvd_act	*rsrvd_scm;
	struct vos_container	*cont = vos_hdl2cont(tins->ti_coh);
	int			 i;

	if (UMOFF_IS_NULL(rec->rec_off))
		return 0;

	if (overwrite) {
		dth = vos_dth_get(cont->vc_pool->vp_sysdb);
		if (dth == NULL)
			return -DER_NO_PERM; /* Not allowed */
	}

	vos_dtx_deregister_record(&tins->ti_umm, tins->ti_coh,
				  irec->ir_dtx, *epc, rec->rec_off);

	if (!overwrite) {
		int	rc;

		/* SCM value is stored together with vos_irec_df */
		if (addr->ba_type == DAOS_MEDIA_NVME) {
			struct vos_pool *pool = tins->ti_priv;

			D_ASSERT(pool != NULL);
			rc = vos_bio_addr_free(pool, addr, irec->ir_size);
			if (rc)
				return rc;
		}

		return umem_free(&tins->ti_umm, rec->rec_off);
	}

	/** There can't be more cancellations than updates in this
	 *  modification so just use the current one
	 */
	D_ASSERT(dth->dth_op_seq > 0);
	D_ASSERT(dth->dth_op_seq <= dth->dth_deferred_cnt);
	i = dth->dth_op_seq - 1;
	rsrvd_scm = dth->dth_deferred[i];
	D_ASSERT(rsrvd_scm != NULL);

	umem_defer_free(&tins->ti_umm, rec->rec_off, rsrvd_scm);

	cancel_nvme_exts(addr, dth);

	return 0;
}

static int
svt_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	return svt_rec_free_internal(tins, rec, false);
}

static int
svt_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_svt_key	*key = NULL;
	struct vos_rec_bundle	*rbund;

	rbund = iov2rec_bundle(val_iov);
	if (key_iov != NULL)
		key = iov2svt_key(key_iov);

	return svt_rec_load(tins, rec, key, rbund);
}

static int
svt_rec_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key_iov, d_iov_t *val_iov, d_iov_t *val_out)
{
	struct vos_svt_key	*skey;
	struct vos_irec_df	*irec;
	struct vos_rec_bundle	*rbund;
	int			 rc;

	rbund = iov2rec_bundle(val_iov);
	skey = (struct vos_svt_key *)key_iov->iov_buf;
	irec = vos_rec2irec(tins, rec);

	/** Disallow same epoch overwrite */
	if (skey->sk_minor_epc <= irec->ir_minor_epc)
		return -DER_NO_PERM;

	D_DEBUG(DB_IO, "Overwrite epoch "DF_X64".%d\n", skey->sk_epoch,
		skey->sk_minor_epc);

	rc = svt_rec_free_internal(tins, rec, true);
	if (rc != 0)
		return rc;

	return svt_rec_alloc_common(tins, rec, skey, rbund);
}

static int
svt_check_availability(struct btr_instance *tins, struct btr_record *rec,
		       uint32_t intent)
{
	daos_epoch_t		*epc = (daos_epoch_t *)&rec->rec_hkey[0];
	struct vos_irec_df	*svt;

	svt = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	return vos_dtx_check_availability(tins->ti_coh, svt->ir_dtx, *epc,
					  intent, DTX_RT_SVT, true);
}

static umem_off_t
svt_node_alloc(struct btr_instance *tins, int size)
{
	return umem_zalloc(&tins->ti_umm, size);
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
	.to_node_alloc		= svt_node_alloc,
};

/**
 * @} vos_singv_btr
 */
static struct vos_btr_attr vos_btr_attrs[] = {
	{
		.ta_class	= VOS_BTR_DKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY | BTR_FEAT_DYNAMIC_ROOT,
		.ta_name	= "vos_dkey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_AKEY,
		.ta_order	= VOS_KTR_ORDER,
		.ta_feats	= BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY | BTR_FEAT_DYNAMIC_ROOT,
		.ta_name	= "vos_akey",
		.ta_ops		= &key_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_SINGV,
		.ta_order	= VOS_SVT_ORDER,
		.ta_feats	= BTR_FEAT_DYNAMIC_ROOT,
		.ta_name	= "singv",
		.ta_ops		= &singv_btr_ops,
	},
	{
		.ta_class	= VOS_BTR_END,
		.ta_name	= "null",
	},
};

static int
evt_dop_bio_free(struct umem_instance *umm, struct evt_desc *desc,
		 daos_size_t nob, void *args)
{
	struct vos_pool *pool = (struct vos_pool *)args;

	return vos_bio_addr_free(pool, &desc->dc_ex_addr, nob);
}

static int
evt_dop_log_status(struct umem_instance *umm, daos_epoch_t epoch,
		   struct evt_desc *desc, int intent, bool retry, void *args)
{
	daos_handle_t coh;

	coh.cookie = (unsigned long)args;
	D_ASSERT(coh.cookie != 0);
	return vos_dtx_check_availability(coh, desc->dc_dtx, epoch, intent, DTX_RT_EVT, retry);
}

int
evt_dop_log_add(struct umem_instance *umm, struct evt_desc *desc, void *args)
{
	return vos_dtx_register_record(umm, umem_ptr2off(umm, desc), DTX_RT_EVT,
				       &desc->dc_dtx);
}

static int
evt_dop_log_del(struct umem_instance *umm, daos_epoch_t epoch,
		struct evt_desc *desc, void *args)
{
	daos_handle_t	coh;

	coh.cookie = (unsigned long)args;
	vos_dtx_deregister_record(umm, coh, desc->dc_dtx, epoch,
				  umem_ptr2off(umm, desc));
	return 0;
}

void
vos_evt_desc_cbs_init(struct evt_desc_cbs *cbs, struct vos_pool *pool,
		      daos_handle_t coh)
{
	/* NB: coh is not required for destroy */
	cbs->dc_bio_free_cb	= evt_dop_bio_free;
	cbs->dc_bio_free_args	= (void *)pool;
	cbs->dc_log_status_cb	= evt_dop_log_status;
	cbs->dc_log_status_args	= (void *)(unsigned long)coh.cookie;
	cbs->dc_log_add_cb	= evt_dop_log_add;
	cbs->dc_log_add_args	= NULL;
	cbs->dc_log_del_cb	= evt_dop_log_del;
	cbs->dc_log_del_args	= (void *)(unsigned long)coh.cookie;
}

static int
tree_open_create(struct vos_object *obj, enum vos_tree_class tclass, int flags,
		 struct vos_krec_df *krec, bool created, daos_handle_t *sub_toh)
{
	struct umem_attr        *uma = vos_obj2uma(obj);
	struct vos_pool		*pool = vos_obj2pool(obj);
	daos_handle_t		 coh = vos_cont2hdl(obj->obj_cont);
	struct evt_desc_cbs	 cbs;
	int			 expected_flag;
	uint64_t                 feats = vos_evt_feats;
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

	vos_evt_desc_cbs_init(&cbs, pool, coh);
	if (krec->kr_bmap & expected_flag) {
		if (flags & SUBTR_EVT) {
			rc = evt_open(&krec->kr_evt, uma, &cbs, sub_toh);
		} else {
			rc = dbtree_open_inplace_ex(&krec->kr_btr, uma, coh,
						    pool, sub_toh);
		}
		if (rc != 0)
			D_ERROR("Failed to open tree: "DF_RC"\n", DP_RC(rc));

		goto out;
	}

	if ((flags & SUBTR_CREATE) == 0) {
		/** This can happen if application does a punch first before any
		 *  updates.   Simply return -DER_NONEXIST in such case.
		 */
		rc = -DER_NONEXIST;
		goto out;
	}

	if (!created) {
		rc = umem_tx_add_ptr(vos_obj2umm(obj), krec,
				     sizeof(*krec));
		if (rc != 0) {
			D_ERROR("Failed to add key record to transaction: " DF_RC "\n", DP_RC(rc));
			goto out;
		}
	}

	if (flags & SUBTR_EVT) {
		if (pool->vp_feats & VOS_POOL_FEAT_DYN_ROOT)
			feats |= EVT_FEAT_DYNAMIC_ROOT;
		rc = evt_create(&krec->kr_evt, feats, VOS_EVT_ORDER, uma, &cbs, sub_toh);
		if (rc != 0) {
			D_ERROR("Failed to create evtree: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	} else {
		struct vos_btr_attr	*ta;
		uint64_t		 tree_feats = 0;

		/* Step-1: find the btree attributes and create btree */
		if (tclass == VOS_BTR_DKEY) {
			enum daos_otype_t type;

			/* Check and setup the akey key compare bits */
			type = daos_obj_id2type(obj->obj_df->vo_id.id_pub);
			if (daos_is_akey_uint64_type(type))
				tree_feats |= VOS_KEY_CMP_UINT64_SET;
			else if (daos_is_akey_lexical_type(type))
				tree_feats |= VOS_KEY_CMP_LEXICAL_SET;
		}

		ta = obj_tree_find_attr(tclass);

		D_DEBUG(DB_TRACE, "Create dbtree %s feats 0x"DF_X64"\n",
			ta->ta_name, tree_feats);

		rc = dbtree_create_inplace_ex(ta->ta_class, tree_feats,
					      ta->ta_order, uma, &krec->kr_btr,
					      coh, pool, sub_toh);
		if (rc != 0) {
			D_ERROR("Failed to create btree: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
	}
	/* NB: Only happens on create so krec will be in the transaction log
	 * already.  Mark that tree supports the aggregation optimizations.
	 * At akey level, this bit map is used for the optimization.  At higher
	 * levels, only the tree_feats version is used.
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
key_tree_prepare(struct vos_object *obj, daos_handle_t toh,
		 enum vos_tree_class tclass, daos_key_t *key, int flags,
		 uint32_t intent, struct vos_krec_df **krecp,
		 daos_handle_t *sub_toh, struct vos_ts_set *ts_set)
{
	struct ilog_df		*ilog = NULL;
	struct vos_krec_df	*krec = NULL;
	struct dcs_csum_info	 csum;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 riov;
	bool			 created = false;
	int			 rc;
	int			 tmprc;

	/** reset the saved hash */
	vos_kh_clear(obj->obj_cont->vc_pool->vp_sysdb);

	if (krecp != NULL)
		*krecp = NULL;

	D_DEBUG(DB_TRACE, "prepare tree, flags=%x, tclass=%d\n", flags, tclass);
	if (tclass != VOS_BTR_AKEY && (flags & SUBTR_EVT))
		D_GOTO(out, rc = -DER_INVAL);

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_off	= UMOFF_NULL;
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
	rc = dbtree_fetch(toh, BTR_PROBE_EQ, intent, key,
			  NULL, &riov);
	switch (rc) {
	default:
		D_ERROR("fetch failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	case 0:
		krec = rbund.rb_krec;
		ilog = &krec->kr_ilog;
		/** fall through to cache re-cache entry */
	case -DER_NONEXIST:
		/** Key hash may already be calculated but isn't for some key
		 * types so pass it in here.
		 */
		if (ilog != NULL && (flags & SUBTR_CREATE))
			vos_ilog_ts_ignore(vos_obj2umm(obj), &krec->kr_ilog);
		tmprc = vos_ilog_ts_add(ts_set, ilog, key->iov_buf,
					(int)key->iov_len);
		if (tmprc != 0) {
			rc = tmprc;
			D_ASSERT(tmprc == -DER_NO_PERM);
			D_ASSERT(tclass == VOS_BTR_AKEY);
			goto out;
		}
		break;
	}

	if (rc == -DER_NONEXIST) {
		if (!(flags & SUBTR_CREATE))
			goto out;

		rbund.rb_iov	= key;
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, intent, key, &riov, NULL);
		if (rc) {
			D_ERROR("Failed to upsert: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
		krec = rbund.rb_krec;
		vos_ilog_ts_ignore(vos_obj2umm(obj), &krec->kr_ilog);
		vos_ilog_ts_mark(ts_set, &krec->kr_ilog);
		created = true;
	}

	if (sub_toh) {
		D_ASSERT(krec != NULL);
		rc = tree_open_create(obj, tclass, flags, krec, created,
				      sub_toh);
	}

	if (rc)
		goto out;

	D_ASSERT(krec != NULL);
	/* For updates, we need to be able to modify the epoch range */
	if (krecp != NULL)
		*krecp = krec;
 out:
	D_CDEBUG(rc == 0, DB_TRACE, DB_IO, "prepare tree, flags=%x, tclass=%d %d\n",
		 flags, tclass, rc);

	if (rc != 0 || tclass != VOS_BTR_AKEY || !(flags & SUBTR_CREATE))
		return rc;

	/** If it's evtree, evt_insert will detect if aggregation is needed.  For single value,
	 *  return 1 to indicate aggregation is on update to an existing tree.
	 */
	if (flags & SUBTR_EVT)
		return 0;

	return created ? 0 : 1;
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
key_tree_punch(struct vos_object *obj, daos_handle_t toh, daos_epoch_t epoch,
	       daos_epoch_t bound, d_iov_t *key_iov, d_iov_t *val_iov,
	       uint64_t flags, struct vos_ts_set *ts_set, umem_off_t *known_key,
	       struct vos_ilog_info *parent, struct vos_ilog_info *info)
{
	struct vos_rec_bundle	*rbund = iov2rec_bundle(val_iov);
	struct vos_krec_df	*krec;
	struct ilog_df		*ilog = NULL;
	daos_epoch_range_t	 epr = {0, epoch};
	bool			 mark = false;
	int			 rc;
	int			 lrc;

	rc = dbtree_fetch(toh, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, key_iov, NULL,
			  val_iov);

	if (rc == 0 || rc == -DER_NONEXIST) {
		if (rc == 0) {
			rbund = iov2rec_bundle(val_iov);
			krec = rbund->rb_krec;
			ilog = &krec->kr_ilog;
		}

		if (ilog)
			vos_ilog_ts_ignore(vos_obj2umm(obj), ilog);
		lrc = vos_ilog_ts_add(ts_set, ilog, key_iov->iov_buf,
				      (int)key_iov->iov_len);
		if (lrc != 0) {
			rc = lrc;
			goto done;
		}
		if (rc == -DER_NONEXIST) {
			if (flags & VOS_OF_COND_PUNCH)
				goto done;
		}
	} else if (rc != 0) {
		/** Abort on any other error */
		goto done;
	}

	if (rc != 0) {
		/** If it's not a replay punch, we should not insert
		 *  anything.   In such case, ts_set will be NULL
		 */
		D_ASSERT(rc == -DER_NONEXIST);
		/* use BTR_PROBE_BYPASS to avoid probe again */
		rc = dbtree_upsert(toh, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE,
				   key_iov, val_iov, NULL);
		if (rc)
			goto done;

		mark = true;
	}

	/** Punch always adds a log entry */
	rbund = iov2rec_bundle(val_iov);
	krec = rbund->rb_krec;
	ilog = &krec->kr_ilog;

	if (mark) {
		vos_ilog_ts_ignore(vos_obj2umm(obj), ilog);
		vos_ilog_ts_mark(ts_set, ilog);
	}

	rc = vos_ilog_punch(obj->obj_cont, ilog, &epr, bound, parent,
			    info, ts_set, true,
			    (flags & VOS_OF_REPLAY_PC) != 0);

	if (rc != 0)
		goto done;

	if (*known_key == umem_ptr2off(vos_obj2umm(obj), krec)) {
		/** Set the value to UMOFF_NULL so punch propagation will run full check */
		rc = umem_tx_add_ptr(vos_obj2umm(obj), known_key, sizeof(*known_key));
		if (rc)
			D_GOTO(done, rc);
		*known_key |= 0x1;
	}

	rc = vos_key_mark_agg(obj->obj_cont, krec, epoch);
done:
	VOS_TX_LOG_FAIL(rc, "Failed to punch key: "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
key_tree_delete(struct vos_object *obj, daos_handle_t toh, d_iov_t *key_iov)
{
	/* Delete a dkey or akey from tree @toh */
	return dbtree_delete(toh, BTR_PROBE_EQ, key_iov, obj->obj_cont);
}

/** initialize tree for an object */
int
obj_tree_init(struct vos_object *obj)
{
	struct vos_btr_attr *ta	= &vos_btr_attrs[0];
	int		     rc;

	if (daos_handle_is_valid(obj->obj_toh))
		return 0;

	D_ASSERT(obj->obj_df);
	if (obj->obj_df->vo_tree.tr_class == 0) {
		uint64_t tree_feats = 0;
		enum daos_otype_t type;

		D_DEBUG(DB_DF, "Create btree for object\n");

		type = daos_obj_id2type(obj->obj_df->vo_id.id_pub);
		if (daos_is_dkey_uint64_type(type))
			tree_feats |= VOS_KEY_CMP_UINT64_SET;
		else if (daos_is_dkey_lexical_type(type))
			tree_feats |= VOS_KEY_CMP_LEXICAL_SET;

		rc = dbtree_create_inplace_ex(ta->ta_class, tree_feats,
					      ta->ta_order, vos_obj2uma(obj),
					      &obj->obj_df->vo_tree,
					      vos_cont2hdl(obj->obj_cont),
					      vos_obj2pool(obj),
					      &obj->obj_toh);
	} else {
		D_DEBUG(DB_DF, "Open btree for object\n");
		rc = dbtree_open_inplace_ex(&obj->obj_df->vo_tree,
					    vos_obj2uma(obj),
					    vos_cont2hdl(obj->obj_cont),
					    vos_obj2pool(obj), &obj->obj_toh);
	}

	if (rc)
		D_ERROR("obj_tree_init failed, "DF_RC"\n", DP_RC(rc));
	return rc;
}

/** close btree for an object */
int
obj_tree_fini(struct vos_object *obj)
{
	int	rc = 0;

	/* NB: tree is created inplace, so don't need to destroy */
	if (daos_handle_is_valid(obj->obj_toh)) {
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
			D_ERROR("Failed to register %s: "DF_RC"\n", ta->ta_name,
				DP_RC(rc));
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
