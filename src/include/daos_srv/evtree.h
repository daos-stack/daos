/**
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Extent Version Tree (EVTree) is a variant of rectangle tree (RTree)
 */
#ifndef __DAOS_EV_TREE_H__
#define __DAOS_EV_TREE_H__

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos_types.h>
#include <daos/mem.h>
#include <gurt/list.h>
#include <daos_srv/bio.h>

/** Minimum tree order for an evtree */
#define EVT_MIN_ORDER	4
/** Maximum tree order for an evtree */
#define EVT_MAX_ORDER	128


enum {
	EVT_UMEM_TYPE	= 150,
	EVT_UMEM_ROOT	= (EVT_UMEM_TYPE + 0),
	EVT_UMEM_NODE	= (EVT_UMEM_TYPE + 1),
	EVT_UMEM_DESC	= (EVT_UMEM_TYPE + 2),
};

/** Valid tree order */
enum {
	EVT_ORDER_MIN			= 4,
	EVT_ORDER_MAX			= 128,
};

/** EVTree data pointer */
struct evt_desc {
	/** buffer on SCM or NVMe */
	bio_addr_t			dc_ex_addr;
	/** Pool map version for the record */
	uint32_t			dc_ver;
	/** Magic number for validation */
	uint32_t			dc_magic;
	/** The DTX entry in SCM. */
	umem_off_t			dc_dtx;
	/** placeholder for csum array buffer */
	/** csum_count * csum_len (from tree root) is length of csum buf */
	uint8_t				pt_csum[0];
};

/**
 * Callbacks and parameters for evtree descriptor
 *
 * NB:
 * - evtree is a standalone algorithm, it should not depend on the rest part
 *   of VOS, this function table is abstraction of those direct calls to
 *   VOS/DTX.
 *
 * - Most part of this function table is about undo log callbacks, we might
 *   want to separate those fuctions to a dedicated function table for undo
 *   log in the future. So both evtree & dbtree can share the same definition
 *   of undo log.
 */
struct evt_desc_cbs {
	/**
	 * callback to free bio address, EVtree does not allocate bio address
	 * so it wouldn't free it as well, user should provide callback to
	 * free it.
	 */
	int		(*dc_bio_free_cb)(struct umem_instance *umm,
					  struct evt_desc *desc,
					  daos_size_t nob, void *args);
	void		 *dc_bio_free_args;
	/**
	 * Availability check, it is for data tracked by DTX undo log.
	 * It is optional, EVTree always treats data extent is available if
	 * this method is absent.
	 */
	int		(*dc_log_status_cb)(struct umem_instance *umm,
					    struct evt_desc *desc,
					    int intent, void *args);
	void		 *dc_log_status_args;
	/** Add a descriptor to undo log */
	int		(*dc_log_add_cb)(struct umem_instance *umm,
					 struct evt_desc *desc, void *args);
	void		 *dc_log_add_args;
	/** remove a descriptor to undo log */
	int		(*dc_log_del_cb)(struct umem_instance *umm,
					 struct evt_desc *desc, void *args);
	void		 *dc_log_del_args;
};

struct evt_extent {
	daos_off_t	ex_lo;	/**< low offset */
	daos_off_t	ex_hi;	/**< high offset */
};

/** A versioned extent is effectively a rectangle...
 *  The epoch range is always to infinity.   The sequence number
 *  gives priority to later overwrites within the same epoch.
 */
struct evt_rect {
	struct evt_extent	rc_ex;	/**< extent range */
	daos_epoch_t		rc_epc;	/**< update epoch */
};

/** A search rectangle to limit scope of a search */
struct evt_filter {
	struct evt_extent	fr_ex;	/**< extent range */
	daos_epoch_range_t	fr_epr;	/**< epoch range */
	/** higher level punch epoch (0 if not punched) */
	daos_epoch_t		fr_punch;
};

/** Log format of extent */
#define DF_EXT				\
	DF_U64"-"DF_U64

/** Log format of rectangle */
#define DF_RECT				\
	DF_EXT"@"DF_U64"-INF"

/** Expanded extent members for debug log */
#define DP_EXT(ext)			\
	(ext)->ex_lo, (ext)->ex_hi

/** Expanded rectangle members for debug log */
#define DP_RECT(r)			\
	DP_EXT(&(r)->rc_ex), (r)->rc_epc

/** Log format of evtree entry */
#define DF_ENT				\
	DF_EXT" from "DF_EXT"@"DF_U64"-INF (%c)"

/** Expanded format of evtree entry */
#define DP_ENT(ent)			\
	DP_EXT(&(ent)->en_sel_ext), DP_EXT(&(ent)->en_ext), (ent)->en_epoch, \
	evt_debug_print_visibility(ent)

/** Log format of evtree filter */
#define DF_FILTER			\
	DF_EXT "@" DF_U64"-"DF_U64"(punch="DF_U64")"

#define DP_FILTER(filter)					\
	DP_EXT(&(filter)->fr_ex), (filter)->fr_epr.epr_lo,	\
	(filter)->fr_epr.epr_hi, (filter)->fr_punch

/** Return the width of an extent */
static inline daos_size_t
evt_extent_width(const struct evt_extent *ext)
{
	return ext->ex_hi - ext->ex_lo + 1;
}

/** Return the width of a versioned extent */
static inline daos_size_t
evt_rect_width(const struct evt_rect *rect)
{
	return evt_extent_width(&rect->rc_ex);
}

/**
 * Weight of a versioned extent, different tree policy could use different
 * algorithm to calculate the weight. The generic code should honor "major"
 * more than "minor".
 *
 * NB: structure members can be negative while computing "difference" between
 * weights.
 *
 * See \a evt_weight_cmp for the details.
 */
struct evt_weight {
	int64_t				wt_major; /**< major weight value */
	int64_t				wt_minor; /**< minor weight value */
};

struct evt_node_entry {
	/* Rectangle for the entry */
	struct evt_rect	ne_rect;
	/* Offset to child entry
	 * Intermediate node:	struct evt_node
	 * Leaf node:		struct evt_desc
	 */
	uint64_t	ne_child;
};

/** evtree node: */
struct evt_node {
	/** the Minimum Bounding Box (MBR) bounds all its children */
	struct evt_rect			tn_mbr;
	/** bits to indicate it's a root or leaf */
	uint16_t			tn_flags;
	/** number of children or leaf records */
	uint16_t			tn_nr;
	/** Magic number for validation */
	uint32_t			tn_magic;
	/** force alignment */
	uint64_t			tn_paddings[2];
	/** The entries in the node */
	struct evt_node_entry		tn_rec[0];
};

struct evt_root {
	/** UUID of pmem pool */
	uint64_t			tr_pool_uuid;
	/** offset of the root node */
	uint64_t			tr_node;
	/** the current tree depth */
	uint16_t			tr_depth;
	/** tree order */
	uint16_t			tr_order;
	/** number of bytes per index */
	uint32_t			tr_inob;
	/** see \a evt_feats */
	uint64_t			tr_feats;
	/** number of bytes used to generate each csum */
	uint32_t			tr_csum_chunk_size;
	/** type of the csum used in tree */
	uint16_t			tr_csum_type;
	/** length of each csum in bytes */
	uint16_t			tr_csum_len;
};

static inline int
evt_is_empty(struct evt_root *root)
{
	D_ASSERT(root != NULL);
	return root->tr_depth == 0;
}

enum evt_feats {
	/** rectangles are Sorted by their Start Offset */
	EVT_FEAT_SORT_SOFF		= (1 << 0),
	/** rectangles split by closest side of MBR
	 */
	EVT_FEAT_SORT_DIST		= (1 << 1),
	/** rectangles are sorted by distance to sides of MBR and split
	 *  evenly
	 */
	EVT_FEAT_SORT_DIST_EVEN		= (1 << 2),
};

#define EVT_FEAT_DEFAULT EVT_FEAT_SORT_DIST
#define EVT_FEATS_SUPPORTED	\
	(EVT_FEAT_SORT_SOFF | EVT_FEAT_SORT_DIST | EVT_FEAT_SORT_DIST_EVEN)

/* Information about record to insert */
struct evt_entry_in {
	/** Extent to insert */
	struct evt_rect		ei_rect;
	/** checksum of entry */
	struct dcs_csum_info	ei_csum;
	/** pool map version */
	uint32_t		ei_ver;
	/** number of bytes per record, zero for punch */
	uint32_t		ei_inob;
	/** Address of record to insert */
	bio_addr_t		ei_addr;
};

enum evt_visibility {
	/** It is unknown if entry is covered or visible */
	EVT_UNKNOWN	= 0,
	/** Entry is covered at specified epoch */
	EVT_COVERED	= (1 << 0),
	/** Entry is visible at specified epoch */
	EVT_VISIBLE	= (1 << 1),
	/** Entry is part of larger in-tree extent */
	EVT_PARTIAL	= (1 << 2),
	/** In sorted iterator, marks final entry */
	EVT_LAST	= (1 << 3),
};

/**
 * Data struct to pass in or return a versioned extent and its data block.
 */
struct evt_entry {
	/** Full in-tree extent */
	struct evt_extent		en_ext;
	/** Actual extent within selected range */
	struct evt_extent		en_sel_ext;
	/** checksums of the actual extent*/
	struct dcs_csum_info		en_csum;
	/** pool map version */
	uint32_t			en_ver;
	/** Visibility flags for extent */
	uint32_t			en_visibility;
	/** Address of record to insert */
	bio_addr_t			en_addr;
	/** update epoch of extent */
	daos_epoch_t			en_epoch;
	/** availability check result for the entry */
	int				en_avail_rc;
};

struct evt_list_entry {
	/** A back pointer to the previous split entry, if applicable */
	struct evt_entry	*le_prev;
	/** List link for the entry */
	d_list_t		 le_link;
	/** The metadata associated with the entry */
	struct evt_entry	 le_ent;
};

#define EVT_EMBEDDED_NR 16
/**
 * list head of \a evt_entry, it contains a few embedded entries to support
 * lightweight allocation of entries.
 */
struct evt_entry_array {
	/** Array of allocated entries */
	struct evt_list_entry		*ea_ents;
	/** total number of entries in the array */
	uint32_t			 ea_ent_nr;
	/** total allocated size of array */
	uint32_t			 ea_size;
	/** Maximum size of array */
	uint32_t			 ea_max;
	/** Number of bytes per index */
	uint32_t			 ea_inob;
	/* Small array of embedded entries */
	struct evt_list_entry		 ea_embedded_ents[EVT_EMBEDDED_NR];
};

static inline char
evt_debug_print_visibility(const struct evt_entry *ent)
{
	int	flags = EVT_VISIBLE | EVT_PARTIAL | EVT_COVERED;

	switch (ent->en_visibility & flags) {
	default:
		D_ASSERT(0);
	case 0:
		break;
	case EVT_PARTIAL:
		return 'p';
	case EVT_VISIBLE:
		return 'V';
	case EVT_VISIBLE | EVT_PARTIAL:
		return 'v';
	case EVT_COVERED:
		return 'C';
	case EVT_COVERED | EVT_PARTIAL:
		return 'c';
	}

	return 'U';
}

static inline struct evt_entry *
evt_ent_array_get(struct evt_entry_array *ent_array, int index)
{
	if (index >= ent_array->ea_ent_nr)
		return NULL;

	return &ent_array->ea_ents[index].le_ent;
}

static inline struct evt_entry *
evt_ent_array_get_next(struct evt_entry_array *ent_array, struct evt_entry *ent)
{
	struct evt_list_entry *el;

	el = container_of(ent, struct evt_list_entry, le_ent);

	if ((el + 1) >= &ent_array->ea_ents[ent_array->ea_ent_nr])
		return NULL;

	return &(el + 1)->le_ent;
}

/**
 * Calculate the offset of the selected extent compared to the actual extent.
 * @param entry - contains both selected and full extents.
 * @return the offset
 */
static inline daos_size_t
evt_entry_selected_offset(const struct evt_entry *entry)
{
	return entry->en_sel_ext.ex_lo - entry->en_ext.ex_lo;
}

/** iterate over all entries of a ent_array */
#define evt_ent_array_for_each(ent, ea)				\
	for (ent = evt_ent_array_get(ea, 0); ent != NULL;	\
	     ent = evt_ent_array_get_next(ea, ent))

#define evt_ent_array_empty(ea)		(ea->ea_ent_nr == 0)

void evt_ent_array_init(struct evt_entry_array *ent_array);
void evt_ent_array_fini(struct evt_entry_array *ent_array);

struct evt_context;

/**
 * Tree policy operation table.
 */
struct evt_policy_ops {
	/**
	 * Add an entry \a entry to a tree node \a node.
	 * Set changed flag if MBR changes
	 */
	int	(*po_insert)(struct evt_context *tcx,
			     struct evt_node *node,
			     uint64_t in_off,
			     const struct evt_entry_in *entry,
			     bool *mbr_changed);
	/**
	 * move half entries of the current node \a nd_src to the new
	 * node \a nd_dst.
	 */
	int	(*po_split)(struct evt_context *tcx, bool leaf,
			    struct evt_node *nd_src, struct evt_node *nd_dst);
	/** Move adjusted \a entry within a node after mbr update.
	 * Returns the offset from at to where the entry was moved
	 */
	int	(*po_adjust)(struct evt_context *tcx,
			     struct evt_node *node,
			     struct evt_node_entry *ne, int at);
	/**
	 * Calculate weight of a rectangle \a rect and return it to \a weight.
	 */
	int	(*po_rect_weight)(struct evt_context *tcx,
				  const struct evt_rect *rect,
				  struct evt_weight *weight);

	/** TODO: add more member functions */
};

/**
 * Create a new tree in the specified address of root \a root, and open it.
 * NOTE: Tree Order must be >= EVT_MIN_ORDER and <= EVT_MAX_ORDER.
 *
 * \param feats		[IN]	Feature bits, see \a evt_feats
 * \param order		[IN]	Tree order
 * \param uma		[IN]	Memory class attributes
 * \param root		[IN]	The address to create the tree.
 * \param coh		[IN]	The container open handle
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_create(struct evt_root *root, uint64_t feats, unsigned int order,
	       struct umem_attr *uma, struct evt_desc_cbs *cbs,
	       daos_handle_t *toh);
/**
 * Open a tree by its root address \a root
 *
 * \param root		[IN]	Root address of the tree
 * \param uma		[IN]	Memory class attributes
 * \param coh		[IN]	The container open handle
 * \param info		[IN]	NVMe free space information
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_open(struct evt_root *root, struct umem_attr *uma,
	     struct evt_desc_cbs *cbs, daos_handle_t *toh);

/**
 * Close a opened tree
 *
 * \param toh		[IN]	The tree open handle
 */
int evt_close(daos_handle_t toh);

/**
 * Delete a opened tree and close its open handle
 *
 * \param toh		[IN]	The tree open handle
 */
int evt_destroy(daos_handle_t toh);

/**
 * This function drains rectangles from the tree, each time it deletes a
 * rectangle, it consumes a @credits, which is input paramter of this function.
 * It returns if all input credits are consumed or the tree is empty, in the
 * later case, it also destroys the evtree.
 *
 * \param toh		[IN]	 Tree open handle.
 * \param credis	[IN/OUT] Input and returned drain credits
 * \param destroyed	[OUT]	 Tree is empty and destroyed
 */
int evt_drain(daos_handle_t toh, int *credits, bool *destroyed);

/**
 * Insert a new extented version \a rect and its data memory ID \a addr to
 * a opened tree.
 *
 * \param toh		[IN]	The tree open handle
 * \param entry		[IN]	The entry to insert
 */
int evt_insert(daos_handle_t toh, const struct evt_entry_in *entry);

/**
 * Delete an extent \a rect from an opened tree.
 *
 * \param toh		[IN]	The tree open handle
 * \param rect		[IN]	The versioned extent to delete
 * \param ent		[OUT]	If not NULL, returns the cached
 *                              entry if deleted
 *
 * Note that the upon successful return, the node is removed
 * from the tree.   The data in referenced in \a ent is not removed.
 * The user could free the associated bio_addr_t.
 */
int evt_delete(daos_handle_t toh, const struct evt_rect *rect,
	       struct evt_entry *ent);

/**
 * Search the tree and return all visible versioned extents which overlap with
 * \a rect to \a ent_array.
 *
 * \param toh		[IN]		The tree open handle
 * \param epr		[IN]		Epoch range to search
 * \param extent	[IN]		The extent to search
 * \param ent_array	[IN,OUT]	Pass in initialized list, filled in by
 *					the function
 */
int evt_find(daos_handle_t toh, const daos_epoch_range_t *epr,
	     const struct evt_extent *extent,
	     struct evt_entry_array *ent_array);

/**
 * Debug function, it outputs status of tree nodes at level \a debug_level,
 * or all levels if \a debug_level is negative.
 */
int evt_debug(daos_handle_t toh, int debug_level);

enum {
	/**
	 * Use the embedded iterator of the open handle.
	 * It can reduce memory consumption, but state of iterator can be
	 * overwritten by other tree operation.
	 */
	EVT_ITER_EMBEDDED	= (1 << 0),
	/** Return extents visible in the search rectangle */
	EVT_ITER_VISIBLE	= (1 << 1),
	/** Return extents fully or partially covered in the search rectangle */
	EVT_ITER_COVERED	= (1 << 2),
	/** Skip visible holes (Only valid with EVT_ITER_VISIBLE) */
	EVT_ITER_SKIP_HOLES	= (1 << 3),
	/** Reverse iterator (ordered iterator only) */
	EVT_ITER_REVERSE	= (1 << 4),
	/* If either EVT_ITER_VISIBLE or EVT_ITER_COVERED are set,
	 * evt_iter_probe will calculate and cache visible extents and iterate
	 * through the cached extents.   Each rectangle will be marked as
	 * visible or covered.  The partial bit will be set if the rectangle
	 * returned differs from what is in the tree.  The state of this type
	 * of iterator is unaffected by tree insertion or deletion so reprobe
	 * isn't necessary.  One should probably not use the embedded iterator
	 * when holding such across yield boundaries.
	 * If neither flag is set, all rectangles in tree that intersect the
	 * search rectangle, including punched extents, are returned.
	 */

	/** The iterator is for purge operation */
	EVT_ITER_FOR_PURGE	= (1 << 5),
	/** The iterator is for rebuild scan */
	EVT_ITER_FOR_REBUILD	= (1 << 6),
};

/**
 * Initialise an iterator.
 *
 * \param toh		[IN]	Tree open handle
 * \param options	[IN]	Options for the iterator.
 *				EVT_ITER_EMBEDDED:
 *				if this bit is set, then this function will
 *				return the iterator embedded in the tree open
 *				handle. It will reduce memory consumption,
 *				but state of iterator could be overwritten
 *				by any other tree operation.
 * \param filter	[IN]	Selects only records within the specified
 *                              search rectangle, NULL for no condition
 * \param ih		[OUT]	Returned iterator handle.
 */
int evt_iter_prepare(daos_handle_t toh, unsigned int options,
		     const struct evt_filter *filter, daos_handle_t *ih);
/**
 * Finalise iterator.
 */
int evt_iter_finish(daos_handle_t ih);

enum evt_iter_opc {
	EVT_ITER_FIRST,
	EVT_ITER_FIND,
};
/**
 * Based on the \a opc, this function can do various things:
 * - set the cursor of the iterator to the first extent in the evtree.
 * - find the provided extent or iteration anchor.
 *
 * This function must be called after evt_iter_prepare, it can be called
 * for arbitrary times for the same iterator.
 *
 * \param opc	[IN]	Probe opcode, see evt_iter_opc for the details.
 * \param rect	[IN]	The extent to probe, it will be ignored if opc is
 *			EVT_ITER_FIRST.
 * \param anchor [IN]	The anchor to probe, it will be ignored if \a rect
 *			is provided.
 */
int evt_iter_probe(daos_handle_t ih, enum evt_iter_opc opc,
		   const struct evt_rect *rect, const daos_anchor_t *anchor);

/**
 * Move the iterator cursor to the next extent in the evtree.
 *
 * \param ih	[IN]	Iterator handle.
 */
int evt_iter_next(daos_handle_t ih);

/**
 * Is the evtree iterator empty or not
 *
 * \return	0	Not empty
 *		1	Empty
 *		-ve	error code
 */
int evt_iter_empty(daos_handle_t ih);

/**
 * Delete the record at the current cursor. This function will set the
 * iterator to the next cursor so a subsequent probe is unnecessary.
 * This isn't implemented for sorted iterator.  Deleting a rectangle
 * while iterating a sorted iterator can be done with evt_delete.  This
 * doesn't require a reprobe either.   Implementing this for sorted
 * iterator can help avoid some of the pitfalls and potentially can
 * be more optimal but it is reserved future work.
 *
 * Any time an entry is deleted from an unsorted iterator, it may
 * result in some entries being visited more than once as existing
 * entries can move around in the tree.
 *
 * \param ih		[IN]	Iterator open handle.
 * \param ent		[OUT]	If not NULL, returns the cached entry.
 */
int evt_iter_delete(daos_handle_t ih, struct evt_entry *ent);

/**
 * Fetch the extent and its data address from the current iterator position.
 *
 * \param ih	[IN]	Iterator open handle.
 * \param inob	[OUT]	Number of bytes per record in tree
 * \param entry	[OUT]	The returned extent and its data address.
 * \param anchor [OUT]	Returned hash anchor.
 */
int evt_iter_fetch(daos_handle_t ih, unsigned int *inob,
		   struct evt_entry *entry, daos_anchor_t *anchor);

/** Get overhead constants for an evtree
 *
 * \param alloc_overhead[IN]	Expected per-allocation overhead in bytes
 * \param tree_order[IN]	The expected tree order used in creation
 * \param ovhd[OUT]		Struct to fill with overheads
 *
 * \return 0 on success, error otherwise
 */
int evt_overhead_get(int alloc_overhead, int tree_order,
		     struct daos_tree_overhead *ovhd);

#endif /* __DAOS_EV_TREE_H__ */
