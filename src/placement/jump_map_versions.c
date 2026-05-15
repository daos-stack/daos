/**
 *
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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

/* NB: this function checks if the component should be skipped in jump hash layout generation
 * process, those components which are in NEW status or being added afterwards should be skipped.
 */
static bool
comp_is_skipped(struct pool_component *comp, uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	if (comp->co_status == PO_COMP_ST_NEW)
		return true;

	if (comp->co_status == PO_COMP_ST_UP && comp->co_fseq <= 1) { /* new added target */
		/* if the target is added after the rebuild, then ignore it */
		if (comp->co_in_ver > allow_version)
			return true;

		/* Only counted in for post rebuild */
		if (gen_mode != POST_REBUILD)
			return true;
	}
	return false;
}

#define dom_is_skipped(dom, allow_ver, gen_mode)                                                   \
	comp_is_skipped(&(dom)->do_comp, allow_ver, gen_mode)
#define tgt_is_skipped(tgt, allow_ver, gen_mode)                                                   \
	comp_is_skipped(&(tgt)->ta_comp, allow_ver, gen_mode)

/* TODO: shouldn't compute each time, it can be very slow for extension of large pool.
 * This should be optimized in the future.
 */
static inline uint32_t
dom_avail_children(struct pool_domain *curr_dom, uint32_t allow_version,
		   enum layout_gen_mode gen_mode, pool_comp_type_t fdom_lvl)
{
	struct pool_domain *next_dom;
	struct pool_target *next_target;
	uint32_t num_dom;

	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl)
		num_dom = curr_dom->do_target_nr;
	else
		num_dom = curr_dom->do_child_nr;

	D_ASSERTF(num_dom > 0, "num dom %u\n", num_dom);
	/* new children(domains/targets) are always appended to old children of the same parent,
	 * this is why it does backward search.
	 */
	if (curr_dom->do_children == NULL || curr_dom->do_comp.co_type == fdom_lvl) {
		next_target = &curr_dom->do_targets[num_dom - 1];

		while (num_dom - 1 > 0 && tgt_is_skipped(next_target, allow_version, gen_mode)) {
			num_dom--;
			next_target = &curr_dom->do_targets[num_dom - 1];
		}
	} else {
		next_dom = &curr_dom->do_children[num_dom - 1];

		while (num_dom - 1 > 0 && dom_is_skipped(next_dom, allow_version, gen_mode)) {
			num_dom--;
			next_dom = &curr_dom->do_children[num_dom - 1];
		}
	}

	return num_dom;
}

static bool
tgt_isset_range(struct pool_target *tgts, uint8_t *tgts_used, uint32_t start_tgt,
		uint32_t end_tgt, uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	uint32_t index;

	for (index = start_tgt; index <= end_tgt;) {
		if (tgts_used[index >> 3] == 0xFF) {
			index = (index | 7) + 1; /* jump to start of next byte */
			continue;
		}

		if (isclr(tgts_used, index) &&
		    !tgt_is_skipped(&tgts[index], allow_version, gen_mode))
			return false;
		++index;
	}
	return true;
}

static bool
dom_isset_range(struct pool_domain *doms, uint8_t *doms_bits, uint32_t start_dom,
		uint32_t end_dom, uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	uint32_t index;

	for (index = start_dom; index <= end_dom;) {
		if (doms_bits[index >> 3] == 0xFF) {
			index = (index | 7) + 1; /* jump to start of next byte */
			continue;
		}

		if (isclr(doms_bits, index) &&
		    !dom_is_skipped(&doms[index], allow_version, gen_mode))
			return false;
		++index;
	}
	return true;
}

static bool
dom_isset_2ranges(struct pool_domain *doms, uint8_t *doms_bits1, uint8_t *doms_bits2,
		  uint32_t start_dom, uint32_t end_dom, uint32_t allow_version,
		  enum layout_gen_mode gen_mode)
{
	uint32_t index;

	for (index = start_dom; index <= end_dom;) {
		if (doms_bits1[index >> 3] == 0xFF || doms_bits2[index >> 3] == 0xFF) {
			index = (index | 7) + 1; /* jump to start of next byte */
			continue;
		}

		if (isclr(doms_bits1, index) && isclr(doms_bits2, index) &&
		    !dom_is_skipped(&doms[index], allow_version, gen_mode))
			return false;
		++index;
	}
	return true;
}

static bool
dom_is_full(struct pool_domain *dom, struct pool_domain *root, uint8_t *dom_full,
	    uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	uint32_t start_dom = dom->do_children - root;
	uint32_t end_dom = start_dom + (dom->do_child_nr - 1);

	if (dom_isset_range(root, dom_full, start_dom, end_dom, allow_version, gen_mode))
		return true;

	return false;
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
 * \param[in]	allow_version	allowed pool map version to generate the layout.
 * \param[in]	gen_mode	layout generation mode(PRE_REBUILD, CURRENT, POST_REBUILD).
 * \param[in]   fdom_lvl	failure domain of the current pool map
 * \param[in]   grp_size	object group size.
 * \param[out]  pd_ignored	true means the PD restrict is ignored inside the loop
 */
#define MAX_STACK	5
static void
get_target_v1(struct pool_domain *root_pos, struct pool_domain *curr_pd,
	      struct pool_target **target, struct pool_domain **dom, uint64_t obj_key,
	      uint8_t *dom_used, uint8_t *dom_full, uint8_t *dom_cur_grp_used, uint8_t *tgts_used,
	      int shard_num, uint32_t allow_version, enum layout_gen_mode gen_mode,
	      pool_comp_type_t fdom_lvl, uint32_t grp_size, bool *pd_ignored)
{
	int                     range_set;
	uint8_t                 found_target = 0;
	struct pool_domain      *curr_dom;
	struct pool_domain	*dom_stack[MAX_STACK] = { 0 };
	struct pool_target      *tgt;
	int			top = -1;

	obj_key = crc(obj_key, shard_num);
	curr_dom = curr_pd;
	do {
		uint32_t children; /* sub-domains or targets */

		/* Retrieve number of nodes in this domain */
		children = dom_avail_children(curr_dom, allow_version, gen_mode, fdom_lvl);

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
						    start_tgt, end_tgt, allow_version, gen_mode);
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
			selected_tgt = d_hash_jump(obj_key, children);
			do {
				selected_tgt = selected_tgt % children;
				tgt          = &curr_dom->do_targets[selected_tgt];
				/* Get target id to check if target used */
				tgt_idx = tgt - root_pos->do_targets;
				selected_tgt++;
			} while (isset(tgts_used, tgt_idx));
			*target = tgt;

			setbit(tgts_used, tgt_idx);
			D_DEBUG(DB_PL, "selected tgt %d\n", tgt_idx);
			D_ASSERTF(isclr(dom_full, (uint32_t)(curr_dom - root_pos)),
				  "selected_tgt %u\n", (uint32_t)(curr_dom - root_pos));
			range_set = tgt_isset_range(root_pos->do_targets, tgts_used,
						    start_tgt, end_tgt, allow_version, gen_mode);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_full, curr_dom - root_pos);
				D_DEBUG(DB_PL, "dom %d used up\n", (int)(curr_dom - root_pos));
				/* Check and set if all of its parent are full */
				while(top != -1) {
					if (dom_is_full(dom_stack[top], root_pos, dom_full,
							allow_version, gen_mode)) {
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

			D_DEBUG(DB_TRACE, "start_dom %u end_dom %u\n", start_dom, end_dom);
			/* Check if all targets under the domain range has been
			 * used up (occupied), go back to its parent if it does.
			 */
			range_set =
			    dom_is_full(curr_dom, root_pos, dom_full, allow_version, gen_mode);
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
						      start_dom, end_dom, allow_version, gen_mode);
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
						      start_dom, end_dom, allow_version, gen_mode);
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
				selected_dom = d_hash_jump(key, children);
				key          = crc(key, fail_num++);
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

/**
 * This function is identical to get_target_v1, except how they call CRC functions,
 * the V1 function may cause a large number of hash collisions in certain cases.
 *
 * Search for jm_crc() and crc() to see how they differ.
 */
static void
get_target_v2(struct pool_domain *root_pos, struct pool_domain *curr_pd,
	      struct pool_target **target, struct pool_domain **dom, uint64_t obj_key,
	      uint8_t *dom_used, uint8_t *dom_full, uint8_t *dom_cur_grp_used, uint8_t *tgts_used,
	      int shard_num, uint32_t allow_version, enum layout_gen_mode gen_mode,
	      pool_comp_type_t fdom_lvl, uint32_t grp_size, bool *pd_ignored)
{
	int                 range_set;
	uint8_t             found_target = 0;
	struct pool_target *tgt;
	struct pool_domain *curr_dom;
	struct pool_domain *dom_stack[MAX_STACK] = {0};
	int                 top                  = -1;

	curr_dom = curr_pd;
	do {
		uint32_t children; /* sub-domains or targets */
		uint64_t key;

		children = dom_avail_children(curr_dom, allow_version, gen_mode, fdom_lvl);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_comp.co_type == fdom_lvl) {
			uint32_t fail_num = 0;
			uint32_t tgt_idx;
			uint32_t start_tgt;
			uint32_t end_tgt;
			uint32_t selected_tgt;

			start_tgt = curr_dom->do_targets - root_pos->do_targets;
			end_tgt   = start_tgt + (curr_dom->do_target_nr - 1);

			range_set = tgt_isset_range(root_pos->do_targets, tgts_used, start_tgt,
						    end_tgt, allow_version, gen_mode);
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
			key = jm_crc(obj_key, shard_num, fail_num++);
			/* Get target for shard */
			selected_tgt = d_hash_jump(key, children);
			do {
				selected_tgt = selected_tgt % children;
				tgt          = &curr_dom->do_targets[selected_tgt];
				/* Get target id to check if target used */
				tgt_idx = tgt - root_pos->do_targets;
				selected_tgt++;
			} while (isset(tgts_used, tgt_idx));
			*target = tgt;

			setbit(tgts_used, tgt_idx);
			D_DEBUG(DB_PL, "selected tgt %d\n", tgt_idx);
			D_ASSERTF(isclr(dom_full, (uint32_t)(curr_dom - root_pos)),
				  "selected_tgt %u\n", (uint32_t)(curr_dom - root_pos));
			range_set = tgt_isset_range(root_pos->do_targets, tgts_used, start_tgt,
						    end_tgt, allow_version, gen_mode);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_full, curr_dom - root_pos);
				D_DEBUG(DB_PL, "dom %d used up\n", (int)(curr_dom - root_pos));
				/* Check and set if all of its parent are full */
				while (top != -1) {
					if (dom_is_full(dom_stack[top], root_pos, dom_full,
							allow_version, gen_mode)) {
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
			uint32_t selected_dom;
			uint32_t fail_num = 0;
			uint32_t start_dom;
			uint32_t end_dom;

			start_dom = (curr_dom->do_children) - root_pos;
			end_dom   = start_dom + (curr_dom->do_child_nr - 1);

			D_DEBUG(DB_TRACE, "start_dom %u end_dom %u\n", start_dom, end_dom);
			/* Check if all targets under the domain range has been
			 * used up (occupied), go back to its parent if it does.
			 */
			range_set =
			    dom_is_full(curr_dom, root_pos, dom_full, allow_version, gen_mode);
			if (range_set) {
				if (top == -1) {
					if (curr_pd != root_pos) {
						/* all domains within the PD are full, ignore the
						 * PD restrict.
						 */
						D_DEBUG(DB_PL,
							"PD[%d] all doms are full, weak the "
							"PD restrict\n",
							(int)(curr_dom - root_pos));
						curr_pd     = root_pos;
						curr_dom    = curr_pd;
						*pd_ignored = true;
						continue;
					}
					/* shard nr > target nr, no extra target for the shard */
					*target = NULL;
					return;
				}
				setbit(dom_full, curr_dom - root_pos);
				D_DEBUG(DB_PL, "used up dom %d\n", (int)(curr_dom - root_pos));
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			/* Check if all children has been used for the current group or full */
			range_set = dom_isset_2ranges(root_pos, dom_full, dom_cur_grp_used,
						      start_dom, end_dom, allow_version, gen_mode);
			if (range_set) {
				if (top == -1) {
					if (curr_pd != root_pos && grp_size > 1) {
						/* All domains within the PD are full, ignore the
						 * PD restrict. For non-replica (grp_size 1) case,
						 * keep the PD restrict until all targets under the
						 * domain range has been used up (see above check
						 * for dom_full bitmap).
						 */
						D_DEBUG(DB_PL,
							"PD[%d] all doms are full, weak the "
							"PD restrict\n",
							(int)(curr_dom - root_pos));
						curr_pd     = root_pos;
						curr_dom    = curr_pd;
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
						      start_dom, end_dom, allow_version, gen_mode);
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
				key          = jm_crc(obj_key, shard_num, fail_num++);
				selected_dom = d_hash_jump(key, children);
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
			curr_dom       = &curr_dom->do_children[selected_dom];
			obj_key        = jm_crc(obj_key, curr_dom->do_comp.co_id, 0xbabecafe);
		}
	} while (!found_target);
}

/* Only reset all used dom/tgts bits if all domain has been used. */
static void
dom_reset_bit(struct pool_domain *dom, uint8_t *dom_bits, struct pool_domain *root,
	      uint32_t allow_version, enum layout_gen_mode gen_mode, uint32_t fdom_lvl)
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
					     end_dom, allow_version, gen_mode))
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
	       struct pool_domain *root, uint32_t allow_version, enum layout_gen_mode gen_mode,
	       uint32_t fdom_lvl)
{
	int i;

	dom_reset_bit(dom, dom_bits, root, allow_version, gen_mode, fdom_lvl);

	/* check and reset all used tgts bits under the domain */
	for (i = 0; i < dom->do_target_nr; i++) {
		if (!isset(tgts_used, &dom->do_targets[i] - root->do_targets))
			return;
	}
	for (i = 0; i < dom->do_target_nr; i++)
		clrbit(tgts_used, &dom->do_targets[i] - root->do_targets);
}

static bool
dom_tgts_are_avaible(struct pool_domain *dom, uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	int i;

	for (i = 0; i < dom->do_target_nr; i++) {
		struct pool_target *tgt;

		tgt = &dom->do_targets[i];
		if (!comp_need_remap(&tgt->ta_comp, allow_version, gen_mode, NULL))
			return true;
	}
	return false;
}

/* Reset dom/targets tracking bits for remapping the layout */
static void
dom_reset_cur_grp(struct pool_domain *root, struct pool_domain *curr_pd, uint8_t *dom_cur_grp_used,
		  uint8_t *dom_cur_grp_real, uint8_t *dom_full, uint8_t *tgts_used,
		  uint32_t fdom_lvl, uint32_t allow_version, enum layout_gen_mode gen_mode)
{
	struct pool_domain	*tree;
	uint32_t		dom_nr;

	tree = curr_pd;
	dom_nr = 1;
	D_DEBUG(DB_PL, "bitmap resetting... curr_pd at dom[%d] (0 is root) gen %u\n",
		(int)(curr_pd - root), gen_mode);
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
				       start_dom, end_dom, allow_version, gen_mode))
			break;

		/* If there are domains, which are not used by this group,
		 * let's reset the full bits first, which might cause multiple shards
		 * from the same object in the same target.
		 */
		if (!dom_isset_range(root, dom_cur_grp_used, start_dom, end_dom,
				     allow_version, gen_mode)) {
			for (i = 0; i < dom_nr; i++) {
				uint32_t		dom_off = start_dom + i;
				struct pool_domain	*dom = &root[dom_off];

				if (!isset(dom_cur_grp_used, dom_off) &&
				    isset(dom_full, dom_off))
					dom_reset_full(dom, dom_full, tgts_used, root,
						       allow_version, gen_mode, fdom_lvl);
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
						     start_tgt, end_tgt, allow_version, gen_mode)) {
					dom_reset_bit(dom, dom_cur_grp_used, root,
						      allow_version, gen_mode, fdom_lvl);
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
			    dom_tgts_are_avaible(dom, allow_version, gen_mode)) {
				dom_reset_full(dom, dom_full, tgts_used, root,
					       allow_version, gen_mode, fdom_lvl);
				dom_reset_bit(dom, dom_cur_grp_used, root,
					      allow_version, gen_mode, fdom_lvl);
				reset = true;
			}
		}

		if (reset)
			break;

		/* Finally reset cur_group_used, which  multiple shards
		 * from the same group be in the same domain.
		 */
		if (dom_isset_range(root, dom_full, start_dom, end_dom, allow_version, gen_mode))
			reset_full = true;

		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *dom = &root[start_dom + i];

			if (reset_full)
				dom_reset_full(dom, dom_full, tgts_used, root,
					       allow_version, gen_mode, fdom_lvl);

			dom_reset_bit(dom, dom_cur_grp_used, root, allow_version, gen_mode,
				      fdom_lvl);
		}
		break;
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
#define MAX_STACK 5
void
get_target(struct pool_domain *root, struct pool_domain *curr_pd, uint32_t layout_ver,
	   struct pool_target **target, struct pool_domain **dom, uint64_t key, uint8_t *dom_used,
	   uint8_t *dom_full, uint8_t *dom_cur_grp_used, uint8_t *dom_cur_grp_real,
	   uint8_t *tgts_used, int shard_num, uint32_t allow_version, enum layout_gen_mode gen_mode,
	   pool_comp_type_t fdom_lvl, uint32_t grp_size)
{
	struct pool_target *found = NULL;
	bool                pd_ignored;

	/* For extending case, it needs to get the layout in two cases, with UP/NEW target
	 * and without UP/NEW targets, then tell the difference, see placement/jump_map.c.
	 */
	/* NB: It does not tell other status of the target in this function, to make sure
	 * the target is mapped according to the failure sequence strictly. And other target
	 * status check is done in get_object_layout().
	 */
	while (found == NULL) {
		pd_ignored = false;
		if (layout_ver == 1) {
			get_target_v1(root, curr_pd, &found, dom, key, dom_used, dom_full,
				      dom_cur_grp_used, tgts_used, shard_num, allow_version,
				      gen_mode, fdom_lvl, grp_size, &pd_ignored);
		} else { /* Fix CRC calls */
			D_ASSERT(layout_ver == 2);
			get_target_v2(root, curr_pd, &found, dom, key, dom_used, dom_full,
				      dom_cur_grp_used, tgts_used, shard_num, allow_version,
				      gen_mode, fdom_lvl, grp_size, &pd_ignored);
		}
		if (found) {
			*target = found;
			return;
		}
		dom_reset_cur_grp(root, pd_ignored ? root : curr_pd, dom_cur_grp_used,
				  dom_cur_grp_real, dom_full, tgts_used, fdom_lvl, allow_version,
				  gen_mode);
	}
}
