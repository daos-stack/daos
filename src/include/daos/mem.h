/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/daos_mem.h
 */

#ifndef __DAOS_MEM_H__
#define __DAOS_MEM_H__

#include <daos_types.h>
#include <daos/common.h>

/**
 * Terminologies:
 *
 * pmem		Persistent Memory
 * vmem		Volatile Memory
 *
 * umem		Unified memory abstraction
 * umoff	Unified Memory offset
 */

int umempobj_settings_init(bool md_on_ssd);

/* convert backend type to umem class id */
int umempobj_backend_type2class_id(int backend);

/* umem persistent object property flags */
#define	UMEMPOBJ_ENABLE_STATS	0x1

#ifdef DAOS_PMEM_BUILD
enum {
	DAOS_MD_PMEM	= 0,
	DAOS_MD_BMEM	= 1,
	DAOS_MD_ADMEM	= 2,
};

/* return umem backend type */
int umempobj_get_backend_type(void);

#endif

struct umem_wal_tx;

struct umem_wal_tx_ops {
	/**
	 * Get number of umem_actions in TX redo log.
	 *
	 * \param tx[in]	umem_wal_tx pointer
	 */
	uint32_t	(*wtx_act_nr)(struct umem_wal_tx *tx);

	/**
	 * Get payload size of umem_actions in TX redo log.
	 *
	 * \param tx[in]	umem_wal_tx pointer
	 */
	uint32_t	(*wtx_payload_sz)(struct umem_wal_tx *tx);

	/**
	 * Get the first umem_action in TX redo log.
	 *
	 * \param tx[in]	umem_wal_tx pointer
	 */
	struct umem_action *	(*wtx_act_first)(struct umem_wal_tx *tx);

	/**
	 * Get the next umem_action in TX redo log.
	 *
	 * \param tx[in]	umem_wal_tx pointer
	 */
	struct umem_action *	(*wtx_act_next)(struct umem_wal_tx *tx);
};

#define UTX_PRIV_SIZE	(256)
struct umem_wal_tx {
	struct umem_wal_tx_ops	*utx_ops;
	int			 utx_stage;	/* enum umem_pobj_tx_stage */
	uint64_t		 utx_id;
	/* umem class specific TX data */
	struct {
		char		 utx_space[UTX_PRIV_SIZE];
	}			 utx_private;
};

/** Describing a storage region for I/O */
struct umem_store_region {
	/** start offset of the region */
	daos_off_t	sr_addr;
	/** size of the region */
	daos_size_t	sr_size;
};

/** I/O descriptor, it can include arbitrary number of storage regions */
struct umem_store_iod {
	/* number of regions */
	int				 io_nr;
	/** embedded one for single region case */
	struct umem_store_region	 io_region;
	struct umem_store_region	*io_regions;
};

struct umem_store;

struct umem_store_ops {
	int	(*so_load)(struct umem_store *store, char *start);
	int	(*so_read)(struct umem_store *store, struct umem_store_iod *iod,
			   d_sg_list_t *sgl);
	int	(*so_write)(struct umem_store *store, struct umem_store_iod *iod,
			    d_sg_list_t *sgl);
	int	(*so_flush_prep)(struct umem_store *store, struct umem_store_iod *iod,
				 daos_handle_t *fh);
	int	(*so_flush_copy)(daos_handle_t fh, d_sg_list_t *sgl);
	int (*so_flush_post)(daos_handle_t fh, int err);
	int	(*so_wal_reserv)(struct umem_store *store, uint64_t *id);
	int	(*so_wal_submit)(struct umem_store *store, struct umem_wal_tx *wal_tx,
				 void *data_iod);
	int	(*so_wal_replay)(struct umem_store *store,
				 int (*replay_cb)(uint64_t tx_id, struct umem_action *act,
						  void *data),
				 void *arg);
	/* See bio_wal_id_cmp() */
	int	(*so_wal_id_cmp)(struct umem_store *store, uint64_t id1, uint64_t id2);
};

/** The offset of an object from the base address of the pool */
typedef uint64_t umem_off_t;

struct umem_store {
	/**
	 * Size of the umem storage, excluding blob header which isn't visible to allocator.
	 */
	daos_size_t		 stor_size;
	uint32_t		 stor_blk_size;
	uint32_t		 stor_hdr_blks;
	/** private data passing between layers */
	void			*stor_priv;
	void			*stor_stats;
	void                    *vos_priv;
	/** Cache for this store */
	struct umem_cache       *cache;
	/**
	 * Callbacks provided by upper level stack, umem allocator uses them to operate
	 * the storage device.
	 */
	struct umem_store_ops	*stor_ops;
	/* backend type */
	int			 store_type;
	/* standalone store */
	bool			 store_standalone;
};

struct umem_slab_desc {
	size_t		unit_size;
	unsigned	class_id;
};

struct umem_pool {
	void			*up_priv;
	struct umem_store	 up_store;
	/** Slabs of the umem pool */
	struct umem_slab_desc	 up_slabs[0];
};

/* umem persistent object functions */
struct umem_pool *umempobj_create(const char *path, const char *layout_name,
				  int prop_flags, size_t poolsize,
				  mode_t mode, struct umem_store *store);
struct umem_pool *umempobj_open(const char *path, const char *layout_name,
				int prop_flags, struct umem_store *store);
void  umempobj_close(struct umem_pool *pool);
void *umempobj_get_rootptr(struct umem_pool *pool, size_t size);
int   umempobj_get_heapusage(struct umem_pool *pool,
			     daos_size_t *cur_allocated);
void  umempobj_log_fraginfo(struct umem_pool *pool);

/** Number of flag bits to reserve for encoding extra information in
 *  a umem_off_t entry.
 */
#define UMOFF_NUM_FLAG_BITS	(8)
/** The absolute value of a flag mask must be <= this value */
#define UMOFF_MAX_FLAG		(1ULL << UMOFF_NUM_FLAG_BITS)
/** Number of bits to shift the flag bits */
#define UMOFF_FLAG_SHIFT	(63 - UMOFF_NUM_FLAG_BITS)
/** Mask for flag bits */
#define UMOFF_FLAG_MASK		((UMOFF_MAX_FLAG - 1) << UMOFF_FLAG_SHIFT)
/** In theory and offset can be NULL but in practice, pmemobj_root
 *  is not at offset 0 as pmdk reserves some space for its internal
 *  use.   So, use 0 for NULL.   Invalid bits are also considered
 *  NULL.
 */
#define UMOFF_NULL		(0ULL)
/** Check for a NULL value including possible invalid flag bits */
#define UMOFF_IS_NULL(umoff)	(umem_off2offset(umoff) == 0)

/** Retrieves any flags that are set.
 *
 *  \param	offset[IN]	The value from which to get flags
 */
static inline uint64_t
umem_off2flags(umem_off_t umoff)
{
	return (umoff & UMOFF_FLAG_MASK) >> UMOFF_FLAG_SHIFT;
}

/** Retrieves the offset portion of a umem_off_t
 *
 *  \param	offset[IN]	The value from which to get offset
 */
static inline uint64_t
umem_off2offset(umem_off_t umoff)
{
	return umoff & ~UMOFF_FLAG_MASK;
}

/** Set flags on a umem_off_t address
 *  The flags parameter must be < UMOFF_MAX_FLAG.
 *
 *  \param	offset[IN,OUT]	The value is marked NULL with additional flags
 *  \param	flags[IN]	Auxilliarly information about the null entry
 */
static inline void
umem_off_set_flags(umem_off_t *offset, uint64_t flags)
{
	D_ASSERTF(flags < UMOFF_MAX_FLAG,
		  "Attempt to set invalid flag bits on umem_off_t\n");
	*offset = umem_off2offset(*offset) | (flags << UMOFF_FLAG_SHIFT);
}

/** Set a umem_off_t address to NULL and set flags
 *  The flags parameter must be < UMOFF_MAX_FLAG.
 *
 *  \param	offset[IN,OUT]	The value is marked NULL with additional flags
 *  \param	flags[IN]	Auxilliarly information about the null entry
 */
static inline void
umem_off_set_null_flags(umem_off_t *offset, uint64_t flags)
{
	D_ASSERTF(flags < UMOFF_MAX_FLAG,
		  "Attempt to set invalid flag bits on umem_off_t\n");
	*offset = flags << UMOFF_FLAG_SHIFT;
}

/* print format of umoff */
#define UMOFF_PF		DF_X64
#define UMOFF_P(umoff)		umem_off2offset(umoff)

enum umem_pobj_tx_stage {
	UMEM_STAGE_NONE,	/* no transaction in this thread */
	UMEM_STAGE_WORK,	/* transaction in progress */
	UMEM_STAGE_ONCOMMIT,	/* successfully committed */
	UMEM_STAGE_ONABORT,	/* tx_begin failed or transaction aborted */
	UMEM_STAGE_FINALLY,	/* always called */

	MAX_UMEM_TX_STAGE
};

typedef enum {
	/** volatile memory */
	UMEM_CLASS_VMEM,
	/** persistent memory */
	UMEM_CLASS_PMEM,
	/** persistent memory but ignore PMDK snapshot */
	UMEM_CLASS_PMEM_NO_SNAP,
	/** blob backed memory */
	UMEM_CLASS_BMEM,
	/** ad-hoc memory */
	UMEM_CLASS_ADMEM,
	/** unknown */
	UMEM_CLASS_UNKNOWN,
} umem_class_id_t;

typedef void (*umem_tx_cb_t)(void *data, bool noop);

#define UMEM_TX_DATA_MAGIC	(0xc01df00d)
#define UMEM_TX_CB_SHIFT_MAX	20	/* 1m callbacks */
#define UMEM_TX_CB_SHIFT_INIT	5	/* 32 callbacks */

struct umem_tx_stage_item;

/**
 * data structure to store all pmem transaction stage callbacks.
 * See pmemobj_tx_begin and pmemobj_tx_end of PMDK for the details.
 */
struct umem_tx_stage_data {
	int				 txd_magic;
	unsigned int			 txd_commit_cnt;
	unsigned int			 txd_commit_max;
	unsigned int			 txd_abort_cnt;
	unsigned int			 txd_abort_max;
	unsigned int			 txd_end_cnt;
	unsigned int			 txd_end_max;
	struct umem_tx_stage_item	*txd_commit_vec;
	struct umem_tx_stage_item	*txd_abort_vec;
	struct umem_tx_stage_item	*txd_end_vec;
};

/** Initialize \a txd which is for attaching pmem transaction stage callbacks */
int  umem_init_txd(struct umem_tx_stage_data *txd);
/** Finalize the txd initialized by \a umem_init_txd */
void umem_fini_txd(struct umem_tx_stage_data *txd);

struct umem_instance;

/* Flags associated with various umem ops */
#define UMEM_FLAG_ZERO		(((uint64_t)1) << 0)
#define UMEM_FLAG_NO_FLUSH	(((uint64_t)1) << 1)
#define UMEM_XADD_NO_SNAPSHOT	(((uint64_t)1) << 2)

/* type num used by umem ops */
enum {
	UMEM_TYPE_ANY,
};

/* Hints for umem atomic copy operation primarily for bmem implementation */
enum acopy_hint {
	UMEM_COMMIT_IMMEDIATE = 0, /* commit immediate, do not call within a tx */
	UMEM_COMMIT_DEFER,	/* OK to defer commit to blob to a later point */
	UMEM_RESERVED_MEM	/* memory from dav_reserve(), commit on publish */
};

typedef struct {
	/** free umoff */
	int		 (*mo_tx_free)(struct umem_instance *umm,
				       umem_off_t umoff);
	/**
	 * allocate umoff with the specified size & flags
	 *
	 * \param umm	   [IN]	umem class instance.
	 * \param size	   [IN]	size to allocate.
	 * \param flags	   [IN]	flags like zeroing, noflush (for PMDK)
	 * \param type_num [IN]	struct type (for PMDK)
	 */
	umem_off_t	 (*mo_tx_alloc)(struct umem_instance *umm, size_t size,
					uint64_t flags, unsigned int type_num);
	/**
	 * Add the specified range of umoff to current memory transaction.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param umoff	[IN]	memory offset to be added to transaction.
	 * \param offset [IN]	start offset of \a umoff tracked by the
	 *			transaction.
	 * \param size	[IN]	size of \a umoff tracked by the transaction.
	 */
	int		 (*mo_tx_add)(struct umem_instance *umm,
				      umem_off_t umoff, uint64_t offset,
				      size_t size);

	/**
	 * Add the specified range of umoff to current memory transaction but
	 * with flags.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param umoff	[IN]	memory offset to be added to transaction.
	 * \param offset [IN]	start offset of \a umoff tracked by the
	 *			transaction.
	 * \param size	[IN]	size of \a umoff tracked by the transaction.
	 * \param flags [IN]	PMDK flags
	 */
	int		 (*mo_tx_xadd)(struct umem_instance *umm,
				       umem_off_t umoff, uint64_t offset,
				       size_t size, uint64_t flags);
	/**
	 * Add the directly accessible pointer to current memory transaction.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param ptr [IN]	Directly accessible memory pointer.
	 * \param size	[IN]	size to be tracked by the transaction.
	 */
	int		 (*mo_tx_add_ptr)(struct umem_instance *umm,
					  void *ptr, size_t size);
	/** abort memory transaction */
	int		 (*mo_tx_abort)(struct umem_instance *umm, int error);
	/** start memory transaction */
	int		 (*mo_tx_begin)(struct umem_instance *umm,
					struct umem_tx_stage_data *txd);
	/** commit memory transaction */
	int		 (*mo_tx_commit)(struct umem_instance *umm, void *data);

#ifdef DAOS_PMEM_BUILD
	/** get TX stage */
	int		 (*mo_tx_stage)(void);

	/**
	 * Reserve space with specified size.
	 *
	 * \param umm	[IN]		umem class instance.
	 * \param act	[IN|OUT]	action used for later cancel/publish.
	 * \param size	[IN]		size to be reserved.
	 * \param type_num [IN]		struct type (for PMDK)
	 */
	umem_off_t	 (*mo_reserve)(struct umem_instance *umm, void *act, size_t size,
				       unsigned int type_num);

	/**
	 * Defer free til commit.  For use with reserved extents that are not
	 * yet published.  For VMEM, it just calls free.
	 *
	 * \param umm	[IN]		umem class instance.
	 * \param off	[IN]		offset of allocation
	 * \param act	[IN|OUT]	action used for later cancel/publish.
	 */
	void		 (*mo_defer_free)(struct umem_instance *umm, umem_off_t off, void *act);

	/**
	 * Cancel the reservation.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param actv	[IN]	action array to be canceled.
	 * \param actv_cnt [IN]	size of action array.
	 */
	void		 (*mo_cancel)(struct umem_instance *umm, void *act, int actv_cnt);

	/**
	 * Publish the reservation (make it persistent).
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param actv	[IN]	action array to be published.
	 * \param actv_cnt [IN]	size of action array.
	 */
	int		 (*mo_tx_publish)(struct umem_instance *umm, void *act, int actv_cnt);

	/**
	 * Atomically copy the contents from src to the destination address.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param dest	[IN]	destination address
	 * \param src	[IN]	source address
	 * \param len	[IN]	length of data to be copied.
	 * \param hint	[IN]	hint on when to persist.
	 */
	 void *		(*mo_atomic_copy)(struct umem_instance *umem,
					  void *dest, const void *src,
					  size_t len, enum acopy_hint hint);

	/** free umoff atomically */
	int		 (*mo_atomic_free)(struct umem_instance *umm,
					   umem_off_t umoff);

	/**
	 * allocate umoff with the specified size & flags atomically
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param size	[IN]	size to allocate.
	 * \param flags	[IN]	flags like zeroing, noflush (for PMDK)
	 * \param type_num [IN]	struct type (for PMDK)
	 */
	umem_off_t	 (*mo_atomic_alloc)(struct umem_instance *umm, size_t size,
					    unsigned int type_num);

	/**
	 * flush data at specific offset to persistent store.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param addr	[IN]	starting address
	 * \param size	[IN]	total bytes to be flushed.
	 */
	void		(*mo_atomic_flush)(struct umem_instance *umm, void *addr,
					   size_t size);

#endif
	/**
	 * Add one commit or abort callback to current transaction.
	 *
	 * PMDK doesn't provide public API to get stage_callback_arg, so
	 * we have to make @txd as an input parameter.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param txd	[IN]	transaction stage data.
	 * \param cb	[IN]	commit or abort callback.
	 * \param data	[IN]	callback data.
	 */
	int		 (*mo_tx_add_callback)(struct umem_instance *umm,
					       struct umem_tx_stage_data *txd,
					       int stage, umem_tx_cb_t cb,
					       void *data);
} umem_ops_t;

/** attributes to initialize an unified memory class */
struct umem_attr {
	umem_class_id_t			 uma_id;
	struct umem_pool		*uma_pool;
};

/** instance of an unified memory class */
struct umem_instance {
	umem_class_id_t		 umm_id;
	int			 umm_nospc_rc;
	const char		*umm_name;
	struct umem_pool	*umm_pool;
	/** Cache the pool id field for umem addresses */
	uint64_t		 umm_pool_uuid_lo;
	/** Cache the base address of the pool */
	uint64_t		 umm_base;
	/** class member functions */
	umem_ops_t		*umm_ops;
};

#ifdef DAOS_PMEM_BUILD
void umem_stage_callback(int stage, void *data);
#endif

int  umem_class_init(struct umem_attr *uma, struct umem_instance *umm);
void umem_attr_get(struct umem_instance *umm, struct umem_attr *uma);

/** Convert an offset to pointer.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	umoff[in]	The offset to convert
 *
 *  \return	The address in memory
 */
static inline void *
umem_off2ptr(const struct umem_instance *umm, umem_off_t umoff)
{
	if (UMOFF_IS_NULL(umoff))
		return NULL;

	return (void *)(umm->umm_base + umem_off2offset(umoff));
}

/** Convert pointer to an offset.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	ptr[in]		The direct pointer to convert
 *
 *  Returns the umem offset
 */
static inline umem_off_t
umem_ptr2off(const struct umem_instance *umm, void *ptr)
{
	if (ptr == NULL)
		return UMOFF_NULL;

	return (umem_off_t)ptr - umm->umm_base;
}

/**
 * Get pmemobj pool uuid
 *
 * \param	umm[IN]	The umem pool instance
 */
static inline uint64_t
umem_get_uuid(const struct umem_instance *umm)
{
	return umm->umm_pool_uuid_lo;
}

static inline bool
umem_has_tx(struct umem_instance *umm)
{
	return umm->umm_ops->mo_tx_add != NULL;
}

#define umem_alloc_verb(umm, flags, size)			                                   \
	({                                                                                         \
		umem_off_t __umoff;                                                                \
                                                                                                   \
		__umoff = (umm)->umm_ops->mo_tx_alloc(umm, size, flags, UMEM_TYPE_ANY);   \
		D_ASSERTF(umem_off2flags(__umoff) == 0,                                            \
			  "Invalid assumption about allocnot using flag bits");                    \
		D_DEBUG(DB_MEM,                                                                    \
			"allocate %s umoff=" UMOFF_PF " size=%zu base=" DF_X64                     \
			" pool_uuid_lo=" DF_X64 "\n",                                              \
			(umm)->umm_name, UMOFF_P(__umoff), (size_t)(size), (umm)->umm_base,        \
			(umm)->umm_pool_uuid_lo);                                                  \
		__umoff;                                                                           \
	})

#define umem_alloc(umm, size)						\
	umem_alloc_verb(umm, 0, size)

#define umem_zalloc(umm, size)						\
	umem_alloc_verb(umm, UMEM_FLAG_ZERO, size)

#define umem_alloc_noflush(umm, size)					\
	umem_alloc_verb(umm, UMEM_FLAG_NO_FLUSH, size)

#define umem_free(umm, umoff)                                                                      \
	({                                                                                         \
		D_DEBUG(DB_MEM,                                                                    \
			"Free %s umoff=" UMOFF_PF " base=" DF_X64 " pool_uuid_lo=" DF_X64 "\n",    \
			(umm)->umm_name, UMOFF_P(umoff), (umm)->umm_base,                          \
			(umm)->umm_pool_uuid_lo);                                                  \
                                                                                                   \
		(umm)->umm_ops->mo_tx_free(umm, umoff);                                            \
	})

static inline int
umem_tx_add_range(struct umem_instance *umm, umem_off_t umoff, uint64_t offset,
		  size_t size)
{
	if (umm->umm_ops->mo_tx_add)
		return umm->umm_ops->mo_tx_add(umm, umoff, offset, size);
	else
		return 0;
}

static inline int
umem_tx_xadd_range(struct umem_instance *umm, umem_off_t umoff, uint64_t offset,
		   size_t size, uint64_t flags)
{
	if (umm->umm_ops->mo_tx_xadd)
		return umm->umm_ops->mo_tx_xadd(umm, umoff, offset, size,
						flags);
	else
		return 0;
}

static inline int
umem_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	if (umm->umm_ops->mo_tx_add_ptr)
		return umm->umm_ops->mo_tx_add_ptr(umm, ptr, size);
	else
		return 0;
}

static inline int
umem_tx_xadd_ptr(struct umem_instance *umm, void *ptr, size_t size,
		 uint64_t flags)
{
	umem_off_t	offset = umem_ptr2off(umm, ptr);

	return umem_tx_xadd_range(umm, offset, 0, size, flags);
}

#define umem_tx_add(umm, umoff, size)					\
	umem_tx_add_range(umm, umoff, 0, size)

#define umem_tx_xadd(umm, umoff, size, flags)				\
	umem_tx_xadd_range(umm, umoff, 0, size, flags)

static inline int
umem_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	if (umm->umm_ops->mo_tx_begin)
		return umm->umm_ops->mo_tx_begin(umm, txd);
	else
		return 0;
}

static inline int
umem_tx_commit_ex(struct umem_instance *umm, void *data)
{
	if (umm->umm_ops->mo_tx_commit)
		return umm->umm_ops->mo_tx_commit(umm, data);
	else
		return 0;
}

static inline int
umem_tx_commit(struct umem_instance *umm)
{
	return umem_tx_commit_ex(umm, NULL);
}

static inline int
umem_tx_abort(struct umem_instance *umm, int err)
{
	if (umm->umm_ops->mo_tx_abort)
		return umm->umm_ops->mo_tx_abort(umm, err);
	else
		return err;
}

static inline int
umem_tx_end_ex(struct umem_instance *umm, int err, void *data)
{
	if (err)
		return umem_tx_abort(umm, err);
	else
		return umem_tx_commit_ex(umm, data);
}

static inline int
umem_tx_end(struct umem_instance *umm, int err)
{
	return umem_tx_end_ex(umm, err, NULL);
}

#ifdef DAOS_PMEM_BUILD
bool umem_tx_inprogress(struct umem_instance *umm);
bool umem_tx_none(struct umem_instance *umm);

int umem_tx_errno(int err);

static inline int
umem_tx_stage(struct umem_instance *umm)
{
	return umm->umm_ops->mo_tx_stage();
}

/* Get number of umem_actions in TX redo log */
static inline uint32_t
umem_tx_act_nr(struct umem_wal_tx *tx)
{
	return tx->utx_ops->wtx_act_nr(tx);
}

/* Get payload size of umem_actions in TX redo list */
static inline uint32_t
umem_tx_act_payload_sz(struct umem_wal_tx *tx)
{
	return tx->utx_ops->wtx_payload_sz(tx);
}

/* Get the first umem_action in TX redo list */
static inline struct umem_action *
umem_tx_act_first(struct umem_wal_tx *tx)
{
	return tx->utx_ops->wtx_act_first(tx);
}

/* Get the next umem_action in TX redo list */
static inline struct umem_action *
umem_tx_act_next(struct umem_wal_tx *tx)
{
	return tx->utx_ops->wtx_act_next(tx);
}

struct umem_rsrvd_act;

/* Get the active reserved actions cnt pending for publications */
int umem_rsrvd_act_cnt(struct umem_rsrvd_act *act);
/* Allocate array of structures for reserved actions */
int umem_rsrvd_act_alloc(struct umem_instance *umm, struct umem_rsrvd_act **act, int cnt);
/* Extend the array of structures for reserved actions to max_cnt */
int umem_rsrvd_act_realloc(struct umem_instance *umm, struct umem_rsrvd_act **act, int max_cnt);
/* Free up the array of reserved actions */
int umem_rsrvd_act_free(struct umem_rsrvd_act **act);

umem_off_t umem_reserve(struct umem_instance *umm,
			struct umem_rsrvd_act *rsrvd_act, size_t size);
void umem_defer_free(struct umem_instance *umm, umem_off_t off,
		     struct umem_rsrvd_act *rsrvd_act);
void umem_cancel(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act);
int umem_tx_publish(struct umem_instance *umm,
		    struct umem_rsrvd_act *rsrvd_act);

static inline void *
umem_atomic_copy(struct umem_instance *umm, void *dest, void *src, size_t len,
		 enum acopy_hint hint)
{
	D_ASSERT(umm->umm_ops->mo_atomic_copy != NULL);
	return umm->umm_ops->mo_atomic_copy(umm, dest, src, len, hint);
}

static inline umem_off_t
umem_atomic_alloc(struct umem_instance *umm, size_t len, unsigned int type_num)
{
	D_ASSERT(umm->umm_ops->mo_atomic_alloc != NULL);
	return umm->umm_ops->mo_atomic_alloc(umm, len, type_num);
}

static inline int
umem_atomic_free(struct umem_instance *umm, umem_off_t umoff)
{
	D_ASSERT(umm->umm_ops->mo_atomic_free != NULL);
	return umm->umm_ops->mo_atomic_free(umm, umoff);
}

static inline void
umem_atomic_flush(struct umem_instance *umm, void *addr, size_t len)
{
	if (umm->umm_ops->mo_atomic_flush)
		umm->umm_ops->mo_atomic_flush(umm, addr, len);
	return;
}

int
umem_tx_add_cb(struct umem_instance *umm, struct umem_tx_stage_data *txd, int stage,
	       umem_tx_cb_t cb, void *data);

static inline int
umem_tx_add_callback(struct umem_instance *umm, struct umem_tx_stage_data *txd,
		     int stage, umem_tx_cb_t cb, void *data)
{
	D_ASSERT(umm->umm_ops->mo_tx_add_callback != NULL);
	return umm->umm_ops->mo_tx_add_callback(umm, txd, stage, cb, data);
}

/*********************************************************************************/

/* Type of memory actions */
enum {
	UMEM_ACT_NOOP			= 0,
	/** copy appended payload to specified storage address */
	UMEM_ACT_COPY,
	/** copy payload addressed by @ptr to specified storage address */
	UMEM_ACT_COPY_PTR,
	/** assign 8/16/32 bits integer to specified storage address */
	UMEM_ACT_ASSIGN,
	/** move specified bytes from source address to destination address */
	UMEM_ACT_MOVE,
	/** memset a region with specified value */
	UMEM_ACT_SET,
	/** set the specified bit in bitmap */
	UMEM_ACT_SET_BITS,
	/** unset the specified bit in bitmap */
	UMEM_ACT_CLR_BITS,
	/** it's checksum of the specified address */
	UMEM_ACT_CSUM,
};

/**
 * Memory operations for redo/undo.
 * 16 bytes for bit operation (set/clr) and integer assignment, 32+ bytes for other operations.
 */
#define UMEM_ACT_PAYLOAD_MAX_LEN	(1ULL << 20)
struct umem_action {
	uint16_t			ac_opc;
	union {
		struct {
			uint64_t		addr;
			uint64_t		size;
			uint8_t			payload[0];
		} ac_copy;	/**< copy payload from @payload to @addr */
		struct {
			uint64_t		addr;
			uint64_t		size;
			uint64_t		ptr;
		} ac_copy_ptr;	/**< copy payload from @ptr to @addr */
		struct {
			uint16_t		size;
			uint32_t		val;
			uint64_t		addr;
		} ac_assign;	/**< assign integer to @addr, int64 should use ac_copy */
		struct {
			uint16_t		num;
			uint32_t		pos;
			uint64_t		addr;
		} ac_op_bits;	/**< set or clear the @pos bit in bitmap @addr */
		struct {
			uint8_t			val;
			uint32_t		size;
			uint64_t		addr;
		} ac_set;	/**< memset(addr, val, size) */
		struct {
			uint32_t		size;
			uint64_t		src;
			uint64_t		dst;
		} ac_move;	/**< memmove(dst, size src) */
		struct {
			uint32_t		csum;
			uint32_t		size;
			uint64_t		addr;
		} ac_csum;	/**< it is checksum of data stored in @addr */
	};
};

#define UMEM_CACHE_PAGE_SZ_SHIFT  24 /* 16MB */
#define UMEM_CACHE_PAGE_SZ        (1 << UMEM_CACHE_PAGE_SZ_SHIFT)
#define UMEM_CACHE_PAGE_SZ_MASK   (UMEM_CACHE_PAGE_SZ - 1)

#define UMEM_CACHE_CHUNK_SZ_SHIFT 12 /* 4KB */
#define UMEM_CACHE_CHUNK_SZ       (1 << UMEM_CACHE_CHUNK_SZ_SHIFT)
#define UMEM_CACHE_CHUNK_SZ_MASK  (UMEM_CACHE_CHUNK_SZ - 1)

#define UMEM_CACHE_BMAP_SZ        (1 << (UMEM_CACHE_PAGE_SZ_SHIFT - UMEM_CACHE_CHUNK_SZ_SHIFT - 6))

struct umem_page_info;
/** 16 MB page */
struct umem_page {
	/** page ID */
	unsigned int		 pg_id;
	/** refcount */
	int			 pg_ref;
	/** page info */
	struct umem_page_info   *pg_info;
};

/** Global cache status for each umem_store */
struct umem_cache {
	struct umem_store	*ca_store;
	/** Total pages store */
	uint64_t                 ca_num_pages;
	/** Total pages in cache */
	uint64_t                 ca_mapped;
	/** Maximum number of cached pages */
	uint64_t                 ca_max_mapped;
	/** Free list for mapped page info */
	d_list_t                 ca_pi_free;
	/** all the dirty pages */
	d_list_t                 ca_pgs_dirty;
	/** Pages waiting for copy to DMA buffer */
	d_list_t                 ca_pgs_copying;
	/** LRU list all pages not in one of the other states for future eviction support */
	d_list_t                 ca_pgs_lru;
	/** TODO: some other global status */
	/** All pages, sorted by umem_page::pg_id */
	struct umem_page         ca_pages[0];
};

struct umem_cache_chkpt_stats {
	/** Last committed checkpoint id */
	uint64_t	*uccs_chkpt_id;
	/** Number of pages processed */
	int		 uccs_nr_pages;
	/** Number of dirty chunks copied */
	int		 uccs_nr_dchunks;
	/** Number of sgl iovs used to copy dirty chunks */
	int		 uccs_nr_iovs;
};

static inline uint64_t
umem_cache_size2pages(uint64_t len)
{
	D_ASSERT((len & UMEM_CACHE_PAGE_SZ_MASK) == 0);

	return len >> UMEM_CACHE_PAGE_SZ_SHIFT;
}

static inline uint64_t
umem_cache_size_round(uint64_t len)
{
	return (len + UMEM_CACHE_PAGE_SZ_MASK) & ~UMEM_CACHE_PAGE_SZ_MASK;
}

static inline struct umem_page *
umem_cache_off2page(struct umem_cache *cache, umem_off_t offset)
{
	uint64_t idx = offset >> UMEM_CACHE_PAGE_SZ_SHIFT;

	D_ASSERTF(idx < cache->ca_num_pages,
		  "offset=" DF_U64 ", num_pages=" DF_U64 ", idx=" DF_U64 "\n", offset,
		  cache->ca_num_pages, idx);

	return &cache->ca_pages[idx];
}

/** From a mapped page address, return the umem_cache it belongs to */
static inline struct umem_cache *
umem_page2cache(struct umem_page *page)
{
	return (struct umem_cache *)container_of(&page[-page->pg_id], struct umem_cache, ca_pages);
}

/** From a mapped page address, return the umem_store it belongs to */
static inline struct umem_store *
umem_page2store(struct umem_page *page)
{
	return umem_page2cache(page)->ca_store;
}

/** Allocate global cache for umem store.  All 16MB pages are initially unmapped
 *
 * \param[in]	store		The umem store
 * \param[in]	max_mapped	0 or Maximum number of mapped 16MB pages (must be 0 for now)
 *
 * \return 0 on success
 */
int
umem_cache_alloc(struct umem_store *store, uint64_t max_mapped);

/** Free global cache for umem store.  Pages must be unmapped first
 *
 * \param[in]	store	Store for which to free cache
 *
 * \return 0 on success
 */
int
umem_cache_free(struct umem_store *store);

/** Query if the page cache has enough space to map a range
 *
 * \param[in]	store		The store
 * \param[in]	num_pages	Number of pages to bring into cache
 *
 * \return number of pages that need eviction to support mapping the range
 */
int
umem_cache_check(struct umem_store *store, uint64_t num_pages);

/** Evict the pages.   This invokes the unmap callback. (XXX: not yet implemented)
 *
 * \param[in]	store		The store
 * \param[in]	num_pages	Number of pages to evict
 *
 * \return 0 on success, -DER_BUSY means a checkpoint is needed to evict the pages
 */
int
umem_cache_evict(struct umem_store *store, uint64_t num_pages);

/** Adds a mapped range of pages to the page cache.
 *
 * \param[in]	store		The store
 * \param[in]	offset		The offset in the umem cache
 * \param[in]	start_addr	Start address of mapping
 * \param[in]	num_pages	Number of consecutive 16MB pages to being cached
 *
 * \return 0 on success
 */
int
umem_cache_map_range(struct umem_store *store, umem_off_t offset, void *start_addr,
		     uint64_t num_pages);

/** Take a reference on the pages in the range.   Only needed for cases where we need the page to
 *  stay loaded across a yield, such as the VOS object cache.  Pages in the range must be mapped.
 *
 *  \param[in]	store	The umem store
 *  \param[in]	addr	The address of the hold
 *  \param[in]	size	The size of the hold
 *
 *  \return 0 on success
 */
int
umem_cache_pin(struct umem_store *store, umem_off_t addr, daos_size_t size);

/** Release a reference on pages in the range.  Pages in the range must be mapped and held.
 *
 *  \param[in]	store	The umem store
 *  \param[in]	addr	The address of the hold
 *  \param[in]	size	The size of the hold
 *
 *  \return 0 on success
 */
int
umem_cache_unpin(struct umem_store *store, umem_off_t addr, daos_size_t size);

/**
 * Touched the region identified by @addr and @size, it will mark pages in this region as
 * dirty (also set bitmap within each page), and put it on dirty list
 *
 * This function is called by allocator(probably VOS as well) each time it creates memory
 * snapshot (calls tx_snap) or just to mark a region to be flushed.
 *
 * \param[in]	store	The umem store
 * \param[in]	wr_tx	The writing transaction
 * \param[in]	addr	The start address
 * \param[in]	size	size of dirty region
 *
 * \return 0 on success, -DER_CHKPT_BUSY if a checkpoint is in progress on the page. The calling
 *         transaction must either abort or find another location to modify.
 */
int
umem_cache_touch(struct umem_store *store, uint64_t wr_tx, umem_off_t addr, daos_size_t size);

/** Callback for checkpoint to wait for the commit of chkpt_tx.
 *
 * \param[in]	arg		Argument passed to umem_cache_checkpoint
 * \param[in]	chkpt_tx	The WAL transaction ID we are waiting to commit to WAL
 * \param[out]	committed_tx	The WAL tx ID of the last transaction committed to WAL
 */
typedef void
umem_cache_wait_cb_t(void *arg, uint64_t chkpt_tx, uint64_t *committed_tx);

/**
 * Write all dirty pages before @wal_tx to MD blob. (XXX: not yet implemented)
 *
 * This function can yield internally, it is called by checkpoint service of upper level stack.
 *
 * \param[in]		store		The umem store
 * \param[in]		wait_cb		Callback for to wait for wal commit completion
 * \param[in]		arg		argument for wait_cb
 * \param[in,out]	chkpt_id	Input is last committed id, output is checkpointed id
 * \param[out]		chkpt_stats	check point stats
 *
 * \return 0 on success
 */
int
umem_cache_checkpoint(struct umem_store *store, umem_cache_wait_cb_t wait_cb, void *arg,
		      uint64_t *chkpt_id, struct umem_cache_chkpt_stats *chkpt_stats);

#endif /** DAOS_PMEM_BUILD */

#endif /* __DAOS_MEM_H__ */
