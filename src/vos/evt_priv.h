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
#ifndef __EVT_PRIV_H__
#define __EVT_PRIV_H__

#include <daos_srv/evtree.h>
#include "vos_internal.h"

/**
 * Tree node types.
 * NB: a node can be both root and leaf.
 */
enum {
	EVT_NODE_LEAF		= (1 << 0),	/**< leaf node */
	EVT_NODE_ROOT		= (1 << 1),	/**< root node */
};

enum evt_iter_state {
	EVT_ITER_NONE		= 0,	/**< uninitialized iterator */
	EVT_ITER_INIT,			/**< initialized but not probed */
	EVT_ITER_READY,			/**< probed, ready to iterate */
	EVT_ITER_FINI,			/**< reach at the end of iteration */
};

/** We store 48 bit length in the tree so an extent can't be larger */
#define MAX_RECT_WIDTH	(((uint64_t)1 << 48) - 1)

struct evt_iterator {
	/* Epoch range for the iterator */
	struct evt_filter		it_filter;
	/** state of the iterator */
	unsigned int			it_state;
	/** options for iterator */
	unsigned int			it_options;
	unsigned int			it_forward:1,
					it_skip_move:1;
	/** index */
	int				it_index;
	/** For sorted iterators */
	struct evt_entry_array		it_entries;
};

#define EVT_TRACE_MAX                   32

struct evt_trace {
	/** the current node offset */
	umem_off_t			tr_node;
	/** child position of the searching trace */
	unsigned int			tr_at;
	/** Indicates whether node has been added to tx */
	bool				tr_tx_added;
};

struct evt_context {
	/** mapped address of the tree root */
	struct evt_root			*tc_root;
	/** magic number to identify invalid tree open handle */
	unsigned int			 tc_magic;
	/** refcount on the context */
	unsigned int			 tc_ref;
	/** cached tree order (reduce PMEM access) */
	uint16_t			 tc_order;
	/** cached tree depth (reduce PMEM access) */
	uint16_t			 tc_depth;
	/** number of credits for "drain" operation */
	int				 tc_creds:30;
	/** credits is enabled */
	int				 tc_creds_on:1;
	/** cached number of bytes per entry */
	uint32_t			 tc_inob;
	/** cached tree feature bits (reduce PMEM access) */
	uint64_t			 tc_feats;
	/** memory instance (PMEM or DRAM) */
	struct umem_instance		 tc_umm;
	/** pmemobj pool uuid */
	uint64_t			 tc_pmempool_uuid;
	/** embedded iterator */
	struct evt_iterator		 tc_iter;
	/** space to store tree search path */
	struct evt_trace		 tc_trace_scratch[EVT_TRACE_MAX];
	/** points to &tc_trace_scratch[EVT_TRACE_MAX - depth] */
	struct evt_trace		*tc_trace;
	/** customized operation table for different tree policies */
	struct evt_policy_ops		*tc_ops;
	struct evt_desc_cbs		 tc_desc_cbs;
};

#define EVT_NODE_NULL			UMOFF_NULL
#define EVT_ROOT_NULL			UMOFF_NULL

#define evt_umm(tcx)			(&(tcx)->tc_umm)
#define evt_has_tx(tcx)			umem_has_tx(evt_umm(tcx))

#define evt_off2ptr(tcx, offset)			\
	umem_off2ptr(evt_umm(tcx), offset)

#define EVT_NODE_MAGIC 0xf00d
#define EVT_DESC_MAGIC 0xbeefdead

/** Convert an offset to a evtree node descriptor
 * \param[IN]	tcx	Tree context
 * \param[IN]	offset	The offset in the umem pool
 */
static inline struct evt_node *
evt_off2node(struct evt_context *tcx, umem_off_t offset)
{
	struct evt_node *node;

	node = evt_off2ptr(tcx, offset);
	D_ASSERT(node->tn_magic == EVT_NODE_MAGIC);

	return node;
}

/** Convert an offset to a evtree data descriptor
 * \param[IN]	tcx	Tree context
 * \param[IN]	offset	The offset in the umem pool
 */
static inline struct evt_desc *
evt_off2desc(struct evt_context *tcx, umem_off_t offset)
{
	struct evt_desc *desc;

	desc = evt_off2ptr(tcx, offset);
	D_ASSERT(desc->dc_magic == EVT_DESC_MAGIC);

	return desc;
}

int
evt_desc_log_status(struct evt_context *tcx, daos_epoch_t epoch,
		    struct evt_desc *desc, int intent);

/** Helper function for starting a PMDK transaction, if applicable */
static inline int
evt_tx_begin(struct evt_context *tcx)
{
	if (!evt_has_tx(tcx))
		return 0;

	return umem_tx_begin(evt_umm(tcx), NULL);
}

/** Helper function for ending a PMDK transaction, if applicable */
static inline int
evt_tx_end(struct evt_context *tcx, int rc)
{
	if (!evt_has_tx(tcx))
		return rc;

	if (rc != 0)
		return umem_tx_abort(evt_umm(tcx), rc);

	return umem_tx_commit(evt_umm(tcx));
}

/** Helper function for calculating the needed csum buffer length */
daos_size_t
evt_csum_buf_len(const struct evt_context *tcx,
		 const struct evt_extent *extent);

/** Helper function for calculating the needed csums for an extent */
daos_size_t
evt_csum_count(const struct evt_context *tcx,
		 const struct evt_extent *extent);

/**
 * Copy the csum from the evt_entry into the evt_desc. It is expected that
 * enough memory was allocated for the evt_desc to account for the csums.
 * Checksums will be placed right after the defined structure.
 */
void
evt_desc_csum_fill(struct evt_context *tcx, struct evt_desc *desc,
		   const struct evt_entry_in *ent, uint8_t **csum_bufp);

/**
 * Fill the entry's checksum from the evt_desc. It is expected that the entry's
 * full and requested extent are already filled. This copies the checksum
 * address to the entry.
 */
void
evt_entry_csum_fill(struct evt_context *tcx, struct evt_desc *desc,
		    struct evt_entry *entry);

/**
 * Calculate an extent that aligns to chunk boundaries for the selected extent,
 * but does not exceed the physical extent
 */
struct evt_extent
evt_entry_align_to_csum_chunk(struct evt_entry *entry, daos_off_t record_size);

/**
 * Update the csum info for an entry so it takes into account the selected
 * extent.
 */
void
evt_entry_csum_update(const struct evt_extent *const ext,
		      const struct evt_extent *const sel,
		      struct dcs_csum_info *csum_info,
		      daos_size_t rec_len);

/* By definition, all rectangles overlap in the epoch range because all
 * are from start to infinity.  However, for common queries, we often only want
 * rectangles intersect at a given epoch
 */
enum evt_find_opc {
	/** find all rectangles overlapped with the input rectangle */
	EVT_FIND_ALL,
	/** find the first rectangle overlapped with the input rectangle */
	EVT_FIND_FIRST,
	/**
	 * Returns -DER_NO_PERM if any overlapping rectangle is found in
	 * the same epoch with an identical sequence number.
	 */
	EVT_FIND_OVERWRITE,
	/** Find the exactly same extent. */
	EVT_FIND_SAME,
};

/** Clone an evtree context
 *
 * \param[IN]	tcx	The context to clone
 * \param[OUT]	tcx_pp	The new context
 */
int evt_tcx_clone(struct evt_context *tcx, struct evt_context **tcx_pp);

/** Remove the leaf node at the current trace
 *
 * \param[IN]	tcx	The context to use for delete
 *
 *  The trace is set as if evt_move_trace were called.
 *
 * Returns -DER_NONEXIST if it's the last item in the trace
 */
int evt_node_delete(struct evt_context *tcx);

#define EVT_HDL_ALIVE	0xbabecafe
#define EVT_HDL_DEAD	0xdeadbeef

static inline void
evt_tcx_addref(struct evt_context *tcx)
{
	tcx->tc_ref++;
	if (tcx->tc_inob == 0)
		tcx->tc_inob = tcx->tc_root->tr_inob;
}

static inline void
evt_tcx_decref(struct evt_context *tcx)
{
	D_ASSERT(tcx->tc_ref > 0);
	tcx->tc_ref--;
	if (tcx->tc_ref == 0) {
		tcx->tc_magic = EVT_HDL_DEAD;
		/* Free any memory allocated by embedded iterator */
		evt_ent_array_fini(&tcx->tc_iter.it_entries);
		D_FREE(tcx);
	}
}

/** Return true if a rectangle doesn't intersect the filter
 *
 * \param[IN]	filter	The optional input filter
 * \param[IN]	rect	The rectangle to check
 * \param[IN]	leaf	Indicates if the rectangle is a leaf entry
 */
static inline bool
evt_filter_rect(const struct evt_filter *filter, const struct evt_rect *rect,
		bool leaf)
{
	if (filter == NULL)
		goto done;

	if (filter->fr_ex.ex_lo > rect->rc_ex.ex_hi ||
	    filter->fr_ex.ex_hi < rect->rc_ex.ex_lo ||
	    filter->fr_epr.epr_hi < rect->rc_epc)
		return true; /* Rectangle is outside of filter */

	/* In tree rectangle only includes lower bound.  For intermediate
	 * nodes, we can't filter based on lower bound.  For leaf nodes,
	 * we can because it represents a point in time.
	 */
	if (!leaf)
		goto done;

	if (filter->fr_epr.epr_lo > rect->rc_epc)
		return true; /* Rectangle is outside of filter */
done:
	return false;
}

/** Create an equivalent evt_rect from an evt_entry
 *
 * \param[OUT]	rect	The output rectangle
 * \param[IN]	ent	The input entry
 */
static inline void
evt_ent2rect(struct evt_rect *rect, const struct evt_entry *ent)
{
	rect->rc_ex = ent->en_sel_ext;
	rect->rc_epc = ent->en_epoch;
	rect->rc_minor_epc = ent->en_minor_epc;
}

/** Sort entries in an entry array
 * \param[IN]		tcx		The evtree context
 * \param[IN, OUT]	ent_array	The entry array to sort
 * \param[IN]		filter		The evt_filter for upper layer punch
 * \param[IN]		flags		Visibility flags
 *					EVT_VISIBLE: Return visible records
 *					EVT_COVERED: Return covered records
 * Returns 0 if successful, error otherwise.   The resulting array will
 * be sorted by start offset, high epoch
 */
int evt_ent_array_sort(struct evt_context *tcx,
		       struct evt_entry_array *ent_array,
		       const struct evt_filter *filter,
		       int flags);
/** Scan the tree and select all rectangles that match
 * \param[IN]		tcx		The evtree context
 * \param[IN]		opc		The opcode for the scan
 * \param[IN]		intent		The operation intent
 *					EVT_FIND_FIRST: First record only
 *					EVT_FIND_SAME:  Same record only
 *					EVT_FIND_ALL:   All records
 * \param[IN]		filter		Filters for records
 * \param[IN]		rect		The specific rectangle to match
 * \param[IN,OUT]	ent_array	The initialized array to fill
 *
 * Returns 0 if successful, error otherwise.  The tree trace will point at last
 * scanned record.
 */
int
evt_ent_array_fill(struct evt_context *tcx, enum evt_find_opc find_opc,
		   uint32_t intent, const struct evt_filter *filter,
		   const struct evt_rect *rect,
		   struct evt_entry_array *ent_array);

/** Compare two rectanglesConvert a context to a daos_handle
 * \param[IN]		rt1	The first rectangle
 * \param[IN]		rt2	The second rectangle
 *
 * Returns < 0 if rt1 < rt2
 * returns > 0 if rt1 > rt2
 * returns 0 if equal
 *
 * Order is lower high offset, higher epoch, lower high offset
 */
int
evt_rect_cmp(const struct evt_rect *rt1, const struct evt_rect *rt2);

/** Convert a context to a daos_handle
 * \param[IN]	tcx	The evtree context
 *
 * Returns the converted handle
 */
daos_handle_t
evt_tcx2hdl(struct evt_context *tcx);

/** Convert a handle to an evtree context
 * \param[IN]	handle	The daos handle
 *
 * Returns the converted handle
 */
struct evt_context *
evt_hdl2tcx(daos_handle_t toh);

/** Move the trace forward.
 * \param[IN]	tcx	The evtree context
 */
bool
evt_move_trace(struct evt_context *tcx);

/** Read the durable format for the rectangle (or child MBR) at the specified
 *  index.
 * \param[IN]	tcx	The evtree context
 * \param[IN]	node	The tree node
 * \param[IN]	at	The index in the node entry
 * \param[out]	rout	Returned rectangle
 *
 * Returns the rectangle at the index
 */
void
evt_node_rect_read_at(struct evt_context *tcx, struct evt_node *node,
		      unsigned int at, struct evt_rect *rout);

/** Read the durable format for the rectangle (or child MBR) at the specified
 *  index.
 * \param[IN]	tcx	The evtree context
 * \param[IN]	nd_off	The offset of the tree node
 * \param[IN]	at	The index in the node entry
 * \param[out]	rout	Returned rectangle
 *
 * Returns the rectangle at the index
 */
static inline void
evt_nd_off_rect_read_at(struct evt_context *tcx, umem_off_t nd_off,
			unsigned int at, struct evt_rect *rout)
{
	struct evt_node	*node;

	node = evt_off2node(tcx, nd_off);

	evt_node_rect_read_at(tcx, node, at, rout);
}

/** Fill an evt_entry from the record at an index in a tree node
 * \param[IN]	tcx		The evtree context
 * \param[IN]	filter		The passed filter for punched epoch
 * \param[IN]	node		The tree node
 * \param[IN]	at		The index in the node
 * \param[IN]	rect_srch	The original rectangle used for the search
 * \param[IN]	intent		The operation intent
 * \param[OUT]	entry		The entry to fill
 *
 * The selected extent will be trimmed by the search rectangle used.
 */
void
evt_entry_fill(struct evt_context *tcx, struct evt_node *node,
	       unsigned int at, const struct evt_rect *rect_srch,
	       uint32_t intent, struct evt_entry *entry);

/**
 * Check whether the EVT record is available or not.
 * \param[IN]	tcx		The evtree context
 * \param[IN]	entry		Address (offset) of the DTX to be checked.
 * \param[IN]	intent		The operation intent
 *
 * \return	ALB_AVAILABLE_DIRTY	The target is available but with
 *					some uncommitted modification
 *					or garbage, need cleanup.
 *		ALB_AVAILABLE_CLEAN	The target is available,
 *					no pending modification.
 *		ALB_UNAVAILABLE		The target is unavailable.
 *		-DER_INPROGRESS		If the target record is in some
 *					uncommitted DTX, the caller needs
 *					to retry related operation some
 *					time later.
 *		Other negative values on error.
 */
int
evt_dtx_check_availability(struct evt_context *tcx, umem_off_t entry,
			   uint32_t intent);

static inline bool
evt_node_is_set(struct evt_context *tcx, struct evt_node *node,
		unsigned int bits)
{
	return node->tn_flags & bits;
}

static inline bool
evt_node_is_leaf(struct evt_context *tcx, struct evt_node *node)
{
	return evt_node_is_set(tcx, node, EVT_NODE_LEAF);
}

static inline bool
evt_node_is_root(struct evt_context *tcx, struct evt_node *node)
{
	return evt_node_is_set(tcx, node, EVT_NODE_ROOT);
}

/** Return the rectangle at the offset of @at */
static inline struct evt_node_entry *
evt_node_entry_at(struct evt_context *tcx, struct evt_node *node,
		  unsigned int at)
{
	/** Intermediate nodes have no entries */
	D_ASSERT(evt_node_is_leaf(tcx, node));

	return &node->tn_rec[at];
}

/** Return the data pointer at the offset of @at */
static inline struct evt_desc *
evt_node_desc_at(struct evt_context *tcx, struct evt_node *node,
		 unsigned int at)
{
	struct evt_node_entry	*ne = evt_node_entry_at(tcx, node, at);

	D_ASSERT(evt_node_is_leaf(tcx, node));

	return evt_off2desc(tcx, ne->ne_child);
}

static inline bool
evt_entry_punched(const struct evt_entry *ent, const struct evt_filter *filter)
{
	struct vos_punch_record	punch;

	if (filter == NULL)
		return false;

	punch.pr_epc = filter->fr_punch_epc;
	punch.pr_minor_epc = filter->fr_punch_minor_epc;

	return vos_epc_punched(ent->en_epoch, ent->en_minor_epc, &punch);
}

#endif /* __EVT_PRIV_H__ */
