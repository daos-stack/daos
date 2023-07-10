/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include "bio_wal.h"

#define BIO_META_MAGIC		(0xbc202210)
#define BIO_META_VERSION	1

#define BIO_WAL_MAGIC		(0xaf202209)
#define BIO_WAL_VERSION		1

#define WAL_HDR_MAGIC		(0xc01d2019)

#define WAL_ID_BITS		64			/* Never change this */
#define WAL_ID_OFF_BITS		32
#define WAL_ID_SEQ_BITS		(WAL_ID_BITS - WAL_ID_OFF_BITS)
#define WAL_ID_OFF_MAX		((1ULL << WAL_ID_OFF_BITS) - 1)
#define WAL_ID_SEQ_MAX		((1ULL << WAL_ID_SEQ_BITS) - 1)
#define WAL_ID_OFF_MASK		WAL_ID_OFF_MAX
#define WAL_ID_SEQ_MASK		WAL_ID_SEQ_MAX

#define WAL_BLK_SZ		4096			/* 4k bytes, atomic block I/O */
D_CASSERT(sizeof(struct wal_header) <= WAL_BLK_SZ);
#define WAL_CSUM_LEN		sizeof(uint32_t)
D_CASSERT(sizeof(struct wal_trans_tail) == WAL_CSUM_LEN);

#define WAL_MIN_CAPACITY	(8192 * WAL_BLK_SZ)	/* Minimal WAL capacity, in bytes */
#define WAL_MAX_TRANS_BLKS	2048			/* Maximal blocks used by a transaction */
#define WAL_HDR_BLKS		1			/* Ensure atomic header write */

#define META_BLK_SZ		WAL_BLK_SZ
#define META_HDR_BLKS		1

static void
meta_csum_fini(struct bio_meta_context *mc)
{
	D_ASSERT(mc->mc_csum_algo != NULL);
	D_ASSERT(mc->mc_csum_ctx != NULL);

	if (mc->mc_csum_algo->cf_destroy)
		mc->mc_csum_algo->cf_destroy(mc->mc_csum_ctx);

	mc->mc_csum_algo = NULL;
	mc->mc_csum_ctx = NULL;
}

static int
meta_csum_init(struct bio_meta_context *mc, uint16_t csum_type)
{
	int	rc = 0;

	D_ASSERT(mc->mc_csum_algo == NULL);
	D_ASSERT(mc->mc_csum_ctx == NULL);

	mc->mc_csum_algo = daos_mhash_type2algo(csum_type);
	if (mc->mc_csum_algo == NULL) {
		D_ERROR("Failed to init csum type: %u\n", csum_type);
		return -DER_INVAL;
	}

	if (mc->mc_csum_algo->cf_init) {
		rc = mc->mc_csum_algo->cf_init(&mc->mc_csum_ctx);
		if (rc)
			D_ERROR("Csum type init failed. "DF_RC"\n", DP_RC(rc));
	}

	return rc;
}

static int
meta_csum_calc(struct bio_meta_context *mc, void *buf, unsigned int buf_len,
	       void *csum_buf, unsigned int csum_len)
{
	int	rc;

	D_ASSERT(mc->mc_csum_algo->cf_reset != NULL);
	D_ASSERT(mc->mc_csum_algo->cf_update != NULL);
	D_ASSERT(mc->mc_csum_algo->cf_finish != NULL);

	rc = mc->mc_csum_algo->cf_reset(mc->mc_csum_ctx);
	if (rc)
		return rc;

	rc = mc->mc_csum_algo->cf_update(mc->mc_csum_ctx, buf, buf_len);
	if (rc)
		return rc;

	rc = mc->mc_csum_algo->cf_finish(mc->mc_csum_ctx, csum_buf, csum_len);
	return rc;
}

static inline int
meta_csum_len(struct bio_meta_context *mc)
{
	unsigned int	csum_len;

	D_ASSERT(mc->mc_csum_algo != NULL);

	if (mc->mc_csum_algo->cf_get_size)
		csum_len = mc->mc_csum_algo->cf_get_size(mc->mc_csum_ctx);
	else
		csum_len = mc->mc_csum_algo->cf_hash_len;
	D_ASSERT(csum_len == WAL_CSUM_LEN);

	return csum_len;
}

/* Low WAL_ID_BITS bits of ID is block offset within the WAL */
static inline uint32_t
id2off(uint64_t tx_id)
{
	return tx_id & WAL_ID_OFF_MASK;
}

/* High WAL_ID_SEQ_BITS bits of ID is sequence number which increase by 1 once WAL wraps */
static inline uint32_t
id2seq(uint64_t tx_id)
{
	return (tx_id >> WAL_ID_OFF_BITS) & WAL_ID_SEQ_MASK;
}

static inline uint64_t
seqoff2id(uint32_t seq, uint32_t off)
{
	return ((uint64_t)seq << WAL_ID_OFF_BITS) + off;
}

/* 0 on equal; -1 on (id1 < id2); +1 on (id1 > id2) */
static inline int
wal_id_cmp(struct wal_super_info *si, uint64_t id1, uint64_t id2)
{
	/*
	 * 32 bits sequence number allows the WAL wrapping 4 billion times,
	 * though we'd still check the unlikely sequence overflow here.
	 */
	if (id2seq(si->si_ckp_id) == WAL_ID_SEQ_MAX && id2seq(si->si_unused_id) == 0) {
		if ((id2seq(id1) == id2seq(id2)) ||
		    (id2seq(id1) > 0 && id2seq(id2) > 0))
			return (id1 < id2) ? -1 : ((id1 > id2) ? 1 : 0);
		else if (id2seq(id1) == 0)
			return 1;
		else
			return -1;
	}

	return (id1 < id2) ? -1 : ((id1 > id2) ? 1 : 0);
}

int
bio_wal_id_cmp(struct bio_meta_context *mc, uint64_t id1, uint64_t id2)
{
	struct wal_super_info	*si = &mc->mc_wal_info;

	return wal_id_cmp(si, id1, id2);
}

/* Get next ID by current ID & blocks used by current ID */
static inline uint64_t
wal_next_id(struct wal_super_info *si, uint64_t id, uint32_t blks)
{
	struct wal_header	*hdr = &si->si_header;
	uint32_t		 next_off, next_seq;
	uint32_t		 seq = id2seq(id);

	/* Start position */
	if (blks == 0) {
		D_ASSERT(id == 0);
		return id;
	}

	next_off = id2off(id) + blks;
	if (next_off < hdr->wh_tot_blks) {
		next_seq = seq;
	} else {
		next_off -= hdr->wh_tot_blks;
		next_seq = (seq == WAL_ID_SEQ_MAX) ? 0 : (seq + 1);
	}

	return seqoff2id(next_seq, next_off);
}

static uint32_t
wal_used_blks(struct wal_super_info *si)
{
	uint64_t	next_ckp_id;
	uint32_t	next_ckp_off, unused_off, next_ckp_seq, unused_seq;
	uint32_t	tot_blks = si->si_header.wh_tot_blks;

	next_ckp_id = wal_next_id(si, si->si_ckp_id, si->si_ckp_blks);

	D_ASSERTF(wal_id_cmp(si, next_ckp_id, si->si_unused_id) <= 0,
		  "Checkpoint ID "DF_U64" > Unused ID "DF_U64"\n",
		  next_ckp_id, si->si_unused_id);

	/* Everything is check-pointed & no pending transactions */
	if (next_ckp_id == si->si_unused_id) {
		D_ASSERT(si->si_ckp_id == si->si_commit_id);
		return 0;
	}

	next_ckp_off = id2off(next_ckp_id);
	next_ckp_seq = id2seq(next_ckp_id);
	unused_off = id2off(si->si_unused_id);
	unused_seq = id2seq(si->si_unused_id);
	D_ASSERT(next_ckp_off < tot_blks && unused_off < tot_blks);

	if (unused_off > next_ckp_off) {
		D_ASSERT(next_ckp_seq == unused_seq);
		return unused_off - next_ckp_off;
	}

	D_ASSERT((next_ckp_seq == WAL_ID_SEQ_MAX && unused_seq == 0) ||
		 (next_ckp_seq + 1) == unused_seq);

	if (unused_off == next_ckp_off)
		return tot_blks;
	else
		return tot_blks - next_ckp_off + unused_off;
}

static inline uint32_t
wal_free_blks(struct wal_super_info *si)
{
	uint32_t	used_blks = wal_used_blks(si);

	D_ASSERT(used_blks <= si->si_header.wh_tot_blks);
	return si->si_header.wh_tot_blks - used_blks;
}

static bool
reserve_allowed(struct wal_super_info *si)
{
	/*
	 * Gap in WAL isn't allowed, so if any transaction failed, it's ID has to be
	 * reused by later transaction. Let's simply freeze ID reserving when any
	 * transaction failed and the depended transactions are not drained.
	 */
	if (si->si_tx_failed) {
		D_ASSERT(!d_list_empty(&si->si_pending_list));
		D_WARN("Prior transaction failed, pending transactions not drained\n");
		return false;
	}

	/* Freeze ID reserving when checkpointing didn't reclaim space in time */
	if (wal_free_blks(si) < WAL_MAX_TRANS_BLKS) {
		D_WARN("WAL space is insufficient (%u free blocks)\n", wal_free_blks(si));
		return false;
	}

	return true;
}

static void
wakeup_reserve_waiters(struct wal_super_info *si, bool wakeup_all)
{
	if (si->si_rsrv_waiters == 0)
		return;

	if (reserve_allowed(si) || wakeup_all) {
		ABT_mutex_lock(si->si_mutex);
		if (wakeup_all)
			ABT_cond_broadcast(si->si_rsrv_wq);
		else
			ABT_cond_signal(si->si_rsrv_wq);
		ABT_mutex_unlock(si->si_mutex);
	}
}

static inline struct bio_dma_stats *
ioc2dma_stats(struct bio_io_context *bic)
{
	D_ASSERT(bic && bic->bic_xs_ctxt && bic->bic_xs_ctxt->bxc_dma_buf);
	return &bic->bic_xs_ctxt->bxc_dma_buf->bdb_stats;
}

/* Caller must guarantee no yield between bio_wal_reserve() and bio_wal_submit() */
int
bio_wal_reserve(struct bio_meta_context *mc, uint64_t *tx_id)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct bio_dma_stats	*stats = ioc2dma_stats(mc->mc_wal);
	int			 rc = 0;

	if (!si->si_rsrv_waiters && reserve_allowed(si))
		goto done;

	si->si_rsrv_waiters++;
	if (stats->bds_wal_waiters)
		d_tm_inc_gauge(stats->bds_wal_waiters, 1);

	ABT_mutex_lock(si->si_mutex);
	ABT_cond_wait(si->si_rsrv_wq, si->si_mutex);
	ABT_mutex_unlock(si->si_mutex);

	D_ASSERT(si->si_rsrv_waiters > 0);
	si->si_rsrv_waiters--;
	if (stats->bds_wal_waiters)
		d_tm_dec_gauge(stats->bds_wal_waiters, 1);

	wakeup_reserve_waiters(si, false);
	/* It could happen when wakeup all on WAL unload */
	if (!reserve_allowed(si))
		rc = -DER_SHUTDOWN;
done:
	*tx_id = si->si_unused_id;
	return rc;
}

struct wal_blks_desc {
	unsigned int	bd_blks;	/* Total blocks for this transaction */
	unsigned int	bd_payload_idx;	/* Start block index for payload */
	unsigned int	bd_payload_off;	/* Offset within block for payload start */
	unsigned int	bd_tail_off;	/* Offset within block for tail */
};

/* Get wal_blks_desc by total action nr & total payload size */
static void
calc_trans_blks(unsigned int act_nr, unsigned int payload_sz, unsigned int blk_sz,
		struct wal_blks_desc *bd)
{
	unsigned int	max_ents, remainder, left_bytes;
	unsigned int	entry_blks, payload_blks;
	unsigned int	entry_sz = sizeof(struct wal_trans_entry);

	D_ASSERT(act_nr > 0);
	blk_sz -= sizeof(struct wal_trans_head);

	/* Calculates entry blocks & left bytes in the last entry block */
	max_ents = blk_sz / entry_sz;
	entry_blks = (act_nr + max_ents - 1) / max_ents;
	D_ASSERT(entry_blks > 0);

	remainder = act_nr - (act_nr / max_ents) * max_ents;
	if (remainder == 0)
		left_bytes = blk_sz - (max_ents * entry_sz);
	else
		left_bytes = blk_sz - (remainder * entry_sz);

	/* Set payload start block */
	bd->bd_payload_off = sizeof(struct wal_trans_head);
	if (left_bytes > 0) {
		bd->bd_payload_idx = entry_blks - 1;
		bd->bd_payload_off += (blk_sz - left_bytes);
	} else {
		bd->bd_payload_idx = entry_blks;
	}

	/* Calculates payload blocks & left bytes in the last payload block */
	if (left_bytes >= payload_sz) {
		payload_blks = 0;
		left_bytes -= payload_sz;
	} else {
		payload_sz -= left_bytes;
		payload_blks = (payload_sz + blk_sz - 1) / blk_sz;
		remainder = payload_sz - (payload_sz / blk_sz) * blk_sz;
		left_bytes = (remainder == 0) ? 0 : blk_sz - remainder;
	}

	/* Set tail csum block & total block */
	bd->bd_tail_off = sizeof(struct wal_trans_head);
	if (left_bytes >= sizeof(struct wal_trans_tail)) {
		bd->bd_blks = entry_blks + payload_blks;
		bd->bd_tail_off += (blk_sz - left_bytes);
		return;
	}

	bd->bd_blks = entry_blks + payload_blks + 1;
}

struct wal_trans_blk {
	struct wal_trans_head	*tb_hdr;
	void			*tb_buf;	/* DMA buffer address mapped for the block */
	unsigned int		 tb_idx;	/* Logical block index within the transaction */
	unsigned int		 tb_off;	/* Start offset within the block */
	unsigned int		 tb_blk_sz;	/* Block size */
};

/* Get the mapped DMA address for a block used by transaction */
static void
get_trans_blk(struct bio_sglist *bsgl, unsigned int idx, unsigned int blk_sz,
	      struct wal_trans_blk *tb)
{
	struct bio_iov	*biov;
	unsigned int	 iov_blks, blk_off = idx;

	D_ASSERT(bsgl->bs_nr_out == 1 || bsgl->bs_nr_out == 2);
	biov = &bsgl->bs_iovs[0];
	iov_blks = (bio_iov2len(biov) + blk_sz - 1) / blk_sz;

	if (blk_off >= iov_blks) {
		D_ASSERT(bsgl->bs_nr_out == 2);

		blk_off -= iov_blks;
		biov = &bsgl->bs_iovs[1];
		iov_blks = (bio_iov2len(biov) + blk_sz - 1) / blk_sz;
		D_ASSERT(blk_off < iov_blks);
	}

	tb->tb_buf = biov->bi_buf + (blk_off * blk_sz);
	tb->tb_idx = idx;
	tb->tb_off = 0;
}

static inline void
place_blk_hdr(struct wal_trans_blk *tb)
{
	D_ASSERT(tb->tb_off == 0);
	memcpy(tb->tb_buf, tb->tb_hdr, sizeof(*tb->tb_hdr));
	tb->tb_off += sizeof(*tb->tb_hdr);
}

static inline void
next_trans_blk(struct bio_sglist *bsgl, struct wal_trans_blk *tb)
{
	get_trans_blk(bsgl, tb->tb_idx + 1, tb->tb_blk_sz, tb);
	place_blk_hdr(tb);
}

static inline void
place_entry(struct wal_trans_blk *tb, struct wal_trans_entry *entry)
{
	D_ASSERT((tb->tb_off >= sizeof(*tb->tb_hdr)) &&
		 (tb->tb_off + sizeof(*entry) <= tb->tb_blk_sz));
	memcpy(tb->tb_buf + tb->tb_off, entry, sizeof(*entry));
	tb->tb_off += sizeof(*entry);
}

static void
place_payload(struct bio_sglist *bsgl, struct wal_blks_desc *bd, struct wal_trans_blk *tb,
	      uint64_t addr, uint32_t len)
{
	unsigned int	left, copy_sz;

	D_ASSERT(len > 0);
	while (len > 0) {
		D_ASSERT(tb->tb_idx >= bd->bd_payload_idx && tb->tb_idx < bd->bd_blks);
		D_ASSERT(tb->tb_off >= sizeof(*tb->tb_hdr) && tb->tb_off <= tb->tb_blk_sz);

		left = tb->tb_blk_sz - tb->tb_off;
		/* Current payload block is full, move to next */
		if (left == 0) {
			next_trans_blk(bsgl, tb);
			continue;
		}

		copy_sz = (left >= len) ? len : left;
		memcpy(tb->tb_buf + tb->tb_off, (void *)addr, copy_sz);

		tb->tb_off += copy_sz;
		addr += copy_sz;
		len -= copy_sz;
	}
}

static inline bool
skip_wal_tx_tail(struct wal_super_info *si)
{
	return (si->si_header.wh_flags & WAL_HDR_FL_NO_TAIL);
}

static void
place_tail(struct bio_meta_context *mc, struct bio_sglist *bsgl, struct wal_blks_desc *bd,
	   struct wal_trans_blk *tb)
{
	struct bio_iov	*biov;
	unsigned int	 left, tot_len, buf_len;
	unsigned int	 tail_sz = sizeof(struct wal_trans_tail);
	void		*csum_buf;
	int		 rc;

	D_ASSERT(tb->tb_off >= sizeof(*tb->tb_hdr) && tb->tb_off <= tb->tb_blk_sz);
	left = tb->tb_blk_sz - tb->tb_off;

	/* Tail is on a new block */
	if (left < tail_sz) {
		D_ASSERT(bd->bd_tail_off == sizeof(*tb->tb_hdr));
		D_ASSERT(tb->tb_idx + 2 == bd->bd_blks);
		/* Zeroing left bytes for csum calculation */
		if (left > 0)
			memset(tb->tb_buf + tb->tb_off, 0, left);
		next_trans_blk(bsgl, tb);
	} else {
		D_ASSERT(bd->bd_tail_off == tb->tb_off);
		D_ASSERT(tb->tb_idx + 1 == bd->bd_blks);
	}

	if (skip_wal_tx_tail(&mc->mc_wal_info))
		return;

	D_ASSERT(mc->mc_csum_algo->cf_reset != NULL);
	D_ASSERT(mc->mc_csum_algo->cf_update != NULL);
	D_ASSERT(mc->mc_csum_algo->cf_finish != NULL);

	rc = mc->mc_csum_algo->cf_reset(mc->mc_csum_ctx);
	D_ASSERT(rc == 0);

	/* Total length excluding tail */
	tot_len = (bd->bd_blks - 1) * tb->tb_blk_sz + bd->bd_tail_off;

	D_ASSERT(bsgl->bs_nr_out == 1 || bsgl->bs_nr_out == 2);
	biov = &bsgl->bs_iovs[0];
	if (bsgl->bs_nr_out == 1) {
		buf_len = tot_len;
		D_ASSERT((buf_len + tail_sz) <= bio_iov2len(biov));
	} else {
		buf_len = bio_iov2len(biov);
		D_ASSERT(buf_len < tot_len);
	}

	rc = mc->mc_csum_algo->cf_update(mc->mc_csum_ctx, bio_iov2buf(biov), buf_len);
	D_ASSERT(rc == 0);

	if (bsgl->bs_nr_out == 2) {
		biov = &bsgl->bs_iovs[1];
		buf_len = tot_len - buf_len;
		D_ASSERT((buf_len + tail_sz) <= bio_iov2len(biov));

		rc = mc->mc_csum_algo->cf_update(mc->mc_csum_ctx, bio_iov2buf(biov), buf_len);
		D_ASSERT(rc == 0);
	}

	csum_buf = tb->tb_buf + tb->tb_off;
	rc = mc->mc_csum_algo->cf_finish(mc->mc_csum_ctx, csum_buf, WAL_CSUM_LEN);
	D_ASSERT(rc == 0);
}

#define	INLINE_DATA_CSUM_NR	5
struct data_csum_array {
	unsigned int		 dca_nr;
	unsigned int		 dca_max_nr;
	struct umem_action	*dca_acts;
	struct umem_action	 dca_inline_acts[INLINE_DATA_CSUM_NR];
};

static void
fill_trans_blks(struct bio_meta_context *mc, struct bio_sglist *bsgl, struct umem_wal_tx *tx,
		struct data_csum_array *dc_arr, unsigned int blk_sz, struct wal_blks_desc *bd)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct umem_action	*act;
	struct wal_trans_head	 blk_hdr;
	struct wal_trans_entry	 entry;
	struct wal_trans_blk	 entry_blk, payload_blk;
	unsigned int		 left, entry_sz = sizeof(struct wal_trans_entry), dc_idx = 0;
	uint64_t		 src_addr;

	/* Simulate a server crash before the in-flight WAL tx committed */
	if (DAOS_FAIL_CHECK(DAOS_NVME_WAL_TX_LOST)) {
		D_ERROR("Injected WAL tx lost for ID:"DF_U64".\n", tx->utx_id);
		return;
	}

	blk_hdr.th_magic = WAL_HDR_MAGIC;
	blk_hdr.th_gen = si->si_header.wh_gen;
	blk_hdr.th_id = tx->utx_id;
	blk_hdr.th_tot_ents = umem_tx_act_nr(tx) + dc_arr->dca_nr;
	blk_hdr.th_tot_payload = umem_tx_act_payload_sz(tx);

	/* Initialize first entry block */
	get_trans_blk(bsgl, 0, blk_sz, &entry_blk);
	entry_blk.tb_hdr = &blk_hdr;
	entry_blk.tb_blk_sz = blk_sz;
	place_blk_hdr(&entry_blk);

	/* Initialize first payload block */
	get_trans_blk(bsgl, bd->bd_payload_idx, blk_sz, &payload_blk);
	payload_blk.tb_hdr = &blk_hdr;
	payload_blk.tb_blk_sz = blk_sz;
	D_ASSERT(bd->bd_payload_off >= sizeof(blk_hdr));
	/* Payload starts from a new block */
	if (bd->bd_payload_off == sizeof(blk_hdr))
		place_blk_hdr(&payload_blk);
	else
		payload_blk.tb_off = bd->bd_payload_off;

	act = umem_tx_act_first(tx);
	D_ASSERT(act != NULL);

	while (act != NULL) {
		/* Locate the entry block for this action */
		if (entry_blk.tb_idx < bd->bd_payload_idx) {
			D_ASSERT(entry_blk.tb_off <= blk_sz);
			left = blk_sz - entry_blk.tb_off;
			/* Current entry block is full, move to next entry block */
			if (left < entry_sz) {
				/* Zeoring left bytes for csum calculation */
				if (left > 0)
					memset(entry_blk.tb_buf + entry_blk.tb_off, 0, left);
				next_trans_blk(bsgl, &entry_blk);
			}
		} else if (entry_blk.tb_idx == bd->bd_payload_idx) {
			D_ASSERT((entry_blk.tb_off + entry_sz) <= bd->bd_payload_off);
		} else {
			D_ASSERTF(0, "Entry blk idx:%u > Payload blk idx:%u\n", entry_blk.tb_idx,
				  bd->bd_payload_idx);
		}

		entry.te_type = act->ac_opc;
		switch (act->ac_opc) {
		case UMEM_ACT_COPY:
		case UMEM_ACT_COPY_PTR:
			entry.te_off = act->ac_copy.addr;
			entry.te_len = act->ac_copy.size;
			entry.te_data = 0;
			if (act->ac_opc == UMEM_ACT_COPY)
				src_addr = (uint64_t)&act->ac_copy.payload;
			else
				src_addr = act->ac_copy_ptr.ptr;
			place_entry(&entry_blk, &entry);
			place_payload(bsgl, bd, &payload_blk, src_addr, entry.te_len);
			break;
		case UMEM_ACT_ASSIGN:
			entry.te_off = act->ac_assign.addr;
			entry.te_len = act->ac_assign.size;
			entry.te_data = act->ac_assign.val;
			place_entry(&entry_blk, &entry);
			break;
		case UMEM_ACT_MOVE:
			entry.te_off = act->ac_move.dst;
			entry.te_len = act->ac_move.size;
			entry.te_data = 0;
			place_entry(&entry_blk, &entry);
			place_payload(bsgl, bd, &payload_blk, (uint64_t)&act->ac_move.src,
				      sizeof(uint64_t));
			break;
		case UMEM_ACT_SET:
			entry.te_off = act->ac_set.addr;
			entry.te_len = act->ac_set.size;
			entry.te_data = act->ac_set.val;
			place_entry(&entry_blk, &entry);
			break;
		case UMEM_ACT_SET_BITS:
		case UMEM_ACT_CLR_BITS:
			entry.te_off = act->ac_op_bits.addr;
			entry.te_len = act->ac_op_bits.num;
			entry.te_data = act->ac_op_bits.pos;
			place_entry(&entry_blk, &entry);
			break;
		case UMEM_ACT_CSUM:
			entry.te_off = act->ac_csum.addr;
			entry.te_len = act->ac_csum.size;
			entry.te_data = act->ac_csum.csum;
			place_entry(&entry_blk, &entry);
			break;
		default:
			D_ASSERTF(0, "Invalid opc %u\n", act->ac_opc);
			break;
		}

		if (dc_idx == 0) {
			act = umem_tx_act_next(tx);
			if (act != NULL)
				continue;
		}
		/* Put data csum actions after other actions */
		if (dc_idx < dc_arr->dca_nr) {
			act = &dc_arr->dca_acts[dc_idx];
			dc_idx++;
		} else {
			act = NULL;
		}
	}

	place_tail(mc, bsgl, bd, &payload_blk);
}

static inline uint64_t
off2lba(struct wal_super_info *si, unsigned int blk_off)
{
	return (uint64_t)(blk_off + WAL_HDR_BLKS) * si->si_header.wh_blk_bytes;
}

struct wal_tx_desc {
	d_list_t		 td_link;
	struct wal_super_info	*td_si;
	struct bio_desc		*td_biod_tx;		/* IOD for WAL I/O */
	struct bio_desc		*td_biod_data;		/* IOD for async data I/O */
	uint64_t		 td_id;
	uint32_t		 td_blks;		/* Blocks used by this tx */
	int			 td_error;
	unsigned int		 td_wal_complete:1;	/* Indicating WAL I/O completed */
};

static inline struct wal_tx_desc *
wal_tx_prev(struct wal_tx_desc *wal_tx)
{
	struct wal_super_info	*si = wal_tx->td_si;

	D_ASSERT(si != NULL);
	D_ASSERT(!d_list_empty(&wal_tx->td_link));

	if (wal_tx->td_link.prev == &si->si_pending_list)
		return NULL;
	return d_list_entry(wal_tx->td_link.prev, struct wal_tx_desc, td_link);
}

static inline struct wal_tx_desc *
wal_tx_next(struct wal_tx_desc *wal_tx)
{
	struct wal_super_info	*si = wal_tx->td_si;

	D_ASSERT(si != NULL);
	D_ASSERT(!d_list_empty(&wal_tx->td_link));

	if (wal_tx->td_link.next == &si->si_pending_list)
		return NULL;
	return d_list_entry(wal_tx->td_link.next, struct wal_tx_desc, td_link);
}

static inline bool
tx_completed(struct wal_tx_desc *wal_tx)
{
	struct wal_tx_desc	*prev = wal_tx_prev(wal_tx);

	/*
	 * Complete WAL transaction when:
	 * - WAL I/O completed, and;
	 * - Async data I/O completed (if any), and;
	 * - No prior pending tx or current tx failed;
	 */
	return (wal_tx->td_wal_complete && (wal_tx->td_biod_data == NULL)) &&
	       ((prev == NULL) || wal_tx->td_error != 0);
}

static void
wal_tx_completion(struct wal_tx_desc *wal_tx, bool complete_next)
{
	struct bio_desc		*biod_tx = wal_tx->td_biod_tx;
	struct wal_super_info	*si = wal_tx->td_si;
	struct wal_tx_desc	*next;
	struct bio_dma_stats	*stats;
	bool			 try_wakeup = false;

	D_ASSERT(!d_list_empty(&wal_tx->td_link));
	D_ASSERT(biod_tx != NULL);
	D_ASSERT(si != NULL);

	next = wal_tx_next(wal_tx);
	biod_tx->bd_result = wal_tx->td_error;

	if (wal_tx->td_error) {
		/* Rollback unused ID */
		if (wal_id_cmp(si, wal_tx->td_id, si->si_unused_id) < 0)
			si->si_unused_id = wal_tx->td_id;

		if (next != NULL) {
			/* Propagate error to depended transactions, block incoming transactions */
			si->si_tx_failed = 1;
			next->td_error = wal_tx->td_error;
		} else {
			/* No depended transactions, unblock incoming transactions */
			si->si_tx_failed = 0;
			try_wakeup = true;
		}
	} else {
		D_ASSERT(wal_next_id(si, si->si_commit_id, si->si_commit_blks) == wal_tx->td_id);
		D_ASSERT(si->si_tx_failed == 0);

		si->si_commit_id = wal_tx->td_id;

		si->si_commit_blks = wal_tx->td_blks;
	}

	d_list_del_init(&wal_tx->td_link);

	stats = ioc2dma_stats(biod_tx->bd_ctxt);
	if (stats->bds_wal_qd)
		d_tm_dec_gauge(stats->bds_wal_qd, 1);

	/* The ABT_eventual could be NULL if WAL I/O IOD failed on DMA mapping in bio_iod_prep() */
	if (biod_tx->bd_dma_done != ABT_EVENTUAL_NULL)
		ABT_eventual_set(biod_tx->bd_dma_done, NULL, 0);

	/*
	 * To ensure the UNDO (for failed transactions) is performed before starting new
	 * transaction, the waiters blocked on WAL reserve should be waken up after the waiters
	 * blocked on WAL commit. Here we assume server ULT scheduler executes ULTs in FIFO
	 * order and no yield during UNDO.
	 */
	if (try_wakeup)
		wakeup_reserve_waiters(si, false);

	if (!complete_next)
		return;

	/* Call completion on depended completed transactions */
	while (next != NULL && tx_completed(next)) {
		wal_tx = next;
		next = wal_tx_next(wal_tx);
		wal_tx_completion(wal_tx, false);
	}
}

/* Transaction WAL I/O completion */
static void
wal_completion(void *arg, int err)
{
	struct wal_tx_desc	*wal_tx = arg;

	wal_tx->td_wal_complete = 1;
	if (err)
		wal_tx->td_error = err;

	if (tx_completed(wal_tx))
		wal_tx_completion(wal_tx, true);
}

/* Transaction associated data I/O (to data blob) completion */
static void
data_completion(void *arg, int err)
{
	struct wal_tx_desc	*wal_tx = arg;

	wal_tx->td_biod_data = NULL;
	if (err && wal_tx->td_error == 0)
		wal_tx->td_error = err;

	if (tx_completed(wal_tx))
		wal_tx_completion(wal_tx, true);
}

static inline void
free_data_csum(struct data_csum_array *dc_arr)
{
	if (dc_arr->dca_max_nr > INLINE_DATA_CSUM_NR)
		D_FREE(dc_arr->dca_acts);
}

static int
generate_data_csum(struct bio_meta_context *mc, struct bio_desc *biod_data,
		   struct data_csum_array *dc_arr)
{
	struct bio_rsrvd_dma	*rsrvd_dma;
	struct bio_rsrvd_region	*rg;
	struct umem_action	*act, *act_arr;
	void			*payload;
	unsigned int		 max_nr;
	int			 i, csum_len = meta_csum_len(mc), rc = 0;

	dc_arr->dca_nr = 0;
	dc_arr->dca_max_nr = INLINE_DATA_CSUM_NR;
	dc_arr->dca_acts = &dc_arr->dca_inline_acts[0];

	/* No async data write or the write is already completed */
	if (biod_data == NULL || biod_data->bd_inflights == 0)
		return 0;

	rsrvd_dma = &biod_data->bd_rsrvd;
	for (i = 0; i < rsrvd_dma->brd_rg_cnt; i++) {
		rg = &rsrvd_dma->brd_regions[i];

		D_ASSERT(rg->brr_chk != NULL);
		D_ASSERT(rg->brr_end > rg->brr_off);

		if (rg->brr_media != DAOS_MEDIA_NVME)
			continue;

		D_ASSERT(rg->brr_chk_off == 0);
		if (dc_arr->dca_nr == dc_arr->dca_max_nr) {
			max_nr = dc_arr->dca_max_nr * 2;
			D_ALLOC_ARRAY(act_arr, max_nr);
			if (act_arr == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			memcpy(act_arr, dc_arr->dca_acts, dc_arr->dca_max_nr * sizeof(*act));
			free_data_csum(dc_arr);
			dc_arr->dca_max_nr = max_nr;
			dc_arr->dca_acts = act_arr;
		}

		act = &dc_arr->dca_acts[dc_arr->dca_nr];
		act->ac_opc = UMEM_ACT_CSUM;
		act->ac_csum.addr = rg->brr_off;
		act->ac_csum.size = rg->brr_end - rg->brr_off;

		payload = rg->brr_chk->bdc_ptr + (rg->brr_pg_idx << BIO_DMA_PAGE_SHIFT);
		rc = meta_csum_calc(mc, payload, act->ac_csum.size, &act->ac_csum.csum, csum_len);
		if (rc) {
			D_ERROR("Failed to calculate data csum off:"DF_U64" len:%u, "DF_RC"\n",
				act->ac_csum.addr, act->ac_csum.size, DP_RC(rc));
			break;
		}
		dc_arr->dca_nr++;
	}

	if (rc)
		free_data_csum(dc_arr);
	else
		D_DEBUG(DB_IO, "Generate %u data csums\n", dc_arr->dca_nr);

	return rc;
}

static void
wait_tx_committed(struct wal_tx_desc *wal_tx)
{
	struct bio_desc		*biod_tx = wal_tx->td_biod_tx;
	struct bio_desc		*biod_data = wal_tx->td_biod_data;
	struct bio_xs_context	*xs_ctxt = biod_tx->bd_ctxt->bic_xs_ctxt;
	int			 rc;

	D_ASSERT(biod_tx->bd_dma_done != ABT_EVENTUAL_NULL);
	D_ASSERT(xs_ctxt != NULL);

	if (xs_ctxt->bxc_self_polling) {
		D_DEBUG(DB_IO, "Self poll completion\n");
		rc = xs_poll_completion(xs_ctxt, &biod_tx->bd_inflights, 0);
		if (rc)
			D_ERROR("Self pool completion failed. "DF_RC"\n", DP_RC(rc));
	} else if (biod_tx->bd_inflights != 0 || biod_data != NULL) {
		rc = ABT_eventual_wait(biod_tx->bd_dma_done, NULL);
		if (rc != ABT_SUCCESS)
			D_ERROR("ABT_eventual_wait failed. %d\n", rc);
	}
	/* The completion must have been called */
	D_ASSERT(d_list_empty(&wal_tx->td_link));
}

int
bio_wal_commit(struct bio_meta_context *mc, struct umem_wal_tx *tx, struct bio_desc *biod_data)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct bio_desc		*biod = NULL;
	struct bio_sglist	*bsgl;
	bio_addr_t		 addr = { 0 };
	struct wal_tx_desc	 wal_tx = { 0 };
	struct wal_blks_desc	 blk_desc = { 0 };
	struct data_csum_array	 dc_arr;
	unsigned int		 blks, unused_off;
	unsigned int		 tot_blks = si->si_header.wh_tot_blks;
	unsigned int		 blk_bytes = si->si_header.wh_blk_bytes;
	uint64_t		 tx_id = tx->utx_id;
	struct bio_dma_stats	*stats;
	int			 iov_nr, rc;

	/* Bypass WAL commit, used for performance evaluation only */
	if (daos_io_bypass & IOBP_WAL_COMMIT) {
		bio_yield(NULL);
		return 0;
	}

	D_DEBUG(DB_IO, "MC:%p WAL commit ID:"DF_U64" seq:%u off:%u, biod_data:%p inflights:%u\n",
		mc, tx_id, id2seq(tx_id), id2off(tx_id), biod_data,
		biod_data != NULL ? biod_data->bd_inflights : 0);

	rc = generate_data_csum(mc, biod_data, &dc_arr);
	if (rc) {
		D_ERROR("Failed to generate async data csum. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Calculate the required log blocks for this transaction */
	calc_trans_blks(umem_tx_act_nr(tx) + dc_arr.dca_nr, umem_tx_act_payload_sz(tx),
			blk_bytes, &blk_desc);

	D_ASSERT(blk_desc.bd_blks > 0);
	if (blk_desc.bd_blks > WAL_MAX_TRANS_BLKS) {
		D_ERROR("Too large transaction (%u blocks)\n", blk_desc.bd_blks);
		rc = -DER_INVAL;
		goto out;
	}

	biod = bio_iod_alloc(mc->mc_wal, NULL, 1, BIO_IOD_TYPE_UPDATE);
	if (biod == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	/* Figure out the regions in WAL for this transaction */
	D_ASSERT(wal_id_cmp(si, tx_id, si->si_unused_id) == 0);
	unused_off = id2off(si->si_unused_id);
	D_ASSERT(unused_off < tot_blks);
	if ((unused_off + blk_desc.bd_blks) <= tot_blks) {
		iov_nr = 1;
		blks = blk_desc.bd_blks;
	} else {
		iov_nr = 2;
		blks = (tot_blks - unused_off);
	}

	bsgl = bio_iod_sgl(biod, 0);
	rc = bio_sgl_init(bsgl, iov_nr);
	if (rc)
		goto out;

	bio_addr_set(&addr, DAOS_MEDIA_NVME, off2lba(si, unused_off));
	bio_iov_set(&bsgl->bs_iovs[0], addr, (uint64_t)blks * blk_bytes);
	if (iov_nr == 2) {
		bio_addr_set(&addr, DAOS_MEDIA_NVME, off2lba(si, 0));
		blks = blk_desc.bd_blks - blks;
		bio_iov_set(&bsgl->bs_iovs[1], addr, (uint64_t)blks * blk_bytes);
	}
	bsgl->bs_nr_out = iov_nr;

	wal_tx.td_id = si->si_unused_id;
	wal_tx.td_si = si;
	wal_tx.td_biod_tx = biod;
	wal_tx.td_biod_data = NULL;
	wal_tx.td_blks = blk_desc.bd_blks;
	/* Track in pending list from now on, since it could yield in bio_iod_prep() */
	d_list_add_tail(&wal_tx.td_link, &si->si_pending_list);

	stats = ioc2dma_stats(mc->mc_wal);
	if (stats->bds_wal_qd)
		d_tm_inc_gauge(stats->bds_wal_qd, 1);
	if (stats->bds_wal_sz)
		d_tm_set_gauge(stats->bds_wal_sz,
			       (blk_desc.bd_blks - 1) * blk_bytes + blk_desc.bd_tail_off);

	/* Update next unused ID */
	si->si_unused_id = wal_next_id(si, si->si_unused_id, blk_desc.bd_blks);

	/*
	 * Map the WAL regions to DMA buffer, bio_iod_prep() can guarantee FIFO order
	 * when it has to yield and wait for DMA buffer.
	 */
	rc = bio_iod_prep(biod, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc) {
		D_ERROR("WAL IOD prepare failed. "DF_RC"\n", DP_RC(rc));
		wal_completion(&wal_tx, rc);
		D_ASSERT(d_list_empty(&wal_tx.td_link));
		goto out;
	}

	/* Fill DMA buffer with transaction entries */
	fill_trans_blks(mc, bsgl, tx, &dc_arr, blk_bytes, &blk_desc);

	/* Set proper completion callbacks for data I/O & WAL I/O */
	if (biod_data != NULL) {
		if (biod_data->bd_inflights == 0) {
			wal_tx.td_error = biod_data->bd_result;
		} else {
			biod_data->bd_completion = data_completion;
			biod_data->bd_comp_arg = &wal_tx;
			wal_tx.td_biod_data = biod_data;
		}
	}
	biod->bd_completion = wal_completion;
	biod->bd_comp_arg = &wal_tx;

	rc = bio_iod_post_async(biod, 0);
	if (rc)
		D_ERROR("WAL commit failed. "DF_RC"\n", DP_RC(rc));

	/* Wait for WAL commit completion */
	wait_tx_committed(&wal_tx);
out:
	free_data_csum(&dc_arr);
	if (biod != NULL)
		bio_iod_free(biod);
	return rc;
}

static int
load_wal_header(struct bio_meta_context *mc)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_header	*hdr = &si->si_header;
	bio_addr_t		 addr = { 0 };
	d_iov_t			 iov;
	uint32_t		 csum;
	int			 rc, csum_len;

	bio_addr_set(&addr, DAOS_MEDIA_NVME, 0);
	d_iov_set(&iov, hdr, sizeof(*hdr));

	rc = bio_read(mc->mc_wal, addr, &iov);
	if (rc) {
		D_ERROR("Failed to load WAL header. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (hdr->wh_magic != BIO_WAL_MAGIC) {
		D_ERROR("Invalid WAL header. %x\n", hdr->wh_magic);
		return -DER_UNINIT;
	}

	if (hdr->wh_version != BIO_WAL_VERSION) {
		D_ERROR("Invalid WAL version. %u\n", hdr->wh_version);
		return -DER_DF_INCOMPT;
	}

	csum_len = meta_csum_len(mc);
	rc = meta_csum_calc(mc, hdr, sizeof(*hdr) - csum_len, &csum, csum_len);
	if (rc) {
		D_ERROR("Calculate WAL headr csum failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (csum != hdr->wh_csum) {
		D_ERROR("WAL header is corrupted.\n");
		return -DER_CSUM;
	}

	return 0;
}

static int
write_header(struct bio_meta_context *mc, struct bio_io_context *ioc, void *hdr,
	     unsigned int hdr_sz, uint32_t *csum)
{
	bio_addr_t	addr = { 0 };
	d_iov_t		iov;
	int		rc, csum_len;

	csum_len = meta_csum_len(mc);
	rc = meta_csum_calc(mc, hdr, hdr_sz - csum_len, csum, csum_len);
	if (rc) {
		D_ERROR("Calculate headr csum failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	bio_addr_set(&addr, DAOS_MEDIA_NVME, 0);
	d_iov_set(&iov, hdr, hdr_sz);

	rc = bio_write(ioc, addr, &iov);
	if (rc) {
		D_ERROR("Failed to write header. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	return 0;
}

int
bio_wal_flush_header(struct bio_meta_context *mc)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_header	*hdr = &si->si_header;

	/* WAL header is up-to-date */
	if (si->si_ckp_id == hdr->wh_ckp_id && si->si_ckp_blks == hdr->wh_ckp_blks &&
	    si->si_commit_id == hdr->wh_commit_id && si->si_commit_blks == hdr->wh_commit_blks)
		return 0;

	hdr->wh_ckp_id = si->si_ckp_id;
	hdr->wh_ckp_blks = si->si_ckp_blks;
	hdr->wh_commit_id = si->si_commit_id;
	hdr->wh_commit_blks = si->si_commit_blks;

	return write_header(mc, mc->mc_wal, hdr, sizeof(*hdr), &hdr->wh_csum);
}

static int
load_wal(struct bio_meta_context *mc, char *buf, unsigned int max_blks, uint64_t tx_id)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	unsigned int		 tot_blks = si->si_header.wh_tot_blks;
	unsigned int		 blk_bytes = si->si_header.wh_blk_bytes;
	struct bio_sglist	 bsgl;
	struct bio_iov		*biov;
	d_sg_list_t		 sgl;
	d_iov_t			 iov;
	unsigned int		 nr_blks, blks, off;
	bio_addr_t		 addr = { 0 };
	int			 iov_nr, rc;

	d_iov_set(&iov, buf, max_blks * blk_bytes);
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;

	/* Read in 1MB sized IOVs */
	nr_blks = (1UL << 20) / blk_bytes;
	D_ASSERT(nr_blks > 0);
	iov_nr = (max_blks + nr_blks - 1) / nr_blks + 1;
	rc = bio_sgl_init(&bsgl, iov_nr);
	if (rc)
		return rc;

	off = id2off(tx_id);
	while (max_blks > 0) {
		biov = &bsgl.bs_iovs[bsgl.bs_nr_out];

		bio_addr_set(&addr, DAOS_MEDIA_NVME, off2lba(si, off));
		blks = min(max_blks, nr_blks);
		if (off + blks > tot_blks)
			blks = tot_blks - off;
		bio_iov_set(biov, addr, (uint64_t)blks * blk_bytes);

		bsgl.bs_nr_out++;
		max_blks -= blks;
		off += blks;
		if (off == tot_blks)
			off = 0;
		D_ASSERT(bsgl.bs_nr_out <= iov_nr);
	}
	/* Adjust the bs_nr for following bio_readv() */
	bsgl.bs_nr = bsgl.bs_nr_out;

	rc = bio_readv(mc->mc_wal, &bsgl, &sgl);
	bio_sgl_fini(&bsgl);

	return rc;
}

/* Check if a tx_id is known to be committed */
static bool
tx_known_committed(struct wal_super_info *si, uint64_t tx_id)
{
	/* Newly created WAL blob without any committed transactions */
	if (si->si_commit_blks == 0) {
		D_ASSERT(si->si_commit_id == 0);
		return false;
	}

	return (wal_id_cmp(si, tx_id, si->si_commit_id) <= 0);
}

static int
verify_tx_hdr(struct wal_super_info *si, struct wal_trans_head *hdr, uint64_t tx_id)
{
	bool	committed = tx_known_committed(si, tx_id);

	if (hdr->th_magic != WAL_HDR_MAGIC) {
		D_CDEBUG(committed, DLOG_ERR, DB_IO, "Mismatched WAL head magic, %x != %x\n",
			 hdr->th_magic, WAL_HDR_MAGIC);
		return committed ? -DER_INVAL : 1;
	}

	if (hdr->th_gen != si->si_header.wh_gen) {
		D_CDEBUG(committed, DLOG_ERR, DB_IO, "Mismatched WAL generation, %u != %u\n",
			 hdr->th_gen, si->si_header.wh_gen);
		return committed ? -DER_INVAL : 1;
	}

	if (id2seq(hdr->th_id) < id2seq(tx_id)) {
		D_CDEBUG(committed, DLOG_ERR, DB_IO, "Stale sequence number detected, %u < %u\n",
			 id2seq(hdr->th_id), id2seq(tx_id));
		return committed ? -DER_INVAL : 1;
	} else if (id2seq(hdr->th_id) > id2seq(tx_id)) {
		D_ERROR("Invalid sequence number detected, %u > %u\n",
			id2seq(hdr->th_id), id2seq(tx_id));
		return -DER_INVAL;
	}

	if (hdr->th_id != tx_id) {
		D_ERROR("Mismatched transaction ID. "DF_U64" != "DF_U64"\n", hdr->th_id, tx_id);
		return -DER_INVAL;
	}

	if (hdr->th_tot_ents == 0) {
		D_ERROR("Invalid entry number\n");
		return -DER_INVAL;
	}

	return 0;
}

static int
verify_data(struct bio_meta_context *mc, uint64_t off, uint32_t len, uint32_t expected_csum,
	    char **dbuf, unsigned int *dbuf_len)
{
	struct bio_sglist	 bsgl;
	struct bio_iov		*biov;
	d_sg_list_t		 sgl;
	d_iov_t			 iov;
	char			*buf;
	unsigned int		 iov_sz = (1UL << 20);	/* 1MB */
	unsigned int		 read_sz, tot_read = len;
	bio_addr_t		 addr = { 0 };
	uint32_t		 csum;
	int			 iov_nr, csum_len, rc;

	D_ASSERT(len > 0);
	if (*dbuf_len < len) {
		D_FREE(*dbuf);
		*dbuf = NULL;
		*dbuf_len = 0;

		D_ALLOC(buf, max(iov_sz, len));
		if (buf == NULL)
			return -DER_NOMEM;
		*dbuf = buf;
		*dbuf_len = max(iov_sz, len);
	} else {
		buf = *dbuf;
	}

	d_iov_set(&iov, buf, len);
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;

	/* Read in 1MB sized IOVs */
	iov_nr = (len + iov_sz - 1) / iov_sz;
	rc = bio_sgl_init(&bsgl, iov_nr);
	if (rc)
		return rc;

	while (tot_read > 0) {
		biov = &bsgl.bs_iovs[bsgl.bs_nr_out];

		bio_addr_set(&addr, DAOS_MEDIA_NVME, off);
		read_sz = min(tot_read, iov_sz);
		bio_iov_set(biov, addr, read_sz);

		bsgl.bs_nr_out++;
		tot_read -= read_sz;
		off += read_sz;
		D_ASSERT(bsgl.bs_nr_out <= iov_nr);
	}

	rc = bio_readv(mc->mc_data, &bsgl, &sgl);
	bio_sgl_fini(&bsgl);
	if (rc) {
		D_ERROR("Read data from data blob failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	csum_len = meta_csum_len(mc);
	rc = meta_csum_calc(mc, buf, len, &csum, csum_len);
	if (rc) {
		D_ERROR("Calculate data csum failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (csum != expected_csum) {
		D_DEBUG(DB_IO, "Mismatched data csum, %u != %u\n", csum, expected_csum);
		return 1;
	}

	return 0;
}

static inline void
init_entry_blk(struct wal_trans_blk *entry_blk, struct wal_trans_head *hdr, char *buf,
	       unsigned int blk_sz)
{
	D_ASSERT(hdr->th_tot_ents > 0);
	entry_blk->tb_hdr = hdr;
	entry_blk->tb_buf = buf;
	entry_blk->tb_idx = 0;
	entry_blk->tb_off = sizeof(*hdr);
	entry_blk->tb_blk_sz = blk_sz;
}

static inline void
next_wal_blk(struct wal_trans_blk *tb)
{
	tb->tb_idx++;
	tb->tb_buf += tb->tb_blk_sz;
	tb->tb_off = sizeof(*tb->tb_hdr);
}

static inline void
entry_move_next(struct wal_trans_blk *entry_blk, struct wal_blks_desc *bd)
{
	unsigned int	entry_sz = sizeof(struct wal_trans_entry);

	entry_blk->tb_off += entry_sz;
	if ((entry_blk->tb_off + entry_sz) > entry_blk->tb_blk_sz)
		next_wal_blk(entry_blk);

	if (entry_blk->tb_idx < bd->bd_payload_idx)
		D_ASSERT((entry_blk->tb_off + entry_sz) <= entry_blk->tb_blk_sz);
	else if (entry_blk->tb_idx == bd->bd_payload_idx)
		D_ASSERT((entry_blk->tb_off + entry_sz) <= bd->bd_payload_off);
	else
		D_ASSERTF(0, "Entry blk idx:%u > Payload blk idx:%u\n",
			  entry_blk->tb_idx, bd->bd_payload_idx);
}

static int
verify_tx_data(struct bio_meta_context *mc, char *buf, struct wal_blks_desc *bd,
	       char **dbuf, unsigned int *dbuf_len)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_trans_head	*hdr = (struct wal_trans_head *)buf;
	struct wal_trans_entry	*entry;
	unsigned int		 blk_sz = si->si_header.wh_blk_bytes, nr = 0;
	struct wal_trans_blk	 entry_blk;
	int			 rc = 0;

	init_entry_blk(&entry_blk, hdr, buf, blk_sz);
	while (1) {
		entry = (struct wal_trans_entry *)(entry_blk.tb_buf + entry_blk.tb_off);

		switch (entry->te_type) {
		case UMEM_ACT_COPY:
		case UMEM_ACT_COPY_PTR:
		case UMEM_ACT_ASSIGN:
		case UMEM_ACT_MOVE:
		case UMEM_ACT_SET:
		case UMEM_ACT_SET_BITS:
		case UMEM_ACT_CLR_BITS:
			break;
		case UMEM_ACT_CSUM:
			rc = verify_data(mc, entry->te_off, entry->te_len, entry->te_data,
					 dbuf, dbuf_len);
			break;
		default:
			D_ASSERTF(0, "Invalid opc %u\n", entry->te_type);
			break;
		}

		nr++;
		if (rc != 0 || nr == hdr->th_tot_ents)
			break;

		entry_move_next(&entry_blk, bd);
	}

	return rc;
}

/* When tail csum is disableld, verify tx header for each block */
static int
verify_tx_blks(struct wal_super_info *si, char *buf, struct wal_blks_desc *blk_desc)
{
	struct wal_trans_head	*hdr = (struct wal_trans_head *)buf;
	uint64_t		 tx_id = hdr->th_id;
	unsigned int		 blk_sz = si->si_header.wh_blk_bytes;
	struct wal_trans_blk	 entry_blk;
	int			 rc = 0;

	init_entry_blk(&entry_blk, hdr, buf, blk_sz);
	/* Header of the first block has been verified, start from the second one */
	while ((entry_blk.tb_idx + 1) < blk_desc->bd_blks) {
		next_wal_blk(&entry_blk);
		hdr = (struct wal_trans_head *)entry_blk.tb_buf;
		rc = verify_tx_hdr(si, hdr, tx_id);
		if (rc) {
			D_CDEBUG(rc > 0, DB_IO, DLOG_ERR, "Verify TX block %u/%u failed.\n",
				 entry_blk.tb_idx, blk_desc->bd_blks);
			break;
		}
	}

	return rc;
}

static int
verify_tx(struct bio_meta_context *mc, char *buf, struct wal_blks_desc *blk_desc,
	  char **dbuf, unsigned int *dbuf_len)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_trans_head	*hdr = (struct wal_trans_head *)buf;
	unsigned int		 blk_bytes = si->si_header.wh_blk_bytes;
	uint32_t		 csum, expected_csum;
	unsigned int		 csum_len, buf_len;
	bool			 committed = tx_known_committed(si, hdr->th_id);
	int			 rc;

	if (skip_wal_tx_tail(&mc->mc_wal_info)) {
		rc = verify_tx_blks(si, buf, blk_desc);
		if (rc)
			return rc;
		goto verify_data;
	}

	csum_len = meta_csum_len(mc);
	/* Total tx length excluding tail */
	D_ASSERT(blk_desc->bd_blks > 0);
	buf_len = (blk_desc->bd_blks - 1) * blk_bytes + blk_desc->bd_tail_off;

	rc = meta_csum_calc(mc, buf, buf_len, &csum, csum_len);
	if (rc) {
		D_ERROR("Calculate WAL tx csum failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	memcpy(&expected_csum, buf + buf_len, csum_len);
	if (csum != expected_csum) {
		D_CDEBUG(committed, DLOG_ERR, DB_IO, "Mismatched tx csum, %u != %u\n",
			 csum, expected_csum);
		return committed ? -DER_INVAL : 1;
	}

verify_data:
	/*
	 * Don't verify data csum when the transaction ID is known to be committed.
	 *
	 * VOS aggregation is responsible to bump the persistent last committed ID before
	 * each round of aggregation, so that here we can avoid verifying the data which
	 * might have been changed by aggregation.
	 */
	if (committed)
		return 0;

	return verify_tx_data(mc, buf, blk_desc, dbuf, dbuf_len);
}

static void
copy_payload(struct wal_blks_desc *bd, struct wal_trans_blk *tb, void *addr, uint32_t len)
{
	unsigned int	left, copy_sz;

	D_ASSERT(len > 0);
	while (len > 0) {
		D_ASSERT(tb->tb_idx >= bd->bd_payload_idx && tb->tb_idx < bd->bd_blks);
		D_ASSERT(tb->tb_off >= sizeof(*tb->tb_hdr) && tb->tb_off <= tb->tb_blk_sz);

		left = tb->tb_blk_sz - tb->tb_off;
		/* Current payload block is done, move to next */
		if (left == 0) {
			next_wal_blk(tb);
			continue;
		}

		copy_sz = (left >= len) ? len : left;
		memcpy(addr, tb->tb_buf + tb->tb_off, copy_sz);

		tb->tb_off += copy_sz;
		addr += copy_sz;
		len -= copy_sz;
	}
}

static int
replay_tx(struct wal_super_info *si, char *buf,
	  int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *arg),
	  void *arg, struct wal_blks_desc *bd, struct umem_action *act)
{
	struct wal_trans_head	*hdr = (struct wal_trans_head *)buf;
	struct wal_trans_entry	*entry;
	struct wal_trans_blk	 entry_blk, payload_blk;
	unsigned int		 blk_sz = si->si_header.wh_blk_bytes;
	int			 nr = 0, rc = 0;

	/* Init entry block */
	init_entry_blk(&entry_blk, hdr, buf, blk_sz);

	/* Init payload block */
	payload_blk.tb_hdr = hdr;
	payload_blk.tb_buf = buf + bd->bd_payload_idx * blk_sz;
	payload_blk.tb_idx = bd->bd_payload_idx;
	payload_blk.tb_off = bd->bd_payload_off;
	payload_blk.tb_blk_sz = blk_sz;

	while (1) {
		entry = (struct wal_trans_entry *)(entry_blk.tb_buf + entry_blk.tb_off);

		act->ac_opc = entry->te_type;
		switch (entry->te_type) {
		case UMEM_ACT_COPY:
		case UMEM_ACT_COPY_PTR:
			act->ac_opc = UMEM_ACT_COPY;
			act->ac_copy.addr = entry->te_off;
			act->ac_copy.size = entry->te_len;
			D_ASSERT(entry->te_len <= UMEM_ACT_PAYLOAD_MAX_LEN);
			copy_payload(bd, &payload_blk, &act->ac_copy.payload, entry->te_len);
			break;
		case UMEM_ACT_ASSIGN:
			act->ac_assign.addr = entry->te_off;
			act->ac_assign.size = entry->te_len;
			act->ac_assign.val = entry->te_data;
			break;
		case UMEM_ACT_MOVE:
			act->ac_move.dst = entry->te_off;
			act->ac_move.size = entry->te_len;
			copy_payload(bd, &payload_blk, &act->ac_move.src, sizeof(uint64_t));
			break;
		case UMEM_ACT_SET:
			act->ac_set.addr = entry->te_off;
			act->ac_set.size = entry->te_len;
			act->ac_set.val = entry->te_data;
			break;
		case UMEM_ACT_SET_BITS:
		case UMEM_ACT_CLR_BITS:
			act->ac_op_bits.addr = entry->te_off;
			act->ac_op_bits.num = entry->te_len;
			act->ac_op_bits.pos = entry->te_data;
			break;
		case UMEM_ACT_CSUM:
			break;
		default:
			D_ASSERTF(0, "Invalid opc %u\n", act->ac_opc);
			break;
		}

		if (act->ac_opc != UMEM_ACT_CSUM) {
			rc = replay_cb(hdr->th_id, act, arg);
			if (rc)
				D_ERROR("Replay CB on action %u failed. "DF_RC"\n",
					act->ac_opc, DP_RC(rc));
		}

		nr++;
		if (rc != 0 || nr == hdr->th_tot_ents)
			break;

		entry_move_next(&entry_blk, bd);
	}

	return rc;
}

static inline uint64_t
off2lba_blk(uint64_t off)
{
	return off + WAL_HDR_BLKS;
}

static int
unmap_wal(struct bio_meta_context *mc, uint64_t unmap_start, uint64_t unmap_end,
	  uint64_t *purged_blks)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	unsigned int		 blk_sz = si->si_header.wh_blk_bytes;
	unsigned int		 tot_blks = si->si_header.wh_tot_blks;
	uint32_t		 tot_purged;
	d_sg_list_t		 unmap_sgl;
	d_iov_t			*unmap_iov;
	int			 rc;

	rc = d_sgl_init(&unmap_sgl, 2);
	if (rc) {
		D_ERROR("Failed to init unmap SGL. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	unmap_sgl.sg_nr_out = 1;
	unmap_iov = &unmap_sgl.sg_iovs[0];

	if (unmap_end == unmap_start) {
		unmap_iov->iov_buf = (void *)off2lba_blk(0);
		unmap_iov->iov_len = tot_blks;
		tot_purged = unmap_iov->iov_len;
	} else if (unmap_end > unmap_start) {
		unmap_iov->iov_buf = (void *)off2lba_blk(unmap_start);
		unmap_iov->iov_len = unmap_end - unmap_start;
		tot_purged = unmap_iov->iov_len;
	} else {
		unmap_iov->iov_buf = (void *)off2lba_blk(unmap_start);
		unmap_iov->iov_len = tot_blks - unmap_start;
		tot_purged = unmap_iov->iov_len;

		if (unmap_end > 0) {
			unmap_sgl.sg_nr_out = 2;
			unmap_iov = &unmap_sgl.sg_iovs[1];

			unmap_iov->iov_buf = (void *)off2lba_blk(0);
			unmap_iov->iov_len = unmap_end;
			tot_purged += unmap_iov->iov_len;
		}
	}

	rc = bio_blob_unmap_sgl(mc->mc_wal, &unmap_sgl, blk_sz);
	d_sgl_fini(&unmap_sgl, false);
	if (rc) {
		D_ERROR("Unmap WAL failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (purged_blks)
		*purged_blks = tot_purged;

	return 0;
}

int
bio_wal_replay(struct bio_meta_context *mc, struct bio_wal_rp_stats *wrs,
	       int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *arg),
	       void *arg)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_trans_head	*hdr;
	unsigned int		 blk_bytes = si->si_header.wh_blk_bytes;
	struct wal_blks_desc	 blk_desc = { 0 };
	char			*buf, *dbuf = NULL;
	struct umem_action	*act;
	unsigned int		 max_blks = WAL_MAX_TRANS_BLKS, blk_off;
	unsigned int		 nr_replayed = 0, tight_loop, dbuf_len = 0;
	uint64_t		 tx_id, start_id, unmap_start, unmap_end;
	int			 rc;
	uint64_t		 total_bytes = 0, rpl_entries = 0, total_tx = 0;
	uint64_t                 s_us = 0;

	D_ALLOC(buf, max_blks * blk_bytes);
	if (buf == NULL)
		return -DER_NOMEM;

	D_ALLOC(act, sizeof(*act) + UMEM_ACT_PAYLOAD_MAX_LEN);
	if (act == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	tx_id = wal_next_id(si, si->si_ckp_id, si->si_ckp_blks);
	start_id = tx_id;

	/* upper layer (VOS) rehydration metrics if any */
	if (wrs != NULL)
		s_us = daos_getutime();

load_wal:
	tight_loop = 0;
	blk_off = 0;
	rc = load_wal(mc, buf, max_blks, tx_id);
	if (rc) {
		D_ERROR("Failed to load WAL. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	while (1) {
		/* Something went wrong, it's impossible to replay the whole WAL */
		if (id2seq(tx_id) != id2seq(start_id) && id2off(tx_id) >= id2off(start_id)) {
			D_ERROR("Whole WAL replayed. "DF_U64"/"DF_U64"\n", start_id, tx_id);
			rc = -DER_INVAL;
			break;
		}

		hdr = (struct wal_trans_head *)(buf + blk_off * blk_bytes);
		rc = verify_tx_hdr(si, hdr, tx_id);
		if (rc)
			break;

		calc_trans_blks(hdr->th_tot_ents, hdr->th_tot_payload, blk_bytes, &blk_desc);

		if (blk_off + blk_desc.bd_blks > max_blks) {
			if (blk_off == 0) {
				D_ERROR("Too large tx, the WAL is corrupted\n");
				rc = -DER_INVAL;
				break;
			}
			memset(buf, 0, max_blks * blk_bytes);
			goto load_wal;
		}

		rc = verify_tx(mc, (char *)hdr, &blk_desc, &dbuf, &dbuf_len);
		if (rc)
			break;

		rc = replay_tx(si, (char *)hdr, replay_cb, arg, &blk_desc, act);
		if (rc)
			break;

		tight_loop++;
		nr_replayed++;
		blk_off += blk_desc.bd_blks;

		/* replay metrics */
		if (wrs != NULL) {
			total_bytes += (blk_desc.bd_blks - 1) * blk_bytes + blk_desc.bd_tail_off;
			rpl_entries += hdr->th_tot_ents;
			total_tx++;
		}

		/* Bump last committed tx ID in WAL super info */
		if (wal_id_cmp(si, tx_id, si->si_commit_id) > 0) {
			si->si_commit_id = tx_id;
			si->si_commit_blks = blk_desc.bd_blks;
		}
		tx_id = wal_next_id(si, tx_id, blk_desc.bd_blks);

		if (blk_off == max_blks) {
			memset(buf, 0, max_blks * blk_bytes);
			goto load_wal;
		}

		if (tight_loop >= 20) {
			tight_loop = 0;
			bio_yield(NULL);
		}
	}
out:
	if (rc >= 0) {
		D_DEBUG(DB_IO, "Replayed %u WAL transactions\n", nr_replayed);
		D_ASSERT(si->si_commit_blks == 0 || wal_id_cmp(si, tx_id, si->si_commit_id) > 0);
		si->si_unused_id = wal_next_id(si, si->si_commit_id, si->si_commit_blks);

		unmap_start = id2off(si->si_unused_id);
		unmap_end = id2off(start_id);
		/*
		 * Unmap the unused region to erase stale tx entries, otherwise, stale tx could
		 * be mistakenly replayed on next restart in following scenario:
		 *
		 * 1. Imagine two in-flight transactions T1 and T2, T1 is submitted before T2;
		 * 2. Before T1 is written to WAL, T2 is written successfully, both transactions
		 *    are still regarded as incompleted since the preceding T1 is not persistent;
		 * 3. Sever restart;
		 * 4. WAL replay hit the hole generated by unfinished T1 and stop relaying as
		 *    expected, both T1 & T2 are not replayed;
		 * 5. A new transaction T3 committed, T3 happen to has same WAL size as T1, so it
		 *    filled the hole perfectly;
		 * 6. Server restart again;
		 * 7. Both T3 & T2 are replayed since there is no way to tell that T2 is stale;
		 *
		 * This unmap solves the issue for any device with unmap properly implemented,
		 * but it won't be helpful for AIO device which doesn't support unmap. Given that
		 * AIO device is only used for unit testing, and zeroing the unused region would
		 * be too heavy, we choose to leave this risk for AIO device.
		 */
		rc = unmap_wal(mc, unmap_start, unmap_end, NULL);
		if (rc)
			D_ERROR("Unmap after replay failed. "DF_RC"\n", DP_RC(rc));

		/* upper layer (VOS) rehydration metrics */
		if (wrs != NULL) {
			wrs->wrs_tm = daos_getutime() - s_us;
			wrs->wrs_sz = total_bytes;
			wrs->wrs_entries = rpl_entries;
			wrs->wrs_tx_cnt = total_tx;
		}
	} else {
		D_ERROR("WAL replay failed, "DF_RC"\n", DP_RC(rc));
	}

	D_FREE(dbuf);
	D_FREE(act);
	D_FREE(buf);
	return rc;
}

int
bio_wal_checkpoint(struct bio_meta_context *mc, uint64_t tx_id, uint64_t *purged_blks)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_trans_head	*hdr;
	struct wal_blks_desc	 blk_desc = { 0 };
	char			*buf;
	unsigned int		 blk_sz = si->si_header.wh_blk_bytes;
	uint64_t		 unmap_start, unmap_end;
	int			 rc;

	D_ASSERT(wal_id_cmp(si, si->si_ckp_id, tx_id) < 0);
	D_ASSERT(wal_id_cmp(si, tx_id, si->si_commit_id) <= 0);

	D_ALLOC(buf, blk_sz);
	if (buf == NULL)
		return -DER_NOMEM;

	/* Load single WAL block to get the block nr used by the transaction */
	rc = load_wal(mc, buf, 1, tx_id);
	if (rc) {
		D_ERROR("Failed to load WAL. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	hdr = (struct wal_trans_head *)buf;
	rc = verify_tx_hdr(si, hdr, tx_id);
	if (rc) {
		D_ERROR("Corrupted WAL transaction head\n");
		goto out;
	}

	calc_trans_blks(hdr->th_tot_ents, hdr->th_tot_payload, blk_sz, &blk_desc);

	unmap_start = id2off(wal_next_id(si, si->si_ckp_id, si->si_ckp_blks));
	unmap_end = id2off(wal_next_id(si, tx_id, blk_desc.bd_blks));

	/* Unmap the checkpointed regions */
	rc = unmap_wal(mc, unmap_start, unmap_end, purged_blks);
	if (rc)	/* Flush the WAL header anyway */
		D_ERROR("Unmap checkpointed region failed. "DF_RC"\n", DP_RC(rc));

	si->si_ckp_id = tx_id;
	si->si_ckp_blks = blk_desc.bd_blks;
	wakeup_reserve_waiters(si, false);

	/* Flush the WAL header */
	rc = bio_wal_flush_header(mc);
	if (rc)
		D_ERROR("Flush WAL header failed. "DF_RC"\n", DP_RC(rc));
out:
	D_FREE(buf);
	return rc;
}

void
bio_meta_get_attr(struct bio_meta_context *mc, uint64_t *capacity, uint32_t *blk_sz,
		  uint32_t *hdr_blks)
{
	/* The mc could be NULL when md on SSD not enabled & data blob not existing */
	if (mc != NULL) {
		*blk_sz = mc->mc_meta_hdr.mh_blk_bytes;
		*capacity = mc->mc_meta_hdr.mh_tot_blks * (*blk_sz);
		*hdr_blks = mc->mc_meta_hdr.mh_hdr_blks;
	}
}

void
wal_close(struct bio_meta_context *mc)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	int			 rc;

	D_ASSERT(d_list_empty(&si->si_pending_list));
	D_ASSERT(si->si_tx_failed == 0);
	if (si->si_rsrv_waiters > 0)
		wakeup_reserve_waiters(si, true);

	/* Simulate a server crash before in-flight WAL commit completed */
	if (DAOS_FAIL_CHECK(DAOS_NVME_WAL_TX_LOST)) {
		D_ERROR("Injected WAL tx lost, reset committed ID to zero.\n");
		si->si_commit_id = 0;
		si->si_commit_blks = 0;
	}

	rc = bio_wal_flush_header(mc);
	if (rc)
		D_ERROR("Flush WAL header failed. "DF_RC"\n", DP_RC(rc));

	ABT_mutex_free(&si->si_mutex);
	ABT_cond_free(&si->si_rsrv_wq);
}

int
wal_open(struct bio_meta_context *mc)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_header	*hdr = &si->si_header;
	int			 rc;

	rc = load_wal_header(mc);
	if (rc)
		return rc;

	rc = ABT_mutex_create(&si->si_mutex);
	if (rc != ABT_SUCCESS)
		return -DER_NOMEM;

	rc = ABT_cond_create(&si->si_rsrv_wq);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&si->si_mutex);
		return -DER_NOMEM;
	}

	D_INIT_LIST_HEAD(&si->si_pending_list);
	si->si_rsrv_waiters = 0;
	si->si_tx_failed = 0;

	si->si_ckp_id = hdr->wh_ckp_id;
	si->si_ckp_blks = hdr->wh_ckp_blks;
	si->si_commit_id = hdr->wh_commit_id;
	si->si_commit_blks = hdr->wh_commit_blks;

	D_ASSERTF(wal_id_cmp(si, si->si_ckp_id, si->si_commit_id) <= 0,
		  "Checkpoint ID "DF_U64" > Committed ID "DF_U64"\n",
		  si->si_ckp_id, si->si_commit_id);

	si->si_unused_id = wal_next_id(si, si->si_commit_id, si->si_commit_blks);

	return 0;

}

static int
load_meta_header(struct bio_meta_context *mc)
{
	struct meta_header	*hdr = &mc->mc_meta_hdr;
	bio_addr_t		 addr = { 0 };
	d_iov_t			 iov;
	uint32_t		 csum;
	int			 rc, csum_len;

	bio_addr_set(&addr, DAOS_MEDIA_NVME, 0);
	d_iov_set(&iov, hdr, sizeof(*hdr));

	rc = bio_read(mc->mc_meta, addr, &iov);
	if (rc) {
		D_ERROR("Failed to load meta header. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (hdr->mh_magic != BIO_META_MAGIC) {
		D_ERROR("Invalid meta header. %x\n", hdr->mh_magic);
		return -DER_UNINIT;
	}

	if (hdr->mh_version != BIO_META_VERSION) {
		D_ERROR("Invalid meta version. %u\n", hdr->mh_version);
		return -DER_DF_INCOMPT;
	}

	csum_len = meta_csum_len(mc);
	rc = meta_csum_calc(mc, hdr, sizeof(*hdr) - csum_len, &csum, csum_len);
	if (rc) {
		D_ERROR("Calculate meta headr csum failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (csum != hdr->mh_csum) {
		D_ERROR("Meta header is corrupted.\n");
		return -DER_CSUM;
	}

	return 0;
}

void
meta_close(struct bio_meta_context *mc)
{
	meta_csum_fini(mc);
}

int
meta_open(struct bio_meta_context *mc)
{
	int	rc;

	rc = meta_csum_init(mc, HASH_TYPE_CRC32);
	if (rc)
		return rc;

	rc = load_meta_header(mc);
	if (rc)
		meta_csum_fini(mc);
	return rc;
}

/*
 * Try to generate an unique generation for WAL blob, the generation will be used
 * to distinguish the stale TX blocks from destroyed pools.
 *
 * Note: It's only useful for AIO device which doesn't support unmap, if the blob
 * is on NVMe SSD, the old data will be cleared by unmap on pool destroy.
 */
static inline uint32_t
get_wal_gen(uuid_t pool_id, uint32_t tgt_id)
{
	uint64_t	pool = d_hash_murmur64(pool_id, sizeof(uuid_t), 5371);
	uint32_t	ts = (uint32_t)daos_wallclock_secs();

	if (tgt_id != BIO_STANDALONE_TGT_ID)
		return (pool >> 32) ^ (pool & UINT32_MAX) ^ ts ^ tgt_id;

	return (pool >> 32) ^ (pool & UINT32_MAX) ^ ts;
}

int
meta_format(struct bio_meta_context *mc, struct meta_fmt_info *fi, bool force)
{
	struct meta_header	*meta_hdr = &mc->mc_meta_hdr;
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct wal_header	*wal_hdr = &si->si_header;
	int			 rc;

	if (fi->fi_meta_size < WAL_MIN_CAPACITY) {
		D_ERROR("Meta size "DF_U64" is too small\n", fi->fi_meta_size);
		return -DER_INVAL;
	}

	if (fi->fi_wal_size < WAL_MIN_CAPACITY) {
		D_ERROR("WAL size "DF_U64" is too small\n", fi->fi_wal_size);
		return -DER_INVAL;
	} else if (fi->fi_wal_size > ((uint64_t)WAL_BLK_SZ * UINT32_MAX)) {
		D_ERROR("WAL size "DF_U64" is too large\n", fi->fi_wal_size);
		return -DER_INVAL;
	}

	rc = meta_csum_init(mc, HASH_TYPE_CRC32);
	if (rc)
		return rc;

	if (!force) {
		rc = load_meta_header(mc);
		if (rc != -DER_UNINIT) {
			D_ERROR("Meta blob is already formatted!\n");
			rc = -DER_ALREADY;
			goto out;
		}
	}

	memset(meta_hdr, 0, sizeof(*meta_hdr));
	meta_hdr->mh_magic = BIO_META_MAGIC;
	meta_hdr->mh_version = BIO_META_VERSION;
	uuid_copy(meta_hdr->mh_meta_devid, fi->fi_meta_devid);
	uuid_copy(meta_hdr->mh_wal_devid, fi->fi_wal_devid);
	uuid_copy(meta_hdr->mh_data_devid, fi->fi_data_devid);
	meta_hdr->mh_meta_blobid = fi->fi_meta_blobid;
	meta_hdr->mh_wal_blobid = fi->fi_wal_blobid;
	meta_hdr->mh_data_blobid = fi->fi_data_blobid;
	meta_hdr->mh_blk_bytes = META_BLK_SZ;
	meta_hdr->mh_hdr_blks = META_HDR_BLKS;
	meta_hdr->mh_tot_blks = (fi->fi_meta_size / META_BLK_SZ) - META_HDR_BLKS;
	meta_hdr->mh_vos_id = fi->fi_vos_id;
	meta_hdr->mh_flags = META_HDR_FL_EMPTY;

	rc = write_header(mc, mc->mc_meta, meta_hdr, sizeof(*meta_hdr), &meta_hdr->mh_csum);
	if (rc) {
		D_ERROR("Write meta header failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	memset(wal_hdr, 0, sizeof(*wal_hdr));
	wal_hdr->wh_magic = BIO_WAL_MAGIC;
	wal_hdr->wh_version = BIO_WAL_VERSION;
	wal_hdr->wh_gen = get_wal_gen(fi->fi_pool_id, fi->fi_vos_id);
	wal_hdr->wh_blk_bytes = WAL_BLK_SZ;
	wal_hdr->wh_flags = 0;	/* Don't skip csum tail by default */
	wal_hdr->wh_tot_blks = (fi->fi_wal_size / WAL_BLK_SZ) - WAL_HDR_BLKS;

	rc = write_header(mc, mc->mc_wal, wal_hdr, sizeof(*wal_hdr), &wal_hdr->wh_csum);
	if (rc) {
		D_ERROR("Write WAL header failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}
out:
	meta_csum_fini(mc);
	return rc;
}

void
bio_wal_query(struct bio_meta_context *mc, struct bio_wal_info *info)
{
	struct wal_super_info	*si = &mc->mc_wal_info;

	info->wi_tot_blks = si->si_header.wh_tot_blks;
	info->wi_used_blks = wal_used_blks(si);
	info->wi_ckp_id = si->si_ckp_id;
	info->wi_commit_id = si->si_commit_id;
	info->wi_unused_id = si->si_unused_id;
}

bool
bio_meta_is_empty(struct bio_meta_context *mc)
{
	struct meta_header	*hdr = &mc->mc_meta_hdr;

	return hdr->mh_flags & META_HDR_FL_EMPTY;
}

int
bio_meta_clear_empty(struct bio_meta_context *mc)
{
	struct meta_header	*hdr = &mc->mc_meta_hdr;
	int			 rc;

	if (!bio_meta_is_empty(mc))
		return 0;

	hdr->mh_flags &= ~META_HDR_FL_EMPTY;
	rc = write_header(mc, mc->mc_meta, hdr, sizeof(*hdr), &hdr->mh_csum);
	if (rc) {
		hdr->mh_flags |= META_HDR_FL_EMPTY;
		D_ERROR("Write meta header failed. "DF_RC"\n", DP_RC(rc));
	}

	return rc;
}
