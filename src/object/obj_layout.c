/*
 *  (C) Copyright 2016-2023 Intel Corporation.
 *  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object layout operation.
 */
#define D_LOGFAC	DD_FAC(object)

#include "obj_internal.h"

/* Choose group by hash */
int
obj_pl_grp_idx(uint32_t layout_gl_ver, uint64_t hash, uint32_t grp_nr)
{
	return d_hash_jump(hash, grp_nr);
}

/* Choose EC start offset within the group. */
int
obj_ec_grp_start(uint16_t layout_gl_ver, uint64_t hash, uint32_t grp_size)
{
	if (layout_gl_ver == 0)
		return 0;

	D_ASSERT(grp_size > 0);
	return hash % grp_size;
}

/* Generate the object layout */
int
obj_pl_place(struct pl_map *map, uint16_t layout_gl_ver, struct daos_obj_md *md,
	     unsigned int mode, struct daos_obj_shard_md *shard_md,
	     struct pl_obj_layout **layout_pp)
{
	return pl_obj_place(map, layout_gl_ver, md, mode, shard_md, layout_pp);
}

/* Find out the difference between different layouts */
int
obj_layout_diff(struct pl_map *map, daos_unit_oid_t oid, uint32_t new_ver, uint32_t old_ver,
		struct daos_obj_md *md, uint32_t *tgts, uint32_t *shards, int array_size)
{
	struct pl_obj_layout	*new_layout = NULL;
	struct pl_obj_layout	*old_layout = NULL;
	uint32_t		shard = oid.id_shard;
	int			rc;

	if (new_ver == old_ver)
		return 0;

	rc = pl_obj_place(map, new_ver, md, 0, NULL, &new_layout);
	if (rc)
		D_GOTO(out, rc);

	rc = pl_obj_place(map, old_ver, md, 0, NULL, &old_layout);
	if (rc)
		D_GOTO(out, rc);

	/* If the new layout changes dkey placement, i.e. dkey->grp, dkey->ec_start changes,
	 * then all shards needs to be changed.
	 */
	if (new_ver == 1 && daos_obj_id_is_ec(oid.id_pub)) {
		struct daos_oclass_attr	*oc;
		unsigned int	grp_size;
		unsigned int	grp_start;
		int		i;

		oc = daos_oclass_attr_find(oid.id_pub, NULL);
		D_ASSERT(oc != NULL);
		grp_size = daos_oclass_grp_size(oc);

		D_ASSERT(grp_size < array_size);
		grp_start = (shard / grp_size) * grp_size;
		for (i = 0; i < grp_size; i++) {
			tgts[i] = new_layout->ol_shards[grp_start + i].po_target;
			shards[i] = grp_start + i;
			D_DEBUG(DB_TRACE, "i %d tgts[i] %u shards %u grp_size %u\n", i, tgts[i], shards[i], grp_size);
		}
		D_GOTO(out, rc = grp_size);
	}

	if (new_layout->ol_shards[shard].po_target != old_layout->ol_shards[shard].po_target) {
		*tgts = new_layout->ol_shards[shard].po_target;
		*shards = shard;
		D_GOTO(out, rc = 1);
	}

out:
	if (new_layout)
		pl_obj_layout_free(new_layout);
	if (old_layout != NULL)
		pl_obj_layout_free(old_layout);

	return rc;
}

void
obj_dump_grp_layout(daos_handle_t oh, uint32_t shard)
{
	struct dc_object    *obj;
	struct dc_obj_shard *obj_shard;
	uint32_t             grp_idx, i, nr;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL) {
		D_INFO("invalid oh");
		return;
	}
	if (shard >= obj->cob_shards_nr) {
		D_ERROR("bad shard %d, cob_shards_nr %d", shard, obj->cob_shards_nr);
		goto out;
	}

	grp_idx = shard / obj->cob_grp_size;
	D_INFO(DF_OID " shard %d, grp_idx %d, grp_size %d, map_ver %d", DP_OID(obj->cob_md.omd_id),
	       shard, grp_idx, obj->cob_grp_size, obj->cob_version);
	for (i = grp_idx * obj->cob_grp_size, nr = 0; nr < obj->cob_grp_size; i++, nr++) {
		obj_shard = &obj->cob_shards->do_shards[i];
		D_INFO("shard %d/%d/%d, tgt_id %d, rank %d, tgt_idx %d, "
		       "rebuilding %d, reintegrating %d, fseq %d",
		       i, obj_shard->do_shard_idx, obj_shard->do_shard, obj_shard->do_target_id,
		       obj_shard->do_target_rank, obj_shard->do_target_idx,
		       obj_shard->do_rebuilding, obj_shard->do_reintegrating, obj_shard->do_fseq);
	}

out:
	obj_decref(obj);
}
