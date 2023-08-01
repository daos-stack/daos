/**
 *
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/jump_map_version.c
 */
#define D_LOGFAC        DD_FAC(placement)
#include "pl_map.h"
#include "jump_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>
#include <isa-l.h>

static bool
is_new_added_dom(struct pool_component *comp)
{
	return (comp->co_status == PO_COMP_ST_UP && comp->co_fseq <= 1) ||
		comp->co_status == PO_COMP_ST_NEW;
}

static bool
is_excluded_comp(struct pool_component *comp, bool exclude_new)
{
	/* All other non new added targets/ranks should not be excluded */
	if (is_new_added_dom(comp) && exclude_new)
		return true;

	return false;
}

static inline uint32_t
get_num_domains(struct pool_domain *curr_dom, bool exclude_new, pool_comp_type_t fdom_lvl)
{
	struct pool_domain *next_dom;
	struct pool_target *next_target;
	uint32_t num_dom;

	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl)
		num_dom = curr_dom->do_target_nr;
	else
		num_dom = curr_dom->do_child_nr;

	D_ASSERTF(num_dom > 0, "num dom %u\n", num_dom);
	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl) {
		next_target = &curr_dom->do_targets[num_dom - 1];

		while (num_dom - 1 > 0 && is_excluded_comp(&next_target->ta_comp, exclude_new)) {
			num_dom--;
			next_target = &curr_dom->do_targets[num_dom - 1];
		}
	} else {
		next_dom = &curr_dom->do_children[num_dom - 1];

		while (num_dom - 1 > 0 && is_excluded_comp(&next_dom->do_comp, exclude_new)) {
			num_dom--;
			next_dom = &curr_dom->do_children[num_dom - 1];
		}
	}

	return num_dom;
}

static bool
tgt_isset_range(struct pool_target *tgts, uint8_t *tgts_used,
		uint32_t start_tgt, uint32_t end_tgt, bool exclude_new)
{
	uint32_t index;

	for (index = start_tgt; index <= end_tgt; ++index) {
		if (is_excluded_comp(&tgts[index].ta_comp, exclude_new))
			continue;
		if (isclr(tgts_used, index))
			return false;
	}

	return true;
}

static bool
dom_isset_range(struct pool_domain *doms, uint8_t *doms_bits,
		uint32_t start_dom, uint32_t end_dom, bool exclude_new)
{
	uint32_t index;

	for (index = start_dom; index <= end_dom; ++index) {
		if (is_excluded_comp(&doms[index].do_comp, exclude_new))
			continue;
		if (isclr(doms_bits, index))
			return false;
	}

	return true;
}

static bool
dom_isset_2ranges(struct pool_domain *doms, uint8_t *doms_bits1, uint8_t *doms_bits2,
		  uint32_t start_dom, uint32_t end_dom, bool exclude_new)
{
	uint32_t index;

	for (index = start_dom; index <= end_dom; ++index) {
		if (is_excluded_comp(&doms[index].do_comp, exclude_new))
			continue;

		if (isclr(doms_bits1, index) && isclr(doms_bits2, index))
			return false;
	}

	return true;
}

static bool
is_dom_full(struct pool_domain *dom, struct pool_domain *root,
	    uint8_t *dom_full, bool exclude_new)
{
	uint32_t start_dom = dom->do_children - root;
	uint32_t end_dom = start_dom + (dom->do_child_nr - 1);

	if (dom_isset_range(root, dom_full, start_dom, end_dom, exclude_new))
		return true;

	return false;
}

struct pool_target *
_get_target(struct pool_domain *doms, uint32_t tgt_idx, bool exclude_new)
{
	uint32_t idx = 0;
	uint32_t i;

	for (i = 0; i < doms->do_target_nr; i++) {
		if (!is_excluded_comp(&doms->do_targets[i].ta_comp, exclude_new)) {
			if (idx == tgt_idx)
				return &doms->do_targets[idx];
			idx++;
		}
	}
	return NULL;
}

struct pool_domain *
_get_dom(struct pool_domain *doms, uint32_t dom_idx, bool exclude_new)
{
	uint32_t idx = 0;
	uint32_t i;

	for (i = 0; i < doms->do_child_nr; i++) {
		if (!is_excluded_comp(&doms->do_children[i].do_comp, exclude_new)) {
			if (idx == dom_idx)
				return &doms->do_children[idx];
			idx++;
		}
	}
	return NULL;
}

/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   root_pos        The root domain of the pool map.
 * \param[in]   curr_pd         The current performance domain that is being used to
 *                              determine the target location for this shard.
 *                              When there is no PD, it is same as root domain.
 * \param[out]  target          This variable is used when returning the
 *                              selected target for this shard.
 * \param[out]  dom             This variable is used when returning the
 *                              selected domain for this shard.
 * \param[in]   obj_key         a unique key generated using the object ID.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   dom_full	This is a contiguous array that contains
 *                              information on whether or not all targets of the
 *                              domain has been occupied.
 * \param[in]	dom_cur_grp_used The array contains information if the domain
 *                              is used by the current group, so it can try not
 *                              put the different shards in the same domain.
 * \param[in]   used_targets    A list of the targets that have been used. We
 *                              iterate through this when selecting the next
 *                              target in a placement to determine if that
 *                              location is valid.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 * \param[in]	exclude_new	exclude new target/rank during mapping.
 * \param[in]   fdom_lvl	failure domain of the current pool map
 * \param[in]   grp_size	object group size.
 * \param[out]  pd_ignored	true means the PD restrict is ignored inside the loop
 */
#define MAX_STACK	5
static void
__get_target_v1(struct pool_domain *root_pos, struct pool_domain *curr_pd,
		struct pool_target **target, struct pool_domain **dom, uint64_t obj_key,
		uint8_t *dom_used, uint8_t *dom_full, uint8_t *dom_cur_grp_used, uint8_t *tgts_used,
		int shard_num, bool exclude_new, pool_comp_type_t fdom_lvl, uint32_t grp_size,
		bool *pd_ignored)
{
	int                     range_set;
	uint8_t                 found_target = 0;
	struct pool_domain      *curr_dom;
	struct pool_domain	*dom_stack[MAX_STACK] = { 0 };
	int			top = -1;

	obj_key = crc(obj_key, shard_num);
	curr_dom = curr_pd;
	do {
		uint32_t        avail_doms;

		/* Retrieve number of nodes in this domain */
		avail_doms = get_num_domains(curr_dom, exclude_new, fdom_lvl);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_comp.co_type == fdom_lvl) {
			uint32_t        fail_num = 0;
			uint32_t        tgt_idx;
			uint32_t        start_tgt;
			uint32_t        end_tgt;
			uint32_t	selected_tgt;

			start_tgt = curr_dom->do_targets - root_pos->do_targets;
			end_tgt = start_tgt + (curr_dom->do_target_nr - 1);

			range_set = tgt_isset_range(root_pos->do_targets, tgts_used,
						    start_tgt, end_tgt, exclude_new);
			if (range_set) {
				/* Used up all targets in this domain */
				D_ASSERT(top != -1);
				curr_dom = dom_stack[top--]; /* try parent */
				continue;
			}

			/*
			 * Must crc key because jump consistent hash
			 * requires an even distribution or it will
			 * not work
			 */
			obj_key = crc(obj_key, fail_num++);
			/* Get target for shard */
			selected_tgt = d_hash_jump(obj_key, avail_doms);
			do {
				selected_tgt = selected_tgt % avail_doms;
				/* Retrieve actual target using index */
				*target = _get_target(curr_dom, selected_tgt, exclude_new);
				/* Get target id to check if target used */
				tgt_idx = *target - root_pos->do_targets;
				selected_tgt++;
			} while (isset(tgts_used, tgt_idx));

			setbit(tgts_used, tgt_idx);
			D_DEBUG(DB_PL, "selected tgt %d\n", tgt_idx);
			D_ASSERTF(isclr(dom_full, (uint32_t)(curr_dom - root_pos)),
				  "selected_tgt %u\n", (uint32_t)(curr_dom - root_pos));
			range_set = tgt_isset_range(root_pos->do_targets, tgts_used,
						    start_tgt, end_tgt, exclude_new);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_full, curr_dom - root_pos);
				D_DEBUG(DB_PL, "dom %d used up\n", (int)(curr_dom - root_pos));
				/* Check and set if all of its parent are full */
				while(top != -1) {
					if (is_dom_full(dom_stack[top], root_pos, dom_full,
							exclude_new)) {
						uint32_t off = dom_stack[top] - root_pos;

						D_DEBUG(DB_PL, "dom %u used up\n", off);
						setbit(dom_full, off);
					}
					--top;
				}
			}
			*dom = curr_dom;
			/* Found target (which may be available or not) */
			found_target = 1;
		} else {
			uint32_t	selected_dom;
			uint32_t        fail_num = 0;
			uint32_t        start_dom;
			uint32_t        end_dom;
			uint64_t        key = obj_key;

			start_dom = (curr_dom->do_children) - root_pos;
			end_dom = start_dom + (curr_dom->do_child_nr - 1);

			/* Check if all targets under the domain range has been
			 * used up (occupied), go back to its parent if it does.
			 */
			range_set = is_dom_full(curr_dom, root_pos, dom_full, exclude_new);
			if (range_set) {
				if (top == -1) {
					if (curr_pd != root_pos) {
						/* all domains within the PD are full, ignore the
						 * PD restrict.
						 */
						D_DEBUG(DB_PL, "PD[%d] all doms are full, weak the "
							"PD restrict\n",
							(int)(curr_dom - root_pos));
						curr_pd = root_pos;
						curr_dom = curr_pd;
						*pd_ignored = true;
						continue;
					}
					/* shard nr > target nr, no extra target for the shard */
					*target = NULL;
					return;
				}
				setbit(dom_full, curr_dom - root_pos);
				D_DEBUG(DB_PL, "used up dom %d\n",
					(int)(curr_dom - root_pos));
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			/* Check if all children has been used for the current group or full */
			range_set = dom_isset_2ranges(root_pos, dom_full, dom_cur_grp_used,
						      start_dom, end_dom, exclude_new);
			if (range_set) {
				if (top == -1) {
					if (curr_pd != root_pos && grp_size > 1) {
						/* All domains within the PD are full, ignore the
						 * PD restrict. For non-replica (grp_size 1) case,
						 * keep the PD restrict until all targets under the
						 * domain range has been used up (see above check
						 * for dom_full bitmap).
						 */
						D_DEBUG(DB_PL, "PD[%d] all doms are full, weak the "
							"PD restrict\n",
							(int)(curr_dom - root_pos));
						curr_pd = root_pos;
						curr_dom = curr_pd;
						*pd_ignored = true;
						continue;
					}
					*target = NULL;
					return;
				}
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				D_DEBUG(DB_PL, "set grp_used %d\n", (int)(curr_dom - root_pos));
				curr_dom = dom_stack[top--];
				continue;
			}

			/* Check if all children has been used for the current group or the
			 * object.
			 */
			range_set = dom_isset_2ranges(root_pos, dom_used, dom_cur_grp_used,
						      start_dom, end_dom, exclude_new);
			if (range_set) {
				int idx;

				for (idx = start_dom; idx <= end_dom; ++idx)
					if (!isset(dom_full, idx)) {
						clrbit(dom_used, idx);
						D_DEBUG(DB_PL, "clrbit dom_used %d\n", idx);
					}
				if (top == -1)
					curr_dom = curr_pd;
				else
					curr_dom = dom_stack[top--];
				continue;
			}

			/* Keep choosing the new domain until the one has not been used. */
			do {
				struct pool_domain *_dom;

				selected_dom = d_hash_jump(key, avail_doms);
				key = crc(key, fail_num++);
				_dom = _get_dom(curr_dom, selected_dom, exclude_new);
				selected_dom = _dom - curr_dom->do_children;
			} while (isset(dom_used, start_dom + selected_dom) ||
				 isset(dom_cur_grp_used, start_dom + selected_dom));

			D_ASSERTF(isclr(dom_full, start_dom + selected_dom), "selected_dom %u\n",
				  selected_dom);
			/* Mark this domain as used */
			if (curr_dom == curr_pd && curr_pd != root_pos)
				setbit(dom_used, (int)(curr_dom - root_pos));
			D_DEBUG(DB_PL, "selected dom %d\n", start_dom + selected_dom);
			setbit(dom_used, start_dom + selected_dom);
			setbit(dom_cur_grp_used, start_dom + selected_dom);
			D_ASSERT(top < MAX_STACK - 1);
			top++;
			dom_stack[top] = curr_dom;
			curr_dom = &curr_dom->do_children[selected_dom];
			obj_key = crc(obj_key, curr_dom->do_comp.co_id);
		}
	} while (!found_target);
}

/* Only reset all used dom/tgts bits if all domain has been used. */
static void
dom_reset_bit(struct pool_domain *dom, uint8_t *dom_bits, struct pool_domain *root,
	      bool exclude_new, uint32_t fdom_lvl)
{
	struct pool_domain	*tree;
	uint32_t		dom_nr;

	tree = dom;
	dom_nr = 1;
	for (; tree != NULL; tree = tree[0].do_children) {
		uint32_t start_dom = tree - root;
		uint32_t end_dom = start_dom + dom_nr - 1;
		uint32_t next_dom_nr = 0;
		int	 i;

		if (tree->do_children) {
			if (!dom_isset_range(root, dom_bits, start_dom,
					     end_dom, exclude_new))
				return;
			for (i = 0; i < dom_nr; i++) {
				clrbit(dom_bits, start_dom + i);
				next_dom_nr += dom->do_child_nr;
			}
		} else {
			clrbit(dom_bits, start_dom);
		}
		dom_nr = next_dom_nr;
	}
}

static void
dom_reset_full(struct pool_domain *dom, uint8_t *dom_bits, uint8_t *tgts_used,
	       struct pool_domain *root, bool exclude_new, uint32_t fdom_lvl)
{
	int i;

	dom_reset_bit(dom, dom_bits, root, exclude_new, fdom_lvl);

	/* check and reset all used tgts bits under the domain */
	for (i = 0; i < dom->do_target_nr; i++) {
		if (!isset(tgts_used, &dom->do_targets[i] - root->do_targets))
			return;
	}
	for (i = 0; i < dom->do_target_nr; i++)
		clrbit(tgts_used, &dom->do_targets[i] - root->do_targets);
}

static bool
dom_tgts_are_avaible(struct pool_domain *dom, uint32_t allow_status, uint32_t allow_version)
{
	int i;

	for (i = 0; i < dom->do_target_nr; i++) {
		struct pool_target *tgt;
		uint32_t status;

		tgt = &dom->do_targets[i];
		status = tgt->ta_comp.co_status;
		if (tgt->ta_comp.co_status == PO_COMP_ST_DOWN) {
			if (tgt->ta_comp.co_fseq > allow_version)
				status = PO_COMP_ST_UPIN;
		} else if (tgt->ta_comp.co_status == PO_COMP_ST_UP) {
			if (tgt->ta_comp.co_in_ver > allow_version)
				status = PO_COMP_ST_DOWNOUT;
		}
		if (status & allow_status)
			return true;
	}
	return false;
}

/* Reset dom/targets tracking bits for remapping the layout */
static void
reset_dom_cur_grp_v1(struct pool_domain *root, struct pool_domain *curr_pd,
		     uint8_t *dom_cur_grp_used, uint8_t *dom_cur_grp_real, uint8_t *dom_full,
		     uint8_t *tgts_used, bool exclude_new, uint32_t fdom_lvl,
		     uint32_t allow_status, uint32_t allow_version)
{
	struct pool_domain	*tree;
	uint32_t		dom_nr;

	tree = curr_pd;
	dom_nr = 1;
	D_DEBUG(DB_PL, "bitmap resetting... curr_pd at dom[%d] (0 is root)\n",
		(int)(curr_pd - root));
	/* Walk through the failure domain to reset full, current_group and tgts used bits */
	for (; tree != NULL && tree->do_comp.co_type >= fdom_lvl; tree = tree[0].do_children) {
		uint32_t start_dom = tree - root;
		uint32_t end_dom = start_dom + dom_nr - 1;
		uint32_t next_dom_nr = 0;
		bool	reset_full = false;
		bool	reset = false;
		int	i;

		/* reset all bits if it is above failure domain */
		if (tree->do_comp.co_type > fdom_lvl) {
			for (i = 0; i < dom_nr; i++) {
				uint32_t curr_dom_off = start_dom + i;
				struct pool_domain *dom = &root[curr_dom_off];

				if (dom->do_children)
					next_dom_nr += dom->do_child_nr;

				clrbit(dom_cur_grp_used, start_dom + i);
				clrbit(dom_full, start_dom + i);
			}
			dom_nr = next_dom_nr;
			continue;
		}

		/* There are still domain available, so no need reset any bits. */
		if (!dom_isset_2ranges(root, dom_full, dom_cur_grp_used,
				       start_dom, end_dom, exclude_new))
			break;

		/* If there are domains, which are not used by this group,
		 * let's reset the full bits first, which might cause multiple shards
		 * from the same object in the same target.
		 */
		if (!dom_isset_range(root, dom_cur_grp_used, start_dom, end_dom,
				     exclude_new)) {
			for (i = 0; i < dom_nr; i++) {
				uint32_t		dom_off = start_dom + i;
				struct pool_domain	*dom = &root[dom_off];

				if (!isset(dom_cur_grp_used, dom_off) &&
				    isset(dom_full, dom_off))
					dom_reset_full(dom, dom_full, tgts_used, root,
						       exclude_new, fdom_lvl);
			}
			break;
		}

		/* Since then all domains have been tried, then let's check if any domain
		 * are not really used due to the chosen target is not available.
		 */
		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *dom = &root[start_dom + i];

			if (!isset(dom_cur_grp_real, start_dom + i)) {
				uint32_t start_tgt;
				uint32_t end_tgt;

				start_tgt = dom->do_targets - root->do_targets;
				end_tgt = start_tgt + dom->do_target_nr - 1;
				if (!tgt_isset_range(root->do_targets, tgts_used,
						     start_tgt, end_tgt, exclude_new)) {
					dom_reset_bit(dom, dom_cur_grp_used, root,
						      exclude_new, fdom_lvl);
					reset = true;
				}
			}
		}

		if (reset)
			break;

		reset = false;
		/* All targets other than on the real used domain has been used up, let's
		 * reset these domain and tgt used bits */
		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *dom = &root[start_dom + i];

			if (!isset(dom_cur_grp_real, start_dom + i) &&
			    dom_tgts_are_avaible(dom, allow_status, allow_version)) {
				dom_reset_full(dom, dom_full, tgts_used, root,
					       exclude_new, fdom_lvl);
				dom_reset_bit(dom, dom_cur_grp_used, root,
					      exclude_new, fdom_lvl);
				reset = true;
			}
		}

		if (reset)
			break;

		/* Finally reset cur_group_used, which  multiple shards
		 * from the same group be in the same domain.
		 */
		if (dom_isset_range(root, dom_full, start_dom, end_dom, exclude_new))
			reset_full = true;

		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *dom = &root[start_dom + i];

			if (reset_full)
				dom_reset_full(dom, dom_full, tgts_used, root,
					       exclude_new, fdom_lvl);

			dom_reset_bit(dom, dom_cur_grp_used, root, exclude_new, fdom_lvl);
		}
		break;
	}
}

static void
get_target_v1(struct pool_domain *root, struct pool_domain *curr_pd, struct pool_target **target,
	      struct pool_domain **dom, uint64_t key, uint8_t *dom_used, uint8_t *dom_full,
	      uint8_t *dom_cur_grp_used, uint8_t *dom_cur_grp_real, uint8_t *tgts_used,
	      int shard_num, uint32_t allow_status, uint32_t allow_version,
	      pool_comp_type_t fdom_lvl, uint32_t grp_size)
{
	struct pool_target	*found = NULL;
	bool			 exclude_new = true;
	bool			 pd_ignored;

	/* For extending case, it needs to get the layout in two cases, with UP/NEW target
	 * and without UP/NEW targets, then tell the difference, see placement/jump_map.c.
	 */
	/* NB: It does not tell other status of the target in this function, to make sure
	 * the target is mapped according to the failure sequence strictly. And other target
	 * status check is done in get_object_layout().
	 */
	if (allow_status & PO_COMP_ST_UP)
		exclude_new = false;

	while (found == NULL) {
		pd_ignored = false;
		__get_target_v1(root, curr_pd, &found, dom, key, dom_used, dom_full,
				dom_cur_grp_used, tgts_used, shard_num, exclude_new,
				fdom_lvl, grp_size, &pd_ignored);
		if (found == NULL) {
			if (pd_ignored)
				reset_dom_cur_grp_v1(root, root, dom_cur_grp_used, dom_cur_grp_real,
						     dom_full, tgts_used, exclude_new, fdom_lvl,
						     allow_status, allow_version);
			else
				reset_dom_cur_grp_v1(root, curr_pd, dom_cur_grp_used,
						     dom_cur_grp_real, dom_full, tgts_used,
						     exclude_new, fdom_lvl, allow_status,
						     allow_version);
		}
	}
	*target = found;
}

static void
reset_dom_cur_grp_v0(uint8_t *dom_cur_grp_used, uint8_t *dom_occupied, uint32_t dom_size)
{
	int i;

	for (i = 0; i < dom_size; i++) {
		if (isset(dom_occupied, i)) {
			/* if all targets used up, this dom will not be used anyway */
			setbit(dom_cur_grp_used, i);
		} else {
			/* otherwise reset it */
			clrbit(dom_cur_grp_used, i);
		}
	}
}

/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   curr_dom        The current domain that is being used to
 *                              determine the target location for this shard.
 *                              initially the root node of the pool map.
 * \param[out]  target          This variable is used when returning the
 *                              selected target for this shard.
 * \param[in]   obj_key         a unique key generated using the object ID.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   dom_occupied    This is a contiguous array that contains
 *                              information on whether or not all targets of the
 *                              domain has been occupied.
 * \param[in]	dom_cur_grp_used The array contains information if the domain
 *                              is used by the current group, so it can try not
 *                              put the different shards in the same domain.
 * \param[in]   used_targets    A list of the targets that have been used. We
 *                              iterate through this when selecting the next
 *                              target in a placement to determine if that
 *                              location is valid.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 *
 */
#define MAX_STACK	5
static void
get_target_v0(struct pool_domain *curr_dom, struct pool_target **target,
	      uint64_t obj_key, uint8_t *dom_used, uint8_t *dom_occupied,
	      uint8_t *dom_cur_grp_used, uint8_t *tgts_used,
	      int shard_num, uint32_t allow_status, pool_comp_type_t fdom_lvl)
{
	int                     range_set;
	uint8_t                 found_target = 0;
	uint32_t                selected_dom;
	struct pool_domain      *root_pos;
	struct pool_domain	*dom_stack[MAX_STACK] = { 0 };
	uint32_t		dom_size;
	int			top = -1;

	obj_key = crc(obj_key, shard_num);
	root_pos = curr_dom;
	dom_size = (struct pool_domain *)(root_pos->do_targets) - (root_pos) + 1;
retry:
	do {
		uint32_t        num_doms;

		/* Retrieve number of nodes in this domain */
		num_doms = get_num_domains(curr_dom, allow_status & PO_COMP_ST_UP,
					   fdom_lvl);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl) {
			uint32_t        fail_num = 0;
			uint32_t        dom_id;
			uint32_t        start_tgt;
			uint32_t        end_tgt;

			start_tgt = curr_dom->do_targets - root_pos->do_targets;
			end_tgt = start_tgt + (num_doms - 1);

			range_set = isset_range(tgts_used, start_tgt, end_tgt);
			if (range_set) {
				/* Used up all targets in this domain */
				D_ASSERT(top != -1);
				curr_dom = dom_stack[top--]; /* try parent */
				continue;
			}

			/*
			 * Must crc key because jump consistent hash
			 * requires an even distribution or it will
			 * not work
			 */
			obj_key = crc(obj_key, fail_num++);
			/* Get target for shard */
			selected_dom = d_hash_jump(obj_key, num_doms);
			do {
				selected_dom = selected_dom % num_doms;
				/* Retrieve actual target using index */
				*target = &curr_dom->do_targets[selected_dom];
				/* Get target id to check if target used */
				dom_id = *target - root_pos->do_targets;
				selected_dom++;
			} while (isset(tgts_used, dom_id));

			setbit(tgts_used, dom_id);
			setbit(dom_cur_grp_used, curr_dom - root_pos);
			range_set = isset_range(tgts_used, start_tgt, end_tgt);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_occupied, curr_dom - root_pos);
				D_DEBUG(DB_PL, "dom %p %d used up\n",
					dom_occupied, (int)(curr_dom - root_pos));
			}

			/* Found target (which may be available or not) */
			found_target = 1;
		} else {
			uint32_t        fail_num = 0;
			uint64_t        start_dom;
			uint64_t        end_dom;
			uint64_t        key;

			key = obj_key;

			start_dom = (curr_dom->do_children) - root_pos;
			end_dom = start_dom + (num_doms - 1);

			/* Check if all targets under the domain range has been
			 * used up (occupied), go back to its parent if it does.
			 */
			range_set = isset_range(dom_occupied, start_dom, end_dom);
			if (range_set) {
				if (top == -1) {
					/* shard nr > target nr, no extra target for the shard */
					*target = NULL;
					return;
				}
				setbit(dom_occupied, curr_dom - root_pos);
				D_DEBUG(DB_PL, "used up dom %d\n",
					(int)(curr_dom - root_pos));
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			/* Check if all domain range has been used for the current group */
			range_set = isset_range(dom_cur_grp_used, start_dom, end_dom);
			if (range_set) {
				if (top == -1) {
					/* all domains have been used by the current group,
					 * then we cleanup the dom_cur_grp_used bits, i.e.
					 * the shards within the same group might be put
					 * to the same domain.
					 */
					reset_dom_cur_grp_v0(dom_cur_grp_used, dom_occupied,
							     dom_size);
					goto retry;
				}
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			/* Check if all targets under the domain have been used, and try to reset
			 * the used targets.
			 */
			range_set = isset_range(dom_used, start_dom, end_dom);
			if (range_set) {
				int idx;
				bool reset_used = false;

				for (idx = start_dom; idx <= end_dom; ++idx) {
					if (isset(dom_occupied, idx)) {
						/* If all targets of the domain has been used up,
						 * then these targets can not be reused. And also
						 * set the group bits here to make the check easier.
						 */
						setbit(dom_cur_grp_used, idx);
					} else if (isclr(dom_cur_grp_used, idx)) {
						/* If the domain has been used for the current
						 * group, then let's do not reset the used bits,
						 * i.e. do not choose the domain unless all domain
						 * are used. see above.
						 */
						clrbit(dom_used, idx);
						reset_used = true;
					}
				}
				/* if all children of the current dom have been
				 * used, then let's go back its parent to check
				 * its siblings.
				 */
				if (curr_dom != root_pos) {
					setbit(dom_used, curr_dom - root_pos);
					D_ASSERT(top != -1);
					curr_dom = dom_stack[top--];
				} else {
					/* If no used dom is being reset at root level, then it
					 * means all domain has been used for the group. So let's
					 * reset dom_cur_grp_used and start put multiple domain in
					 * the same group.
					 */
					if (!reset_used)
						reset_dom_cur_grp_v0(dom_cur_grp_used, dom_occupied,
								     dom_size);
					curr_dom = root_pos;
				}
				continue;
			}
			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {
				selected_dom = d_hash_jump(key, num_doms);
				key = crc(key, fail_num++);
			} while (isset(dom_used, start_dom + selected_dom));

			/* Mark this domain as used */
			setbit(dom_used, start_dom + selected_dom);
			D_ASSERT(top < MAX_STACK - 1);
			dom_stack[++top] = curr_dom;
			curr_dom = &curr_dom->do_children[selected_dom];
			obj_key = crc(obj_key, curr_dom->do_comp.co_id);
		}
	} while (!found_target);
}

void
get_target(struct pool_domain *root, struct pool_domain *curr_pd, uint32_t layout_ver,
	   struct pool_target **target, struct pool_domain **dom, uint64_t key, uint8_t *dom_used,
	   uint8_t *dom_full, uint8_t *dom_cur_grp_used, uint8_t *dom_cur_grp_real,
	   uint8_t *tgts_used, int shard_num, uint32_t allow_status, uint32_t allow_version,
	   pool_comp_type_t fdom_lvl, uint32_t grp_size, uint32_t *spare_left, bool *spare_avail)
{
	switch(layout_ver) {
	case 0:
		get_target_v0(root, target, key, dom_used, dom_full, dom_cur_grp_used,
			      tgts_used, shard_num, allow_status, fdom_lvl);
		if (spare_avail) {
			if ( --(*spare_left) > 0)
				*spare_avail = true;
			else
				*spare_avail = false;
		}
		break;
	case 1:
		get_target_v1(root, curr_pd, target, dom, key, dom_used, dom_full, dom_cur_grp_used,
			      dom_cur_grp_real, tgts_used, shard_num, allow_status, allow_version,
			      fdom_lvl, grp_size);
		if (spare_avail)
			*spare_avail = true;
		break;
	default:
		break;
	}
}
