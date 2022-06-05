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
obj_layout_diff(daos_unit_oid_t oid, uint32_t new_ver, uint32_t old_ver,
		uint32_t *tgts, uint32_t *shard)
{
	/* If the new layout changes dkey placement, i.e. dkey->grp, dkey->ec_start changes,
	 * then all shards needs to be changed.
	 */
	D_ASSERT(new_ver != old_ver);

	return 0;
}
