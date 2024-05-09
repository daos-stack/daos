/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include "dav_internal.h"
#include "wal_tx.h"
#include "util.h"
#include "heap.h"

struct umem_wal_tx_ops dav_wal_tx_ops;

static inline uint64_t
mdblob_addr2offset(struct dav_obj *hdl, void *addr)
{
	return umem_cache_ptr2off(hdl->do_store, addr);
}

#define AD_TX_ACT_ADD(tx, wa)							\
	do {									\
		d_list_add_tail(&(wa)->wa_link, &(tx)->wt_redo);		\
		(tx)->wt_redo_cnt++;						\
		if ((wa)->wa_act.ac_opc == UMEM_ACT_COPY ||			\
		    (wa)->wa_act.ac_opc == UMEM_ACT_COPY_PTR) {			\
			(tx)->wt_redo_payload_len += (wa)->wa_act.ac_copy.size;	\
		} else if ((wa)->wa_act.ac_opc == UMEM_ACT_MOVE) {		\
			/* ac_move src addr is playload after wal_trans_entry */\
			(tx)->wt_redo_payload_len += sizeof(uint64_t);		\
		}								\
	} while (0)

/** allocate wal_action, if success the wa_link and wa_act.ac_opc will be init-ed */
#define D_ALLOC_ACT(wa, opc, size)							\
	do {										\
		if (opc == UMEM_ACT_COPY)						\
			D_ALLOC(wa, offsetof(struct wal_action,				\
					     wa_act.ac_copy.payload[size]));		\
		else									\
			D_ALLOC_PTR(wa);						\
		if (likely(wa != NULL)) {						\
			D_INIT_LIST_HEAD(&wa->wa_link);					\
			wa->wa_act.ac_opc = opc;					\
		}									\
	} while (0)

static inline void
act_copy_payload(struct umem_action *act, void *addr, daos_size_t size)
{
	char	*dst = (char *)&act->ac_copy.payload[0];

	if (size > 0)
		memcpy(dst, addr, size);
}

static void
dav_wal_tx_init(struct umem_wal_tx *utx, struct dav_obj *dav_hdl)
{
	struct dav_tx	*tx = utx2wtx(utx);

	D_INIT_LIST_HEAD(&tx->wt_redo);
	tx->wt_redo_cnt = 0;
	tx->wt_redo_payload_len = 0;
	tx->wt_redo_act_pos = NULL;
	tx->wt_dav_hdl = dav_hdl;
}

struct umem_wal_tx *
dav_umem_wtx_new(struct dav_obj *dav_hdl)
{
	struct umem_wal_tx *umem_wtx;

	D_ASSERT(dav_hdl->do_utx == NULL);
	D_ALLOC_PTR(umem_wtx);
	if (umem_wtx == NULL)
		return NULL;

	umem_wtx->utx_ops = &dav_wal_tx_ops;
	umem_wtx->utx_id = ULLONG_MAX;
	dav_wal_tx_init(umem_wtx, dav_hdl);
	dav_hdl->do_utx = umem_wtx;
	return umem_wtx;
}

void
dav_umem_wtx_cleanup(struct umem_wal_tx *utx)
{
	struct dav_tx		*tx = utx2wtx(utx);
	d_list_t		*list = &tx->wt_redo;
	struct wal_action	*wa, *next;

	d_list_for_each_entry_safe(wa, next, list, wa_link) {
		d_list_del(&wa->wa_link);
		D_FREE(wa);
	}
}

static int
dav_wal_tx_submit(struct dav_obj *dav_hdl, struct umem_wal_tx *utx, void *data)
{
	struct wal_action	*wa, *next;
	struct umem_action	*ua;
	struct umem_store	*store = dav_hdl->do_store;
	struct dav_tx		*tx = utx2wtx(utx);
	d_list_t		*redo_list = &tx->wt_redo;

	char	*pathname = basename(dav_hdl->do_path);
	uint64_t id = utx->utx_id;
	int	 rc;

	if (wal_tx_act_nr(utx) == 0)
		return 0;

	d_list_for_each_entry_safe(wa, next, redo_list, wa_link) {
		ua = &wa->wa_act;
		switch (ua->ac_opc) {
		case UMEM_ACT_COPY:
			D_DEBUG(DB_TRACE,
				"%s: ACT_COPY txid=%lu, (p,o)=%lu,%lu size=%lu\n",
				pathname, id,
				ua->ac_copy.addr / PAGESIZE, ua->ac_copy.addr % PAGESIZE,
				ua->ac_copy.size);
			break;
		case UMEM_ACT_COPY_PTR:
			D_DEBUG(DB_TRACE,
				"%s: ACT_COPY_PTR txid=%lu, (p,o)=%lu,%lu size=%lu ptr=0x%lx\n",
				pathname, id,
				ua->ac_copy_ptr.addr / PAGESIZE, ua->ac_copy_ptr.addr % PAGESIZE,
				ua->ac_copy_ptr.size, ua->ac_copy_ptr.ptr);
			break;
		case UMEM_ACT_ASSIGN:
			D_DEBUG(DB_TRACE,
				"%s: ACT_ASSIGN txid=%lu, (p,o)=%lu,%lu size=%u\n",
				pathname, id,
				ua->ac_assign.addr / PAGESIZE, ua->ac_assign.addr % PAGESIZE,
				ua->ac_assign.size);
			break;
		case UMEM_ACT_SET:
			D_DEBUG(DB_TRACE,
				"%s: ACT_SET txid=%lu, (p,o)=%lu,%lu size=%u val=%u\n",
				pathname, id,
				ua->ac_set.addr / PAGESIZE, ua->ac_set.addr % PAGESIZE,
				ua->ac_set.size, ua->ac_set.val);
			break;
		case UMEM_ACT_SET_BITS:
			D_DEBUG(DB_TRACE,
				"%s: ACT_SET_BITS txid=%lu, (p,o)=%lu,%lu bit_pos=%u num_bits=%u\n",
				pathname, id,
				ua->ac_op_bits.addr / PAGESIZE, ua->ac_op_bits.addr % PAGESIZE,
				ua->ac_op_bits.pos, ua->ac_op_bits.num);
			break;
		case UMEM_ACT_CLR_BITS:
			D_DEBUG(DB_TRACE,
				"%s: ACT_CLR_BITS txid=%lu, (p,o)=%lu,%lu bit_pos=%u num_bits=%u\n",
				pathname, id,
				ua->ac_op_bits.addr / PAGESIZE, ua->ac_op_bits.addr % PAGESIZE,
				ua->ac_op_bits.pos, ua->ac_op_bits.num);
			break;
		default:
			D_ERROR("%s: unknown opc %d\n", dav_hdl->do_path, ua->ac_opc);
			ASSERT(0);
		}
	}
	DAV_DBG("tx_id:%lu submitting to WAL: %u bytes in %u actions",
		id, tx->wt_redo_payload_len, tx->wt_redo_cnt);
	rc = store->stor_ops->so_wal_submit(store, utx, data);
	return rc;
}

/** complete the wl transaction */
int
dav_wal_tx_commit(struct dav_obj *hdl, struct umem_wal_tx *utx, void *data)
{
	int rc;

	/* write actions in redo list to WAL */
	rc = dav_wal_tx_submit(hdl, utx, data);

	/* FAIL the engine if commit fails */
	D_ASSERT(rc == 0);
	dav_umem_wtx_cleanup(utx);
	return 0;
}

int
dav_wal_tx_reserve(struct dav_obj *hdl, uint64_t *id)
{
	int rc;

	rc = hdl->do_store->stor_ops->so_wal_reserv(hdl->do_store, id);
	/* REVISIT:
	 * Remove this assert once callers of dav_free() and dav_memcpy_persist()
	 * are modified to handle failures.
	 */
	D_ASSERT(rc == 0);
	return rc;
}

/**
 * snapshot data from src to either wal redo log.
 */
int
dav_wal_tx_snap(void *hdl, void *addr, daos_size_t size, void *src, uint32_t flags)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct dav_tx		*tx = utx2wtx(dav_hdl->do_utx);
	struct wal_action	*wa_redo;
	int                      rc;

	D_ASSERT(hdl != NULL);

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	rc = umem_cache_touch(dav_hdl->do_store, dav_hdl->do_utx->utx_id,
			      mdblob_addr2offset(tx->wt_dav_hdl, addr), size);
	if (rc != 0)
		return rc;

	if (flags & DAV_XADD_WAL_CPTR) {
		D_ALLOC_ACT(wa_redo, UMEM_ACT_COPY_PTR, size);
		if (wa_redo == NULL)
			return -DER_NOMEM;
		wa_redo->wa_act.ac_copy_ptr.ptr = (uintptr_t)src;
		wa_redo->wa_act.ac_copy_ptr.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
		wa_redo->wa_act.ac_copy_ptr.size = size;
	} else {
		D_ALLOC_ACT(wa_redo, UMEM_ACT_COPY, size);
		if (wa_redo == NULL)
			return -DER_NOMEM;
		act_copy_payload(&wa_redo->wa_act, src, size);
		wa_redo->wa_act.ac_copy.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
		wa_redo->wa_act.ac_copy.size = size;
	}
	AD_TX_ACT_ADD(tx, wa_redo);
	return 0;
}

/** assign uint64_t value to @addr */
int
dav_wal_tx_assign(void *hdl, void *addr, uint64_t val)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct dav_tx		*tx = utx2wtx(dav_hdl->do_utx);
	struct wal_action	*wa_redo;
	int                      rc;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	rc = umem_cache_touch(dav_hdl->do_store, dav_hdl->do_utx->utx_id,
			      mdblob_addr2offset(tx->wt_dav_hdl, addr), sizeof(uint64_t));
	if (rc != 0)
		return rc;

	D_ALLOC_ACT(wa_redo, UMEM_ACT_ASSIGN, sizeof(uint64_t));
	if (wa_redo == NULL)
		return -DER_NOMEM;
	wa_redo->wa_act.ac_assign.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	wa_redo->wa_act.ac_assign.size = 8;
	wa_redo->wa_act.ac_assign.val = val;
	AD_TX_ACT_ADD(tx, wa_redo);

	return 0;
}

/** Set bits starting from pos */
int
dav_wal_tx_set_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct dav_tx		*tx = utx2wtx(dav_hdl->do_utx);
	struct wal_action	*wa_redo;
	int                      rc;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	rc = umem_cache_touch(dav_hdl->do_store, dav_hdl->do_utx->utx_id,
			      mdblob_addr2offset(tx->wt_dav_hdl, addr), sizeof(uint64_t));
	if (rc != 0)
		return rc;

	D_ALLOC_ACT(wa_redo, UMEM_ACT_SET_BITS, sizeof(uint64_t));
	if (wa_redo == NULL)
		return -DER_NOMEM;
	wa_redo->wa_act.ac_op_bits.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	wa_redo->wa_act.ac_op_bits.num = num_bits;
	wa_redo->wa_act.ac_op_bits.pos = pos;
	AD_TX_ACT_ADD(tx, wa_redo);

	return 0;
}

/** Clr bits starting from pos */
int
dav_wal_tx_clr_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct dav_tx		*tx = utx2wtx(dav_hdl->do_utx);
	struct wal_action	*wa_redo;
	int                      rc;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	rc = umem_cache_touch(dav_hdl->do_store, dav_hdl->do_utx->utx_id,
			      mdblob_addr2offset(tx->wt_dav_hdl, addr), sizeof(uint64_t));
	if (rc != 0)
		return rc;

	D_ALLOC_ACT(wa_redo, UMEM_ACT_CLR_BITS, sizeof(uint64_t));
	if (wa_redo == NULL)
		return -DER_NOMEM;
	wa_redo->wa_act.ac_op_bits.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	wa_redo->wa_act.ac_op_bits.num = num_bits;
	wa_redo->wa_act.ac_op_bits.pos = pos;
	AD_TX_ACT_ADD(tx, wa_redo);

	return 0;
}

/**
 * memset a storage region, save the operation for redo
 */
int
dav_wal_tx_set(void *hdl, void *addr, char c, daos_size_t size)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct dav_tx		*tx = utx2wtx(dav_hdl->do_utx);
	struct wal_action	*wa_redo;
	int                      rc;

	D_ASSERT(hdl != NULL);

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	rc = umem_cache_touch(dav_hdl->do_store, dav_hdl->do_utx->utx_id,
			      mdblob_addr2offset(tx->wt_dav_hdl, addr), size);
	if (rc != 0)
		return rc;

	D_ALLOC_ACT(wa_redo, UMEM_ACT_SET, size);
	if (wa_redo == NULL)
		return -DER_NOMEM;

	wa_redo->wa_act.ac_set.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	wa_redo->wa_act.ac_set.size = size;
	wa_redo->wa_act.ac_set.val = c;
	AD_TX_ACT_ADD(tx, wa_redo);
	return 0;
}

/**
 * query action number in redo list.
 */
uint32_t
wal_tx_act_nr(struct umem_wal_tx *utx)
{
	struct dav_tx *tx = utx2wtx(utx);

	return tx->wt_redo_cnt;
}

/**
 * query payload length in redo list.
 */
uint32_t
wal_tx_payload_len(struct umem_wal_tx *utx)
{
	struct dav_tx *tx = utx2wtx(utx);

	return tx->wt_redo_payload_len;
}

/**
 * get first action pointer, NULL for list empty.
 */
struct umem_action *
wal_tx_act_first(struct umem_wal_tx *utx)
{
	struct dav_tx *tx = utx2wtx(utx);

	if (d_list_empty(&tx->wt_redo)) {
		tx->wt_redo_act_pos = NULL;
		return NULL;
	}

	tx->wt_redo_act_pos = dav_action_get_next(tx->wt_redo);
	return &tx->wt_redo_act_pos->wa_act;
}

/**
 * get next action pointer, NULL for done or list empty.
 */
struct umem_action *
wal_tx_act_next(struct umem_wal_tx *utx)
{
	struct dav_tx *tx = utx2wtx(utx);

	if (tx->wt_redo_act_pos == NULL) {
		if (d_list_empty(&tx->wt_redo))
			return NULL;
		tx->wt_redo_act_pos = dav_action_get_next(tx->wt_redo);
		return &tx->wt_redo_act_pos->wa_act;
	}

	D_ASSERT(!d_list_empty(&tx->wt_redo));
	tx->wt_redo_act_pos = dav_action_get_next(tx->wt_redo_act_pos->wa_link);
	if (&tx->wt_redo_act_pos->wa_link == &tx->wt_redo) {
		tx->wt_redo_act_pos = NULL;
		return NULL;
	}
	return &tx->wt_redo_act_pos->wa_act;
}

struct umem_wal_tx_ops dav_wal_tx_ops = {
	.wtx_act_nr = wal_tx_act_nr,
	.wtx_payload_sz = wal_tx_payload_len,
	.wtx_act_first = wal_tx_act_first,
	.wtx_act_next = wal_tx_act_next,
};

struct dav_wal_replay_cache {
	uint64_t                last_txid;
	int                     capacity;
	int                     cur_pos;
	struct umem_pin_handle *pinhdl[];
};

static inline void *
dav_wal_replay_heap_off2ptr(dav_obj_t *dav_hdl, uint64_t off)
{
	uint32_t                     z_id = OFFSET_TO_ZID(off);
	struct umem_cache_range      rg   = {0};
	int                          rc;
	struct umem_store           *store = dav_hdl->do_store;
	struct dav_wal_replay_cache *dwrc  = (struct dav_wal_replay_cache *)dav_hdl->do_cb_wa;
	struct umem_pin_handle      *pin_handle;

	if (!umem_cache_offispinned(store, off)) {
		rg.cr_off  = GET_ZONE_OFFSET(z_id);
		rg.cr_size = ((store->stor_size - rg.cr_off) > ZONE_MAX_SIZE)
				 ? ZONE_MAX_SIZE
				 : (store->stor_size - rg.cr_off);
		rc         = umem_cache_pin(store, &rg, 1, 0, &pin_handle);
		if (rc) {
			D_ERROR("Failed to load pages to umem cache");
			errno = daos_der2errno(rc);
			return NULL;
		}
		D_ASSERT(dwrc->capacity > dwrc->cur_pos);
		dwrc->pinhdl[dwrc->cur_pos++] = pin_handle;
	}
	return umem_cache_off2ptr(store, off);
}

static inline void
dav_wal_replay_check_txid(dav_obj_t *dav_hdl, uint64_t tx_id)
{
	struct dav_wal_replay_cache *dwrc  = (struct dav_wal_replay_cache *)dav_hdl->do_cb_wa;
	struct umem_store           *store = dav_hdl->do_store;
	int                          i;

	if (tx_id == dwrc->last_txid)
		return;

	if (dwrc->last_txid)
		umem_cache_commit(store, dwrc->last_txid);

	for (i = 0; i < dwrc->cur_pos; i++)
		umem_cache_unpin(store, dwrc->pinhdl[i]);

	dwrc->cur_pos   = 0;
	dwrc->last_txid = tx_id;

	return;
}

int
dav_wal_replay_cb(uint64_t tx_id, struct umem_action *act, void *arg)
{
	void *src, *dst;
	ptrdiff_t off;
	uint64_t *p, mask;
	daos_size_t size;
	int pos, num, val;
	int rc = 0;
	dav_obj_t         *dav_hdl = arg;
	struct umem_store *store   = dav_hdl->do_store;

	dav_wal_replay_check_txid(dav_hdl, tx_id);
	switch (act->ac_opc) {
	case UMEM_ACT_COPY:
		D_DEBUG(DB_TRACE,
			"ACT_COPY txid=%lu, (p,o)=%lu,%lu size=%lu\n",
			tx_id,
			act->ac_copy.addr / PAGESIZE, act->ac_copy.addr % PAGESIZE,
			act->ac_copy.size);
		off  = act->ac_copy.addr;
		src = (void *)&act->ac_copy.payload;
		size = act->ac_copy.size;
		dst  = dav_wal_replay_heap_off2ptr(dav_hdl, off);
		if (dst == NULL) {
			rc = daos_errno2der(errno);
			goto out;
		}
		memcpy(dst, src, size);
		break;
	case UMEM_ACT_ASSIGN:
		D_DEBUG(DB_TRACE,
			"ACT_ASSIGN txid=%lu, (p,o)=%lu,%lu size=%u\n",
			tx_id,
			act->ac_assign.addr / PAGESIZE, act->ac_assign.addr % PAGESIZE,
			act->ac_assign.size);
		off = act->ac_assign.addr;
		dst = dav_wal_replay_heap_off2ptr(dav_hdl, off);
		if (dst == NULL) {
			rc = daos_errno2der(errno);
			goto out;
		}
		size = act->ac_assign.size;
		ASSERT_rt(size == 1 || size == 2 || size == 4);
		src = &act->ac_assign.val;
		memcpy(dst, src, size);
		break;
	case UMEM_ACT_SET:
		D_DEBUG(DB_TRACE,
			"ACT_SET txid=%lu, (p,o)=%lu,%lu size=%u val=%u\n",
			tx_id,
			act->ac_set.addr / PAGESIZE, act->ac_set.addr % PAGESIZE,
			act->ac_set.size, act->ac_set.val);
		off = act->ac_set.addr;
		dst = dav_wal_replay_heap_off2ptr(dav_hdl, off);
		if (dst == NULL) {
			rc = daos_errno2der(errno);
			goto out;
		}
		size = act->ac_set.size;
		val = act->ac_set.val;
		memset(dst, val, size);
		break;
	case UMEM_ACT_SET_BITS:
	case UMEM_ACT_CLR_BITS:
		D_DEBUG(DB_TRACE,
			"ACT_CLR_BITS txid=%lu, (p,o)=%lu,%lu bit_pos=%u num_bits=%u\n",
			tx_id,
			act->ac_op_bits.addr / PAGESIZE, act->ac_op_bits.addr % PAGESIZE,
			act->ac_op_bits.pos, act->ac_op_bits.num);
		off = act->ac_op_bits.addr;
		size = sizeof(uint64_t);
		p    = dav_wal_replay_heap_off2ptr(dav_hdl, off);
		if (p == NULL) {
			rc = daos_errno2der(errno);
			goto out;
		}
		num = act->ac_op_bits.num;
		pos = act->ac_op_bits.pos;
		ASSERT_rt((pos >= 0) && (pos + num) <= 64);
		mask = ((1ULL << num) - 1) << pos;
		if (act->ac_opc == UMEM_ACT_SET_BITS)
			*p |= mask;
		else
			*p &= ~mask;
		break;
	default:
		D_ASSERT(0);
		break;
	}

	if (rc == 0)
		rc = umem_cache_touch(store, tx_id, off, size);

out:
	return rc;
}

int
dav_wal_replay(dav_obj_t *hdl, uint32_t mem_pages)
{
	int                          rc;
	struct dav_wal_replay_cache *dwrc;

	umem_cache_set_early_boot(hdl->do_store, true);
	D_ALLOC(hdl->do_cb_wa,
		sizeof(struct dav_wal_replay_cache) + sizeof(struct umem_pin_handle *) * mem_pages);
	if (hdl->do_cb_wa == NULL) {
		D_ERROR("Failed to allocate for pinhdl_vec");
		rc = ENOMEM;
		goto out;
	}
	dwrc           = (struct dav_wal_replay_cache *)hdl->do_cb_wa;
	dwrc->capacity = mem_pages;

	rc = hdl->do_store->stor_ops->so_wal_replay(hdl->do_store, dav_wal_replay_cb, hdl);
	dav_wal_replay_check_txid(hdl, (-1UL));
	D_FREE(hdl->do_cb_wa);

out:
	umem_cache_set_early_boot(hdl->do_store, false);
	return rc;
}
