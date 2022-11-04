/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include "common.h"
#include "dav_internal.h"
#include "wal_tx.h"
#include "util.h"

static inline uint64_t
mdblob_addr2offset(struct dav_obj *hdl, void *addr)
{
	D_ASSERT(((uintptr_t)addr >= (uintptr_t)hdl->do_base) &&
		 ((uintptr_t)addr <= ((uintptr_t)hdl->do_base + hdl->do_size)));
	return (uintptr_t)addr - (uintptr_t)hdl->do_base;
}

#define AD_TX_ACT_ADD(tx, it)							\
	do {									\
		d_list_add_tail(&(it)->it_link, &(tx)->wt_redo);		\
		(tx)->wt_redo_cnt++;						\
		if ((it)->it_act.ac_opc == UMEM_ACT_COPY ||			\
		    (it)->it_act.ac_opc == UMEM_ACT_COPY_PTR) {			\
			(tx)->wt_redo_payload_len += (it)->it_act.ac_copy.size;	\
		} else if ((it)->it_act.ac_opc == UMEM_ACT_MOVE) {		\
			/* ac_move src addr is playload after wal_trans_entry */\
			(tx)->wt_redo_payload_len += sizeof(uint64_t);		\
		}								\
	} while (0)

#define AD_TX_ACT_DEL(it)	d_list_del(it)

/** allocate umem_act_item, if success the it_link and it_act.ac_opc will be init-ed */
#define D_ALLOC_ACT(it, opc, size)							\
	do {										\
		if (opc == UMEM_ACT_COPY)						\
			D_ALLOC(it, offsetof(struct umem_act_item,			\
					     it_act.ac_copy.payload[size]));		\
		else									\
			D_ALLOC_PTR(it);						\
		if (likely(it != NULL)) {						\
			D_INIT_LIST_HEAD(&it->it_link);					\
			it->it_act.ac_opc = opc;					\
		}									\
	} while (0)

static inline void
act_copy_payload(struct umem_action *act, void *addr, daos_size_t size)
{
	char	*dst = (char *)&act->ac_copy.payload[0];

	if (size > 0)
		memcpy(dst, addr, size);
}

static int
wal_tx_reinit(struct dav_obj *dav_hdl)
{
	struct wal_tx	*tx = &dav_hdl->do_wtx;
	int		 rc = 0;

	if (dav_hdl == NULL) {
		D_FATAL("invalid argument\n");
		return -DER_INVAL;
	}
	tx->wt_id++;
	D_INIT_LIST_HEAD(&tx->wt_redo);
	tx->wt_redo_cnt = 0;
	tx->wt_redo_payload_len = 0;
	tx->wt_redo_act_pos = NULL;
	tx->wt_dav_hdl = dav_hdl;

	return rc;
}

int
wal_tx_init(struct dav_obj *dav_hdl)
{
	D_ASSERT(dav_hdl != NULL);
	memset(&dav_hdl->do_wtx, 0, sizeof(dav_hdl->do_wtx));
	return wal_tx_reinit(dav_hdl);
}

static void
wal_tx_act_cleanup(d_list_t *list)
{
	struct umem_act_item	*it, *next;

	d_list_for_each_entry_safe(it, next, list, it_link) {
		d_list_del(&it->it_link);
		D_FREE(it);
	}
}

#define WAL_PRT_PG_OFF(p) ((p) / PAGESIZE), ((p) % PAGESIZE)
static int
wal_tx_push(struct dav_obj *dav_hdl, d_list_t *redo_list, uint64_t id)
{
	struct umem_act_item    *it, *next;
	char *pathname = basename(dav_hdl->do_path);

	d_list_for_each_entry_safe(it, next, redo_list, it_link) {
		switch (it->it_act.ac_opc) {
		case UMEM_ACT_COPY:
			D_ERROR("%s: UMEM_ACT_COPY     txid=%lu, (p,o)=%lu,%lu size=%lu\n",
				pathname, id,
				WAL_PRT_PG_OFF(it->it_act.ac_copy.addr),
				it->it_act.ac_copy.size);
			break;
		case UMEM_ACT_ASSIGN:
			D_ERROR("%s: UMEM_ACT_ASSIGN   txid=%lu, (p,o)=%lu,%lu size=%lu\n",
				pathname, id,
				WAL_PRT_PG_OFF(it->it_act.ac_assign.addr),
				it->it_act.ac_assign.size);
			break;
		case UMEM_ACT_SET:
			D_ERROR("%s: UMEM_ACT_SET      txid=%lu, (p,o)=%lu,%lu size=%u val=%u\n",
				pathname, id,
				WAL_PRT_PG_OFF(it->it_act.ac_set.addr),
				it->it_act.ac_set.size, it->it_act.ac_set.val);
			break;
		case UMEM_ACT_SET_BITS:
			D_ERROR("%s: UMEM_ACT_SET_BITS txid=%lu, (p,o)=%lu,%lu bit_pos=%u num_bits=%u\n",
				pathname, id,
				WAL_PRT_PG_OFF(it->it_act.ac_op_bits.addr),
				it->it_act.ac_op_bits.pos, it->it_act.ac_op_bits.num);
			break;
		case UMEM_ACT_CLR_BITS:
			D_ERROR("%s: UMEM_ACT_CLR_BITS txid=%lu, (p,o)=%lu,%lu bit_pos=%u num_bits=%u\n",
				pathname, id, 
				WAL_PRT_PG_OFF(it->it_act.ac_op_bits.addr),
				it->it_act.ac_op_bits.pos, it->it_act.ac_op_bits.num);
			break;
		default:
			D_ERROR("%s: unknown opc %d\n", dav_hdl->do_path, it->it_act.ac_opc);
			ASSERT(0);
		}
	}
	return 0;
}
#undef WAL_PRT_PG_OFF

/** complete the wl transaction */
int
wal_tx_commit(void *hdl)
{
	struct dav_obj	*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx	*tx = &dav_hdl->do_wtx;
	d_list_t	 wt_redo;
	int		 rc = 0;

	D_ASSERT(hdl != NULL);

	D_INIT_LIST_HEAD(&wt_redo);
	d_list_splice_init(&tx->wt_redo, &wt_redo);
	wal_tx_reinit(hdl);

	/* write actions in redo list to WAL */
	rc = wal_tx_push(dav_hdl, &wt_redo, tx->wt_id);
	/* FAIL the engine if commit fails */
	D_ASSERT(rc == 0);
	wal_tx_act_cleanup(&wt_redo);

	return 0;
}

/**
 * snapshot data from src to either wal redo log.
 */
int
wal_tx_snap(void *hdl, void *addr, daos_size_t size, void *src, uint32_t flags)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx		*tx = &dav_hdl->do_wtx;
	struct umem_act_item	*it_redo;

	D_ASSERT(hdl != NULL);

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	D_ALLOC_ACT(it_redo, UMEM_ACT_COPY, size);
	if (it_redo == NULL)
		return -DER_NOMEM;

	act_copy_payload(&it_redo->it_act, src, size);
	it_redo->it_act.ac_copy.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	it_redo->it_act.ac_copy.size = size;
	AD_TX_ACT_ADD(tx, it_redo);
	return 0;
}

/** assign uint64_t value to @addr */
int
wal_tx_assign(void *hdl, void *addr, uint64_t val)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx		*tx = &dav_hdl->do_wtx;
	struct umem_act_item	*it_redo;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	D_ALLOC_ACT(it_redo, UMEM_ACT_ASSIGN, sizeof(uint64_t));
	if (it_redo == NULL)
		return -DER_NOMEM;
	it_redo->it_act.ac_assign.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	it_redo->it_act.ac_assign.size = 8;
	it_redo->it_act.ac_assign.val = val;
	AD_TX_ACT_ADD(tx, it_redo);

	return 0;
}

/** Set bits starting from pos */
int
wal_tx_set_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx		*tx = &dav_hdl->do_wtx;
	struct umem_act_item	*it_redo;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	D_ALLOC_ACT(it_redo, UMEM_ACT_SET_BITS, sizeof(uint64_t));
	if (it_redo == NULL)
		return -DER_NOMEM;
	it_redo->it_act.ac_op_bits.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	it_redo->it_act.ac_op_bits.num = num_bits;
	it_redo->it_act.ac_op_bits.pos = pos;
	AD_TX_ACT_ADD(tx, it_redo);

	return 0;
}

/** Clr bits starting from pos */
int
wal_tx_clr_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx		*tx = &dav_hdl->do_wtx;
	struct umem_act_item	*it_redo;

	D_ASSERT(hdl != NULL);
	if (addr == NULL)
		return -DER_INVAL;

	D_ALLOC_ACT(it_redo, UMEM_ACT_CLR_BITS, sizeof(uint64_t));
	if (it_redo == NULL)
		return -DER_NOMEM;
	it_redo->it_act.ac_op_bits.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	it_redo->it_act.ac_op_bits.num = num_bits;
	it_redo->it_act.ac_op_bits.pos = pos;
	AD_TX_ACT_ADD(tx, it_redo);

	return 0;
}

/**
 * memset a storage region, save the operation for redo
 */
int
wal_tx_set(void *hdl, void *addr, char c, daos_size_t size)
{
	struct dav_obj		*dav_hdl = (struct dav_obj *)hdl;
	struct wal_tx		*tx = &dav_hdl->do_wtx;
	struct umem_act_item	*it_redo;

	D_ASSERT(hdl != NULL);

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	D_ALLOC_ACT(it_redo, UMEM_ACT_SET, size);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_set.addr = mdblob_addr2offset(tx->wt_dav_hdl, addr);
	it_redo->it_act.ac_set.size = size;
	it_redo->it_act.ac_set.val = c;
	AD_TX_ACT_ADD(tx, it_redo);
	return 0;
}

/**
 * query action number in redo list.
 */
uint32_t
wal_tx_act_nr(struct wal_tx *tx)
{
	return tx->wt_redo_cnt;
}

/**
 * query payload length in redo list.
 */
uint32_t
wal_tx_payload_len(struct wal_tx *tx)
{
	return tx->wt_redo_payload_len;
}

/**
 * get first action pointer, NULL for list empty.
 */
struct umem_action *
wal_tx_act_first(struct wal_tx *tx)
{
	if (d_list_empty(&tx->wt_redo)) {
		tx->wt_redo_act_pos = NULL;
		return NULL;
	}

	tx->wt_redo_act_pos = d_list_entry(&tx->wt_redo.next, struct umem_act_item, it_link);
	return &tx->wt_redo_act_pos->it_act;
}

/**
 * get next action pointer, NULL for done or list empty.
 */
struct umem_action *
wal_tx_act_next(struct wal_tx *tx)
{
	if (tx->wt_redo_act_pos == NULL) {
		if (d_list_empty(&tx->wt_redo))
			return NULL;
		tx->wt_redo_act_pos = d_list_entry(&tx->wt_redo.next, struct umem_act_item,
						   it_link);
		return &tx->wt_redo_act_pos->it_act;
	}

	D_ASSERT(!d_list_empty(&tx->wt_redo));
	tx->wt_redo_act_pos = d_list_entry(&tx->wt_redo_act_pos->it_link.next,
					   struct umem_act_item, it_link);
	if (&tx->wt_redo_act_pos->it_link == &tx->wt_redo) {
		tx->wt_redo_act_pos = NULL;
		return NULL;
	}
	return &tx->wt_redo_act_pos->it_act;
}
