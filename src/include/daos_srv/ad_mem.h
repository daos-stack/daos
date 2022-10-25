#ifndef __DAOS_AD_HOC_MEM_H__
#define __DAOS_AD_HOC_MEM_H__

#include <daos/mem.h>

#define AD_ARENA_DEFAULT	0

/* the memory region (blob) manged by ad-hoc allocator */
struct ad_blob;
struct ad_arena;
struct ad_group;

struct ad_blob_handle {
	struct ad_blob	*bh_blob;
};

/* Ad-hoc mem transaction handle */
struct ad_tx;

/** start a ad-hoc memory transaction */
int
ad_tx_begin(struct ad_blob_handle bh, struct ad_tx *tx);

/** complete a ad-hoc memory transaction */
int
ad_tx_end(struct ad_tx *tx, int err);

enum ad_tx_copy_flags {
	AD_TX_UNDO	= (1 << 0),
	AD_TX_REDO	= (1 << 1),
	AD_TX_LOG_ONLY	= (1 << 2),
	AD_TX_COPY_PTR	= (1 << 3),
	AD_TX_SAVE_OLD	= (1 << 4),
	AD_TX_CHECK	= (1 << 5), /* assign zero to it to disable bits check */
};

/**
 * copy data from buffer @ptr to storage address @addr, both old and new data can be saved
 * for TX redo and undo.
 */
int
ad_tx_copy(struct ad_tx *tx, void *addr, daos_size_t size, void *ptr, uint32_t flags);

/** assign integer value to @addr, both old and new value should be saved for redo and undo */
int
ad_tx_assign(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t val);

/**
 * memset a storage region, save the operation for redo (and old value for undo if it's
 * required by @save_old).
 */
int
ad_tx_set(struct ad_tx *tx, void *addr, char c, daos_size_t size, uint32_t flags);

/**
 * memmove a storage region, save the operation for redo and old memory content for undo.
 */
int
ad_tx_move(struct ad_tx *tx, void *dst, void *src, daos_size_t size);

/** setbit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_setbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits);

/** clear bit in the bitmap, save the operation for redo and the reversed operation for undo */
int
ad_tx_clrbits(struct ad_tx *tx, void *bmap, uint32_t pos, uint16_t nbits);

/** create snapshot for the content in @addr, either for redo or undo */
int
ad_tx_snap(struct ad_tx *tx, void *addr, daos_size_t size, uint32_t flags);

/**
 * query action number in redo list.
 */
uint32_t
ad_tx_redo_act_nr(struct ad_tx *tx);

/**
 * query payload length in redo list.
 */
uint32_t
ad_tx_redo_payload_len(struct ad_tx *tx);

/**
 * get first action pointer, NULL for list empty.
 */
struct umem_action *
ad_tx_redo_act_first(struct ad_tx *tx);

/**
 * get next action pointer, NULL for done or list empty.
 */
struct umem_action *
ad_tx_redo_act_next(struct ad_tx *tx);

static inline int
ad_tx_decrease(struct ad_tx *tx, int32_t *addr)
{
	int32_t	val = *(int32_t *)addr;

	return ad_tx_assign(tx, addr, sizeof(val), val - 1);
}

static inline int
ad_tx_increase(struct ad_tx *tx, int32_t *addr)
{
	int32_t	val = *(int32_t *)addr;

	return ad_tx_assign(tx, addr, sizeof(val), val + 1);
}

int ad_blob_prep_create(char *path, daos_size_t size, struct ad_blob_handle *bh);
int ad_blob_post_create(struct ad_blob_handle bh);
int ad_blob_prep_open(char *path, struct ad_blob_handle *bh);
int ad_blob_post_open(struct ad_blob_handle bh);
int ad_blob_close(struct ad_blob_handle bh);
int ad_blob_destroy(struct ad_blob_handle bh);
struct umem_store *ad_blob_hdl2store(struct ad_blob_handle bh);

#define AD_ARENA_ANY	(~0U)

/* XXX reserved for future use */
enum {
	AD_FL_TRY_OTHER		= (1 << 0),
	AD_FL_TRY_HARD		= (1 << 1),
	AD_FL_TRY_ALL		= (1 << 2),
	AD_FL_ALIGN_SZ		= (1 << 3),
	/* could migrate to SSD */
	AD_FL_DATA		= (1 << 4),
};

/** action parameters for reserve() */
struct ad_reserv_act {
	struct ad_arena		*ra_arena;
	struct ad_group		*ra_group;
	/** reserved allocation bit (in group) */
	int			 ra_bit;
};

/** default values of each group */
struct ad_group_spec {
	/** allocation minimum unit size in bytes */
	uint32_t		gs_unit;
	/** number of units in each group */
	uint32_t		gs_count;
};

int ad_arena_register(struct ad_blob_handle bh, unsigned int arena_type,
		      struct ad_group_spec *specs, unsigned int specs_nr, struct ad_tx *tx);
daos_off_t ad_reserve(struct ad_blob_handle bh, int type, daos_size_t size, uint32_t *arena_id,
		      struct ad_reserv_act *act);
int ad_tx_free(struct ad_tx *tx, daos_off_t addr);
int ad_tx_publish(struct ad_tx *tx, struct ad_reserv_act *acts, int act_nr);
void ad_cancel(struct ad_reserv_act *acts, int act_nr);

daos_off_t ad_alloc(struct ad_blob_handle bh, int type, daos_size_t size, uint32_t *arena_id);

void *ad_addr2ptr(struct ad_blob_handle bh, daos_off_t addr);
daos_off_t ad_ptr2addr(struct ad_blob_handle bh, void *ptr);

#endif /* __DAOS_AD_HOC_MEM_H__ */
