/**
 *
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>
#include <isa-l.h>


/*
 * These ops determine whether extra information is calculated during
 * placement.
 *
 * PL_PLACE_EXTENDED calculates an extended layout for use when there
 * is a reintegration operation currently ongoing.
 *
 * PL_REINT calculates the post-reintegration layout for use during
 * reintegration, it treats the UP status targets as UP_IN.
 *
 * Currently the other OP types calculate a normal layout without extra info.
 */
enum PL_OP_TYPE {
	PL_PLACE,
	PL_PLACE_EXTENDED,
	PL_REBUILD,
	PL_REINT,
	PL_ADD,
};

/**
 * Contains information related to object layout size.
 */
struct jm_obj_placement {
	unsigned int    jmop_grp_size;
	unsigned int    jmop_grp_nr;
};

/**
 * jump_map Placement map structure used to place objects.
 * The map is returned as a struct pl_map and then converted back into a
 * pl_jump_map once passed from the caller into the object placement
 * functions.
 */
struct pl_jump_map {
	/** placement map interface */
	struct pl_map		jmp_map;
	/* Total size of domain type specified during map creation */
	unsigned int		jmp_domain_nr;
	/* # UPIN targets */
	unsigned int		jmp_target_nr;
	/* The dom that will contain no colocated shards */
	pool_comp_type_t	jmp_redundant_dom;
};

/**
 * This functions finds the pairwise differences in the two layouts provided
 * and appends them into the d_list provided. The function appends the targets
 * from the "new" layout and not those from the "original" layout.
 *
 * \param[in]	jmap		A pointer to the jump map used to retrieve a
 *				reference to the pool map target.
 * \param[in]	original	The original layout calculated not including any
 *				recent pool map changes, like reintegration.
 * \param[in]	new		The new layout that contains changes in layout
 *				that occurred due to pool status changes.
 * \param[out]	diff		The d_list that contains the differences that
 *				were calculated.
 */
static inline void
layout_find_diff(struct pl_jump_map *jmap, struct pl_obj_layout *original,
		 struct pl_obj_layout *new, d_list_t *diff)
{
	int index;

	/* We assume they are the same size */
	D_ASSERT(original->ol_nr == new->ol_nr);

	for (index = 0; index < original->ol_nr; ++index) {
		uint32_t original_target = original->ol_shards[index].po_target;
		uint32_t reint_tgt = new->ol_shards[index].po_target;
		struct pool_target *temp_tgt;

		if (reint_tgt != original_target) {
			pool_map_find_target(jmap->jmp_map.pl_poolmap,
					     reint_tgt, &temp_tgt);
			if (pool_target_avail(temp_tgt, PO_COMP_ST_UPIN |
							PO_COMP_ST_UP |
							PO_COMP_ST_DRAIN |
							PO_COMP_ST_NEW))
				remap_alloc_one(diff, index, temp_tgt, true, NULL);
			else
				/* XXX: This isn't desirable - but it can happen
				 * when a reintegration is happening when
				 * something else fails. Placement will do a
				 * pass to determine what failed (good), and
				 * then do another pass to figure out where
				 * things moved to. But that 2nd pass will
				 * re-find failed things, and this diff function
				 * will cause the failed targets to be re-added
				 * to the layout as rebuilding. This should be
				 * removed when placement is able to handle
				 * this situation better
				 */
				D_DEBUG(DB_PL,
					"skip remap %d to unavail tgt %u\n",
					index, reint_tgt);

		}
	}
}
/**
 * This is useful for jump_map placement to pseudorandomly permute input keys
 * that are similar to each other. This dramatically improves the even-ness of
 * the distribution of output placements.
 */
static inline uint64_t
crc(uint64_t data, uint32_t init_val)
{
	return crc64_ecma_refl(init_val, (uint8_t *)&data, sizeof(data));
}

/**
 * This function gets the replication and size requirements and then
 * stores those requirements into a obj_placement  struct for usage during
 * layout creation.
 *
 * \param[in] jmap      The placement map for jump_map Placement, used for
 *                      retrieving domain requirements for layout.
 * \param[in] md        Object metadata used for retrieve information
 *                      about the object class.
 * \param[in] shard_md  Shard metadata used for determining the group number
 * \param[out] jmop     Stores layout requirements for use later in placement.
 *
 * \return              Return code, 0 for success and an error code if the
 *                      layout requirements could not be determined / satisfied.
 */
static int
jm_obj_placement_get(struct pl_jump_map *jmap, struct daos_obj_md *md,
		     struct daos_obj_shard_md *shard_md,
		     struct jm_obj_placement *jmop)
{
	struct daos_oclass_attr *oc_attr;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	int                     rc;

	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid, NULL);

	if (oc_attr == NULL) {
		D_ERROR("Can not find obj class, invalid oid="DF_OID"\n",
			DP_OID(oid));
		return -DER_INVAL;
	}

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);

	rc = op_get_grp_size(jmap->jmp_domain_nr, &jmop->jmop_grp_size, oid);
	if (rc)
		return rc;

	if (shard_md == NULL) {
		unsigned int grp_max = root->do_target_nr / jmop->jmop_grp_size;

		if (grp_max == 0)
			grp_max = 1;

		jmop->jmop_grp_nr = daos_oclass_grp_nr(oc_attr, md);
		if (jmop->jmop_grp_nr == DAOS_OBJ_GRP_MAX)
			jmop->jmop_grp_nr = grp_max;
		else if (jmop->jmop_grp_nr > grp_max)
			return -DER_INVAL;
	} else {
		jmop->jmop_grp_nr = 1;
	}

	D_ASSERT(jmop->jmop_grp_nr > 0);
	D_ASSERT(jmop->jmop_grp_size > 0);

	D_DEBUG(DB_PL,
		"obj="DF_OID"/ grp_size=%u grp_nr=%d\n",
		DP_OID(oid), jmop->jmop_grp_size, jmop->jmop_grp_nr);

	return 0;
}

/**
 * This function converts a generic pl_map pointer into the
 * proper placement map. This function assumes that the original
 * map was allocated as a pl_jump_map with a pl_map as it's
 * first member.
 *
 * \param[in]   *map    A pointer to a pl_map which is the first
 *                      member of the specific placement map we're
 *                      converting to.
 *
 * \return      A pointer to the full pl_jump_map used for
 *              object placement, rebuild, and reintegration
 */
static inline struct pl_jump_map *
pl_map2jmap(struct pl_map *map)
{
	return container_of(map, struct pl_jump_map, jmp_map);
}

static void debug_print_allow_status(uint32_t allow_status)
{
	D_DEBUG(DB_PL, "Allow status: [%s%s%s%s%s%s%s ]\n",
		allow_status & PO_COMP_ST_UNKNOWN ? " UNKNOWN" : "",
		allow_status & PO_COMP_ST_NEW ? " NEW" : "",
		allow_status & PO_COMP_ST_UP ? " UP" : "",
		allow_status & PO_COMP_ST_UPIN ? " UPIN" : "",
		allow_status & PO_COMP_ST_DOWN ? " DOWN" : "",
		allow_status & PO_COMP_ST_DOWNOUT ? " DOWNOUT" : "",
		allow_status & PO_COMP_ST_DRAIN ? " DRAIN" : "");
}

static inline uint32_t
get_num_domains(struct pool_domain *curr_dom, uint32_t allow_status)
{
	struct pool_domain *next_dom;
	struct pool_target *next_target;
	uint32_t num_dom;
	uint8_t status;

	if (curr_dom->do_children == NULL)
		num_dom = curr_dom->do_target_nr;
	else
		num_dom = curr_dom->do_child_nr;

	if (allow_status & PO_COMP_ST_NEW)
		return num_dom;

	if (curr_dom->do_children != NULL) {
		next_dom = &curr_dom->do_children[num_dom - 1];
		status = next_dom->do_comp.co_status;

		while (num_dom - 1 > 0 && status == PO_COMP_ST_NEW) {
			num_dom--;
			next_dom = &curr_dom->do_children[num_dom - 1];
			status = next_dom->do_comp.co_status;
		}
	} else {
		next_target = &curr_dom->do_targets[num_dom - 1];
		status = next_target->ta_comp.co_status;

		while (num_dom - 1 > 0 && status == PO_COMP_ST_NEW) {
			num_dom--;
			next_target = &curr_dom->do_targets[num_dom - 1];
			status = next_target->ta_comp.co_status;
		}
	}

	return num_dom;
}

static void
reset_dom_cur_grp(uint8_t *dom_cur_grp_used, uint8_t *dom_occupied, uint32_t dom_size)
{
	int i;

	for (i = 0; i < dom_size; i++) {
		if (isset(dom_occupied, i))
			/* if all targets used up, this dom will not be used anyway */
			setbit(dom_cur_grp_used, i);
		else
			/* otherwise reset it */
			clrbit(dom_cur_grp_used, i);
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
get_target(struct pool_domain *curr_dom, struct pool_target **target,
	   uint64_t obj_key, uint8_t *dom_used, uint8_t *dom_occupied,
	   uint8_t *dom_cur_grp_used, uint8_t *tgts_used, int shard_num,
	   uint32_t allow_status)
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
		num_doms = get_num_domains(curr_dom, allow_status);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_children == NULL) {
			uint32_t        fail_num = 0;
			uint32_t        dom_id;
			uint32_t        start_tgt;
			uint32_t        end_tgt;

			start_tgt = curr_dom->do_targets[0].ta_comp.co_id;
			end_tgt = start_tgt + (num_doms - 1);

			range_set = isset_range(tgts_used, start_tgt, end_tgt);
			if (range_set) {
				/* Used up all targets in this domain */
				setbit(dom_occupied, curr_dom - root_pos);
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
					reset_dom_cur_grp(dom_cur_grp_used, dom_occupied, dom_size);
					goto retry;
				}
				setbit(dom_cur_grp_used, curr_dom - root_pos);
				curr_dom = dom_stack[top--];
				continue;
			}

			range_set = isset_range(dom_used, start_dom, end_dom);
			if (range_set) {
				int idx;
				bool reset_used = false;

				/* Skip the domain whose targets are used up */
				for (idx = start_dom; idx <= end_dom; ++idx) {
					/* Only reused the domain, if there are still targets
					 * available (not being used) within this domain, and the
					 * domain has not being used by current group yet. so
					 * 1. there won't be multiple shards in the same target.
					 * 2. there won't be multiple shards within same group
					 *    are in the same domain.
					 */
					if (isclr(dom_occupied, idx) &&
					    isclr(dom_cur_grp_used, idx)) {
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
					/* If no used dom is being reset, then let's reset
					 * dom_cur_grp_used and start put multiple same group
					 * in the same domain.
					 */
					if (!reset_used)
						reset_dom_cur_grp(dom_cur_grp_used, dom_occupied,
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
			curr_dom = &(curr_dom->do_children[selected_dom]);
			obj_key = crc(obj_key, curr_dom->do_comp.co_id);
		}
	} while (!found_target);
}

uint32_t
count_available_spares(struct pl_jump_map *jmap, struct pl_obj_layout *layout,
		uint32_t failed_in_layout)
{
	uint32_t unusable_tgts;
	uint32_t num_targets;

	num_targets =  pool_map_find_target(jmap->jmp_map.pl_poolmap,
			PO_COMP_ID_ALL, NULL);

	/* we might not have any valid targets left at all */
	unusable_tgts = layout->ol_nr;

	if (unusable_tgts >= num_targets)
		return 0;

	return num_targets - unusable_tgts;
}

struct dom_grp_used {
	uint8_t		*dgu_used;
	d_list_t	dgu_list;
};

/**
* Try to remap all the failed shards in the @remap_list to proper
* targets. The new target id will be updated in the @layout if the
* remap succeeds, otherwise, corresponding shard and target id in
* @layout will be cleared as -1.
*
* \paramp[in]   jmap            The placement map being used for placement.
* \paramp[in]   md              Object Metadata.
* \paramp[in]   layout          The original layout which contains some shards
*                               on failed targets.
* \paramp[in]   jmop            Structure containing information related to
*                               layout characteristics.
* \paramp[in]   remap_list      List containing shards to be remapped sorted
*                               by failure sequence.
* \paramp[in]   dom_used        Bookkeeping array used to keep track of which
*                               domain components have already been used.
*
* \return       return an error code signifying whether the shards were
*               successfully remapped properly.
*/
static int
obj_remap_shards(struct pl_jump_map *jmap, struct daos_obj_md *md,
		 struct pl_obj_layout *layout, struct jm_obj_placement *jmop,
		 d_list_t *remap_list, uint32_t allow_status,
		 uint8_t *tgts_used, uint8_t *dom_used, uint8_t *dom_occupied,
		 uint32_t failed_in_layout, bool *is_extending)
{
	struct failed_shard     *f_shard;
	struct pl_obj_shard     *l_shard;
	struct pool_target      *spare_tgt;
	struct pool_domain      *root;
	d_list_t                *current;
	daos_obj_id_t           oid;
	bool                    spare_avail = true;
	uint64_t                key;
	uint32_t		spares_left;
	int                     rc;


	remap_dump(remap_list, md, "remap:");

	current = remap_list->next;
	spare_tgt = NULL;
	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	spares_left = count_available_spares(jmap, layout, failed_in_layout);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);
	while (current != remap_list) {
		uint64_t rebuild_key;
		uint32_t shard_id;

		f_shard = d_list_entry(current, struct failed_shard, fs_list);

		shard_id = f_shard->fs_shard_idx;
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];
		D_DEBUG(DB_PL, "Attempting to remap failed shard: "
			DF_FAILEDSHARD"\n", DP_FAILEDSHARD(*f_shard));
		debug_print_allow_status(allow_status);

		D_ASSERT(f_shard->fs_data != NULL);
		/*
		 * If there are any targets left, there are potentially valid
		 * spares. Don't be picky here about refusing to accept a
		 * potential spare because of doubling up in the same fault
		 * domain - if this is the case, fault tolerance is already at
		 * risk because of failures up to this point. Rebuilding data
		 * on a non-redundant other fault domain won't make this worse.
		 */
		spare_avail = spares_left > 0;
		if (spare_avail) {
			struct dom_grp_used *dgu = f_shard->fs_data;

			D_ASSERT(dgu != NULL);
			rebuild_key = crc(key, f_shard->fs_shard_idx);
			get_target(root, &spare_tgt, crc(key, rebuild_key),
				   dom_used, dom_occupied,
				   dgu->dgu_used, tgts_used,
				   shard_id, allow_status);
			D_ASSERT(spare_tgt != NULL);
			D_DEBUG(DB_PL, "Trying new target: "DF_TARGET"\n",
				DP_TARGET(spare_tgt));
			spares_left--;
		}

		determine_valid_spares(spare_tgt, md, spare_avail, &current,
				       remap_list, allow_status, f_shard,
				       l_shard, is_extending);
	}

	return 0;
}

static int
jump_map_obj_spec_place_get(struct pl_jump_map *jmap, daos_obj_id_t oid,
			    struct pool_target **target, uint8_t *dom_used,
			    uint32_t dom_bytes)
{
	struct pool_target      *tgts;
	struct pool_domain      *current_dom;
	struct pool_domain      *root;
	unsigned int            pos;
	int                     rc;

	tgts = pool_map_targets(jmap->jmp_map.pl_poolmap);

	rc = spec_place_rank_get(&pos, oid, (jmap->jmp_map.pl_poolmap));
	if (rc)
		return rc;

	*target = &(tgts[pos]);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);
	current_dom = root;

	/* Update collision map to account for this shard. */
	while (current_dom->do_children != NULL) {
		int index;
		uint64_t child_pos;

		child_pos = (current_dom->do_children) - root;

		for (index = 0; index < current_dom->do_child_nr; ++index) {
			struct pool_domain *temp_dom;
			int num_children;
			int last;
			struct pool_target *start;
			struct pool_target *end;

			temp_dom = &(current_dom->do_children[index]);
			num_children = temp_dom->do_target_nr;
			last = num_children - 1;

			start = &(temp_dom->do_targets[0]);
			end = &(temp_dom->do_targets[last]);

			if ((start <= (*target)) && ((*target) <= end)) {
				current_dom = temp_dom;
				setbit(dom_used, (index + child_pos));
				break;
			}
		}
	}

	return 0;
}

static struct dom_grp_used*
remap_gpu_alloc_one(d_list_t *remap_list, uint8_t *dom_cur_grp_used)
{
	struct dom_grp_used	*dgu;

	D_ALLOC_PTR(dgu);
	if (dgu == NULL)
		return NULL;

	dgu->dgu_used = dom_cur_grp_used;
	d_list_add_tail(&dgu->dgu_list, remap_list);
	return dgu;
}

/**
 * This function handles getting the initial layout for the object as well as
 * determining if there are targets that are unavailable.
 *
 * \param[in]   jmap            The placement map used for this placement.
 * \param[in]   jmop            The layout group size and count.
 * \param[in]   md              Object metadata.
 * \param[in]	allow_status	target status allowed to be in the layout.
 * \param[out]  layout          This will contain the layout for the object
 * \param[out]  out_list	This will contain the targets that need to
 *                              be rebuilt and in the case of rebuild, may be
 *                              returned during the rebuild process.
 * \param[out]	is_extending	if there is drain/extending/reintegrating tgts
 *                              exists in this layout, which we might need
 *                              insert extra shards into the layout.
 *
 * \return                      An error code determining if the function
 *                              succeeded (0) or failed.
 */
#define	LOCAL_DOM_ARRAY_SIZE	2
#define	LOCAL_TGT_ARRAY_SIZE	4
static int
get_object_layout(struct pl_jump_map *jmap, struct pl_obj_layout *layout,
		  struct jm_obj_placement *jmop, d_list_t *out_list,
		  uint32_t allow_status, struct daos_obj_md *md,
		  bool *is_extending)
{
	struct pool_target      *target;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	uint8_t                 *dom_used = NULL;
	uint8_t                 *dom_occupied = NULL;
	uint8_t                 *tgts_used = NULL;
	uint8_t			*dom_cur_grp_used = NULL;
	uint8_t			dom_used_array[LOCAL_DOM_ARRAY_SIZE] = { 0 };
	uint8_t			dom_occupied_array[LOCAL_DOM_ARRAY_SIZE] = { 0 };
	uint8_t			tgts_used_array[LOCAL_TGT_ARRAY_SIZE] = { 0 };
	d_list_t		dgu_remap_list;
	uint32_t                dom_size;
	uint32_t                dom_array_size;
	uint64_t                key;
	uint32_t		fail_tgt_cnt = 0;
	bool			spec_oid = false;
	bool			realloc_grp_used = true;
	d_list_t		local_list;
	d_list_t		*remap_list;
	struct dom_grp_used	*dgu;
	struct dom_grp_used	*tmp;
	int			i, j, k;
	int			rc = 0;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(&(jmap->jmp_map));
	D_DEBUG(DB_PL, "Building layout. map version: %d\n", layout->ol_ver);
	debug_print_allow_status(allow_status);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		return -DER_NONEXIST;
	}
	rc = 0;

	D_INIT_LIST_HEAD(&local_list);
	D_INIT_LIST_HEAD(&dgu_remap_list);
	if (out_list != NULL)
		remap_list = out_list;
	else
		remap_list = &local_list;

	dom_size = (struct pool_domain *)(root->do_targets) - (root) + 1;
	dom_array_size = dom_size/NBBY + 1;
	if (dom_array_size > LOCAL_DOM_ARRAY_SIZE) {
		D_ALLOC_ARRAY(dom_used, dom_array_size);
		D_ALLOC_ARRAY(dom_occupied, dom_array_size);
	} else {
		dom_used = dom_used_array;
		dom_occupied = dom_occupied_array;
	}

	if (root->do_target_nr / NBBY + 1 > LOCAL_TGT_ARRAY_SIZE)
		D_ALLOC_ARRAY(tgts_used, (root->do_target_nr / NBBY) + 1);
	else
		tgts_used = tgts_used_array;

	if (dom_used == NULL || dom_occupied == NULL || tgts_used == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	if (daos_obj_is_srank(oid))
		spec_oid = true;

	for (i = 0, k = 0; i < jmop->jmop_grp_nr; i++) {
		struct dom_grp_used  *remap_grp_used = NULL;

		if (realloc_grp_used) {
			realloc_grp_used = false;
			D_ALLOC_ARRAY(dom_cur_grp_used, dom_array_size);
			if (dom_cur_grp_used == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			memset(dom_cur_grp_used, 0, dom_array_size);
		}

		for (j = 0; j < jmop->jmop_grp_size; j++, k++) {
			target = NULL;
			if (spec_oid && i == 0 && j == 0) {
				/**
				 * If the object class is a special class then
				 * the first shard must be picked specially.
				 */
				rc = jump_map_obj_spec_place_get(jmap, oid,
								 &target,
								 dom_used,
								 dom_size);
				if (rc) {
					D_ERROR("special oid "DF_OID
						" failed: rc %d\n",
						DP_OID(oid), rc);
					D_GOTO(out, rc);
				}
				setbit(tgts_used, target->ta_comp.co_id);
			} else {
				get_target(root, &target, key, dom_used,
					   dom_occupied, dom_cur_grp_used,
					   tgts_used, k, allow_status);
			}

			if (target == NULL) {
				D_DEBUG(DB_PL, "no targets for %d/%d/%d\n",
					i, j, k);
				layout->ol_shards[k].po_target = -1;
				layout->ol_shards[k].po_shard = -1;
				layout->ol_shards[k].po_fseq = 0;
				continue;
			}
			layout->ol_shards[k].po_target =
				target->ta_comp.co_id;
			layout->ol_shards[k].po_fseq =
				target->ta_comp.co_fseq;
			layout->ol_shards[k].po_shard = k;

			/** If target is failed queue it for remap*/
			if (!pool_target_avail(target, allow_status)) {
				fail_tgt_cnt++;
				D_DEBUG(DB_PL, "Target unavailable " DF_TARGET
					". Adding to remap_list: fail cnt %d\n",
					DP_TARGET(target), fail_tgt_cnt);

				if (remap_grp_used == NULL) {
					remap_grp_used = remap_gpu_alloc_one(&dgu_remap_list,
									     dom_cur_grp_used);
					if (remap_grp_used == NULL)
						D_GOTO(out, rc = -DER_NOMEM);
					realloc_grp_used = true;
				}

				rc = remap_alloc_one(remap_list, k, target, false, remap_grp_used);
				if (rc)
					D_GOTO(out, rc);

				if (is_extending != NULL &&
				    (target->ta_comp.co_status ==
				     PO_COMP_ST_UP ||
				     target->ta_comp.co_status ==
				     PO_COMP_ST_DRAIN))
					*is_extending = true;
			}
		}
	}

	if (fail_tgt_cnt > 0)
		rc = obj_remap_shards(jmap, md, layout, jmop, remap_list,
				      allow_status, tgts_used, dom_used,
				      dom_occupied, fail_tgt_cnt, is_extending);
out:
	if (rc)
		D_ERROR("jump_map_obj_layout_fill failed, rc "DF_RC"\n",
			DP_RC(rc));
	if (remap_list == &local_list)
		remap_list_free_all(&local_list);

	if (dom_cur_grp_used != NULL) {
		bool cur_grp_freed = false;

		d_list_for_each_entry_safe(dgu, tmp, &dgu_remap_list, dgu_list) {
			d_list_del(&dgu->dgu_list);
			if (dgu->dgu_used == dom_cur_grp_used)
				cur_grp_freed = true;
			D_FREE(dgu->dgu_used);
			D_FREE(dgu);
		}
		/* If dom_cur_grp_used is not attached to dgu, i.e. no targets needs
		 * be remapped, then free dom_cur_grp_used separately.
		 */
		if (!cur_grp_freed)
			D_FREE(dom_cur_grp_used);
	}

	if (dom_used && dom_used != dom_used_array)
		D_FREE(dom_used);
	if (dom_occupied && dom_occupied != dom_occupied_array)
		D_FREE(dom_occupied);
	if (tgts_used && tgts_used != tgts_used_array)
		D_FREE(tgts_used);

	return rc;
}

static int
obj_layout_alloc_and_get(struct pl_jump_map *jmap,
			 struct jm_obj_placement *jmop, struct daos_obj_md *md,
			 uint32_t allow_status, struct pl_obj_layout **layout_p,
			 d_list_t *remap_list, bool *is_extending)
{
	int rc;

	/* Allocate space to hold the layout */
	D_ASSERT(jmop->jmop_grp_size > 0);
	D_ASSERT(jmop->jmop_grp_nr > 0);
	rc = pl_obj_layout_alloc(jmop->jmop_grp_size, jmop->jmop_grp_nr,
				 layout_p);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = get_object_layout(jmap, *layout_p, jmop, remap_list, allow_status,
			       md, is_extending);
	if (rc) {
		D_ERROR("get object layout failed, rc "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (rc != 0) {
		if (*layout_p != NULL)
			pl_obj_layout_free(*layout_p);
		*layout_p = NULL;
	}
	return rc;
}

/**
 * Frees the placement map
 *
 * \param[in]   map     The placement map to be freed
 */
static void
jump_map_destroy(struct pl_map *map)
{
	struct pl_jump_map   *jmap;

	jmap = pl_map2jmap(map);

	if (jmap->jmp_map.pl_poolmap)
		pool_map_decref(jmap->jmp_map.pl_poolmap);

	D_FREE(jmap);
}

/**
 * This function allocates and initializes the placement map.
 *
 * \param[in]   poolmap The pool map to be used when calculating object
 *                      placement.
 * \param[in]   mia     placement map initialization values. Part of the
 *                      placement map API but currently not used in this map.
 * \param[in]   mapp    The placement map interface that will be passed out
 *                      and used when placing objects.
 *
 * \return              The error status of the function.
 */
static int
jump_map_create(struct pool_map *poolmap, struct pl_map_init_attr *mia,
		struct pl_map **mapp)
{
	struct pool_domain      *root;
	struct pl_jump_map      *jmap;
	struct pool_domain      *doms;
	int                     rc;

	D_ALLOC_PTR(jmap);
	if (jmap == NULL)
		return -DER_NOMEM;

	pool_map_addref(poolmap);
	jmap->jmp_map.pl_poolmap = poolmap;

	rc = pool_map_find_domain(poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		goto ERR;
	}

	jmap->jmp_redundant_dom = mia->ia_jump_map.domain;
	rc = pool_map_find_domain(poolmap, mia->ia_jump_map.domain,
				  PO_COMP_ID_ALL, &doms);
	if (rc <= 0) {
		rc = (rc == 0) ? -DER_INVAL : rc;
		goto ERR;
	}

	jmap->jmp_domain_nr = rc;
	rc = pool_map_find_upin_tgts(poolmap, NULL, &jmap->jmp_target_nr);
	if (rc) {
		D_ERROR("cannot find active targets: %d\n", rc);
		goto ERR;
	}
	*mapp = &jmap->jmp_map;
	return DER_SUCCESS;
ERR:
	jump_map_destroy(&jmap->jmp_map);
	return rc;
}

static void
jump_map_print(struct pl_map *map)
{
	/** Currently nothing to print */
}

static int
jump_map_query(struct pl_map *map, struct pl_map_attr *attr)
{
	struct pl_jump_map   *jmap = pl_map2jmap(map);

	attr->pa_type	   = PL_TYPE_JUMP_MAP;
	attr->pa_target_nr = jmap->jmp_target_nr;
	attr->pa_domain_nr = jmap->jmp_domain_nr;
	attr->pa_domain    = jmap->jmp_redundant_dom;
	return 0;
}

/**
 * Determines the locations that a given object shard should be located.
 *
 * \param[in]   map             A reference to the placement map being used to
 *                              place the object shard.
 * \param[in]   md              The object metadata which contains data about
 *                              the object being placed such as the object ID.
 * \param[in]   shard_md        Shard metadata.
 * \param[out]  layout_pp       The layout generated for the object. Contains
 *                              references to the targets in the pool map where
 *                              the shards will be placed.
 *
 * \return                      An integer value containing the error
 *                              code or 0 if the function returned
 *                              successfully.
 */
static int
jump_map_obj_place(struct pl_map *map, struct daos_obj_md *md,
		   struct daos_obj_shard_md *shard_md,
		   struct pl_obj_layout **layout_pp)
{
	struct pl_jump_map	*jmap;
	struct pl_obj_layout	*layout = NULL;
	struct pl_obj_layout	*extend_layout = NULL;
	struct jm_obj_placement	jmop;
	d_list_t		extend_list;
	bool			is_extending = false;
	bool			is_adding_new = false;
	daos_obj_id_t		oid;
	struct pool_domain	*root;
	uint32_t		allow_status;
	int			rc;

	jmap = pl_map2jmap(map);
	oid = md->omd_id;
	D_DEBUG(DB_PL, "Determining location for object: "DF_OID", ver: %d\n",
		DP_OID(oid), md->omd_ver);

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_INIT_LIST_HEAD(&extend_list);
	allow_status = PO_COMP_ST_UPIN;
	rc = obj_layout_alloc_and_get(jmap, &jmop, md, allow_status, &layout,
				      NULL, &is_extending);
	if (rc != 0) {
		D_ERROR("get_layout_alloc failed, rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	obj_layout_dump(oid, layout);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap,
				  PO_COMP_TP_ROOT, PO_COMP_ID_ALL,
				  &root);
	D_ASSERT(rc == 1);
	rc = 0;
	if (is_pool_adding(root))
		is_adding_new = true;

	/* If the layout might being extended, i.e. so extra shards needs
	 * to be added to the layout.
	 */
	if (unlikely(is_extending || is_adding_new)) {
		/* Needed to check if domains are being added to pool map */
		D_DEBUG(DB_PL, DF_OID"/%d is being added: %s or extended: %s\n",
			DP_OID(oid), md->omd_ver, is_adding_new ? "yes" : "no",
			is_extending ? "yes" : "no");

		if (is_adding_new)
			allow_status |= PO_COMP_ST_NEW;

		if (is_extending)
			allow_status |= PO_COMP_ST_UP | PO_COMP_ST_DRAIN;

		/* Don't repeat remapping failed shards during this phase -
		 * they have already been remapped.
		 */
		allow_status |= PO_COMP_ST_DOWN;
		rc = obj_layout_alloc_and_get(jmap, &jmop, md, allow_status,
					      &extend_layout, NULL, NULL);
		if (rc)
			D_GOTO(out, rc);

		obj_layout_dump(oid, extend_layout);
		layout_find_diff(jmap, layout, extend_layout, &extend_list);
		if (!d_list_empty(&extend_list)) {
			rc = pl_map_extend(layout, &extend_list);
			if (rc)
				D_GOTO(out, rc);
		}
		obj_layout_dump(oid, layout);
	}

	*layout_pp = layout;
out:
	remap_list_free_all(&extend_list);

	if (extend_layout != NULL)
		pl_obj_layout_free(extend_layout);

	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc "DF_RC"\n",
			DP_RC(rc));
		if (layout != NULL)
			pl_obj_layout_free(layout);
	}

	return rc;
}

/**
 *
 * \param[in]   map             The placement map to be used to generate the
 *                              placement layouts and to calculate rebuild
 *                              targets.
 * \param[in]   md              Metadata describing the object.
 * \param[in]   shard_md        Metadata describing how the shards.
 * \param[in]   rebuild_ver     Current Rebuild version
 * \param[out]   tgt_rank       The rank of the targets that need to be rebuild
 *                              will be stored in this array to be passed out
 *                              (this is allocated by the caller)
 * \param[out]   shard_id       The shard IDs of the shards that need to be
 *                              rebuilt (This is allocated by the caller)
 * \param[in]   array_size      The max size of the passed in arrays to store
 *                              info about the shards that need to be rebuilt.
 *
 * \return                      The number of shards that need to be rebuilt on
 *                              another target, Or 0 if none need to be rebuilt.
 */
static int
jump_map_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
			  struct daos_obj_shard_md *shard_md,
			  uint32_t rebuild_ver, uint32_t *tgt_id,
			  uint32_t *shard_idx, unsigned int array_size)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout;
	d_list_t                        remap_list;
	struct jm_obj_placement         jmop;
	daos_obj_id_t                   oid;
	int                             rc;

	int idx = 0;

	D_DEBUG(DB_PL, "Finding Rebuild at version: %u\n", rebuild_ver);

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < rebuild_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), rebuild_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2jmap(map);
	oid = md->omd_id;

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = obj_layout_alloc_and_get(jmap, &jmop, md, PO_COMP_ST_UPIN, &layout,
				      &remap_list, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	obj_layout_dump(oid, layout);
	rc = remap_list_fill(map, md, shard_md, rebuild_ver, tgt_id, shard_idx,
			     array_size, &idx, layout, &remap_list, false);

out:
	remap_list_free_all(&remap_list);
	if (layout != NULL)
		pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
			struct daos_obj_shard_md *shard_md,
			uint32_t reint_ver, uint32_t *tgt_rank,
			uint32_t *shard_id, unsigned int array_size)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout = NULL;
	struct pl_obj_layout            *reint_layout = NULL;
	d_list_t			reint_list;
	struct jm_obj_placement         jop;
	uint32_t			allow_status;
	int                             rc;

	int idx = 0;

	D_DEBUG(DB_PL, "Finding Reint at version: %u\n", reint_ver);

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < reint_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), reint_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2jmap(map);
	rc = jm_obj_placement_get(jmap, md, shard_md, &jop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	/* Ignore DOWN and DRAIN objects here - this API is only for finding
	 * reintegration candidates
	 */
	allow_status = PO_COMP_ST_UPIN | PO_COMP_ST_DOWN | PO_COMP_ST_DRAIN;
	D_INIT_LIST_HEAD(&reint_list);
	rc = obj_layout_alloc_and_get(jmap, &jop, md, allow_status, &layout,
				      NULL, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	allow_status |= PO_COMP_ST_UP;
	rc = obj_layout_alloc_and_get(jmap, &jop, md, allow_status,
				      &reint_layout, NULL, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	layout_find_diff(jmap, layout, reint_layout, &reint_list);

	rc = remap_list_fill(map, md, shard_md, reint_ver, tgt_rank, shard_id,
			     array_size, &idx, reint_layout, &reint_list,
			     false);
out:
	remap_list_free_all(&reint_list);
	if (layout != NULL)
		pl_obj_layout_free(layout);
	if (reint_layout != NULL)
		pl_obj_layout_free(reint_layout);

	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_addition(struct pl_map *map, struct daos_obj_md *md,
			   struct daos_obj_shard_md *shard_md,
			   uint32_t reint_ver, uint32_t *tgt_rank,
			   uint32_t *shard_id, unsigned int array_size)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout = NULL;
	struct pl_obj_layout            *add_layout = NULL;
	d_list_t                        add_list;
	struct jm_obj_placement         jop;
	uint32_t			allow_status;
	int				idx = 0;
	int                             rc;

	D_DEBUG(DB_PL, "Finding new layout for server addition\n");

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < reint_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), reint_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2jmap(map);

	rc = jm_obj_placement_get(jmap, md, shard_md, &jop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	allow_status = PO_COMP_ST_UPIN;
	D_INIT_LIST_HEAD(&add_list);
	rc = obj_layout_alloc_and_get(jmap, &jop, md, allow_status,
				      &layout, NULL, NULL);
	if (rc)
		D_GOTO(out, rc);

	allow_status |= PO_COMP_ST_NEW;
	rc = obj_layout_alloc_and_get(jmap, &jop, md, allow_status,
				      &add_layout, NULL, NULL);
	if (rc)
		D_GOTO(out, rc);

	layout_find_diff(jmap, layout, add_layout, &add_list);
	rc = remap_list_fill(map, md, shard_md, reint_ver, tgt_rank, shard_id,
			     array_size, &idx, add_layout, &add_list, true);
out:
	remap_list_free_all(&add_list);

	if (layout != NULL)
		pl_obj_layout_free(layout);
	if (add_layout != NULL)
		pl_obj_layout_free(add_layout);

	return rc < 0 ? rc : idx;
}

/** API for generic placement map functionality */
struct pl_map_ops       jump_map_ops = {
	.o_create               = jump_map_create,
	.o_destroy              = jump_map_destroy,
	.o_query		= jump_map_query,
	.o_print                = jump_map_print,
	.o_obj_place            = jump_map_obj_place,
	.o_obj_find_rebuild     = jump_map_obj_find_rebuild,
	.o_obj_find_reint       = jump_map_obj_find_reint,
	.o_obj_find_addition      = jump_map_obj_find_addition,
};
