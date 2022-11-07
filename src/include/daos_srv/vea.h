/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Versioned Extent Allocator (VEA) is an extent based block allocator designed
 * for NVMe block device space management, it tracks allocation metadata on
 * separated storage media (SCM), prefers sequential locality on the allocations
 * from same I/O stream.
 *
 * VEA is used by VOS to manage the space inside of SPDK blob on NVMe device.
 */

#ifndef __VEA_API_H__
#define __VEA_API_H__

#include <gurt/list.h>
#include <daos/mem.h>
#include <daos/btree.h>

/* Common free extent structure for both SCM & in-memory index */
struct vea_free_extent {
	uint64_t	vfe_blk_off;	/* Block offset of the extent */
	uint32_t	vfe_blk_cnt;	/* Total blocks of the extent */
	uint32_t	vfe_age;	/* Monotonic timestamp */
};

/* Maximum extents a non-contiguous allocation can have */
#define VEA_EXT_VECTOR_MAX	9

/* Allocated extent vector */
struct vea_ext_vector {
	uint64_t	vev_blk_off[VEA_EXT_VECTOR_MAX];
	uint32_t	vev_blk_cnt[VEA_EXT_VECTOR_MAX];
	uint32_t	vev_size;	/* Size of the extent vector */
};

/* Reserved extent(s) */
struct vea_resrvd_ext {
	/* Link to a list for a series of vea_reserve() calls */
	d_list_t		 vre_link;
	/* Start block offset of the reserved extent */
	uint64_t		 vre_blk_off;
	/* Hint offset before the reserve */
	uint64_t		 vre_hint_off;
	/* Hint sequence to detect interleaved reserve -> publish */
	uint64_t		 vre_hint_seq;
	/* Total reserved blocks */
	uint32_t		 vre_blk_cnt;
	/* Extent vector for non-contiguous reserve */
	struct vea_ext_vector	*vre_vector;
};

/*
 * Per I/O stream persistent hint data provided by VEA caller, caller
 * is responsible to initialize the vhd_off & vhd_seq to zero.
 */
struct vea_hint_df {
	/* Hint block offset */
	uint64_t	vhd_off;
	/* Hint sequence to detect interleaved reserve -> publish */
	uint64_t	vhd_seq;
};

/* Per I/O stream In-memory hint context */
struct vea_hint_context;

/* Unmap context provided by caller */
struct vea_unmap_context {
	/**
	 * Unmap (TRIM) the extent being freed.
	 *
	 * \param unmap_sgl [IN]    SGL to be unmapped (offset & len are in blocks)
	 * \param blk_sz    [IN]    Block size
	 * \param data      [IN]    Block device opaque data
	 *
	 * \return                 Zero on success, negative value on error
	 */
	int (*vnc_unmap)(d_sg_list_t *unmap_sgl, uint32_t blk_sz, void *data);
	void *vnc_data;
	bool vnc_ext_flush;
};

/* Free space tracking information on SCM */
struct vea_space_df {
	uint32_t	vsd_magic;
	uint32_t	vsd_compat;
	/* Block size, 4k bytes by default */
	uint32_t	vsd_blk_sz;
	/* Reserved blocks for the block device header */
	uint32_t	vsd_hdr_blks;
	/* Block device capacity */
	uint64_t	vsd_tot_blks;
	/* Free extent tree, sorted by offset */
	struct btr_root	vsd_free_tree;
	/* Allocated extent vector tree, for non-contiguous allocation */
	struct btr_root	vsd_vec_tree;
};

/* VEA attributes */
struct vea_attr {
	uint32_t	va_compat;	/* VEA compatibility*/
	uint32_t	va_blk_sz;	/* Block size in bytes */
	uint32_t	va_hdr_blks;	/* Blocks for header */
	uint32_t	va_large_thresh;/* Large extent threshold in blocks */
	uint64_t	va_tot_blks;	/* Total capacity in blocks */
	uint64_t	va_free_blks;	/* Free blocks available for alloc */
};

/* VEA statistics */
struct vea_stat {
	uint64_t	vs_free_persistent;	/* Persistent free blocks */
	uint64_t	vs_free_transient;	/* Transient free blocks */
	uint64_t	vs_resrv_hint;	/* Number of hint reserve */
	uint64_t	vs_resrv_large;	/* Number of large reserve */
	uint64_t	vs_resrv_small;	/* Number of small reserve */
	uint64_t	vs_frags_large;	/* Large free frags */
	uint64_t	vs_frags_small;	/* Small free frags */
	uint64_t	vs_frags_aging;	/* Aging frags */
};

struct vea_space_info;

/* Callback to initialize block device header */
typedef int (*vea_format_callback_t)(void *cb_data);

/**
 * Initialize the space tracking information on SCM and the header of the
 * block device.
 *
 * \param umem     [IN]	An instance of SCM
 * \param txd      [IN]	Stage callback data for PMDK transaction
 * \param md       [IN]	The allocation metadata on SCM
 * \param blk_sz   [IN]	Block size in bytes (4k by default)
 * \param hdr_blks [IN] How many blocks reserved for device header
 * \param capacity [IN]	Block device capacity in bytes
 * \param cb       [IN]	Callback to initialize block device header
 * \param cb_data  [IN]	Callback data
 * \param force    [IN]	Forcibly re-initialize an already initialized device
 *
 * \return		Zero on success; -DER_EXIST when try to format an
 *			already initialized device without setting @force to
 *			true; Appropriated negative value for other errors
 */
int vea_format(struct umem_instance *umem, struct umem_tx_stage_data *txd,
	       struct vea_space_df *md, uint32_t blk_sz, uint32_t hdr_blks,
	       uint64_t capacity, vea_format_callback_t cb, void *cb_data,
	       bool force);

/**
 * Load space tracking information from SCM to initialize the in-memory compound
 * index.
 *
 * \param umem       [IN]	An instance of SCM
 * \param txd        [IN]	Stage callback data for PMDK transaction
 * \param md         [IN]	Space tracking information on SCM
 * \param unmap_ctxt [IN]	Context for unmap operation
 * \param metrics    [IN]	Metrics
 * \param vsip       [OUT]	In-memory compound index
 *
 * \return			Zero on success, in-memory compound free extent
 *				index returned by @vsi; Appropriated negative
 *				value on error
 */
int vea_load(struct umem_instance *umem, struct umem_tx_stage_data *txd,
	     struct vea_space_df *md, struct vea_unmap_context *unmap_ctxt,
	     void *metrics, struct vea_space_info **vsip);

/**
 * Free the memory footprint created by vea_load().
 *
 * \param vsi	[IN]	In-memory compound free extent index
 *
 * \return		N/A
 */
void vea_unload(struct vea_space_info *vsi);

/**
 * Load persistent hint from SCM and initialize in-memory hint. It's usually
 * called before starting an I/O stream.
 *
 * \param phd [IN]	Hint data on SCM
 * \param thc [OUT]	In-memory hint context
 *
 * \return		Zero on success, in-memory hint data returned by @thd;
 *			Appropriated negative value on error
 */
int vea_hint_load(struct vea_hint_df *phd, struct vea_hint_context **thc);

/**
 * Free the in-memory hint data created by vea_hint_load(). It's usually
 * called after an I/O stream finished or on memory pressure.
 *
 * \param thc [IN}	In-memory hint context
 *
 * \return		N/A
 */
void vea_hint_unload(struct vea_hint_context *thc);

/**
 * Reserve an extent on block device, if the block device is too fragmented
 * to satisfy a contiguous reservation, an extent vector could be reserved.
 *
 * \param vsi         [IN]	In-memory compound index
 * \param blk_cnt     [IN]	Total block count to be reserved
 * \param hint        [IN]	Hint data
 * \param resrvd_list [OUT]	List for storing the reserved extents
 *
 * \return			Zero on success, reserved extent(s) will be
 *				added in the @resrvd_list; Appropriated
 *				negative value on error
 */
int vea_reserve(struct vea_space_info *vsi, uint32_t blk_cnt,
		struct vea_hint_context *hint, d_list_t *resrvd_list);

/**
 * Cancel the reserved extent(s)
 *
 * \param vsi         [IN]	In-memory compound index
 * \param hint        [IN]	Hint data
 * \param resrvd_list [IN]	List of reserved extent(s) to be canceled
 *
 * \return			Zero on success; Appropriated negative value
 *				on error
 */
int vea_cancel(struct vea_space_info *vsi, struct vea_hint_context *hint,
	       d_list_t *resrvd_list);

/**
 * Make the reservation persistent. It should be part of transaction
 * manipulated by caller.
 *
 * \param vsi         [IN]	In-memory compound index
 * \param hint        [IN]	Hint data
 * \param resrvd_list [IN]	List of reserved extent(s) to be published
 *
 * \return			Zero on success; Appropriated negative value
 *				on error
 */
int vea_tx_publish(struct vea_space_info *vsi, struct vea_hint_context *hint,
		   d_list_t *resrvd_list);

/**
 * Free allocated extent.
 *
 * \param vsi     [IN]		In-memory compound index
 * \param blk_off [IN]		Start offset of the extent to be freed
 * \param blk_cnt [IN]		Total block count to be freed
 *
 * \return			Zero on success; Appropriated negative value
 *				on error
 */
int vea_free(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt);

/**
 * Set an arbitrary age to a free extent with specified start offset.
 *
 * \param vsi     [IN]		In-memory compound index
 * \param blk_off [IN]		Start offset of the free extent to be modified
 * \param age     [IN]		Monotonic timestamp, 0 indicates a non-active
 *				free extent
 *
 * \return			Zero on success; -DER_ENOENT when the free
 *				extent isn't found; Appropriated negative value
 *				on other error
 */
int vea_set_ext_age(struct vea_space_info *vsi, uint64_t blk_off, uint64_t age);

/**
 * Convert an extent into an allocated extent vector.
 *
 * \param vsi        [IN]	In-memory compound index
 * \param blk_off    [IN]	Start offset of the extent
 * \param blk_cnt    [IN]	Total block count of the extent
 * \param ext_vector [OUT]	Converted extent vector
 *
 * \return			Zero on success, converted extent vector is
 *				returned by @ext_vector; Appropriated negative
 *				value on error
 */
int vea_get_ext_vector(struct vea_space_info *vsi, uint64_t blk_off,
		       uint32_t blk_cnt, struct vea_ext_vector *ext_vector);

/**
 * Query space attributes and allocation statistics.
 *
 * \param vsi       [IN]	In-memory compound index
 * \param attr      [OUT]	Space attributes
 * \param stat      [OUT]	Allocation statistics
 *
 * \return			Zero on success; Appropriated negative value
 *				on error
 */
int vea_query(struct vea_space_info *vsi, struct vea_attr *attr,
	      struct vea_stat *stat);

/**
 * Flushing the free frags in aging buffer
 *
 * \param vsi        [IN]	In-memory compound index
 * \param force      [IN]	Force flush no matter if there is qualified extent
 * \param nr_flush   [IN]	Flush at most @nr_flush frags
 * \param nr_flushed [OUT]	How many frags are actually flushed (optional)
 *
 * \return			Zero on success; Appropriated negative value on error
 */
int vea_flush(struct vea_space_info *vsi, bool force, uint32_t nr_flush, uint32_t *nr_flushed);

/**
 * Free metrcis
 *
 * \param data      [IN]	Metrics to be freed
 */
void vea_metrics_free(void *data);

/**
 * Allocate VEA metrics
 *
 * \param path      [IN]	Metrics path
 * \param tgt_id    [IN]	Target ID
 *
 * \return			VEA metrics on success, NULL on error
 */
void *vea_metrics_alloc(const char *path, int tgt_id);

/**
 * Get VEA metrics count
 */
int vea_metrics_count(void);

#endif /* __VEA_API_H__ */
