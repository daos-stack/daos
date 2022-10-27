/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos_srv/ad_mem.h>
#include "ad_mem.h"

static __thread struct ad_tx	*tls_tx;

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
act_item_add(struct ad_tx *tx, struct umem_act_item *it, int undo_or_redo)
{
	if (undo_or_redo == ACT_UNDO) {
		D_DEBUG(DB_TRACE, "Add to undo %s\n", act_opc2str(it->it_act.ac_opc));
		d_list_add(&it->it_link, &tx->tx_undo);

	} else {
		D_DEBUG(DB_TRACE, "Add to redo %s\n", act_opc2str(it->it_act.ac_opc));
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
act_copy_payload(struct umem_action *act, const void *addr, daos_size_t size)
{
	char	*dst = (char *)&act->ac_copy.payload[0];

	if (size > 0)
		memcpy(dst, addr, size);
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
	D_INIT_LIST_HEAD(&tx->tx_gp_free);

	tx->tx_redo_act_nr	= 0;
	tx->tx_redo_payload_len	= 0;
	tx->tx_redo_act_pos	= NULL;

	tx->tx_layer = 1;
	tx->tx_last_errno = 0;

	return rc;
}

static int
ad_act_item_replay(struct ad_tx *tx, struct umem_action *act)
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
		D_ERROR("Failed to replay %s, "DF_RC"\n", act_opc2str(act->ac_opc), DP_RC(rc));

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

	rc = tx_complete(tx, err);
	if (rc)
		ad_tx_act_replay(tx, &tx->tx_undo);

	ad_tx_act_cleanup(&tx->tx_undo);
	ad_tx_act_cleanup(&tx->tx_redo);

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
		act_item_add(tx, it_undo, ACT_UNDO);
	} else {
		struct umem_act_item	*it_redo = NULL;

		D_ALLOC_ACT(it_redo, UMEM_ACT_COPY, size);
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
		struct umem_act_item *it_undo;

		D_ALLOC_ACT(it_undo, UMEM_ACT_COPY, size);
		if (it_undo == NULL)
			return -DER_NOMEM;

		act_copy_payload(&it_undo->it_act, addr, size);
		it_undo->it_act.ac_copy.addr = blob_ptr2addr(tx->tx_blob, addr);
		it_undo->it_act.ac_copy.size = size;
		act_item_add(tx, it_undo, ACT_UNDO);

	} else {
		struct umem_act_item *it_redo;

		if (!(flags & AD_TX_REDO))
			return -DER_INVAL;

		if (flags & AD_TX_COPY_PTR) {
			D_ALLOC_ACT(it_redo, UMEM_ACT_COPY_PTR, size);
			if (it_redo == NULL)
				return -DER_NOMEM;

			it_redo->it_act.ac_copy_ptr.addr = blob_ptr2addr(tx->tx_blob, addr);
			it_redo->it_act.ac_copy_ptr.ptr = (uintptr_t)ptr;
			it_redo->it_act.ac_set.size = size;
		} else {
			D_ALLOC_ACT(it_redo, UMEM_ACT_COPY, size);
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
	act_item_add(tx, it_undo, ACT_UNDO);

	assign_integer(addr, size, val);

	D_ALLOC_ACT(it_redo, UMEM_ACT_ASSIGN, size);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_assign.addr = blob_ptr2addr(tx->tx_blob, addr);
	it_redo->it_act.ac_assign.size = size;
	it_redo->it_act.ac_assign.val = val;
	act_item_add(tx, it_redo, ACT_REDO);

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
		act_item_add(tx, it_undo, ACT_UNDO);
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
	struct umem_act_item	*it_undo, *it_redo;
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

	D_ALLOC_ACT(it_undo, UMEM_ACT_CLR_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_undo, ACT_UNDO);

	setbit_range(bmap, pos, end);

	D_ALLOC_ACT(it_redo, UMEM_ACT_SET_BITS, 0);
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
	struct umem_act_item	*it_undo, *it_redo;
	uint32_t		 end = pos + nbits - 1;

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

	D_ALLOC_ACT(it_undo, UMEM_ACT_SET_BITS, 0);
	if (it_undo == NULL)
		return -DER_NOMEM;

	it_undo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_undo->it_act.ac_op_bits.pos = pos;
	it_undo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_undo, ACT_UNDO);

	clrbit_range(bmap, pos, end);

	D_ALLOC_ACT(it_redo, UMEM_ACT_CLR_BITS, 0);
	if (it_redo == NULL)
		return -DER_NOMEM;

	it_redo->it_act.ac_op_bits.addr = blob_ptr2addr(tx->tx_blob, bmap);
	it_redo->it_act.ac_op_bits.pos = pos;
	it_redo->it_act.ac_op_bits.num = nbits;
	act_item_add(tx, it_redo, ACT_REDO);

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

static int
tx_end(struct ad_tx *tx, int err)
{
	struct umem_tx	*utx;
	int		 rc;

	if (err == 0)
		err = tx->tx_last_errno;
	else
		tx->tx_last_errno = err;

	D_ASSERTF(tx->tx_layer >= 0, "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);
	if (tx->tx_layer != 0)
		return 0;

	rc = ad_tx_end(tx, err);
	if (err == 0) {
		if (rc == 0) {
			ad_tx_stage_set(tx, UMEM_STAGE_ONCOMMIT);
		} else {
			D_ERROR("ad_tx_end(%d) failed, "DF_RC"\n", err, DP_RC(rc));
			tx->tx_last_errno = rc;
			ad_tx_stage_set(tx, UMEM_STAGE_ONABORT);
		}
	} else {
		D_ASSERT(rc != 0);
		D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_ONABORT,
			  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));
	}

	tx_callback(tx);
	if (tx_get() == tx)
		tx_set(NULL);
	/* trigger UMEM_STAGE_NONE callback, this TX is finished but possibly with other WIP TX */
	ad_tx_stage_set(tx, UMEM_STAGE_NONE);
	tx_callback(tx);
	rc = tx->tx_last_errno;
	utx = ad_tx2umem_tx(tx);
	D_FREE(utx);

	return rc;
}

static int
tx_abort(struct ad_tx *tx, int err)
{
	if (err == 0)
		err = -DER_CANCELED;

	ad_tx_stage_set(tx, UMEM_STAGE_ONABORT);

	return tx_end(tx, err);
}

int
mo_ad_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	struct umem_tx		*utx = NULL;
	struct ad_tx		*tx;
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	struct ad_blob		*blob;
	struct umem_store	*store;
	uint64_t		 tx_id;
	int			 rc = 0;

	blob = bh.bh_blob;
	tx = tx_get();
	if (tx == NULL) {
		D_ALLOC_PTR(utx);
		if (utx == NULL)
			return -DER_NOMEM;

		tx = umem_tx2ad_tx(utx);
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
		tx->tx_stage_cb = umem_stage_callback;
		tx->tx_stage_cb_arg = txd;
		ad_tx_id_set(tx, tx_id);
		ad_tx_stage_set(tx, UMEM_STAGE_WORK);
		tx_set(tx);
		D_DEBUG(DB_TRACE, "TX "DF_U64" started\n", tx_id);
	} else if (ad_tx_stage(tx) == UMEM_STAGE_WORK) {
		if (blob != tx->tx_blob) {
			D_ERROR("Nested TX for different blob\n");
			rc = -DER_INVAL;
			goto err_abort;
		}
		if (tx->tx_stage_cb_arg != txd) {
			D_ERROR("Cannot set different TX callback argument\n");
			rc = -DER_INVAL;
			goto err_abort;
		}
		tx->tx_layer++;
		D_DEBUG(DB_TRACE, "Nested TX "DF_U64", layer %d\n", ad_tx_id(tx), tx->tx_layer);
	} else {
		D_ERROR("Invalid stage %d to begin new tx\n", ad_tx_stage(tx));
		rc = -DER_INVAL;
		goto err_abort;
	}

	return 0;

err_abort:
	D_ASSERT(rc != 0);
	D_ASSERT(tx != NULL);
	rc = tx_abort(tx, rc);
	return rc;
}

int
mo_ad_tx_abort(struct umem_instance *umm, int err)
{
	struct ad_tx	*tx = tx_get();

	D_ASSERTF(tx->tx_layer > 0,
		  "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);
	tx->tx_layer--;

	return tx_abort(tx, err);
}

int
mo_ad_tx_commit(struct umem_instance *umm)
{
	struct ad_tx	*tx = tx_get();
	int		 rc = 0;

	D_ASSERTF(tx->tx_layer > 0,
		  "TX "DF_U64", bad layer %d\n", ad_tx_id(tx), tx->tx_layer);
	tx->tx_layer--;
	if (tx->tx_layer == 0) {
		/* possibly yield when tx_end() -> ad_tx_end() -> tx_complete() -> so_wal_submit */
		tx_set(NULL);
		rc = tx_end(tx, 0);
	}

	return rc;
}

int
mo_ad_tx_stage(void)
{
	struct ad_tx	*tx = tx_get();

	/* XXX when return UMEM_STAGE_NONE possibly with TX in committing */
	return (tx == NULL) ? UMEM_STAGE_NONE : ad_tx_stage(tx);
}

int
mo_ad_tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	struct ad_tx	*tx = tx_get();
	int		 rc = 0;

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

umem_off_t
mo_ad_tx_alloc(struct umem_instance *umm, size_t size, int slab_id, uint64_t flags,
	       unsigned int type_num)
{
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);

	return ad_alloc(bh, 0, size, NULL);
}

int
mo_ad_tx_add(struct umem_instance *umm, umem_off_t umoff, uint64_t offset, size_t size)
{
	struct ad_tx		*tx = tx_get();
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);
	struct ad_blob		*blob = bh.bh_blob;
	void			*ptr;

	D_ASSERT(offset == 0);
	D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
		  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));
	ptr = blob_addr2ptr(blob, umoff);
	return ad_tx_snap(tx, ptr, size, AD_TX_UNDO);
}

int
mo_ad_tx_xadd(struct umem_instance *umm, umem_off_t umoff, uint64_t offset, size_t size,
	      uint64_t flags)
{
	/* NOOP for UMEM_XADD_NO_SNAPSHOT */
	if (flags & UMEM_XADD_NO_SNAPSHOT)
		return 0;

	return mo_ad_tx_add(umm, umoff, offset, size);
}

int
mo_ad_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	struct ad_tx		*tx = tx_get();

	D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
		  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));

	return ad_tx_snap(tx, ptr, size, AD_TX_UNDO);
}

umem_off_t
mo_ad_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num)
{
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);

	return ad_reserve(bh, 0, size, NULL, (struct ad_reserv_act *)act);
}

void
mo_ad_cancel(struct umem_instance *umm, void *actv, int actv_cnt)
{
	ad_cancel((struct ad_reserv_act *)actv, actv_cnt);
}

int
mo_ad_tx_publish(struct umem_instance *umm, void *actv, int actv_cnt)
{
	struct ad_tx		*tx = tx_get();

	D_ASSERTF(ad_tx_stage(tx) == UMEM_STAGE_WORK,
		  "TX "DF_U64", bad stage %d\n", ad_tx_id(tx), ad_tx_stage(tx));

	return ad_tx_publish(tx, (struct ad_reserv_act *)actv, actv_cnt);
}

void *
mo_ad_atomic_copy(struct umem_instance *umm, void *dest, const void *src, size_t len)
{
	struct ad_tx	*tx = tx_get();
	bool		 tx_started = false;
	int		 rc = 0;

	if (tx == NULL) {
		rc = mo_ad_tx_begin(umm, NULL);
		if (rc) {
			D_ERROR("mo_ad_tx_begin failed, "DF_RC"\n", DP_RC(rc));
			return NULL;
		}
		tx_started = true;
		tx = tx_get();
	}

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

	if (tx_started)
		rc = mo_ad_tx_commit(umm);

	return rc == 0 ? dest: NULL;

failed:
	D_ASSERT(rc != 0);
	if (tx_started)
		mo_ad_tx_abort(umm, rc);

	return NULL;
}

umem_off_t
mo_ad_atomic_alloc(struct umem_instance *umm, size_t size, unsigned int type_num)
{
	struct ad_blob_handle	 bh = umm2ad_blob_hdl(umm);

	return ad_alloc(bh, 0, size, NULL);
}

int
mo_ad_atomic_free(struct umem_instance *umm, umem_off_t umoff)
{
	struct ad_tx		*tx = tx_get();
	bool			 tx_started = false;
	int			 rc = 0;

	if (tx == NULL) {
		rc = mo_ad_tx_begin(umm, NULL);
		if (rc) {
			D_ERROR("mo_ad_tx_begin failed, "DF_RC"\n", DP_RC(rc));
			return rc;
		}
		tx_started = true;
		tx = tx_get();
	}

	rc = ad_tx_free(tx, umoff);
	if (rc) {
		D_ERROR("ad_tx_free failed, "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	if (tx_started)
		rc = mo_ad_tx_commit(umm);

	return rc;

failed:
	D_ASSERT(rc != 0);
	if (tx_started)
		rc = mo_ad_tx_abort(umm, rc);

	return rc;
}

uint32_t
mo_ad_tx_act_nr(struct umem_tx *utx)
{
	return ad_tx_redo_act_nr(umem_tx2ad_tx(utx));
}

uint32_t
mo_ad_tx_payload_sz(struct umem_tx *utx)
{
	return ad_tx_redo_payload_len(umem_tx2ad_tx(utx));
}

struct umem_action *
mo_ad_tx_act_first(struct umem_tx *utx)
{
	return ad_tx_redo_act_first(umem_tx2ad_tx(utx));
}

struct umem_action *
mo_ad_tx_act_next(struct umem_tx *utx)
{
	return ad_tx_redo_act_next(umem_tx2ad_tx(utx));
}
