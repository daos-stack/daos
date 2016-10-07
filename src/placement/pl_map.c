/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of daos_sr
 *
 * src/placement/pl_map.c
 */
#include "pl_map.h"

extern struct pl_map_ops	ring_map_ops;

/** dictionary for all unknown placement maps */
struct pl_map_dict {
	/** type of the placement map */
	pl_map_type_t		 pd_type;
	/** customized functions */
	struct pl_map_ops	*pd_ops;
	/** name of the placement map */
	char			*pd_name;
};

/** array of defined placement maps */
static struct pl_map_dict pl_maps[] = {
	{
		.pd_type	= PL_TYPE_RING,
		.pd_ops		= &ring_map_ops,
		.pd_name	= "ring",
	},
	{
		.pd_type	= PL_TYPE_UNKNOWN,
		.pd_ops		= NULL,
		.pd_name	= "unknown",
	},
};

/**
 * Create a placement map based on attributes in \a mia
 */
int
pl_map_create(struct pool_map *pool_map, struct pl_map_init_attr *mia,
	      struct pl_map **pl_mapp)
{
	struct pl_map_dict	*dict = pl_maps;
	struct pl_map		*map;
	int			 rc;

	for (dict = &pl_maps[0]; dict->pd_type != PL_TYPE_UNKNOWN; dict++) {
		if (dict->pd_type == mia->ia_type)
			break;
	}

	if (dict->pd_type == PL_TYPE_UNKNOWN) {
		D_DEBUG(DF_PL,
			"Unknown placement map type %d\n", dict->pd_type);
		return -EINVAL;
	}

	D_DEBUG(DF_PL, "Create a %s placement map\n", dict->pd_name);

	rc = dict->pd_ops->o_create(pool_map, mia, &map);
	if (rc != 0)
		return rc;

	map->pl_type = mia->ia_type;
	map->pl_ops  = dict->pd_ops;

	*pl_mapp = map;
	return 0;
}

/**
 * Destroy a placement map
 */
void
pl_map_destroy(struct pl_map *map)
{
	D_ASSERT(map->pl_ops != NULL);
	D_ASSERT(map->pl_ops->o_destroy != NULL);

	map->pl_ops->o_destroy(map);
}

/** Print a placement map, it's optional and for debug only */
void pl_map_print(struct pl_map *map)
{
	D_ASSERT(map->pl_ops != NULL);

	if (map->pl_ops->o_print != NULL)
		map->pl_ops->o_print(map);
}

/**
 * Compute layout for the input object metadata @md. It only generates the
 * layout of the redundancy group that @shard_md belongs to if @shard_md
 * is not NULL.
 */
int
pl_obj_place(struct pl_map *map, struct daos_obj_md *md,
	     struct daos_obj_shard_md *shard_md,
	     struct pl_obj_layout **layout_pp)
{
	D_ASSERT(map->pl_ops != NULL);
	D_ASSERT(map->pl_ops->o_obj_place != NULL);

	return map->pl_ops->o_obj_place(map, md, shard_md, layout_pp);
}

/**
 * Check if the provided object shard needs to be rebuilt due to failure of
 * @tgp_failed.
 *
 * \return	1	Rebuild the object on the returned target @tgt_rebuild.
 *		0	No rebuild.
 *		-ve	error code.
 */
int
pl_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
		    struct daos_obj_shard_md *shard_md,
		    struct pl_target_grp *tgp_failed, uint32_t *tgt_rebuild)
{
	D_ASSERT(map->pl_ops != NULL);

	if (!map->pl_ops->o_obj_find_rebuild)
		return -DER_NOSYS;

	return map->pl_ops->o_obj_find_rebuild(map, md, shard_md, tgp_failed,
					       tgt_rebuild);
}

/**
 * Check if the provided object shard needs to be built on the reintegrated
 * targets @tgp_reint.
 *
 * \return	1	Build the object on the returned target @tgt_reint.
 *		0	Skip this object.
 *		-ve	error code.
 */
int
pl_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
		  struct daos_obj_shard_md *shard_md,
		  struct pl_target_grp *tgp_reint, uint32_t *tgt_reint)
{
	D_ASSERT(map->pl_ops != NULL);

	if (!map->pl_ops->o_obj_find_reint)
		return -DER_NOSYS;

	return map->pl_ops->o_obj_find_reint(map, md, shard_md, tgp_reint,
					     tgt_reint);
}

void
pl_obj_layout_free(struct pl_obj_layout *layout)
{
	if (layout->ol_targets != NULL) {
		D_FREE(layout->ol_targets,
		       layout->ol_nr * sizeof(*layout->ol_targets));
	}

	if (layout->ol_shards != NULL) {
		D_FREE(layout->ol_shards,
		       layout->ol_nr * sizeof(*layout->ol_shards));
	}
	D_FREE_PTR(layout);
}

int
pl_obj_layout_alloc(unsigned int grp_size, unsigned int grp_nr,
		    struct pl_obj_layout **layout_pp)
{
	struct pl_obj_layout *layout;

	D_ALLOC_PTR(layout);
	if (layout == NULL)
		return -DER_NOMEM;

	D_ASSERT(grp_nr > 0);
	D_ASSERT(grp_size > 0);
	layout->ol_nr = grp_nr * grp_size;
	D_ALLOC(layout->ol_targets,
		layout->ol_nr * sizeof(*layout->ol_targets));
	if (layout->ol_targets == NULL)
		goto failed;

	D_ALLOC(layout->ol_shards,
		layout->ol_nr * sizeof(*layout->ol_shards));
	if (layout->ol_shards == NULL)
		goto failed;

	*layout_pp = layout;
	return 0;
 failed:
	pl_obj_layout_free(layout);
	return -DER_NOMEM;
}

/**
 * Return the index of the first shard of the redundancy group that @shard
 * belongs to.
 */
unsigned int
pl_obj_shard2grp_head(struct daos_obj_shard_md *shard_md,
		      struct daos_oclass_attr *oc_attr)
{
	int sid	= shard_md->smd_id.id_shard;

	/* XXX: only for the static stripe classes for the time being */
	D_ASSERT(oc_attr->ca_schema == DAOS_OS_SINGLE ||
		 oc_attr->ca_schema == DAOS_OS_STRIPED);

	switch (oc_attr->ca_resil) {
	default:
		return sid;

	case DAOS_RES_EC:
	case DAOS_RES_REPL:
		return sid - sid % daos_oclass_grp_size(oc_attr);
	}
}
/**
 * Returns the redundancy group index of @shard_md.
 */
unsigned int
pl_obj_shard2grp_index(struct daos_obj_shard_md *shard_md,
		       struct daos_oclass_attr *oc_attr)
{
	int sid	= shard_md->smd_id.id_shard;

	/* XXX: only for the static stripe classes for the time being */
	D_ASSERT(oc_attr->ca_schema == DAOS_OS_SINGLE ||
		 oc_attr->ca_schema == DAOS_OS_STRIPED);

	switch (oc_attr->ca_resil) {
	default:
		return sid; /* no protection */

	case DAOS_RES_EC:
	case DAOS_RES_REPL:
		return sid / daos_oclass_grp_size(oc_attr);
	}
}

/**
 * XXX this should be per-pool.
 */
struct daos_placement_data {
	struct pl_map	*pd_pl_map;
	unsigned int	 pd_ref;
	pthread_mutex_t	 pd_lock;
};

static struct daos_placement_data placement_data = {
	.pd_pl_map	= NULL,
	.pd_ref		= 0,
	.pd_lock	= PTHREAD_MUTEX_INITIALIZER,
};

#define DSR_RING_DOMAIN		PO_COMP_TP_RACK

struct pl_map *
pl_map_find(daos_handle_t coh, daos_obj_id_t oid)
{
	return placement_data.pd_pl_map;
}

/**
 * Initialize placement maps for a pool.
 * XXX: placement maps should be attached on each pool.
 */
int
daos_placement_init(struct pool_map *po_map)
{
	struct pl_map_init_attr	mia;
	int			rc = 0;

	pthread_mutex_lock(&placement_data.pd_lock);
	if (placement_data.pd_pl_map != NULL) {
		D_DEBUG(DF_SR, "Placement map has been referenced %d\n",
			placement_data.pd_ref);
		placement_data.pd_ref++;
		goto out;
	}

	D_ASSERT(placement_data.pd_ref == 0);

	memset(&mia, 0, sizeof(mia));
	mia.ia_ver	    = pool_map_get_version(po_map);
	mia.ia_type	    = PL_TYPE_RING;
	mia.ia_ring.domain  = DSR_RING_DOMAIN;
	mia.ia_ring.ring_nr = 1;

	rc = pl_map_create(po_map, &mia, &placement_data.pd_pl_map);
	if (rc != 0)
		goto out;

	placement_data.pd_ref = 1;
 out:
	pthread_mutex_unlock(&placement_data.pd_lock);
	return rc;
}

/** Finalize placement maps for a pool */
void
daos_placement_fini(struct pool_map *po_map)
{
	pthread_mutex_lock(&placement_data.pd_lock);
	placement_data.pd_ref--;
	if (placement_data.pd_ref == 0) {
		pl_map_destroy(placement_data.pd_pl_map);
		placement_data.pd_pl_map = NULL;
	}
	pthread_mutex_unlock(&placement_data.pd_lock);
}
