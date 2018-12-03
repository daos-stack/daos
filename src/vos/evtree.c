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
#define D_LOGFAC	DD_FAC(vos)

#include "evt_priv.h"
#include "vos_internal.h"

enum {
	/** no overlap */
	RT_OVERLAP_NO		= 0,
	/* set if rt1 range or epoch matches rt2 */
	RT_OVERLAP_SAME		= (1 << 1),
	/* set if rt1 is before rt2 */
	RT_OVERLAP_OVER		= (1 << 2),
	/* set if rt1 is after rt2 */
	RT_OVERLAP_UNDER	= (1 << 3),
	/* set if rt1 range includes rt2 */
	RT_OVERLAP_INCLUDED	= (1 << 4),
	/* set if rt2 range includes rt1 */
	RT_OVERLAP_INCLUDES	= (1 << 5),
	/* set if rt2 range overlaps rt1 */
	RT_OVERLAP_PARTIAL	= (1 << 6),
};

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

/**
 * Returns true if the first rectangle \a rt1 is at least as wide as the second
 * rectangle \a rt2.
 */
static bool
evt_rect_is_wider(struct evt_rect *rt1, struct evt_rect *rt2)
{
	return (rt1->rc_off_lo <= rt2->rc_off_lo &&
		rt1->rc_off_hi >= rt2->rc_off_hi);
}

static bool
evt_rect_same_extent(struct evt_rect *rt1, struct evt_rect *rt2)
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
static void
evt_rect_overlap(struct evt_rect *rt1, struct evt_rect *rt2, int *range,
		 int *time)
{
	*time = *range = RT_OVERLAP_NO;

	if (rt1->rc_off_lo > rt2->rc_off_hi || /* no offset overlap */
	    rt1->rc_off_hi < rt2->rc_off_lo)
		return;

	/* NB: By definition, there is always epoch overlap since all
	 * updates are from epc to INF.  Determine here what kind
	 * of overlap exists.
	 */
	if (rt1->rc_epc == rt2->rc_epc)
		*time = RT_OVERLAP_SAME;
	else if (rt1->rc_epc < rt2->rc_epc)
		*time = RT_OVERLAP_OVER;
	else
		*time = RT_OVERLAP_UNDER;

	if (evt_rect_same_extent(rt1, rt2))
		*range = RT_OVERLAP_SAME;
	else if (evt_rect_is_wider(rt1, rt2))
		*range = RT_OVERLAP_INCLUDED;
	else if (evt_rect_is_wider(rt2, rt1))
		*range = RT_OVERLAP_INCLUDES;
	else
		*range = RT_OVERLAP_PARTIAL;
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

	if (rt1->rc_epc > rt2->rc_epc) {
		rt1->rc_epc = rt2->rc_epc;
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
	ent_list->el_ents = ent_list->el_embedded_ents;
}

/** Finalize an entry list */
void
evt_ent_list_fini(struct evt_entry_list *ent_list)
{
	if (ent_list->el_size)
		D_FREE(ent_list->el_ents);

	D_INIT_LIST_HEAD(&ent_list->el_list);
	ent_list->el_size = ent_list->el_ent_nr = 0;
}

/**
 * Take an embedded entry, or allocate a new entry if all embedded entries
 * have been taken.
 */
static struct evt_entry *
evt_ent_list_alloc(struct evt_context *tcx, struct evt_entry_list *ent_list)
{
	uint32_t		 size;
	int			 i;

	if (ent_list->el_ent_nr == EVT_EMBEDDED_NR) {
		D_ASSERT(ent_list->el_size == 0);
		/* Transition to allocated array.
		 * Insert enough entries to fit everything in the tree.  Most
		 * space will be wated in practice but it's just virtual address
		 * space and it's ephemeral
		 */
		size = 1;
		for (i = 0; i < tcx->tc_depth; i++)
			size *= tcx->tc_order;
		/* With splitting, we need 2x the space in worst case.  Each new
		 * extent inserted can add at most 1 extent to the output. The
		 * cases are:
		 * 1. New extent covers existing one:   1 visible, 1 covered
		 * 2. New extent splits existing one:   3 visible, 0 covered
		 * 3. New extent overlaps existing one: 2 visible, 0 covered
		 * 4. New extent covers nothing:        1 visible, 0 covered
		 *
		 * So, each new extent can add at most 1 new rectangle (as in
		 * case #2.   So if we allocate 2x the max entries in the tree,
		 * we will always have sufficient space to store entries.
		 */
		size *= 2;
		D_ALLOC_ARRAY(ent_list->el_ents, size);
		if (ent_list->el_ents == NULL)
			return NULL;

		/* Copy the embedded entries over to new array list */
		memcpy(ent_list->el_ents, ent_list->el_embedded_ents,
		       sizeof(ent_list->el_ents[0]) * EVT_EMBEDDED_NR);
		ent_list->el_size = size;
	}
	D_ASSERT(ent_list->el_ent_nr < EVT_EMBEDDED_NR ||
		 ent_list->el_ent_nr < ent_list->el_size);

	return &ent_list->el_ents[ent_list->el_ent_nr++];
}

static int
evt_cmp_rect_helper(const struct evt_rect *rt1, const struct evt_rect *rt2)
{
	if (rt1->rc_off_lo < rt2->rc_off_lo)
		return -1;

	if (rt1->rc_off_lo > rt2->rc_off_lo)
		return 1;

	if (rt1->rc_epc > rt2->rc_epc)
		return -1;

	if (rt1->rc_epc < rt2->rc_epc)
		return 1;

	if (rt1->rc_off_hi < rt2->rc_off_hi)
		return -1;

	if (rt1->rc_off_hi > rt2->rc_off_hi)
		return 1;

	return 0;
}

int evt_ent_cmp(const void *p1, const void *p2)
{
	const struct evt_entry	*ent1	= p1;
	const struct evt_entry	*ent2	= p2;

	return evt_cmp_rect_helper(&ent1->en_sel_rect, &ent2->en_sel_rect);
}

/* Use top bit of inob field to temporarily mark a partial rectangle as part
 * of another rectangle so we don't return it in the covered list
 */
#define EVT_PARTIAL_FLAG (1 << 31)

static struct evt_entry *
evt_find_next_uncovered(struct evt_entry *this_ent, d_list_t *head,
			d_list_t **next, d_list_t *free_list, int *flag_bit)
{
	d_list_t		*temp;
	struct evt_rect		*this_rect;
	struct evt_rect		*next_rect;
	struct evt_entry	*next_ent;
	struct evt_ptr		*next_ptr;

	while (*next != head) {
		next_ent = d_list_entry(*next, struct evt_entry, en_link);
		next_ptr = &next_ent->en_ptr;

		/* NB: Flag is set if part of the extent is visible */
		*flag_bit = next_ptr->pt_inob & EVT_PARTIAL_FLAG;
		next_ptr->pt_inob &= ~(EVT_PARTIAL_FLAG);

		this_rect = &this_ent->en_sel_rect;
		next_rect = &next_ent->en_sel_rect;
		if (next_rect->rc_epc > this_rect->rc_epc)
			return next_ent; /* next_ent is a later update */
		if (next_rect->rc_off_hi > this_rect->rc_off_hi)
			return next_ent; /* next_ent extends past end */

		temp = *next;
		*next = temp->next;
		if (*flag_bit) /* NB: part of the extent is visible */
			d_list_move(temp, free_list);

	}

	return NULL;
}

static struct evt_entry *
evt_get_unused_entry(struct evt_context *tcx, struct evt_entry_list *ent_list,
		     d_list_t *unused)
{
	d_list_t *entry;

	if (d_list_empty(unused))
		return evt_ent_list_alloc(tcx, ent_list);

	entry = unused->next;
	d_list_del(entry);

	return d_list_entry(entry, struct evt_entry, en_link);
}

static void
evt_split_entry(struct evt_entry *current, struct evt_entry *next,
		struct evt_entry *split)
{
	struct evt_ptr	*ptr = &split->en_ptr;
	daos_off_t	 diff;

	*split = *current;
	diff = next->en_sel_rect.rc_off_hi + 1 - split->en_sel_rect.rc_off_lo;
	split->en_sel_rect.rc_off_lo = next->en_sel_rect.rc_off_hi + 1;
	ptr->pt_ex_addr.ba_off += diff * ptr->pt_inob;
	/* Mark the split entry so we don't keep it in covered list */
	ptr->pt_inob |= EVT_PARTIAL_FLAG;

	current->en_sel_rect.rc_off_hi = next->en_sel_rect.rc_off_lo - 1;
}

static d_list_t *
evt_insert_sorted(struct evt_entry *this_ent, d_list_t *head, d_list_t *current)
{
	d_list_t		*start = current;
	struct evt_entry	*next_ent;
	int			 cmp;

	while (current != head) {
		next_ent = d_list_entry(current, struct evt_entry, en_link);
		cmp = evt_cmp_rect_helper(&this_ent->en_sel_rect,
					       &next_ent->en_sel_rect);
		if (cmp < 0) {
			d_list_add(&this_ent->en_link, current->prev);
			goto out;
		}
		current = current->next;
	}
	d_list_add_tail(&this_ent->en_link, head);
out:
	if (start == current)
		return &this_ent->en_link;
	return start;
}

static int
evt_uncover_entries(struct evt_context *tcx, struct evt_entry_list *ent_list,
		    d_list_t *covered)
{
	struct evt_rect		*this_rect;
	struct evt_rect		*next_rect;
	struct evt_entry	*this_ent;
	struct evt_entry	*next_ent;
	struct evt_entry	*temp_ent;
	struct evt_ptr		*ptr;
	d_list_t		*current;
	d_list_t		*next;
	d_list_t		 unused;
	daos_size_t		 diff;
	int			 flag_bit;
	bool			 insert;

	D_INIT_LIST_HEAD(&unused);  /* list of entries that can be reclaimed */

	/* reset the linked list.  We'll reconstruct it */
	D_INIT_LIST_HEAD(&ent_list->el_list);

	insert = true;
	/* Now uncover entries */
	current = covered->next;
	/* Some compilers can't tell that this_ent will be initialized */
	this_ent = d_list_entry(current, struct evt_entry,
				en_link);
	next = current->next;

	while (next != covered) {
		if (insert) {
			this_ent = d_list_entry(current, struct evt_entry,
						en_link);
			d_list_move_tail(current, &ent_list->el_list);
		}

		insert = true;

		/* Find next uncovered rectangle */
		next_ent = evt_find_next_uncovered(this_ent, covered, &next,
						   &unused, &flag_bit);
		if (next_ent == NULL)
			return 0;

		this_rect = &this_ent->en_sel_rect;
		next_rect = &next_ent->en_sel_rect;
		current = next;
		next = current->next;
		/* NB: Three possibilities
		 * 1. No intersection.  Current entry is inserted in entirety
		 * 2. Partial intersection, next is earlier. Next is truncated
		 * 3. Partial intersection, next is later. Current is truncated
		 * 4. Current entry contains next_entry.  Current is split
		 * in two and both are truncated.
		 */
		if (next_rect->rc_off_lo >= this_rect->rc_off_hi + 1) {
			/* Case #1, entry already inserted, nothing to do */
		} else if (next_rect->rc_epc < this_rect->rc_epc) {
			/* Case #2, next rect is partially under this rect,
			 * Truncate left end of next_rec, reinsert.
			 */
			ptr = &next_ent->en_ptr;
			diff = this_rect->rc_off_hi + 1 - next_rect->rc_off_lo;
			next_rect->rc_off_lo = this_rect->rc_off_hi + 1;
			ptr->pt_ex_addr.ba_off +=
				diff * ptr->pt_inob;
			/* current now points at next_ent.  Remove it and
			 * reinsert it in the list in case truncation moved
			 * it to a new position
			 */
			d_list_del(current);
			next = evt_insert_sorted(next_ent, covered, next);
			/* Reset the flag bit */
			ptr->pt_inob |= flag_bit;

			/* Now we need to rerun this iteration without
			 * inserting this_ent again
			 */
			insert = false;
		} else if (next_rect->rc_off_hi >= this_rect->rc_off_hi) {
			/* Case #3, truncate entry */
			this_rect->rc_off_hi = next_rect->rc_off_lo - 1;
		} else {
			/* Case #4, split, insert tail into sorted list */
			temp_ent = evt_get_unused_entry(tcx, ent_list, &unused);
			if (temp_ent == NULL)
				return -DER_NOMEM;
			evt_split_entry(this_ent, next_ent, temp_ent);
			/* Current points at next_ent */
			next = evt_insert_sorted(temp_ent, covered, next);
		}
	}

	d_list_move_tail(current, &ent_list->el_list);

	return 0;
}

/** Place all entries into covered list in sorted order based on selected
 * range.   Then walk through the range to find only extents that are visible
 * and place them in the main list.   Update the selection bounds for visible
 * rectangles.
 */
static int
evt_ent_list_sort(struct evt_context *tcx, struct evt_entry_list *ent_list,
		  d_list_t *covered)
{
	struct evt_entry	*ents;
	uint32_t		 i;

	D_INIT_LIST_HEAD(covered);

	if (ent_list->el_ent_nr == 0)
		return 0;

	ents = ent_list->el_ents;
	if (ent_list->el_ent_nr == 1) {
		d_list_add_tail(&ents[0].en_link, &ent_list->el_list);
		return 0;
	}

	/* Sort the array first */
	qsort(ents, ent_list->el_ent_nr, sizeof(ents[0]), evt_ent_cmp);

	/* Now place all entries sorted in covered list */
	for (i = 0; i < ent_list->el_ent_nr; i++)
		d_list_add_tail(&ents[i].en_link, covered);

	/* Now separate entries into covered and visible */
	return evt_uncover_entries(tcx, ent_list, covered);
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
	D_ASSERT(tcx->tc_depth > 0);
	D_ASSERT(level >= 0 && level < tcx->tc_depth);
	D_ASSERT(&tcx->tc_trace[level] < &tcx->tc_traces[EVT_TRACE_MAX]);

	return &tcx->tc_trace[level];
}

static void
evt_tcx_set_trace(struct evt_context *tcx, int level,
		  TMMID(struct evt_node) nd_mmid, int at)
{
	struct evt_trace *trace;

	D_ASSERT(at >= 0 && at < tcx->tc_order);

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
 * \param info		[IN]	NVMe free space info
 * \param tcx_pp	[OUT]	The returned tree context
 */
static int
evt_tcx_create(TMMID(struct evt_root) root_mmid, struct evt_root *root,
	       uint64_t feats, unsigned int order, struct umem_attr *uma,
	       void *info, struct evt_context **tcx_pp)
{
	struct evt_context	*tcx;
	int			 depth;
	int			 rc;

	D_ALLOC_PTR(tcx);
	if (tcx == NULL)
		return -DER_NOMEM;

	tcx->tc_ref = 1; /* for the caller */
	tcx->tc_magic = EVT_HDL_ALIVE;

	/* XXX choose ops based on feature bits */
	tcx->tc_ops = evt_policies[0];

	rc = umem_class_init(uma, &tcx->tc_umm);
	if (rc != 0) {
		D_ERROR("Failed to setup mem class %d: %d\n", uma->uma_id, rc);
		D_GOTO(failed, rc);
	}
	tcx->tc_pmempool_uuid = umem_get_uuid(&tcx->tc_umm);
	tcx->tc_blks_info = info;

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
			    tcx->tc_blks_info, tcx_pp);
	return rc;
}

/**
 * Create a data pointer for extent address @mmid. It allocates buffer
 * if @mmid is NULL.
 *
 * \param addr		[IN]	Optional, address of the external buffer
 * \param idx_nob	[IN]	Number Of Bytes per index
 * \param idx_num	[IN]	Indicies within the extent
 * \param ptr_mmid_p	[OUT]	The returned memory ID of extent pointer.
 */
static int
evt_ptr_init(struct evt_context *tcx, uuid_t cookie, uint32_t pm_ver,
	     bio_addr_t addr, uint32_t idx_nob, uint64_t idx_num,
	     struct evt_ptr *ptr)
{
	D_ASSERT(idx_num > 0);
	D_ASSERTF((idx_nob && !bio_addr_is_hole(&addr)) ||
		  (!idx_nob && bio_addr_is_hole(&addr)), "nob: %u hole: %d\n",
		  idx_nob, bio_addr_is_hole(&addr));

	memset(ptr, 0, sizeof(*ptr));

	ptr->pt_inob = idx_nob;
	ptr->pt_inum = idx_num;
	uuid_copy(ptr->pt_cookie, cookie);
	ptr->pt_ver = pm_ver;
	ptr->pt_ex_addr = addr;

	return 0;
}

static int
evt_ptr_free(struct evt_context *tcx, struct evt_ptr *ptr)
{
	bio_addr_t	*addr = &ptr->pt_ex_addr;
	int		 rc = 0;

	if (bio_addr_is_hole(addr))
		return 0;

	if (addr->ba_type == BIO_ADDR_SCM) {
		umem_id_t mmid;

		mmid.pool_uuid_lo = tcx->tc_pmempool_uuid;
		mmid.off = addr->ba_off;
		rc = umem_free(evt_umm(tcx), mmid);
	} else {
		struct vea_space_info *vsi = tcx->tc_blks_info;
		uint64_t blk_off;
		uint32_t blk_cnt;

		D_ASSERT(addr->ba_type == BIO_ADDR_NVME);
		D_ASSERT(vsi != NULL);

		blk_off = vos_byte2blkoff(addr->ba_off);
		blk_cnt = vos_byte2blkcnt(ptr->pt_inum * ptr->pt_inob);

		rc = vea_free(vsi, blk_off, blk_cnt);
		if (rc)
			D_ERROR("Error on block free. %d\n", rc);
	}

	return rc;
}

/** check if a node is full */
static bool
evt_node_is_full(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	struct evt_node *nd = evt_tmmid2ptr(tcx, nd_mmid);

	D_ASSERT(nd->tn_nr <= tcx->tc_order);
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
struct evt_node_entry *
evt_node_entry_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 unsigned int at)
{
	struct evt_node		*nd = evt_tmmid2ptr(tcx, nd_mmid);

	return &nd->tn_rec[at];
}

/** Return the address of child mmid at the offset of @at */
static TMMID(struct evt_node) *
evt_node_child_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		  unsigned int at)
{
	struct evt_node_entry	*ne = evt_node_entry_at(tcx, nd_mmid, at);

	D_ASSERT(!evt_node_is_leaf(tcx, nd_mmid));
	return &ne->ne_node;
}

/** Return the data pointer at the offset of @at */
static struct evt_ptr *
evt_node_ptr_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 unsigned int at)
{
	struct evt_node_entry	*ne = evt_node_entry_at(tcx, nd_mmid, at);

	D_ASSERT(evt_node_is_leaf(tcx, nd_mmid));
	return evt_tmmid2ptr(tcx, ne->ne_ptr);
}

/** Return the rectangle at the offset of @at */
struct evt_rect *
evt_node_rect_at(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 unsigned int at)
{
	struct evt_node_entry	*ne = evt_node_entry_at(tcx, nd_mmid, at);

	return &ne->ne_rect;
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
	struct evt_node_entry	*etmp;
	struct evt_rect		*rtmp;
	bool			 changed;

	/* update the rectangle at the specified position */
	etmp = evt_node_entry_at(tcx, tn_mmid, at);
	etmp->ne_rect = *rect;

	/* make adjustments to the position of the rectangle */
	if (tcx->tc_ops->po_adjust)
		tcx->tc_ops->po_adjust(tcx, tn_mmid, etmp, at);

	/* merge the rectangle with the current node */
	rtmp = evt_node_mbr_get(tcx, tn_mmid);
	changed = evt_rect_merge(rtmp, rect);

	return changed;
}

/**
 * Return the size of evtree node, leaf node has different size with internal
 * node.
 */
static int
evt_node_size(struct evt_context *tcx)
{
	return sizeof(struct evt_node) +
	       sizeof(struct evt_node_entry) * tcx->tc_order;
}

/** Allocate a evtree node */
static int
evt_node_alloc(struct evt_context *tcx, unsigned int flags,
	       TMMID(struct evt_node) *nd_mmid_p)
{
	struct evt_node		*nd;
	TMMID(struct evt_node)	 nd_mmid;

	nd_mmid = umem_zalloc_typed(evt_umm(tcx), struct evt_node,
				    evt_node_size(tcx));
	if (TMMID_IS_NULL(nd_mmid))
		return -DER_NOMEM;

	D_DEBUG(DB_TRACE, "Allocate new node "TMMID_PF" %d bytes\n",
		TMMID_P(nd_mmid), evt_node_size(tcx));
	nd = evt_tmmid2ptr(tcx, nd_mmid);
	nd->tn_flags = flags;

	*nd_mmid_p = nd_mmid;
	return 0;
}

static inline int
evt_node_tx_add(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	if (!evt_has_tx(tcx))
		return 0;

	return umem_tx_add_typed(evt_umm(tcx), nd_mmid, evt_node_size(tcx));
}

static int
evt_node_free(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid)
{
	return umem_free_typed(evt_umm(tcx), nd_mmid);
}

/**
 * Destroy a tree node and all its desendants nodes, or leaf records and
 * data extents.
 */
static int
evt_node_destroy(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		 int level)
{
	struct evt_node_entry	*ne;
	struct evt_node		*nd;
	bool			 leaf;
	int			 i;
	int			 rc = 0;

	nd = evt_tmmid2ptr(tcx, nd_mmid);
	leaf = evt_node_is_leaf(tcx, nd_mmid);

	D_DEBUG(DB_TRACE, "Destroy %s node at level %d (nr = %d)\n",
		leaf ? "leaf" : "", level, nd->tn_nr);

	for (i = 0; i < nd->tn_nr; i++) {
		ne = evt_node_entry_at(tcx, nd_mmid, i);
		if (leaf) {
			/* NB: This will be replaced with a callback */
			rc = evt_ptr_free(tcx, evt_tmmid2ptr(tcx, ne->ne_ptr));
			if (rc != 0)
				return rc;
			rc = umem_free_typed(evt_umm(tcx), ne->ne_ptr);
			if (rc != 0)
				return rc;
		} else {
			rc = evt_node_destroy(tcx, ne->ne_node, level + 1);
			if (rc != 0)
				return rc;
		}
	}
	return evt_node_free(tcx, nd_mmid);
}

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
	D_ASSERT(node->tn_nr != 0);

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
		TMMID(struct evt_node) in_mmid, struct evt_entry *ent,
		bool *mbr_changed)
{
	struct evt_rect *mbr;
	struct evt_node *nd;
	int		 rc;
	bool		 changed = 0;

	nd  = evt_tmmid2ptr(tcx, nd_mmid);
	mbr = evt_node_mbr_get(tcx, nd_mmid);

	D_DEBUG(DB_TRACE, "Insert "DF_RECT" into "DF_RECT"("TMMID_PF")\n",
		DP_RECT(&ent->en_rect), DP_RECT(mbr), TMMID_P(nd_mmid));

	rc = tcx->tc_ops->po_insert(tcx, nd_mmid, in_mmid, ent);
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
	int		   range;
	int		   time;

	evt_rect_overlap(&nd->tn_mbr, rect, &range, &time);
	if ((time & (RT_OVERLAP_SAME | RT_OVERLAP_OVER)) &&
	    (range & RT_OVERLAP_INCLUDED)) {
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

	if (!evt_has_tx(tcx))
		return 0;

	if (!TMMID_IS_NULL(tcx->tc_root_mmid)) {
		rc = umem_tx_add_mmid_typed(umm, tcx->tc_root_mmid);
	} else {
		D_ASSERT(tcx->tc_root != NULL);
		rc = umem_tx_add_ptr(umm, tcx->tc_root, sizeof(*tcx->tc_root));
	}
	return rc;
}

/** Initialize the tree root */
static int
evt_root_init(struct evt_context *tcx)
{
	int	rc;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

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
	int	rc;
	if (!TMMID_IS_NULL(tcx->tc_root_mmid)) {
		rc = umem_free_typed(evt_umm(tcx), tcx->tc_root_mmid);
		tcx->tc_root_mmid = EVT_ROOT_NULL;
	} else {
		rc = evt_root_tx_add(tcx);
		if (rc != 0)
			goto out;
		memset(tcx->tc_root, 0, sizeof(*tcx->tc_root));
	}
out:
	tcx->tc_root = NULL;
	return rc;
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

	D_ASSERT(root->tr_depth == 0);
	D_ASSERT(TMMID_IS_NULL(root->tr_node));

	/* root node is also a leaf node */
	rc = evt_node_alloc(tcx, EVT_NODE_ROOT | EVT_NODE_LEAF, &nd_mmid);
	if (rc != 0)
		return rc;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

	root->tr_node = nd_mmid;
	root->tr_depth = 1;

	evt_tcx_set_dep(tcx, root->tr_depth);
	evt_tcx_set_trace(tcx, 0, nd_mmid, 0);
	return 0;
}

static int
evt_root_deactivate(struct evt_context *tcx)
{
	struct evt_root	*root = tcx->tc_root;
	int		 rc;

	D_ASSERT(root->tr_depth != 0);
	D_ASSERT(!TMMID_IS_NULL(root->tr_node));

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

	root->tr_depth = 0;
	rc = umem_free_typed(evt_umm(tcx), root->tr_node);
	if (rc != 0)
		return rc;

	root->tr_node = TMMID_NULL(struct evt_node);
	evt_tcx_set_dep(tcx, 0);
	return 0;
}

/** Destroy the root node and all its descendants. */
static int
evt_root_destroy(struct evt_context *tcx)
{
	int	rc;

	if (!TMMID_IS_NULL(tcx->tc_root->tr_node)) {
		/* destroy the root node and all descendants */
		rc = evt_node_destroy(tcx, tcx->tc_root->tr_node, 0);
		if (rc != 0)
			return rc;
	}

	return evt_root_free(tcx);
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
	TMMID(struct evt_node)	 nm_save  = TMMID_NULL(struct evt_node);
	struct evt_rect		*mbr	  = NULL;
	struct evt_entry	 entry	  = *ent_new;
	int			 rc	  = 0;
	int			 level	  = tcx->tc_depth - 1;
	bool			 mbr_changed = false;

	while (1) {
		struct evt_trace	*trace;
		TMMID(struct evt_node)	 nm_cur;
		TMMID(struct evt_node)	 nm_new;
		TMMID(struct evt_node)	 nm_ins;
		bool			 leaf;

		trace	= &tcx->tc_trace[level];
		nm_cur	= trace->tr_node;
		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, nm_cur);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		if (mbr) { /* This is set only if no more insert or split */
			D_ASSERT(mbr_changed);
			/* Update the child MBR stored in the current node
			 * because MBR of child has been enlarged.
			 */
			mbr_changed = evt_node_rect_update(tcx, nm_cur,
							   trace->tr_at, mbr);
			if (!mbr_changed || level == 0)
				D_GOTO(out, 0);

			/* continue to merge MBR with upper level node */
			mbr = evt_node_mbr_get(tcx, nm_cur);
			level--;
			continue;
		}

		if (!evt_node_is_full(tcx, nm_cur)) {
			bool	changed;

			rc = evt_node_insert(tcx, nm_cur, nm_save,
					     &entry, &changed);
			if (rc != 0)
				D_GOTO(failed, rc);

			/* NB: mbr_changed could have been set while splitting
			 * the child node.
			 */
			mbr_changed |= changed;
			if (!mbr_changed || level == 0)
				D_GOTO(out, 0);

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
			D_GOTO(failed, rc);

		rc = evt_node_split(tcx, leaf, nm_cur, nm_new);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "Failed to split node: %d\n", rc);
			D_GOTO(failed, rc);
		}

		/* choose a node for insert between the current node and the
		 * new created node.
		 */
		nm_ins = evt_select_node(tcx, &entry.en_rect, nm_cur, nm_new);
		rc = evt_node_insert(tcx, nm_ins, nm_save, &entry, NULL);
		if (rc != 0)
			D_GOTO(failed, rc);

		/* Insert the new node to upper level node:
		 * - If the current node is not root, insert it to its parent
		 * - If the current node is root, create a new root
		 */
		nm_save = nm_new;
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

		D_ASSERT(evt_node_is_root(tcx, nm_cur));
		evt_node_unset(tcx, nm_cur, EVT_NODE_ROOT);

		rc = evt_node_alloc(tcx, EVT_NODE_ROOT, &nm_new);
		if (rc != 0)
			D_GOTO(failed, rc);

		rc = evt_node_insert(tcx, nm_new, nm_save, &entry, NULL);
		if (rc != 0)
			D_GOTO(failed, rc);

		evt_tcx_set_dep(tcx, tcx->tc_depth + 1);
		tcx->tc_trace->tr_node = nm_new;
		tcx->tc_trace->tr_at = 0;

		rc = evt_root_tx_add(tcx);
		if (rc != 0)
			D_GOTO(failed, rc);
		tcx->tc_root->tr_node = nm_new;
		tcx->tc_root->tr_depth++;

		/* continue the loop and insert the original root node into
		 * the new root node.
		 */
		entry.en_rect = *evt_node_mbr_get(tcx, nm_cur);
		nm_save = nm_cur;
	}
 out:
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
	D_ASSERT(level == tcx->tc_depth - 1);

	return evt_insert_or_split(tcx, ent);
}

static void
evt_ptr_copy(struct evt_context *tcx, struct evt_ptr *src_ptr)
{
	struct evt_ptr		*dst_ptr;
	struct evt_trace	*trace;
	TMMID(struct evt_node)	 nd_mmid;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	nd_mmid = trace->tr_node;
	dst_ptr = evt_node_ptr_at(tcx, nd_mmid, trace->tr_at);

	D_DEBUG(DB_IO, "dst num="DF_U64", nob=%d, src num="DF_U64", nob=%d\n",
		dst_ptr->pt_inum, dst_ptr->pt_inob,
		src_ptr->pt_inum, src_ptr->pt_inob);

	/* Free the pmem that dst_ptr references */
	evt_ptr_free(tcx, dst_ptr);

	memcpy(dst_ptr, src_ptr, sizeof(*dst_ptr));
}

/**
 * Insert a versioned extent (rectangle) and its data mmid into the tree.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_insert(daos_handle_t toh, uuid_t cookie, uint32_t pm_ver,
	   struct evt_rect *rect, uint32_t inob, bio_addr_t addr)
{
	struct evt_context	*tcx;
	struct evt_entry_list	 ent_list;
	struct evt_entry	 ent;
	int			 rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	evt_ent_list_init(&ent_list);

	memset(&ent, 0, sizeof(ent));
	ent.en_rect = *rect;

	/* Phase-1: Check for overwrite */
	rc = evt_find_ent_list(tcx, EVT_FIND_OVERWRITE, &ent.en_rect,
			       &ent_list);
	if (rc != 0)
		return rc;

	evt_ptr_init(tcx, cookie, pm_ver, addr, inob,
		     evt_rect_width(&ent.en_rect), &ent.en_ptr);

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	if (tcx->tc_depth == 0) { /* empty tree */
		rc = evt_root_activate(tcx);
		if (rc != 0)
			goto out;
	}

	D_ASSERT(ent_list.el_ent_nr <= 1);
	if (ent_list.el_ent_nr == 1) {
		/*
		 * NB: This is part of the current hack to keep "supporting"
		 * overwrite for same epoch, full overwrite.
		 */
		evt_ptr_copy(tcx, &ent.en_ptr);
		goto out;
	}

	/* Phase-2: Inserting */
	rc = evt_insert_entry(tcx, &ent);

	/* No need for evt_find_ent_list_fini as there will be no allocations
	 * with 1 entry in the list
	 */
out:
	return evt_tx_end(tcx, rc);
}

/** Fill the entry with the extent at the specified position of \a nd_mmid */
void
evt_fill_entry(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
	       unsigned int at, struct evt_rect *rect_srch,
	       struct evt_entry *entry)
{
	struct evt_ptr	   *ptr;
	struct evt_rect	   *rect;
	daos_off_t	    offset;
	daos_size_t	    width;
	daos_size_t	    nr;

	rect = evt_node_rect_at(tcx, nd_mmid, at);
	ptr = evt_node_ptr_at(tcx, nd_mmid, at);

	offset = 0;
	width = evt_rect_width(rect);

	if (rect_srch && rect_srch->rc_off_lo > rect->rc_off_lo) {
		offset = rect_srch->rc_off_lo - rect->rc_off_lo;
		D_ASSERTF(width > offset, DF_U64"/"DF_U64"\n", width, offset);
		width -= offset;
	}

	if (rect_srch && rect_srch->rc_off_hi < rect->rc_off_hi) {
		nr = rect->rc_off_hi - rect_srch->rc_off_hi;
		D_ASSERTF(width > nr, DF_U64"/"DF_U64"\n", width, nr);
		width -= nr;
	}

	entry->en_rect = entry->en_sel_rect = *rect;
	entry->en_sel_rect.rc_off_lo += offset;
	entry->en_sel_rect.rc_off_hi = entry->en_sel_rect.rc_off_lo + width - 1;

	entry->en_ptr = *ptr;
	ptr = &entry->en_ptr; /* We have the data cached, so use it now */

	if (offset != 0 && !bio_addr_is_hole(&ptr->pt_ex_addr)) {
		D_ASSERT(ptr->pt_inob != 0); /* Ensure not punched */
		/* Adjust cached pointer since we're only referencing a
		 * part of the extent
		 */
		ptr->pt_ex_addr.ba_off += offset * ptr->pt_inob;
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

	D_DEBUG(DB_TRACE, "Searching rectangle "DF_RECT" opc=%d\n",
		DP_RECT(rect), find_opc);
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

		D_ASSERT(!leaf || at == 0);
		D_DEBUG(DB_TRACE,
			"Checking "DF_RECT"("TMMID_PF"), l=%d, a=%d, f=%d\n",
			DP_RECT(mbr), TMMID_P(nd_mmid), level, at, leaf);

		for (i = at; i < node->tn_nr; i++) {
			struct evt_entry	*ent;
			struct evt_rect		*rtmp;
			int			 time_overlap;
			int			 range_overlap;

			rtmp = evt_node_rect_at(tcx, nd_mmid, i);
			D_DEBUG(DB_TRACE, " rect[%d]="DF_RECT"\n",
				i, DP_RECT(rtmp));

			evt_rect_overlap(rtmp, rect, &range_overlap,
					 &time_overlap);
			switch (range_overlap) {
			default:
				D_ASSERT(0);
			case RT_OVERLAP_NO:
				continue; /* skip, no overlap */

			case RT_OVERLAP_SAME:
			case RT_OVERLAP_INCLUDED:
			case RT_OVERLAP_INCLUDES:
			case RT_OVERLAP_PARTIAL:
				break; /* overlapped */
			}

			switch (time_overlap) {
			default:
				D_ASSERT(0);
			case RT_OVERLAP_NO:
			case RT_OVERLAP_UNDER:
				continue; /* skip, no overlap */
			case RT_OVERLAP_OVER:
			case RT_OVERLAP_SAME:
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
				D_ASSERTF(0, "%d\n", find_opc);
			case EVT_FIND_OVERWRITE:
				if (time_overlap != RT_OVERLAP_SAME)
					continue; /* not same epoch, skip */
				/* NB: This is temporary to allow full overwrite
				 * in same epoch to avoid breaking rebuild.
				 * Without some sequence number and client
				 * identifier, we can't do this robustly.
				 * There can be a race between rebuild and
				 * client doing different updates.  But this
				 * isn't any worse than what we already have in
				 * place so I did it this way to minimize
				 * change while we decide how to handle this
				 * properly.
				 */
				if (range_overlap != RT_OVERLAP_SAME) {
					D_DEBUG(DB_IO, "Same epoch partial "
						"overwrite not supported:"
						DF_RECT" overlaps with "DF_RECT
						"\n", DP_RECT(rect),
						DP_RECT(rtmp));
					rc = -DER_NO_PERM;
					goto out;
				}
				break; /* we can update the record in place */
			case EVT_FIND_SAME:
				if (range_overlap != RT_OVERLAP_SAME)
					continue;
				if (time_overlap != RT_OVERLAP_SAME)
					continue;
				break;
			case EVT_FIND_FIRST:
			case EVT_FIND_ALL:
				break;
			}

			ent = evt_ent_list_alloc(tcx, ent_list);
			if (ent == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			evt_fill_entry(tcx, nd_mmid, i, rect, ent);
			switch (find_opc) {
			default:
				D_ASSERTF(0, "%d\n", find_opc);
			case EVT_FIND_OVERWRITE:
			case EVT_FIND_FIRST:
			case EVT_FIND_SAME:
				/* store the trace and return for clip or
				 * iteration.
				 * NB: clip is not implemented yet.
				 */
				evt_tcx_set_trace(tcx, level, nd_mmid, i);
				D_GOTO(out, rc = 0);

			case EVT_FIND_ALL:
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
			D_ASSERT(at <= tcx->tc_order);
		}
	}
out:
	if (rc != 0)
		evt_ent_list_fini(ent_list);

	return rc;
}

struct evt_max_rect {
	struct evt_rect		mr_rect;
	bool			mr_valid;
	bool			mr_punched;
};

static bool
saved_rect_is_greater(struct evt_max_rect *saved, struct evt_rect *r2)
{
	struct evt_rect	*r1;
	bool		 is_greater = false;

	if (!saved->mr_valid) /* No rectangle saved yet */
		return false;

	r1 = &saved->mr_rect;

	D_DEBUG(DB_TRACE, "Comparing saved "DF_RECT" to "DF_RECT"\n",
		DP_RECT(r1), DP_RECT(r2));
	if (r1->rc_off_hi > r2->rc_off_hi) {
		is_greater = true;
		goto out;
	}

	if (r1->rc_off_hi < r2->rc_off_hi)
		goto out;

	if (r1->rc_epc > r2->rc_epc)
		is_greater = true;

out:
	/* Now we need to update the lower bound of whichever rectangle is
	 * selected if the chosen rectangle is partially covered
	 */
	if (is_greater) {
		if (r2->rc_epc > r1->rc_epc) {
			if (r2->rc_off_hi >= r1->rc_off_lo)
				r1->rc_off_lo = r2->rc_off_hi + 1;
		}
	} else {
		if (r1->rc_epc > r2->rc_epc) {
			if (r1->rc_off_hi >= r2->rc_off_lo)
				r2->rc_off_lo = r1->rc_off_hi + 1;
		}
	}
	return is_greater;
}


int
evt_get_size(daos_handle_t toh, daos_epoch_t epoch, daos_size_t *size)
{
	struct evt_context	 *tcx;
	struct evt_rect		  rect; /* specifies range we are searching */
	struct evt_max_rect	  saved_rect = {0};
	struct evt_entry	  ent;
	TMMID(struct evt_node)	  nd_mmid;
	int			  level;
	int			  at;
	int			  i;

	if (size == NULL)
		return -DER_INVAL;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	*size = 0;

	D_DEBUG(DB_TRACE, "Finding evt range at epoch "DF_U64"\n", epoch);
	/* Start with the whole range.  We'll repeat the algorithm until we
	 * either we find nothing or we find a non-punched rectangle.
	 */
	rect.rc_off_lo = 0;
	rect.rc_off_hi = (daos_off_t)-1;
	rect.rc_epc = epoch;

	if (tcx->tc_root->tr_depth == 0)
		return 0; /* empty tree */

try_again:
	D_DEBUG(DB_TRACE, "Scanning for maximum in "DF_RECT"\n",
		DP_RECT(&rect));

	evt_tcx_reset_trace(tcx);

	saved_rect.mr_valid = false; /* Reset the saved entry */

	level = at = 0;
	nd_mmid = tcx->tc_root->tr_node;
	while (1) {
		struct evt_rect *mbr;
		struct evt_node	*node;
		bool		 leaf;

		node = evt_tmmid2ptr(tcx, nd_mmid);
		leaf = evt_node_is_leaf(tcx, nd_mmid);
		mbr  = evt_node_mbr_get(tcx, nd_mmid);

		D_DEBUG(DB_TRACE, "Checking mbr="DF_RECT", l=%d, a=%d\n",
			DP_RECT(mbr), level, at);

		D_ASSERT(!leaf || at == 0);

		for (i = at; i < node->tn_nr; i++) {
			struct evt_rect		*rtmp;
			int			 time_overlap;
			int			 range_overlap;

			rtmp = evt_node_rect_at(tcx, nd_mmid, i);
			D_DEBUG(DB_TRACE, "Checking rect[%d]="DF_RECT"\n",
				i, DP_RECT(rtmp));

			evt_rect_overlap(rtmp, &rect, &range_overlap,
					 &time_overlap);
			if (range_overlap == RT_OVERLAP_NO)
				continue;
			D_ASSERT(time_overlap != RT_OVERLAP_NO);

			if (time_overlap == RT_OVERLAP_UNDER)
				continue;

			if (!leaf) {
				/* break the internal loop and enter the
				 * child node.
				 */
				break;
			}

			memset(&ent, 0, sizeof(ent));
			evt_fill_entry(tcx, nd_mmid, i, &rect, &ent);

			/* Ok, now that we've potentially trimmed the rectangle
			 * in ent, let's do the check again
			 */
			if (saved_rect_is_greater(&saved_rect,
						  &ent.en_sel_rect))
				continue;

			saved_rect.mr_valid = true;
			if (bio_addr_is_hole(&ent.en_ptr.pt_ex_addr))
				saved_rect.mr_punched = true;
			else
				saved_rect.mr_punched = false;
			saved_rect.mr_rect = ent.en_sel_rect;

			D_DEBUG(DB_TRACE, "New saved rectangle "DF_RECT
				" punched? : %s\n",
				DP_RECT(&saved_rect.mr_rect),
				saved_rect.mr_punched ? "yes" : "no");

			evt_tcx_set_trace(tcx, level, nd_mmid, i);
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
				daos_off_t	old;

				if (!saved_rect.mr_valid)
					return 0;

				old = saved_rect.mr_rect.rc_off_lo;

				if (saved_rect.mr_punched) {
					D_DEBUG(DB_TRACE,
						"Final extent in range is"
						" punched ("DF_RECT")\n",
						DP_RECT(&saved_rect.mr_rect));
					if (old == 0)
						return 0;
					rect.rc_off_hi = old - 1;

					goto try_again;
				}
				*size = saved_rect.mr_rect.rc_off_hi + 1;
				break;
			}

			level--;
			trace = evt_tcx_trace(tcx, level);
			nd_mmid = trace->tr_node;
			at = trace->tr_at + 1;
			D_ASSERT(at <= tcx->tc_order);
		}
	}
	/* Only way to break the outer loop is if we found a valid record */
	D_ASSERT(saved_rect.mr_valid);
	return 0;
}

/**
 * Find all versioned extents intercepting with the input rectangle \a rect
 * and return their data pointers.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_find(daos_handle_t toh, struct evt_rect *rect,
	 struct evt_entry_list *ent_list, d_list_t *covered)
{
	struct evt_context *tcx;
	int		    rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	evt_ent_list_init(ent_list);
	rc = evt_find_ent_list(tcx, EVT_FIND_ALL, rect, ent_list);
	if (rc == 0 && covered != NULL)
		rc = evt_ent_list_sort(tcx, ent_list, covered);
	if (rc != 0)
		evt_ent_list_fini(ent_list);
	return rc;
}

/** move the probing trace forward or backward */
bool
evt_move_trace(struct evt_context *tcx, bool forward, daos_epoch_range_t *epr)
{
	struct evt_trace	*trace;
	struct evt_node		*nd;
	TMMID(struct evt_node)	 nd_mmid;

	if (evt_root_empty(tcx))
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
next:
	while (1) {
		nd_mmid = trace->tr_node;
		nd = evt_tmmid2ptr(tcx, nd_mmid);

		/* already reach at the begin or end of this node */
		if ((trace->tr_at == (nd->tn_nr - 1) && forward) ||
		    (trace->tr_at == 0 && !forward)) {
			if (evt_node_is_root(tcx, nd_mmid)) {
				D_ASSERT(trace == tcx->tc_trace);
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
		D_ASSERTF(nd->tn_nr != 0, "%d\n", nd->tn_nr);

		trace++;
		trace->tr_at = forward ? 0 : nd->tn_nr - 1;
		trace->tr_node = tmp;
	}

	if (epr != NULL) {
		struct evt_rect *rect;

		rect = evt_node_rect_at(tcx, trace->tr_node, trace->tr_at);
		if (rect->rc_epc < epr->epr_lo ||
		    rect->rc_epc > epr->epr_hi)
			goto next;
	}

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

	rc = evt_tcx_create(root_mmid, NULL, -1, -1, uma, NULL, &tcx);
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
		 void *info, daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (root->tr_order == 0) {
		D_DEBUG(DB_TRACE, "Tree order is zero\n");
		return -DER_INVAL;
	}

	rc = evt_tcx_create(EVT_ROOT_NULL, root, -1, -1, uma, info, &tcx);
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

	rc = evt_tcx_create(EVT_ROOT_NULL, NULL, feats, order, uma, NULL, &tcx);
	if (rc != 0)
		return rc;

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		goto err;

	rc = evt_root_alloc(tcx);
	if (rc != 0)
		D_GOTO(out, rc);

	*root_mmid_p = tcx->tc_root_mmid;
	*toh = evt_tcx2hdl(tcx); /* take refcount for open */
out:
	rc = evt_tx_end(tcx, rc);

err:
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

	rc = evt_tcx_create(EVT_ROOT_NULL, root, feats, order, uma, NULL, &tcx);
	if (rc != 0)
		return rc;

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		goto err;

	rc = evt_root_init(tcx);
	if (rc != 0)
		D_GOTO(out, rc);

	*toh = evt_tcx2hdl(tcx); /* take refcount for open */
out:
	rc = evt_tx_end(tcx, rc);
err:
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

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = evt_root_destroy(tcx);

	rc = evt_tx_end(tcx, rc);

	/* Close the tcx even if the destroy failed */
	evt_tcx_decref(tcx);

	return rc;
}

/* Special value to not only print MBRs but also bounds for leaf records */
#define EVT_DEBUG_LEAF (-2)
/* Number of spaces to add at each level in debug output */
#define EVT_DEBUG_INDENT (4)

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
		D_PRINT("%*snode="TMMID_PF", lvl=%d, mbr="DF_RECT
			", rect_nr=%d\n", cur_level * EVT_DEBUG_INDENT, "",
			TMMID_P(nd_mmid), cur_level, DP_RECT(rect), nd->tn_nr);

		if (leaf && debug_level == EVT_DEBUG_LEAF) {
			for (i = 0; i < nd->tn_nr; i++) {
				rect = evt_node_rect_at(tcx, nd_mmid, i);

				D_PRINT("%*s    rect[%d] = "DF_RECT"\n",
					cur_level * EVT_DEBUG_INDENT, "", i,
					DP_RECT(rect));
			}
		}

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

	D_PRINT("Tree depth=%d, order=%d, feats="DF_X64"\n",
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
 * Sorted by Start Offset (SSOF)
 *
 * Extents are sorted by start offset first, then high to low epoch, then end
 * offset
 */

/** Rectangle comparison for sorting */
static int
evt_ssof_cmp_rect(struct evt_context *tcx, const struct evt_rect *rt1,
		  const struct evt_rect *rt2)
{
	return evt_cmp_rect_helper(rt1, rt2);
}

static int
evt_ssof_insert(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		TMMID(struct evt_node) in_mmid, struct evt_entry *ent)
{
	struct evt_node		*nd   = evt_tmmid2ptr(tcx, nd_mmid);
	struct evt_node_entry	*ne = NULL;
	int			 i;
	int			 rc;
	bool			 leaf;

	D_ASSERT(!evt_node_is_full(tcx, nd_mmid));

	leaf = evt_node_is_leaf(tcx, nd_mmid);

	/* NB: can use binary search to optimize */
	for (i = 0; i < nd->tn_nr; i++) {
		int	nr;

		ne = evt_node_entry_at(tcx, nd_mmid, i);
		rc = evt_ssof_cmp_rect(tcx, &ne->ne_rect, &ent->en_rect);
		if (rc < 0)
			continue;

		nr = nd->tn_nr - i;
		memmove(ne + 1, ne, nr * sizeof(*ne));
		break;
	}

	if (i == nd->tn_nr) { /* attach at the end */
		ne = evt_node_entry_at(tcx, nd_mmid, nd->tn_nr);
	}

	ne->ne_rect = ent->en_rect;
	if (leaf) {
		struct evt_ptr	*ptr;

		ne->ne_ptr = umem_zalloc_typed(evt_umm(tcx), struct evt_ptr,
					       sizeof(struct evt_ptr));
		if (TMMID_IS_NULL(ne->ne_ptr))
			return -DER_NOMEM;
		ptr = evt_tmmid2ptr(tcx, ne->ne_ptr);
		*ptr = ent->en_ptr;
	} else {
		ne->ne_node = in_mmid;
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
	struct evt_node_entry	*entry_src;
	struct evt_node_entry	*entry_dst;
	int		    nr;

	D_ASSERT(nd_src->tn_nr == tcx->tc_order);
	nr = nd_src->tn_nr / 2;
	/* give one more entry to the left (original) node if tree order is
	 * odd, because "append" could be the most common use-case at here,
	 * which means new entres will never be inserted into the original
	 * node. So we want to utilize the original as much as possible.
	 */
	nr += (nd_src->tn_nr % 2 != 0);

	entry_src = evt_node_entry_at(tcx, src_mmid, nr);
	entry_dst = evt_node_entry_at(tcx, dst_mmid, 0);
	memcpy(entry_dst, entry_src, sizeof(*entry_dst) * (nd_src->tn_nr - nr));

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
	weight->wt_minor = -rect->rc_epc;
	return 0;
}

static void
evt_ssof_adjust(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		struct evt_node_entry *ne, int at)
{
	struct evt_node_entry	*etmp;
	struct evt_node		*nd = evt_tmmid2ptr(tcx, nd_mmid);
	struct evt_node_entry	*dst_entry;
	struct evt_node_entry	*src_entry;
	struct evt_node_entry	 cached_entry;
	int			 count;
	int			 i;

	D_ASSERT(!evt_node_is_leaf(tcx, nd_mmid));

	/* Check if we need to move the entry left */
	for (i = at - 1, etmp = ne - 1; i >= 0; i--, etmp--) {
		if (evt_ssof_cmp_rect(tcx, &etmp->ne_rect, &ne->ne_rect) <= 0)
			break;
	}

	i++;
	if (i != at) {
		/* The entry needs to move left */
		etmp++;
		dst_entry = etmp + 1;
		src_entry = etmp;
		cached_entry = *ne;

		count = at - i;
		goto move;
	}

	/* Ok, now check if we need to move the entry right */
	for (i = at + 1, etmp = ne + 1; i < nd->tn_nr; i++, etmp++) {
		if (evt_ssof_cmp_rect(tcx, &etmp->ne_rect, &ne->ne_rect) >= 0)
			break;
	}

	i--;
	if (i != at) {
		/* the entry needs to move right */
		etmp--;
		count = i - at;
		dst_entry = ne;
		src_entry = dst_entry + 1;
		cached_entry = *ne;
		goto move;
	}

	return;
move:
	/* Execute the move */
	memmove(dst_entry, src_entry, sizeof(*dst_entry) * count);
	*etmp = cached_entry;
}

static struct evt_policy_ops evt_ssof_pol_ops = {
	.po_insert		= evt_ssof_insert,
	.po_adjust		= evt_ssof_adjust,
	.po_split		= evt_ssof_split,
	.po_rect_weight		= evt_ssof_rect_weight,
};

/* Delete the node pointed to by current trace */
static int
evt_node_delete(struct evt_context *tcx)
{
	TMMID(struct evt_node)	 nm_cur;
	struct evt_trace	*trace;
	struct evt_node		*node;
	struct evt_node_entry	*ne;
	bool			 leaf;
	int			 level	= tcx->tc_depth - 1;
	int			 rc;

	/* We take a simple approach here which may be refined later.
	 * We simply remove the record, and if it's the last record, we
	 * bubble up removing any nodes that only have one record.
	 * Then we check the mbr at each level and make appropriate
	 * adjustments.
	 */
	while (1) {
		int			 count;

		trace = &tcx->tc_trace[level];
		nm_cur = trace->tr_node;
		leaf = evt_node_is_leaf(tcx, nm_cur);
		node = evt_tmmid2ptr(tcx, nm_cur);

		ne = evt_node_entry_at(tcx, nm_cur, trace->tr_at);
		if (leaf) {
			/* Free the evt_ptr */
			rc = umem_free_typed(evt_umm(tcx), ne->ne_ptr);
			if (rc != 0)
				return rc;
			ne->ne_ptr = TMMID_NULL(struct evt_ptr);
		}

		if (node->tn_nr == 1) {
			/* this node can be removed so bubble up */
			if (level == 0) {
				evt_root_deactivate(tcx);
				return 0;
			}

			rc = umem_free_typed(evt_umm(tcx), nm_cur);
			if (rc != 0)
				return rc;
			level--;
			continue;
		}

		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, nm_cur);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		/* Ok, remove the rect at the current trace */
		count = node->tn_nr - trace->tr_at - 1;
		node->tn_nr--;

		if (count == 0)
			break;

		memmove(ne, ne + 1, sizeof(*ne) * count);

		break;
	};

	/* Update MBR and bubble up */
	while (1) {
		struct evt_rect	mbr;
		int		i;

		ne -= trace->tr_at;
		mbr = ne->ne_rect;
		ne++;
		for (i = 1; i < node->tn_nr; i++, ne++)
			evt_rect_merge(&mbr, &ne->ne_rect);

		if (evt_rect_same_extent(&node->tn_mbr, &mbr) &&
		    node->tn_mbr.rc_epc == mbr.rc_epc)
			return 0; /* mbr hasn't changed */

		node->tn_mbr = mbr;

		if (level == 0)
			return 0;

		level--;

		trace = &tcx->tc_trace[level];
		nm_cur = trace->tr_node;
		node = evt_tmmid2ptr(tcx, nm_cur);

		ne = evt_node_entry_at(tcx, nm_cur, trace->tr_at);
		ne->ne_rect = mbr;

		/* make adjustments to the position of the rectangle */
		if (!tcx->tc_ops->po_adjust)
			continue;

		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, nm_cur);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		tcx->tc_ops->po_adjust(tcx, nm_cur, ne, trace->tr_at);
	}

	return 0;
}

int evt_delete(daos_handle_t toh, struct evt_rect *rect, struct evt_entry *ent)
{
	struct evt_context	*tcx;
	struct evt_entry_list	 ent_list;
	int			 rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	/* NB: This function presently only supports exact match on extent. */
	evt_ent_list_init(&ent_list);

	rc = evt_find_ent_list(tcx, EVT_FIND_SAME, rect, &ent_list);
	if (rc != 0)
		return rc;

	if (ent_list.el_ent_nr == 0)
		return -DER_ENOENT;

	D_ASSERT(ent_list.el_ent_nr == 1);
	if (ent != NULL)
		*ent = ent_list.el_ents[0];

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = evt_node_delete(tcx);

	/* No need for evt_find_ent_list_fini as there will be no allocations
	 * with 1 entry in the list
	 */
	return evt_tx_end(tcx, rc);
}
