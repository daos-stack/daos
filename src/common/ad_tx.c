/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos_srv/ad_mem.h>
#include "ad_mem.h"

struct umem_wal_tx_item {
	d_list_t		ti_link;
	struct umem_wal_tx	ti_utx;
};

struct ad_tls_cache {
	d_list_t	atc_act_list;
	d_list_t	atc_tx_list;
	d_list_t	atc_act_copy_list;
	int		atc_act_nr;
	int		atc_tx_nr;
	int		atc_act_copy_nr;
	int		atc_inited;
};

static __thread struct ad_tx	*tls_tx;

#define AD_TLS_CACHE_ENABLED	(1)
#define TLS_ACT_NUM		(64)
#define TLS_ACT_MAX		(512)
#define TLS_TX_NUM		(16)
#define TLS_ACT_COPY_NUM	(64)
#define TLS_ACT_COPY_MAX	(256)
/* payload size of cached UMEM_ACT_COPY act, if payload size exceed it then
 * will directly allocate.
 */
#define TSL_ACT_COPY_SZ		(512)

static __thread struct ad_tls_cache tls_cache;

static struct ad_act *
tls_act_get(int opc, size_t size)
{
	struct ad_act	*act;

#if AD_TLS_CACHE_ENABLED
	if (opc != UMEM_ACT_COPY && tls_cache.atc_act_nr > 0) {
		act = d_list_pop_entry(&tls_cache.atc_act_list, struct ad_act, it_link);
		act->it_act.ac_opc = opc;
		tls_cache.atc_act_nr--;
		return act;
	}
	if (opc == UMEM_ACT_COPY && size <= TSL_ACT_COPY_SZ && tls_cache.atc_act_copy_nr > 0) {
		act = d_list_pop_entry(&tls_cache.atc_act_copy_list, struct ad_act, it_link);
		act->it_act.ac_opc = opc;
		tls_cache.atc_act_copy_nr--;
		return act;
	}
#endif

	if (opc == UMEM_ACT_COPY) {
		size = max(size, TSL_ACT_COPY_SZ);
		D_ALLOC_NZ(act, offsetof(struct ad_act, it_act.ac_copy.payload[size]));
	} else {
		D_ALLOC_PTR_NZ(act);
	}
	if (likely(act != NULL)) {
		D_INIT_LIST_HEAD(&act->it_link);
		act->it_act.ac_opc = opc;
	}

	return act;
}

static void
tls_act_put(struct ad_act *act)
{
	d_list_del(&act->it_link);

#if AD_TLS_CACHE_ENABLED
	if (unlikely(!tls_cache.atc_inited)) {
		D_FREE(act);
		return;
	}
	if (act->it_act.ac_opc == UMEM_ACT_COPY && act->it_act.ac_copy.size <= TSL_ACT_COPY_SZ &&
	    tls_cache.atc_act_copy_nr < TLS_ACT_COPY_MAX) {
		d_list_add(&act->it_link, &tls_cache.atc_act_copy_list);
		tls_cache.atc_act_copy_nr++;
		return;
	}
	if (act->it_act.ac_opc != UMEM_ACT_COPY && tls_cache.atc_act_nr < TLS_ACT_MAX) {
		d_list_add(&act->it_link, &tls_cache.atc_act_list);
		tls_cache.atc_act_nr++;
		return;
	}
#endif

	D_FREE(act);
}

static struct umem_wal_tx *
tls_utx_get(void)
{
	struct umem_wal_tx_item	*item;

#if AD_TLS_CACHE_ENABLED
	if (tls_cache.atc_tx_nr > 0) {
		item = d_list_pop_entry(&tls_cache.atc_tx_list, struct umem_wal_tx_item, ti_link);
		item->ti_utx.utx_stage = 0;
		tls_cache.atc_tx_nr--;
		return &item->ti_utx;
	}
#endif
	D_ALLOC_PTR(item);
	if (item == NULL)
		return NULL;
	D_INIT_LIST_HEAD(&item->ti_link);
	return &item->ti_utx;
}

static void
tls_utx_put(struct umem_wal_tx *utx)
{
	struct umem_wal_tx_item	*item;

	item = container_of(utx, struct umem_wal_tx_item, ti_utx);

#if AD_TLS_CACHE_ENABLED
	if (unlikely(!tls_cache.atc_inited)) {
		D_FREE(item);
		return;
	}
	d_list_add(&item->ti_link, &tls_cache.atc_tx_list);
	tls_cache.atc_tx_nr++;
	return;
#endif

	D_FREE(item);
}

void
ad_tls_cache_init(void)
{
	struct umem_wal_tx_item	*item;
	struct ad_act		*act;
	int			 i;

	tls_cache.atc_act_nr = 0;
	D_INIT_LIST_HEAD(&tls_cache.atc_act_list);
	tls_cache.atc_tx_nr = 0;
	D_INIT_LIST_HEAD(&tls_cache.atc_tx_list);
	tls_cache.atc_act_copy_nr = 0;
	D_INIT_LIST_HEAD(&tls_cache.atc_act_copy_list);
	tls_cache.atc_inited = true;

	for (i = 0; i < TLS_ACT_NUM; i++) {
		D_ALLOC_PTR(act);
		if (act == NULL)
			goto failed;
		act->it_act.ac_opc = UMEM_ACT_NOOP;
		D_INIT_LIST_HEAD(&act->it_link);
		tls_act_put(act);
	}

	for (i = 0; i < TLS_TX_NUM; i++) {
		D_ALLOC_PTR(item);
		if (item == NULL)
			goto failed;
		D_INIT_LIST_HEAD(&item->ti_link);
		tls_utx_put(&item->ti_utx);
	}

	for (i = 0; i < TLS_ACT_COPY_NUM; i++) {
		D_ALLOC(act, offsetof(struct ad_act, it_act.ac_copy.payload[TSL_ACT_COPY_SZ]));
		if (act == NULL)
			goto failed;
		act->it_act.ac_opc = UMEM_ACT_COPY;
		act->it_act.ac_copy.size = TSL_ACT_COPY_SZ;
		D_INIT_LIST_HEAD(&act->it_link);
		tls_act_put(act);
	}
	return;
failed:
	ad_tls_cache_fini();
}

void
ad_tls_cache_fini(void)
{
	struct ad_act		*act, *next;
	struct umem_wal_tx_item	*item, *tmp;

	d_list_for_each_entry_safe(act, next, &tls_cache.atc_act_list, it_link) {
		d_list_del(&act->it_link);
		D_FREE(act);
	}
	tls_cache.atc_act_nr = 0;

	d_list_for_each_entry_safe(act, next, &tls_cache.atc_act_copy_list, it_link) {
		d_list_del(&act->it_link);
		D_FREE(act);
	}
	tls_cache.atc_act_copy_nr = 0;

	d_list_for_each_entry_safe(item, tmp, &tls_cache.atc_tx_list, ti_link) {
		d_list_del(&item->ti_link);
		D_FREE(item);
	}
	tls_cache.atc_tx_nr = 0;
}

static char *
act_opc2str(int act)
{
	switch (act) {
	default:
		D_ASSERTF(0, "unknown opcode=%d\n", act);
		return "unknown";
	case UMEM_ACT_NOOP:
		return "NOOP";
	case UMEM_ACT_COPY:
		return "copy";
	case UMEM_ACT_COPY_PTR:
		return "copy_ptr";
	case UMEM_ACT_ASSIGN:
		return "assign";
	case UMEM_ACT_MOVE:
		return "move";
	case UMEM_ACT_SET:
		return "set";
	case UMEM_ACT_SET_BITS:
		return "setbits";
	case UMEM_ACT_CLR_BITS:
		return "clrbits";
	case UMEM_ACT_CSUM:
		return "csum";
	}
}

D_CASSERT(sizeof(struct ad_tx) <= UTX_PRIV_SIZE);

enum {
	ACT_UNDO = 0,
	ACT_REDO = 1,
};

static inline void
act_item_add(struct ad_tx *tx, struct ad_act *it, int undo_or_redo)
{
	if (undo_or_redo == ACT_UNDO) {
		D_DEBUG(DB_TRACE, "Add act %s (%p), to tx %p undo\n",
			act_opc2str(it->it_act.ac_opc), &it->it_act, tx);
		d_list_add(&it->it_link, &tx->tx_undo);

	} else {
		D_DEBUG(DB_TRACE, "Add act %s (%p), to tx %p redo\n",
			act_opc2str(it->it_act.ac_opc), &it->it_act, tx);
		d_list_add_tail(&it->it_link, &tx->tx_redo);
		tx->tx_redo_act_nr++;

		if (it->it_act.ac_opc == UMEM_ACT_COPY ||
		    it->it_act.ac_opc == UMEM_ACT_COPY_PTR) {
			tx->tx_redo_payload_len += it->it_act.ac_copy.size;
		} else if (it->it_act.ac_opc == UMEM_ACT_MOVE) {
			/* ac_move src addr is playload after wal_trans_entry */
			tx->tx_redo_payload_len += sizeof(uint64_t);
		}
	}
}

static inline void
act_copy_payload(struct umem_action *act, const void *addr, daos_size_t size)
{
	char	*dst = (char *)&act->ac_copy.payload[0];

	if (size > 0)
		memcpy(dst, addr, size);
}

/**
 * query action number in redo list.
 */
static inline uint32_t
ad_tx_redo_act_nr(struct umem_wal_tx *wal_tx)
{
	struct ad_tx	*tx = umem_tx2ad_tx(wal_tx);

	return tx->tx_redo_act_nr;
}

/**
 * query payload length in redo list.
 */
static inline uint32_t
ad_tx_redo_payload_len(struct umem_wal_tx *wal_tx)
{
	struct ad_tx	*tx = umem_tx2ad_tx(wal_tx);

	return tx->tx_redo_payload_len;
}

/**
 * get first action pointer, NULL for list empty.
 */
struct umem_action *
ad_tx_redo_act_first(struct umem_wal_tx *wal_tx)
{
	struct ad_tx	*tx = umem_tx2ad_tx(wal_tx);

	if (d_list_empty(&tx->tx_redo)) {
		tx->tx_redo_act_pos = NULL;
		return NULL;
	}

	tx->tx_redo_act_pos = d_list_entry(tx->tx_redo.next, struct ad_act, it_link);
	return &tx->tx_redo_act_pos->it_act;
}

/**
 * get next action pointer, NULL for done or list empty.
 */
struct umem_action *
ad_tx_redo_act_next(struct umem_wal_tx *wal_tx)
{
	struct ad_tx	*tx = umem_tx2ad_tx(wal_tx);

	if (tx->tx_redo_act_pos == NULL) {
		if (d_list_empty(&tx->tx_redo))
			return NULL;
		tx->tx_redo_act_pos = d_list_entry(tx->tx_redo.next, struct ad_act, it_link);
		return &tx->tx_redo_act_pos->it_act;
	}

	D_ASSERT(!d_list_empty(&tx->tx_redo));
	tx->tx_redo_act_pos = d_list_entry(tx->tx_redo_act_pos->it_link.next,
					   struct ad_act, it_link);
	if (&tx->tx_redo_act_pos->it_link == &tx->tx_redo) {
		tx->tx_redo_act_pos = NULL;
		return NULL;
	}
	return &tx->tx_redo_act_pos->it_act;
}

static struct umem_wal_tx_ops ad_wal_tx_ops = {
	.wtx_act_nr	= ad_tx_redo_act_nr,
	.wtx_payload_sz	= ad_tx_redo_payload_len,
	.wtx_act_first	= ad_tx_redo_act_first,
	.wtx_act_next	= ad_tx_redo_act_next,
};

#define ad_range_end(r)		((r)->ar_off + (r)->ar_size)

static bool
tx_range_canmerge(struct ad_range *r1, struct ad_range *r2)
{
	return (r1->ar_off < ad_range_end(r2) && r2->ar_off < ad_range_end(r1)) ||
	       (r1->ar_off == ad_range_end(r2)) || (r2->ar_off == ad_range_end(r1));
}

/* merge r2 to r1 */
static void
tx_range_merge(struct ad_range *r1, struct ad_range *r2)
{
	r1->ar_off = min(r1->ar_off, r2->ar_off);
	r1->ar_size = max(ad_range_end(r1), ad_range_end(r2)) - r1->ar_off;
}

static int
tx_range_add(struct ad_tx *tx, uint64_t off, uint64_t size, bool alloc)
{
	struct ad_range		 range;
	struct ad_range		*tmp;
	d_list_t		*at = &tx->tx_ranges;

	range.ar_off = off;
	range.ar_size = size;
	d_list_for_each_entry(tmp, &tx->tx_ranges, ar_link) {
		if (!alloc && !tmp->ar_alloc && tx_range_canmerge(tmp, &range)) {
			tx_range_merge(tmp, &range);
			return 0;
		}
		if (off <= tmp->ar_off) {
			at = &tmp->ar_link;
			break;
		}
	}

	D_ALLOC_PTR_NZ(tmp);
	if(tmp == NULL)
		return -DER_NOMEM;
	D_INIT_LIST_HEAD(&tmp->ar_link);
	tmp->ar_off = off;
	tmp->ar_size = size;
	tmp->ar_alloc = alloc;
	d_list_add(&tmp->ar_link, at);

	return 0;
}

/* delete a range (only for newly allocated) */
static void
tx_range_del(struct ad_tx *tx, uint64_t off)
{
	struct ad_range		*tmp, *next;

	d_list_for_each_entry_safe(tmp, next, &tx->tx_ranges, ar_link) {
		if (off < tmp->ar_off)
			break;
		if (off == tmp->ar_off && tmp->ar_alloc) {
			d_list_del(&tmp->ar_link);
			D_FREE(tmp);
			break;
		}
	}
}

/* post process for tx ranges in tx commit - insert tx_add redo actions */
static int
tx_range_post(struct ad_tx *tx)
{
	struct ad_range		*tmp, *next;
	struct ad_blob_handle	 bh;

	bh.bh_blob = tx->tx_blob;
	d_list_for_each_entry_safe(tmp, next, &tx->tx_ranges, ar_link) {
		int	rc;

		if (&next->ar_link != &tx->tx_ranges &&
		    tx_range_canmerge(next, tmp)) {
			tx_range_merge(next, tmp);
			d_list_del(&tmp->ar_link);
			D_FREE(tmp);
			continue;
		}

		rc = ad_tx_snap(tx, ad_addr2ptr(bh, tmp->ar_off), tmp->ar_size, AD_TX_REDO);
		if (rc) {
			D_ERROR("ad_tx_snap failed, "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	return 0;
}

/** start a ad-hoc memory transaction */
int
ad_tx_begin(struct ad_blob_handle bh, struct ad_tx *tx)
{
	int	rc = 0;

	blob_addref(bh.bh_blob);
	tx->tx_blob = bh.bh_blob;
	D_INIT_LIST_HEAD(&tx->tx_redo);
	D_INIT_LIST_HEAD(&tx->tx_undo);
	D_INIT_LIST_HEAD(&tx->tx_ar_pub);
	D_INIT_LIST_HEAD(&tx->tx_gp_pub);
	D_INIT_LIST_HEAD(&tx->tx_gp_reset);
	D_INIT_LIST_HEAD(&tx->tx_frees);
	D_INIT_LIST_HEAD(&tx->tx_allocs);
	D_INIT_LIST_HEAD(&tx->tx_ranges);

	tx->tx_redo_act_nr	= 0;
	tx->tx_redo_payload_len	= 0;
	tx->tx_redo_act_pos	= NULL;

	tx->tx_layer = 1;
	tx->tx_last_errno = 0;

	return rc;
}

static int
ad_act_replay(struct ad_tx *tx, struct umem_action *act)
{
	int rc = 0;

	D_DEBUG(DB_TRACE, "replay action=%s\n", act_opc2str(act->ac_opc));
	switch (act->ac_opc) {
	case UMEM_ACT_NOOP:
		break;
	case UMEM_ACT_COPY:
		rc = ad_tx_copy(NULL, blob_addr2ptr(tx->tx_blob, act->ac_copy.addr),
				act->ac_copy.size, act->ac_copy.payload, 0);
		break;
	case UMEM_ACT_COPY_PTR:
		rc = ad_tx_copy(NULL, blob_addr2ptr(tx->tx_blob, act->ac_copy_ptr.addr),
				act->ac_copy_ptr.size, (void *)act->ac_copy_ptr.ptr, 0);
		break;
	case UMEM_ACT_ASSIGN:
		rc = ad_tx_assign(NULL, blob_addr2ptr(tx->tx_blob, act->ac_assign.addr),
				  act->ac_assign.size, act->ac_assign.val, 0);
		break;
	case UMEM_ACT_MOVE:
		rc = ad_tx_move(NULL, blob_addr2ptr(tx->tx_blob, act->ac_move.dst),
				blob_addr2ptr(tx->tx_blob, act->ac_move.src), act->ac_move.size);
		break;
	case UMEM_ACT_SET:
		rc = ad_tx_set(NULL, blob_addr2ptr(tx->tx_blob, act->ac_set.addr),
			       act->ac_set.val, act->ac_set.size, false);
		break;
	case UMEM_ACT_SET_BITS:
		rc = ad_tx_setbits(NULL, blob_addr2ptr(tx->tx_blob, act->ac_op_bits.addr),
				   act->ac_op_bits.pos, act->ac_op_bits.num);
		break;
	case UMEM_ACT_CLR_BITS:
		rc = ad_tx_clrbits(NULL, blob_addr2ptr(tx->tx_blob, act->ac_op_bits.addr),
				   act->ac_op_bits.pos, act->ac_op_bits.num);
		break;
	case UMEM_ACT_CSUM:
		break;
	default:
		rc = -DER_INVAL;
		D_ERROR("bad ac_opc %d\n", act->ac_opc);
		break;
	}

	if (rc)
		D_ERROR("Failed to replay %s, "DF_RC"\n", act_opc2str(act->ac_opc), DP_RC(rc));

	return rc;
}

static int
ad_tx_act_replay(struct ad_tx *tx, d_list_t *list)
{
	struct ad_act	*it;
	int		 rc = 0;

	d_list_for_each_entry(it, list, it_link) {
		rc = ad_act_replay(tx, &it->it_act);
		if (rc)
			break;
	}
	return rc;
}

static void
ad_tx_act_cleanup(d_list_t *list)
{
	struct ad_act	*it, *next;

	d_list_for_each_entry_safe(it, next, list, it_link)
		tls_act_put(it);
}

static void
ad_tx_ranges_cleanup(d_list_t *list)
{
	struct ad_range	*it, *next;

	d_list_for_each_entry_safe(it, next, list, ar_link) {
		d_list_del(&it->ar_link);
		D_FREE(it);
	}
}

/** complete a ad-hoc memory transaction */
int
ad_tx_end(struct ad_tx *tx, int err)
{
	int	rc = 0;

	if (err == 0)
		err = tx->tx_last_errno;
	if (err == 0)
		err = tx_range_post(tx);

	rc = tx_complete(tx, err);
	if (rc)
		ad_tx_act_replay(tx, &tx->tx_undo);

	ad_tx_act_cleanup(&tx->tx_undo);
	ad_tx_act_cleanup(&tx->tx_redo);
	ad_tx_ranges_cleanup(&tx->tx_ranges);

	blob_decref(tx->tx_blob);
	return rc;
}

/**
 * snapshot data from address to either redo or undo log
 */
int
ad_tx_snap(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t flags)
{
	bool	 undo = (flags & AD_TX_UNDO);
	bool	 redo = (flags & AD_TX_REDO);

	if (redo == undo)
		return -DER_INVAL;

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) /* noop */
		return 0;

	if (undo) {
		struct ad_act	*it_undo;

		it_undo = tls_act_get(UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		act_item_add(tx, it_undo, ACT_UNDO);
	} else {
		struct ad_act	*it_redo = NULL;

		it_redo = tls_act_get(UMEM_ACT_COPY, size);
		if (it_redo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_redo->it_act, addr, size);
		it_redo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_redo->it_act.ac_copy.size = size;
		act_item_add(tx, it_redo, ACT_REDO);
	}
	return 0;
}

/**
 * copy data from buffer @ptr to storage address @addr, both old and new data can be saved
 * for TX redo and undo.
 */
int
ad_tx_copy(struct ad_tx *tx, void *addr, daos_size_t size, const void *ptr, uint32_t flags)
{
	if (addr == NULL || ptr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		memcpy(addr, ptr, size);
		return 0;
	}

	if (flags & AD_TX_UNDO) {
		struct ad_act *it_undo;

		it_undo = tls_act_get(UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		act_item_add(tx, it_undo, ACT_UNDO);

	} else {
		struct ad_act *it_redo;

		if (!(flags & AD_TX_REDO))
			return -DER_INVAL;

		if (flags & AD_TX_COPY_PTR) {
			it_redo = tls_act_get(UMEM_ACT_COPY_PTR, size);
			if (it_redo == NULL)
				return -DER_NOMEM;

			it_redo->it_act.ac_copy_ptr.addr = blob_ptr2addr(tx->tx_blob, addr);
			it_redo->it_act.ac_copy_ptr.ptr = (uintptr_t)ptr;
			it_redo->it_act.ac_set.size = size;
		} else {
			it_redo = tls_act_get(UMEM_ACT_COPY, size);
			if (it_redo == NULL)
				return -DER_NOMEM;

			act_copy_payload(&it_redo->it_act, ptr, size);
			it_redo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
			it_redo->it_act.ac_copy.size = size;
		}
		act_item_add(tx, it_redo, ACT_REDO);
	}
	return 0;
}

static uint32_t
get_integer(void *addr, int size)
{
	switch (size) {
	default:
		D_ASSERT(0);
		break;
	case 1:
		return *((uint8_t *)addr);
	case 2:
		return *((uint16_t *)addr);
	case 4:
		return *((uint32_t *)addr);
	}
}

static void
assign_integer(void *addr, int size, uint32_t val)
{
	switch (size) {
	default:
		D_ASSERT(0);
		break;
	case 1:
		*((uint8_t *)addr) = (uint8_t)val;
		break;
	case 2:
		*((uint16_t *)addr) = (uint16_t)val;
		break;
	case 4:
		*((uint32_t *)addr) = val;
		break;
	}
}

/** assign integer value to @addr, both old and new value should be saved for redo and undo */
int
ad_tx_assign(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t val, uint32_t flags)
{
	if (addr == NULL || (size != 1 && size != 2 && size != 4))
		return -DER_INVAL;

	if (tx == NULL) {
		assign_integer(addr, size, val);
		return 0;
	}

	if (flags & AD_TX_UNDO) {
		struct ad_act	*it_undo;

		it_undo = tls_act_get(UMEM_ACT_ASSIGN, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		it_undo->it_act.ac_assign.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_assign.size = size;
		assign_integer(&it_undo->it_act.ac_assign.val, size, get_integer(addr, size));
		act_item_add(tx, it_undo, ACT_UNDO);
	}

	if (!(flags & AD_TX_LOG_ONLY))
		assign_integer(addr, size, val);

	if (flags & AD_TX_REDO) {
		struct ad_act	*it_redo;

		it_redo = tls_act_get(UMEM_ACT_ASSIGN, size);
		if (it_redo == NULL)
			return -DER_NOMEM;

		it_redo->it_act.ac_assign.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_redo->it_act.ac_assign.size = size;
		it_redo->it_act.ac_assign.val = val;
		act_item_add(tx, it_redo, ACT_REDO);
	}
	return 0;
}

/**
 * memset a storage region, save the operation for redo (and old value for undo if it's
 * required by @save_old).
 * If AD_TX_LOG_ONLY is set for @flags, this function only logs the operation itself,
 * it does not call the memset(), this is for reserve() interface.
 */
int
ad_tx_set(struct ad_tx *tx, void *addr, char c, daos_size_t size, uint32_t flags)
{
	struct ad_act	*it_undo, *it_redo;

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		if (!(flags & AD_TX_LOG_ONLY))
			memset(addr, c, size);
		return 0;
	}

	if (flags & AD_TX_UNDO) {
		it_undo = tls_act_get(UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		act_item_add(tx, it_undo, ACT_UNDO);
	}

	if (!(flags & AD_TX_LOG_ONLY))
		memset(addr, c, size);

	if (flags & AD_TX_REDO) {
		it_redo = tls_act_get(UMEM_ACT_SET, size);
		if (it_redo == NULL)
			return -DER_NOMEM;

		it_redo->it_act.ac_set.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_redo->it_act.ac_set.size = size;
		it_redo->it_act.ac_set.val = c;
		act_item_add(tx, it_redo, ACT_REDO);
	}
	return 0;
}

/**
 * memmove a storage region, save the operation for redo and old memory content for undo.
 */
int
ad_tx_move(struct ad_tx *tx, void *dst, void *src, daos_size_t size)
{
	struct ad_act	*it_undo, *it_redo;

	if (dst == NULL || src == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		memmove(dst, src, size);
		return 0;
	}

	it_undo = tls_act_get(UMEM_ACT_COPY, size);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_redo = tls_act_get(UMEM_ACT_MOVE, size);
	if (it_redo == NULL) {
		D_FREE(it_undo);
		return -DER_NOMEM;
	}

	act_copy_payload(&it_undo->it_act, dst, size);
	it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, dst);
	it_undo->it_act.ac_copy.size = size;
	act_item_add(tx, it_undo, ACT_UNDO);

	it_redo->it_act.ac_move.dst = blob_ptr2addr(tx->tx_blob, dst);
	it_redo->it_act.ac_move.src = blob_ptr2addr(tx->tx_blob, src);
	it_redo->it_act.ac_move.size = size;
	act_item_add(tx, it_redo, ACT_REDO);

	return 0;
}

/** setbit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_setbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits)
{
	struct ad_act	*it_undo, *it_redo;
	uint32_t		 end = pos + nbits - 1;

	if (bmap == NULL) {
		D_ERROR("empty bitmap\n");
		return -DER_INVAL;
	}

	/* if use cases cannot satisfy this requirement, need to add copybits action for undo */
	if (!isclr_range(bmap, pos, end)) {
		D_ERROR("bitmap already set in the range.\n");
		return -DER_INVAL;
	}

	if (tx == NULL) {
		setbit_range(bmap, pos, end);
		return 0;
	}

	it_undo = tls_act_get(UMEM_ACT_CLR_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_undo, ACT_UNDO);

	setbit_range(bmap, pos, end);

	it_redo = tls_act_get(UMEM_ACT_SET_BITS, 0);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_redo->it_act.ac_op_bits.pos = pos;
	it_redo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_redo, ACT_REDO);

	return 0;
}

/** clear bit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_clrbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits)
{
	struct ad_act	*it_undo, *it_redo;
	uint32_t	 end = pos + nbits - 1;

	if (bmap == NULL) {
		D_ERROR("empty bitmap\n");
		return -DER_INVAL;
	}

	/* if use cases cannot satisfy this requirement, need to add copybits action for undo */
	if (!isset_range(bmap, pos, end)) {
		D_ERROR("bitmap already cleared in the range.\n");
		return -DER_INVAL;
	}

	if (tx == NULL) {
		clrbit_range(bmap, pos, end);
		return 0;
	}

	it_undo = tls_act_get(UMEM_ACT_SET_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_undo, ACT_UNDO);

	clrbit_range(bmap, pos, end);

	it_redo = tls_act_get(UMEM_ACT_CLR_BITS, 0);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_redo->it_act.ac_op_bits.pos = pos;
	it_redo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_redo, ACT_REDO);

	return 0;
}

static struct ad_tx *
tx_get()
{
	return tls_tx;
}

static void
tx_set(struct ad_tx *tx)
{
	tls_tx = tx;
}

static void
tx_callback(struct ad_tx *tx)
{
	if (!tx->tx_stage_cb || tx->tx_layer != 0)
		return;
	tx->tx_stage_cb(ad_tx_stage(tx), tx->tx_stage_cb_arg);
}

int
tx_end(struct ad_tx *tx, int err)
{
	struct umem_wal_tx	*utx;
	int			 rc;

	if (err)
		tx->tx_last_errno = err;

	tx->tx_layer--;
	D_ASSERTF(tx->tx_layer >= 0, "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);
	if (tx->tx_layer != 0)
		return 0;

	/* possibly yield in ad_tx_end() -> tx_complete() -> so_wal_submit */
	tx_set(NULL);

	rc = ad_tx_end(tx, err);
	if (rc == 0) {
		ad_tx_stage_set(tx, UMEM_STAGE_ONCOMMIT);
	} else {
		D_DEBUG(DB_TRACE, "ad_tx_end(%d) failed, "DF_RC"\n", err, DP_RC(rc));
		tx->tx_last_errno = rc;
		ad_tx_stage_set(tx, UMEM_STAGE_ONABORT);
	}
	tx_callback(tx);

	/* trigger UMEM_STAGE_NONE callback, this TX is finished but possibly with other WIP TX */
	ad_tx_stage_set(tx, UMEM_STAGE_NONE);
	tx_callback(tx);
	rc = tx->tx_last_errno;
	utx = ad_tx2umem_tx(tx);
	tls_utx_put(utx);

	return rc;
}

static int
tx_abort(struct ad_tx *tx, int err)
{
	if (err == 0)
		err = -DER_CANCELED;

	return tx_end(tx, err);
}

int
tx_begin(struct ad_blob_handle bh, struct umem_tx_stage_data *txd, struct ad_tx **tx_pp)
{
	struct ad_blob		*blob = bh.bh_blob;
	struct umem_wal_tx	*utx = NULL;
	struct ad_tx		*tx;
	struct umem_store	*store;
	uint64_t		 tx_id;
	int			 rc = 0;

	tx = tx_get();
	if (tx == NULL) {
		utx = tls_utx_get();
		if (utx == NULL)
			return -DER_NOMEM;

		utx->utx_ops = &ad_wal_tx_ops;
		tx = umem_tx2ad_tx(utx);
		D_DEBUG(DB_TRACE, "Allocated tx %p\n", tx);
		rc = ad_tx_begin(bh, tx);
		if (rc) {
			D_ERROR("ad_tx_begin failed, "DF_RC"\n", DP_RC(rc));
			D_FREE(utx);
			return rc;
		}

		store = &blob->bb_store;
		rc = store->stor_ops->so_wal_reserv(store, &tx_id);
		if (rc) {
			D_ERROR("so_wal_reserv failed, "DF_RC"\n", DP_RC(rc));
			blob_decref(blob); /* drop ref taken in ad_tx_begin */
			D_FREE(utx);
			return rc;
		}

		/* possibly yield in so_wal_reserv, but tls_tx should be NULL when it get back */
		D_ASSERT(tx_get() == NULL);
		if (txd != NULL) {
			tx->tx_stage_cb = umem_stage_callback;
			tx->tx_stage_cb_arg = txd;
		}
		ad_tx_id_set(tx, tx_id);
		ad_tx_stage_set(tx, UMEM_STAGE_WORK);
		tx_set(tx);
		D_DEBUG(DB_TRACE, "TX "DF_U64" started\n", tx_id);
	} else {
		D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
			  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));

		tx->tx_layer++;
		if (blob != tx->tx_blob) {
			D_ERROR("Nested TX for different blob\n");
			rc = -DER_INVAL;
			goto err_abort;
		}
		if (txd != NULL) {
			if (tx->tx_stage_cb_arg == NULL) {
				tx->tx_stage_cb = umem_stage_callback;
				tx->tx_stage_cb_arg = txd;
			} else if (txd != tx->tx_stage_cb_arg) {
				D_ERROR("Cannot set different TX callback argument\n");
				rc = -DER_CANCELED;
				goto err_abort;
			}
		}
		D_DEBUG(DB_TRACE, "Nested TX "DF_U64", layer %d\n", ad_tx_id(tx), tx->tx_layer);
	}

	*tx_pp = tx;
	return 0;

err_abort:
	D_ASSERT(rc != 0);
	D_ASSERT(tx != NULL);
	rc = tx_abort(tx, rc);
	return rc;
}

static int
umo_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	struct ad_tx		*tx;
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);

	return tx_begin(bh, txd, &tx);
}

static int
umo_tx_abort(struct umem_instance *umm, int err)
{
	struct ad_tx	*tx = tx_get();

	D_ASSERTF(tx->tx_layer > 0,
		  "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);

	return tx_abort(tx, err);
}

static int
umo_tx_commit(struct umem_instance *umm, void *data)
{
	struct ad_tx	*tx = tx_get();

	D_ASSERTF(tx->tx_layer > 0,
		  "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);

	return tx_end(tx, 0);
}

static int
umo_tx_stage(void)
{
	struct ad_tx	*tx = tx_get();

	/* XXX when return UMEM_STAGE_NONE possibly with TX in committing */
	return (tx == NULL) ? UMEM_STAGE_NONE : ad_tx_stage(tx);
}

static int
umo_tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	struct ad_tx		*tx = tx_get();
	int			 rc = 0;

	tx_range_del(tx, umoff);

	/*
	 * This free call could be on error cleanup code path where
	 * the transaction is already aborted due to previous failed
	 * ad_tx call. Let's just skip it in this case.
	 *
	 * The reason we don't fix caller to avoid calling tx_free()
	 * in an aborted transaction is that the caller code could be
	 * shared by both transactional and non-transactional (where
	 * UMEM_CLASS_VMEM is used, see btree code) interfaces, and
	 * the explicit umem_free() on error cleanup is necessary for
	 * non-transactional case.
	 */
	if (ad_tx_stage(tx) == UMEM_STAGE_ONABORT)
		return 0;

	if (!UMOFF_IS_NULL(umoff))
		rc = ad_tx_free(tx, umoff);

	return rc;
}

static umem_off_t
umo_tx_alloc(struct umem_instance *umm, size_t size, uint64_t flags,
	     unsigned int type_num)
{
	struct ad_tx		*tx = tx_get();
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	umem_off_t		 off;
	int			 type;

	D_ASSERT(!(flags & UMEM_FLAG_NO_FLUSH));
	type = size > 4096 ? ARENA_TYPE_LARGE : 0;
	off = ad_alloc(bh, type, size, NULL);
	if (!UMOFF_IS_NULL(off)) {
		int	rc;

		rc = tx_range_add(tx, off, size, true);
		if (rc) {
			D_ERROR("tx_range_add failed, "DF_RC"\n", DP_RC(rc));
			rc = ad_tx_free(tx, off);
			if (rc)
				D_ERROR("ad_tx_free failed, "DF_RC"\n", DP_RC(rc));
			return 0;
		}

		if (flags & UMEM_FLAG_ZERO)
			memset(ad_addr2ptr(bh, off), 0, size);
	}

	return off;
}

static int
tx_add_internal(struct ad_tx *tx, void *ptr, size_t size, uint32_t flags)
{
	int	rc;

	D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
		  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));

	if (flags & AD_TX_REDO) {
		rc = tx_range_add(tx, blob_ptr2addr(tx->tx_blob, ptr), size, false);
		if (rc) {
			D_ERROR("tx_range_add failed, "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	if (flags & AD_TX_UNDO)
		return ad_tx_snap(tx, ptr, size, AD_TX_UNDO);

	return 0;
}

static int
umo_tx_add(struct umem_instance *umm, umem_off_t umoff, uint64_t offset, size_t size)
{
	struct ad_tx		*tx = tx_get();
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	struct ad_blob		*blob = bh.bh_blob;
	void			*ptr;

	D_ASSERT(offset == 0);
	ptr = blob_addr2ptr(blob, umoff);
	return tx_add_internal(tx, ptr, size, AD_TX_UNDO | AD_TX_REDO);
}

static int
umo_tx_xadd(struct umem_instance *umm, umem_off_t umoff, uint64_t offset, size_t size,
	    uint64_t flags)
{
	struct ad_tx		*tx = tx_get();
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	struct ad_blob		*blob = bh.bh_blob;
	void			*ptr;
	uint32_t		 ad_flags = 0;

	D_ASSERT(!(flags & UMEM_FLAG_NO_FLUSH));
	ad_flags |= AD_TX_REDO;
	if (!(flags & UMEM_XADD_NO_SNAPSHOT))
		ad_flags |= AD_TX_UNDO;

	D_ASSERT(offset == 0);
	ptr = blob_addr2ptr(blob, umoff);

	return tx_add_internal(tx, ptr, size, ad_flags);
}

static int
umo_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	struct ad_tx	*tx = tx_get();

	return tx_add_internal(tx, ptr, size, AD_TX_UNDO | AD_TX_REDO);
}

static umem_off_t
umo_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num)
{
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	struct ad_reserv_act	*ract = act;
	umem_off_t		 off;
	int			 type;

	type = size > 4096 ? ARENA_TYPE_LARGE : 0;
	off = ad_reserve(bh, type, size, NULL, ract);

	if (!UMOFF_IS_NULL(off)) {
		ract->ra_off = off;
		ract->ra_size = size;
	}

	return off;
}

static void
umo_cancel(struct umem_instance *umm, void *actv, int actv_cnt)
{
	ad_cancel((struct ad_reserv_act *)actv, actv_cnt);
}

static int
umo_tx_publish(struct umem_instance *umm, void *actv, int actv_cnt)
{
	struct ad_tx		*tx = tx_get();
	struct ad_reserv_act	*ractv = actv;
	int			 i, rc;

	D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
		  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));

	rc = ad_tx_publish(tx, ractv, actv_cnt);
	if (rc == 0) {
		for (i = 0; i < actv_cnt; i++) {
			rc = tx_range_add(tx, ractv[i].ra_off, ractv[i].ra_size, true);
			if (rc) {
				D_ERROR("tx_range_add failed, "DF_RC"\n", DP_RC(rc));
				break;
			}
		}
	}

	return rc;
}

static void *
umo_atomic_copy(struct umem_instance *umm, void *dest, const void *src, size_t len,
		enum acopy_hint hint)
{
	struct ad_tx	*tx;
	int		 rc = 0;

	rc = umo_tx_begin(umm, NULL);
	if (rc) {
		D_ERROR("umo_tx_begin failed, "DF_RC"\n", DP_RC(rc));
		return NULL;
	}

	tx = tx_get();
	rc = ad_tx_copy(tx, dest, len, src, AD_TX_UNDO);
	if (rc) {
		D_ERROR("ad_tx_copy failed, "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	memcpy(dest, src, len);

	rc = ad_tx_copy(tx, dest, len, src, AD_TX_REDO);
	if (rc) {
		D_ERROR("ad_tx_copy failed, "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	rc = umo_tx_commit(umm, NULL);

	return rc == 0 ? dest: NULL;

failed:
	D_ASSERT(rc != 0);
	umo_tx_abort(umm, rc);

	return NULL;
}

static umem_off_t
umo_atomic_alloc(struct umem_instance *umm, size_t size, unsigned int type_num)
{
	return umo_tx_alloc(umm, size, 0, type_num);
}

static int
umo_atomic_free(struct umem_instance *umm, umem_off_t umoff)
{
	struct ad_tx		*tx;
	int			 rc = 0;

	rc = umo_tx_begin(umm, NULL);
	if (rc) {
		D_ERROR("umo_tx_begin failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	tx = tx_get();

	tx_range_del(tx, umoff);

	rc = ad_tx_free(tx, umoff);
	if (rc) {
		D_ERROR("ad_tx_free failed, "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	rc = umo_tx_commit(umm, NULL);

	return rc;

failed:
	D_ASSERT(rc != 0);
	umo_tx_abort(umm, rc);
	return rc;
}

umem_ops_t	ad_mem_ops = {
	.mo_tx_free		= umo_tx_free,
	.mo_tx_alloc		= umo_tx_alloc,
	.mo_tx_add		= umo_tx_add,
	.mo_tx_xadd		= umo_tx_xadd,
	.mo_tx_add_ptr		= umo_tx_add_ptr,
	.mo_tx_abort		= umo_tx_abort,
	.mo_tx_begin		= umo_tx_begin,
	.mo_tx_commit		= umo_tx_commit,
	.mo_tx_stage		= umo_tx_stage,
	.mo_reserve		= umo_reserve,
	/* defer_free will go to umem_free() -> mo_tx_free, see umem_defer_free */
	.mo_defer_free		= NULL,
	.mo_cancel		= umo_cancel,
	.mo_tx_publish		= umo_tx_publish,
	.mo_atomic_copy		= umo_atomic_copy,
	.mo_atomic_alloc	= umo_atomic_alloc,
	.mo_atomic_free		= umo_atomic_free,
	/* NOOP flush for ADMEM */
	.mo_atomic_flush	= NULL,
	.mo_tx_add_callback	= umem_tx_add_cb,
};
