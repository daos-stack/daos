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
ad_tx_copy(struct ad_tx *tx, void *addr, daos_size_t size, const void *ptr, uint32_t flags);

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

int ad_blob_create(char *path, unsigned int flags, struct umem_store *store,
		   struct ad_blob_handle *bh);
int ad_blob_open(char *path, unsigned int flags, struct umem_store *store,
		 struct ad_blob_handle *bh);
int ad_blob_close(struct ad_blob_handle bh);
int ad_blob_destroy(struct ad_blob_handle bh);

enum ad_blob_iter_flags {
	AD_ITER_SORT_BY_GROUP_ADDR	= (1 << 0),
	AD_ITER_SORT_BY_GROUP_WEIGHT	= (1 << 1),
};

struct ad_blob_group_info {
	/** unit size in bytes, e.g., 64 bytes, 128 bytes */
	uint32_t	gi_unit;
	/** number of units in this group */
	int32_t		gi_unit_nr;
	/** number of free units in this group */
	uint32_t	gi_unit_free;
};

/** default values of each group */
struct ad_group_spec {
	/** allocation minimum unit size in bytes */
	uint32_t		gs_unit;
	/** number of units in each group */
	uint32_t		gs_count;
};

#define ARENA_GRP_SPEC_MAX	24
#define ARENA_GRP_BMSZ		8

/* register up to 31 arenas types (type=0 is predefined) */
#define ARENA_SPEC_MAX		32

/** Customized specs for arena. */
struct ad_arena_spec {
	/** arena type, default arena type is 0 */
	uint32_t		as_type;
	/** arena unit size, reserved for future use */
	uint32_t		as_unit;
	/* last active arena of this type, this is not really part of spec... */
	uint32_t		as_last_used;
	/** number of group specs (valid members of as_specs) */
	uint32_t		as_specs_nr;
	/** group sizes and number of units within each group */
	struct ad_group_spec	as_specs[ARENA_GRP_SPEC_MAX];
};

struct ad_blob_arena_info {
	/* arena is uninit */
	bool			ai_arena_uninit;
	/** number of groups */
	int			ai_grp_nr;
	/* customized specs for arena */
	struct ad_arena_spec	ai_arena_spec;
	/* number of groups for each spec */
	uint32_t		ai_num_of_each_spec[ARENA_GRP_SPEC_MAX];
	/** 64 bytes (512 bits) for each, each bit represents 32K(minimum group size) */
	uint64_t		ai_bmap[ARENA_GRP_BMSZ];
};

struct ad_blob_info {
	/* total number of arenas */
	uint32_t		bi_total_arenas;
	/** number of registered arena types */
	uint32_t		bi_asp_nr;
	/** specifications of registered arena types */
	struct ad_arena_spec	bi_asp[ARENA_SPEC_MAX];
	/* number of arenas for each spec */
	uint32_t		bi_num_of_each_spec[ARENA_SPEC_MAX];
};

struct ad_blob_iter_param {
	uint32_t			ip_arena_index;
	uint32_t			ip_group_index;
	enum ad_blob_iter_flags		ip_flags;
	/* this arena information is ready to return */
	bool				ip_arena_info_ready;
	/* this blob information is ready to return */
	bool				ip_blob_info_ready;
	/* group information is ready to return */
	bool				ip_group_info_ready;
	struct ad_blob_group_info	ip_group_info;
	struct ad_blob_arena_info	ip_arena_info;
	struct ad_blob_info		ip_blob_info;
};

int ad_blob_iter_prep(struct ad_blob_iter_param **ret_param, enum ad_blob_iter_flags);
int ad_blob_iter_start(struct ad_blob_handle bh, struct ad_blob_iter_param *param);
int ad_blob_iter_next(struct ad_blob_handle bh, struct ad_blob_iter_param *param);
void ad_blob_iter_finish(struct ad_blob_handle bh, struct ad_blob_iter_param *param);

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
	uint64_t		 ra_off;
	uint64_t		 ra_size;
	/** reserved allocation bit (in group) */
	int			 ra_bit;
};

/** reserved arena types */
enum {
	ARENA_TYPE_DEF		= 0,
	ARENA_TYPE_LARGE	= 1,
	/**
	 * type={0, 1, 2, 3} are for internal usage, customized arena should between
	 * ARENA_TYPE_BASE and ARENA_TYPE_MAX.
	 */
	ARENA_TYPE_BASE		= 4,
	ARENA_TYPE_MAX		= 31,
};

int ad_arena_register(struct ad_blob_handle bh, unsigned int arena_type,
		      struct ad_group_spec *specs, unsigned int specs_nr);
daos_off_t ad_reserve(struct ad_blob_handle bh, int type, daos_size_t size, uint32_t *arena_id,
		      struct ad_reserv_act *act);
int ad_tx_free(struct ad_tx *tx, daos_off_t addr);
int ad_tx_publish(struct ad_tx *tx, struct ad_reserv_act *acts, int act_nr);
void ad_cancel(struct ad_reserv_act *acts, int act_nr);

daos_off_t ad_alloc(struct ad_blob_handle bh, int type, daos_size_t size, uint32_t *arena_id);

void *ad_addr2ptr(struct ad_blob_handle bh, daos_off_t addr);
daos_off_t ad_ptr2addr(struct ad_blob_handle bh, void *ptr);

static inline struct ad_blob_handle
umm2ad_blob_hdl(struct umem_instance *umm)
{
	struct ad_blob_handle	hdl;

	hdl.bh_blob = (struct ad_blob *)umm->umm_pool; /* FIXME */
	return hdl;
}

extern umem_ops_t ad_mem_ops;

#endif /* __DAOS_AD_HOC_MEM_H__ */
