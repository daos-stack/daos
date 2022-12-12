/*
 *  (C) Copyright 2016-2022 Intel Corporation.
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
obj_ec_grp_start(uint32_t layout_gl_ver, uint64_t hash, uint32_t grp_size)
{
	if (layout_gl_ver == 0)
		return 0;

	D_ASSERT(grp_size > 0);
	return hash % grp_size;
}

/* Generate the object layout */
int
obj_pl_place(struct pl_map *map, uint32_t layout_gl_ver, struct daos_obj_md *md,
	     unsigned int mode, uint32_t rebuild_ver, struct daos_obj_shard_md *shard_md,
	     struct pl_obj_layout **layout_pp)
{
	return pl_obj_place(map, layout_gl_ver, md, mode, rebuild_ver, shard_md, layout_pp);
}

/* Find out the difference between different layouts */
int
obj_layout_diff(struct pl_map *map, daos_unit_oid_t oid, uint32_t new_ver, uint32_t old_ver,
		struct daos_obj_md *md, uint32_t *tgt, uint32_t *shard_p)
{
	struct pl_obj_layout	*new_layout = NULL;
	struct pl_obj_layout	*old_layout = NULL;
	uint32_t		shard = oid.id_shard;
	int			rc;

	if (new_ver == old_ver)
		return 0;

	rc = pl_obj_place(map, new_ver, md, 0, -1, NULL, &new_layout);
	if (rc)
		D_GOTO(out, rc);

	rc = pl_obj_place(map, old_ver, md, 0, -1, NULL, &old_layout);
	if (rc)
		D_GOTO(out, rc);

	if (new_layout->ol_shards[shard].po_target != old_layout->ol_shards[shard].po_target) {
		*tgt = new_layout->ol_shards[shard].po_target;
		*shard_p = shard;
		D_GOTO(out, rc = 1);
	}

	/* If the new layout changes dkey placement, i.e. dkey->grp, dkey->ec_start changes,
	 * then all shards needs to be changed.
	 */
	if (new_ver == 1) { /* LAYOUT_VER == 1 change dkey mapping */
		*tgt = new_layout->ol_shards[shard].po_target;
		*shard_p = shard;
		D_GOTO(out, rc = 1);
	}

out:
	if (new_layout)
		pl_obj_layout_free(new_layout);
	if (old_layout != NULL)
		pl_obj_layout_free(old_layout);

	return rc;
}
