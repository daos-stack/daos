/**
 *
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include "jump_map.h"
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

static void
jm_obj_placement_fini(struct jm_obj_placement *jmop)
{
	if (jmop->jmop_pd_ptrs != NULL && jmop->jmop_pd_ptrs != jmop->jmop_pd_ptrs_inline)
		D_FREE(jmop->jmop_pd_ptrs);
}

/** Initialize the PDs for object to prepare for the layout calculation */
#define LOCAL_PD_ARRAY_SIZE	(4)
static int
jm_obj_pd_init(struct pl_jump_map *jmap, struct daos_obj_md *md, struct pool_domain *root,
	       struct jm_obj_placement *jmop)
{
	struct pool_domain	*pds, *pd;
	uint8_t			*pd_used = NULL;
	uint8_t			 pd_used_array[LOCAL_PD_ARRAY_SIZE] = {0};
	uint32_t		 pd_array_size;
	daos_obj_id_t		 oid;
	uint64_t		 key;
	uint32_t		 selected_pd, pd_id;
	uint32_t		 pd_nr = jmap->jmp_pd_nr;
	uint32_t		 dom_nr = jmop->jmop_dom_nr;
	uint32_t		 shard_nr = jmop->jmop_grp_size * jmop->jmop_grp_nr;
	uint32_t		 doms_per_pd;
	uint32_t		 pd_grp_size, pd_grp_nr;
	int			 i, rc;

	jmop->jmop_root = root;
	/* Enable the PDA placement only when -
	 * 1) daos_obj_md::omd_pdom_lvl is PO_COMP_TP_GRP,
	 * 2) daos_obj_md::omd_pda is non-zero, and
	 * 3) with PO_COMP_TP_GRP layer in pool map.
	 */
	if (md->omd_pda == 0 || md->omd_pdom_lvl != PO_COMP_TP_GRP)
		pd_nr = 0;
	jmop->jmop_pd_nr = pd_nr;
	if (jmop->jmop_pd_nr == 0) {
		jmop->jmop_pd_grp_size = 0;
		return 0;
	}

	D_ASSERT(pd_nr >= 1);
	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_GRP, PO_COMP_ID_ALL, &pds);
	D_ASSERT(rc == pd_nr);
	rc = 0;

	doms_per_pd = dom_nr / pd_nr;
	D_ASSERTF(doms_per_pd >= 1, "bad dom_nr %d, pd_nr %d\n", dom_nr, pd_nr);

	if (jmop->jmop_grp_size == 1) { /* non-replica */
		pd_grp_size = min(md->omd_pda, pds->do_target_nr);
	} else {
		if (md->omd_pda == DAOS_PROP_PDA_MAX)
			pd_grp_size = jmop->jmop_grp_size;
		else
			pd_grp_size = md->omd_pda;
		pd_grp_size = min(pd_grp_size, doms_per_pd);
	}
	pd_grp_nr = shard_nr / pd_grp_size + (shard_nr % pd_grp_size != 0);
	jmop->jmop_pd_grp_size = pd_grp_size;
	jmop->jmop_pd_nr = min(pd_grp_nr, pd_nr);

	if (jmop->jmop_pd_nr <= JMOP_PD_INLINE) {
		jmop->jmop_pd_ptrs = jmop->jmop_pd_ptrs_inline;
	} else {
		D_ALLOC_ARRAY(jmop->jmop_pd_ptrs, jmop->jmop_pd_nr);
		if (jmop->jmop_pd_ptrs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}
	pd_array_size = jmap->jmp_pd_nr / NBBY + 1;
	if (pd_array_size > LOCAL_PD_ARRAY_SIZE) {
		D_ALLOC_ARRAY(pd_used, pd_array_size);
		if (pd_used == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		pd_used = pd_used_array;
	}

	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	for (i = 0; i < jmop->jmop_pd_nr; i++) {
		key = crc(key, i);
		selected_pd = d_hash_jump(key, jmap->jmp_pd_nr);
		do {
			selected_pd = selected_pd % jmap->jmp_pd_nr;
			pd = &pds[selected_pd];
			pd_id = pd->do_comp.co_id;
			selected_pd++;
		} while (isset(pd_used, pd_id));

		setbit(pd_used, pd_id);
		D_DEBUG(DB_PL, "PD[%d] -- pd_id %d\n", i, pd_id);
		jmop->jmop_pd_ptrs[i] = pd;
	}

out:
	if (pd_used != NULL && pd_used != pd_used_array)
		D_FREE(pd_used);
	if (rc)
		jm_obj_placement_fini(jmop);
	return rc;
}

/** Calculates the PD for the shard */
struct pool_domain *
jm_obj_shard_pd(struct jm_obj_placement *jmop, uint32_t shard)
{
	uint32_t pd_idx;

	if (jmop->jmop_pd_nr == 0)
		return jmop->jmop_root;

	D_ASSERT(shard < jmop->jmop_grp_size * jmop->jmop_grp_nr);
	pd_idx = (shard / jmop->jmop_pd_grp_size) % jmop->jmop_pd_nr;

	D_DEBUG(DB_PL, "shard %d, on pd_idx %d\n", shard, pd_idx);
	return jmop->jmop_pd_ptrs[pd_idx];
}

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
 * \param[in]	for_reint	diff calls from find_reint() to extract the reintegrating
 *                              shards.
 * \param[out]	diff		The d_list that contains the differences that
 *				were calculated.
 */
static inline void
layout_find_diff(struct pl_jump_map *jmap, struct pl_obj_layout *original,
		 struct pl_obj_layout *new, d_list_t *diff, bool for_reint)
{
	int index;

	/* We assume they are the same size */
	D_ASSERT(original->ol_nr == new->ol_nr);

	for (index = 0; index < original->ol_nr; ++index) {
		uint32_t original_target = original->ol_shards[index].po_target;
		uint32_t reint_tgt = new->ol_shards[index].po_target;
		struct pool_target *temp_tgt;

		/* For reintegration, rebuilding shards should be added to the
		 * reintegrated shards, since "DOWN" shard is being considered
		 * during layout recalculation.
		 */
		if (reint_tgt != original_target ||
		    (for_reint && original->ol_shards[index].po_rebuilding)) {
			pool_map_find_target(jmap->jmp_map.pl_poolmap,
					     reint_tgt, &temp_tgt);
			if (pool_target_avail(temp_tgt, PO_COMP_ST_UPIN | PO_COMP_ST_UP |
					      PO_COMP_ST_DRAIN))
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
jm_obj_placement_init(struct pl_jump_map *jmap, struct daos_obj_md *md,
		      struct daos_obj_shard_md *shard_md,
		      struct jm_obj_placement *jmop)
{
	struct daos_oclass_attr *oc_attr;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	int                     rc;
	uint32_t		dom_nr;
	uint32_t		nr_grps;

	jmop->jmop_pd_ptrs = NULL;
	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid, &nr_grps);

	if (oc_attr == NULL) {
		D_ERROR("Can not find obj class, invalid oid="DF_OID"\n",
			DP_OID(oid));
		return -DER_INVAL;
	}

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);

	if (md->omd_fdom_lvl == 0 || md->omd_fdom_lvl == jmap->jmp_redundant_dom) {
		dom_nr = jmap->jmp_domain_nr;
		jmop->jmop_fdom_lvl = jmap->jmp_redundant_dom;
	} else {
		D_ASSERT(md->omd_fdom_lvl == PO_COMP_TP_RANK ||
			 md->omd_fdom_lvl == PO_COMP_TP_NODE);
		rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, md->omd_fdom_lvl,
					  PO_COMP_ID_ALL, NULL);
		D_ASSERT(rc > 0);
		jmop->jmop_fdom_lvl = md->omd_fdom_lvl;
		dom_nr = rc;
	}
	jmop->jmop_dom_nr = dom_nr;
	rc = op_get_grp_size(dom_nr, &jmop->jmop_grp_size, oid);
	if (rc)
		return rc;

	if (shard_md == NULL) {
		unsigned int grp_max = root->do_target_nr / jmop->jmop_grp_size;

		if (grp_max == 0)
			grp_max = 1;

		jmop->jmop_grp_nr = nr_grps;
		if (jmop->jmop_grp_nr == DAOS_OBJ_GRP_MAX)
			jmop->jmop_grp_nr = grp_max;
		else if (jmop->jmop_grp_nr > grp_max) {
			D_ERROR("jmop->jmop_grp_nr %d, grp_max %d, grp_size %d\n",
				jmop->jmop_grp_nr, grp_max, jmop->jmop_grp_size);
			return -DER_INVAL;
		}
	} else {
		jmop->jmop_grp_nr = 1;
	}

	D_ASSERT(jmop->jmop_grp_nr > 0);
	D_ASSERT(jmop->jmop_grp_size > 0);

	rc = jm_obj_pd_init(jmap, md, root, jmop);
	if (rc == 0)
		D_DEBUG(DB_PL, "obj="DF_OID"/ grp_size=%u grp_nr=%d, pd_nr=%u pd_grp_size=%u\n",
			DP_OID(oid), jmop->jmop_grp_size, jmop->jmop_grp_nr,
			jmop->jmop_pd_nr, jmop->jmop_pd_grp_size);
	else
		D_ERROR("obj="DF_OID", jm_obj_pd_init failed, "DF_RC"\n", DP_OID(oid), DP_RC(rc));

	return rc;
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
	uint8_t		*dgu_real;
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
* \paramp[in]	allow_status	the target status allowed to be in the layout.
* \paramp[in]	allow_version	the target status needs to be restored to its
*                               original status by version, then check.
* \paramp[in]   dom_used        Bookkeeping array used to keep track of which
*                               domain components have already been used.
*
* \return       return an error code signifying whether the shards were
*               successfully remapped properly.
*/
static int
obj_remap_shards(struct pl_jump_map *jmap, uint32_t layout_ver, struct daos_obj_md *md,
		 struct pl_obj_layout *layout, struct jm_obj_placement *jmop,
		 d_list_t *remap_list, d_list_t *out_list, uint32_t allow_status,
		 uint32_t allow_version, uint8_t *tgts_used, uint8_t *dom_used,
		 uint8_t *dom_full, uint32_t failed_in_layout, bool *is_extending,
		 uint32_t fdom_lvl)
{
	struct failed_shard     *f_shard;
	struct pl_obj_shard     *l_shard;
	struct pool_target      *spare_tgt = NULL;
	struct pool_domain      *spare_dom = NULL;
	struct pool_domain      *root, *curr_pd;
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
		struct dom_grp_used *dgu = NULL;

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
		if (spare_avail) {
			dgu = f_shard->fs_data;

			D_ASSERT(dgu != NULL);
			rebuild_key = crc(key, f_shard->fs_shard_idx);
			curr_pd = jm_obj_shard_pd(jmop, shard_id);
			get_target(root, curr_pd, layout_ver, &spare_tgt, &spare_dom,
				   crc(key, rebuild_key), dom_used, dom_full,
				   dgu->dgu_used, dgu->dgu_real, tgts_used,
				   shard_id, allow_status, allow_version, fdom_lvl,
				   jmop->jmop_grp_size, &spares_left, &spare_avail);
			D_ASSERT(spare_tgt != NULL);
			D_DEBUG(DB_PL, "Trying new target: "DF_TARGET"\n",
				DP_TARGET(spare_tgt));
		}

		rc = determine_valid_spares(spare_tgt, md, spare_avail, remap_list,
					    allow_status, f_shard, l_shard, is_extending);
		if (rc == 1) {
			/* Current shard is remapped, move the remap to the output list or
			 * delete it.
			 */
			if (out_list != NULL) {
				d_list_move_tail(current, out_list);
			} else {
				d_list_del(&f_shard->fs_list);
				D_FREE(f_shard);
			}

			if (spare_dom != NULL && dgu != NULL)
				setbit(dgu->dgu_real, spare_dom - root);
		}
		current = remap_list->next;
	}

	return 0;
}

static int
jump_map_obj_spec_place_get(struct pl_jump_map *jmap, daos_obj_id_t oid,
			    uint8_t *dom_used, uint32_t dom_bytes,
			    struct pool_target **target, struct pool_domain **domain)
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
				*domain = current_dom;
				current_dom = temp_dom;
				setbit(dom_used, (index + child_pos));
				break;
			}
		}
	}

	return 0;
}

static struct dom_grp_used*
remap_gpu_alloc_one(d_list_t *remap_list, uint8_t *dom_cur_grp_used,
		    uint8_t *dom_cur_grp_real)
{
	struct dom_grp_used	*dgu;

	D_ALLOC_PTR(dgu);
	if (dgu == NULL)
		return NULL;

	dgu->dgu_used = dom_cur_grp_used;
	dgu->dgu_real = dom_cur_grp_real;
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
get_object_layout(struct pl_jump_map *jmap, uint32_t layout_ver, struct pl_obj_layout *layout,
		  struct jm_obj_placement *jmop, d_list_t *out_list, uint32_t allow_status,
		  uint32_t allow_version, struct daos_obj_md *md, bool *is_extending)
{
	struct pool_target      *target;
	struct pool_domain      *domain;
	struct pool_domain      *root, *curr_pd;
	daos_obj_id_t           oid;
	uint8_t                 *dom_used = NULL;
	uint8_t                 *dom_full = NULL;
	uint8_t                 *tgts_used = NULL;
	uint8_t			*dom_cur_grp_used = NULL;
	uint8_t			*dom_cur_grp_real = NULL;
	uint8_t			dom_used_array[LOCAL_DOM_ARRAY_SIZE] = { 0 };
	uint8_t			dom_full_array[LOCAL_DOM_ARRAY_SIZE] = { 0 };
	uint8_t			tgts_used_array[LOCAL_TGT_ARRAY_SIZE] = { 0 };
	uint8_t			dom_cur_grp_used_array[LOCAL_TGT_ARRAY_SIZE] = { 0 };
	uint8_t			dom_cur_grp_real_array[LOCAL_TGT_ARRAY_SIZE] = { 0 };
	d_list_t		dgu_remap_list;
	uint32_t                dom_size;
	uint32_t                dom_array_size;
	uint64_t                key;
	uint32_t		fail_tgt_cnt = 0;
	bool			spec_oid = false;
	bool			realloc_grp_used = false;
	d_list_t		remap_list;
	int			fdom_lvl;
	int			i, j, k;
	int			rc = 0;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(&(jmap->jmp_map));
	D_DEBUG(DB_PL, "Building layout. map version: %d/%u/%u\n", layout->ol_ver,
		layout_ver, allow_version);
	debug_print_allow_status(allow_status);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.\n");
		return -DER_NONEXIST;
	}
	rc = 0;

	D_INIT_LIST_HEAD(&remap_list);
	D_INIT_LIST_HEAD(&dgu_remap_list);

	dom_size = (struct pool_domain *)(root->do_targets) - (root) + 1;
	dom_array_size = dom_size/NBBY + 1;
	if (dom_array_size > LOCAL_DOM_ARRAY_SIZE) {
		D_ALLOC_ARRAY(dom_used, dom_array_size);
		D_ALLOC_ARRAY(dom_full, dom_array_size);
		D_ALLOC_ARRAY(dom_cur_grp_used, dom_array_size);
		D_ALLOC_ARRAY(dom_cur_grp_real, dom_array_size);
	} else {
		dom_used = dom_used_array;
		dom_full = dom_full_array;
		dom_cur_grp_used = dom_cur_grp_used_array;
		dom_cur_grp_real = dom_cur_grp_real_array;
	}

	if (root->do_target_nr / NBBY + 1 > LOCAL_TGT_ARRAY_SIZE)
		D_ALLOC_ARRAY(tgts_used, (root->do_target_nr / NBBY) + 1);
	else
		tgts_used = tgts_used_array;

	if (dom_used == NULL || dom_full == NULL || tgts_used == NULL ||
	    dom_cur_grp_used == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	if (daos_obj_is_srank(oid))
		spec_oid = true;

	fdom_lvl = pool_map_failure_domain_level(jmap->jmp_map.pl_poolmap, jmop->jmop_fdom_lvl);
	D_ASSERT(fdom_lvl > 0);
	for (i = 0, k = 0; i < jmop->jmop_grp_nr; i++) {
		struct dom_grp_used  *remap_grp_used = NULL;

		if (realloc_grp_used) {
			realloc_grp_used = false;
			D_ALLOC_ARRAY(dom_cur_grp_used, dom_array_size);
			if (dom_cur_grp_used == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			D_ALLOC_ARRAY(dom_cur_grp_real, dom_array_size);
			if (dom_cur_grp_real == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			memset(dom_cur_grp_used, 0, dom_array_size);
			memset(dom_cur_grp_real, 0, dom_array_size);
		}

		for (j = 0; j < jmop->jmop_grp_size; j++, k++) {
			target = NULL;
			domain = NULL;
			if (spec_oid && i == 0 && j == 0) {
				/**
				 * If the object class is a special class then
				 * the first shard must be picked specially.
				 */
				rc = jump_map_obj_spec_place_get(jmap, oid, dom_used,
								 dom_size, &target, &domain);
				if (rc) {
					D_ERROR("special oid "DF_OID" failed: rc %d\n",
						DP_OID(oid), rc);
					D_GOTO(out, rc);
				}
				setbit(tgts_used, target->ta_comp.co_id);
				setbit(dom_cur_grp_used, domain - root);
			} else {
				curr_pd = jm_obj_shard_pd(jmop, k);
				get_target(root, curr_pd, layout_ver, &target, &domain, key, dom_used,
					   dom_full, dom_cur_grp_used, dom_cur_grp_real, tgts_used,
					   k, allow_status, allow_version, fdom_lvl, jmop->jmop_grp_size,
					   NULL, NULL);
			}

			if (target == NULL) {
				D_DEBUG(DB_PL, "no targets for %d/%d/%d\n", i, j, k);
				layout->ol_shards[k].po_target = -1;
				layout->ol_shards[k].po_shard = -1;
				layout->ol_shards[k].po_fseq = 0;
				continue;
			}

			layout->ol_shards[k].po_target = target->ta_comp.co_id;
			layout->ol_shards[k].po_fseq = target->ta_comp.co_fseq;
			layout->ol_shards[k].po_shard = k;

			/** If target is failed queue it for remap*/
			if (need_remap_comp(&target->ta_comp, allow_status)) {
				fail_tgt_cnt++;
				D_DEBUG(DB_PL, "Target unavailable " DF_TARGET
					". Adding to remap_list: fail cnt %d\n",
					DP_TARGET(target), fail_tgt_cnt);

				if (remap_grp_used == NULL) {
					remap_grp_used = remap_gpu_alloc_one(&dgu_remap_list,
									     dom_cur_grp_used,
									     dom_cur_grp_real);
					if (remap_grp_used == NULL)
						D_GOTO(out, rc = -DER_NOMEM);
					realloc_grp_used = true;
				}

				rc = remap_alloc_one(&remap_list, k, target, false, remap_grp_used);
				if (rc)
					D_GOTO(out, rc);
			} else {
				if (domain != NULL)
					setbit(dom_cur_grp_real, domain - root);
			}

			if (is_extending != NULL && pool_target_is_up_or_drain(target))
				*is_extending = true;
		}
	}

	if (fail_tgt_cnt > 0)
		rc = obj_remap_shards(jmap, layout_ver, md, layout, jmop, &remap_list, out_list,
				      allow_status, md->omd_ver, tgts_used, dom_used, dom_full,
				      fail_tgt_cnt, is_extending, fdom_lvl);
out:
	if (rc)
		D_ERROR("jump_map_obj_layout_fill failed, rc "DF_RC"\n", DP_RC(rc));

	remap_list_free_all(&remap_list);

	if (!d_list_empty(&dgu_remap_list)) {
		struct dom_grp_used *dgu;
		struct dom_grp_used *tmp;

		d_list_for_each_entry_safe(dgu, tmp, &dgu_remap_list, dgu_list) {
			d_list_del(&dgu->dgu_list);
			if (dgu->dgu_used) {
				if (dgu->dgu_used == dom_cur_grp_used)
					dom_cur_grp_used = NULL;
				if (dgu->dgu_real == dom_cur_grp_real)
					dom_cur_grp_real = NULL;
				if (dgu->dgu_used != dom_cur_grp_used_array)
					D_FREE(dgu->dgu_used);
				if (dgu->dgu_real != dom_cur_grp_real_array)
					D_FREE(dgu->dgu_real);
			}
			D_FREE(dgu);
		}
	}

	if (dom_used && dom_used != dom_used_array)
		D_FREE(dom_used);
	if (dom_full && dom_full != dom_full_array)
		D_FREE(dom_full);
	if (tgts_used && tgts_used != tgts_used_array)
		D_FREE(tgts_used);

	if (dom_cur_grp_used && dom_cur_grp_used != dom_cur_grp_used_array)
		D_FREE(dom_cur_grp_used);

	if (dom_cur_grp_real && dom_cur_grp_real != dom_cur_grp_real_array)
		D_FREE(dom_cur_grp_real);


	return rc;
}

static int
obj_layout_alloc_and_get(struct pl_jump_map *jmap, uint32_t layout_ver,
			 struct jm_obj_placement *jmop, struct daos_obj_md *md,
			 uint32_t allow_status, uint32_t allow_version,
			 struct pl_obj_layout **layout_p, d_list_t *remap_list,
			 bool *is_extending)
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

	rc = get_object_layout(jmap, layout_ver, *layout_p, jmop, remap_list, allow_status,
			       allow_version, md, is_extending);
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

	rc = pool_map_find_domain(poolmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.\n");
		rc = -DER_NONEXIST;
		goto ERR;
	}

	jmap->jmp_redundant_dom = mia->ia_jump_map.domain;

	rc = pool_map_find_domain(poolmap, PO_COMP_TP_GRP, PO_COMP_ID_ALL, &doms);
	if (rc < 0)
		goto ERR;
	jmap->jmp_pd_nr = rc;

	rc = pool_map_find_domain(poolmap, mia->ia_jump_map.domain, PO_COMP_ID_ALL, &doms);
	if (rc <= 0) {
		rc = (rc == 0) ? -DER_INVAL : rc;
		goto ERR;
	}
	jmap->jmp_domain_nr = rc;
	if (jmap->jmp_domain_nr < jmap->jmp_pd_nr) {
		D_ERROR("Bad parameter, dom_nr %d < pd_nr %d\n",
			jmap->jmp_domain_nr, jmap->jmp_pd_nr);
		return -DER_INVAL;
	}

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
	struct pl_jump_map	*jmap = pl_map2jmap(map);
	struct pool_map		*pool_map = map->pl_poolmap;
	int			 rc;

	attr->pa_type	   = PL_TYPE_JUMP_MAP;
	attr->pa_target_nr = jmap->jmp_target_nr;

	if (attr->pa_domain <= 0 || attr->pa_domain == jmap->jmp_redundant_dom ||
	    attr->pa_domain > PO_COMP_TP_MAX) {
		attr->pa_domain_nr = jmap->jmp_domain_nr;
		attr->pa_domain    = jmap->jmp_redundant_dom;
	} else {
		rc = pool_map_find_domain(pool_map, attr->pa_domain, PO_COMP_ID_ALL, NULL);
		if (rc < 0)
			return rc;
		attr->pa_domain_nr = rc;
	}

	return 0;
}

static int
jump_map_obj_extend_layout(struct pl_jump_map *jmap, struct jm_obj_placement *jmop,
			   uint32_t layout_version, struct daos_obj_md *md,
			   struct pl_obj_layout *layout)
{
	struct pl_obj_layout	*new_layout = NULL;
	d_list_t		extend_list;
	uint32_t		allow_status;
	int			rc;

	/* Needed to check if domains are being added to pool map */
	D_DEBUG(DB_PL, DF_OID"/%u/%u is being added or extended.\n",
		DP_OID(md->omd_id), md->omd_ver, layout_version);

	D_INIT_LIST_HEAD(&extend_list);
	allow_status = PO_COMP_ST_UPIN | /*PO_COMP_ST_DRAIN |*/ PO_COMP_ST_UP;
	rc = obj_layout_alloc_and_get(jmap, layout_version, jmop, md, allow_status,
				      md->omd_ver, &new_layout, NULL, NULL);
	if (rc != 0) {
		D_ERROR(DF_OID" get_layout_alloc failed, rc "DF_RC"\n",
			DP_OID(md->omd_id), DP_RC(rc));
		D_GOTO(out, rc);
	}

	obj_layout_dump(md->omd_id, new_layout);

	layout_find_diff(jmap, layout, new_layout, &extend_list, false);
	if (!d_list_empty(&extend_list)) {
		rc = pl_map_extend(layout, &extend_list);
		if (rc != 0) {
			D_ERROR("extend layout failed, rc "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}
	obj_layout_dump(md->omd_id, layout);
out:
	remap_list_free_all(&extend_list);
	if (new_layout != NULL)
		pl_obj_layout_free(new_layout);

	return rc;
}
/**
 * Determines the locations that a given object shard should be located.
 *
 * \param[in]   map             A reference to the placement map being used to
 *                              place the object shard.
 * \param[in]   layout_version	layout version.
 * \param[in]   md              The object metadata which contains data about
 *                              the object being placed such as the object ID.
 * \param[in]   mode		mode of daos_obj_open(DAOS_OO_RO, DAOS_OO_RW etc).
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
jump_map_obj_place(struct pl_map *map, uint32_t layout_version, struct daos_obj_md *md,
		   unsigned int mode, struct daos_obj_shard_md *shard_md,
		   struct pl_obj_layout **layout_pp)
{
	struct pl_jump_map	*jmap;
	struct pl_obj_layout	*layout = NULL;
	struct jm_obj_placement	jmop;
	bool			is_extending = false;
	bool			is_adding_new = false;
	daos_obj_id_t		oid;
	struct pool_domain	*root;
	uint32_t		allow_status;
	int			rc;

	jmap = pl_map2jmap(map);
	oid = md->omd_id;
	D_DEBUG(DB_PL, "Determining location for object: "DF_OID", ver: %d, pda %u\n",
		DP_OID(oid), md->omd_ver, md->omd_pda);

	rc = jm_obj_placement_init(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_init failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	allow_status = PO_COMP_ST_UPIN | PO_COMP_ST_DRAIN;
	rc = obj_layout_alloc_and_get(jmap, layout_version, &jmop, md, allow_status,
				      md->omd_ver, &layout, NULL, &is_extending);
	if (rc != 0) {
		D_ERROR("get_layout_alloc failed, rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	obj_layout_dump(oid, layout);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL,
				  &root);
	D_ASSERT(rc == 1);
	rc = 0;

	if (is_pool_map_adding(jmap->jmp_map.pl_poolmap))
		is_adding_new = true;

	/**
	 * If the layout is being extended or drained, it need recreate the layout
	 * strictly by rebuild version to make sure both new and old shards being
	 * updated.
	 */
	if (unlikely(is_extending || is_adding_new) && !(mode & DAOS_OO_RO)) {
		D_DEBUG(DB_PL, DF_OID"/%d is being extended: %s\n", DP_OID(oid),
			md->omd_ver, is_extending ? "yes" : "no");
		rc = jump_map_obj_extend_layout(jmap, &jmop, layout_version, md, layout);
		if (rc)
			D_GOTO(out, rc);
	}

	*layout_pp = layout;
out:
	jm_obj_placement_fini(&jmop);
	if (rc != 0) {
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
 * \param[in]	layout_ver	obj layout version.
 * \param[in]   md              Metadata describing the object.
 * \param[in]   shard_md        Metadata describing how the shards.
 * \param[in]   rebuild_ver     Current Rebuild version
 * \param[out]   tgt_rank       The engine rank of the targets that need to be
 *                              rebuilt will be stored in this array to be passed
 *                              out (this is allocated by the caller)
 * \param[out]   shard_id       The shard IDs of the shards that need to be
 *                              rebuilt (This is allocated by the caller)
 * \param[in]   array_size      The max size of the passed in arrays to store
 *                              info about the shards that need to be rebuilt.
 *
 * \return                      The number of shards that need to be rebuilt on
 *                              another target, Or 0 if none need to be rebuilt.
 */
static int
jump_map_obj_find_rebuild(struct pl_map *map, uint32_t layout_ver, struct daos_obj_md *md,
			  struct daos_obj_shard_md *shard_md, uint32_t rebuild_ver,
			  uint32_t *tgt_id, uint32_t *shard_idx, unsigned int array_size)
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

	rc = jm_obj_placement_init(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_init failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = obj_layout_alloc_and_get(jmap, layout_ver, &jmop, md, PO_COMP_ST_UPIN,
				      rebuild_ver, &layout, &remap_list, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	obj_layout_dump(oid, layout);
	rc = remap_list_fill(map, md, shard_md, rebuild_ver, tgt_id, shard_idx,
			     array_size, &idx, layout, &remap_list, false);

out:
	jm_obj_placement_fini(&jmop);
	remap_list_free_all(&remap_list);
	if (layout != NULL)
		pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_reint(struct pl_map *map, uint32_t layout_ver, struct daos_obj_md *md,
			struct daos_obj_shard_md *shard_md, uint32_t reint_ver,
			uint32_t *tgt_rank, uint32_t *shard_id, unsigned int array_size)
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
	rc = jm_obj_placement_init(jmap, md, shard_md, &jop);
	if (rc) {
		D_ERROR("jm_obj_placement_init failed, rc %d.\n", rc);
		return rc;
	}

	allow_status = PO_COMP_ST_UPIN | PO_COMP_ST_DRAIN;
	D_INIT_LIST_HEAD(&reint_list);
	rc = obj_layout_alloc_and_get(jmap, layout_ver, &jop, md, allow_status,
				      reint_ver, &layout, NULL, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	obj_layout_dump(md->omd_id, layout);
	allow_status |= PO_COMP_ST_UP;
	rc = obj_layout_alloc_and_get(jmap, layout_ver, &jop, md, allow_status,
				      reint_ver, &reint_layout, NULL, NULL);
	if (rc < 0)
		D_GOTO(out, rc);

	obj_layout_dump(md->omd_id, reint_layout);
	layout_find_diff(jmap, layout, reint_layout, &reint_list, true);

	rc = remap_list_fill(map, md, shard_md, reint_ver, tgt_rank, shard_id,
			     array_size, &idx, reint_layout, &reint_list, false);
out:
	jm_obj_placement_fini(&jop);
	remap_list_free_all(&reint_list);
	if (layout != NULL)
		pl_obj_layout_free(layout);
	if (reint_layout != NULL)
		pl_obj_layout_free(reint_layout);

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
	.o_obj_find_addition      = jump_map_obj_find_reint,
};
