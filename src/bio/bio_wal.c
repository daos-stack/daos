/**
 * (C) Copyright 2018-2022 Intel Corporation.
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

/* Caller must guarantee no yield between bio_wal_reserve() and bio_wal_submit() */
int
bio_wal_reserve(struct bio_meta_context *mc, uint64_t *tx_id)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	int			 rc = 0;

	if (!si->si_rsrv_waiters && reserve_allowed(si))
		goto done;

	si->si_rsrv_waiters++;

	ABT_mutex_lock(si->si_mutex);
	ABT_cond_wait(si->si_rsrv_wq, si->si_mutex);
	ABT_mutex_unlock(si->si_mutex);

	D_ASSERT(si->si_rsrv_waiters > 0);
	si->si_rsrv_waiters--;

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
	if (left_bytes > 0) {
		bd->bd_payload_idx = entry_blks - 1;
		bd->bd_payload_off = blk_sz - left_bytes;
	} else {
		bd->bd_payload_idx = entry_blks;
		bd->bd_payload_off = sizeof(struct wal_trans_head);
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
	if (left_bytes >= sizeof(struct wal_trans_tail)) {
		bd->bd_blks = entry_blks + payload_blks;
		bd->bd_tail_off = blk_sz - left_bytes;
		return;
	}

	bd->bd_blks = entry_blks + payload_blks + 1;
	bd->bd_tail_off = sizeof(struct wal_trans_head);
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
	unsigned int	 iov_blks;

	D_ASSERT(bsgl->bs_nr_out == 1 || bsgl->bs_nr_out == 2);
	biov = &bsgl->bs_iovs[0];
	iov_blks = (bio_iov2len(biov) + blk_sz - 1) / blk_sz;

	if (idx >= iov_blks) {
		D_ASSERT(bsgl->bs_nr_out == 2);

		idx -= iov_blks;
		biov = &bsgl->bs_iovs[1];
		iov_blks = (bio_iov2len(biov) + blk_sz - 1) / blk_sz;
		D_ASSERT(idx < iov_blks);
	}

	tb->tb_buf = biov->bi_buf + (idx * blk_sz);
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
	      uint64_t addr, uint16_t len)
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

static void
fill_trans_blks(struct bio_meta_context *mc, struct bio_sglist *bsgl, struct umem_tx *tx,
		unsigned int blk_sz, struct wal_blks_desc *bd)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct umem_action	*act;
	struct wal_trans_head	 blk_hdr;
	struct wal_trans_entry	 entry;
	struct wal_trans_blk	 entry_blk, payload_blk;
	unsigned int		 left, entry_sz = sizeof(struct wal_trans_entry);
	uint64_t		 src_addr;

	blk_hdr.th_magic = WAL_HDR_MAGIC;
	blk_hdr.th_gen = si->si_header.wh_gen;
	blk_hdr.th_id = tx->utx_id;
	blk_hdr.th_tot_ents = umem_tx_act_nr(mc->mc_meta->bic_umem, tx);
	blk_hdr.th_tot_payload = umem_tx_act_payload_sz(mc->mc_meta->bic_umem, tx);

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

	act = umem_tx_act_first(mc->mc_meta->bic_umem, tx);
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
			place_payload(bsgl, bd, &payload_blk, act->ac_move.src, sizeof(uint64_t));
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
			entry.te_len = WAL_CSUM_LEN;
			entry.te_data = act->ac_csum.csum;
			break;
		default:
			D_ASSERTF(0, "Invalid opc %u\n", act->ac_opc);
			break;
		}

		act = umem_tx_act_next(mc->mc_meta->bic_umem, tx);
	}

	place_tail(mc, bsgl, bd, &payload_blk);
}

static inline uint64_t
off2lba(struct wal_super_info *si, unsigned int blk_off)
{
	return (blk_off + WAL_HDR_BLKS) * si->si_header.wh_blk_bytes;
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
			wakeup_reserve_waiters(si, false);
		}
	} else {
		D_ASSERT(wal_next_id(si, si->si_commit_id, si->si_commit_blks) == wal_tx->td_id);
		D_ASSERT(si->si_tx_failed == 0);

		si->si_commit_id = wal_tx->td_id;
		si->si_commit_blks = wal_tx->td_blks;
	}

	d_list_del_init(&wal_tx->td_link);
	/* The ABT_eventual could be NULL if WAL I/O IOD failed on DMA mapping in bio_iod_prep() */
	if (biod_tx->bd_dma_done != ABT_EVENTUAL_NULL)
		ABT_eventual_set(biod_tx->bd_dma_done, NULL, 0);

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

int
bio_wal_commit(struct bio_meta_context *mc, struct umem_tx *tx, struct bio_desc *biod_data)
{
	struct wal_super_info	*si = &mc->mc_wal_info;
	struct bio_desc		*biod;
	struct bio_sglist	*bsgl;
	bio_addr_t		 addr = { 0 };
	struct wal_tx_desc	 wal_tx = { 0 };
	struct wal_blks_desc	 blk_desc = { 0 };
	unsigned int		 blks, unused_off;
	unsigned int		 tot_blks = si->si_header.wh_tot_blks;
	unsigned int		 blk_bytes = si->si_header.wh_blk_bytes;
	uint64_t		 tx_id = tx->utx_id;
	int			 iov_nr, rc;

	/* Calculate the required log blocks for this transaction */
	calc_trans_blks(umem_tx_act_nr(mc->mc_meta->bic_umem, tx),
			umem_tx_act_payload_sz(mc->mc_meta->bic_umem, tx),
			blk_bytes, &blk_desc);

	if (blk_desc.bd_blks > WAL_MAX_TRANS_BLKS) {
		D_ERROR("Too large transaction (%u blocks)\n", blk_desc.bd_blks);
		return -DER_INVAL;
	}

	biod = bio_iod_alloc(mc->mc_wal, 1, BIO_IOD_TYPE_UPDATE);
	if (biod == NULL)
		return -DER_NOMEM;

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
	bio_iov_set(&bsgl->bs_iovs[0], addr, blks * blk_bytes);
	if (iov_nr == 2) {
		bio_addr_set(&addr, DAOS_MEDIA_NVME, off2lba(si, 0));
		blks = blk_desc.bd_blks - blks;
		bio_iov_set(&bsgl->bs_iovs[1], addr, blks * blk_bytes);
	}
	bsgl->bs_nr_out = iov_nr;

	wal_tx.td_id = si->si_unused_id;
	wal_tx.td_si = si;
	wal_tx.td_biod_tx = biod;
	wal_tx.td_biod_data = NULL;
	wal_tx.td_blks = blk_desc.bd_blks;
	/* Track in pending list from now on, since it could yield in bio_iod_prep() */
	d_list_add_tail(&wal_tx.td_link, &si->si_pending_list);

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
	fill_trans_blks(mc, bsgl, tx, blk_bytes, &blk_desc);

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
	D_ASSERT(biod->bd_dma_done != ABT_EVENTUAL_NULL);
	ABT_eventual_wait(biod->bd_dma_done, NULL);
	/* The completion must have been called */
	D_ASSERT(d_list_empty(&wal_tx.td_link));
out:
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

static int
flush_wal_header(struct bio_meta_context *mc)
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

int
bio_wal_replay(struct bio_meta_context *mc,
	       int (*replay_cb)(struct umem_action *actv, unsigned int act_nr),
	       unsigned int max_replay_nr)
{
	/* TODO */
	return 0;
}

int
bio_wal_ckp_start(struct bio_meta_context *mc, uint64_t *tx_id)
{
	/* TODO */
	return 0;
}

int
bio_wal_ckp_end(struct bio_meta_context *mc, uint64_t tx_id)
{
	/* TODO */
	return 0;
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

	rc = flush_wal_header(mc);
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

	rc = write_header(mc, mc->mc_meta, meta_hdr, sizeof(*meta_hdr), &meta_hdr->mh_csum);
	if (rc) {
		D_ERROR("Write meta header failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	memset(wal_hdr, 0, sizeof(*wal_hdr));
	wal_hdr->wh_magic = BIO_WAL_MAGIC;
	wal_hdr->wh_version = BIO_WAL_VERSION;
	wal_hdr->wh_gen = (uint32_t)daos_wallclock_secs();
	wal_hdr->wh_blk_bytes = WAL_BLK_SZ;
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
