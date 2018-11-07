/**
 * (C) Copyright 2017-2018 Intel Corporation.
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
#include <daos_types.h>
#include <daos/mem.h>
#include <gurt/list.h>
#include <daos_srv/bio.h>

enum {
	EVT_UMEM_TYPE	= 150,
	EVT_UMEM_ROOT	= (EVT_UMEM_TYPE + 0),
	EVT_UMEM_NODE	= (EVT_UMEM_TYPE + 1),
	EVT_UMEM_PTR	= (EVT_UMEM_TYPE + 2),
};

struct evt_node;
struct evt_root;
struct evt_ptr;

TMMID_DECLARE(struct evt_root, EVT_UMEM_ROOT);
TMMID_DECLARE(struct evt_node, EVT_UMEM_NODE);
TMMID_DECLARE(struct evt_ptr, EVT_UMEM_PTR);

/** Valid tree order */
enum {
	EVT_ORDER_MIN			= 4,
	EVT_ORDER_MAX			= 128,
};

/** EVTree data pointer */
struct evt_ptr {
	/** cookie to insert this extent */
	uuid_t				pt_cookie;
	uint64_t			pt_csum;
	/** number of indices */
	uint64_t			pt_inum;
	/** number of bytes per index */
	uint32_t			pt_inob;
	/** Pool map version for the record */
	uint32_t			pt_ver;
	/** buffer on SCM or NVMe */
	bio_addr_t			pt_ex_addr;
};

/** A versioned extent is effectively a rectangle...
 *  The epoch range is always to infinity.   The sequence number
 *  gives priority to later overwrites within the same epoch.
 */
struct evt_rect {
	daos_off_t			rc_off_lo;	/**< low offset */
	daos_off_t			rc_off_hi;	/**< high offset */
	daos_epoch_t			rc_epc_lo;	/**< low epoch */
};

/** Log format of rectangle */
#define DF_RECT				\
	DF_U64"-"DF_U64"@"DF_U64"-INF"

/** Expanded rectangle members for debug log */
#define DP_RECT(r)			\
	(r)->rc_off_lo, (r)->rc_off_hi, (r)->rc_epc_lo

/** Return the width of a versioned extent */
static inline daos_size_t
evt_rect_width(struct evt_rect *rect)
{
	return rect->rc_off_hi - rect->rc_off_lo + 1;
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
	union {
		/* Pointer to child node (in intermediate node) */
		TMMID(struct evt_node)	ne_node;
		/* Pointer to data descriptor (in leaf node) */
		TMMID(struct evt_ptr)	ne_ptr;
	};
	char ne_pad[24];
};

D_CASSERT(sizeof(struct evt_node_entry) == 64);

/** evtree node: */
struct evt_node {
	/** the Minimum Bounding Box (MBR) bounds all its children */
	struct evt_rect			tn_mbr;
	/** bits to indicate it's a root or leaf */
	uint16_t			tn_flags;
	/** number of children or leaf records */
	uint16_t			tn_nr;
	/** padding for alignment */
	uint32_t			tn_pad_32;
	/** The entries in the node */
	struct evt_node_entry		tn_rec[0];
};

struct evt_root {
	/** mmid of the root node */
	TMMID(struct evt_node)		tr_node;
	/** the current tree depth */
	uint32_t			tr_depth;
	/** tree order */
	uint32_t			tr_order;
	/** see \a evt_feats */
	uint64_t			tr_feats;
};

enum evt_feats {
	/** rectangles are Sorted by their Start Offset */
	EVT_FEAT_SORT_SOFF		= (1 << 0),
};

#define EVT_FEAT_DEFAULT		EVT_FEAT_SORT_SOFF

/**
 * Data struct to pass in or return a versioned extent and its data block.
 */
struct evt_entry {
	/** link chain on evt_entry_list */
	d_list_t			 en_link;
	/** Cached information about the data */
	struct evt_ptr			 en_ptr;
	/** the input/output versioned extent */
	struct evt_rect			 en_rect;
	/** the trimmed rect selected by a search */
	struct evt_rect			 en_sel_rect;
};

#define ERT_ENT_EMBEDDED		32

/**
 * list head of \a evt_entry, it contains a few embedded entries to support
 * lightweight allocation of entries.
 */
struct evt_entry_list {
	/** All entries returned by the search */
	d_list_t			el_list;
	/** pool of allocated blocks of spare entries */
	d_list_t			el_pool;
	/** total number of entries in the list */
	unsigned int			el_ent_nr;
	/** embedded entries (avoid allocation) */
	struct evt_entry		el_ents[ERT_ENT_EMBEDDED];
};

/** iterate over all entries of a ent_list */
#define evt_ent_list_for_each(ent, el)	\
	d_list_for_each_entry(ent, (&(el)->el_list), en_link)

#define evt_ent_list_empty(el)		d_list_empty(&(el)->el_list)

void evt_ent_list_init(struct evt_entry_list *ent_list);
void evt_ent_list_fini(struct evt_entry_list *ent_list);

struct evt_context;

/**
 * Tree policy operation table.
 */
struct evt_policy_ops {
	/**
	 * Add an entry \a entry to a tree node \a nd_mmid.
	 */
	int	(*po_insert)(struct evt_context *tcx,
			     TMMID(struct evt_node) nd_mmid,
			     TMMID(struct evt_node) in_mmid,
			     struct evt_entry *entry);
	/**
	 * move half entries of the current node \a src_mmid to the new
	 * node \a dst_mmid.
	 */
	int	(*po_split)(struct evt_context *tcx, bool leaf,
			    TMMID(struct evt_node) src_mmid,
			    TMMID(struct evt_node) dst_mmid);
	/** Move adjusted \a entry within a node after mbr update */
	void	(*po_adjust)(struct evt_context *tcx,
			     TMMID(struct evt_node) nd_mmid,
			     struct evt_node_entry *ne, int at);
	/**
	 * Calculate weight of a rectangle \a rect and return it to \a weight.
	 */
	int	(*po_rect_weight)(struct evt_context *tcx,
				  struct evt_rect *rect,
				  struct evt_weight *weight);

	/** TODO: add more member functions */
};

/**
 * Create a new tree and open it.
 *
 * \param feats		[IN]	Feature bits, see \a evt_feats
 * \param order		[IN]	Tree order
 * \param uma		[IN]	Memory class attributes
 * \param root_mmidp	[OUT]	The returned tree root mmid
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_create(uint64_t feats, unsigned int order, struct umem_attr *uma,
	       TMMID(struct evt_root) *root_mmidp, daos_handle_t *toh);

/**
 * Create a new tree in the specified address of root \a root, and open it.
 *
 * \param feats		[IN]	Feature bits, see \a evt_feats
 * \param order		[IN]	Tree order
 * \param uma		[IN]	Memory class attributes
 * \param root		[IN]	The address to create the tree.
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_create_inplace(uint64_t feats, unsigned int order,
		       struct umem_attr *uma, struct evt_root *root,
		       daos_handle_t *toh);
/**
 * Open a tree by its memory ID \a root_mmid
 *
 * \param root_mmid	[IN]	Memory ID of the tree root
 * \param uma		[IN]	Memory class attributes
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_open(TMMID(struct evt_root) root_mmid, struct umem_attr *uma,
	     daos_handle_t *toh);
/**
 * Open a tree by its root address \a root
 *
 * \param root		[IN]	Root address of the tree
 * \param uma		[IN]	Memory class attributes
 * \param info		[IN]	NVMe free space information
 * \param toh		[OUT]	The returned tree open handle
 *
 * \return		0	Success
 *			-ve	error code
 */
int evt_open_inplace(struct evt_root *root, struct umem_attr *uma,
		     void *info, daos_handle_t *toh);

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
 * Insert a new extented version \a rect and its data memory ID \a addr to
 * a opened tree.
 *
 * \param toh		[IN]	The tree open handle
 * \param cookie	[IN]	The VOS cookie
 * \param pm_ver	[IN]	Pool map version
 * \param rect		[IN]	The versioned extent to insert
 * \param inob		[IN]	Number of bytes per index in \a rect.  Set to
 *                              zero for punched record
 * \param addr		[IN]	Address of the input data.
 */
int evt_insert(daos_handle_t toh, uuid_t cookie, uint32_t pm_ver,
	       struct evt_rect *rect, uint32_t inob, bio_addr_t addr);

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
int evt_delete(daos_handle_t toh, struct evt_rect *rect, struct evt_entry *ent);

/**
 * Search the tree and return all versioned extents which overlap with \a rect
 * to \a ent_list.
 *
 * \param toh		[IN]	The tree open handle
 * \param rect		[IN]	The versioned extent to search
 * \param ent_list	[OUT]	The returned entry list
 * \param covered	[OUT]	Optional argument to get list of evt_entries
 *				fully covered at specified epoch
 *
 * If \a covered is NULL, \a ent_list will be unsorted and will contain all
 * history for specified epoch for the range.   If a pointer is passed in,
 * ent_list will be sorted and will only contain visible rectangles and
 * covered will be filled with any rectangles that are entirely covered
 * at the specified epoch for the range.  If the covered rectangle is only
 * partially covered by the range,
 */
int evt_find(daos_handle_t toh, struct evt_rect *rect,
	     struct evt_entry_list *ent_list, d_list_t *covered);

/** Scan the tree for the non-punched visible rectangle with the highest
 *  end offset and return the offset + 1 as the size.  Size is set to 0
 *  if no entries exist.   Size is undefined if an error is returned.
 *
 *  \param toh		[IN]	The tree open handle
 *  \param epoch	[IN]	The epoch at which to scan
 *  \param size		[OUT]	The size of the evtree
 *
 *  \return		0		Size is valid
 *			-rc		Other error code
 */
int evt_get_size(daos_handle_t toh, daos_epoch_t epoch, daos_size_t *size);

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
 *
 * \param ih		[OUT]	Returned iterator handle.
 */
int evt_iter_prepare(daos_handle_t toh, unsigned int options,
		     daos_handle_t *ih);
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
 *			EVT_PROBE_FIRST.
 * \param anchor [IN]	The anchor to probe, it will be ignored if \a rect
 *			is provided.
 */
int evt_iter_probe(daos_handle_t ih, enum evt_iter_opc opc,
		   struct evt_rect *rect, daos_anchor_t *anchor);

/**
 * Move the iterator cursor to the next extent in the evtree.
 *
 * \param ih	[IN]	Iterator handle.
 */
int evt_iter_next(daos_handle_t ih);

/**
 * Fetch the extent and its data address from the current iterator position.
 *
 * \param ih	[IN]	Iterator open handle.
 * \param entry	[OUT]	The returned extent and its data address.
 * \param anchor [OUT]	Returned hash anchor.
 */
int evt_iter_fetch(daos_handle_t ih, struct evt_entry *entry,
		   daos_anchor_t *anchor);

#endif /* __DAOS_EV_TREE_H__ */
