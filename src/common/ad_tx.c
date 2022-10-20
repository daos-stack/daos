/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos_srv/ad_mem.h>
#include "ad_mem.h"

enum {
	ACT_UNDO = 0,
	ACT_REDO = 1,
};

#define AD_TX_ACT_ADD(tx, it, undo_or_redo)						\
	do {										\
		if (undo_or_redo == ACT_UNDO) {						\
			d_list_add(&(it)->it_link, &(tx)->tx_undo);			\
		} else {								\
			d_list_add_tail(&(it)->it_link, &(tx)->tx_redo);		\
			(tx)->tx_redo_act_nr++;						\
			if ((it)->it_act.ac_opc == UMEM_ACT_COPY ||			\
			    (it)->it_act.ac_opc == UMEM_ACT_COPY_PTR) {			\
				(tx)->tx_redo_payload_len += (it)->it_act.ac_copy.size;	\
			} else if ((it)->it_act.ac_opc == UMEM_ACT_MOVE) {		\
				/* ac_move src addr is playload after wal_trans_entry */\
				(tx)->tx_redo_payload_len += sizeof(uint64_t);		\
			}								\
		}									\
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
	if (size > 0)
		memcpy(&act->ac_copy.payload[0], addr, size);
}

/** start a ad-hoc memory transaction */
int
ad_tx_begin(struct ad_blob_handle bh, struct ad_tx *tx)
{
	static __thread uint64_t	tx_id;
	int				rc = 0;

	blob_addref(bh.bh_blob);
	tx->tx_blob = bh.bh_blob;
	tx->tx_id = ++tx_id;
	D_INIT_LIST_HEAD(&tx->tx_redo);
	D_INIT_LIST_HEAD(&tx->tx_undo);
	D_INIT_LIST_HEAD(&tx->tx_ar_pub);
	D_INIT_LIST_HEAD(&tx->tx_gp_pub);
	D_INIT_LIST_HEAD(&tx->tx_gp_free);

	tx->tx_redo_act_nr	= 0;
	tx->tx_redo_payload_len	= 0;
	tx->tx_redo_act_pos	= NULL;

	return rc;
}

static int
ad_act_item_replay(struct ad_tx *tx, struct umem_action *act)
{
	int rc = 0;

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
				  act->ac_assign.size, act->ac_assign.val);
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
		D_ERROR("Failed to replay act->ac_opc %d, "DF_RC"\n", act->ac_opc, DP_RC(rc));

	return rc;
}

static int
ad_tx_act_replay(struct ad_tx *tx, d_list_t *list)
{
	struct umem_act_item	*it;
	int			 rc = 0;

	d_list_for_each_entry(it, list, it_link) {
		rc = ad_act_item_replay(tx, &it->it_act);
		if (rc)
			break;
	}

	return rc;
}

static void
ad_tx_act_cleanup(d_list_t *list)
{
	struct umem_act_item	*it, *next;

	d_list_for_each_entry_safe(it, next, list, it_link) {
		d_list_del(&it->it_link);
		D_FREE(it);
	}
}

/** complete a ad-hoc memory transaction */
int
ad_tx_end(struct ad_tx *tx, int err)
{
	int	rc = 0;

	if (err)
		rc = ad_tx_act_replay(tx, &tx->tx_undo);

	/* TODO write actions in redo list to WAL */
	rc = tx_complete(tx, err);
	if (rc == 0) {
		ad_tx_act_cleanup(&tx->tx_undo);
		ad_tx_act_cleanup(&tx->tx_redo);
	}
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
		struct umem_act_item	*it_undo;

		D_ALLOC_ACT(it_undo, UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);
	} else {
		struct umem_act_item	*it_redo = NULL;

		D_ALLOC_ACT(it_redo, UMEM_ACT_COPY, size);
		if (it_redo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_redo->it_act, addr, size);
		it_redo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_redo->it_act.ac_copy.size = size;
		AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);
	}
	return 0;
}

/**
 * copy data from buffer @ptr to storage address @addr, both old and new data can be saved
 * for TX redo and undo.
 */
int
ad_tx_copy(struct ad_tx *tx, void *addr, daos_size_t size, void *ptr, uint32_t flags)
{
	struct umem_act_item	*it_undo = NULL, *it_redo = NULL;
	bool			 ptr_only = flags & AD_TX_COPY_PTR;
	bool			 undo = flags & AD_TX_UNDO;
	bool			 redo = flags & AD_TX_REDO;

	if (addr == NULL || ptr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		memcpy(addr, ptr, size);
		return 0;
	}

	if (undo) {
		D_ALLOC_ACT(it_undo, UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;
	}

	if (redo) {
		if (ptr_only)
			D_ALLOC_ACT(it_redo, UMEM_ACT_COPY_PTR, size);
		else
			D_ALLOC_ACT(it_redo, UMEM_ACT_COPY, size);
		if (it_redo == NULL && it_undo != NULL) {
			D_FREE(it_undo);
			return -DER_NOMEM;
		}
	}

	if (undo) {
		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);
	}

	if (redo) {
		if (ptr_only) {
			it_redo->it_act.ac_copy_ptr.addr = blob_ptr2addr(tx->tx_blob, addr);
			it_redo->it_act.ac_copy_ptr.ptr = (uintptr_t)ptr;
			it_redo->it_act.ac_set.size = size;
		} else {
			act_copy_payload(&it_redo->it_act, ptr, size);
			it_redo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
			it_redo->it_act.ac_copy.size = size;
		}
		AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);
	}

	return 0;
}

static int
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
ad_tx_assign(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t val)
{
	struct umem_act_item	*it_undo, *it_redo;

	if (addr == NULL || (size != 1 && size != 2 && size != 4))
		return -DER_INVAL;

	if (tx == NULL) {
		assign_integer(addr, size, val);
		return 0;
	}

	D_ALLOC_ACT(it_undo, UMEM_ACT_ASSIGN, size);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_assign.addr = blob_ptr2addr(tx->tx_blob, addr);
	it_undo->it_act.ac_assign.size = size;
	assign_integer(&it_undo->it_act.ac_assign.val, size, get_integer(addr, size));
	AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);

	assign_integer(addr, size, val);

	D_ALLOC_ACT(it_redo, UMEM_ACT_ASSIGN, size);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_assign.addr = blob_ptr2addr(tx->tx_blob, addr);
	it_redo->it_act.ac_assign.size = size;
	it_undo->it_act.ac_assign.val = val;
	AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);

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
	struct umem_act_item	*it_undo, *it_redo;

	if (addr == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		if (!(flags & AD_TX_LOG_ONLY))
			memset(addr, c, size);
		return 0;
	}

	if (flags & AD_TX_UNDO) {
		D_ALLOC_ACT(it_undo, UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);
	}

	if (!(flags & AD_TX_LOG_ONLY))
		memset(addr, c, size);

	if (flags & AD_TX_REDO) {
		D_ALLOC_ACT(it_redo, UMEM_ACT_SET, size);
		if (it_redo == NULL)
			return -DER_NOMEM;

		it_redo->it_act.ac_set.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_redo->it_act.ac_set.size = size;
		it_redo->it_act.ac_set.val = c;
		AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);
	}
	return 0;
}

/**
 * memmove a storage region, save the operation for redo and old memory content for undo.
 */
int
ad_tx_move(struct ad_tx *tx, void *dst, void *src, daos_size_t size)
{
	struct umem_act_item	*it_undo, *it_redo;

	if (dst == NULL || src == NULL || size == 0 || size > UMEM_ACT_PAYLOAD_MAX_LEN)
		return -DER_INVAL;

	if (tx == NULL) {
		memmove(dst, src, size);
		return 0;
	}

	D_ALLOC_ACT(it_undo, UMEM_ACT_COPY, size);
	if (it_undo == NULL)
		return -DER_NOMEM;

	D_ALLOC_ACT(it_redo, UMEM_ACT_MOVE, size);
	if (it_redo == NULL) {
		D_FREE(it_undo);
		return -DER_NOMEM;
	}

	act_copy_payload(&it_undo->it_act, dst, size);
	it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, dst);
	it_undo->it_act.ac_copy.size = size;
	AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);

	it_redo->it_act.ac_move.dst = blob_ptr2addr(tx->tx_blob, dst);
	it_redo->it_act.ac_move.src = blob_ptr2addr(tx->tx_blob, src);
	it_redo->it_act.ac_move.size = size;
	AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);

	return 0;
}

/** setbit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_setbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits)
{
	struct umem_act_item	*it_undo, *it_redo;
	uint32_t		 end = pos + nbits - 1;

	if (bmap == NULL)
		return -DER_INVAL;

	/* if use cases cannot satisfy this requirement, need to add copybits action for undo */
	if (!isclr_range(bmap, pos, end)) {
		D_ERROR("bitmap already set in the range.\n");
		return -DER_INVAL;
	}

	if (tx == NULL) {
		setbit_range(bmap, pos, end);
		return 0;
	}

	D_ALLOC_ACT(it_undo, UMEM_ACT_CLR_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);

	setbit_range(bmap, pos, end);

	D_ALLOC_ACT(it_redo, UMEM_ACT_SET_BITS, 0);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_redo->it_act.ac_op_bits.pos = pos;
	it_redo->it_act.ac_op_bits.num = nbits;
	AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);

	return 0;
}

/** clear bit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_clrbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits)
{
	struct umem_act_item	*it_undo, *it_redo;
	uint32_t		 end = pos + nbits - 1;

	if (bmap == NULL)
		return -DER_INVAL;

	/* if use cases cannot satisfy this requirement, need to add copybits action for undo */
	if (!isset_range(bmap, pos, end)) {
		D_ERROR("bitmap already cleared in the range.\n");
		return -DER_INVAL;
	}

	if (tx == NULL) {
		clrbit_range(bmap, pos, end);
		return 0;
	}

	D_ALLOC_ACT(it_undo, UMEM_ACT_SET_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	AD_TX_ACT_ADD(tx, it_undo, ACT_UNDO);

	clrbit_range(bmap, pos, end);

	D_ALLOC_ACT(it_redo, UMEM_ACT_CLR_BITS, 0);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_redo->it_act.ac_op_bits.pos = pos;
	it_redo->it_act.ac_op_bits.num = nbits;
	AD_TX_ACT_ADD(tx, it_redo, ACT_REDO);

	return 0;
}

/**
 * query action number in redo list.
 */
uint32_t
ad_tx_redo_act_nr(struct ad_tx *tx)
{
	return tx->tx_redo_act_nr;
}

/**
 * query payload length in redo list.
 */
uint32_t
ad_tx_redo_payload_len(struct ad_tx *tx)
{
	return tx->tx_redo_payload_len;
}

/**
 * get first action pointer, NULL for list empty.
 */
struct umem_action *
ad_tx_redo_act_first(struct ad_tx *tx)
{
	if (d_list_empty(&tx->tx_redo)) {
		tx->tx_redo_act_pos = NULL;
		return NULL;
	}

	tx->tx_redo_act_pos = d_list_entry(&tx->tx_redo.next, struct umem_act_item, it_link);
	return &tx->tx_redo_act_pos->it_act;
}

/**
 * get next action pointer, NULL for done or list empty.
 */
struct umem_action *
ad_tx_redo_act_next(struct ad_tx *tx)
{
	if (tx->tx_redo_act_pos == NULL) {
		if (d_list_empty(&tx->tx_redo))
			return NULL;
		tx->tx_redo_act_pos = d_list_entry(&tx->tx_redo.next, struct umem_act_item,
						   it_link);
		return &tx->tx_redo_act_pos->it_act;
	}

	D_ASSERT(!d_list_empty(&tx->tx_redo));
	tx->tx_redo_act_pos = d_list_entry(&tx->tx_redo_act_pos->it_link.next,
					   struct umem_act_item, it_link);
	if (&tx->tx_redo_act_pos->it_link == &tx->tx_redo) {
		tx->tx_redo_act_pos = NULL;
		return NULL;
	}
	return &tx->tx_redo_act_pos->it_act;
}
