/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include "place_obj_common.h"
#include <daos_obj_class.h>
#include <daos/pool_map.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>

void
print_layout(struct pl_obj_layout *layout)
{
	int grp;
	int sz;
	int index;

	for (grp = 0; grp < layout->ol_grp_nr; ++grp) {
		printf("[");
		for (sz = 0; sz < layout->ol_grp_size; ++sz) {
			struct pl_obj_shard shard;

			index = (grp * layout->ol_grp_size) + sz;
			shard = layout->ol_shards[index];
			printf("%d=>%d%s ", shard.po_shard, shard.po_target,
			       shard.po_rebuilding ? "R" : "");
		}
		printf("\b]");
	}
	printf("\n");
}

int
plt_obj_place(daos_obj_id_t oid, struct pl_obj_layout **layout,
		struct pl_map *pl_map, bool print_layout_flag)
{
	struct daos_obj_md	 md;
	int			 rc;

	memset(&md, 0, sizeof(md));
	md.omd_id  = oid;
	D_ASSERT(pl_map != NULL);
	md.omd_ver = pool_map_get_version(pl_map->pl_poolmap);

	rc = pl_obj_place(pl_map, &md, NULL, layout);

	if (print_layout_flag) {
		if (*layout != NULL)
			print_layout(*layout);
		else
			print_message("No layout created.\n");
	}

	return rc;
}

/*
 * Verifies that num_allowed_failures (-1 target) is not exceeded and the same
 * target isn't used more than once.
 */
void
plt_obj_layout_check(struct pl_obj_layout *layout, uint32_t pool_size,
		int num_allowed_failures)
{
	int i;
	int target_num;
	uint8_t *target_set;

	D_ALLOC_ARRAY(target_set, pool_size);
	D_ASSERT(target_set != NULL);

	for (i = 0; i < layout->ol_nr; i++) {
		target_num = layout->ol_shards[i].po_target;

		if (target_num == -1)
			num_allowed_failures--;
		D_ASSERT(num_allowed_failures >= 0);

		if (target_num != -1) {
			D_ASSERT(target_set[target_num] != 1);
			target_set[target_num] = 1;
		}
	}
	D_FREE(target_set);
}

void
plt_obj_rebuild_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *down_tgts, int num_down, int num_spares_left,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids)
{
	uint32_t	curr_tgt_id;
	uint32_t	spare_id;
	int		i, layout_idx;

	/* Rebuild for DOWN targets should not generate an extended layout */
	D_ASSERT(layout->ol_nr == org_layout->ol_nr);

	/* Rebuild targets should be no more than down targets */
	D_ASSERT(num_spares_returned <= num_down);

	/* If rebuild returns targets they should be in the layout */
	for (i = 0; i < num_spares_returned; ++i) {
		spare_id = spare_tgt_ranks[i];
		for (layout_idx = 0; layout_idx < layout->ol_nr; ++layout_idx) {
			if (spare_id == layout->ol_shards[layout_idx].po_target)
				break;
		}
		D_ASSERT(layout_idx < layout->ol_nr);

		/* Target IDs for spare target shards should be -1 */
		curr_tgt_id = layout->ol_shards[shard_ids[i]].po_target;
		D_ASSERT(curr_tgt_id == spare_id);
	}

	/* Down targets should not be in the layout */
	for (i = 0; i < num_down; ++i) {
		spare_id = down_tgts[i];
		for (layout_idx = 0; layout_idx < layout->ol_nr; ++layout_idx) {
			curr_tgt_id = layout->ol_shards[layout_idx].po_target;
			D_ASSERT(spare_id != curr_tgt_id);
		}
	}
}

void
plt_obj_drain_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *draining_tgts, int num_draining, int num_spares,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids)
{
	uint32_t	curr_tgt_id;
	uint32_t	spare_id;
	bool		contains_drain_tgt;
	int		i, layout_idx, org_idx;

	contains_drain_tgt = false;
	/* If layout before draining does not contain the element being drained
	 * then skip most tests, this layout shouldn't be effected
	 */
	for (i = 0; i < num_draining; ++i) {
		spare_id = draining_tgts[i];
		for (org_idx = 0; org_idx < org_layout->ol_nr; ++org_idx) {
			curr_tgt_id = org_layout->ol_shards[org_idx].po_target;
			if (spare_id == curr_tgt_id) {
				contains_drain_tgt = true;
				break;
			}
		}

		if (org_idx < org_layout->ol_nr)
			break;
	}

	if (contains_drain_tgt == false) {
		D_ASSERT(layout->ol_nr == org_layout->ol_nr);
		D_ASSERT(num_spares_returned == 0);
		return;
	}

	/* Rebuild targets should be no more than down targets */
	D_ASSERT(num_spares_returned <= num_draining);

	/* If rebuild returns targets they should be in the layout */
	for (i = 0; i < num_spares_returned; ++i) {
		spare_id = spare_tgt_ranks[i];
		for (layout_idx = 0; layout_idx < layout->ol_nr; ++layout_idx) {
			if (spare_id == layout->ol_shards[layout_idx].po_target)
				break;
		}
		D_ASSERT(layout_idx < layout->ol_nr);

	}

	/* Draining targets should be in the layout */
	for (i = 0; i < num_draining; ++i) {
		spare_id = draining_tgts[i];
		for (layout_idx = 0; layout_idx < layout->ol_nr; ++layout_idx) {
			if (spare_id == layout->ol_shards[layout_idx].po_target)
				break;
		}
		D_ASSERT(layout_idx < layout->ol_nr);
	}
}

void
plt_obj_reint_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *reint_tgts, int num_reint, int num_spares,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids)
{
	uint32_t	reint_id;
	uint32_t	curr_tgt_id;
	uint8_t		*target_set;
	bool		contains_reint_tgt;
	int		i;

	D_ALLOC_ARRAY(target_set, pool_size);
	D_ASSERT(target_set != NULL);

	/*
	 * If org_layout does not contain a target to be reintegrated
	 * then the layout should be the same as before reintegration
	 * started
	 */
	contains_reint_tgt = false;
	for (i = 0; i < org_layout->ol_nr; ++i) {
		curr_tgt_id = org_layout->ol_shards[i].po_target;
		if (curr_tgt_id != -1)
			target_set[curr_tgt_id] = 1;
	}

	for (i = 0; i < num_reint; ++i) {
		if (target_set[reint_tgts[i]] == 1) {
			contains_reint_tgt = true;
			target_set[reint_tgts[i]] = 2;
		}
	}

	if (contains_reint_tgt == false) {
		D_ASSERT(plt_obj_layout_match(layout, org_layout));
		D_ASSERT(num_spares_returned == 0);
		D_FREE(target_set);
		return;
	}

	/* Layout should be extended */
	D_ASSERT(org_layout->ol_nr < layout->ol_nr);

	/* Rebuild targets should be no more than down targets */
	D_ASSERT(num_spares_returned > 0);
	D_ASSERT(num_spares_returned <= num_reint);

	/* Layout should contain targets returned by rebuild */
	for (i = 0; i < num_spares_returned; ++i) {
		reint_id = spare_tgt_ranks[i];
		D_ASSERT(target_set[reint_id] == 2);
	}
	D_FREE(target_set);
}

void
plt_obj_add_layout_check(struct pl_obj_layout *layout,
			 struct pl_obj_layout *org_layout, uint32_t pool_size,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids)
{
	uint32_t	curr_tgt_id;
	uint32_t	spare_id;
	uint8_t		*target_set;
	bool		contains_new_tgt;
	int		i;

	D_ALLOC_ARRAY(target_set, pool_size);
	D_ASSERT(target_set != NULL);

	/*
	 * If org_layout does not contain a target to be reintegrated
	 * then the layout should be the same as before reintegration
	 * started
	 */
	contains_new_tgt = false;
	for (i = 0; i < org_layout->ol_nr; ++i) {
		curr_tgt_id = org_layout->ol_shards[i].po_target;
		if (curr_tgt_id != -1)
			target_set[curr_tgt_id] = 1;
	}

	for (i = 0; i < layout->ol_nr; ++i) {
		curr_tgt_id = layout->ol_shards[i].po_target;
		if (curr_tgt_id != -1) {
			contains_new_tgt = true;
			target_set[curr_tgt_id] = 2;
		}
	}

	if (contains_new_tgt == false || org_layout->ol_nr == layout->ol_nr) {
		D_ASSERT(plt_obj_layout_match(layout, org_layout));
		D_ASSERT(num_spares_returned == 0);
		D_FREE(target_set);
		return;
	}

	/* Layout should be extended */
	D_ASSERT(org_layout->ol_nr < layout->ol_nr);

	/* we should have new targets */
	D_ASSERT(num_spares_returned > 0);

	/* Layout should contain targets returned by rebuild */
	for (i = 0; i < num_spares_returned; ++i) {
		spare_id = spare_tgt_ranks[i];
		D_ASSERT(target_set[spare_id] == 2);
	}
	D_FREE(target_set);
}
void
plt_obj_rebuild_unique_check(uint32_t *shard_ids, uint32_t num_shards,
			     uint32_t pool_size)
{
	int i;
	int  target_num;
	uint8_t *target_set;

	D_ALLOC_ARRAY(target_set, pool_size);
	D_ASSERT(target_set != NULL);

	for (i = 0; i < num_shards; i++) {
		target_num = shard_ids[i];

		D_ASSERT(target_set[target_num] != 1);
		target_set[target_num] = 1;
	}

	D_FREE(target_set);
}

bool
plt_obj_layout_match(struct pl_obj_layout *lo_1, struct pl_obj_layout *lo_2)
{
	int	i;

	if (lo_1->ol_nr != lo_2->ol_nr)
		return false;

	for (i = 0; i < lo_1->ol_nr; i++) {
		if (lo_1->ol_shards[i].po_target !=
		    lo_2->ol_shards[i].po_target)
			return false;
	}

	return true;
}

static pool_comp_type_t
plt_next_level(pool_comp_type_t current)
{
	switch (current) {
	case PO_COMP_TP_ROOT:
		return PO_COMP_TP_NODE;
	case PO_COMP_TP_NODE:
		return PO_COMP_TP_RANK;
	case PO_COMP_TP_RANK:
	default:
		return PO_COMP_TP_TARGET;
	}
}

void
plt_set_domain_status(uint32_t id, int status, uint32_t *ver,
		      struct pool_map *po_map, bool pl_debug_msg,
		      enum pool_comp_type level)
{
	struct pool_domain	*domain;
	char			*str;
	int			 rc;

	switch (status) {
	case PO_COMP_ST_UP:
		str = "PO_COMP_ST_UP";
		break;
	case PO_COMP_ST_UPIN:
		str = "PO_COMP_ST_UPIN";
		break;
	case PO_COMP_ST_DOWN:
		str = "PO_COMP_ST_DOWN";
		break;
	case PO_COMP_ST_DRAIN:
		str = "PO_COMP_ST_DRAIN";
		break;
	case PO_COMP_ST_DOWNOUT:
		str = "PO_COMP_ST_DOWNOUT";
		break;
	case PO_COMP_ST_NEW:
		str = "PO_COMP_ST_NEW";
		break;
	default:
		str = "unknown";
		break;
	}

	rc = pool_map_find_domain(po_map, level, id, &domain);
	D_ASSERT(rc == 1);

	int i;

	for (i = 0; i < domain->do_child_nr; i++) {
		plt_set_domain_status(domain->do_children[i].do_comp.co_id,
				      status, ver,
				      po_map, pl_debug_msg,
				      plt_next_level(level));
	}
	if (level == PO_COMP_TP_RANK) {
		for (i = 0; i < domain->do_target_nr; i++) {
			plt_set_tgt_status(domain->do_targets[i].ta_comp.co_id,
				status, ver, po_map, pl_debug_msg);
		}
	}

	if (pl_debug_msg)
		D_PRINT("set domain id %d, rank %d as %s, ver %d.\n",
			id, domain->do_comp.co_rank, str, *ver);
	domain->do_comp.co_status = status;
	domain->do_comp.co_fseq = *ver;

	pool_map_update_failed_cnt(po_map);
	rc = pool_map_set_version(po_map, *ver);
	D_ASSERT(rc == 0);
}

void
plt_set_tgt_status(uint32_t id, int status, uint32_t *ver,
		struct pool_map *po_map, bool pl_debug_msg)
{
	struct pool_target	*target;
	char			*str;
	int			 rc;

	switch (status) {
	case PO_COMP_ST_UP:
		str = "PO_COMP_ST_UP";
		break;
	case PO_COMP_ST_UPIN:
		str = "PO_COMP_ST_UPIN";
		break;
	case PO_COMP_ST_DOWN:
		str = "PO_COMP_ST_DOWN";
		break;
	case PO_COMP_ST_DRAIN:
		str = "PO_COMP_ST_DRAIN";
		break;
	case PO_COMP_ST_DOWNOUT:
		str = "PO_COMP_ST_DOWNOUT";
		break;
	case PO_COMP_ST_NEW:
		str = "PO_COMP_ST_NEW";
		break;
	default:
		str = "unknown";
		break;
	};

	rc = pool_map_find_target(po_map, id, &target);
	D_ASSERT(rc == 1);
	(*ver)++;
	target->ta_comp.co_status = status;

	if (status == PO_COMP_ST_DRAIN || status == PO_COMP_ST_DOWN)
		target->ta_comp.co_fseq = *ver;
	if (pl_debug_msg)
		D_PRINT("set target id %d, rank %d as %s, ver %d.\n",
			id, target->ta_comp.co_rank, str, *ver);
	pool_map_update_failed_cnt(po_map);
	rc = pool_map_set_version(po_map, *ver);
	D_ASSERT(rc == 0);
}

void
plt_drain_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg)
{
	plt_set_tgt_status(id, PO_COMP_ST_DRAIN, po_ver, po_map, pl_debug_msg);
}

void
plt_fail_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg)
{
	plt_set_tgt_status(id, PO_COMP_ST_DOWN, po_ver, po_map, pl_debug_msg);
}

void
plt_fail_tgt_out(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg)
{
	plt_set_tgt_status(id, PO_COMP_ST_DOWNOUT, po_ver, po_map,
			   pl_debug_msg);
}

void
plt_reint_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg)
{
	plt_set_tgt_status(id, PO_COMP_ST_UP, po_ver, po_map, pl_debug_msg);
}

void
plt_reint_tgt_up(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg)
{
	plt_set_tgt_status(id, PO_COMP_ST_UPIN, po_ver, po_map, pl_debug_msg);
}

void
plt_spare_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *spare_tgt_ranks, bool pl_debug_msg,
		   uint32_t *shard_ids, uint32_t *spare_cnt, uint32_t *po_ver,
		   pl_map_type_t map_type, uint32_t spare_max_nr,
		   struct pool_map *po_map, struct pl_map *pl_map)
{
	struct daos_obj_md	md = { 0 };
	int			i;
	int			rc;

	for (i = 0; i < failed_cnt; i++)
		plt_fail_tgt(failed_tgts[i], po_ver, po_map, pl_debug_msg);

	rc = pl_map_update(pl_uuid, po_map, false, map_type);
	assert_success(rc);
	pl_map = pl_map_find(pl_uuid, oid);
	D_ASSERT(pl_map != NULL);
	dc_obj_fetch_md(oid, &md);
	md.omd_ver = *po_ver;
	*spare_cnt = pl_obj_find_rebuild(pl_map, &md, NULL, *po_ver,
					 spare_tgt_ranks, shard_ids,
					 spare_max_nr);
	D_PRINT("spare_cnt %d for version %d -\n", *spare_cnt, *po_ver);
	for (i = 0; i < *spare_cnt; i++)
		D_PRINT("shard %d, spare target rank %d\n",
			shard_ids[i], spare_tgt_ranks[i]);

	pl_map_decref(pl_map);
	for (i = 0; i < failed_cnt; i++)
		plt_reint_tgt_up(failed_tgts[i], po_ver, po_map, pl_debug_msg);
}

void
gen_pool_and_placement_map(int num_domains, int nodes_per_domain,
			   int vos_per_target, pl_map_type_t pl_type,
			   struct pool_map **po_map_out,
			   struct pl_map **pl_map_out)
{
	struct pool_buf         *buf;
	int                      i;
	struct pl_map_init_attr  mia;
	int                      nr;
	struct pool_component   *comps;
	struct pool_component   *comp;
	int                      rc;

	nr = num_domains + (nodes_per_domain * num_domains) +
	     (num_domains * nodes_per_domain * vos_per_target);
	D_ALLOC_ARRAY(comps, nr);
	D_ASSERT(comps != NULL);

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < num_domains; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = nodes_per_domain;
	}

	for (i = 0; i < num_domains * nodes_per_domain; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RANK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = vos_per_target;
	}

	for (i = 0; i < num_domains * nodes_per_domain * vos_per_target;
	     i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i / vos_per_target;
		comp->co_index	= i % vos_per_target;
		comp->co_ver    = 1;
		comp->co_nr     = 1;
	}

	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	assert_success(rc);

	/* No longer needed, copied into pool buf */
	D_FREE(comps);

	rc = pool_map_create(buf, 1, po_map_out);
	assert_success(rc);

	/* No longer needed, copied into pool map */
	D_FREE(buf);

	mia.ia_type         = pl_type;
	mia.ia_ring.ring_nr = 1;
	mia.ia_ring.domain  = PO_COMP_TP_NODE;

	rc = pl_map_create(*po_map_out, &mia, pl_map_out);
	assert_success(rc);
}

void
gen_pool_and_placement_map_non_standard(int num_domains,
			   int domain_targets[], pl_map_type_t pl_type,
			   struct pool_map **po_map_out,
			   struct pl_map **pl_map_out)
{
	struct pl_map_init_attr	 mia;
	struct pool_buf		*buf;
	struct pool_component	*comps;
	struct pool_component	*comp;
	int			 i;
	int			 nr;
	uint32_t		 node_idx = 0;
	uint32_t		 node_tgt_count = 0;
	int			 rc;

	/* count total components */
	nr = num_domains * 2; /* 1 for rack and 1 for node */
	for (i = 0; i < num_domains; i++)
		nr += domain_targets[i];

	D_ALLOC_ARRAY(comps, nr);
	D_ASSERT(comps != NULL);

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < num_domains; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = 1; /* hard code 1 node each */
	}

	/* Using 1 node for each domain */
	for (i = 0; i < num_domains; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RANK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = domain_targets[i];
	}



	/* what's left are targets */
	for (i = 0; i < nr - (num_domains * 2); i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		if (domain_targets[node_idx] < node_tgt_count) {
			node_idx++;
			node_tgt_count = 0;
		} else {
			node_tgt_count++;
		}
		comp->co_rank   = node_idx;

		comp->co_ver    = 1;
		comp->co_nr     = 1;
	}

	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	assert_success(rc);

	/* No longer needed, copied into pool buf */
	D_FREE(comps);

	rc = pool_map_create(buf, 1, po_map_out);
	assert_success(rc);

	/* No longer needed, copied into pool map */
	D_FREE(buf);

	mia.ia_type         = pl_type;
	mia.ia_ring.ring_nr = 1;
	mia.ia_ring.domain  = PO_COMP_TP_NODE;

	rc = pl_map_create(*po_map_out, &mia, pl_map_out);
	assert_success(rc);
}

void
free_pool_and_placement_map(struct pool_map *po_map_in,
			    struct pl_map *pl_map_in)
{
	struct pool_buf *buf;

	pool_buf_extract(po_map_in, &buf);
	pool_map_decref(po_map_in);
	pool_buf_free(buf);

	pl_map_decref(pl_map_in);

}
void
plt_reint_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *reint_tgts, int reint_cnt,
		   uint32_t *spare_tgt_ranks, uint32_t *shard_ids,
		   uint32_t *spare_cnt, pl_map_type_t map_type,
		   uint32_t spare_max_nr, struct pool_map *po_map,
		   struct pl_map *pl_map, uint32_t *po_ver, bool pl_debug_msg)
{
	struct daos_obj_md	md = { 0 };
	int			i;
	int			rc;

	for (i = 0; i < failed_cnt; i++)
		plt_fail_tgt(failed_tgts[i], po_ver, po_map, pl_debug_msg);

	for (i = 0; i < reint_cnt; i++)
		plt_reint_tgt(reint_tgts[i], po_ver, po_map, pl_debug_msg);

	rc = pl_map_update(pl_uuid, po_map, false, map_type);
	assert_success(rc);
	pl_map = pl_map_find(pl_uuid, oid);
	D_ASSERT(pl_map != NULL);
	dc_obj_fetch_md(oid, &md);
	md.omd_ver = *po_ver;
	rc = pl_obj_find_reint(pl_map, &md, NULL, *po_ver, spare_tgt_ranks,
			       shard_ids, spare_max_nr);

	D_ASSERT(rc >= 0);
	*spare_cnt = rc;

	D_PRINT("reint_cnt %d for version %d -\n", *spare_cnt, *po_ver);
	for (i = 0; i < *spare_cnt; i++)
		D_PRINT("shard %d, spare target rank %d\n",
			shard_ids[i], spare_tgt_ranks[i]);

	pl_map_decref(pl_map);

	for (i = 0; i < reint_cnt; i++)
		plt_reint_tgt_up(reint_tgts[i], po_ver, po_map, pl_debug_msg);

	for (i = 0; i < failed_cnt; i++)
		plt_reint_tgt_up(failed_tgts[i], po_ver, po_map, pl_debug_msg);
}

int
get_object_classes(daos_oclass_id_t **oclass_id_pp)
{
	const uint32_t str_size = (16 << 10);
	char *oclass_names;
	char oclass[64];
	daos_oclass_id_t *oclass_id;
	uint32_t length = 0;
	uint32_t num_oclass = 0;
	uint32_t oclass_str_index = 0;
	uint32_t i, oclass_index;

	D_ALLOC(oclass_names, str_size);
	if (!oclass_names)
		return -1;

	length = daos_oclass_names_list(str_size, oclass_names);

	for (i = 0; i < length; ++i) {
		if (oclass_names[i] == ',')
			num_oclass++;
	}

	D_ALLOC_ARRAY(*oclass_id_pp, num_oclass);

	for (i = 0, oclass_index = 0; i < length; ++i) {
		if (oclass_names[i] == ',') {
			oclass_id = &(*oclass_id_pp)[oclass_index];
			oclass[oclass_str_index] = 0;
			*oclass_id = daos_oclass_name2id(oclass);

			oclass_index++;
			oclass_str_index = 0;
		} else if (oclass_names[i] != ' ') {
			oclass[oclass_str_index] = oclass_names[i];
			oclass_str_index++;
		}
	}
	D_FREE(oclass_names);
	return num_oclass;
}

int
extend_test_pool_map(struct pool_map *map, uint32_t nnodes, d_rank_list_t *rank_list,
		     uint32_t ndomains, uint32_t *domains, bool *updated_p,
		     uint32_t *map_version_p, uint32_t dss_tgt_nr)
{
	struct pool_buf	*map_buf = NULL;
	uint32_t	map_version;
	int		ntargets;
	int		rc;

	ntargets = nnodes * dss_tgt_nr;

	map_version = pool_map_get_version(map) + 1;

	rc = gen_pool_buf(map, &map_buf, map_version, ndomains, nnodes, ntargets, domains,
			  rank_list, dss_tgt_nr);
	assert_success(rc);

	D_ASSERT(map_buf != NULL);
	/* Extend the current pool map */
	rc = pool_map_extend(map, map_version, map_buf);
	if (rc != 0) {
		pool_buf_free(map_buf);
	}

	assert_success(rc);

	return rc;
}

bool
is_max_class_obj(daos_oclass_id_t cid)
{
	struct daos_oclass_attr *oc_attr;
	daos_obj_id_t oid;

	oid.hi = 5;
	oid.lo = rand();
	daos_obj_set_oid(&oid, 0, cid, 0);
	oc_attr = daos_oclass_attr_find(oid, NULL);

	if (oc_attr->ca_grp_nr == DAOS_OBJ_GRP_MAX ||
	    oc_attr->u.rp.r_num == DAOS_OBJ_REPL_MAX)
		return true;
	return false;
}
