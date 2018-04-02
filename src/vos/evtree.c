/**
 * (C) Copyright 2017 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(vos)

#include "evt_priv.h"

static struct evt_policy_ops evt_ssof_pol_ops;
/**
 * Tree policy table.
 * - Sorted by Start Offset(SSOF): it is the only policy for now.
 */
static struct evt_policy_ops *evt_policies[] = {
	&evt_ssof_pol_ops,
	NULL,
};

static struct evt_rect *evt_node_mbr_get(struct evt_context *tcx,
					 TMMID(struct evt_node) nd_mmid);

/**
 * Returns true if the first rectangle \a rt1 is wider than the second
 * rectangle \a rt2.
 */
static bool
evt_rect_is_wider(struct evt_rect *rt1, struct evt_rect *rt2)
{
	return (rt1->rc_off_lo <= rt2->rc_off_lo &&
		rt1->rc_off_hi >= rt2->rc_off_hi);
}

static bool
evt_rect_equal_width(struct evt_rect *rt1, struct evt_rect *rt2)
{
	return (rt1->rc_off_lo == rt2->rc_off_lo &&
		rt1->rc_off_hi == rt2->rc_off_hi);
}
/**
 * Check if two rectangles overlap with each other.
 *
 * NB: This function is not symmetric(caller cannot arbitrarily change order
 * of input rectangles), the first rectangle \a rt1 should be in-tree, the
 * second rectangle \a rt2 should be the one being searched/inserted.
 */
static int
evt_rect_overlap(struct evt_rect *rt1, struct evt_rect *rt2, bool leaf)
{

	if (rt1->rc_off_lo > rt2->rc_off_hi || /* no offset overlap */
	    rt1->rc_off_hi < rt2->rc_off_lo ||
	    rt1->rc_epc_lo > rt2->rc_epc_hi || /* no epoch overlap */
	    rt1->rc_epc_hi < rt2->rc_epc_lo)
		return RT_OVERLAP_NO;

	if (leaf) {
		if (rt1->rc_epc_lo == rt2->rc_epc_lo) {
			if (evt_rect_equal_width(rt1, rt2))
				return RT_OVERLAP_SAME;

			if (evt_rect_is_wider(rt1, rt2))
				return RT_OVERLAP_INPLACE;

			if (evt_rect_is_wider(rt2, rt1))
				return RT_OVERLAP_EXPAND;

		} else if (rt1->rc_epc_lo < rt2->rc_epc_lo) {
			/* the in-tree extent is older */
			if (evt_rect_is_wider(rt2, rt1))
				return RT_OVERLAP_CAPPING;

		} else {
			/* the current extent is older */
			if (evt_rect_is_wider(rt1, rt2))
				return RT_OVERLAP_CAPPED;
		}
		return RT_OVERLAP_YES;

	} else { /* non-leaf */
		if (evt_rect_is_wider(rt1, rt2) &&
		    rt1->rc_epc_lo <= rt2->rc_epc_lo &&
		    rt1->rc_epc_hi >= rt2->rc_epc_hi)
			return RT_OVERLAP_INCLUDED;

		return RT_OVERLAP_YES;
	}
}

/**
 * Calculate the Minimum Bounding Rectangle (MBR) of two rectangles and store
 * the MBR into the first rectangle \a rt1.
 *
 * This function returns false if \a rt1 is unchanged (it fully includes rt2),
 * otherwise it returns true.
 */
static bool
evt_rect_merge(struct evt_rect *rt1, struct evt_rect *rt2)
{
	bool	changed = false;

	if (rt1->rc_off_lo > rt2->rc_off_lo) {
		rt1->rc_off_lo = rt2->rc_off_lo;
		changed = true;
	}

	if (rt1->rc_off_hi < rt2->rc_off_hi) {
		rt1->rc_off_hi = rt2->rc_off_hi;
		changed = true;
	}

	if (rt1->rc_epc_lo > rt2->rc_epc_lo) {
		rt1->rc_epc_lo = rt2->rc_epc_lo;
		changed = true;
	}

	if (rt1->rc_epc_hi < rt2->rc_epc_hi) {
		rt1->rc_epc_hi = rt2->rc_epc_hi;
		changed = true;
	}

	return changed;
}

/**
 * Compare two weights.
 *
 * \return
 *	-1: \a wt1 is smaller than \a wt2
 *	+1: \a wt1 is larger than \a wt2
 *	 0: equal
 */
static int
evt_weight_cmp(struct evt_weight *wt1, struct evt_weight *wt2)
{
	if (wt1->wt_major < wt2->wt_major)
		return -1;

	if (wt1->wt_major > wt2->wt_major)
		return 1;

	if (wt1->wt_minor < wt2->wt_minor)
		return -1;

	if (wt1->wt_minor > wt2->wt_minor)
		return 1;

	return 0;
}

/**
 * Calculate difference between two weights and return it to \a wt_diff.
 */
static void
evt_weight_diff(struct evt_weight *wt1, struct evt_weight *wt2,
		struct evt_weight *wt_diff)
{
	/* NB: they can be negative */
	wt_diff->wt_major = wt1->wt_major - wt2->wt_major;
	wt_diff->wt_minor = wt1->wt_minor - wt2->wt_minor;
}

/** Initialize an entry list */
void
evt_ent_list_init(struct evt_entry_list *ent_list)
{
	memset(ent_list, 0, sizeof(*ent_list));
	D_INIT_LIST_HEAD(&ent_list->el_list);
}

/** Finalize an entry list */
void
evt_ent_list_fini(struct evt_entry_list *ent_list)
{
	/* NB: free allocated entries and leave alone embedded entries */
	while (ent_list->el_ent_nr > ERT_ENT_EMBEDDED) {
		struct evt_entry *ent;

		ent = d_list_entry(ent_list->el_list.prev, struct evt_entry,
				   en_link);
		d_list_del(&ent->en_link);
		D__FREE_PTR(ent);
		ent_list->el_ent_nr--;
	}

	D_INIT_LIST_HEAD(&ent_list->el_list);
	ent_list->el_ent_nr = 0;
}

/**
 * Take an embedded entry, or allocate a new entry if all embedded entries
 * have been taken.
 */
static struct evt_entry *
evt_ent_list_alloc(struct evt_entry_list *ent_list)
{
	struct evt_entry *ent;

	if (ent_list->el_ent_nr < ERT_ENT_EMBEDDED) {
		/* consume a embedded entry */
		ent = &ent_list->el_ents[ent_list->el_ent_nr];
		ent_list->el_ent_nr++;

		memset(ent, 0, sizeof(*ent));
	} else {
		D__ALLOC_PTR(ent);
		if (ent == NULL)
			return NULL;

	}
	d_list_add_tail(&ent->en_link, &ent_list->el_list);
	return ent;
}

/** Sort all entries of the ent_list */
static void
evt_ent_list_sort(struct evt_entry_list *ent_list)
{
	/* TODO */
}

daos_handle_t
evt_tcx2hdl(struct evt_context *tcx)
{
	daos_handle_t hdl;

	evt_tcx_addref(tcx); /* +1 for opener */
	hdl.cookie = (uint64_t)tcx;
	return hdl;
}

struct evt_context *
evt_hdl2tcx(daos_handle_t toh)
{
	struct evt_context *tcx;

	tcx = (struct evt_context *)toh.cookie;
	if (tcx->tc_magic != EVT_HDL_ALIVE) {
		D_WARN("Invalid tree handle %x\n", tcx->tc_magic);
		return NULL;
	}
	return tcx;
}

static void
evt_tcx_set_dep(struct evt_context *tcx, unsigned int depth)
{
	tcx->tc_depth = depth;
	tcx->tc_trace = &tcx->tc_traces[EVT_TRACE_MAX - depth];
}

static struct evt_trace *
evt_tcx_trace(struct evt_context *tcx, int level)
{
	D__ASSERT(tcx->tc_depth > 0);
	D__ASSERT(level >= 0 && level < tcx->tc_depth);
	D__ASSERT(&tcx->tc_trace[level] < &tcx->tc_traces[EVT_TRACE_MAX]);

	return &tcx->tc_trace[level];
}

static void
evt_tcx_set_trace(struct evt_context *tcx, int level,
		  TMMID(struct evt_node) nd_mmid, int at)
{
	struct evt_trace *trace;

	D__ASSERT(at >= 0 && at < tcx->tc_order);

	D_DEBUG(DB_TRACE, "set trace[%d] "TMMID_PF"/%d\n",
		level, TMMID_P(nd_mmid), at);

	trace = evt_tcx_trace(tcx, level);
	trace->tr_node = nd_mmid;
	trace->tr_at = at;
}

/** Reset all traces within context and set root as the 0-level trace */
static void
evt_tcx_reset_trace(struct evt_context *tcx)
{
	memset(&tcx->tc_traces[0], 0,
	       sizeof(tcx->tc_traces[0]) * EVT_TRACE_MAX);
	evt_tcx_set_dep(tcx, tcx->tc_root->tr_depth);
	evt_tcx_set_trace(tcx, 0, tcx->tc_root->tr_node, 0);
}

/**
 * Create a evtree context for create or open
 *
 * \param root_mmid	[IN]	Optional, root memory ID for open
 * \param root		[IN]	Optional, root address for inplace open
 * \param feats		[IN]	Optional, feature bits for create
 * \param order		[IN]	Optional, tree order for create
 * \param uma		[IN]	Memory attribute for the tree
 * \param tcx_pp	[OUT]	The returned tree context
 */
int
evt_tcx_create(TMMID(struct evt_root) root_mmid, struct evt_root *root,
	       uint64_t feats, unsigned int order, struct umem_attr *uma,
	       struct evt_context **tcx_pp)
{
	struct evt_context	*tcx;
	int			 depth;
	int			 rc;

	D__ALLOC_PTR(tcx);
	if (tcx == NULL)
		return -DER_NOMEM;

	tcx->tc_ref = 1; /* for the caller */
	tcx->tc_magic = EVT_HDL_ALIVE;

	/* XXX choose ops based on feature bits */
	tcx->tc_ops = evt_policies[0];

	evt_ent_list_init(&tcx->tc_ent_list);
	D_INIT_LIST_HEAD(&tcx->tc_ent_clipping);
	D_INIT_LIST_HEAD(&tcx->tc_ent_inserting);
	D_INIT_LIST_HEAD(&tcx->tc_ent_dropping);

	rc = umem_class_init(uma, &tcx->tc_umm);
	if (rc != 0) {
		D_ERROR("Failed to setup mem class %d: %d\n", uma->uma_id, rc);
		D__GOTO(failed, rc);
	}

	if (!TMMID_IS_NULL(root_mmid)) { /* non-inplace tree open */
		tcx->tc_root_mmid = root_mmid;
		if (root == NULL)
			root = umem_id2ptr_typed(&tcx->tc_umm, root_mmid);
	}
	tcx->tc_root = root;

	if (root == NULL || root->tr_feats == 0) { /* tree creation */
		tcx->tc_feats	= feats;
		tcx->tc_order	= order;
		depth		= 0;
		D_DEBUG(DB_TRACE, "Create context for a new tree\n");

	} else {
		tcx->tc_feats	= root->tr_feats;
		tcx->tc_order	= root->tr_order;
		depth		= root->tr_depth;
		D_DEBUG(DB_TRACE, "Load tree context from "TMMID_PF"\n",
			TMMID_P(root_mmid));
	}

	evt_tcx_set_dep(tcx, depth);
	*tcx_pp = tcx;
	return 0;

 failed:
	D_DEBUG(DB_TRACE, "Failed to create tree context: %d\n", rc);
	evt_tcx_decref(tcx);
	return rc;
}

int
evt_tcx_clone(struct evt_context *tcx, struct evt_context **tcx_pp)
{
	struct umem_attr uma;
	int		 rc;

	umem_attr_get(&tcx->tc_umm, &uma);
	if (!tcx->tc_root || tcx->tc_root->tr_feats == 0)
		return -DER_INVAL;

	rc = evt_tcx_create(tcx->tc_root_mmid, tcx->tc_root, -1, -1, &uma,
			    tcx_pp);
	return rc;
}

/**
 * Create a data pointer for extent address @mmid. It allocates buffer
 * if @mmid is NULL.
 *
 * \param mmid		[IN]	Optional, memory ID of the external buffer
 * \param idx_nob	[IN]	Number Of Bytes per index
 * \param idx_num	[IN]	Indicies within the extent
 * \param ptr_mmid_p	[OUT]	The returned memory ID of extent pointer.
 */
static int
evt_ptr_create(struct evt_context *tcx, uuid_t cookie, uint32_t pm_ver,
	       umem_id_t mmid, uint32_t idx_nob, uint64_t idx_num,
	       TMMID(struct evt_ptr) *ptr_mmid_p)
{
	struct evt_ptr		*ptr;
	TMMID(struct evt_ptr)	 ptr_mmid;
	int			 rc;

	ptr_mmid = umem_znew_typed(evt_umm(tcx), struct evt_ptr);
	if (TMMID_IS_NULL(ptr_mmid))
		return -DER_NOMEM;

	ptr = evt_tmmid2ptr(tcx, ptr_mmid);
	ptr->pt_inob = idx_nob;
	ptr->pt_inum = idx_num;
	uuid_copy(ptr->pt_cookie, cookie);
	ptr->pt_ver = pm_ver;

	if (UMMID_IS_NULL(mmid) && idx_nob * idx_num > EVT_PTR_PAYLOAD) {
		mmid = umem_alloc(evt_umm(tcx), idx_nob * idx_num);
		if (UMMID_IS_NULL(mmid))
			D__GOTO(failed, rc = -DER_NOMEM);
	}

	ptr->pt_mmid = mmid;
	*ptr_mmid_p = ptr_mmid;
	return 0;
 failed:
	umem_free_typed(evt_umm(tcx), ptr_mmid);
	return rc;
}

/**
 * Free a data pointer. It also frees the data buffer if \a free_data is true.
 */
static void
evt_ptr_free(struct evt_context *tcx, TMMID(struct evt_ptr) ptr_mmid,
	     bool free_data)
{
	struct evt_ptr	*ptr = evt_tmmid2ptr(tcx, ptr_mmid);

	D__ASSERT(ptr->pt_ref == 0);
	if (free_data) {
		if (!UMMID_IS_NULL(ptr->pt_mmid))
			umem_free(evt_umm(tcx), ptr->pt_mmid);
	}
	umem_free_typed(evt_umm(tcx), ptr_mmid);
}

/**
 * Return the data address, NOB per index, and number of indices of the data
 * extent pointed by \a ptr_mmid.
 */
static void *
evt_ptr_payload(struct evt_context *tcx, TMMID(struct evt_ptr) ptr_mmid,
		uint32_t *idx_nob, uint64_t *idx_num)
{
	struct evt_ptr	*ptr = evt_tmmid2ptr(tcx, ptr_mmid);

	if (idx_nob != NULL)
		*idx_nob = ptr->pt_inob;

	if (idx_num != NULL)
		*idx_num = ptr->pt_inum;

	if (!UMMID_IS_NULL(ptr->pt_mmid))
		return evt_mmid2ptr(tcx, ptr->pt_mmid);

	if (ptr->pt_inob == 0)
		return NULL;

	/* returns the embedded data */
	D__ASSERT(ptr->pt_inob * ptr->pt_inum <= EVT_PTR_PAYLOAD);
	return &ptr->pt_payload[0];
}

static void
evt_ptr_copy(struct evt_context *tcx, TMMID(struct evt_ptr) dst_mmid,
	     TMMID(struct evt_ptr) src_mmid)
{
	struct evt_ptr *src_ptr = evt_tmmid2ptr(tcx, src_mmid);
	struct evt_ptr *dst_ptr = evt_tmmid2ptr(tcx, dst_mmid);
	int		ref	= dst_ptr->pt_ref;

	D_DEBUG(DB_IO, "dst r=%d, num=%d, nob=%d, src r=%d, num=%d, nob=%d\n",
		dst_ptr->pt_ref, (int)dst_ptr->pt_inum, dst_ptr->pt_inob,
		src_ptr->pt_ref, (int)src_ptr->pt_inum, src_ptr->pt_inob);

	if (!UMMID_IS_NULL(dst_ptr->pt_mmid))
		umem_free(evt_umm(tcx), dst_ptr->pt_mmid);

	memcpy(dst_ptr, src_ptr, sizeof(*dst_ptr));
	dst_ptr->pt_ref = ref;
}

/** copy data from \a sgl to the buffer addressed by \a ptr_mmid */
static int
evt_ptr_copy_sgl(struct evt_context *tcx, TMMID(struct evt_ptr) ptr_mmid,
		 daos_sg_list_t *sgl)
{
	void		*addr;
	uint64_t	 nob;
	uint64_t	 idx_num;
	uint32_t	 idx_nob;
	int		 i;

	addr = evt_ptr_payload(tcx, ptr_mmid, &idx_nob, &idx_num);
	nob  = idx_nob * idx_num;
	if (idx_nob == 0) /* punch */
		return 0;

	D__ASSERT(addr != NULL);
	if (sgl->sg_iovs[0].iov_buf == NULL) {
		/* special use-case for VOS, we just return the address and
		 * allow vos to copy in data.
		 */
		daos_iov_set(&sgl->sg_iovs[0], addr, nob);
		sgl->sg_nr_out = 1;
		return 0;
	}

	for (i = 0; i < sgl->sg_nr && nob != 0; i++) {
		daos_iov_t *iov = &sgl->sg_iovs[i];

		if (nob < iov->iov_len) {
			D_DEBUG(DB_IO, "sgl is too large\n");
			return -DER_IO_INVAL;
		}

		memcpy(addr, iov->iov_buf, iov->iov_len);
		addr += iov->iov_len;
		nob -= iov->iov_len;
	}

	if (nob != 0) /* ignore if data buffer is short */
		D_DEBUG(DB_IO, "sgl is too small\n");

	return 0;
}

/** Take refcount on the specified extent pointer */
static void
evt_ptr_addref(struct evt_context *tcx, TMMID(struct evt_ptr) ptr_mmid)
{
	struct evt_ptr	*ptr = evt_tmmid2ptr(tcx, ptr_mmid);

	ptr->pt_ref++;
}

/**
 * Release refcount on an extent pointer, it will release the extent pointer
 * and its data on releasing of the last refcount.
 */
static void
evt_ptr_decref(struct evt_context *tcx, TMMID(struct evt_ptr) ptr_mmid)
{
	struct evt_ptr	*ptr = evt_tmmid2ptr(tcx, ptr_mmid);

	D__ASSERTF(ptr->pt_ref > 0, "ptr=%p, ref=%u\n", ptr, ptr->pt_ref);
	ptr->pt_ref--;
	if (ptr->pt_ref == 0)
		evt_ptr_free(tcx, ptr_mmid, true);
}

/** check if a node is full */
static bool
evt_node_is_full(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	struct evt_node *nd = evt_tmmid2ptr(tcx, nd_mmid);

	D__ASSERT(nd->tn_nr <= tcx->tc_order);
	return nd->tn_nr == tcx->tc_order;
}

#if 0
static inline void
evt_node_set(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
	     unsigned int bits)
{
	struct evt_node *nd = evt_tmmid2ptr(tcx, nd_mmid);

	nd->tn_flags |= bits;
}
#endif

static inline void
evt_node_unset(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
	       unsigned int bits)
{
	struct evt_node *nd = evt_tmmid2ptr(tcx, nd_mmid);

	nd->tn_flags &= ~bits;
}

static inline bool
evt_node_is_set(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		unsigned int bits)
{
	struct evt_node *nd = evt_tmmid2ptr(tcx, nd_mmid);

	return nd->tn_flags & bits;
}

static inline bool
evt_node_is_leaf(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	return evt_node_is_set(tcx, nd_mmid, EVT_NODE_LEAF);
}

static inline bool
evt_node_is_root(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	return evt_node_is_set(tcx, nd_mmid, EVT_NODE_ROOT);
}

/** Return the rectangle at the offset of @at */
struct evt_rect *
evt_node_rect_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 unsigned int at)
{
	struct evt_node		*nd = evt_tmmid2ptr(tcx, nd_mmid);
	struct evt_rect		*rects;

	rects = (struct evt_rect *)(&nd[1]);
	return &rects[at];
}

/**
 * Update the rectangle stored at the offset \a at of the specified node.
 * This function should update the MBR of the tree node the new rectangle
 * can enlarge the MBR.
 *
 * XXX, It will be ignored if the change shrinks the MBR of the node, this
 * should be fixed in the future.
 *
 * \param	tn_mmid [IN]	Tree node memory ID
 * \param	at	[IN]	Rectangle offset within the tree node.
 * \return	true		Node MBR changed
 *		false		No changed.
 */
static bool
evt_node_rect_update(struct evt_context *tcx, TMMID(struct evt_node) tn_mmid,
		     unsigned int at, struct evt_rect *rect)
{
	struct evt_rect *rtmp;
	bool		 changed;

	/* update the rectangle at the specified position */
	rtmp = evt_node_rect_at(tcx, tn_mmid, at);
	*rtmp = *rect;

	/* merge the rectangle with the current node */
	rtmp = evt_node_mbr_get(tcx, tn_mmid);
	changed = evt_rect_merge(rtmp, rect);

	return changed;
}

/** Return the adress of child mmid at the offset of @at */
TMMID(struct evt_node) *
evt_node_child_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		  unsigned int at)
{
	struct evt_rect		*rects;
	TMMID(struct evt_node)	*mmids;

	D__ASSERT(!evt_node_is_leaf(tcx, nd_mmid));
	rects = evt_node_rect_at(tcx, nd_mmid, tcx->tc_order);
	mmids = (TMMID(struct evt_node) *)rects;

	return &mmids[at];
}

/** Return the data pointer at the offset of @at */
struct evt_ptr_ref *
evt_node_pref_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 unsigned int at)
{
	struct evt_rect		*rects;
	struct evt_ptr_ref	*prefs;

	D__ASSERT(evt_node_is_leaf(tcx, nd_mmid));
	rects = evt_node_rect_at(tcx, nd_mmid, tcx->tc_order);
	prefs = (struct evt_ptr_ref *)rects;

	return &prefs[at];
}

/**
 * Return the size of evtree node, leaf node has different size with internal
 * node.
 */
static int
evt_node_size(struct evt_context *tcx, unsigned int flags)
{
	unsigned int size;

	size = sizeof(struct evt_node) +
	       sizeof(struct evt_rect) * tcx->tc_order;

	D__ASSERT(sizeof(struct evt_ptr_ref) >= sizeof(TMMID(struct evt_node)));
	if (flags & EVT_NODE_LEAF)
		size += sizeof(struct evt_ptr_ref) * tcx->tc_order;
	else
		size += sizeof(TMMID(struct evt_node)) * tcx->tc_order;

	return size;
}

/** Allocate a evtree node */
static int
evt_node_alloc(struct evt_context *tcx, unsigned int flags,
	       TMMID(struct evt_node) *nd_mmid_p)
{
	struct evt_node		*nd;
	TMMID(struct evt_node)	 nd_mmid;

	nd_mmid = umem_zalloc_typed(evt_umm(tcx), struct evt_node,
				    evt_node_size(tcx, flags));
	if (TMMID_IS_NULL(nd_mmid))
		return -DER_NOMEM;

	D_DEBUG(DB_TRACE, "Allocate new node "TMMID_PF"\n", TMMID_P(nd_mmid));
	nd = evt_tmmid2ptr(tcx, nd_mmid);
	nd->tn_flags = flags;

	*nd_mmid_p = nd_mmid;
	return 0;
}

static void
evt_node_free(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	umem_free_typed(evt_umm(tcx), nd_mmid);
}

/**
 * Destroy a tree node and all its desendants nodes, or leaf records and
 * data extents.
 */
static void
evt_node_destroy(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 int level)
{
	struct evt_node	*nd;
	bool		 leaf;
	int		 i;

	nd = evt_tmmid2ptr(tcx, nd_mmid);
	leaf = evt_node_is_leaf(tcx, nd_mmid);

	D_DEBUG(DB_TRACE, "Destroy %s node at level %d (nr = %d)\n",
		leaf ? "leaf" : "", level, nd->tn_nr);

	for (i = 0; i < nd->tn_nr; i++) {
		if (leaf) {
			struct evt_ptr_ref *pref;

			pref = evt_node_pref_at(tcx, nd_mmid, i);
			evt_ptr_decref(tcx, pref->pr_ptr_mmid);
		} else {
			TMMID(struct evt_node) child_mmid;

			child_mmid = *evt_node_child_at(tcx, nd_mmid, i);
			evt_node_destroy(tcx, child_mmid, level + 1);
		}
	}
	evt_node_free(tcx, nd_mmid);
}

#if 0
static inline int
evt_node_tx_add(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	struct evt_node	*nd;
	int		 rc;

	nd = evt_tmmid2ptr(tcx, nd_mmid);
	rc = umem_tx_add_typed(evt_umm(tcx), nd_mmid,
			       evt_node_size(tcx, nd->tn_flags));
	return rc;
}
#endif

/** Return the MBR of a node */
static struct evt_rect *
evt_node_mbr_get(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	struct evt_node	*node;

	node = evt_tmmid2ptr(tcx, nd_mmid);
	return &node->tn_mbr;
}

/** (Re)compute MBR for a tree node */
static void
evt_node_mbr_cal(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	struct evt_node	*node;
	struct evt_rect *mbr;
	int		 i;

	node = evt_tmmid2ptr(tcx, nd_mmid);
	D__ASSERT(node->tn_nr != 0);

	mbr = &node->tn_mbr;
	*mbr = *evt_node_rect_at(tcx, nd_mmid, 0);
	for (i = 1; i < node->tn_nr; i++) {
		struct evt_rect *rect;

		rect = evt_node_rect_at(tcx, nd_mmid, i);
		evt_rect_merge(mbr, rect);
	}
	D_DEBUG(DB_TRACE, "Compute out MBR "DF_RECT"("TMMID_PF"), nr=%d\n",
		DP_RECT(mbr), TMMID_P(nd_mmid), node->tn_nr);
}

/**
 * Split tree node \a src_mmid by moving some entries from it to the new
 * node \a dst_mmid. This function also updates MBRs for both nodes.
 *
 * Node split is a customized method of tree policy.
 */
static int
evt_node_split(struct evt_context *tcx, bool leaf,
	       TMMID(struct evt_node) src_mmid,
	       TMMID(struct evt_node) dst_mmid)
{
	int	rc;

	rc = tcx->tc_ops->po_split(tcx, leaf, src_mmid, dst_mmid);
	if (rc == 0) { /* calculate MBR for both nodes */
		evt_node_mbr_cal(tcx, src_mmid);
		evt_node_mbr_cal(tcx, dst_mmid);
	}
	return rc;
}

/**
 * Insert a new entry into a node \a nd_mmid, update MBR of the node if it's
 * enlarged after inserting the new entry. This function should be called
 * only if the node has empty slot (not full).
 *
 * Entry insertion is a customized method of tree policy.
 */
static int
evt_node_insert(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		struct evt_entry *ent, bool *mbr_changed)
{
	struct evt_rect *mbr;
	struct evt_node *nd;
	int		 rc;
	bool		 changed = 0;

	nd  = evt_tmmid2ptr(tcx, nd_mmid);
	mbr = evt_node_mbr_get(tcx, nd_mmid);

	D_DEBUG(DB_TRACE, "Insert "DF_RECT" into "DF_RECT"("TMMID_PF")\n",
		DP_RECT(&ent->en_rect), DP_RECT(mbr), TMMID_P(nd_mmid));

	rc = tcx->tc_ops->po_insert(tcx, nd_mmid, ent);
	if (rc == 0) {
		if (nd->tn_nr == 1) {
			nd->tn_mbr = ent->en_rect;
			changed = true;
		} else {
			changed = evt_rect_merge(&nd->tn_mbr, &ent->en_rect);
		}
		D_DEBUG(DB_TRACE, "New MBR is "DF_RECT", nr=%d\n",
			DP_RECT(mbr), nd->tn_nr);
	}

	if (mbr_changed)
		*mbr_changed = changed;

	return rc;
}

/**
 * Calculate weight difference of the node between before and after adding
 * a new rectangle \a rect. This function is supposed to help caller to choose
 * destination node for insertion.
 *
 * Weight calculation is a customized method of tree policy.
 */
static void
evt_node_weight_diff(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		     struct evt_rect *rect, struct evt_weight *weight_diff)
{
	struct evt_node	  *nd = evt_tmmid2ptr(tcx, nd_mmid);
	struct evt_rect	   rtmp;
	struct evt_weight  wt_org;
	struct evt_weight  wt_new;
	int		   rc;

	rc = evt_rect_overlap(&nd->tn_mbr, rect, false);
	if (rc == RT_OVERLAP_INCLUDED) {
		/* no difference, because the rectangle is included by the
		 * MBR of the node.
		 */
		memset(weight_diff, 0, sizeof(*weight_diff));
		return;
	}

	memset(&wt_org, 0, sizeof(wt_org));
	memset(&wt_new, 0, sizeof(wt_new));

	rtmp = nd->tn_mbr;
	tcx->tc_ops->po_rect_weight(tcx, &rtmp, &wt_org);

	evt_rect_merge(&rtmp, rect);
	tcx->tc_ops->po_rect_weight(tcx, &rtmp, &wt_new);

	evt_weight_diff(&wt_new, &wt_org, weight_diff);
}

/** The tree root is empty */
static inline bool
evt_root_empty(struct evt_context *tcx)
{
	struct evt_root *root = tcx->tc_root;

	return root == NULL || TMMID_IS_NULL(root->tr_node);
}

/** Add the tree root to the transaction */
static int
evt_root_tx_add(struct evt_context *tcx)
{
	struct umem_instance *umm = evt_umm(tcx);
	int		      rc;

	if (!TMMID_IS_NULL(tcx->tc_root_mmid)) {
		rc = umem_tx_add_mmid_typed(umm, tcx->tc_root_mmid);
	} else {
		D__ASSERT(tcx->tc_root != NULL);
		rc = umem_tx_add_ptr(umm, tcx->tc_root, sizeof(*tcx->tc_root));
	}
	return rc;
}

/** Free the tree root, or reset it if it's been created inplace */
void
evt_root_fini(struct evt_context *tcx)
{
	if (TMMID_IS_NULL(tcx->tc_root_mmid)) {
		struct evt_root *root = tcx->tc_root;

		D_DEBUG(DB_TRACE, "Destroy inplace created tree root\n");
		if (root == NULL)
			return;

		if (evt_has_tx(tcx))
			evt_root_tx_add(tcx);

		memset(root, 0, sizeof(*root));

	} else {
		D_DEBUG(DB_TRACE, "Destroy tree root\n");
		umem_free_typed(evt_umm(tcx), tcx->tc_root_mmid);
	}

	tcx->tc_root_mmid = EVT_ROOT_NULL;
	tcx->tc_root = NULL;
}

/** Initialize the tree root */
static int
evt_root_init(struct evt_context *tcx)
{
	int	rc;

	if (evt_has_tx(tcx)) {
		rc = evt_root_tx_add(tcx);
		if (rc != 0)
			return rc;
	}

	tcx->tc_root->tr_feats = tcx->tc_feats;
	tcx->tc_root->tr_order = tcx->tc_order;
	tcx->tc_root->tr_node  = EVT_NODE_NULL;
	return 0;
}

/** Allocate a root node for a new tree. */
static int
evt_root_alloc(struct evt_context *tcx)
{
	tcx->tc_root_mmid = umem_znew_typed(evt_umm(tcx), struct evt_root);
	if (TMMID_IS_NULL(tcx->tc_root_mmid))
		return -DER_NOMEM;

	tcx->tc_root = evt_tmmid2ptr(tcx, tcx->tc_root_mmid);
	return evt_root_init(tcx);
}

static int
evt_root_free(struct evt_context *tcx)
{
	if (!TMMID_IS_NULL(tcx->tc_root_mmid)) {
		umem_free_typed(evt_umm(tcx), tcx->tc_root_mmid);
		tcx->tc_root_mmid = EVT_ROOT_NULL;
	} else {
		memset(tcx->tc_root, 0, sizeof(*tcx->tc_root));
	}
	tcx->tc_root = NULL;
	return 0;
}

/**
 * Activate an empty tree by allocating a node for the root and set the
 * tree depth to one.
 */
static int
evt_root_activate(struct evt_context *tcx)
{
	struct evt_root		*root;
	TMMID(struct evt_node)	 nd_mmid;
	int			 rc;

	root = tcx->tc_root;

	D__ASSERT(root->tr_depth == 0);
	D__ASSERT(TMMID_IS_NULL(root->tr_node));

	/* root node is also a leaf node */
	rc = evt_node_alloc(tcx, EVT_NODE_ROOT | EVT_NODE_LEAF, &nd_mmid);
	if (rc != 0)
		return rc;

	evt_root_tx_add(tcx);
	root->tr_node = nd_mmid;
	root->tr_depth = 1;

	evt_tcx_set_dep(tcx, root->tr_depth);
	evt_tcx_set_trace(tcx, 0, nd_mmid, 0);
	return 0;
}

/** Destroy the root node and all its descendants. */
static int
evt_root_destroy(struct evt_context *tcx)
{
	if (!TMMID_IS_NULL(tcx->tc_root->tr_node)) {
		/* destroy the root node and all descendants */
		evt_node_destroy(tcx, tcx->tc_root->tr_node, 0);
	}

	evt_root_free(tcx);
	return 0;
}

/**
 * Clip the new entry if its rectangle overlaps with any existent leaf node,
 * clipped shards are attached on tcx::tc_ent_inserting:
 *
 * - If there is no overlap, then simply copy the entry \a ent and attach it
 *   to tcx::tc_ent_inserting
 *
 * - If there is overlap:
 *   . Clip the original rectangle (the rectangle already in tree), some of
 *     the clipped shards could be re-inserted straightaway if their parent
 *     still have empty slots. If their parent have no available slots, then
 *     new shards should be attached on tcx::tc_ent_inserting and be inserted
 *     later in the "inserting" phase.
 *
 *   . Clip the new rectangle, then clipped shards should be attached on
 *     tcx::tc_ent_clipping and they should be checked again. A rectangle can
 *     be moved from tcx::tc_ent_clipping to tcx::tc_ent_inserting only if
 *     there is no more overlap.
 *
 * XXX This function is not done yet.
 */
static int
evt_clip_entry(struct evt_context *tcx, struct evt_entry *ent)
{
	struct evt_entry *entmp;
	int		  rc;
	bool		  replaced;

	/* XXX we only clip those epoch capped rectangles, also, we don't
	 * even update their parent (MBR of their parent could be shrinked).
	 * This may not be good for performance but should maintain the
	 * correctness of the tree.
	 */
	evt_ent_list_init(&tcx->tc_ent_list);
	rc = evt_find_ent_list(tcx, EVT_FIND_CAP, &ent->en_rect,
			       &tcx->tc_ent_list);
	if (rc != 0)
		return rc;

	replaced = false;
	/* cap epoch for returned rectangles */
	evt_ent_list_for_each(entmp, &tcx->tc_ent_list) {
		struct evt_rect	   *rt1 = (struct evt_rect *)entmp->en_addr;
		struct evt_rect	   *rt2 = &ent->en_rect;

		D__ASSERT(evt_rect_is_wider(rt2, rt1));
		D__ASSERT(rt1->rc_epc_lo <= rt2->rc_epc_lo);

		if (rt1->rc_epc_lo < rt2->rc_epc_lo) { /* cap epoch */
			D_DEBUG(DB_TRACE,
				"Recap epoch to "DF_U64" for "DF_RECT"\n",
				rt2->rc_epc_lo - 1, DP_RECT(rt1));

			D__ASSERT(rt1->rc_epc_hi >= rt2->rc_epc_lo);
			rt1->rc_epc_hi = rt2->rc_epc_lo - 1;
			continue;
		} /* else: replace */

		D__ASSERT(!replaced);
		D__ASSERT(evt_rect_equal_width(rt1, rt2));

		D_DEBUG(DB_TRACE, "Replacing "DF_RECT"\n", DP_RECT(rt1));
		evt_ptr_copy(tcx, umem_id_u2t(entmp->en_mmid, struct evt_ptr),
			     umem_id_u2t(ent->en_mmid, struct evt_ptr));
		ent->en_mmid = entmp->en_mmid;
		replaced = true;
	}
	evt_ent_list_fini(&tcx->tc_ent_list);

	if (replaced) {
		D_DEBUG(DB_TRACE, "Replaced\n");
		return 0; /* nothing to insert */
	}

	entmp = evt_ent_list_alloc(&tcx->tc_ent_list);
	if (entmp == NULL)
		return -DER_NOMEM;

	entmp->en_rect	 = ent->en_rect;
	entmp->en_offset = ent->en_offset;
	entmp->en_mmid	 = ent->en_mmid;
	d_list_add_tail(&entmp->en_link, &tcx->tc_ent_inserting);

	return 0;
}

/** Select a node from two for the rectangle \a rect being inserted */
static TMMID(struct evt_node)
evt_select_node(struct evt_context *tcx, struct evt_rect *rect,
		TMMID(struct evt_node) nd_mmid1,
		TMMID(struct evt_node) nd_mmid2)
{
	struct evt_weight	wt1;
	struct evt_weight	wt2;
	int			rc;

	evt_node_weight_diff(tcx, nd_mmid1, rect, &wt1);
	evt_node_weight_diff(tcx, nd_mmid2, rect, &wt2);

	rc = evt_weight_cmp(&wt1, &wt2);
	return rc < 0 ? nd_mmid1 : nd_mmid2;
}

/**
 * Insert an entry \a entry to the leaf node located by the trace of \a tcx.
 * If the leaf node is full it will be split. The split will bubble up if its
 * parent is also full.
 */
static int
evt_insert_or_split(struct evt_context *tcx, struct evt_entry *ent_new)
{
	struct evt_rect	 *mbr	  = NULL;
	struct evt_entry  entry	  = *ent_new;
	int		  rc	  = 0;
	int		  level	  = tcx->tc_depth - 1;
	bool		  mbr_changed = false;

	while (1) {
		struct evt_trace	*trace;
		TMMID(struct evt_node)	 nm_cur;
		TMMID(struct evt_node)	 nm_new;
		TMMID(struct evt_node)	 nm_ins;
		bool			 leaf;

		trace	= &tcx->tc_trace[level];
		nm_cur	= trace->tr_node;

		if (mbr) { /* This is set only if no more insert or split */
			D__ASSERT(mbr_changed);
			/* Update the child MBR stored in the current node
			 * because MBR of child has been enlarged.
			 */
			mbr_changed = evt_node_rect_update(tcx, nm_cur,
							   trace->tr_at, mbr);
			if (!mbr_changed || level == 0)
				D__GOTO(out, 0);

			/* continue to merge MBR with upper level node */
			mbr = evt_node_mbr_get(tcx, nm_cur);
			level--;
			continue;
		}

		if (!evt_node_is_full(tcx, nm_cur)) {
			bool	changed;

			rc = evt_node_insert(tcx, nm_cur, &entry, &changed);
			if (rc != 0)
				D__GOTO(failed, rc);

			/* NB: mbr_changed could have been set while splitting
			 * the child node.
			 */
			mbr_changed |= changed;
			if (!mbr_changed || level == 0)
				D__GOTO(out, 0);

			/* continue to merge MBR with upper level node */
			mbr = evt_node_mbr_get(tcx, nm_cur);
			level--;
			continue;
		}
		/* Try to split */

		D_DEBUG(DB_TRACE, "Split node at level %d\n", level);

		leaf = evt_node_is_leaf(tcx, nm_cur);
		rc = evt_node_alloc(tcx, leaf ? EVT_NODE_LEAF : 0, &nm_new);
		if (rc != 0)
			D__GOTO(failed, rc);

		rc = evt_node_split(tcx, leaf, nm_cur, nm_new);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "Failed to split node: %d\n", rc);
			D__GOTO(failed, rc);
		}

		/* choose a node for insert between the current node and the
		 * new created node.
		 */
		nm_ins = evt_select_node(tcx, &entry.en_rect, nm_cur, nm_new);
		rc = evt_node_insert(tcx, nm_ins, &entry, NULL);
		if (rc != 0)
			D__GOTO(failed, rc);

		/* Insert the new node to upper level node:
		 * - If the current node is not root, insert it to its parent
		 * - If the current node is root, create a new root
		 */
		entry.en_mmid = umem_id_t2u(nm_new);
		entry.en_rect = *evt_node_mbr_get(tcx, nm_new);
		if (level != 0) { /* not root */
			level--;
			/* After splitting, MBR of the current node has been
			 * changed (half of its entries are moved out, and
			 * probably added a new entry), so we need to update
			 * its MBR stored in its parent.
			 */
			trace = &tcx->tc_trace[level];
			mbr_changed = evt_node_rect_update(tcx,
						trace->tr_node, trace->tr_at,
						evt_node_mbr_get(tcx, nm_cur));
			/* continue to insert the new node to its parent */
			continue;
		}

		D_DEBUG(DB_TRACE, "Create a new root, depth=%d.\n",
			tcx->tc_root->tr_depth + 1);

		D__ASSERT(evt_node_is_root(tcx, nm_cur));
		evt_node_unset(tcx, nm_cur, EVT_NODE_ROOT);

		rc = evt_node_alloc(tcx, EVT_NODE_ROOT, &nm_new);
		if (rc != 0)
			D__GOTO(failed, rc);

		rc = evt_node_insert(tcx, nm_new, &entry, NULL);
		if (rc != 0)
			D__GOTO(failed, rc);

		evt_tcx_set_dep(tcx, tcx->tc_depth + 1);
		tcx->tc_trace->tr_node = nm_new;
		tcx->tc_trace->tr_at = 0;

		tcx->tc_root->tr_node = nm_new;
		tcx->tc_root->tr_depth++;

		/* continue the loop and insert the original root node into
		 * the new root node.
		 */
		entry.en_rect = *evt_node_mbr_get(tcx, nm_cur);
		entry.en_mmid = umem_id_t2u(nm_cur);
	}
 out:
	D_EXIT;
	return 0;
 failed:
	D_ERROR("Failed to insert entry to level %d: %d\n", level, rc);
	return rc;
}

/** Insert a single entry to evtree */
static int
evt_insert_entry(struct evt_context *tcx, struct evt_entry *ent)
{
	TMMID(struct evt_node)	 nd_mmid;
	int			 level;
	int			 i;

	D_DEBUG(DB_TRACE, "Inserting rectangle "DF_RECT"\n",
		DP_RECT(&ent->en_rect));

	evt_tcx_reset_trace(tcx);
	nd_mmid = tcx->tc_trace->tr_node; /* NB: trace points at root node */
	level = 0;

	while (1) {
		struct evt_node		*nd;
		TMMID(struct evt_node)	 nm_cur;
		TMMID(struct evt_node)	 nm_dst;
		int			 tr_at;

		if (evt_node_is_leaf(tcx, nd_mmid)) {
			evt_tcx_set_trace(tcx, level, nd_mmid, 0);
			break;
		}

		tr_at = -1;
		nm_dst = EVT_NODE_NULL;
		nd = evt_tmmid2ptr(tcx, nd_mmid);

		for (i = 0; i < nd->tn_nr; i++) {
			nm_cur = *evt_node_child_at(tcx, nd_mmid, i);
			if (TMMID_IS_NULL(nm_dst)) {
				nm_dst = nm_cur;
			} else {
				nm_dst = evt_select_node(tcx, &ent->en_rect,
							 nm_dst, nm_cur);
			}

			/* check if the current child is the new destination */
			if (umem_id_equal_typed(evt_umm(tcx), nm_dst, nm_cur))
				tr_at = i;
		}

		/* store the trace in case we need to bubble split */
		evt_tcx_set_trace(tcx, level, nd_mmid, tr_at);
		nd_mmid = nm_dst;
		level++;
	}
	D__ASSERT(level == tcx->tc_depth - 1);

	return evt_insert_or_split(tcx, ent);
}

/** Insert all entries attached on tcx::tc_ent_inserting to the evtree */
static int
evt_insert_entries(struct evt_context *tcx)
{
	int	rc;

	while (!d_list_empty(&tcx->tc_ent_inserting)) {
		struct evt_entry *ent;

		ent = d_list_entry(tcx->tc_ent_inserting.next, struct evt_entry,
				   en_link);
		d_list_del_init(&ent->en_link);

		rc = evt_insert_entry(tcx, ent);
		if (rc != 0)
			D__GOTO(failed, rc);
	}
	D_EXIT;
	return 0;
failed:
	D_DEBUG(DB_IO, "Failed to add rect list: %d\n", rc);
	return rc;
}

/**
 * Insert a rectangle \a rect and data pointed by \a ptr_mmid to the tree.
 * It consists of two phases:
 *
 * - clipping phase:
 *   clip all overlapped rectangles, attach rectangles ready to be inserted
 *   on tcx::tc_ent_inserting.
 *
 * - inserting phase:
 *   Insert all rectangles attached on tcx::tc_ent_inserting.
 */
static int
evt_insert_ptr(struct evt_context *tcx, struct evt_rect *rect,
	       TMMID(struct evt_ptr) ptr_mmid, TMMID(struct evt_ptr) *out_mmid)
{
	struct evt_entry ent;
	int		 rc;

	D_INIT_LIST_HEAD(&tcx->tc_ent_clipping);
	D_INIT_LIST_HEAD(&tcx->tc_ent_inserting);
	evt_ent_list_init(&tcx->tc_ent_list);

	if (tcx->tc_depth == 0) { /* empty tree */
		rc = evt_root_activate(tcx);
		if (rc != 0)
			D__GOTO(failed, rc);
	}

	memset(&ent, 0, sizeof(ent));
	ent.en_mmid = umem_id_t2u(ptr_mmid);
	ent.en_rect = *rect;
	if (ent.en_rect.rc_epc_hi == 0)
		ent.en_rect.rc_epc_hi = DAOS_EPOCH_MAX;

	/* Phase-1: Clipping */
	rc = evt_clip_entry(tcx, &ent);
	if (rc != 0)
		D__GOTO(failed, rc);

	if (out_mmid)
		*out_mmid = umem_id_u2t(ent.en_mmid, struct evt_ptr);

	D__ASSERT(d_list_empty(&tcx->tc_ent_clipping));
	/* Phase-2: Inserting */
	rc = evt_insert_entries(tcx);
	if (rc != 0)
		D__GOTO(failed, rc);

	D__ASSERT(d_list_empty(&tcx->tc_ent_inserting));
	D_EXIT;
 failed:
	evt_ent_list_fini(&tcx->tc_ent_list);
	return rc;
}

/**
 * Insert a versioned extent (rectangle) and its data mmid into the tree.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_insert(daos_handle_t toh, uuid_t cookie, uint32_t pm_ver,
	   struct evt_rect *rect, uint32_t inob, umem_id_t mmid)
{
	struct evt_context	*tcx;
	TMMID(struct evt_ptr)	 ptr_mmid;
	int			 rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = evt_ptr_create(tcx, cookie, pm_ver, mmid, inob,
			    evt_rect_width(rect), &ptr_mmid);
	if (rc != 0)
		return rc;

	rc = evt_insert_ptr(tcx, rect, ptr_mmid, NULL);
	if (rc != 0)
		D__GOTO(failed, rc);

	D__RETURN(0);
 failed:
	evt_ptr_free(tcx, ptr_mmid, false);
	return rc;
}

/**
 * Insert a versioned extent \a rect to the evtree and copy its data from the
 * scatter/gather list \a sgl.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_insert_sgl(daos_handle_t toh, uuid_t cookie, uint32_t pm_ver,
	       struct evt_rect *rect, uint32_t inob, daos_sg_list_t *sgl)
{
	struct evt_context	*tcx;
	TMMID(struct evt_ptr)	 ptr_mmid;
	TMMID(struct evt_ptr)	 out_mmid;
	int			 rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = evt_ptr_create(tcx, cookie, pm_ver, UMMID_NULL, inob,
			    evt_rect_width(rect), &ptr_mmid);
	if (rc != 0)
		return rc;

	rc = evt_ptr_copy_sgl(tcx, ptr_mmid, sgl);
	if (rc != 0)
		D__GOTO(failed, rc);

	rc = evt_insert_ptr(tcx, rect, ptr_mmid, &out_mmid);
	if (rc != 0)
		D__GOTO(failed, rc);

	if (!umem_id_equal_typed(evt_umm(tcx), ptr_mmid, out_mmid)) {
		struct evt_ptr	*ptr = evt_tmmid2ptr(tcx, ptr_mmid);

		if (UMMID_IS_NULL(ptr->pt_mmid)) {
			if (sgl->sg_iovs[0].iov_buf ==
				evt_ptr_payload(tcx, ptr_mmid, NULL, NULL)) {
				memset(&sgl->sg_iovs[0], 0,
				       sizeof(sgl->sg_iovs[0]));
				rc = evt_ptr_copy_sgl(tcx, out_mmid, sgl);
				if (rc)
					D__GOTO(failed, rc);
			}
		}
		evt_ptr_free(tcx, ptr_mmid, false);
	}

	D__RETURN(0);
failed:
	evt_ptr_free(tcx, ptr_mmid, true);
	return rc;
}

/** Fill the entry with the extent at the specified position of \a nd_mmid */
void
evt_fill_entry(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
	       unsigned int at, struct evt_rect *rect_srch,
	       struct evt_entry *entry)
{
	struct evt_ptr_ref *pref;
	struct evt_ptr	   *ptr;
	struct evt_rect	   *rect;
	void		   *addr;
	daos_off_t	    offset;
	daos_size_t	    width;
	daos_size_t	    nr;

	pref = evt_node_pref_at(tcx, nd_mmid, at);
	rect = evt_node_rect_at(tcx, nd_mmid, at);
	ptr  = evt_tmmid2ptr(tcx, pref->pr_ptr_mmid);

	offset = 0;
	width = evt_rect_width(rect);

	if (rect_srch && rect_srch->rc_off_lo > rect->rc_off_lo) {
		offset = rect_srch->rc_off_lo - rect->rc_off_lo;
		D__ASSERTF(width > offset, DF_U64"/"DF_U64"\n", width, offset);
		width -= offset;
	}

	if (rect_srch && rect_srch->rc_off_hi < rect->rc_off_hi) {
		nr = rect->rc_off_hi - rect_srch->rc_off_hi;
		D__ASSERTF(width > nr, DF_U64"/"DF_U64"\n", width, nr);
		width -= nr;
	}

	entry->en_rect = *rect;
	entry->en_rect.rc_off_lo += offset;
	entry->en_rect.rc_off_hi = entry->en_rect.rc_off_lo + width - 1;

	entry->en_mmid = umem_id_t2u(pref->pr_ptr_mmid);
	uuid_copy(entry->en_cookie, ptr->pt_cookie);
	entry->en_ver = ptr->pt_ver;

	addr = evt_ptr_payload(tcx, pref->pr_ptr_mmid, &entry->en_inob, NULL);
	if (addr == NULL) { /* punched */
		entry->en_addr   = NULL;
		entry->en_offset = 0;

	} else {
		D__ASSERT(entry->en_inob != 0);
		entry->en_offset = pref->pr_offset + offset;
		entry->en_addr   = addr + entry->en_offset * entry->en_inob;
	}
}

/**
 * Find all versioned extents which intercept with the input one \a rect.
 * It attaches all found extents and their data pointers on \a ent_list if
 * \a no_overlap is false, otherwise returns error if there is any overlapped
 * extent.
 *
 * \param rect		[IN]	Rectangle to check.
 * \param no_overlap	[IN]	Returns error if \a rect overlap with any
 *				existent extent.
 * \param ent_list	[OUT]	The returned entries for overlapped extents.
 */
int
evt_find_ent_list(struct evt_context *tcx, enum evt_find_opc find_opc,
		  struct evt_rect *rect, struct evt_entry_list *ent_list)
{
	TMMID(struct evt_node)	 nd_mmid;
	int			 level;
	int			 at;
	int			 i;
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "Searching rectangle "DF_RECT"\n", DP_RECT(rect));
	if (tcx->tc_root->tr_depth == 0)
		return 0; /* empty tree */

	evt_tcx_reset_trace(tcx);

	level = at = 0;
	nd_mmid = tcx->tc_root->tr_node;
	while (1) {
		struct evt_rect *mbr;
		struct evt_node	*node;
		bool		 leaf;

		node = evt_tmmid2ptr(tcx, nd_mmid);
		leaf = evt_node_is_leaf(tcx, nd_mmid);
		mbr  = evt_node_mbr_get(tcx, nd_mmid);

		D__ASSERT(!leaf || at == 0);
		D_DEBUG(DB_TRACE,
			"Checking "DF_RECT"("TMMID_PF"), l=%d, a=%d, f=%d\n",
			DP_RECT(mbr), TMMID_P(nd_mmid), level, at, leaf);

		for (i = at; i < node->tn_nr; i++) {
			struct evt_entry	*ent;
			struct evt_rect		*rtmp;
			int			 overlap;

			rtmp = evt_node_rect_at(tcx, nd_mmid, i);
			D_DEBUG(DB_TRACE, " rect[%d]="DF_RECT"\n",
				i, DP_RECT(rtmp));

			overlap = evt_rect_overlap(rtmp, rect, leaf);
			switch (overlap) {
			default:
				D__ASSERT(0);
			case RT_OVERLAP_INVAL:
				return -DER_INVAL;

			case RT_OVERLAP_NO:
				continue; /* search the next one */

			case RT_OVERLAP_INCLUDED:
				D__ASSERT(!leaf);
				/* fall through */
			case RT_OVERLAP_YES:
			case RT_OVERLAP_SAME:
			case RT_OVERLAP_INPLACE:
			case RT_OVERLAP_EXPAND:
			case RT_OVERLAP_CAPPING:
			case RT_OVERLAP_CAPPED:
				break; /* overlapped */
			}

			if (!leaf) {
				/* break the internal loop and enter the
				 * child node.
				 */
				D_DEBUG(DB_TRACE, "Enter the next level\n");
				break;
			}
			D_DEBUG(DB_TRACE, "Found overlapped leaf rect\n");

			/* early check */
			switch (find_opc) {
			default:
				D__ASSERTF(0, "%d\n", find_opc);
			case EVT_FIND_CAP:
				if (overlap == RT_OVERLAP_CAPPED) {
					/* Trying to insert an extent which is
					 * fully overwriten by an in-tree
					 * extent. We just modify the high
					 * epoch of the current extent and
					 * continue the scan.
					 */
					rect->rc_epc_hi = rtmp->rc_epc_lo - 1;
					continue;
				}

				if (overlap == RT_OVERLAP_CAPPING ||
				    overlap == RT_OVERLAP_SAME)
					break; /* matched */

				D_DEBUG(DB_IO, "Invalid overlap for capping :"
					DF_RECT" overlaps with "DF_RECT"\n",
					DP_RECT(rect), DP_RECT(rtmp));
				D__GOTO(out, rc = -DER_NO_PERM);

			case EVT_FIND_SAME:
				if (overlap != RT_OVERLAP_SAME)
					continue;
				break;

			case EVT_FIND_FIRST:
			case EVT_FIND_ALL:
				break;
			}

			ent = evt_ent_list_alloc(ent_list);
			if (ent == NULL)
				D__GOTO(out, rc = -DER_NOMEM);

			evt_fill_entry(tcx, nd_mmid, i, rect, ent);
			switch (find_opc) {
			default:
				D__ASSERTF(0, "%d\n", find_opc);
			case EVT_FIND_FIRST:
			case EVT_FIND_SAME:
				/* store the trace and return for clip or
				 * iteration.
				 * NB: clip is not implemented yet.
				 */
				evt_tcx_set_trace(tcx, level, nd_mmid, i);
				D__GOTO(out, rc = 0);

			case EVT_FIND_ALL:
				break;

			case EVT_FIND_CAP:
				/* store the intree rectangle and cap all
				 * them together.
				 */
				ent->en_addr = rtmp;
				break;
			}
		}

		if (i < node->tn_nr) {
			/* overlapped with a non-leaf node, dive into it. */
			evt_tcx_set_trace(tcx, level, nd_mmid, i);
			nd_mmid = *evt_node_child_at(tcx, nd_mmid, i);
			at = 0;
			level++;

		} else {
			struct evt_trace *trace;

			if (level == 0) { /* done with the root */
				D_DEBUG(DB_TRACE, "Found total %d rects\n",
					ent_list ? ent_list->el_ent_nr : 0);
				return 0; /* succeed and return */
			}

			level--;
			trace = evt_tcx_trace(tcx, level);
			nd_mmid = trace->tr_node;
			at = trace->tr_at + 1;
			D__ASSERT(at <= tcx->tc_order);
		}
	}
	D_EXIT;
out:
	if (rc != 0)
		evt_ent_list_fini(ent_list);
	return rc;
}

/**
 * Find all versioned extents intercepting with the input rectangle \a rect
 * and return their data pointers.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_find(daos_handle_t toh, struct evt_rect *rect,
	 struct evt_entry_list *ent_list)
{
	struct evt_context *tcx;
	int		    rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	evt_ent_list_init(ent_list);
	rc = evt_find_ent_list(tcx, EVT_FIND_ALL, rect, ent_list);
	if (rc != 0) {
		evt_ent_list_fini(ent_list);
		D__GOTO(out, rc);
	}
	evt_ent_list_sort(ent_list);
	D_EXIT;
 out:
	return rc;
}

/** move the probing trace forward or backward */
bool
evt_move_trace(struct evt_context *tcx, bool forward)
{
	struct evt_trace	*trace;
	struct evt_node		*nd;
	TMMID(struct evt_node)	 nd_mmid;

	if (evt_root_empty(tcx))
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	while (1) {
		nd_mmid = trace->tr_node;
		nd = evt_tmmid2ptr(tcx, nd_mmid);

		/* already reach at the begin or end of this node */
		if ((trace->tr_at == (nd->tn_nr - 1) && forward) ||
		    (trace->tr_at == 0 && !forward)) {
			if (evt_node_is_root(tcx, nd_mmid)) {
				D__ASSERT(trace == tcx->tc_trace);
				D_DEBUG(DB_TRACE, "End\n");
				return false;
			}
			/* check its parent */
			trace--;
			continue;
		} /* else: not yet */

		if (forward)
			trace->tr_at++;
		else
			trace->tr_at--;
		break;
	}

	/* move to the first/last entry in the subtree */
	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		TMMID(struct evt_node) tmp;

		tmp = *evt_node_child_at(tcx, trace->tr_node, trace->tr_at);
		nd = evt_tmmid2ptr(tcx, tmp);
		D__ASSERTF(nd->tn_nr != 0, "%d\n", nd->tn_nr);

		trace++;
		trace->tr_at = forward ? 0 : nd->tn_nr - 1;
		trace->tr_node = tmp;
	}

	D_EXIT;
	return true;
}

/**
 * Open a tree by memory ID @root_mmid.
 * Please check API comment in evtree.h for the details.
 */
int
evt_open(TMMID(struct evt_root) root_mmid, struct umem_attr *uma,
	 daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	rc = evt_tcx_create(root_mmid, NULL, -1, -1, uma, &tcx);
	if (rc != 0)
		return rc;

	*toh = evt_tcx2hdl(tcx); /* take refcount for open */
	evt_tcx_decref(tcx); /* -1 for create */
	return 0;
}

/**
 * Open a inplace tree by root address @root.
 * Please check API comment in evtree.h for the details.
 */
int
evt_open_inplace(struct evt_root *root, struct umem_attr *uma,
		 daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (root->tr_order == 0) {
		D_DEBUG(DB_TRACE, "Tree order is zero\n");
		return -DER_INVAL;
	}

	rc = evt_tcx_create(EVT_ROOT_NULL, root, -1, -1, uma, &tcx);
	if (rc != 0)
		return rc;

	*toh = evt_tcx2hdl(tcx);
	evt_tcx_decref(tcx); /* -1 for tcx_create */
	return 0;
}

/**
 * Close a tree open handle.
 * Please check API comment in evtree.h for the details.
 */
int
evt_close(daos_handle_t toh)
{
	struct evt_context *tcx;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	evt_tcx_decref(tcx); /* -1 for open/create */
	return 0;
}

/**
 * Create a new tree and open it.
 * Please check API comment in evtree.h for the details.
 */
int
evt_create(uint64_t feats, unsigned int order, struct umem_attr *uma,
	   TMMID(struct evt_root) *root_mmid_p, daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (!(feats & EVT_FEAT_SORT_SOFF)) {
		D_DEBUG(DB_TRACE, "Unknown feature bits "DF_X64"\n", feats);
		return -DER_INVAL;
	}

	if (order < EVT_ORDER_MIN || order > EVT_ORDER_MAX) {
		D_DEBUG(DB_TRACE, "Invalid tree order %d\n", order);
		return -DER_INVAL;
	}

	rc = evt_tcx_create(EVT_ROOT_NULL, NULL, feats, order, uma, &tcx);
	if (rc != 0)
		return rc;

	rc = evt_root_alloc(tcx);
	if (rc != 0)
		D__GOTO(out, rc);

	*root_mmid_p = tcx->tc_root_mmid;
	*toh = evt_tcx2hdl(tcx); /* take refcount for open */
	D_EXIT;
 out:
	evt_tcx_decref(tcx); /* -1 for tcx_create */
	return rc;
}

/**
 * Create a new tree inplace of \a root, return the open handle.
 * Please check API comment in evtree.h for the details.
 */
int
evt_create_inplace(uint64_t feats, unsigned int order, struct umem_attr *uma,
		   struct evt_root *root, daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (!(feats & EVT_FEAT_SORT_SOFF)) {
		D_DEBUG(DB_TRACE, "Unknown feature bits "DF_X64"\n", feats);
		return -DER_INVAL;
	}

	if (order < EVT_ORDER_MIN || order > EVT_ORDER_MAX) {
		D_DEBUG(DB_TRACE, "Invalid tree order %d\n", order);
		return -DER_INVAL;
	}

	rc = evt_tcx_create(EVT_ROOT_NULL, root, feats, order, uma, &tcx);
	if (rc != 0)
		return rc;

	rc = evt_root_init(tcx);
	if (rc != 0)
		D__GOTO(out, rc);

	*toh = evt_tcx2hdl(tcx); /* take refcount for open */
	D_EXIT;
 out:
	evt_tcx_decref(tcx); /* -1 for tcx_create */
	return rc;
}

/**
 * Destroy the tree associated with the open handle.
 * Please check API comment in evtree.h for the details.
 */
int
evt_destroy(daos_handle_t toh)
{
	struct evt_context *tcx;
	int		    rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = evt_root_destroy(tcx);

	evt_tcx_decref(tcx);
	return rc;
}

/** Output tree node status */
static void
evt_node_debug(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
	       int cur_level, int debug_level)
{
	struct evt_node *nd;
	int		 i;
	bool		 leaf;

	nd = evt_tmmid2ptr(tcx, nd_mmid);
	leaf = evt_node_is_leaf(tcx, nd_mmid);

	/* NB: debug_level < 0 means output debug info for all levels,
	 * otherwise only output debug info for the specified tree level.
	 */
	if (leaf || cur_level == debug_level || debug_level < 0) {
		struct evt_rect *rect;

		rect = evt_node_mbr_get(tcx, nd_mmid);
		D__PRINT("node="TMMID_PF", lvl=%d, mbr="DF_RECT", rect_nr=%d\n",
			TMMID_P(nd_mmid), cur_level, DP_RECT(rect), nd->tn_nr);

		if (leaf || cur_level == debug_level)
			return;
	}

	for (i = 0; i < nd->tn_nr; i++) {
		TMMID(struct evt_node)   child_mmid;

		child_mmid = *evt_node_child_at(tcx, nd_mmid, i);
		evt_node_debug(tcx, child_mmid, cur_level + 1, debug_level);
	}
}

/**
 * Output status of tree nodes at level \a debug_level.
 * All nodes will be printed out if \a debug_level is negative.
 */
int
evt_debug(daos_handle_t toh, int debug_level)
{
	struct evt_context *tcx;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	D__PRINT("Tree depth=%d, order=%d, feats="DF_X64"\n",
		tcx->tc_depth, tcx->tc_order, tcx->tc_feats);

	if (!TMMID_IS_NULL(tcx->tc_root->tr_node))
		evt_node_debug(tcx, tcx->tc_root->tr_node, 0, debug_level);

	return 0;
}

/**
 * Tree policies
 *
 * Only support SSOF for now (see below).
 */

/**
 * Sorted by Start OFfset (SSOF)
 *
 * Assume there is no overwrite, all versioned extents are sorted by start
 * offset in the tree.
 */

/** Rectangle comparison for sorting */
static int
evt_ssof_cmp_rect(struct evt_context *tcx, struct evt_rect *rt1,
		  struct evt_rect *rt2)
{
	if (rt1->rc_off_lo < rt2->rc_off_lo)
		return -1;

	if (rt1->rc_off_lo > rt2->rc_off_lo)
		return 1;

	if (rt1->rc_off_hi < rt2->rc_off_hi)
		return -1;

	if (rt1->rc_off_hi > rt2->rc_off_hi)
		return 1;

	if (rt1->rc_epc_lo < rt2->rc_epc_lo)
		return -1;

	if (rt1->rc_epc_lo > rt2->rc_epc_lo)
		return 1;

	return 0;
}

static int
evt_ssof_insert(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		struct evt_entry *ent)
{
	struct evt_node		*nd   = evt_tmmid2ptr(tcx, nd_mmid);
	struct evt_rect		*rect = NULL;
	struct evt_ptr_ref	*pref = NULL;
	TMMID(struct evt_node)	*nmid = NULL;
	int			 i;
	int			 rc;
	bool			 leaf;

	D__ASSERT(!evt_node_is_full(tcx, nd_mmid));

	leaf = evt_node_is_leaf(tcx, nd_mmid);

	/* NB: can use binary search to optimize */
	for (i = 0; i < nd->tn_nr; i++) {
		int	nr;

		rect = evt_node_rect_at(tcx, nd_mmid, i);
		rc = evt_ssof_cmp_rect(tcx, rect, &ent->en_rect);
		if (rc < 0)
			continue;

		nr = nd->tn_nr - i;
		memmove(rect + 1, rect, nr * sizeof(*rect));
		if (leaf) {
			pref = evt_node_pref_at(tcx, nd_mmid, i);
			memmove(pref + 1, pref, nr * sizeof(*pref));
		} else {
			nmid = evt_node_child_at(tcx, nd_mmid, i);
			memmove(nmid + 1, nmid, nr * sizeof(*nmid));
		}
		break;
	}

	if (i == nd->tn_nr) { /* attach at the end */
		rect = evt_node_rect_at(tcx, nd_mmid, nd->tn_nr);
		if (leaf)
			pref = evt_node_pref_at(tcx, nd_mmid, nd->tn_nr);
		else
			nmid = evt_node_child_at(tcx, nd_mmid, nd->tn_nr);
	}

	*rect = ent->en_rect;
	if (leaf) {
		pref->pr_offset   = ent->en_offset;
		pref->pr_inum	  = evt_rect_width(&ent->en_rect);
		pref->pr_ptr_mmid = umem_id_u2t(ent->en_mmid, struct evt_ptr),
		evt_ptr_addref(tcx, pref->pr_ptr_mmid);
	} else {
		*nmid = umem_id_u2t(ent->en_mmid, struct evt_node);
	}

	nd->tn_nr++;
	return 0;
}

static int
evt_ssof_split(struct evt_context *tcx, bool leaf,
	       TMMID(struct evt_node) src_mmid,
	       TMMID(struct evt_node) dst_mmid)
{
	struct evt_node	   *nd_src = evt_tmmid2ptr(tcx, src_mmid);
	struct evt_node	   *nd_dst = evt_tmmid2ptr(tcx, dst_mmid);
	struct evt_rect	   *rt_src;
	struct evt_rect	   *rt_dst;
	int		    nr;

	D__ASSERT(nd_src->tn_nr == tcx->tc_order);
	nr = nd_src->tn_nr / 2;
	/* give one more entry to the left (original) node if tree order is
	 * odd, because "append" could be the most common use-case at here,
	 * which means new entres will never be inserted into the original
	 * node. So we want to utilize the original as much as possible.
	 */
	nr += (nd_src->tn_nr % 2 != 0);

	rt_src = evt_node_rect_at(tcx, src_mmid, nr);
	rt_dst = evt_node_rect_at(tcx, dst_mmid, 0);
	memcpy(rt_dst, rt_src, sizeof(*rt_dst) * (nd_src->tn_nr - nr));

	if (leaf) {
		struct evt_ptr_ref	*src;
		struct evt_ptr_ref	*dst;

		src = evt_node_pref_at(tcx, src_mmid, nr);
		dst = evt_node_pref_at(tcx, dst_mmid, 0);
		memcpy(dst, src, sizeof(*dst) * (nd_src->tn_nr - nr));
	} else {
		TMMID(struct evt_node)	*src;
		TMMID(struct evt_node)	*dst;

		src = evt_node_child_at(tcx, src_mmid, nr);
		dst = evt_node_child_at(tcx, dst_mmid, 0);
		memcpy(dst, src, sizeof(*dst) * (nd_src->tn_nr - nr));
	}

	nd_dst->tn_nr = nd_src->tn_nr - nr;
	nd_src->tn_nr = nr;
	return 0;
}

static int
evt_ssof_rect_weight(struct evt_context *tcx, struct evt_rect *rect,
		     struct evt_weight *weight)
{
	memset(weight, 0, sizeof(*weight));
	weight->wt_major = rect->rc_off_hi - rect->rc_off_lo;
	/* NB: we don't consider about high epoch for SSOF because it's based
	 * on assumption there is no overwrite.
	 */
	weight->wt_minor = -rect->rc_epc_lo;
	return 0;
}

static struct evt_policy_ops evt_ssof_pol_ops = {
	.po_insert		= evt_ssof_insert,
	.po_split		= evt_ssof_split,
	.po_rect_weight		= evt_ssof_rect_weight,
};
