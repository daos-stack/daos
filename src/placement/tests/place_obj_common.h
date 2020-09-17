/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(tests)

#ifndef __PL_MAP_COMMON_H__
#define __PL_MAP_COMMON_H__

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>

void
print_layout(struct pl_obj_layout *layout);

void
plt_obj_place(daos_obj_id_t oid, struct pl_obj_layout **layout,
		struct pl_map *pl_map, bool print_layout);

void
plt_obj_layout_check(struct pl_obj_layout *layout, uint32_t pool_size,
		int num_allowed_failures);

void
plt_obj_rebuild_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *down_tgts, int num_down, int num_spares_left,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_drain_layout_check(struct pl_obj_layout *layout,
		struct pl_obj_layout *org_layout, uint32_t pool_size,
		int *draining_tgts, int num_draining, int num_spares,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_add_layout_check(struct pl_obj_layout *layout,
			 struct pl_obj_layout *org_layout, uint32_t pool_size,
		uint32_t num_spares_returned, uint32_t *spare_tgt_ranks,
		uint32_t *shard_ids);

void
plt_obj_reint_layout_check(struct pl_obj_layout *layout,
			   struct pl_obj_layout *org_layout, uint32_t pool_size,
			   int *reint_tgts, int num_reint, int num_spares,
			   uint32_t num_spares_returned,
			   uint32_t *spare_tgt_ranks, uint32_t *shard_ids);

void
plt_obj_rebuild_unique_check(uint32_t *shard_ids, uint32_t num_shards,
		uint32_t pool_size);

bool
plt_obj_layout_match(struct pl_obj_layout *lo_1, struct pl_obj_layout *lo_2);

void
plt_set_tgt_status(uint32_t id, int status, uint32_t ver,
		struct pool_map *po_map, bool pl_debug_msg);

void
plt_drain_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_fail_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_fail_tgt_out(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_reint_tgt(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_reint_tgt_up(uint32_t id, uint32_t *po_ver, struct pool_map *po_map,
		bool pl_debug_msg);

void
plt_spare_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *spare_tgt_ranks, bool pl_debug_msg,
		   uint32_t *shard_ids, uint32_t *spare_cnt, uint32_t *po_ver,
		   pl_map_type_t map_type, uint32_t spare_max_nr,
		   struct pool_map *po_map, struct pl_map *pl_map);

void
gen_pool_and_placement_map(int num_domains, int nodes_per_domain,
			   int vos_per_target, pl_map_type_t pl_type,
			   struct pool_map **po_map_out,
			   struct pl_map **pl_map_out);

void
free_pool_and_placement_map(struct pool_map *po_map_in,
			    struct pl_map *pl_map_in);

void
plt_reint_tgts_get(uuid_t pl_uuid, daos_obj_id_t oid, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *reint_tgts, int reint_cnt,
		   uint32_t *spare_tgt_ranks, uint32_t *shard_ids,
		   uint32_t *spare_cnt, pl_map_type_t map_type,
		   uint32_t spare_max_nr, struct pool_map *po_map,
		   struct pl_map *pl_map, uint32_t *po_ver, bool pl_debug_msg);

int
getObjectClasses(daos_oclass_id_t **oclass_id_pp);

int
extend_test_pool_map(struct pool_map *map, uint32_t nnodes,
		     uuid_t target_uuids[], d_rank_list_t *rank_list,
		uint32_t ndomains, int32_t *domains, bool *updated_p,
		uint32_t *map_version_p, uint32_t dss_tgt_nr);

bool
is_max_class_obj(daos_oclass_id_t cid);
#endif /*   PL_MAP_COMMON_H   */
