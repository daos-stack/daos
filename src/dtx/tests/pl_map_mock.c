/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/placement.h>

void
pl_obj_layout_free(struct pl_obj_layout *layout)
{
	assert_true(false);
}

void
pl_map_decref(struct pl_map *map)
{
	assert_true(false);
}

int
pl_obj_place(struct pl_map *map, uint16_t gl_layout_ver, struct daos_obj_md *md, unsigned int mode,
	     struct daos_obj_shard_md *shard_md, struct pl_obj_layout **layout_pp)
{
	assert_true(false);
	return -DER_NOMEM;
}

struct pl_map *
pl_map_find(uuid_t uuid, daos_obj_id_t oid)
{
	assert_true(false);
	return NULL;
}
