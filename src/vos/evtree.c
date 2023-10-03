/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/checksum.h>
#include "evt_priv.h"

#ifdef VOS_DISABLE_TRACE
#define V_TRACE(...) (void)0
#else
#define V_TRACE(...) D_DEBUG(__VA_ARGS__)
#endif

/** Get the length of the extent from durable format */
static inline uint64_t
evt_len_read(const struct evt_rect_df *rin)
{
	return ((uint64_t)rin->rd_len_hi << 16) + rin->rd_len_lo;
}

/** Store length into durable format */
static inline void
evt_len_write(struct evt_rect_df *rout, uint64_t len)
{
	rout->rd_len_hi = len >> 16;
	rout->rd_len_lo = len & 0xffff;
}

/** Convert rect_df to an extent */
static inline void
evt_ext_read(struct evt_extent *ext, const struct evt_rect_df *rin)
{
	ext->ex_lo = rin->rd_lo;
	ext->ex_hi = rin->rd_lo + evt_len_read(rin)  - 1;
}

/** Read and translate the rectangle in durable format to in-memory format */
static inline void
evt_rect_read(struct evt_rect *rout, const struct evt_rect_df *rin)
{
	rout->rc_epc = rin->rd_epc;
	rout->rc_minor_epc = rin->rd_minor_epc;
	evt_ext_read(&rout->rc_ex, rin);
};

/** Translate and write a rectangle from in memory format to durable format */
static inline void
evt_rect_write(struct evt_rect_df *rout, const struct evt_rect *rin)
{
	evt_len_write(rout, evt_rect_width(rin));
	rout->rd_epc = rin->rc_epc;
	rout->rd_minor_epc = rin->rc_minor_epc;
	rout->rd_lo = rin->rc_ex.ex_lo;
};

#define DF_BUF_LEN	128
static __thread char	df_rect_buf[DF_BUF_LEN];
#define DF_RECT_DF "%s"
char *
DP_RECT_DF(struct evt_rect_df *rect)
{
	struct evt_rect	rtmp;
	int		len;

	evt_rect_read(&rtmp, rect);
	len = snprintf(df_rect_buf, DF_BUF_LEN, DF_RECT, DP_RECT(&rtmp));
	D_ASSERT(len < DF_BUF_LEN);

	return df_rect_buf;
}

#define DF_MBR DF_EXT"@"DF_X64".%d"
#define DP_MBR(node) \
	DP_EXT(&(node)->tn_mbr_ex), (node)->tn_mbr_epc, (node)->tn_mbr_minor_epc

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

static struct evt_policy_ops  evt_soff_pol_ops;
static struct evt_policy_ops evt_sdist_pol_ops;
static struct evt_policy_ops evt_sdist_even_pol_ops;
/**
 * Tree policy table.
 * - Sorted by Start Offset(SSOF): it is the only policy for now.
 */
static struct evt_policy_ops *evt_policies[] = {
    &evt_soff_pol_ops,
    &evt_sdist_pol_ops,
    &evt_sdist_even_pol_ops,
    NULL,
};

static void
evt_mbr_read(struct evt_rect *rout, const struct evt_node *node)
{
	rout->rc_ex = node->tn_mbr_ex;
	rout->rc_epc = node->tn_mbr_epc;
	rout->rc_minor_epc = node->tn_mbr_minor_epc;
}

static void
evt_mbr_write(struct evt_node *node, const struct evt_rect *rin)
{
	node->tn_mbr_ex = rin->rc_ex;
	node->tn_mbr_epc = rin->rc_epc;
	node->tn_mbr_minor_epc = rin->rc_minor_epc;
}

/**
 * Returns true if the first rectangle \a rt1 is at least as wide as the second
 * rectangle \a rt2.
 */
static bool
evt_rect_is_wider(const struct evt_rect *rt1, const struct evt_rect *rt2)
{
	return (rt1->rc_ex.ex_lo <= rt2->rc_ex.ex_lo &&
		rt1->rc_ex.ex_hi >= rt2->rc_ex.ex_hi);
}

static bool
evt_same_extent(const struct evt_extent *ex1, const struct evt_extent *ex2)
{
	return (ex1->ex_lo == ex2->ex_lo &&
		ex1->ex_hi == ex2->ex_hi);
}

static bool
evt_mbr_same(const struct evt_node *node, const struct evt_rect *rect)
{
	if (evt_same_extent(&node->tn_mbr_ex, &rect->rc_ex) &&
	    node->tn_mbr_epc == rect->rc_epc &&
	    node->tn_mbr_minor_epc == rect->rc_minor_epc)
		return true;
	return false;
}

static bool
time_cmp(uint64_t t1, uint64_t t2, int *out)
{
	if (t1 == t2) {
		*out = RT_OVERLAP_SAME;
		return true;
	}

	if (t1 < t2)
		*out = RT_OVERLAP_OVER;
	else
		*out = RT_OVERLAP_UNDER;

	return false;
}

/**
 * Check if two rectangles overlap with each other.
 *
 * NB: This function is not symmetric(caller cannot arbitrarily change order
 * of input rectangles), the first rectangle \a rt1 should be in-tree, the
 * second rectangle \a rt2 should be the one being searched/inserted.
 */
static void
evt_rect_overlap(const struct evt_rect *rt1, const struct evt_rect *rt2,
		 int *range, int *time)
{
	*time = *range = RT_OVERLAP_NO;

	if (rt1->rc_ex.ex_lo > rt2->rc_ex.ex_hi || /* no offset overlap */
	    rt1->rc_ex.ex_hi < rt2->rc_ex.ex_lo)
		return;

	/* NB: By definition, there is always epoch overlap since all
	 * updates are from epc to INF.  Determine here what kind
	 * of overlap exists.
	 */
	if (time_cmp(rt1->rc_epc, rt2->rc_epc, time))
		time_cmp(rt1->rc_minor_epc, rt2->rc_minor_epc, time);

	if (evt_same_extent(&rt1->rc_ex, &rt2->rc_ex))
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
evt_rect_merge(struct evt_rect *rt1, const struct evt_rect *rt2)
{
	bool	changed = false;

	if (rt1->rc_ex.ex_lo > rt2->rc_ex.ex_lo) {
		rt1->rc_ex.ex_lo = rt2->rc_ex.ex_lo;
		changed = true;
	}

	if (rt1->rc_ex.ex_hi < rt2->rc_ex.ex_hi) {
		rt1->rc_ex.ex_hi = rt2->rc_ex.ex_hi;
		changed = true;
	}

	if (rt1->rc_epc > rt2->rc_epc) {
		rt1->rc_epc = rt2->rc_epc;
		rt1->rc_minor_epc = rt2->rc_minor_epc;
		changed = true;
	} else if (rt1->rc_epc == rt2->rc_epc &&
		   rt1->rc_minor_epc > rt2->rc_minor_epc) {
		rt1->rc_minor_epc = rt2->rc_minor_epc;
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
evt_ent_array_init_(struct evt_entry_array *ent_array, int embedded, int max)
{
	memset(ent_array, 0, sizeof(*ent_array));
	ent_array->ea_ents = &ent_array->ea_embedded_ents[0];
	ent_array->ea_size = embedded;
	ent_array->ea_max = max;
}

/** Finalize an entry list */
void
evt_ent_array_fini_(struct evt_entry_array *ent_array, int embedded)
{
	if (ent_array->ea_size > embedded)
		D_FREE(ent_array->ea_ents);

	ent_array->ea_size = ent_array->ea_ent_nr = 0;
}

/** When we go over the embedded limit, set a minimum allocation */
#define EVT_MIN_ALLOC 4096

static bool
ent_array_resize(struct evt_context *tcx, struct evt_entry_array *ent_array,
		 uint32_t new_size)
{
	struct evt_list_entry	*ents;

	D_ALLOC_ARRAY(ents, new_size);
	if (ents == NULL)
		return -DER_NOMEM;

	memcpy(ents, ent_array->ea_ents,
	       sizeof(ents[0]) * ent_array->ea_ent_nr);
	if (ent_array->ea_ents != ent_array->ea_embedded_ents)
		D_FREE(ent_array->ea_ents);
	ent_array->ea_ents = ents;
	ent_array->ea_size = new_size;

	return 0;
}
static inline struct evt_list_entry *
evt_array_entry2le(struct evt_entry *ent)
{
	return container_of(ent, struct evt_list_entry, le_ent);
}

/**
 * Take an embedded entry, or allocate a new entry if all embedded entries
 * have been taken. If notify_realloc is set, it will return -DER_AGAIN
 * if reallocation was done so callers can revalidate cached references
 * to entries, if necessary.
 */
static int
ent_array_alloc(struct evt_context *tcx, struct evt_entry_array *ent_array,
		struct evt_entry **entry, bool notify_realloc)
{
	struct evt_list_entry	*le;
	uint32_t		 size;
	int			 i;
	int			 rc;

	if (ent_array->ea_max == 0) {
		/* Calculate the upper size bound for the array.  Based on the
		 * size of the evtree, this is an upper limit on the number of
		 * entries needed to fit all covered and visible extents
		 * including split extents.
		 */
		size = 1;
		for (i = 0; i < tcx->tc_depth; i++)
			size *= tcx->tc_order;
		/* With splitting, we need 3x the space in worst case.  Each new
		 * extent inserted can add at most 1 extent to the output. The
		 * cases are:
		 * 1. New extent covers existing one:   1 visible, 1 covered
		 * 2. New extent splits existing one:   3 visible, 1 covered
		 * 3. New extent overlaps existing one: 2 visible, 1 covered
		 * 4. New extent covers nothing:        1 visible, 0 covered
		 *
		 * So, each new extent can add at most 2 new rectangle (as in
		 * case #2.   So if we allocate 3x the max entries in the tree,
		 * we will always have sufficient space to store entries.
		 */
		size *= 3;
		ent_array->ea_max = size;
	}
	if (ent_array->ea_ent_nr == ent_array->ea_size) {
		/** We should never exceed the maximum number of entries. */
		D_ASSERTF(ent_array->ea_size != ent_array->ea_max,
			  "Maximum number of ent_array entries exceeded: %d\n",
			  ent_array->ea_max);
		size = ent_array->ea_size * 4;
		if (size < EVT_MIN_ALLOC)
			size = EVT_MIN_ALLOC;
		if (size > ent_array->ea_max)
			size = ent_array->ea_max;

		rc = ent_array_resize(tcx, ent_array, size);
		if (rc != 0)
			return rc;

		D_ASSERTF(ent_array->ea_ent_nr < ent_array->ea_size,
			  "%u >= %u, depth:%u, order:%u\n",
			  ent_array->ea_ent_nr, ent_array->ea_size,
			  tcx->tc_depth, tcx->tc_order);

		if (notify_realloc)
			return -DER_AGAIN; /* Invalidate any cached state */
	}
	D_ASSERT(ent_array->ea_ent_nr < ent_array->ea_size);

	*entry = evt_ent_array_get(ent_array, ent_array->ea_ent_nr++);
	le = evt_array_entry2le(*entry);
	memset(le, 0, sizeof(*le));

	return 0;
}

int
evt_rect_cmp(const struct evt_rect *rt1, const struct evt_rect *rt2)
{
	if (rt1->rc_ex.ex_lo < rt2->rc_ex.ex_lo)
		return -1;

	if (rt1->rc_ex.ex_lo > rt2->rc_ex.ex_lo)
		return 1;

	if (rt1->rc_epc > rt2->rc_epc)
		return -1;

	if (rt1->rc_epc < rt2->rc_epc)
		return 1;

	if (rt1->rc_minor_epc > rt2->rc_minor_epc)
		return -1;

	if (rt1->rc_minor_epc < rt2->rc_minor_epc)
		return 1;

	if (rt1->rc_ex.ex_hi < rt2->rc_ex.ex_hi)
		return -1;

	if (rt1->rc_ex.ex_hi > rt2->rc_ex.ex_hi)
		return 1;

	return 0;
}

#define PRINT_ENT(ent)							\
	D_PRINT("%s:%d " #ent ": " DF_ENT " visibility = %c\n",		\
		__func__, __LINE__, DP_ENT(ent),			\
		evt_vis2dbg((ent)->en_visibility))

#define evt_flags_get(flags) \
	((flags) & EVT_VIS_MASK)

#define evt_flags_equal(flags, set) \
	(evt_flags_get(flags) == (set))

#define evt_flags_valid(flags)			\
	(evt_flags_get(flags) == EVT_VISIBLE ||	\
	 evt_flags_get(flags) == EVT_REMOVE ||	\
	 evt_flags_get(flags) == EVT_COVERED)

/* Mask for prioritizing visible entries */
static const int vis_cmp_mask[] = {
	0,
	0, /* EVT_VISIBLE */
	1, /* EVT_COVERED */
	0,
	1, /* EVT_REMOVE */
};

static inline int
evt_ent_cmp(const struct evt_entry *ent1, const struct evt_entry *ent2,
	    const int mask[])
{
	struct evt_rect		 rt1;
	struct evt_rect		 rt2;

	if (mask == NULL)
		goto cmp_ext;

	/* Ensure we've selected one or the other */
	D_ASSERT(evt_flags_valid(ent1->en_visibility));
	D_ASSERT(evt_flags_valid(ent2->en_visibility));

	if (mask[evt_flags_get(ent1->en_visibility)] ==
	    mask[evt_flags_get(ent2->en_visibility)])
		goto cmp_ext;

	if (mask[evt_flags_get(ent1->en_visibility)] <
	    mask[evt_flags_get(ent2->en_visibility)])
		return -1;

	return 1;
cmp_ext:
	evt_ent2rect(&rt1, ent1);
	evt_ent2rect(&rt2, ent2);

	return evt_rect_cmp(&rt1, &rt2);
}

static int
evt_ent_list_cmp(const void *p1, const void *p2)
{
	const struct evt_list_entry	*le1	= p1;
	const struct evt_list_entry	*le2	= p2;

	return evt_ent_cmp(&le1->le_ent, &le2->le_ent, NULL);
}

static int
evt_ent_list_cmp_visible(const void *p1, const void *p2)
{
	const struct evt_list_entry	*le1	= p1;
	const struct evt_list_entry	*le2	= p2;

	return evt_ent_cmp(&le1->le_ent, &le2->le_ent, vis_cmp_mask);
}

static inline struct evt_list_entry *
evt_array_link2le(d_list_t *link)
{
	return d_list_entry(link, struct evt_list_entry, le_link);
}

static inline struct evt_entry *
evt_array_link2entry(d_list_t *link)
{
	struct evt_list_entry *le = evt_array_link2le(link);

	return &le->le_ent;
}

static inline d_list_t *
evt_array_entry2link(struct evt_entry *ent)
{
	struct evt_list_entry *le = evt_array_entry2le(ent);

	return &le->le_link;
}

/** Return true if ent1 is a later update (comparing epoch and minor epoch */
static inline bool
evt_ent_is_later(struct evt_entry *ent1, struct evt_entry *ent2)
{
	if (ent1->en_epoch > ent2->en_epoch)
		return true;

	if (ent1->en_epoch < ent2->en_epoch)
		return false;

	if (ent1->en_minor_epc > ent2->en_minor_epc)
		return true;

	return false;
}

static inline void
set_visibility(struct evt_entry *ent, uint32_t flags)
{
	ent->en_visibility &= ~EVT_VIS_MASK;
	ent->en_visibility |= flags;
}

static struct evt_entry *
evt_find_next_visible(struct evt_entry *this_ent, d_list_t *head,
		      d_list_t **next)
{
	d_list_t		*temp;
	struct evt_extent	*this_ext;
	struct evt_extent	*next_ext;
	struct evt_entry	*next_ent;

	while (*next != head) {
		next_ent = evt_array_link2entry(*next);

		if (evt_ent_is_later(next_ent, this_ent))
			return next_ent; /* next_ent is a later update */
		this_ext = &this_ent->en_sel_ext;
		next_ext = &next_ent->en_sel_ext;
		if (next_ext->ex_hi > this_ext->ex_hi)
			return next_ent; /* next_ent extends past end */

		/* next_ent is covered */
		set_visibility(next_ent, EVT_COVERED);
		temp = *next;
		*next = temp->next;
	}

	return NULL;
}

static void
evt_ent_addr_update(struct evt_context *tcx, struct evt_entry *ent,
		    daos_size_t diff)
{
	if (bio_addr_is_hole(&ent->en_addr))
		return; /* Nothing to do for holes */

	D_ASSERT(tcx->tc_inob != 0);
	ent->en_addr.ba_off += diff * tcx->tc_inob;
}

static void
evt_split_entry(struct evt_context *tcx, struct evt_entry *current,
		struct evt_entry *next, struct evt_entry *split,
		struct evt_entry *covered)
{
	struct evt_list_entry	*le;
	daos_off_t		 diff;

	D_ASSERT((current->en_visibility & EVT_REMOVE) == 0);
	current->en_visibility |= EVT_PARTIAL;
	*covered = *split = *current;
	diff = next->en_sel_ext.ex_hi + 1 - split->en_sel_ext.ex_lo;
	split->en_sel_ext.ex_lo = next->en_sel_ext.ex_hi + 1;
	/* mark the entries as partial */
	current->en_sel_ext.ex_hi = next->en_sel_ext.ex_lo - 1;
	covered->en_sel_ext = next->en_sel_ext;
	set_visibility(covered, EVT_COVERED);
	evt_ent_addr_update(tcx, split, diff);
	evt_ent_addr_update(tcx, covered,
			    evt_extent_width(&current->en_sel_ext));
	/* the split entry may also be covered so store a back pointer */
	le = evt_array_entry2le(split);
	le->le_prev = covered;
}

static d_list_t *
evt_insert_sorted(struct evt_entry *this_ent, d_list_t *head, d_list_t *current)
{
	d_list_t		*start = current;
	struct evt_entry	*next_ent;
	d_list_t		*this_link;
	int			 cmp;

	this_link = evt_array_entry2link(this_ent);

	while (current != head) {
		next_ent = evt_array_link2entry(current);
		cmp = evt_ent_cmp(this_ent, next_ent, NULL);
		if (cmp < 0) {
			d_list_add(this_link, current->prev);
			goto out;
		}
		current = current->next;
	}
	d_list_add_tail(this_link, head);
out:
	if (start == current)
		return this_link;
	return start;
}

static int
evt_truncate_next(struct evt_context *tcx, struct evt_entry_array *ent_array,
		  struct evt_entry *this_ent, struct evt_entry *next_ent)
{
	struct evt_list_entry	*le;
	struct evt_entry	*temp_ent;
	daos_size_t		 diff;
	int			 rc;

	le = evt_array_entry2le(next_ent);
	if (le->le_prev &&
	    le->le_prev->en_visibility == (EVT_COVERED | EVT_PARTIAL)) {
		/* The truncated part has same visibility as prior split */
		temp_ent = le->le_prev;
		D_ASSERTF(temp_ent->en_sel_ext.ex_hi + 1 ==
			  next_ent->en_sel_ext.ex_lo,
			  "next_ent "DF_ENT" is not contiguous "DF_ENT"\n",
			  DP_ENT(next_ent), DP_ENT(temp_ent));
	} else {
		/* allocate a record for truncated entry */
		rc = ent_array_alloc(tcx, ent_array, &temp_ent, true);
		if (rc != 0)
			return rc;

		next_ent->en_visibility |= EVT_PARTIAL;
		*temp_ent = *next_ent;
	}

	le->le_prev = temp_ent;
	diff = this_ent->en_sel_ext.ex_hi + 1 - next_ent->en_sel_ext.ex_lo;
	next_ent->en_sel_ext.ex_lo = this_ent->en_sel_ext.ex_hi + 1;
	temp_ent->en_sel_ext.ex_hi = next_ent->en_sel_ext.ex_lo - 1;
	set_visibility(temp_ent, EVT_COVERED);
	evt_ent_addr_update(tcx, next_ent, diff);

	return 0;
}

static inline void
evt_mark_visible(struct evt_entry *ent, bool check, int *num_visible)
{
	if (ent->en_minor_epc == EVT_MINOR_EPC_MAX) {
		/** This entry is a "remove" added by evt_remove_all */
		D_ASSERT(check);
		set_visibility(ent, EVT_REMOVE);
		return;
	}

	set_visibility(ent, EVT_VISIBLE);
	(*num_visible)++;
}

static int
evt_mark_removed(struct evt_context *tcx, struct evt_entry_array *ent_array,
		 d_list_t *deleted, d_list_t *to_process)
{
	d_list_t		*cur_update;
	d_list_t		*cur_del;
	d_list_t		*temp;
	struct evt_entry	*del_ent;
	struct evt_entry	*this_ent;
	struct evt_entry	*temp_ent;
	struct evt_extent	*del_ext;
	struct evt_extent	*this_ext;
	struct evt_extent	*temp_ext;
	int			 rc;

	if (d_list_empty(deleted) || d_list_empty(to_process))
		return 0;

	cur_update = to_process->next;
	cur_del = deleted->next;

	while (cur_update != to_process) {
		this_ent = evt_array_link2entry(cur_update);
		this_ext = &this_ent->en_sel_ext;

		/** Find matching deleted extent */
		d_list_for_each_safe(cur_del, temp, deleted) {
			del_ent = evt_array_link2entry(cur_del);
			del_ext = &del_ent->en_sel_ext;
			if (this_ext->ex_lo > del_ext->ex_hi) {
				/* We are done processing this delete record */
				d_list_del(cur_del);
				continue;
			}

			if (this_ent->en_epoch == del_ent->en_epoch &&
			    this_ext->ex_hi >= del_ext->ex_lo) {
				/* Found a deleted extent */
				goto process_ext;
			}
		}

		if (d_list_empty(deleted))
			break;

		/** Skip to next record */
		D_ASSERT(cur_del == deleted);
		cur_update = cur_update->next;
		continue;

process_ext:
		temp_ent = NULL;
		if (this_ext->ex_lo < del_ext->ex_lo) {
			/* Split, head is not covered */
			rc = ent_array_alloc(tcx, ent_array, &temp_ent, true);
			if (rc != 0)
				return rc;

			this_ent->en_visibility |= EVT_PARTIAL;
			*temp_ent = *this_ent;
			temp_ext = &temp_ent->en_sel_ext;
			temp_ext->ex_lo = del_ext->ex_lo;
			this_ext->ex_hi = del_ext->ex_lo - 1;
			evt_ent_addr_update(tcx, temp_ent,
					    evt_extent_width(this_ext));
			if (temp_ext->ex_hi <= del_ext->ex_hi) {
				/* Tail is fully covered */
				set_visibility(temp_ent,
					      EVT_COVERED);
				cur_update = cur_update->next;
				continue;
			}

			/** Leave marking it covered until processed */
			cur_update = evt_insert_sorted(temp_ent,
						    to_process,
						    cur_update);
		} else if (this_ext->ex_hi > del_ext->ex_hi) {
			/* Split, tail is not covered */
			rc = ent_array_alloc(tcx, ent_array, &temp_ent, true);
			if (rc != 0)
				return rc;

			this_ent->en_visibility |= EVT_PARTIAL;
			*temp_ent = *this_ent;
			temp_ext = &temp_ent->en_sel_ext;
			this_ext->ex_hi = del_ext->ex_hi;
			temp_ext->ex_lo = del_ext->ex_hi + 1;
			set_visibility(this_ent, EVT_COVERED);
			evt_ent_addr_update(tcx, temp_ent,
					    evt_extent_width(this_ext));
			temp = cur_update;
			cur_update = cur_update->next;
			d_list_del(temp);
			/** Leave marking it covered until processed */
			cur_update = evt_insert_sorted(temp_ent,
						    to_process,
						    cur_update);
		} else {
			temp = cur_update;
			cur_update = cur_update->next;
			d_list_del(temp);
			set_visibility(this_ent, EVT_COVERED);
		}
	}

	return 0;
}

static int
evt_find_visible(struct evt_context *tcx, const struct evt_filter *filter,
		 struct evt_entry_array *ent_array, int *num_visible,
		 bool removals_only)
{
	struct evt_extent	*this_ext;
	struct evt_extent	*next_ext;
	struct evt_list_entry	*le;
	struct evt_list_entry   *next_le;
	struct evt_list_entry   *tmp_le;
	struct evt_entry	*this_ent;
	struct evt_entry	*next_ent;
	struct evt_entry	*temp_ent;
	struct evt_entry	*split;
	d_list_t		 covered;
	d_list_t		 removals;
	d_list_t		*current;
	d_list_t		*next;
	bool			 insert;
	int			 rc = 0;

	D_INIT_LIST_HEAD(&covered);
	D_INIT_LIST_HEAD(&removals);
	*num_visible = 0;

	/* Some of the entries may be punched by a key.  We don't need to
	 * consider such entries for the visibility algorithm and can mark them
	 * covered to start.   All other entries are placed into the sorted list
	 * to be considered in the visibility algorithm.
	 */
	evt_ent_array_for_each(this_ent, ent_array) {
		next = evt_array_entry2link(this_ent);

		if (evt_entry_punched(this_ent, filter)) {
			set_visibility(this_ent, EVT_COVERED);
			continue;
		}

		if (this_ent->en_minor_epc == EVT_MINOR_EPC_MAX) {
			evt_array_entry2le(this_ent)->le_prev = NULL;
			set_visibility(this_ent, EVT_REMOVE);
			d_list_add_tail(next, &removals);
			continue;
		}

		evt_array_entry2le(this_ent)->le_prev = NULL;
		d_list_add_tail(next, &covered);
	}

	/** Mark removed entries as covered */
	rc = evt_mark_removed(tcx, ent_array, &removals, &covered);
	if (rc != 0)
		return rc;

	if (d_list_empty(&covered))
		return 0;

	if (removals_only) {
		/** Ok, everything remaining in the list is not covered by a removal.  One more
		 *  step is required to merge records at the same major epoch so we don't attempt
		 *  to insert overlapping removal records.  Once this step is complete, we can sort
		 *  the remaining records and return to the caller.
		 */
		while ((le = d_list_pop_entry(&covered, struct evt_list_entry, le_link)) != NULL) {
			this_ent = &le->le_ent;
			d_list_for_each_entry_safe(next_le, tmp_le, &covered, le_link) {
				next_ent = &next_le->le_ent;
				if (next_ent->en_sel_ext.ex_lo > (this_ent->en_sel_ext.ex_hi + 1)) {
					/** If the physical extent is also beyond the end,
					 *  everything else in the list is guaranteed to be later
					 *  so we can break the loop. Otherwise, just continue to
					 *  next entry.
					 */
					if (next_ent->en_ext.ex_lo > (this_ent->en_ext.ex_hi + 1))
						break;
					continue;
				}
				if (next_ent->en_epoch != this_ent->en_epoch)
					continue;
				set_visibility(next_ent, EVT_COVERED);
				d_list_del(&next_le->le_link);
				/** For removals, we don't care about the actual extent,
				 *  so we can just fake it.  We only want to know what
				 *  removal to insert.
				 */
				this_ent->en_sel_ext.ex_hi = next_ent->en_sel_ext.ex_hi;
			}
			/** Mark the current entry as visible for sorting */
			evt_mark_visible(this_ent, false, num_visible);
		}
		return 0;
	}

	/* Now uncover entries */
	current = covered.next;
	/* Some compilers can't tell that this_ent will be initialized */
	this_ent = evt_array_link2entry(current);
	insert = true;
	next = current->next;

	while (next != &covered) {
		if (insert) {
			this_ent = evt_array_link2entry(current);
			evt_mark_visible(this_ent, false, num_visible);
			evt_array_entry2le(this_ent)->le_prev = NULL;
		}

		insert = true;

		/* Find next visible rectangle */
		next_ent = evt_find_next_visible(this_ent, &covered, &next);
		if (next_ent == NULL)
			return 0;

		this_ext = &this_ent->en_sel_ext;
		next_ext = &next_ent->en_sel_ext;
		current = next;
		next = current->next;
		/* NB: Three possibilities
		 * 1. No intersection.  Current entry is inserted in entirety
		 * 2. Partial intersection, next is earlier. Next is truncated
		 * 3. Partial intersection, next is later. Current is truncated
		 * 4. Current entry contains next_entry.  Current is split
		 * in two and both are truncated.
		 */
		if (next_ext->ex_lo >= this_ext->ex_hi + 1) {
			/* Case #1, entry already inserted, nothing to do */
			continue;
		}

		if (evt_ent_is_later(this_ent, next_ent)) {
			/* Case #2, next rect is partially under this rect,
			 * Truncate left end of next_ent, reinsert.
			 *
			 * This case is complicated by the fact that the
			 * left end may just be an extension of a previously
			 * split but covered entry.
			 */
			rc = evt_truncate_next(tcx, ent_array, this_ent,
					       next_ent);
			if (rc != 0)
				return rc;

			/* current now points at next_ent.  Remove it and
			 * reinsert it in the list in case truncation moved
			 * it to a new position
			 */
			d_list_del(current);
			next = evt_insert_sorted(next_ent, &covered, next);

			/* Now we need to rerun this iteration without
			 * inserting this_ent again
			 */
			insert = false;
			continue;
		}

		/* allocate a record for truncated entry */
		rc = ent_array_alloc(tcx, ent_array, &temp_ent, true);
		if (rc != 0)
			return rc;

		if (next_ext->ex_hi >= this_ext->ex_hi) {
			/* Case #3, truncate this_ent */
			this_ent->en_visibility |= EVT_PARTIAL;
			*temp_ent = *this_ent;
			this_ext->ex_hi = next_ext->ex_lo - 1;
			temp_ent->en_sel_ext.ex_lo = next_ext->ex_lo;
			set_visibility(temp_ent, EVT_COVERED);
			evt_ent_addr_update(tcx, temp_ent,
					    evt_extent_width(this_ext));
		} else {
			rc = ent_array_alloc(tcx, ent_array, &split, true);
			if (rc != 0) {
				/* Decrement ea_ent_nr by 1 to "free" the space
				 * allocated for temp_ent
				 */
				ent_array->ea_ent_nr--;
				return rc;
			}
			/* Case #4, split, insert tail into sorted list */
			evt_split_entry(tcx, this_ent, next_ent, split,
					temp_ent);
			/* Current points at next_ent */
			next = evt_insert_sorted(split, &covered, next);
		}
	}

	this_ent = evt_array_link2entry(current);
	D_ASSERT(!evt_flags_equal(this_ent->en_visibility, EVT_COVERED));
	evt_mark_visible(this_ent, false, num_visible);

	return 0;
}

/** Place all entries into covered list in sorted order based on selected
 * range.   Then walk through the range to find only extents that are visible
 * and place them in the main list.   Update the selection bounds for visible
 * rectangles.
 */
int
evt_ent_array_sort(struct evt_context *tcx, struct evt_entry_array *ent_array,
		   const struct evt_filter *filter, int flags)
{
	struct evt_list_entry	*ents;
	struct evt_entry	*ent;
	int			(*compar)(const void *, const void *);
	int			 total;
	int			 num_visible = 0;
	int			 rc;

	D_DEBUG(DB_TRACE, "Sorting array with filter "DF_FILTER", ea_ent_nr %d.\n",
		DP_FILTER(filter), ent_array->ea_ent_nr);
	if (ent_array->ea_ent_nr == 0)
		return 0;

	if (ent_array->ea_ent_nr == 1) {
		ent = evt_ent_array_get(ent_array, 0);
		if (evt_entry_punched(ent, filter)) {
			ent->en_visibility |= EVT_COVERED;
		} else {
			evt_mark_visible(ent, true, &num_visible);
		}
		goto re_sort;
	}

	for (;;) {
		ents = ent_array->ea_ents;

		/* Sort the array first */
		qsort(ents, ent_array->ea_ent_nr, sizeof(ents[0]),
		      evt_ent_list_cmp);

		/* Now separate entries into covered and visible */
		rc = evt_find_visible(tcx, filter, ent_array, &num_visible,
				      (flags & EVT_ITER_REMOVALS) != 0);
		if (rc != 0) {
			if (rc == -DER_AGAIN)
				continue; /* List reallocated, start over */
			return rc;
		}
		break;
	}

re_sort:
	ents = ent_array->ea_ents;
	/* Now re-sort the entries */
	if (flags & EVT_COVERED) {
		total = ent_array->ea_ent_nr;
		compar = evt_ent_list_cmp;
	} else {
		D_ASSERT(flags & (EVT_ITER_VISIBLE | EVT_ITER_REMOVALS));
		compar = evt_ent_list_cmp_visible;
		total = num_visible;
	}

	if (ent_array->ea_ent_nr != 1)
		qsort(ents, ent_array->ea_ent_nr, sizeof(ents[0]), compar);

	ent_array->ea_ent_nr = total;

	return 0;
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
	tcx->tc_trace = &tcx->tc_trace_scratch[EVT_TRACE_MAX - depth];
}

static struct evt_trace *
evt_tcx_trace(struct evt_context *tcx, int level)
{
	D_ASSERT(tcx->tc_depth > 0);
	D_ASSERT(level >= 0 && level < tcx->tc_depth);
	D_ASSERT(&tcx->tc_trace[level] < &tcx->tc_trace_scratch[EVT_TRACE_MAX]);

	return &tcx->tc_trace[level];
}

static void
evt_tcx_set_trace(struct evt_context *tcx, int level, umem_off_t nd_off, int at,
		  bool alloc)
{
	struct evt_trace *trace;

	D_ASSERTF(at >= 0 && at < tcx->tc_order, "at=%d, tcx->tc_order=%d, level=%d\n", at,
		  tcx->tc_order, level);

	V_TRACE(DB_TRACE, "set trace[%d] "DF_X64"/%d\n", level, nd_off, at);

	trace = evt_tcx_trace(tcx, level);
	trace->tr_at = at;
	if (trace->tr_node == nd_off)
		return;

	trace->tr_node = nd_off;
	trace->tr_tx_added = alloc;
}

/** Reset all traces within context and set root as the 0-level trace */
static void
evt_tcx_reset_trace(struct evt_context *tcx)
{
	memset(&tcx->tc_trace_scratch[0], 0,
	       sizeof(tcx->tc_trace_scratch[0]) * EVT_TRACE_MAX);
	evt_tcx_set_dep(tcx, tcx->tc_root->tr_depth);
	evt_tcx_set_trace(tcx, 0, tcx->tc_root->tr_node, 0, false);
}

/**
 * Create a evtree context for create or open
 *
 * \param root		[IN]	root address for inplace open
 * \param feats		[IN]	Optional, feature bits for create
 * \param order		[IN]	Optional, tree order for create
 * \param cbs		[IN]	Callbacks and arguments for evt_desc
 * \param tcx_pp	[OUT]	The returned tree context
 */
static int
evt_tcx_create(struct evt_root *root, uint64_t feats, unsigned int order,
	       struct umem_attr *uma, struct evt_desc_cbs *cbs,
	       struct evt_context **tcx_pp)
{
	struct evt_context	*tcx;
	int			 depth;
	int			 rc;
	int			 policy;

	D_ASSERT(root != NULL);

	D_ALLOC_PTR(tcx);
	if (tcx == NULL)
		return -DER_NOMEM;

	tcx->tc_ref	 = 1; /* for the caller */
	tcx->tc_magic	 = EVT_HDL_ALIVE;
	tcx->tc_root	 = root;
	if (cbs != NULL)
		tcx->tc_desc_cbs = *cbs;

	rc = umem_class_init(uma, &tcx->tc_umm);
	if (rc != 0) {
		D_ERROR("Failed to setup mem class %d: "DF_RC"\n", uma->uma_id,
			DP_RC(rc));
		D_GOTO(failed, rc);
	}

	if (feats != -1) { /* tree creation */
		tcx->tc_feats	= feats;
		if (feats & EVT_FEAT_DYNAMIC_ROOT) {
			tcx->tc_order     = 1;
			tcx->tc_max_order = order;
		} else {
			tcx->tc_order     = order;
			tcx->tc_max_order = order;
		}
		depth		= 0;
		V_TRACE(DB_TRACE, "Create context for a new tree\n");

	} else {
		if (root->tr_pool_uuid != umem_get_uuid(evt_umm(tcx))) {
			D_ERROR("Mixing pools in same evtree not allowed\n");
			rc = -DER_INVAL;
			goto failed;
		}

		tcx->tc_feats	= root->tr_feats;
		tcx->tc_order	= root->tr_order;
		if (tcx->tc_feats & EVT_FEAT_DYNAMIC_ROOT)
			tcx->tc_max_order = root->tr_max_order;
		else
			tcx->tc_max_order = root->tr_order;
		tcx->tc_inob	= root->tr_inob;
		depth		= root->tr_depth;
		V_TRACE(DB_TRACE, "Load tree context from %p\n", root);
	}

	policy = tcx->tc_feats & EVT_POLICY_MASK;
	switch (policy) {
	case EVT_FEAT_SORT_SOFF:
		tcx->tc_ops = evt_policies[0];
		break;
	case EVT_FEAT_SORT_DIST:
		tcx->tc_ops = evt_policies[1];
		break;
	case EVT_FEAT_SORT_DIST_EVEN:
		tcx->tc_ops = evt_policies[2];
		break;
	default:
		D_ERROR("Bad sort policy specified: %#x\n", policy);
		D_GOTO(failed, rc = -DER_INVAL);
	}
	D_DEBUG(DB_TRACE, "EVTree sort policy is %#x\n", policy);

	/* Initialize the embedded iterator entry array.  This is a minor
	 * optimization if the iterator is used more than once
	 */
	evt_ent_array_init(tcx->tc_iter.it_entries, 0);
	evt_tcx_set_dep(tcx, depth);
	*tcx_pp = tcx;
	return 0;

 failed:
	V_TRACE(DB_TRACE, "Failed to create tree context: "DF_RC"\n",
		DP_RC(rc));
	evt_tcx_decref(tcx);
	return rc;
}

int
evt_tcx_clone(struct evt_context *tcx, struct evt_context **tcx_pp)
{
	struct umem_attr uma = {0};
	int		 rc;

	umem_attr_get(&tcx->tc_umm, &uma);
	if (!tcx->tc_root || tcx->tc_root->tr_feats == 0)
		return -DER_INVAL;

	rc = evt_tcx_create(tcx->tc_root, -1, -1, &uma,
			    &tcx->tc_desc_cbs, tcx_pp);
	return rc;
}

int
evt_desc_bio_free(struct evt_context *tcx, struct evt_desc *desc,
		  daos_size_t nob)
{
	struct evt_desc_cbs *cbs = &tcx->tc_desc_cbs;

	/* Free the bio address referenced by dst_desc, it is a callback
	 * because evtree should not depend on bio functions
	 */
	D_ASSERT(cbs && cbs->dc_bio_free_cb);
	return cbs->dc_bio_free_cb(evt_umm(tcx), desc, nob,
				   cbs->dc_bio_free_args);
}

int
evt_desc_log_status(struct evt_context *tcx, daos_epoch_t epoch,
		    struct evt_desc *desc, int intent)
{
	struct evt_desc_cbs *cbs = &tcx->tc_desc_cbs;

	D_ASSERT(cbs);
	if (!cbs->dc_log_status_cb) {
		return ALB_AVAILABLE_CLEAN;
	} else {
		return cbs->dc_log_status_cb(evt_umm(tcx), epoch, desc, intent, true,
					     cbs->dc_log_status_args);
	}
}

static int
evt_desc_log_add(struct evt_context *tcx, struct evt_desc *desc)
{
	struct evt_desc_cbs *cbs = &tcx->tc_desc_cbs;

	D_ASSERT(cbs);
	return cbs->dc_log_add_cb ?
	       cbs->dc_log_add_cb(evt_umm(tcx), desc, cbs->dc_log_add_args) : 0;
}

int
evt_desc_log_del(struct evt_context *tcx, daos_epoch_t epoch,
		 struct evt_desc *desc)
{
	struct evt_desc_cbs *cbs = &tcx->tc_desc_cbs;

	D_ASSERT(cbs);
	return cbs->dc_log_del_cb ?
	       cbs->dc_log_del_cb(evt_umm(tcx), epoch, desc,
				  cbs->dc_log_del_args) : 0;
}

static int
evt_node_entry_free(struct evt_context *tcx, struct evt_node_entry *ne)
{
	struct evt_desc	*desc;
	struct evt_rect	 rect;
	int		 rc;

	if (UMOFF_IS_NULL(ne->ne_child))
		return 0;

	evt_rect_read(&rect, &ne->ne_rect);

	desc = evt_off2desc(tcx, ne->ne_child);
	rc = evt_desc_log_del(tcx, rect.rc_epc, desc);
	if (rc)
		goto out;

	rc = evt_desc_bio_free(tcx, desc,
			       tcx->tc_inob * evt_rect_width(&rect));
	if (rc)
		goto out;

	rc = umem_free(evt_umm(tcx), ne->ne_child);
	if (rc)
		goto out;

	return 0;
out:
	D_ERROR("Failed to release entry: " DF_RC "\n", DP_RC(rc));
	return rc;
}

/** check if a node is full */
static bool
evt_node_is_full(struct evt_context *tcx, struct evt_node *nd)
{
	D_ASSERTF(nd->tn_nr <= tcx->tc_order, "tn_nr=%d tc_order=%d\n", nd->tn_nr, tcx->tc_order);
	return nd->tn_nr == tcx->tc_order;
}

static inline void
evt_node_unset(struct evt_context *tcx, struct evt_node *nd, unsigned int bits)
{
	nd->tn_flags &= ~bits;
}

/** Return the address of child umem offset at the offset of @at */
static umem_off_t
evt_node_child_at(struct evt_context *tcx, struct evt_node *node,
		  unsigned int at)
{
	D_ASSERT(!evt_node_is_leaf(tcx, node));
	return node->tn_child[at];
}

void
evt_node_rect_read_at(struct evt_context *tcx, struct evt_node *node,
		      unsigned int at, struct evt_rect *rout)
{
	struct evt_node_entry	*ne;
	struct evt_node		*child;

	if (evt_node_is_leaf(tcx, node)) {
		ne = evt_node_entry_at(tcx, node, at);
		evt_rect_read(rout, &ne->ne_rect);
	} else {
		child = evt_off2node(tcx, evt_node_child_at(tcx, node, at));
		evt_mbr_read(rout, child);
	}
}


/**
 * This function adjusts the location of the child record,
 * if necessary and updates the mbr of the parent, if necessary.
 *
 * \param[in]	tcx	Evtree context
 * \param[in]	node	Node with MBR to update
 * \param[in]	child	Child node to add
 * \param[in]	at	Location of child record in node
 * \return	true	Node MBR changed
 *		false	No changed.
 */
static bool
evt_node_mbr_update(struct evt_context *tcx, struct evt_node *node,
		    const struct evt_node *child, int at)
{
	struct evt_rect		 rin;
	struct evt_rect		 rout;
	bool			 changed;

	/* make adjustments to the position of the rectangle */
	if (tcx->tc_ops->po_adjust)
		tcx->tc_ops->po_adjust(tcx, node, at);

	evt_mbr_read(&rin, child);

	/* merge the rectangle with the current node */
	evt_mbr_read(&rout, node);
	changed = evt_rect_merge(&rout, &rin);

	if (changed)
		evt_mbr_write(node, &rout);

	return changed;
}

static inline int
evt_order2size(int order, bool leaf)
{
	size_t entry_size;

	entry_size = leaf ? sizeof(struct evt_node_entry) : sizeof(uint64_t);

	return sizeof(struct evt_node) + entry_size * order;
}

/**
 * Return the size of evtree node, leaf node has different size with internal
 * node.
 */
static inline int
evt_node_size(struct evt_context *tcx, bool leaf)
{
	return evt_order2size(tcx->tc_order, leaf);
}

/** Allocate a evtree node */
static int
evt_node_alloc(struct evt_context *tcx, unsigned int flags,
	       umem_off_t *nd_off_p)
{
	struct evt_node         *nd;
	umem_off_t		 nd_off;
	bool                     leaf = (flags & EVT_NODE_LEAF);

	nd_off = umem_zalloc(evt_umm(tcx), evt_node_size(tcx, leaf));
	if (UMOFF_IS_NULL(nd_off))
		return -DER_NOSPACE;

	V_TRACE(DB_TRACE, "Allocate new node "DF_U64" %d bytes\n",
		nd_off, evt_node_size(tcx, leaf));
	nd = evt_off2ptr(tcx, nd_off);
	nd->tn_flags = flags;
	nd->tn_magic = EVT_NODE_MAGIC;

	*nd_off_p = nd_off;
	return 0;
}

static inline int
evt_node_tx_add(struct evt_context *tcx, struct evt_node *nd)
{
	if (!evt_has_tx(tcx))
		return 0;

	return umem_tx_add_ptr(evt_umm(tcx), nd,
			       evt_node_size(tcx, evt_node_is_leaf(tcx, nd)));
}

static inline int
evt_node_free(struct evt_context *tcx, umem_off_t nd_off)
{
	return umem_free(evt_umm(tcx), nd_off);
}

/**
 * Destroy a tree node and all its desendants nodes, or leaf records and
 * data extents.
 */
static int
evt_node_destroy(struct evt_context *tcx, umem_off_t nd_off, int level,
		 bool *empty_ret)
{
	struct evt_node_entry	*ne;
	struct evt_node		*nd;
	bool			 empty;
	bool			 leaf;
	int			 i;
	int			 rc = 0;

	nd = evt_off2node(tcx, nd_off);
	leaf = evt_node_is_leaf(tcx, nd);

	V_TRACE(DB_TRACE, "Destroy %s node at level %d (nr = %d)\n",
		leaf ? "leaf" : "", level, nd->tn_nr);

	empty = true;
	for (i = nd->tn_nr - 1; i >= 0; i--) {
		if (leaf) {
			ne = evt_node_entry_at(tcx, nd, i);
			/* NB: This will be replaced with a callback */
			rc = evt_node_entry_free(tcx, ne);
			if (rc)
				goto out;

			if (!tcx->tc_creds_on)
				continue;

			D_ASSERT(tcx->tc_creds > 0);
			tcx->tc_creds--;
			if (tcx->tc_creds == 0) {
				empty = (i == 0);
				break;
			}
		} else {
			rc = evt_node_destroy(tcx, nd->tn_child[i], level + 1,
					      &empty);
			if (rc) {
				D_ERROR("destroy failed: " DF_RC "\n", DP_RC(rc));
				goto out;
			}

			if (!tcx->tc_creds_on || tcx->tc_creds > 0) {
				D_ASSERT(empty);
				continue;
			}
			D_ASSERT(tcx->tc_creds == 0);

			if (empty) {
				if (i > 0) /* some children are not empty */
					empty = false;
			} else {
				i += 1;
			}
			break;
		}
	}

	if (empty) {
		rc = evt_node_free(tcx, nd_off);
	} else {
		rc = evt_node_tx_add(tcx, nd);
		if (rc == 0)
			nd->tn_nr = i;
	}

	if (rc == 0 && empty_ret)
		*empty_ret = empty;
out:
	return rc;
}

/** (Re)compute MBR for a tree node */
static void
evt_node_mbr_cal(struct evt_context *tcx, struct evt_node *node)
{
	struct evt_rect	 mbr;
	int		 i;

	D_ASSERT(node->tn_nr != 0);

	evt_node_rect_read_at(tcx, node, 0, &mbr);
	for (i = 1; i < node->tn_nr; i++) {
		struct evt_rect rect;

		evt_node_rect_read_at(tcx, node, i, &rect);
		evt_rect_merge(&mbr, &rect);
	}
	evt_mbr_write(node, &mbr);
	V_TRACE(DB_TRACE, "Compute out MBR "DF_RECT", nr=%d\n", DP_RECT(&mbr),
		node->tn_nr);
}

/**
 * Split tree node \a src_nd by moving some entries from it to the new
 * node \a dst_nd. This function also updates MBRs for both nodes.
 *
 * Node split is a customized method of tree policy.
 */
static int
evt_node_split(struct evt_context *tcx, bool leaf,
	       struct evt_node *src_nd, struct evt_node *dst_nd)
{
	int	rc;

	rc = tcx->tc_ops->po_split(tcx, leaf, src_nd, dst_nd);
	if (rc == 0) { /* calculate MBR for both nodes */
		evt_node_mbr_cal(tcx, src_nd);
		evt_node_mbr_cal(tcx, dst_nd);
	}
	return rc;
}

/**
 * Insert a new entry into a node \a nd, update MBR of the node if it's
 * enlarged after inserting the new entry. This function should be called
 * only if the node has empty slot (not full).
 *
 * Entry insertion is a customized method of tree policy.
 */
static int
evt_node_insert(struct evt_context *tcx, struct evt_node *nd, umem_off_t in_off,
		const struct evt_entry_in *ent, bool *mbr_changed,
		uint8_t **csum_bufp)
{
	int		 rc;
	bool		 changed = 0;

	V_TRACE(DB_TRACE, "Insert "DF_RECT" into "DF_MBR"\n",
		DP_RECT(&ent->ei_rect), DP_MBR(nd));

	rc = tcx->tc_ops->po_insert(tcx, nd, in_off, ent, &changed, csum_bufp);
	if (rc != 0)
		return rc;

	V_TRACE(DB_TRACE, "New MBR is "DF_MBR", nr=%d\n", DP_MBR(nd),
		nd->tn_nr);
	if (mbr_changed)
		*mbr_changed = changed;

	return 0;
}

/**
 * Calculate weight difference of the node between before and after adding
 * a new rectangle \a rect. This function is supposed to help caller to choose
 * destination node for insertion.
 *
 * Weight calculation is a customized method of tree policy.
 */
static void
evt_node_weight_diff(struct evt_context *tcx, struct evt_node *nd,
		     const struct evt_rect *rect,
		     struct evt_weight *weight_diff)
{
	struct evt_rect	   rtmp;
	struct evt_weight  wt_org;
	struct evt_weight  wt_new;

	wt_org.wt_major = 0;
	wt_org.wt_minor = 0;
	wt_new.wt_major = 0;
	wt_new.wt_minor = 0;

	evt_mbr_read(&rtmp, nd);
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

	return root == NULL || UMOFF_IS_NULL(root->tr_node);
}

/** Add the tree root to the transaction */
static int
evt_root_tx_add(struct evt_context *tcx)
{
	struct evt_root	*root;

	if (!evt_has_tx(tcx))
		return 0;

	D_ASSERT(tcx->tc_root != NULL);
	root = tcx->tc_root;

	return umem_tx_add_ptr(evt_umm(tcx), root, sizeof(*root));
}

/** Initialize the tree root */
static int
evt_root_init(struct evt_context *tcx)
{
	struct evt_root	*root;
	int		 rc;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

	root = tcx->tc_root;

	root->tr_feats = tcx->tc_feats;
	root->tr_order = tcx->tc_order;
	if (tcx->tc_feats & EVT_FEAT_DYNAMIC_ROOT)
		root->tr_max_order = tcx->tc_max_order;
	else
		root->tr_max_order = 0; /** For backward compatibility */
	root->tr_node  = UMOFF_NULL;
	root->tr_pool_uuid = umem_get_uuid(evt_umm(tcx));

	return 0;
}

static int
evt_root_free(struct evt_context *tcx)
{
	int	rc;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		goto out;
	memset(tcx->tc_root, 0, sizeof(*tcx->tc_root));
out:
	tcx->tc_root = NULL;
	return rc;
}

/**
 * Activate an empty tree by allocating a node for the root and set the
 * tree depth to one.
 */
static int
evt_root_activate(struct evt_context *tcx, const struct evt_entry_in *ent)
{
	struct evt_root			*root;
	umem_off_t			 nd_off;
	int				 rc;
	const struct dcs_csum_info	*csum;

	root = tcx->tc_root;
	uint32_t inob = ent->ei_inob;
	csum = &ent->ei_csum;

	D_ASSERT(root->tr_depth == 0);
	D_ASSERT(UMOFF_IS_NULL(root->tr_node));

	/* root node is also a leaf node */
	rc = evt_node_alloc(tcx, EVT_NODE_ROOT | EVT_NODE_LEAF, &nd_off);
	if (rc != 0)
		return rc;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

	root->tr_node = nd_off;
	root->tr_depth = 1;
	if (inob != 0)
		tcx->tc_inob = root->tr_inob = inob;
	if (ci_is_valid((struct dcs_csum_info *) csum)) {
		/**
		 * csum len, type, and chunksize will be a configuration stored
		 * in the container meta data. for now trust the entity checksum
		 * to have correct values.
		 */
		tcx->tc_root->tr_csum_len		= csum->cs_len;
		tcx->tc_root->tr_csum_type		= csum->cs_type;
		tcx->tc_root->tr_csum_chunk_size	= csum->cs_chunksize;
	}

	evt_tcx_set_dep(tcx, root->tr_depth);
	evt_tcx_set_trace(tcx, 0, nd_off, 0, true);

	return 0;
}

static int
evt_root_deactivate(struct evt_context *tcx)
{
	struct evt_root	*root = tcx->tc_root;
	int		 rc;

	D_ASSERT(root->tr_depth != 0);
	D_ASSERT(root->tr_node != 0);

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		return rc;

	root->tr_depth = 0;
	rc = umem_free(evt_umm(tcx), root->tr_node);
	if (rc != 0)
		return rc;

	root->tr_node = UMOFF_NULL;
	evt_tcx_set_dep(tcx, 0);
	return 0;
}

/** Destroy the root node and all its descendants. */
static int
evt_root_destroy(struct evt_context *tcx, bool *destroyed)
{
	struct evt_root *root;
	int		 rc = 0;
	bool		 empty = true;

	root = tcx->tc_root;
	if (root && !UMOFF_IS_NULL(root->tr_node)) {
		/* destroy the root node and all descendants */
		rc = evt_node_destroy(tcx, root->tr_node, 0, &empty);
		if (rc != 0)
			return rc;
	}

	*destroyed = empty;
	if (empty)
		rc = evt_root_free(tcx);

	return rc;
}

static int64_t
evt_epoch_dist(struct evt_context *tcx, struct evt_node *nd,
	       const struct evt_rect *rect)
{
	struct evt_rect	 mbr;
	int64_t		 diff1, diff2;

	evt_mbr_read(&mbr, nd);

	diff1 = (mbr.rc_epc - rect->rc_epc) << 16;
	if (diff1 < 0)
		diff1 = -diff1;

	diff2 = (mbr.rc_minor_epc - rect->rc_minor_epc);
	if (diff2 < 0)
		diff2 = -diff2;

	return diff1 + diff2;
}

/** Select a node from two for the rectangle \a rect being inserted */
static struct evt_node *
evt_select_node(struct evt_context *tcx, const struct evt_rect *rect,
		struct evt_node *nd1, struct evt_node *nd2)
{
	struct evt_weight	wt1;
	struct evt_weight	wt2;
	uint64_t		dist1;
	uint64_t		dist2;
	int			rc;

	evt_node_weight_diff(tcx, nd1, rect, &wt1);
	evt_node_weight_diff(tcx, nd2, rect, &wt2);

	rc = evt_weight_cmp(&wt1, &wt2);

	if (rc == 0) {
		dist1 = evt_epoch_dist(tcx, nd1, rect);
		dist2 = evt_epoch_dist(tcx, nd2, rect);

		if (dist1 < dist2)
			return nd1;
		else
			return nd2;
	}

	return rc < 0 ? nd1 : nd2;
}

/** Maximum dynamic root size before switching to user-specified order */
#define MAX_DYN_ROOT 7
static inline int
evt_new_order(int order, int max_order)
{
	if (order == MAX_DYN_ROOT)
		return max_order;

	return ((order + 1) << 1) - 1;
}

/** Expand the dynamic tree root node if it is currently full and not
 *  already at full size
 */
static inline int
evt_node_extend(struct evt_context *tcx, struct evt_trace *trace)
{
	struct evt_node *nd_cur;
	int              rc;
	uint8_t          old_order;
	void            *new_node;
	int              old_size;
	umem_off_t       new_off;

	if (tcx->tc_order == tcx->tc_max_order)
		return 0;

	nd_cur = evt_off2node(tcx, trace->tr_node);
	D_ASSERT(evt_node_is_leaf(tcx, nd_cur));
	D_ASSERT(tcx->tc_depth == 1);
	D_ASSERT(tcx->tc_feats & EVT_FEAT_DYNAMIC_ROOT);

	if (!evt_node_is_full(tcx, nd_cur))
		return 0;

	old_order = tcx->tc_order;
	old_size  = evt_node_size(tcx, true);

	tcx->tc_order = evt_new_order(tcx->tc_order, tcx->tc_max_order);

	if (tcx->tc_order > tcx->tc_max_order)
		tcx->tc_order = tcx->tc_max_order;

	rc = evt_node_alloc(tcx, true, &new_off);
	if (rc != 0)
		goto failed;

	new_node = umem_off2ptr(evt_umm(tcx), new_off);
	memcpy(new_node, nd_cur, old_size);
	rc = umem_free(evt_umm(tcx), trace->tr_node);
	if (rc != 0)
		goto failed;

	rc = evt_root_tx_add(tcx);
	if (rc != 0)
		goto failed;

	tcx->tc_root->tr_order = tcx->tc_order;
	tcx->tc_root->tr_node  = new_off;

	trace->tr_node     = new_off;
	trace->tr_tx_added = true;

	return 0;

failed:
	tcx->tc_order = old_order;
	return rc;
}

/**
 * Insert an entry \a entry to the leaf node located by the trace of \a tcx.
 * If the leaf node is full it will be split. The split will bubble up if its
 * parent is also full.
 */
static int
evt_insert_or_split(struct evt_context *tcx, const struct evt_entry_in *ent_new,
		    uint8_t **csum_bufp)
{
	struct evt_node		*mbr	  = NULL;
	struct evt_node		*nd_tmp   = NULL;
	umem_off_t		 nm_save  = UMOFF_NULL;
	struct evt_entry_in	 entry	  = *ent_new;
	int			 rc	  = 0;
	int			 level	  = tcx->tc_depth - 1;
	bool			 mbr_changed = false;
	bool			 mbr_update = false;

	while (1) {
		struct evt_trace	*trace;
		struct evt_node		*nd_cur;
		struct evt_node		*nd_new;
		umem_off_t		 nm_cur;
		umem_off_t		 nm_new;
		bool			 leaf;
		bool                     changed;

		trace	= &tcx->tc_trace[level];
		if (tcx->tc_depth > 1) {
			D_ASSERTF(tcx->tc_order == tcx->tc_max_order,
				  "Dynamic ordering for root only. order=%d != max_order=%d\n",
				  tcx->tc_order, tcx->tc_max_order);
		} else {
			rc = evt_node_extend(tcx, trace);
			if (rc != 0)
				goto failed;
		}

		nm_cur	= trace->tr_node;
		nd_cur = evt_off2node(tcx, nm_cur);

		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, nd_cur);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		if (mbr_update) {
			D_ASSERT(nd_tmp != NULL);
			mbr_update = false;
			mbr_changed = evt_node_mbr_update(tcx, nd_cur, nd_tmp,
							  trace->tr_at);
		}

		if (mbr) { /* This is set only if no more insert or split */
			D_ASSERT(mbr_changed);
			/* Update the child MBR stored in the current node
			 * because MBR of child has been enlarged.
			 */
			mbr_changed = evt_node_mbr_update(tcx, nd_cur, mbr,
							  trace->tr_at);
			if (!mbr_changed || level == 0)
				D_GOTO(out, 0);

			/* continue to merge MBR with upper level node */
			mbr = nd_cur;
			level--;
			continue;
		}

		if (!evt_node_is_full(tcx, nd_cur)) {
			rc = evt_node_insert(tcx, nd_cur, nm_save,
					     &entry, &changed, csum_bufp);
			if (rc != 0)
				D_GOTO(failed, rc);

			/* NB: mbr_changed could have been set while splitting
			 * the child node.
			 */
			mbr_changed |= changed;
			if (!mbr_changed || level == 0)
				D_GOTO(out, 0);

			/* continue to merge MBR with upper level node */
			mbr = nd_cur;
			level--;
			continue;
		}

		/* Try to split */

		V_TRACE(DB_TRACE, "Split node at level %d\n", level);

		leaf = evt_node_is_leaf(tcx, nd_cur);
		rc = evt_node_alloc(tcx, leaf ? EVT_NODE_LEAF : 0, &nm_new);
		if (rc != 0)
			D_GOTO(failed, rc);
		nd_new = evt_off2node(tcx, nm_new);

		rc = evt_node_split(tcx, leaf, nd_cur, nd_new);
		if (rc != 0) {
			V_TRACE(DB_TRACE, "Failed to split node: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(failed, rc);
		}

		/* choose a node for insert between the current node and the
		 * new created node.
		 */
		nd_tmp = evt_select_node(tcx, &entry.ei_rect, nd_cur, nd_new);
		rc = evt_node_insert(tcx, nd_tmp, nm_save, &entry, NULL,
				     csum_bufp);
		if (rc != 0)
			D_GOTO(failed, rc);

		/* Insert the new node to upper level node:
		 * - If the current node is not root, insert it to its parent
		 * - If the current node is root, create a new root
		 */
		nm_save = nm_new;
		evt_mbr_read(&entry.ei_rect, nd_new);
		if (level != 0) { /* not root */
			level--;
			/* After splitting, MBR of the current node has been
			 * changed (half of its entries are moved out, and
			 * probably added a new entry), so we need to update
			 * its MBR stored in its parent.
			 */
			nd_tmp = nd_cur;
			mbr_update = true;
			/* continue to insert the new node to its parent */
			continue;
		}

		V_TRACE(DB_TRACE, "Create a new root, depth=%d.\n",
			tcx->tc_root->tr_depth + 1);

		D_ASSERT(evt_node_is_root(tcx, nd_cur));
		evt_node_unset(tcx, nd_cur, EVT_NODE_ROOT);

		rc = evt_node_alloc(tcx, EVT_NODE_ROOT, &nm_new);
		if (rc != 0)
			D_GOTO(failed, rc);
		nd_new = evt_off2node(tcx, nm_new);

		rc = evt_node_insert(tcx, nd_new, nm_save, &entry, NULL,
				     csum_bufp);
		if (rc != 0)
			D_GOTO(failed, rc);

		evt_tcx_set_dep(tcx, tcx->tc_depth + 1);
		tcx->tc_trace->tr_node = nm_new;
		tcx->tc_trace->tr_at = 0;
		tcx->tc_trace->tr_tx_added = true;

		rc = evt_root_tx_add(tcx);
		if (rc != 0)
			D_GOTO(failed, rc);
		tcx->tc_root->tr_node = nm_new;
		tcx->tc_root->tr_depth++;

		/* continue the loop and insert the original root node into
		 * the new root node.
		 */
		evt_mbr_read(&entry.ei_rect, nd_cur);
		nm_save = nm_cur;
	}
 out:
	return 0;
 failed:
	D_ERROR("Failed to insert entry to level %d: "DF_RC"\n", level,
		DP_RC(rc));
	return rc;
}

/** Insert a single entry to evtree */
static int
evt_insert_entry(struct evt_context *tcx, const struct evt_entry_in *ent,
		 uint8_t **csum_bufp)
{
	umem_off_t		nd_off;
	int			level;
	int			i;

	V_TRACE(DB_TRACE, "Inserting rectangle "DF_RECT"\n",
		DP_RECT(&ent->ei_rect));

	evt_tcx_reset_trace(tcx);
	nd_off = tcx->tc_trace->tr_node; /* NB: trace points at root node */
	level = 0;

	while (1) {
		struct evt_node		*nd;
		struct evt_node		*nd_dst;
		struct evt_node		*nd_cur;
		umem_off_t		 nm_cur;
		umem_off_t		 nm_dst;
		int			 tr_at;

		nd = evt_off2node(tcx, nd_off);

		if (evt_node_is_leaf(tcx, nd)) {
			evt_tcx_set_trace(tcx, level, nd_off, 0, false);
			break;
		}

		tr_at = -1;
		nm_dst = UMOFF_NULL;
		nd_dst = NULL;

		for (i = 0; i < nd->tn_nr; i++) {
			nm_cur = evt_node_child_at(tcx, nd, i);
			nd_cur = evt_off2node(tcx, nm_cur);
			if (UMOFF_IS_NULL(nm_dst)) {
				nm_dst = nm_cur;
				nd_dst = nd_cur;
			} else {
				D_ASSERT(nd_dst != NULL);
				nd_dst = evt_select_node(tcx, &ent->ei_rect,
							 nd_dst, nd_cur);
				if (nd_dst == nd_cur)
					nm_dst = nm_cur;
			}

			/* check if the current child is the new destination */
			if (nm_dst == nm_cur)
				tr_at = i;
		}

		/* store the trace in case we need to bubble split */
		evt_tcx_set_trace(tcx, level, nd_off, tr_at, false);
		nd_off = nm_dst;
		level++;
	}
	D_ASSERT(level == tcx->tc_depth - 1);

	return evt_insert_or_split(tcx, ent, csum_bufp);
}

static int
evt_desc_copy(struct evt_context *tcx, const struct evt_entry_in *ent,
	      uint8_t **csum_bufp)
{
	struct evt_desc		*dst_desc;
	struct evt_trace	*trace;
	struct evt_node		*node;
	daos_size_t		 csum_buf_len = 0;
	daos_size_t		 size;
	int			 rc;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	node = evt_off2node(tcx, trace->tr_node);
	dst_desc = evt_node_desc_at(tcx, node, trace->tr_at);

	D_ASSERT(ent->ei_inob != 0);
	size = ent->ei_inob * evt_rect_width(&ent->ei_rect);

	rc = evt_desc_bio_free(tcx, dst_desc, size);
	if (rc != 0)
		return rc;

	if (ci_is_valid(&ent->ei_csum))
		csum_buf_len = ci_csums_len(ent->ei_csum);

	rc = umem_tx_add_ptr(evt_umm(tcx), dst_desc,
			     sizeof(*dst_desc) + csum_buf_len);
	if (rc != 0)
		return rc;

	dst_desc->dc_ex_addr = ent->ei_addr;
	dst_desc->dc_ver = ent->ei_ver;
	evt_desc_csum_fill(tcx, dst_desc, ent, csum_bufp);

	return 0;
}

/** For hole extents that are too large for a single entry, search the tree
 *  first and only insert holes where an extent is visible
 */
static int
evt_large_hole_insert(daos_handle_t toh, const struct evt_entry_in *entry)
{
	struct evt_entry	*ent;
	struct evt_entry_in	 hole;
	struct evt_filter	 filter = {0};
	EVT_ENT_ARRAY_SM_PTR(ent_array);
	int			 alt_rc = 0;
	int			 rc = 0;

	filter.fr_epr.epr_hi = entry->ei_bound;
	filter.fr_epoch = entry->ei_rect.rc_epc;
	filter.fr_ex = entry->ei_rect.rc_ex;

	evt_ent_array_init(ent_array, 0);
	rc = evt_find(toh, &filter, ent_array);
	if (rc != 0)
		goto done;

	evt_ent_array_for_each(ent, ent_array) {
		if (bio_addr_is_hole(&ent->en_addr))
			continue; /* Skip holes */
		/** Insert a hole to cover the record */
		hole = *entry;
		hole.ei_rect.rc_ex = ent->en_sel_ext;
		rc = evt_insert(toh, &hole, NULL);
		if (rc < 0)
			break;
		if (rc == 1) {
			alt_rc = 1;
			rc = 0;
		}
	}
done:
	evt_ent_array_fini(ent_array);

	return rc == 0 ? alt_rc : rc;
}

/**
 * Insert a versioned extent (rectangle) and its data offset into the tree.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_insert(daos_handle_t toh, const struct evt_entry_in *entry,
	   uint8_t **csum_bufp)
{
	struct evt_context		*tcx;
	struct evt_entry		*ent = NULL;
	struct evt_entry_in		 ent_cpy;
	EVT_ENT_ARRAY_SM_PTR(ent_array);
	const struct evt_entry_in	*entryp = entry;
	struct evt_filter		 filter;
	int				 rc;
	int				 alt_rc = 0;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (tcx->tc_inob && entry->ei_inob && tcx->tc_inob != entry->ei_inob) {
		D_ERROR("Variable record size not supported in evtree:"
			" %d != %d\n", entry->ei_inob, tcx->tc_inob);
		return -DER_INVAL;
	}

	D_ASSERT(evt_rect_width(&entry->ei_rect) != 0);
	D_ASSERT(entry->ei_inob != 0 || bio_addr_is_hole(&entry->ei_addr));
	D_ASSERT(bio_addr_is_hole(&entry->ei_addr) ||
		 entry->ei_addr.ba_off != 0);
	if (evt_rect_width(&entry->ei_rect) > MAX_RECT_WIDTH) {
		if (bio_addr_is_hole(&entry->ei_addr)) {
			/** csum_bufp is specific to aggregation case and we
			 * should never do this with aggregation.
			 */
			D_ASSERT(csum_bufp == NULL);
			return evt_large_hole_insert(toh, entry);
		}
		D_ERROR("Extent is too large\n");
		/** The update isn't a punch, just reject it as too large */
		return -DER_NO_PERM;
	}

	evt_ent_array_init(ent_array, 1);

	filter.fr_ex = entry->ei_rect.rc_ex;
	filter.fr_epr.epr_lo = entry->ei_rect.rc_epc;
	filter.fr_epr.epr_hi = entry->ei_bound;
	filter.fr_epoch = entry->ei_rect.rc_epc;
	filter.fr_punch_epc = 0;
	filter.fr_punch_minor_epc = 0;
	/* Phase-1: Check for overwrite and uncertainty */
	rc = evt_ent_array_fill(tcx, EVT_FIND_OVERWRITE, DAOS_INTENT_UPDATE,
				&filter, &entry->ei_rect, ent_array);
	alt_rc = rc;
	if (rc < 0)
		return rc;

	if (ent_array->ea_ent_nr == 1) {
		if (entry->ei_rect.rc_minor_epc == EVT_MINOR_EPC_MAX) {
			/** Special case.   This is an overlapping delete record
			 *  which can happen when there are minor epochs
			 *  involved.   Rather than rejecting, insert prefix
			 *  and/or suffix extents.
			 */
			ent = evt_ent_array_get(ent_array, 0);
			if (ent->en_ext.ex_lo <= entry->ei_rect.rc_ex.ex_lo &&
			    ent->en_ext.ex_hi >= entry->ei_rect.rc_ex.ex_hi) {
				/** Nothing to do, existing extent contains
				 *  the new one
				 */
				return 0;
			}
		}
	}

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	if (tcx->tc_depth == 0) { /* empty tree */
		rc = evt_root_activate(tcx, entry);
		if (rc != 0)
			goto out;
	} else if (tcx->tc_inob == 0 && entry->ei_inob != 0) {
		rc = evt_root_tx_add(tcx);
		if (rc != 0)
			goto out;
		tcx->tc_inob = tcx->tc_root->tr_inob = entry->ei_inob;
	}

	D_ASSERT(ent_array->ea_ent_nr <= 1);
	if (ent_array->ea_ent_nr == 1) {
		if (ent != NULL) {
			memcpy(&ent_cpy, entry, sizeof(*entry));
			entryp = &ent_cpy;
			/** We need to edit the existing extent */
			if (entry->ei_rect.rc_ex.ex_lo < ent->en_ext.ex_lo) {
				ent_cpy.ei_rect.rc_ex.ex_hi = ent->en_ext.ex_lo - 1;
				if (entry->ei_rect.rc_ex.ex_hi <= ent->en_ext.ex_hi)
					goto insert;
				/* There is also a suffix, so insert the prefix */
				rc = evt_insert_entry(tcx, entryp, csum_bufp);
				if (rc != 0)
					goto out;
			}

			D_ASSERT(entry->ei_rect.rc_ex.ex_hi > ent->en_ext.ex_hi);
			ent_cpy.ei_rect.rc_ex.ex_hi = entry->ei_rect.rc_ex.ex_hi;
			ent_cpy.ei_rect.rc_ex.ex_lo = ent->en_ext.ex_hi + 1;

			/* Now insert the suffix */
			goto insert;
		}
		/*
		 * NB: This is part of the current hack to keep "supporting"
		 * overwrite for same epoch, full overwrite.
		 * No copy for duplicate punch.
		 */
		if (entry->ei_inob > 0)
			rc = evt_desc_copy(tcx, entry, csum_bufp);
		goto out;
	}

insert:
	/* Phase-2: Inserting */
	rc = evt_insert_entry(tcx, entryp, csum_bufp);

	/* No need for evt_ent_array_fini as there will be no allocations
	 * with 1 entry in the list
	 */
out:
	rc = evt_tx_end(tcx, rc);

	return rc == 0 ? alt_rc : rc;
}

/** Fill the entry with the extent at the specified position of \a node */
void
evt_entry_fill(struct evt_context *tcx, struct evt_node *node, unsigned int at,
	       const struct evt_rect *rect_srch, uint32_t intent,
	       struct evt_entry *entry)
{
	struct evt_desc	   *desc;
	struct evt_rect     rect;
	daos_off_t	    offset;
	daos_size_t	    width;
	daos_size_t	    nr;

	evt_node_rect_read_at(tcx, node, at, &rect);
	desc = evt_node_desc_at(tcx, node, at);

	offset = 0;
	width = evt_rect_width(&rect);

	entry->en_visibility = 0; /* Unknown */

	if (rect_srch && rect_srch->rc_ex.ex_lo > rect.rc_ex.ex_lo) {
		offset = rect_srch->rc_ex.ex_lo - rect.rc_ex.ex_lo;
		D_ASSERTF(width > offset, DF_U64"/"DF_U64"\n", width, offset);
		width -= offset;
		entry->en_visibility = EVT_PARTIAL;
	}

	if (rect_srch && rect_srch->rc_ex.ex_hi < rect.rc_ex.ex_hi) {
		nr = rect.rc_ex.ex_hi - rect_srch->rc_ex.ex_hi;
		D_ASSERTF(width > nr, DF_U64"/"DF_U64"\n", width, nr);
		width -= nr;
		entry->en_visibility = EVT_PARTIAL;
	}

	entry->en_epoch = rect.rc_epc;
	entry->en_minor_epc = rect.rc_minor_epc;
	entry->en_ext = entry->en_sel_ext = rect.rc_ex;
	entry->en_sel_ext.ex_lo += offset;
	entry->en_sel_ext.ex_hi = entry->en_sel_ext.ex_lo + width - 1;

	entry->en_addr = desc->dc_ex_addr;
	entry->en_ver = desc->dc_ver;
	evt_entry_csum_fill(tcx, desc, entry);
	entry->en_avail_rc = evt_desc_log_status(tcx, entry->en_epoch, desc,
						 intent);

	if (offset != 0) {
		/* Adjust cached pointer since we're only referencing a
		 * part of the extent
		 */
		evt_ent_addr_update(tcx, entry, offset);
	}
}

struct evt_data_loss_item {
	d_list_t		edli_link;
	struct evt_rect		edli_rect;
};

static struct evt_data_loss_item *
evt_data_loss_add(d_list_t *head, struct evt_rect *rect)
{
	struct evt_data_loss_item	*edli;

	D_ALLOC_PTR(edli);
	if (edli != NULL) {
		edli->edli_rect = *rect;
		d_list_add(&edli->edli_link, head);
	}

	return edli;
}

static int
evt_data_loss_check(d_list_t *head, struct evt_entry_array *ent_array)
{
	struct evt_data_loss_item	*edli;
	struct evt_data_loss_item	*tmp;
	struct evt_list_entry		*entry;
	int				 i;

	d_list_for_each_entry_safe(edli, tmp, head, edli_link) {
		bool	visible = true;

		d_list_del(&edli->edli_link);

		for (i = 0; i < ent_array->ea_ent_nr; i++) {
			entry = &ent_array->ea_ents[i];

			/* edli is newer */
			if (edli->edli_rect.rc_epc > entry->le_ent.en_epoch)
				continue;

			/* non-overlap */
			if (edli->edli_rect.rc_ex.ex_lo >
			    entry->le_ent.en_ext.ex_hi ||
			    edli->edli_rect.rc_ex.ex_hi <
			    entry->le_ent.en_ext.ex_lo)
				continue;

			/* edli is totally covered by entry */
			if (edli->edli_rect.rc_ex.ex_lo >=
			    entry->le_ent.en_ext.ex_lo &&
			    edli->edli_rect.rc_ex.ex_hi <=
			    entry->le_ent.en_ext.ex_lo) {
				visible = false;
				break;
			}

			/* edli totally covers entry */
			if (edli->edli_rect.rc_ex.ex_lo <=
			    entry->le_ent.en_ext.ex_lo &&
			    edli->edli_rect.rc_ex.ex_hi >=
			    entry->le_ent.en_ext.ex_lo) {
				/* cur low part */
				if (edli->edli_rect.rc_ex.ex_lo ==
				    entry->le_ent.en_ext.ex_lo) {
					edli->edli_rect.rc_ex.ex_lo =
						entry->le_ent.en_ext.ex_hi + 1;
					continue;
				}

				/* cut high part */
				if (edli->edli_rect.rc_ex.ex_hi ==
				    entry->le_ent.en_ext.ex_hi) {
					edli->edli_rect.rc_ex.ex_hi =
						entry->le_ent.en_ext.ex_lo - 1;
					continue;
				}

				tmp = evt_data_loss_add(head, &edli->edli_rect);
				if (tmp == NULL) {
					D_FREE(edli);
					return -DER_NOMEM;
				}

				/* split edli */
				tmp->edli_rect.rc_ex.ex_lo =
					entry->le_ent.en_ext.ex_hi + 1;
				tmp->edli_rect.rc_ex.ex_hi =
					edli->edli_rect.rc_ex.ex_hi;
				edli->edli_rect.rc_ex.ex_hi =
					entry->le_ent.en_ext.ex_lo - 1;
				continue;
			}

			/* edli low part overlap with entry */
			if (edli->edli_rect.rc_ex.ex_lo <=
			    entry->le_ent.en_ext.ex_hi &&
			    edli->edli_rect.rc_ex.ex_hi >
			    entry->le_ent.en_ext.ex_hi) {
				edli->edli_rect.rc_ex.ex_lo =
					entry->le_ent.en_ext.ex_hi + 1;
				continue;
			}

			/* edli high part overlap with entry */
			if (edli->edli_rect.rc_ex.ex_hi >=
			    entry->le_ent.en_ext.ex_lo &&
			    edli->edli_rect.rc_ex.ex_lo <
			    entry->le_ent.en_ext.ex_lo) {
				edli->edli_rect.rc_ex.ex_hi =
					entry->le_ent.en_ext.ex_lo - 1;
				continue;
			}
		}

		D_FREE(edli);

		if (visible)
			return -DER_DATA_LOSS;
	}

	return 0;
}

static inline bool
agg_check(const struct evt_extent *inserted, const struct evt_extent *intree)
{
	if (inserted->ex_hi + 1 >= intree->ex_lo &&
	    intree->ex_hi + 1 >= inserted->ex_lo) {
		/** Extent overlaps or is adjacent, so assume aggregation is needed. */
		return true;
	} else if ((inserted->ex_hi & DAOS_EC_PARITY_BIT) !=
		   (intree->ex_hi & DAOS_EC_PARITY_BIT)) {
		/** EC aggregation needs to run if there is parity and we are doing a
		 *  partial stripe write or vice versa.   Since we don't know the
		 *  cell or stripe size, we can only approximate this by just flagging
		 *  any parity mismatch between what we are writing and what is in
		 *  the tree.
		 */
		return true;
	}

	return false;
}

/**
 * See the description in evt_priv.h
 */
int
evt_ent_array_fill(struct evt_context *tcx, enum evt_find_opc find_opc,
		   uint32_t intent, const struct evt_filter *filter,
		   const struct evt_rect *rect,
		   struct evt_entry_array *ent_array)
{
	struct evt_data_loss_item	*edli;
	d_list_t			 data_loss_list;
	umem_off_t			 nd_off;
	int				 level;
	int				 at;
	int				 i;
	int				 rc = 0;
	bool				 has_agg = false;

	V_TRACE(DB_TRACE, "Searching rectangle "DF_RECT" opc=%d\n",
		DP_RECT(rect), find_opc);
	if (tcx->tc_root->tr_depth == 0)
		return 0; /* empty tree */

	/** On re-probe, the tree order may have changed */
	if (tcx->tc_root->tr_order != tcx->tc_order)
		tcx->tc_order = tcx->tc_root->tr_order;

	D_INIT_LIST_HEAD(&data_loss_list);

	evt_tcx_reset_trace(tcx);
	ent_array->ea_inob = tcx->tc_inob;

	level = at = 0;
	nd_off = tcx->tc_root->tr_node;
	while (1) {
		struct evt_node		*node;
		bool			 leaf;

		node = evt_off2node(tcx, nd_off);
		leaf = evt_node_is_leaf(tcx, node);

		D_ASSERT(!leaf || at == 0);
		V_TRACE(DB_TRACE,
			"Checking mbr="DF_MBR"("DF_X64"), l=%d, a=%d, f=%d\n",
			DP_MBR(node), nd_off, level, at, leaf);

		for (i = at; i < node->tn_nr; i++) {
			struct evt_entry	*ent;
			struct evt_desc		*desc;
			struct evt_rect		 rtmp;
			int			 time_overlap;
			int			 range_overlap;

			evt_node_rect_read_at(tcx, node, i, &rtmp);

			if (evt_filter_rect(filter, &rtmp, leaf)) {
				V_TRACE(DB_TRACE, "Filtered "DF_RECT" filter=("
					DF_FILTER")\n", DP_RECT(&rtmp), DP_FILTER(filter));
				if (find_opc == EVT_FIND_OVERWRITE && !has_agg) {
					if (agg_check(&filter->fr_ex, &rtmp.rc_ex))
						has_agg = true;
				}
				continue; /* Doesn't match the filter */
			}

			if (find_opc == EVT_FIND_OVERWRITE)
				has_agg = true;

			evt_rect_overlap(&rtmp, rect, &range_overlap,
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

			if (evt_epoch_uncertain(filter, &rtmp, leaf)) {
				V_TRACE(DB_TRACE, "Epoch uncertainty found for "
					DF_RECT" filter="DF_FILTER"\n",
					DP_RECT(&rtmp), DP_FILTER(filter));
				D_GOTO(out, rc = -DER_TX_RESTART);
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
				V_TRACE(DB_TRACE, "Enter the next level\n");
				break;
			}
			V_TRACE(DB_TRACE, "Found overlapped leaf rect: "DF_RECT
				"\n", DP_RECT(&rtmp));

			desc = evt_node_desc_at(tcx, node, i);
			rc = evt_desc_log_status(tcx, rtmp.rc_epc, desc,
						 intent);
			/* Skip the unavailable record. */
			if (rc == ALB_UNAVAILABLE) {
				continue;
			}

			/* early check */
			switch (find_opc) {
			default:
				D_ASSERTF(0, "%d\n", find_opc);
			case EVT_FIND_OVERWRITE:
				if (time_overlap != RT_OVERLAP_SAME)
					continue; /* not same epoch, skip */

				/* If the availability is unknown, go out. */
				if (rc < 0)
					D_GOTO(out, rc);

				D_ASSERTF(rect->rc_minor_epc != EVT_MINOR_EPC_MAX,
					  "Should never have overlap with removals: " DF_RECT
					  " overlaps with " DF_RECT "\n",
					  DP_RECT(&rtmp), DP_RECT(rect));

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
					D_ERROR("Same epoch partial "
						"overwrite not supported:"
						DF_RECT" overlaps with "DF_RECT
						"\n", DP_RECT(rect),
						DP_RECT(&rtmp));
					rc = -DER_VOS_PARTIAL_UPDATE;
					goto out;
				}
				break; /* we can update the record in place */
			case EVT_FIND_SAME:
				if (range_overlap != RT_OVERLAP_SAME)
					continue;
				if (time_overlap != RT_OVERLAP_SAME)
					continue;

				/* If the availability is unknown, go out. */
				if (rc < 0)
					D_GOTO(out, rc);

				break;
			case EVT_FIND_FIRST:
				/* If the availability is unknown, go out. */
				if (rc < 0)
					D_GOTO(out, rc);
			case EVT_FIND_ALL:
				if (rc == -DER_DATA_LOSS) {
					if (evt_data_loss_add(&data_loss_list,
							      &rtmp) == NULL)
						D_GOTO(out, rc = -DER_NOMEM);
					continue;
				}

				/* Stop when read hit -DER_INPROGRESS. */
				if (rc == -DER_INPROGRESS &&
				    intent == DAOS_INTENT_DEFAULT)
					goto out;

				break;
			}

			rc = ent_array_alloc(tcx, ent_array, &ent, false);
			if (rc != 0) {
				D_ASSERT(rc != -DER_AGAIN);
				goto out;
			}

			evt_entry_fill(tcx, node, i, rect, intent, ent);
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
				evt_tcx_set_trace(tcx, level, nd_off, i, false);
				D_GOTO(out, rc = 0);

			case EVT_FIND_ALL:
				break;
			}
		}

		if (i < node->tn_nr) {
			/* overlapped with a non-leaf node, dive into it. */
			evt_tcx_set_trace(tcx, level, nd_off, i, false);
			nd_off = evt_node_child_at(tcx, node, i);
			at = 0;
			level++;

		} else {
			struct evt_trace *trace;

			if (level == 0) { /* done with the root */
				V_TRACE(DB_TRACE, "Found total %d rects\n",
					ent_array ? ent_array->ea_ent_nr : 0);
				return has_agg ? 1 : 0; /* succeed and return */
			}

			level--;
			trace = evt_tcx_trace(tcx, level);
			nd_off = trace->tr_node;
			at = trace->tr_at + 1;
			D_ASSERT(at <= tcx->tc_order);
		}
	}
out:
	if (rc == 0 && !d_list_empty(&data_loss_list))
		rc = evt_data_loss_check(&data_loss_list, ent_array);

	if (rc != 0)
		ent_array->ea_ent_nr = 0;

	while ((edli = d_list_pop_entry(&data_loss_list,
					struct evt_data_loss_item,
					edli_link)) != NULL)
		D_FREE(edli);

	if (rc == 0 && has_agg)
		rc = 1;
	return rc;
}

struct evt_max_rect {
	struct evt_rect		mr_rect;
	bool			mr_valid;
	bool			mr_punched;
};

/**
 * Find all versioned extents intercepting with the input rectangle \a rect
 * and return their data pointers.
 *
 * Please check API comment in evtree.h for the details.
 */
int
evt_find(daos_handle_t toh, const struct evt_filter *filter,
	 struct evt_entry_array *ent_array)
{
	struct evt_context	*tcx;
	struct evt_rect		 rect;
	int			 rc;

	D_ASSERT(filter != NULL);
	D_ASSERT(filter->fr_epoch != 0);

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rect.rc_ex = filter->fr_ex;
	rect.rc_epc = filter->fr_epoch;
	rect.rc_minor_epc = EVT_MINOR_EPC_MAX;

	rc = evt_ent_array_fill(tcx, EVT_FIND_ALL, DAOS_INTENT_DEFAULT,
				filter, &rect, ent_array);

	if (rc == 0)
		rc = evt_ent_array_sort(tcx, ent_array, filter, EVT_ITER_VISIBLE);

	return rc;
}

/** move the probing trace forward */
bool
evt_move_trace(struct evt_context *tcx)
{
	struct evt_trace	*trace;
	struct evt_node		*nd;
	umem_off_t		 nd_off;

	if (evt_root_empty(tcx))
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	while (1) {
		nd_off = trace->tr_node;
		nd = evt_off2node(tcx, nd_off);

		/* We reached the end of this node */
		if (trace->tr_at == (nd->tn_nr - 1)) {
			if (evt_node_is_root(tcx, nd)) {
				D_ASSERT(trace == tcx->tc_trace);
				V_TRACE(DB_TRACE, "End\n");
				return false;
			}
			/* check its parent */
			trace--;
			continue;
		} /* else: not yet */

		trace->tr_at++;
		break;
	}

	/* move to the first/last entry in the subtree */
	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		umem_off_t	tmp;

		tmp = evt_node_child_at(tcx, nd, trace->tr_at);
		nd = evt_off2node(tcx, tmp);
		D_ASSERTF(nd->tn_nr != 0, "%d\n", nd->tn_nr);

		trace++;
		trace->tr_at = 0;
		trace->tr_node = tmp;
	}

	return true;
}

/**
 * Open a inplace tree by root address @root.
 * Please check API comment in evtree.h for the details.
 */
int
evt_open(struct evt_root *root, struct umem_attr *uma,
	 struct evt_desc_cbs *cbs, daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (root->tr_order == 0) {
		V_TRACE(DB_TRACE, "Nonexistent tree.\n");
		return -DER_NONEXIST;
	}

	rc = evt_tcx_create(root, -1, -1, uma, cbs, &tcx);
	if (rc != 0)
		return rc;

	*toh = evt_tcx2hdl(tcx);
	evt_tcx_decref(tcx); /* -1 for tcx_create */
	return 0;
}

int
evt_has_data(struct evt_root *root, struct umem_attr *uma)
{
	struct evt_entry	*ent;
	struct evt_context	*tcx;
	struct evt_rect		 rect;
	int			 rc;

	if (evt_is_empty(root))
		return 0;

	rc = evt_tcx_create(root, -1, -1, uma, NULL, &tcx);
	if (rc != 0)
		return rc;

	rect.rc_ex.ex_lo = 0;
	rect.rc_ex.ex_hi = -1ULL;
	rect.rc_epc = DAOS_EPOCH_MAX;
	rect.rc_minor_epc = EVT_MINOR_EPC_MAX;

	rc = evt_ent_array_fill(tcx, EVT_FIND_ALL, 0 /* DTX check disabled */, NULL, &rect,
				tcx->tc_iter.it_entries);
	if (rc != 0)
		goto out;

	rc = 0; /* Assume there is no data */
	evt_ent_array_for_each(ent, tcx->tc_iter.it_entries) {
		if (ent->en_minor_epc != EVT_MINOR_EPC_MAX) {
			D_INFO("Found orphaned extent "DF_ENT"\n", DP_ENT(ent));
			rc = 1;
			break;
		}
		D_DEBUG(DB_IO, "Ignoring "DF_ENT"\n", DP_ENT(ent));
	}
out:
	evt_tcx_decref(tcx); /* -1 for tcx_create */
	return rc;
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

#define EVT_AGG_MASK (VOS_TF_AGG_HLC | VOS_AGG_TIME_MASK | VOS_TF_AGG_OPT)
/**
 * Create a new tree inplace of \a root, return the open handle.
 * Please check API comment in evtree.h for the details.
 */
int
evt_create(struct evt_root *root, uint64_t feats, unsigned int order,
	   struct umem_attr *uma, struct evt_desc_cbs *cbs, daos_handle_t *toh)
{
	struct evt_context *tcx;
	int		    rc;

	if (!(feats & (EVT_AGG_MASK | EVT_FEATS_SUPPORTED))) {
		D_ERROR("Unknown feature bits "DF_X64"\n", feats);
		return -DER_INVAL;
	}

	if (order < EVT_ORDER_MIN || order > EVT_ORDER_MAX) {
		D_ERROR("Invalid tree order %d\n", order);
		return -DER_INVAL;
	}

	rc = evt_tcx_create(root, feats, order, uma, cbs, &tcx);
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
	bool		    destroyed;
	int		    rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	D_ASSERT(!tcx->tc_creds_on);
	rc = evt_root_destroy(tcx, &destroyed);
	D_ASSERT(rc || destroyed);

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
evt_node_debug(struct evt_context *tcx, umem_off_t nd_off,
	       int cur_level, int debug_level)
{
	struct evt_node *nd;
	int		 i;
	bool		 leaf;

	nd = evt_off2node(tcx, nd_off);
	leaf = evt_node_is_leaf(tcx, nd);

	/* NB: debug_level < 0 means output debug info for all levels,
	 * otherwise only output debug info for the specified tree level.
	 */
	if (leaf || cur_level == debug_level || debug_level < 0) {
		struct evt_rect rect;

		D_PRINT("%*snode="DF_X64", lvl=%d, mbr="DF_MBR
			", rect_nr=%d\n", cur_level * EVT_DEBUG_INDENT, "",
			nd_off, cur_level, DP_MBR(nd), nd->tn_nr);

		if (leaf && debug_level == EVT_DEBUG_LEAF) {
			for (i = 0; i < nd->tn_nr; i++) {
				evt_node_rect_read_at(tcx, nd, i, &rect);

				D_PRINT("%*s    rect[%d] = "DF_RECT"\n",
					cur_level * EVT_DEBUG_INDENT, "", i,
					DP_RECT(&rect));
			}
		}

		if (leaf || cur_level == debug_level)
			return;
	}

	for (i = 0; i < nd->tn_nr; i++) {
		umem_off_t	child_off;

		child_off = evt_node_child_at(tcx, nd, i);
		evt_node_debug(tcx, child_off, cur_level + 1, debug_level);
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

	D_PRINT("Tree depth=%d, order=%d, max=%d, feats=" DF_X64 "\n", tcx->tc_depth, tcx->tc_order,
		tcx->tc_feats & EVT_FEAT_DYNAMIC_ROOT ? tcx->tc_max_order : tcx->tc_order,
		tcx->tc_feats);

	if (tcx->tc_root->tr_node != 0)
		evt_node_debug(tcx, tcx->tc_root->tr_node, 0, debug_level);

	return 0;
}

/**
 * Tree policies
 *
 * Only support SSOF for now (see below).
 */

/** Common routines */
typedef int (cmp_rect_cb)(struct evt_context *tcx,
			  const struct evt_node *nd,
			  const struct evt_rect *rt1,
			  const struct evt_rect *rt2);
static int
evt_common_insert(struct evt_context *tcx, struct evt_node *nd,
		  umem_off_t in_off, const struct evt_entry_in *ent,
		  bool *changed, cmp_rect_cb cb, uint8_t **csum_bufp)
{
	struct evt_node_entry	*ne = NULL;
	struct evt_desc		*desc = NULL;
	int			 i;
	int			 rc;
	bool			 leaf;
	bool			 reuse = false;

	D_ASSERT(!evt_node_is_full(tcx, nd));

	leaf = evt_node_is_leaf(tcx, nd);
	if (nd->tn_nr == 0) {
		evt_mbr_write(nd, &ent->ei_rect);
		*changed = true;
	} else {
		struct evt_rect	rtmp;

		evt_mbr_read(&rtmp, nd);
		*changed = evt_rect_merge(&rtmp, &ent->ei_rect);
		if (*changed)
			evt_mbr_write(nd, &rtmp);
	}

	/* NB: can use binary search to optimize */
	for (i = 0; i < nd->tn_nr; i++) {
		struct evt_rect	rtmp;
		int		nr;

		evt_node_rect_read_at(tcx, nd, i, &rtmp);

		rc = cb(tcx, nd, &rtmp, &ent->ei_rect);
		if (rc < 0)
			continue;

		if (!leaf) {
			nr = nd->tn_nr - i;
			memmove(&nd->tn_child[i + 1], &nd->tn_child[i],
				nr * sizeof(nd->tn_child[0]));
			break;
		}

		ne = evt_node_entry_at(tcx, nd, i);
		desc = evt_off2desc(tcx, ne->ne_child);
		rc = evt_desc_log_status(tcx, ne->ne_rect.rd_epc, desc,
					 DAOS_INTENT_CHECK);
		if (rc != ALB_UNAVAILABLE) {
			nr = nd->tn_nr - i;
			memmove(ne + 1, ne, nr * sizeof(*ne));
		} else {
			umem_off_t	off = ne->ne_child;

			/* We do not know whether the former @desc has checksum
			 * buffer or not, and do not know whether such buffer
			 * is large enough or not even if it had. So we have to
			 * free the former @desc and re-allocate it properly.
			 */
			rc = evt_node_entry_free(tcx, ne);
			if (rc != 0)
				return rc;

			reuse = true;
			D_DEBUG(DB_TRACE, "reuse slot at %d, nr %d, "
				"off "UMOFF_PF" (1)\n",
				i, nd->tn_nr, UMOFF_P(off));
		}

		break;
	}

	if (i == nd->tn_nr) { /* attach at the end */
		/* Check whether the previous one is an aborted one. */
		if (i != 0 && leaf) {
			ne = evt_node_entry_at(tcx, nd, i - 1);
			desc = evt_off2desc(tcx, ne->ne_child);
			rc = evt_desc_log_status(tcx, ne->ne_rect.rd_epc,
						 desc, DAOS_INTENT_CHECK);
			if (rc == ALB_UNAVAILABLE) {
				umem_off_t	off = ne->ne_child;

				rc = evt_node_entry_free(tcx, ne);
				if (rc != 0)
					return rc;

				reuse = true;
				D_DEBUG(DB_TRACE, "reuse slot at %d, nr %d, "
					"off "UMOFF_PF" (2)\n",
					i, nd->tn_nr, UMOFF_P(off));
				i = nd->tn_nr - 1;
			}
		}
	}

	if (leaf) {
		umem_off_t desc_off;
		uint32_t   csum_buf_size = 0;

		if (ci_is_valid(&ent->ei_csum))
			csum_buf_size = ci_csums_len(ent->ei_csum);
		size_t     desc_size = sizeof(struct evt_desc) + csum_buf_size;
		ne = evt_node_entry_at(tcx, nd, i);

		evt_rect_write(&ne->ne_rect, &ent->ei_rect);

		if (csum_buf_size > 0) {
			D_DEBUG(DB_TRACE, "Allocating an extra %d bytes "
						"for checksum", csum_buf_size);
		}
		desc_off = umem_zalloc(evt_umm(tcx), desc_size);
		if (UMOFF_IS_NULL(desc_off))
			return -DER_NOSPACE;

		ne->ne_child = desc_off;
		desc = evt_off2ptr(tcx, desc_off);
		rc = evt_desc_log_add(tcx, desc);
		if (rc != 0)
			/* It is unnecessary to free the PMEM that will be
			 * dropped automatically when the PMDK transaction
			 * is aborted.
			 */
			return rc;

		desc->dc_magic = EVT_DESC_MAGIC;
		desc->dc_ex_addr = ent->ei_addr;
		evt_desc_csum_fill(tcx, desc, ent, csum_bufp);
		desc->dc_ver = ent->ei_ver;
	} else {
		nd->tn_child[i] = in_off;
	}

	if (!reuse)
		nd->tn_nr++;

	return 0;
}

static int
evt_common_rect_weight(struct evt_context *tcx, const struct evt_rect *rect,
		       struct evt_weight *weight)
{
	weight->wt_major = rect->rc_ex.ex_hi - rect->rc_ex.ex_lo;
	weight->wt_minor = 0; /* Disable minor weight in favor of distance */

	return 0;
}

void
evt_split_common(struct evt_context *tcx, bool leaf, struct evt_node *nd_src,
		 struct evt_node *nd_dst, int idx)
{
	void	*entry_src;
	void	*entry_dst;
	size_t	 entry_size;

	if (leaf) {
		entry_src = evt_node_entry_at(tcx, nd_src, idx);
		entry_dst = evt_node_entry_at(tcx, nd_dst, 0);
		entry_size = sizeof(struct evt_node_entry);
	} else {
		entry_src = &nd_src->tn_child[idx];
		entry_dst = &nd_dst->tn_child[0];
		entry_size = sizeof(nd_dst->tn_child[0]);
	}

	memcpy(entry_dst, entry_src, entry_size * (nd_src->tn_nr - idx));
	nd_dst->tn_nr = nd_src->tn_nr - idx;
	nd_src->tn_nr = idx;
}

static int
evt_even_split(struct evt_context *tcx, bool leaf, struct evt_node *nd_src,
	       struct evt_node *nd_dst)
{
	int		    nr;

	D_ASSERT(nd_src->tn_nr == tcx->tc_order);
	nr = nd_src->tn_nr / 2;
	/* give one more entry to the left (original) node if tree order is
	 * odd, because "append" could be the most common use-case at here,
	 * which means new entres will never be inserted into the original
	 * node. So we want to utilize the original as much as possible.
	 */
	nr += (nd_src->tn_nr % 2 != 0);

	evt_split_common(tcx, leaf, nd_src, nd_dst, nr);
	return 0;
}

static int
evt_common_adjust(struct evt_context *tcx, struct evt_node *nd,
		  int at, cmp_rect_cb cb)
{
	uint64_t		*dst_entry;
	uint64_t		*src_entry;
	uint64_t		 cached_entry;
	struct evt_rect		 rtmp, rect;
	int			 count;
	int			 i;
	int			 offset;

	D_ASSERT(!evt_node_is_leaf(tcx, nd));

	evt_node_rect_read_at(tcx, nd, at, &rect);

	/* Check if we need to move the entry left */
	for (i = at - 1; i >= 0; i--) {
		evt_node_rect_read_at(tcx, nd, i, &rtmp);
		if (cb(tcx, nd, &rtmp, &rect) <= 0)
			break;
	}

	i++;
	if (i != at) {
		/* The entry needs to move left */
		dst_entry = &nd->tn_child[i + 1];
		src_entry = &nd->tn_child[i];
		cached_entry = nd->tn_child[at];

		count = at - i;
		offset = -count;
		goto move;
	}

	/* Ok, now check if we need to move the entry right */
	for (i = at + 1; i < nd->tn_nr; i++) {
		evt_node_rect_read_at(tcx, nd, i, &rtmp);
		if (cb(tcx, nd, &rtmp, &rect) >= 0)
			break;
	}

	i--;
	if (i != at) {
		/* the entry needs to move right */
		dst_entry = &nd->tn_child[at];
		src_entry = &nd->tn_child[at + 1];
		cached_entry = nd->tn_child[at];
		count = i - at;
		offset = count;
		goto move;
	}

	return 0;
move:
	/* Execute the move */
	memmove(dst_entry, src_entry, sizeof(*dst_entry) * count);
	nd->tn_child[i] = cached_entry;

	return offset;
}

/**
 * Sorted by Start Offset (SSOF)
 *
 * Extents are sorted by start offset first, then high to low epoch, then end
 * offset
 */

/** Rectangle comparison for sorting */
static int
evt_soff_cmp_rect(struct evt_context *tcx, const struct evt_node *nd, const struct evt_rect *rt1,
		  const struct evt_rect *rt2)
{
	return evt_rect_cmp(rt1, rt2);
}

static int
evt_soff_insert(struct evt_context *tcx, struct evt_node *nd, umem_off_t in_off,
		const struct evt_entry_in *ent, bool *changed, uint8_t **csum_bufp)
{
	return evt_common_insert(tcx, nd, in_off, ent, changed, evt_soff_cmp_rect, csum_bufp);
}

static int
evt_soff_adjust(struct evt_context *tcx, struct evt_node *nd, int at)
{
	return evt_common_adjust(tcx, nd, at, evt_soff_cmp_rect);
}

static struct evt_policy_ops evt_soff_pol_ops = {
    .po_insert      = evt_soff_insert,
    .po_adjust      = evt_soff_adjust,
    .po_split       = evt_even_split,
    .po_rect_weight = evt_common_rect_weight,
};

/**
 * Sorted by distances to sides of bounding box
 */

/** Rectangle comparison for sorting */
static int64_t
evt_mbr_dist(const struct evt_rect *mbr, const struct evt_rect *rect)
{
	int64_t ldist = rect->rc_ex.ex_lo - mbr->rc_ex.ex_lo;
	int64_t rdist = mbr->rc_ex.ex_hi - rect->rc_ex.ex_hi;

	return ldist - rdist;
}

static int
evt_sdist_cmp_rect(struct evt_context *tcx, const struct evt_node *nd,
		   const struct evt_rect *rt1, const struct evt_rect *rt2)
{
	struct evt_rect	mtmp;
	int64_t		dist1, dist2;

	evt_mbr_read(&mtmp, nd);

	dist1 = evt_mbr_dist(&mtmp, rt1);
	dist2 = evt_mbr_dist(&mtmp, rt2);

	if (dist1 < dist2)
		return -1;
	if (dist1 > dist2)
		return 1;

	/* All else being equal, revert to soff */
	return evt_rect_cmp(rt1, rt2);
}

static int
evt_sdist_split(struct evt_context *tcx, bool leaf, struct evt_node *nd_src,
		struct evt_node *nd_dst)
{
	struct evt_rect		 mbr;
	struct evt_rect		 rtmp;
	int			 nr;
	int			 delta;
	int			 boundary;
	bool			 cond;
	int64_t			 dist;

	if (unlikely(tcx->tc_depth > 6))
		return evt_even_split(tcx, leaf, nd_src, nd_dst);

	evt_mbr_read(&mbr, nd_src);

	D_ASSERT(nd_src->tn_nr == tcx->tc_order);
	nr = nd_src->tn_nr / 2;

	nr += nd_src->tn_nr % 2;

	evt_node_rect_read_at(tcx, nd_src, nr, &rtmp);
	dist = evt_mbr_dist(&mbr, &rtmp);

	if (dist == 0) /* special case if middle node is equal distance */
		goto done;

	cond = dist > 0;
	delta = cond ? -1 : 1;
	boundary = cond ? 1 : nd_src->tn_nr - 1;

	do {
		nr += delta;
		if (nr == boundary)
			break;
		evt_node_rect_read_at(tcx, nd_src, nr, &rtmp);
		dist = evt_mbr_dist(&mbr, &rtmp);
	} while ((dist > 0) == cond);

done:
	evt_split_common(tcx, leaf, nd_src, nd_dst, nr);
	return 0;
}

static int
evt_sdist_insert(struct evt_context *tcx, struct evt_node *nd,
		umem_off_t in_off, const struct evt_entry_in *ent,
		bool *changed, uint8_t **csum_bufp)
{
	return evt_common_insert(tcx, nd, in_off, ent, changed,
				 evt_sdist_cmp_rect, csum_bufp);
}

static int
evt_sdist_adjust(struct evt_context *tcx, struct evt_node *nd, int at)
{
	return evt_common_adjust(tcx, nd, at, evt_sdist_cmp_rect);
}

static struct evt_policy_ops evt_sdist_pol_ops = {
	.po_insert		= evt_sdist_insert,
	.po_adjust		= evt_sdist_adjust,
	.po_split		= evt_sdist_split,
	.po_rect_weight		= evt_common_rect_weight,
};

static struct evt_policy_ops evt_sdist_even_pol_ops = {
	.po_insert		= evt_sdist_insert,
	.po_adjust		= evt_sdist_adjust,
	.po_split		= evt_even_split,
	.po_rect_weight		= evt_common_rect_weight,
};

/** After the current cursor is deleted, the trace
 *  needs to be fixed between the changed level
 *  and the leaf.   This function sets it to the
 *  next entry in the tree, the one that was after
 *  the entry that was deleted.  Note that it may
 *  have been adjusted to be before the deleted
 *  entry.  In such a case, some entries may be
 *  visited more than one.
 */
static int
evt_tcx_fix_trace(struct evt_context *tcx, int level)
{
	struct evt_trace	*trace;
	struct evt_node		*pn;
	struct evt_node		*nd;
	int			 index;

	/* Go up if we have no more entries at this level. */
	trace = &tcx->tc_trace[level];
	for (;;) {
		pn = evt_off2node(tcx, trace->tr_node);
		if (trace->tr_at < pn->tn_nr)
			break;
		if (level == 0) /* No more entries */
			return -DER_NONEXIST;
		level--;
		trace = &tcx->tc_trace[level];
		trace->tr_at++;
	}

	if (level == tcx->tc_depth - 1)
		return 0;

	/* The trace will already be correct if no node is deleted or we haven't
	 * reached the end of the current node.   It will be such at the current
	 * level.   So this just resets it to the left most child of the tree
	 * below the current level.
	 */
	for (index = level + 1; index < tcx->tc_depth; index++) {
		trace = &tcx->tc_trace[index - 1];
		nd = evt_off2node(tcx, trace->tr_node);
		evt_tcx_set_trace(tcx, index, nd->tn_child[trace->tr_at], 0,
				  false);
	}

	return 0;
}

/* Delete the node pointed to by current trace */
int
evt_node_delete(struct evt_context *tcx)
{
	struct evt_trace	*trace;
	struct evt_node		*node;
	struct evt_node_entry	*ne = NULL;
	void			*data;
	umem_off_t		*child_offp;
	umem_off_t		 child_off;
	size_t			 child_size;
	umem_off_t		 nm_cur;
	umem_off_t		 old_cur = UMOFF_NULL;
	bool			 leaf;
	int			 level	= tcx->tc_depth - 1;
	int			 rc;
	int			 changed_level;

	/* We take a simple approach here which may be refined later.
	 * We simply remove the record, and if it's the last record, we
	 * bubble up removing any nodes that only have one record.
	 * Then we check the mbr at each level and make appropriate
	 * adjustments.
	 */
	while (1) {
		int	count;

		trace = &tcx->tc_trace[level];
		nm_cur = trace->tr_node;
		node = evt_off2node(tcx, nm_cur);
		leaf = evt_node_is_leaf(tcx, node);

		if (leaf) {
			ne = evt_node_entry_at(tcx, node, trace->tr_at);
			data = ne;
			child_off = ne->ne_child;
			child_offp = &ne->ne_child;
			child_size = sizeof(*ne);
		} else {
			child_offp = &node->tn_child[trace->tr_at];
			data = child_offp;
			child_off = *child_offp;
			child_size = sizeof(child_off);
		}
		if (!UMOFF_IS_NULL(old_cur))
			D_ASSERT(old_cur == child_off);
		if (leaf) {
			/* Free the evt_desc */
			rc = evt_node_entry_free(tcx, ne);
			if (rc != 0)
				return rc;
		}

		if (node->tn_nr == 1) {
			/* this node can be removed so bubble up */
			if (level == 0)
				return evt_root_deactivate(tcx);

			old_cur = nm_cur;
			rc = umem_free(evt_umm(tcx), nm_cur);
			if (rc != 0)
				return rc;
			level--;
			continue;
		}

		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, node);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		/* If it's not a leaf, it will already have been deleted */
		*child_offp = UMOFF_NULL;

		/* Ok, remove the rect at the current trace */
		count = node->tn_nr - trace->tr_at - 1;
		node->tn_nr--;

		if (count == 0)
			break;

		memmove(data, data + child_size, child_size * count);

		break;
	};

	changed_level = level;

	/* Update MBR and bubble up */
	while (1) {
		struct evt_rect	rect;
		struct evt_rect	mbr;
		int		i;
		int		offset;

		evt_node_rect_read_at(tcx, node, 0, &mbr);
		for (i = 1; i < node->tn_nr; i++) {
			evt_node_rect_read_at(tcx, node, i, &rect);
			evt_rect_merge(&mbr, &rect);
		}

		if (evt_mbr_same(node, &mbr))
			goto fix_trace; /* mbr hasn't changed */

		evt_mbr_write(node, &mbr);

		if (level == 0)
			goto fix_trace;

		level--;

		trace = &tcx->tc_trace[level];
		nm_cur = trace->tr_node;
		node = evt_off2node(tcx, nm_cur);

		if (!trace->tr_tx_added) {
			rc = evt_node_tx_add(tcx, node);
			if (rc != 0)
				return rc;
			trace->tr_tx_added = true;
		}

		/* make adjustments to the position of the rectangle */
		if (!tcx->tc_ops->po_adjust)
			continue;
		offset = tcx->tc_ops->po_adjust(tcx, node, trace->tr_at);
		if (offset == 0)
			continue;

		changed_level = level;
		if (offset < 0) {
			D_ASSERTF(trace->tr_at >= -offset,
				  "at:%u, offset:%d\n", trace->tr_at, offset);
			trace->tr_at += offset;
		}
	}

fix_trace:
	return evt_tcx_fix_trace(tcx, changed_level);
}

int
evt_delete_internal(struct evt_context *tcx, const struct evt_rect *rect,
		    struct evt_entry *ent, bool in_tx)
{
	EVT_ENT_ARRAY_SM_PTR(ent_array);
	struct evt_filter	 filter = {0};
	int			 rc;

	/* NB: This function presently only supports exact match on extent. */
	evt_ent_array_init(ent_array, 1);

	filter.fr_ex = rect->rc_ex;
	filter.fr_epr.epr_lo = rect->rc_epc;
	filter.fr_epr.epr_hi = rect->rc_epc;
	filter.fr_epoch = rect->rc_epc;
	rc = evt_ent_array_fill(tcx, EVT_FIND_SAME, DAOS_INTENT_PURGE,
				&filter, rect, ent_array);
	if (rc != 0)
		return rc;

	if (ent_array->ea_ent_nr == 0)
		return -DER_ENOENT;

	D_ASSERT(ent_array->ea_ent_nr == 1);
	if (ent != NULL)
		*ent = *evt_ent_array_get(ent_array, 0);

	if (!in_tx) {
		rc = evt_tx_begin(tcx);
		if (rc != 0)
			return rc;
	}

	rc = evt_node_delete(tcx);

	/* We return NON_EXIST from evt_node_delete if there
	 * are no subsequent nodes in the tree.  We can
	 *  ignore this error here
	 */
	if (rc == -DER_NONEXIST)
		rc = 0;

	/* No need for evt_ent_array_fini as there will be no allocations
	 * with 1 entry in the list
	 */
	return in_tx ? rc : evt_tx_end(tcx, rc);
}

int evt_delete(daos_handle_t toh, const struct evt_rect *rect,
	       struct evt_entry *ent)
{
	struct evt_context	*tcx;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;


	return evt_delete_internal(tcx, rect, ent, false);
}

int
evt_remove_all(daos_handle_t toh, const struct evt_extent *ext,
	       const daos_epoch_range_t *epr)
{
	EVT_ENT_ARRAY_SM_PTR(ent_array);
	struct evt_context	*tcx;
	struct evt_entry	*ent;
	struct evt_entry_in	 entry = {0};
	struct evt_filter	 filter = {0};
	struct evt_rect		 rect;
	int			 rc = 0;
	int			 alt_rc = 0;

	/** Find all of the overlapping rectangles and insert a delete record
	 *  for each one in the specified epoch range
	 */
	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rect.rc_epc = filter.fr_epoch = filter.fr_epr.epr_hi = epr->epr_hi;
	filter.fr_epr.epr_lo = epr->epr_lo;
	rect.rc_ex = filter.fr_ex = *ext;
	rect.rc_minor_epc = EVT_MINOR_EPC_MAX;

	evt_ent_array_init(ent_array, 0);

	rc = evt_ent_array_fill(tcx, EVT_FIND_ALL, DAOS_INTENT_PURGE,
				&filter, &rect, ent_array);
	if (rc != 0)
		goto done;

	rc = evt_ent_array_sort(tcx, ent_array, &filter, EVT_ITER_REMOVALS);
	if (rc != 0)
		goto done;

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		goto done;

	evt_ent_array_for_each(ent, ent_array) {
		D_ASSERT(ent->en_minor_epc != EVT_MINOR_EPC_MAX);

		entry.ei_rect.rc_ex = ent->en_sel_ext;
		entry.ei_bound = entry.ei_rect.rc_epc = ent->en_epoch;
		entry.ei_rect.rc_minor_epc = EVT_MINOR_EPC_MAX;

		D_DEBUG(DB_IO, "Insert removal record "DF_RECT"\n", DP_RECT(&entry.ei_rect));
		BIO_ADDR_SET_HOLE(&entry.ei_addr);

		rc = evt_insert(toh, &entry, NULL);
		if (rc == 1) {
			alt_rc = 1;
			rc = 0;
		}
		if (rc < 0)
			break;
	}
	rc = evt_tx_end(tcx, rc);
done:
	evt_ent_array_fini(ent_array);

	return rc == 0 ? alt_rc : rc;
}

daos_size_t
evt_csum_count(const struct evt_context *tcx,
	       const struct evt_extent *extent)
{
	return csum_chunk_count(tcx->tc_root->tr_csum_chunk_size,
				extent->ex_lo,
				extent->ex_hi, tcx->tc_root->tr_inob);
}

daos_size_t
evt_csum_buf_len(const struct evt_context *tcx,
		 const struct evt_extent *extent)
{
	if (tcx->tc_root->tr_csum_chunk_size == 0)
		return 0;
	return tcx->tc_root->tr_csum_len * evt_csum_count(tcx, extent);
}

void
evt_desc_csum_fill(struct evt_context *tcx, struct evt_desc *desc,
		   const struct evt_entry_in *ent, uint8_t **csum_bufp)
{
	const struct dcs_csum_info	*csum = &ent->ei_csum;
	daos_size_t			 csum_buf_len = 0;

	if (!ci_is_valid(csum))
		return;

	/**
	 * If a punch was inserted first (rec_len = 0), then the csum info
	 * won't have been set in evt_root_activate.
	 */
	if (tcx->tc_root->tr_csum_len == 0 && csum->cs_len > 0) {
		tcx->tc_root->tr_csum_len = csum->cs_len;
		tcx->tc_root->tr_csum_type = csum->cs_type;
		tcx->tc_root->tr_csum_chunk_size = csum->cs_chunksize;
	}

	csum_buf_len = ci_csums_len(ent->ei_csum);

	if (csum->cs_buf_len < csum_buf_len) {
		D_ERROR("Issue copying checksum. Source (%d) is larger than destination (%" PRIu64
			")\n",
			csum->cs_buf_len, csum_buf_len);
	} else if (csum_buf_len > 0) {
		memcpy(desc->pt_csum, csum->cs_csum, csum_buf_len);
		if (csum_bufp != NULL)
			*csum_bufp = desc->pt_csum;
	}
}

void
evt_entry_csum_fill(struct evt_context *tcx, struct evt_desc *desc,
		    struct evt_entry *entry)
{
	uint32_t csum_count;

	if (tcx->tc_root->tr_csum_len == 0)
		return;

	/**
	 * Fill these in even if is a hole. Aggregation depends on these
	 * being set to always know checksums is enabled
	 */
	entry->en_csum.cs_type = tcx->tc_root->tr_csum_type;
	entry->en_csum.cs_len = tcx->tc_root->tr_csum_len;
	entry->en_csum.cs_chunksize = tcx->tc_root->tr_csum_chunk_size;

	if (bio_addr_is_hole(&desc->dc_ex_addr)) {
		entry->en_csum.cs_nr = 0;
		entry->en_csum.cs_buf_len = 0;
		entry->en_csum.cs_csum = NULL;
		return;
	}

	D_DEBUG(DB_TRACE, "Filling entry csum from evt_desc");
	csum_count = evt_csum_count(tcx, &entry->en_ext);
	entry->en_csum.cs_nr = csum_count;
	entry->en_csum.cs_buf_len = csum_count * tcx->tc_root->tr_csum_len;
	entry->en_csum.cs_csum = &desc->pt_csum[0];
}

struct evt_extent
evt_entry_align_to_csum_chunk(struct evt_entry *entry, daos_off_t record_size)
{
	struct evt_extent	result;

	struct daos_csum_range chunk = csum_align_boundaries(
		entry->en_sel_ext.ex_lo, entry->en_sel_ext.ex_hi,
		entry->en_ext.ex_lo, entry->en_ext.ex_hi,
		record_size, entry->en_csum.cs_chunksize);

	result.ex_hi = chunk.dcr_hi;
	result.ex_lo = chunk.dcr_lo;

	return result;
}

void
evt_entry_csum_update(const struct evt_extent *const ext,
		      const struct evt_extent *const sel,
		      struct dcs_csum_info *csum_info,
		      daos_size_t rec_len)
{
	uint32_t csum_to_remove;
	daos_size_t chunk_records;

	D_ASSERT(csum_info->cs_chunksize > 0);
	D_ASSERT(sel->ex_lo >= ext->ex_lo);

	chunk_records = csum_record_chunksize(csum_info->cs_chunksize, rec_len)
			/ rec_len;

	csum_to_remove = sel->ex_lo / chunk_records -
			 ext->ex_lo / chunk_records;

	csum_info->cs_csum += csum_info->cs_len * csum_to_remove;
	csum_info->cs_nr -= csum_to_remove;
	csum_info->cs_buf_len -= csum_info->cs_len * csum_to_remove;
}

int
evt_overhead_get(int alloc_overhead, int tree_order,
		 struct daos_tree_overhead *ovhd)
{
	int order;
	int order_idx;

	if (ovhd == NULL) {
		D_ERROR("Invalid ovhd argument\n");
		return -DER_INVAL;
	}

	ovhd->to_record_msize = alloc_overhead + sizeof(struct evt_desc);
	ovhd->to_node_rec_msize = sizeof(struct evt_node_entry);
	ovhd->to_leaf_overhead.no_size  = alloc_overhead + evt_order2size(tree_order, true);
	ovhd->to_leaf_overhead.no_order = tree_order;
	ovhd->to_int_node_size          = alloc_overhead + evt_order2size(tree_order, false);

	order_idx = 0;
	order     = 1;
	while (order != tree_order) {
		ovhd->to_dyn_overhead[order_idx].no_order = order;
		ovhd->to_dyn_overhead[order_idx].no_size  = evt_order2size(order, true);
		order_idx++;
		order = evt_new_order(order, tree_order);
	}
	ovhd->to_dyn_count = order_idx;

	return 0;
}

int
evt_drain(daos_handle_t toh, int *credits, bool *destroyed)
{
	struct evt_context *tcx;
	int		    rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (credits) {
		if (*credits <= 0)
			return -DER_INVAL;

		tcx->tc_creds = *credits;
		tcx->tc_creds_on = 1;
	}

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = evt_root_destroy(tcx, destroyed);
	if (rc)
		goto out;

	if (credits)
		*credits = tcx->tc_creds;
out:
	rc = evt_tx_end(tcx, rc);

	tcx->tc_creds_on = 0;
	tcx->tc_creds = 0;
	return rc;
}

int
evt_feats_set(struct evt_root *root, struct umem_instance *umm, uint64_t feats)

{
	int			 rc = 0;
	bool                     end = false;

	if (root->tr_feats == feats)
		return 0;

	if ((feats & ~EVT_AGG_MASK) != (root->tr_feats & EVT_FEATS_SUPPORTED)) {
		D_ERROR("Attempt to set internal features denied "DF_X64"\n", feats);
		return -DER_INVAL;
	}

	if (!umem_tx_inprogress(umm)) {
		rc = umem_tx_begin(umm, NULL);
		if (rc != 0)
			return rc;
		end = true;
	}
	rc = umem_tx_xadd_ptr(umm, &root->tr_feats, sizeof(root->tr_feats), UMEM_XADD_NO_SNAPSHOT);

	if (rc == 0)
		root->tr_feats = feats;

	if (end)
		rc = umem_tx_end(umm, rc);

	return rc;
}

