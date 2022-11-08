/**
 *
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/jump_map_version.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>
#include <isa-l.h>

static inline uint32_t
get_num_domains(struct pool_domain *curr_dom, uint32_t allow_status, pool_comp_type_t fdom_lvl)
{
	struct pool_domain *next_dom;
	struct pool_target *next_target;
	uint32_t num_dom;
	uint8_t status;

	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl)
		num_dom = curr_dom->do_target_nr;
	else
		num_dom = curr_dom->do_child_nr;

	if (allow_status & PO_COMP_ST_UP)
		return num_dom;

	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl) {
		next_target = &curr_dom->do_targets[num_dom - 1];
		status = next_target->ta_comp.co_status;

		while (num_dom - 1 > 0 &&
		       ((status == PO_COMP_ST_UP && next_target->ta_comp.co_fseq <= 1) ||
			status == PO_COMP_ST_NEW)) {
			num_dom--;
			next_target = &curr_dom->do_targets[num_dom - 1];
			status = next_target->ta_comp.co_status;
		}
	} else {
		next_dom = &curr_dom->do_children[num_dom - 1];
		status = next_dom->do_comp.co_status;

		while (num_dom - 1 > 0 &&
		       ((status == PO_COMP_ST_UP && next_dom->do_comp.co_fseq <= 1) ||
			status == PO_COMP_ST_NEW)) {
			num_dom--;
			next_dom = &curr_dom->do_children[num_dom - 1];
			status = next_dom->do_comp.co_status;
		}
	}

	return num_dom;
}

static bool
is_dom_full(struct pool_domain *dom, uint32_t num_doms, struct pool_domain *root,
	    uint8_t *dom_full)
{
	uint32_t start_dom = dom->do_children - root;
	uint32_t end_dom = start_dom + (num_doms - 1);

	D_DEBUG(DB_TRACE, "check dom %u-%u\n", start_dom, end_dom);
	if (isset_range(dom_full, start_dom, end_dom))
		return true;

	return false;
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
 * \param[in]	allow_status	target status to be allowed for mapping the target.
 * \param[in]	allow_version	target in/out version to be allowed for mapping the
 *                              target.
 * \param[in]   fdom_lvl	failure domain of the current pool map
 *
 */
#define MAX_STACK	5
static void
__get_target(struct pool_domain *curr_dom, struct pool_target **target,
	     uint64_t obj_key, uint8_t *dom_used, uint8_t *dom_full,
	     uint8_t *dom_cur_grp_used, uint8_t *tgts_used, int shard_num,
	     uint32_t allow_status, uint32_t allow_version,
	     pool_comp_type_t fdom_lvl)
{
	int                     range_set;
	uint8_t                 found_target = 0;
	uint32_t                selected_dom;
	struct pool_domain      *root_pos;
	struct pool_domain	*dom_stack[MAX_STACK] = { 0 };
	uint32_t		nums_stack[MAX_STACK] = { 0 };
	int			top = -1;

	obj_key = crc(obj_key, shard_num);
	root_pos = curr_dom;
	do {
		uint32_t        num_doms;

		/* Retrieve number of nodes in this domain */
		num_doms = get_num_domains(curr_dom, allow_status, fdom_lvl);
		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_comp.co_type == fdom_lvl) {
			uint32_t        fail_num = 0;
			uint32_t        dom_id;
			uint32_t        start_tgt;
			uint32_t        end_tgt;

			start_tgt = curr_dom->do_targets[0].ta_comp.co_id;
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
				dom_id = (*target)->ta_comp.co_id;
				selected_dom++;
			} while (isset(tgts_used, dom_id));

			setbit(tgts_used, dom_id);
			D_ASSERTF(isclr(dom_full, (uint32_t)(curr_dom - root_pos)),
				  "selected_dom %u\n", (uint32_t)(curr_dom - root_pos));
			range_set = isset_range(tgts_used, start_tgt, end_tgt);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_full, curr_dom - root_pos);
				/* Check and set if all of its parent are full */
				while(top != -1) {
					if (is_dom_full(dom_stack[top], nums_stack[top],
							root_pos, dom_full)) {
						uint32_t off = dom_stack[top] - root_pos;

						D_DEBUG(DB_PL, "dom %u used up\n", off);
						setbit(dom_full, off);
					}
					--top;
				}
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
			range_set = isset_range(dom_full, start_dom, end_dom);
			if (range_set) {
				if (top == -1) {
					/* shard nr > target nr, no extra target for the shard */
					setbit(dom_full, 0);
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

			/* Check if all domain range has been used for the current group */
			range_set = isset_2ranges(dom_full, dom_cur_grp_used, start_dom, end_dom);
			if (range_set) {
				if (top == -1) {
					*target = NULL;
					return;
				}
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			/*
			 * Check if all targets under the domain have been used by objects
			 * or current group, and try to reset the used targets.
			 */
			range_set = isset_2ranges(dom_used, dom_cur_grp_used, start_dom, end_dom);
			if (range_set) {
				int idx;

				for (idx = start_dom; idx <= end_dom; ++idx)
					if (!isset(dom_full, idx))
						clrbit(dom_used, idx);
				if (top == -1)
					curr_dom = root_pos;
				else
					curr_dom = dom_stack[top--];
				continue;
			}

			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {
				selected_dom = d_hash_jump(key, num_doms);
				key = crc(key, fail_num++);
			} while (isset(dom_used, start_dom + selected_dom) ||
				 isset(dom_cur_grp_used, start_dom + selected_dom));

			D_ASSERTF(isclr(dom_full, start_dom + selected_dom), "selected_dom %u\n",
				  selected_dom);
			/* Mark this domain as used */
			setbit(dom_used, start_dom + selected_dom);
			setbit(dom_cur_grp_used, start_dom + selected_dom);
			D_ASSERT(top < MAX_STACK - 1);
			top++;
			dom_stack[top] = curr_dom;
			nums_stack[top] = num_doms;
			curr_dom = &curr_dom->do_children[selected_dom];
			obj_key = crc(obj_key, curr_dom->do_comp.co_id);
		}
	} while (!found_target);
}

/* Only reset all used dom/tgts bits if all domain has been used. */
static void
dom_reset_bit(struct pool_domain *dom, uint8_t *dom_bits, struct pool_domain *root,
	      uint32_t allow_status, uint32_t fdom_lvl)
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
			if (!isset_range(dom_bits, start_dom, end_dom))
				return;
			for (i = 0; i < dom_nr; i++) {
				clrbit(dom_bits, start_dom + i);
				next_dom_nr += get_num_domains(dom, allow_status, fdom_lvl);
			}
		} else {
			clrbit(dom_bits, start_dom);
		}
		dom_nr = next_dom_nr;
	}
}

static void
dom_reset_full(struct pool_domain *dom, uint8_t *dom_bits, uint8_t *tgts_used,
	       struct pool_domain *root, uint32_t allow_status, uint32_t fdom_lvl)
{
	int i;

	dom_reset_bit(dom, dom_bits, root, allow_status, fdom_lvl);

	/* check and reset all used tgts bits under the domain */
	for (i = 0; i < dom->do_target_nr; i++) {
		if (!isset(tgts_used, dom->do_targets[i].ta_comp.co_id))
			return;
	}
	for (i = 0; i < dom->do_target_nr; i++)
		clrbit(tgts_used, dom->do_targets[i].ta_comp.co_id);
}

/* Reset dom/targets tracking bits for remapping the layout */
static void
reset_dom_cur_grp_v1(struct pool_domain *root, uint8_t *dom_cur_grp_used,
		     uint8_t *dom_full, uint8_t *tgts_used, uint32_t allow_version,
		     uint32_t allow_status, uint32_t fdom_lvl)
{
	struct pool_domain	*tree;
	uint32_t		dom_nr;

	tree = root;
	dom_nr = tree[0].do_children - tree;
	/* Walk through the failure domain to reset full, current_group and tgts used bits */
	for (; tree != NULL && tree->do_comp.co_type >= fdom_lvl; tree = tree[0].do_children) {
		uint32_t start_dom = tree - root;
		uint32_t end_dom = start_dom + dom_nr - 1;
		uint32_t next_dom_nr = 0;
		bool	reset_full = false;
		int	i;

		/* reset all bits if it is above failure domain */
		if (tree->do_comp.co_type > fdom_lvl) {
			for (i = 0; i < dom_nr; i++) {
				uint32_t curr_dom_off = start_dom + i;
				struct pool_domain *dom = &root[curr_dom_off];

				if (dom->do_children)
					next_dom_nr += get_num_domains(dom, allow_status,
								       fdom_lvl);

				clrbit(dom_cur_grp_used, start_dom + i);
				clrbit(dom_full, start_dom + i);
			}
			dom_nr = next_dom_nr;
			continue;
		}

		/* There are still domain available, so no need reset any bits. */
		if (!isset_2ranges(dom_full, dom_cur_grp_used, start_dom, end_dom))
			break;

		/* If there are domains, which are not used by this group,
		 * let's reset the full bits first, which might cause multiple shards
		 * from the same object in the same target.
		 */
		if (!isset_range(dom_cur_grp_used, start_dom, end_dom)) {
			for (i = 0; i < dom_nr; i++) {
				uint32_t		dom_off = start_dom + i;
				struct pool_domain	*dom = &root[dom_off];

				if (!isset(dom_cur_grp_used, dom_off) &&
				    isset(dom_full, dom_off))
					dom_reset_full(dom, dom_full, tgts_used, root,
						       allow_status, fdom_lvl);
			}
			break;
		}

		/* Then reset the cur_group used, which will cause multiple shards
		 * from the same group be in the same domain.
		 */
		if (isset_range(dom_full, start_dom, end_dom))
			reset_full = true;

		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *dom = &root[start_dom + i];

			if (reset_full)
				dom_reset_full(dom, dom_full, tgts_used, root,
					       allow_status, fdom_lvl);

			dom_reset_bit(dom, dom_cur_grp_used, root, allow_status,
				      fdom_lvl);
		}
		break;
	}
}

static void
get_target_v1(struct pool_domain *root, struct pool_target **target,
	      uint64_t key, uint8_t *dom_used, uint8_t *dom_full,
	      uint8_t *dom_cur_grp_used, uint8_t *tgts_used, int shard_num,
	      uint32_t allow_status, uint32_t allow_version, pool_comp_type_t fdom_lvl)
{
	struct pool_target *found = NULL;

	while (found == NULL) {
		__get_target(root, &found, key, dom_used, dom_full,
			     dom_cur_grp_used, tgts_used, shard_num, allow_status,
			     allow_version, fdom_lvl);
		if (found == NULL)
			reset_dom_cur_grp_v1(root, dom_cur_grp_used, dom_full,
					     tgts_used, allow_version, allow_status,
					     fdom_lvl);
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
		num_doms = get_num_domains(curr_dom, allow_status, fdom_lvl);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl) {
			uint32_t        fail_num = 0;
			uint32_t        dom_id;
			uint32_t        start_tgt;
			uint32_t        end_tgt;

			start_tgt = curr_dom->do_targets[0].ta_comp.co_id;
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
				dom_id = (*target)->ta_comp.co_id;
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
get_target(struct pool_domain *root, uint32_t layout_ver, struct pool_target **target,
	   /*struct pool_domain **domain,*/ uint64_t key, uint8_t *dom_used,
	   uint8_t *dom_full, uint8_t *dom_cur_grp_used, /*uint8_t *dom_cur_grp_remap,*/
	   uint8_t *tgts_used, int shard_num, uint32_t allow_status,
	   uint32_t allow_version, pool_comp_type_t fdom_lvl, uint32_t *spare_left,
	   bool *spare_avail)
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
		get_target_v1(root, target, key, dom_used, dom_full, dom_cur_grp_used,
			      tgts_used, shard_num, allow_status, allow_version,
			      fdom_lvl);
		if (spare_avail)
			*spare_avail = true;
		break;
	default:
		break;
	}
}
