/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

/* get page size for the backend */
size_t
umempobj_pgsz(int backend);

/* umem persistent object property flags */
#define	UMEMPOBJ_ENABLE_STATS	0x1

#ifdef DAOS_PMEM_BUILD

/* The backend type is stored in meta blob header, don't change the value */
enum {
	DAOS_MD_PMEM	= 0,
	DAOS_MD_BMEM	= 1,
	DAOS_MD_ADMEM	= 2,
	DAOS_MD_BMEM_V2	= 3,
};

/* return umem backend type */
int umempobj_get_backend_type(void);

/* returns whether bmem_v2 pools are allowed */
bool
umempobj_allow_md_bmem_v2();

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
	int	(*so_waitqueue_create)(void **wq);
	void	(*so_waitqueue_destroy)(void *wq);
	void	(*so_waitqueue_wait)(void *wq, bool yield_only);
	void	(*so_waitqueue_wakeup)(void *wq, bool wakeup_all);
	int	(*so_load)(struct umem_store *store, char *start_addr, daos_off_t offset,
			   daos_size_t len);
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
	/* whether the store has evictable zones */
	bool			 store_evictable;
	/* standalone store */
	bool			 store_standalone;
	/* backend SSD is in faulty state */
	bool			 store_faulty;
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

#ifdef DAOS_PMEM_BUILD
#define UMEM_CACHE_PAGE_SZ_SHIFT  24 /* 16MB */
#define UMEM_CACHE_PAGE_SZ        (1 << UMEM_CACHE_PAGE_SZ_SHIFT)

#define UMEM_CACHE_CHUNK_SZ_SHIFT 12 /* 4KB */
#define UMEM_CACHE_CHUNK_SZ       (1 << UMEM_CACHE_CHUNK_SZ_SHIFT)
#define UMEM_CACHE_CHUNK_SZ_MASK  (UMEM_CACHE_CHUNK_SZ - 1)

#define UMEM_CACHE_MIN_PAGES      1

enum umem_page_event_types {
	UMEM_CACHE_EVENT_PGLOAD = 0,
	UMEM_CACHE_EVENT_PGEVICT
};

struct umem_page_info;
/* MD page */
struct umem_page {
	/** Pointing to memory page when it's mapped */
	struct umem_page_info *pg_info;
};

enum umem_page_stats {
	UMEM_PG_STATS_NONEVICTABLE = 0,
	UMEM_PG_STATS_PINNED,
	UMEM_PG_STATS_FREE,
	UMEM_PG_STATS_MAX,
};

enum umem_cache_stats {
	/* How many page cache hit */
	UMEM_CACHE_STATS_HIT	= 0,
	/* How many page cache miss */
	UMEM_CACHE_STATS_MISS,
	/* How many pages are evicted */
	UMEM_CACHE_STATS_EVICT,
	/* How many dirty pages are flushed on evicting */
	UMEM_CACHE_STATS_FLUSH,
	/* How many pages are loaded on cache miss */
	UMEM_CACHE_STATS_LOAD,
	UMEM_CACHE_STATS_MAX,
};

/** Global cache status for each umem_store */
struct umem_cache {
	struct umem_store *ca_store;
	/** Base address of the page cache */
	void            *ca_base;
	/** Offset of first page */
	uint32_t         ca_base_off;
	/** Cache Mode */
	uint32_t         ca_mode;
	/** WAL replay status */
	uint32_t         ca_replay_done;
	/** Total MD pages */
	uint32_t         ca_md_pages;
	/** Total memory pages in cache */
	uint32_t         ca_mem_pages;
	/** Maximum non-evictable memory pages */
	uint32_t         ca_max_ne_pages;
	/** Page size */
	uint32_t         ca_page_sz;
	/** Page size shift */
	uint32_t         ca_page_shift;
	/** Page size mask */
	uint32_t         ca_page_mask;
	/** Per-page Bitmap size (in uint64_t) */
	uint32_t         ca_bmap_sz;
	/** Free list for unmapped page info */
	d_list_t         ca_pgs_free;
	/** Non-evictable & evictable dirty pages */
	d_list_t         ca_pgs_dirty;
	/** All Non-evictable[0] & evictable[1] pages */
	d_list_t         ca_pgs_lru[2];
	/** all the pages in the progress of flushing */
	d_list_t         ca_pgs_flushing;
	/** all the pages waiting for commit */
	d_list_t         ca_pgs_wait_commit;
	/** all the pages being pinned */
	d_list_t         ca_pgs_pinned;
	/** Highest committed transaction ID */
	uint64_t         ca_commit_id;
	/** Callback to tell if a page is evictable */
	bool		 (*ca_evictable_fn)(void *arg, uint32_t pg_id);
	/** Callback being called on page loaded/evicted */
	int		 (*ca_evtcb_fn)(int event_type, void *arg, uint32_t pg_id);
	/** Argument to the callback function */
	void            *ca_fn_arg;
	/** Page stats */
	uint32_t         ca_pgs_stats[UMEM_PG_STATS_MAX];
	/** Cache stats */
	uint64_t	 ca_cache_stats[UMEM_CACHE_STATS_MAX];
	/** How many waiters waiting on free page reserve */
	uint32_t         ca_reserve_waiters;
	/** Waitqueue for free page reserve: umem_cache_reserve() */
	void            *ca_reserve_wq;
	/** TODO: some other global status */
	/** MD page array, array index is page ID */
	struct umem_page ca_pages[0];
};

struct umem_cache_chkpt_stats {
	/** Number of pages processed */
	unsigned int       uccs_nr_pages;
	/** Number of dirty chunks copied */
	unsigned int       uccs_nr_dchunks;
	/** Number of sgl iovs used to copy dirty chunks */
	unsigned int       uccs_nr_iovs;
};

/** Allocate global cache for umem store.
 *
 * \param[in]	store		The umem store
 * \param[in]	page_sz		Page size
 * \param[in]	md_pgs		Total MD pages
 * \param[in]	mem_pgs		Total memory pages
 * \param[in]	max_ne_pgs	Maximum Non-evictable pages
 * \param[in]	base_off	Offset of the umem cache base
 * \param[in]	base		Start address of the page cache
 * \param[in]	is_evictable_fn	Callback function to check if page is evictable
 * \param[in]	pageload_fn	Callback called on page being loaded
 * \param[in]	arg		Argument to callback functions.
 *
 * \return 0 on success
 */
int
umem_cache_alloc(struct umem_store *store, uint32_t page_sz, uint32_t md_pgs, uint32_t mem_pgs,
		 uint32_t max_ne_pgs, uint32_t base_off, void *base,
		 bool (*is_evictable_fn)(void *arg, uint32_t pg_id),
		 int (*evtcb_fn)(int evt_flag, void *arg, uint32_t pg_id), void *arg);

/** Free global cache for umem store.
 *
 * \param[in]	store	Store for which to free cache
 *
 * \return 0 on success
 */
int
umem_cache_free(struct umem_store *store);

/** Check MD-blob offset is already loaded onto umem cache.
 *
 * \param[in]	store	The umem store
 * \param[in]	offset	MD-blob offset to be converted
 *
 * \return	true or false
 */
bool
umem_cache_offisloaded(struct umem_store *store, umem_off_t offset);

/** Convert MD-blob offset to memory pointer, the corresponding page must be mapped already.
 *
 * \param[in]	store	The umem store
 * \param[in]	offset	MD-blob offset to be converted
 *
 * \return	Memory pointer
 */
void *
umem_cache_off2ptr(struct umem_store *store, umem_off_t offset);

/** Convert memory pointer to MD-blob offset, the corresponding page must be mapped already.
 *
 * \param[in]	store	The umem store
 * \param[in]	ptr	Memory pointer to be converted
 *
 * \return	MD-blob offset
 */
umem_off_t
umem_cache_ptr2off(struct umem_store *store, const void *ptr);

/** Update umem_cache post WAL replay. This routine is called after
 *  WAL replay and the evictability of all pages are determined.
 *
 * \param[in]	store	The umem store
 *
 * \return      None
 */
void
umem_cache_post_replay(struct umem_store *store);

struct umem_cache_range {
	umem_off_t  cr_off;
	daos_size_t cr_size;
};

/** Map MD pages in specified range to memory pages. The range to be mapped should be empty
 *  (no page loading required). If caller tries to map non-evictable pages, page eviction
 *  won't be triggered when there are not enough free pages; If caller tries to map evictable
 *  page, page eviction could be triggered, but it can only map single evictable page at a time.
 *
 * \param[in]	store		The umem store
 * \param[in]	ranges		Ranges to be mapped
 * \param[in]	range_nr	Number of ranges
 *
 * \return	0		: On success
 *		-DER_BUSY	: Not enough free pages
 *		-ve		: Errors
 */
int
umem_cache_map(struct umem_store *store, struct umem_cache_range *ranges, int range_nr);

/** Load & map MD pages in specified range to memory pages.
 *
 * \param[in]	store		The umem store
 * \param[in]	ranges		Ranges to be mapped
 * \param[in]	range_nr	Number of ranges
 * \param[in]	for_sys		Internal access from system ULTs (aggregation etc.)
 *
 * \return	0 on success, negative value on error.
 */
int
umem_cache_load(struct umem_store *store, struct umem_cache_range *ranges, int range_nr,
		bool for_sys);

struct umem_pin_handle;

/** Load & map MD pages in specified range to memory pages, then take a reference on the mapped
 *  memory pages, so that the pages won't be evicted until unpin is called. It's usually for the
 *  cases where we need the pages stay loaded across a yield.
 *
 *  \param[in]	store		The umem store
 *  \param[in]	ranges		Ranges to be pinned
 *  \param[in]	range_nr	Number of ranges
 *  \param[in]	for_sys		Internal access from system ULTs (aggregation etc.)
 *  \param[out] pin_handle	Returned pin handle
 *
 *  \return 0 on success
 */
int
umem_cache_pin(struct umem_store *store, struct umem_cache_range *rangs, int range_nr, bool for_sys,
	       struct umem_pin_handle **pin_handle);

/** Unpin the pages pinned by prior umem_cache_pin().
 *
 *  \param[in]	store		The umem store
 *  \param[in]	pin_handle	Pin handle got from umem_cache_pin()
 *  \param[in]	range_nr	Number of ranges
 */
void
umem_cache_unpin(struct umem_store *store, struct umem_pin_handle *pin_handle);

/** Reserve few free pages for potential non-evictable zone grow within a transaction.
 *  Caller needs to ensure there is no CPU yielding after this call till transaction
 *  start.
 *
 *  \param[in]	store		The umem store
 *
 *  \return 0 on success
 */
int
umem_cache_reserve(struct umem_store *store);

/** Inform umem cache the last committed ID.
 *
 * \param[in]	store		The umem store
 * \param[in]	commit_id	The last committed ID
 */
void
umem_cache_commit(struct umem_store *store, uint64_t commit_id);

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

#endif /*DAOS_PMEM_BUILD*/

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
int
      umempobj_get_mbusage(struct umem_pool *pool, uint32_t mb_id, daos_size_t *cur_allocated,
			   daos_size_t *maxsz);
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
	/** blob backed memory v2 */
	UMEM_CLASS_BMEM_V2,
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

/* Macros associated with Memory buckets */
#define	UMEM_DEFAULT_MBKT_ID	0

/* type num used by umem ops */
enum {
	UMEM_TYPE_ANY,
};

/* Hints for umem atomic copy operation primarily for bmem implementation */
enum acopy_hint {
	UMEM_COMMIT_IMMEDIATE = 0, /* commit immediate, do not call within a tx */
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
	 * \param flags	   [IN]	flags like zeroing, noflush (for PMDK and BMEM)
	 * \param type_num [IN]	struct type (for PMDK and BMEM)
	 * \param mbkt_id  [IN]	memory bucket id (for BMEM)
	 */
	umem_off_t (*mo_tx_alloc)(struct umem_instance *umm, size_t size, uint64_t flags,
				  unsigned int type_num, unsigned int mbkt_id);
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
	 * \param flags [IN]	PMDK and BMEM flags
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
	 * \param mbkt_id  [IN]		memory bucket id (for BMEM)
	 */
	umem_off_t (*mo_reserve)(struct umem_instance *umm, void *act, size_t size,
				 unsigned int type_num, unsigned int mbkt_id);

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
	 * \param umm	   [IN]	 umem class instance.
	 * \param size	   [IN]	 size to allocate.
	 * \param flags	   [IN]	 flags like zeroing, noflush (for PMDK)
	 * \param type_num [IN]	 struct type (for PMDK)
	 * \param mbkt_id  [IN]	 memory bucket id (for BMEM)
	 */
	umem_off_t (*mo_atomic_alloc)(struct umem_instance *umm, size_t size, unsigned int type_num,
				      unsigned int mbkt_id);

	/**
	 * flush data at specific offset to persistent store.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param addr	[IN]	starting address
	 * \param size	[IN]	total bytes to be flushed.
	 */
	void		(*mo_atomic_flush)(struct umem_instance *umm, void *addr,
					   size_t size);

	/**
	 * returns an evictable memory bucket for tasks like new object creation etc.
	 *
	 * \param umm	   [IN]	 umem class instance.
	 * \param flags	   [IN]	 flags for MB selection criteria. Currently unused.
	 */
	uint32_t (*mo_allot_evictable_mb)(struct umem_instance *umm, int flags);

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

#ifdef DAOS_PMEM_BUILD
	if (umm->umm_pool && (umm->umm_pool->up_store.store_type == DAOS_MD_BMEM_V2))
		return umem_cache_off2ptr(&umm->umm_pool->up_store, umem_off2offset(umoff));
#endif
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

#ifdef DAOS_PMEM_BUILD
	if (umm->umm_pool && (umm->umm_pool->up_store.store_type == DAOS_MD_BMEM_V2)) {
			return umem_cache_ptr2off(&umm->umm_pool->up_store, ptr);
	} else
#endif
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

#define umem_alloc_verb(umm, flags, size, mbkt_id)                                                 \
	({                                                                                         \
		umem_off_t __umoff;                                                                \
                                                                                                   \
		__umoff = (umm)->umm_ops->mo_tx_alloc(umm, size, flags, UMEM_TYPE_ANY, mbkt_id);   \
		D_ASSERTF(umem_off2flags(__umoff) == 0,                                            \
			  "Invalid assumption about allocnot using flag bits");                    \
		D_DEBUG(DB_MEM,                                                                    \
			"allocate %s umoff=" UMOFF_PF " size=%zu base=" DF_X64                     \
			" pool_uuid_lo=" DF_X64 "\n",                                              \
			(umm)->umm_name, UMOFF_P(__umoff), (size_t)(size), (umm)->umm_base,        \
			(umm)->umm_pool_uuid_lo);                                                  \
		__umoff;                                                                           \
	})

#define umem_alloc(umm, size)  umem_alloc_verb(umm, 0, size, UMEM_DEFAULT_MBKT_ID)

#define umem_alloc_from_bucket(umm, size, mbkt_id)  umem_alloc_verb(umm, 0, size, mbkt_id)

#define umem_zalloc(umm, size) umem_alloc_verb(umm, UMEM_FLAG_ZERO, size, UMEM_DEFAULT_MBKT_ID)

#define umem_zalloc_from_bucket(umm, size, mbkt_id)						   \
	umem_alloc_verb(umm, UMEM_FLAG_ZERO, size, mbkt_id)

#define umem_alloc_noflush(umm, size)								   \
	umem_alloc_verb(umm, UMEM_FLAG_NO_FLUSH, size, UMEM_DEFAULT_MBKT_ID)

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

umem_off_t
umem_reserve_common(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act, size_t size,
	     unsigned int mbkt_id);
#define umem_reserve(umm, rsrvd_act, size)							\
	umem_reserve_common(umm, rsrvd_act, size, UMEM_DEFAULT_MBKT_ID)
#define umem_reserve_from_bucket(umm, rsrvd_act, size, mbkt_id)					\
	umem_reserve_common(umm, rsrvd_act, size, mbkt_id)

void
umem_defer_free(struct umem_instance *umm, umem_off_t off, struct umem_rsrvd_act *rsrvd_act);
void
umem_cancel(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act);
int
umem_tx_publish(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act);

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
	return umm->umm_ops->mo_atomic_alloc(umm, len, type_num, UMEM_DEFAULT_MBKT_ID);
}

static inline umem_off_t
umem_atomic_alloc_from_bucket(struct umem_instance *umm, size_t len, unsigned int type_num,
		  unsigned int mbkt_id)
{
	D_ASSERT(umm->umm_ops->mo_atomic_alloc != NULL);
	return umm->umm_ops->mo_atomic_alloc(umm, len, type_num, mbkt_id);
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

/**
 * Allot an evictable memory bucket for tasks like new object creation etc.
 *
 * \param[in]		umm		umem instance pointer.
 * \param[in]		flags		MB selection criteria.
 *
 * \return id > 0, memory bucket id.
 *	   id = 0, no evictable memory was be chosen.
 */
static inline uint32_t
umem_allot_mb_evictable(struct umem_instance *umm, int flags)
{
	if (umm->umm_ops->mo_allot_evictable_mb)
		return umm->umm_ops->mo_allot_evictable_mb(umm, flags);
	else
		return 0;
}

/**
 * Get memory bucket id associated with the offset.
 *
 * \param[in]		umm		umem instance pointer.
 * \param[in]		off		offset within the umem pool
 *
 * \return id > 0, id of evictable memory bucket.
 *         id = 0, Memory bucket is non-evictable.
 */
uint32_t
umem_get_mb_from_offset(struct umem_instance *umm, umem_off_t off);

/**
 * Get base offset of the memory bucket
 *
 * \param[in]		umm		umem instance pointer.
 * \param[in]		mb_id		memory bucket id.
 *
 * \return off > 0, base offset of evictable memory bucket.
 *         off = 0, base offset of non-evictable memory bucket.
 */
umem_off_t
umem_get_mb_base_offset(struct umem_instance *umm, uint32_t mb_id);

/**
 * Force GC within the heap to optimize umem_cache usage with DAV
 *  v2 allocator.
 *
 * \param[in]		umm		umem instance pointer.
 *
 * \return 0, success
 *         < 0, error
 */
int
umem_heap_gc(struct umem_instance *umm);

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

#endif /** DAOS_PMEM_BUILD */
#endif /* __DAOS_MEM_H__ */
