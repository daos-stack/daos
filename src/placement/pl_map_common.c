/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/pl_map_common.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"

/**
 * Add a new failed shard into remap list
 *
 * \param[in] remap_list        List for the failed shard to be added onto.
 * \param[in] f_new             Failed shard to be added.
 */
void
remap_add_one(d_list_t *remap_list, struct failed_shard *f_new)
{
	struct failed_shard     *f_shard;

	d_list_t                *tmp;

	D_DEBUG(DB_PL, "fnew: %u/%d/%u/%u", f_new->fs_shard_idx,
		f_new->fs_tgt_id, f_new->fs_fseq, f_new->fs_status);

	/* All failed shards are sorted by fseq in ascending order */
	d_list_for_each_prev(tmp, remap_list) {
		f_shard = d_list_entry(tmp, struct failed_shard, fs_list);
		/*
		* Since we can only rebuild one target at a time, the
		* target fseq should be assigned uniquely, even if all
		* the targets of the same domain failed at same time.
		*/
		D_DEBUG(DB_PL, "fnew: %u, fshard: %u", f_new->fs_shard_idx,
			f_shard->fs_shard_idx);

		D_ASSERTF(f_new->fs_fseq != f_shard->fs_fseq,
			  "same fseq %u!\n", f_new->fs_fseq);

		if (f_new->fs_fseq < f_shard->fs_fseq)
			continue;

		d_list_add(&f_new->fs_list, tmp);
		return;
	}
	d_list_add(&f_new->fs_list, remap_list);
}

/**
   * Allocate a new failed shard then add it into remap list
   *
   * \param[in] remap_list        List for the failed shard to be added onto.
   * \param[in] shard_idx         The shard number of the failed shard.
   * \paramp[in] tgt              The failed target that will be added to the
   *                              remap list.
   */
int
remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		struct pool_target *tgt, bool for_reint, void *data)
{
	struct failed_shard *f_new;

	D_ALLOC_PTR(f_new);
	if (f_new == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&f_new->fs_list);
	f_new->fs_shard_idx = shard_idx;
	f_new->fs_fseq = tgt->ta_comp.co_fseq;
	f_new->fs_status = tgt->ta_comp.co_status;
	f_new->fs_data = data;

	D_DEBUG(DB_PL, "tgt %u status %u reint %s\n", tgt->ta_comp.co_id,
		tgt->ta_comp.co_status, for_reint ? "yes" : "no");
	if (!for_reint) {
		f_new->fs_tgt_id = -1;
		remap_add_one(remap_list, f_new);
	} else {
		f_new->fs_tgt_id = tgt->ta_comp.co_id;
		d_list_add_tail(&f_new->fs_list, remap_list);
	}

	return 0;
}

/**
 * Free all elements in the remap list
 *
 * \param[in] The remap list to be freed.
 */
inline void
remap_list_free_all(d_list_t *remap_list)
{
	struct failed_shard *f_shard;
	struct failed_shard *tmp;

	d_list_for_each_entry_safe(f_shard, tmp, remap_list, fs_list) {
		d_list_del(&f_shard->fs_list);
		D_FREE(f_shard);
	}
}

/** dump remap list, for debug only */
void
remap_dump(d_list_t *remap_list, struct daos_obj_md *md,
	   char *comment)
{
	struct failed_shard *f_shard;

	D_DEBUG(DB_PL, "remap list for "DF_OID", %s, ver %d\n",
		DP_OID(md->omd_id), comment, md->omd_ver);

	d_list_for_each_entry(f_shard, remap_list, fs_list) {
		D_DEBUG(DB_PL, "fseq:%u, shard_idx:%u status:%u tgt %d\n",
			f_shard->fs_fseq, f_shard->fs_shard_idx,
			f_shard->fs_status, f_shard->fs_tgt_id);
	}
}

/**
* If replication max use all available domains as specified
* in map initialization.
*/
int
op_get_grp_size(unsigned int domain_nr, unsigned int *grp_size,
		daos_obj_id_t oid)
{
	struct daos_oclass_attr *oc_attr;

	oc_attr = daos_oclass_attr_find(oid, NULL);

	*grp_size = daos_oclass_grp_size(oc_attr);
	D_ASSERT(*grp_size != 0);

	if (*grp_size == DAOS_OBJ_REPL_MAX)
		*grp_size = domain_nr;

	if (*grp_size > domain_nr) {
		D_ERROR("obj="DF_OID": grp size (%u) (%u) is larger than "
			"domain nr (%u)\n", DP_OID(oid), *grp_size,
			DAOS_OBJ_REPL_MAX, domain_nr);
		return -DER_INVAL;
	}

	return 0;
}

int
spec_place_rank_get(unsigned int *pos, daos_obj_id_t oid,
		    struct pool_map *pl_poolmap)
{
	struct pool_target      *tgts;
	unsigned int            tgts_nr;
	d_rank_t                rank;
	int                     tgt;
	int                     current_index;

	D_ASSERT(daos_obj_is_srank(oid));

	/* locate rank in the pool map targets */
	tgts = pool_map_targets(pl_poolmap);
	tgts_nr = pool_map_target_nr(pl_poolmap);
	/* locate rank in the pool map targets */
	rank = daos_oclass_sr_get_rank(oid);
	tgt = daos_oclass_st_get_tgt(oid);

	for (current_index = 0; current_index < tgts_nr; current_index++) {
		if (rank == tgts[current_index].ta_comp.co_rank &&
		    (tgt == tgts[current_index].ta_comp.co_index))
			break;
	}
	if (current_index == tgts_nr)
		return -DER_INVAL;

	*pos = current_index;
	return 0;
}

int
remap_list_fill(struct pl_map *map, struct daos_obj_md *md,
		struct daos_obj_shard_md *shard_md, uint32_t r_ver,
		uint32_t *tgt_id, uint32_t *shard_idx,
		unsigned int array_size, int *idx,
		struct pl_obj_layout *layout, d_list_t *remap_list,
		bool fill_addition)
{
	struct failed_shard     *f_shard;
	struct pl_obj_shard     *l_shard;

	d_list_for_each_entry(f_shard, remap_list, fs_list) {
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];

		if (f_shard->fs_fseq > r_ver)
			break;

		/**
		 * NB: due to colrelocation, the data on healthy
		 * shard might need move to, so let's include
		 * UPIN status here as well.
		 */
		if (f_shard->fs_status == PO_COMP_ST_DOWN ||
		    f_shard->fs_status == PO_COMP_ST_UP ||
		    f_shard->fs_status == PO_COMP_ST_DRAIN ||
		    f_shard->fs_status == PO_COMP_ST_UPIN ||
		    fill_addition == true) {
			/*
			 * Target id is used for rw, but rank is used
			 * for rebuild, perhaps they should be unified.
			 */
			if (l_shard->po_shard != -1) {
				if (*idx >= array_size)
					/*
					 * Not enough space for this layout in
					 * the buffer provided by the caller
					 */
					return -DER_REC2BIG;
				tgt_id[*idx] = l_shard->po_target;
				shard_idx[*idx] = l_shard->po_shard;
				(*idx)++;
			}
		} else {
			D_DEBUG(DB_REBUILD, ""DF_OID" skip idx %u"
				"fseq:%d(status:%d)? rbd_ver:%d\n",
				DP_OID(md->omd_id), f_shard->fs_shard_idx,
				f_shard->fs_fseq, f_shard->fs_status, r_ver);
		}
	}

	return 0;
}

void
determine_valid_spares(struct pool_target *spare_tgt, struct daos_obj_md *md,
		bool spare_avail, d_list_t **current, d_list_t *remap_list,
		uint32_t allow_status, struct failed_shard *f_shard,
		struct pl_obj_shard *l_shard, bool *is_extending)
{
	struct failed_shard *f_tmp;

	if (!spare_avail)
		goto next_fail;

	/* The selected spare target is down as well */
	if (!pool_target_avail(spare_tgt, allow_status)) {
		D_ASSERTF(spare_tgt->ta_comp.co_fseq !=
			  f_shard->fs_fseq, "same fseq %u!\n",
			  f_shard->fs_fseq);
		D_DEBUG(DB_PL, "Spare target is also unavailable " DF_TARGET
			".\n", DP_TARGET(spare_tgt));

		/* If the spare target fseq > the current object pool
		 * version, the current failure shard will be handled
		 * by the following rebuild.
		 */
		if (spare_tgt->ta_comp.co_fseq > md->omd_ver) {
			D_DEBUG(DB_PL, DF_OID", "DF_TARGET", ver: %d\n",
				DP_OID(md->omd_id),
				DP_TARGET(spare_tgt),
				md->omd_ver);
			spare_avail = false;
			goto next_fail;
		}

		/*
		 * The selected spare is down prior to current failed
		 * one, then it can't be a valid spare, let's skip it
		 * and try next spare in the placement.
		 */
		if (spare_tgt->ta_comp.co_fseq < f_shard->fs_fseq) {
			D_DEBUG(DB_PL, "spare tgt %u co fs_seq %u"
				" shard f_seq %u\n", spare_tgt->ta_comp.co_id,
				spare_tgt->ta_comp.co_fseq,
				f_shard->fs_fseq);
			return; /* try next spare */
		}
		/*
		 * If both failed target and spare target are down, then
		 * add the spare target to the fail list for remap, and
		 * try next spare.
		 */
		if (f_shard->fs_status == PO_COMP_ST_DOWN ||
		    f_shard->fs_status == PO_COMP_ST_DRAIN)
			D_ASSERTF(spare_tgt->ta_comp.co_status !=
				  PO_COMP_ST_DOWNOUT,
				  "down fseq(%u) < downout fseq(%u)\n",
				  f_shard->fs_fseq,
				  spare_tgt->ta_comp.co_fseq);

		f_shard->fs_fseq = spare_tgt->ta_comp.co_fseq;
		f_shard->fs_status = spare_tgt->ta_comp.co_status;

		(*current) = (*current)->next;
		d_list_del_init(&f_shard->fs_list);
		D_DEBUG(DB_PL, "failed shard ("DF_FAILEDSHARD") added to "
			       "remamp_list\n", DP_FAILEDSHARD(*f_shard));
		remap_add_one(remap_list, f_shard);
		if (is_extending != NULL &&
		    (spare_tgt->ta_comp.co_status == PO_COMP_ST_UP ||
		     spare_tgt->ta_comp.co_status == PO_COMP_ST_DRAIN))
			*is_extending = true;

		/* Continue with the failed shard has minimal fseq */
		if ((*current) == remap_list) {
			(*current) = &f_shard->fs_list;
		} else {
			f_tmp = d_list_entry((*current),
					     struct failed_shard,
					     fs_list);
			if (f_shard->fs_fseq < f_tmp->fs_fseq)
				(*current) = &f_shard->fs_list;
		}
		D_DEBUG(DB_PL, "spare_tgt %u status %u f_seq %u try next.\n",
			spare_tgt->ta_comp.co_id, spare_tgt->ta_comp.co_status,
			spare_tgt->ta_comp.co_fseq);
		return; /* try next spare */
	}

next_fail:
	if (spare_avail) {
		/* The selected spare target is up and ready */
		l_shard->po_target = spare_tgt->ta_comp.co_id;
		l_shard->po_fseq = f_shard->fs_fseq;

		/*
		 * Mark the shard as 'rebuilding' so that read will
		 * skip this shard.
		 */
		if (f_shard->fs_status == PO_COMP_ST_DOWN ||
		    f_shard->fs_status == PO_COMP_ST_DRAIN)
			l_shard->po_rebuilding = 1;
	} else {
		l_shard->po_shard = -1;
		l_shard->po_target = -1;
	}
	(*current) = (*current)->next;
}

#define STACK_TGTS_SIZE	32
bool
grp_map_is_set(uint32_t *grp_map, uint32_t grp_map_size, uint32_t tgt_id)
{
	int i;

	for (i = 0; i < grp_map_size; i++) {
		if (grp_map[i] == tgt_id)
			return true;
	}

	return false;
}

uint32_t*
grp_map_extend(uint32_t *grp_map, uint32_t *grp_map_size)
{
	uint32_t *new_grp_map;
	uint32_t new_grp_size = *grp_map_size + STACK_TGTS_SIZE;
	int	 i;

	if (*grp_map_size > STACK_TGTS_SIZE)
		D_REALLOC_ARRAY(new_grp_map, grp_map, *grp_map_size,
				new_grp_size);
	else
		D_ALLOC_ARRAY(new_grp_map, new_grp_size);

	for (i = *grp_map_size; i < new_grp_size; i++)
		grp_map[i] = -1;

	*grp_map_size = new_grp_size;
	return new_grp_map;
}

int
pl_map_extend(struct pl_obj_layout *layout, d_list_t *extended_list)
{
	struct pl_obj_shard	*new_shards;
	struct failed_shard	*f_shard;
	struct failed_shard	*tmp;
	uint32_t                *grp_map = NULL;
	uint32_t		 grp_map_idx = 0;
	uint32_t		 grp_map_size;
	uint32_t		 grp_map_array[STACK_TGTS_SIZE] = {-1};
	/* holds number of "extra" shards for the group in the new_shards */
	uint32_t                *grp_count = NULL;
	uint32_t		 grp_cnt_array[STACK_TGTS_SIZE] = {0};
	uint32_t                 max_fail_grp;
	uint32_t		 new_group_size;
	uint32_t		 grp;
	uint32_t		 grp_idx;
	uint32_t		 new_shards_nr;
	int			 i, j, k = 0;
	int			 rc = 0;

	/* Empty list, no extension needed */
	if (d_list_empty(extended_list))
		goto out;

	grp_map = grp_map_array;
	grp_map_size = STACK_TGTS_SIZE;
	if (layout->ol_grp_nr <= STACK_TGTS_SIZE) {
		grp_count = grp_cnt_array;
	} else {
		D_ALLOC_ARRAY(grp_count, layout->ol_grp_nr);
		if (grp_count == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	i = 0;
	max_fail_grp = 0;

	/* Eliminate duplicate targets and calculate the grp number. */
	d_list_for_each_entry_safe(f_shard, tmp, extended_list, fs_list) {
		if (grp_map_is_set(grp_map, grp_map_idx, f_shard->fs_tgt_id)) {
			d_list_del_init(&f_shard->fs_list);
			D_FREE_PTR(f_shard);
			continue;
		}

		if (grp_map_idx >= grp_map_size) {
			uint32_t *new_grp_map;

			new_grp_map = grp_map_extend(grp_map, &grp_map_size);
			if (new_grp_map == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			grp_map = new_grp_map;
		}
		grp_map[grp_map_idx++] = f_shard->fs_tgt_id;
		grp = f_shard->fs_shard_idx / layout->ol_grp_size;
		grp_count[grp]++;
		if (max_fail_grp < grp_count[grp])
			max_fail_grp = grp_count[grp];
	}

	new_group_size = layout->ol_grp_size + max_fail_grp;
	new_shards_nr = new_group_size * layout->ol_grp_nr;
	D_ALLOC_ARRAY(new_shards, new_shards_nr);
	if (new_shards == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	while (k < layout->ol_nr) {
		for (j = 0; j < layout->ol_grp_size; ++j, ++k, ++i)
			new_shards[i] = layout->ol_shards[k];
		for (; j < new_group_size; ++j, ++i) {
			new_shards[i].po_shard = -1;
			new_shards[i].po_target = -1;
		}
	}

	d_list_for_each_entry(f_shard, extended_list, fs_list) {
		/* get the group number for this shard */
		grp = f_shard->fs_shard_idx / layout->ol_grp_size;
		/* grp_idx will be the last shard index within the group */
		grp_idx = (grp * new_group_size) + (layout->ol_grp_size - 1);
		/* grp_idx will be the index to one of the "new" shards in the
		 * group array
		 */
		grp_idx += grp_count[grp];
		grp_count[grp]--;

		new_shards[grp_idx].po_fseq = f_shard->fs_fseq;
		new_shards[grp_idx].po_shard = f_shard->fs_shard_idx;
		new_shards[grp_idx].po_target = f_shard->fs_tgt_id;
		if (f_shard->fs_status != PO_COMP_ST_DRAIN)
			new_shards[grp_idx].po_rebuilding = 1;
	}

	layout->ol_grp_size += max_fail_grp;
	layout->ol_nr = layout->ol_grp_size * layout->ol_grp_nr;

	D_FREE(layout->ol_shards);
	layout->ol_shards = new_shards;

out:
	if (grp_map != grp_map_array && grp_map != NULL)
		D_FREE(grp_map);
	if (grp_count != grp_cnt_array && grp_count != NULL)
		D_FREE(grp_count);
	return rc;
}

bool
is_pool_adding(struct pool_domain *dom)
{
	uint32_t child_nr;

	while (dom->do_children &&
	       dom->do_comp.co_status != PO_COMP_ST_NEW) {
		child_nr = dom->do_child_nr;
		dom = &dom->do_children[child_nr - 1];
	}

	if (dom->do_comp.co_status == PO_COMP_ST_NEW)
		return true;

	child_nr = dom->do_target_nr;
	if (dom->do_targets[child_nr - 1].ta_comp.co_status == PO_COMP_ST_NEW)
		return true;

	return false;
}
