/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos_sr
 *
 * src/placement/pl_map.h
 */

#ifndef __PL_MAP_H__
#define __PL_MAP_H__

#include <daos/placement.h>
#include <isa-l.h>

struct pl_map_ops;

/**
 * Function table for placement map.
 */
struct pl_map_ops {
	/** create a placement map */
	int (*o_create)(struct pool_map *poolmap,
			struct pl_map_init_attr *mia,
			struct pl_map **mapp);
	/** destroy a placement map */
	void (*o_destroy)(struct pl_map *map);
	/** query placement map attributes */
	int (*o_query)(struct pl_map *map, struct pl_map_attr *attr);
	/** print debug information of a placement map */
	void (*o_print)(struct pl_map *map);

	/** object methods */
	/** see \a pl_map_obj_select and \a pl_map_obj_rebalance */
	int (*o_obj_place)(struct pl_map *map,
			   uint32_t layout_gl_version,
			   struct daos_obj_md *md,
			   unsigned int	mode,
			   struct daos_obj_shard_md *shard_md,
			   struct pl_obj_layout **layout_pp);
	/** see \a pl_map_obj_rebuild */
	int (*o_obj_find_rebuild)(struct pl_map *map,
				  uint32_t layout_gl_version,
				  struct daos_obj_md *md,
				  struct daos_obj_shard_md *shard_md,
				  uint32_t rebuild_ver,
				  uint32_t *tgt_rank,
				  uint32_t *shard_id,
				  unsigned int array_size);
};

unsigned int pl_obj_shard2grp_head(struct daos_obj_shard_md *shard_md,
				   struct daos_oclass_attr *oc_attr);
unsigned int pl_obj_shard2grp_index(struct daos_obj_shard_md *shard_md,
				    struct daos_oclass_attr *oc_attr);

/** Common placement map functions */

/**
 * This struct is used to to hold information while
 * finding rebuild targets for shards located on unavailable
 * targets.
 */
struct failed_shard {
	d_list_t        fs_list;
	void		*fs_data;
	uint32_t        fs_shard_idx;
	uint32_t        fs_fseq;
	uint32_t        fs_tgt_id;
	uint16_t	fs_rank;
	uint8_t		fs_index;
	uint8_t         fs_status;
};

#define	DF_FAILEDSHARD "shard_idx: %d, fseq: %d, tgt_id: %d, status: %d"
#define	DP_FAILEDSHARD(x) (x).fs_shard_idx, (x).fs_fseq, \
			(x).fs_tgt_id, (x).fs_status

/**
 * layout_gen_mode indicate how the layout is generated:
 *
 * PRE_REBUILD means the targets statuses need to be converted to the original
 * status before rebuild, for example UP --> NEW, DOWN --> UPIN, during layout
 * generation.
 *
 * CURRENT means the target status will not change during layout generation.
 *
 * POST_REBUILD means the targets statuses need to be converted to the status
 * after rebuild finished, for example UP --> UPIN, DOWN --> DOWNOUT, during
 * layout generation.
 */
enum layout_gen_mode {
	PRE_REBUILD	= 0,
	CURRENT		= 1,
	POST_REBUILD	= 2,
};

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

void
remap_add_one(d_list_t *remap_list, struct failed_shard *f_new);
void
reint_add_one(d_list_t *remap_list, struct failed_shard *f_new);

int
remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		struct pool_target *tgt, bool for_reint, void *data);

int
remap_insert_copy_one(d_list_t *remap_list, struct failed_shard *original);

void
remap_list_free_all(d_list_t *remap_list);

void
remap_dump(d_list_t *remap_list, struct daos_obj_md *md,
	   char *comment);

int
op_get_grp_size(unsigned int domain_nr, unsigned int *grp_size,
		daos_obj_id_t oid);

int
remap_list_fill(struct pl_map *map, struct daos_obj_md *md,
		struct daos_obj_shard_md *shard_md, uint32_t rebuild_ver,
		uint32_t *tgt_id, uint32_t *shard_idx,
		unsigned int array_size, int *idx,
		struct pl_obj_layout *layout, d_list_t *remap_list,
		bool fill_addition);

int
determine_valid_spares(struct pool_target *spare_tgt, struct daos_obj_md *md,
		       bool spare_avail, d_list_t *remap_list, uint32_t allow_version,
		       enum layout_gen_mode gen_mode, struct failed_shard *f_shard,
		       struct pl_obj_shard *l_shard, bool *is_extending);

int
spec_place_rank_get(unsigned int *pos, daos_obj_id_t oid,
		    struct pool_map *pl_poolmap);

int
pl_map_extend(struct pl_obj_layout *layout, d_list_t *extended_list);

bool
is_comp_avaible(struct pool_component *comp, uint32_t allow_version,
		enum layout_gen_mode gen_mode);
bool
need_remap_comp(struct pool_component *comp, uint32_t allow_status,
		enum layout_gen_mode gen_mode);

#endif /* __PL_MAP_H__ */
