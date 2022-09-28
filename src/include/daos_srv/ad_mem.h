#ifndef __DAOS_AD_HOC_MEM_H__
#define __DAOS_AD_HOC_MEM_H__

#include <daos/mem.h>

/* the memory region (blob) manged by ad-hoc allocator */
struct ad_blob;

/* Ad-hoc mem transaction handle */
struct ad_tx {
	struct ad_blob		*tx_blob;
	uint64_t		 tx_id;
	d_list_t		 tx_redo;
	d_list_t		 tx_undo;
	struct {
		void		*tr_addr;
		daos_size_t	 tr_size;
	}			 tx_touch_region;
};

#define ad_ptr2addr(ptr)	((uintptr_t)ptr)
#define ad_addr2ptr(addr)	((void *)addr)

/** start a ad-hoc memory transaction */
int
ad_tx_begin(struct ad_blob *blob, struct ad_tx *tx);

/** complete a ad-hoc memory transaction */
int
ad_tx_end(struct ad_tx *tx, int err);

/**
 * declare this storage region is going to be changed (for redo and undo), it does memset
 * the region if @reset is true.
 */
int
ad_tx_touch_region(struct ad_tx *tx, void *addr, daos_size_t size, bool reset);

/** finished the change */
int
ad_tx_touch_done(struct ad_tx *tx);

/**
 * copy data from buffer @ptr to storage address @addr, both old and new data will be saved
 * for TX redo and undo.
 */
int
ad_tx_copy(struct ad_tx *tx, void *addr, daos_size_t size, void *ptr, bool ptr_only);

/** assign integer value to @addr, both old and new value should be saved for redo and undo */
int
ad_tx_assign(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t val);

/**
 * memset a storage region, save the operation for redo (and old value for undo if it's
 * required by @save_old).
 */
int
ad_tx_set(struct ad_tx *tx, void *addr, char c, daos_size_t size, bool save_old);

/**
 * memmove a storage region, save the operation for redo and old memory content for undo.
 */
int
ad_tx_move(struct ad_tx *tx, void *dst, void *src, daos_size_t size);

/** setbit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_setbit(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits);

/** clear bit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_clrbit(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits);

#endif /* __DAOS_AD_HOC_MEM_H__ */
