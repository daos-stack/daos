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
/**
 * This file is part of daos_sr
 *
 * src/placement/pl_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <gurt/hash.h>

extern struct pl_map_ops        ring_map_ops;
extern struct pl_map_ops        jump_map_ops;

/** dictionary for all unknown placement maps */
struct pl_map_dict {
	/** type of the placement map */
	pl_map_type_t            pd_type;
	/** customized functions */
	struct pl_map_ops       *pd_ops;
	/** name of the placement map */
	char                    *pd_name;
};

/** array of defined placement maps */
static struct pl_map_dict pl_maps[] = {
	{
		.pd_type        = PL_TYPE_RING,
		.pd_ops         = &ring_map_ops,
		.pd_name        = "ring",
	},
	{
		.pd_type    = PL_TYPE_JUMP_MAP,
		.pd_ops     = &jump_map_ops,
		.pd_name    = "jump",
	},
	{
		.pd_type        = PL_TYPE_UNKNOWN,
		.pd_ops         = NULL,
		.pd_name        = "unknown",
	},
};


static int
pl_map_create_inited(struct pool_map *pool_map, struct pl_map_init_attr *mia,
		     struct pl_map **pl_mapp)
{
	struct pl_map_dict      *dict = pl_maps;
	struct pl_map           *map;
	int                      rc;

	for (dict = &pl_maps[0]; dict->pd_type != PL_TYPE_UNKNOWN; dict++) {
		if (dict->pd_type == mia->ia_type)
			break;
	}

	if (dict->pd_type == PL_TYPE_UNKNOWN) {
		D_DEBUG(DB_PL,
			"Unknown placement map type %d\n", dict->pd_type);
		return -EINVAL;
	}

	D_DEBUG(DB_PL, "Create a %s placement map\n", dict->pd_name);

	rc = dict->pd_ops->o_create(pool_map, mia, &map);
	if (rc != 0)
		return rc;

	rc = D_SPIN_INIT(&map->pl_lock, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0) {
		dict->pd_ops->o_destroy(map);
		return rc;
	}

	map->pl_ref  = 1; /* for the caller */
	map->pl_connects = 0;
	map->pl_type = mia->ia_type;
	map->pl_ops  = dict->pd_ops;
	D_INIT_LIST_HEAD(&map->pl_link);

	*pl_mapp = map;
	return 0;
}

/**
 * Destroy a placement map
 */
void
pl_map_destroy(struct pl_map *map)
{
	D_ASSERT(map->pl_ref == 0);
	D_ASSERT(map->pl_ops != NULL);
	D_ASSERT(map->pl_ops->o_destroy != NULL);

	D_SPIN_DESTROY(&map->pl_lock);
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
 * Check if the provided object has any shard needs to be rebuilt for the
 * given rebuild version @rebuild_ver.
 *
 * \param  map [IN]             pl_map this check is performed on
 * \param  md  [IN]             object metadata
 * \param  shard_md [IN]        shard metadata (optional)
 * \param  rebuild_ver [IN]     current rebuild version
 * \param  tgt_rank [OUT]       spare target ranks
 * \param  shard_id [OUT]       shard ids to be rebuilt
 * \param  array_size [IN]      array size of tgt_rank & shard_id
 * \prarm  myrank [IN]          rank of current server in communication group

 * \return      > 0     the array size of tgt_rank & shard_id, so it means
 *                      getting the spare targets for the failure shards.
 *              0       No need rebuild or find spare tgts successfully.
 *              -ve     error code.
 */
int
pl_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
		    struct daos_obj_shard_md *shard_md,
		    uint32_t rebuild_ver, uint32_t *tgt_rank,
		    uint32_t *shard_id, unsigned int array_size, int myrank)
{
	D_ASSERT(map->pl_ops != NULL);

	if (!map->pl_ops->o_obj_find_rebuild)
		return -DER_NOSYS;

	return map->pl_ops->o_obj_find_rebuild(map, md, shard_md, rebuild_ver,
					       tgt_rank, shard_id, array_size,
					       myrank);
}

int
pl_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
		    struct daos_obj_shard_md *shard_md,
		    uint32_t reint_ver, uint32_t *tgt_rank,
		    uint32_t *shard_id, unsigned int array_size, int myrank)
{
	D_ASSERT(map->pl_ops != NULL);

	if (!map->pl_ops->o_obj_find_reint)
		return -DER_NOSYS;

	return map->pl_ops->o_obj_find_reint(map, md, shard_md, reint_ver,
					       tgt_rank, shard_id, array_size,
					       myrank);
}

int
pl_obj_find_addition(struct pl_map *map, struct daos_obj_md *md,
		     struct daos_obj_shard_md *shard_md,
		    uint32_t reint_ver, uint32_t *tgt_rank,
		    uint32_t *shard_id, unsigned int array_size, int myrank)
{
	D_ASSERT(map->pl_ops != NULL);

	if (!map->pl_ops->o_obj_find_addition)
		return -DER_NOSYS;

	return map->pl_ops->o_obj_find_addition(map, md, shard_md, reint_ver,
					       tgt_rank, shard_id, array_size,
					       myrank);
}

void
pl_obj_layout_free(struct pl_obj_layout *layout)
{
	if (layout->ol_shards != NULL)
		D_FREE(layout->ol_shards);
	D_FREE(layout);
}

int
pl_obj_layout_alloc(unsigned int grp_size, unsigned int grp_nr,
		struct pl_obj_layout **layout_pp)
{
	struct pl_obj_layout *layout;
	unsigned int shard_nr = grp_size * grp_nr;

	D_ALLOC_PTR(layout);
	if (layout == NULL)
		return -DER_NOMEM;

	layout->ol_nr = shard_nr;
	layout->ol_grp_nr = grp_nr;
	layout->ol_grp_size = grp_size;

	D_ALLOC_ARRAY(layout->ol_shards, layout->ol_nr);
	if (layout->ol_shards == NULL)
		goto failed;

	*layout_pp = layout;
	return 0;
failed:
	pl_obj_layout_free(layout);
	return -DER_NOMEM;
}

/** Dump layout for debugging purposes*/
void
obj_layout_dump(daos_obj_id_t oid, struct pl_obj_layout *layout)
{
	int i;

	D_DEBUG(DB_PL, "dump layout for "DF_OID", ver %d\n",
		DP_OID(oid), layout->ol_ver);

	for (i = 0; i < layout->ol_nr; i++)
		D_DEBUG(DB_PL, "%d: shard_id %d, tgt_id %d, f_seq %d, %s\n",
			i, layout->ol_shards[i].po_shard,
			layout->ol_shards[i].po_target,
			layout->ol_shards[i].po_fseq,
			layout->ol_shards[i].po_rebuilding ?
			"rebuilding" : "healthy");
}

/**
 * Return the index of the first shard of the redundancy group that @shard
 * belongs to.
 */
unsigned int
pl_obj_shard2grp_head(struct daos_obj_shard_md *shard_md,
		      struct daos_oclass_attr *oc_attr)
{
	int sid = shard_md->smd_id.id_shard;

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
	int sid = shard_md->smd_id.id_shard;

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

/** serialize operations on pl_htable */
static pthread_rwlock_t		pl_rwlock = PTHREAD_RWLOCK_INITIALIZER;
/** hash table for placement maps */
static struct d_hash_table	pl_htable;

#define DSR_RING_DOMAIN         PO_COMP_TP_RACK
#define DSR_JUMP_MAP_DOMAIN      PO_COMP_TP_RACK

static void
pl_map_attr_init(struct pool_map *po_map, pl_map_type_t type,
		 struct pl_map_init_attr *mia)
{
	memset(mia, 0, sizeof(*mia));

	switch (type) {
	default:
		D_ASSERTF(0, "Unknown placemet map type: %d.\n", type);
		break;

	case PL_TYPE_RING:
		mia->ia_type         = PL_TYPE_RING;
		mia->ia_ring.domain  = DSR_RING_DOMAIN;
		mia->ia_ring.ring_nr = 1;
		break;
	case PL_TYPE_JUMP_MAP:
		mia->ia_type            = PL_TYPE_JUMP_MAP;
		mia->ia_jump_map.domain  = DSR_JUMP_MAP_DOMAIN;
	}

}

struct pl_map *
pl_link2map(d_list_t *link)
{
	return container_of(link, struct pl_map, pl_link);
}

static uint32_t
pl_hop_key_hash(struct d_hash_table *htab, const void *key,
		unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(uuid_t));
	return *((const uint32_t *)key);
}

static bool
pl_hop_key_cmp(struct d_hash_table *htab, d_list_t *link,
	       const void *key, unsigned int ksize)
{
	struct pl_map *map = pl_link2map(link);

	D_ASSERT(ksize == sizeof(uuid_t));
	return !uuid_compare(map->pl_uuid, key);
}

static void
pl_hop_rec_addref(struct d_hash_table *htab, d_list_t *link)
{
	struct pl_map *map = pl_link2map(link);

	D_SPIN_LOCK(&map->pl_lock);
	map->pl_ref++;
	D_SPIN_UNLOCK(&map->pl_lock);
}

static bool
pl_hop_rec_decref(struct d_hash_table *htab, d_list_t *link)
{
	struct pl_map   *map = pl_link2map(link);
	bool             zombie;

	D_ASSERT(map->pl_ref > 0);

	D_SPIN_LOCK(&map->pl_lock);
	map->pl_ref--;
	zombie = (map->pl_ref == 0);
	D_SPIN_UNLOCK(&map->pl_lock);

	return zombie;
}

void
pl_hop_rec_free(struct d_hash_table *htab, d_list_t *link)
{
	struct pl_map *map = pl_link2map(link);

	D_ASSERT(map->pl_ref == 0);
	pl_map_destroy(map);
}

static d_hash_table_ops_t pl_hash_ops = {
	.hop_key_hash           = pl_hop_key_hash,
	.hop_key_cmp            = pl_hop_key_cmp,
	.hop_rec_addref         = pl_hop_rec_addref,
	.hop_rec_decref         = pl_hop_rec_decref,
	.hop_rec_free           = pl_hop_rec_free,
};

/**
 * Create a placement map based on attributes in \a mia
 */
int
pl_map_create(struct pool_map *pool_map, struct pl_map_init_attr *mia,
	      struct pl_map **pl_mapp)
{
	return pl_map_create_inited(pool_map, mia, pl_mapp);
}

/**
 * Generate a new placement map from the pool map @pool_map, and replace the
 * original placement map for the same pool.
 *
 * \param       uuid [IN]       uuid of \a pool_map
 * \param       pool_map [IN]   pool_map
 * \param       connect [IN]    from pool connect or not
 */
int
pl_map_update(uuid_t uuid, struct pool_map *pool_map, bool connect,
		pl_map_type_t default_type)
{
	d_list_t                *link;
	struct pl_map           *map;
	struct pl_map_init_attr  mia;
	int                      rc;

	D_RWLOCK_WRLOCK(&pl_rwlock);

	link = d_hash_rec_find(&pl_htable, uuid, sizeof(uuid_t));
	if (!link) {
		pl_map_attr_init(pool_map, default_type, &mia);
		rc = pl_map_create_inited(pool_map, &mia, &map);
		if (rc != 0)
			D_GOTO(out, rc);
	} else {
		struct pl_map   *tmp;

		tmp = container_of(link, struct pl_map, pl_link);
		if (pl_map_version(tmp) >= pool_map_get_version(pool_map)) {
			if (connect)
				tmp->pl_connects++;
			d_hash_rec_decref(&pl_htable, link);
			D_GOTO(out, rc = 0);
		}

		pl_map_attr_init(pool_map, PL_TYPE_JUMP_MAP, &mia);
		rc = pl_map_create_inited(pool_map, &mia, &map);
		if (rc != 0) {
			d_hash_rec_decref(&pl_htable, link);
			D_GOTO(out, rc);
		}

		/* transfer the pool connection count */
		map->pl_connects = tmp->pl_connects;

		/* evict the old placement map for this pool */
		d_hash_rec_delete_at(&pl_htable, link);
		d_hash_rec_decref(&pl_htable, link);
	}

	if (connect)
		map->pl_connects++;

	/* insert the new placement map into hash table */
	uuid_copy(map->pl_uuid, uuid);
	rc = d_hash_rec_insert(&pl_htable, uuid, sizeof(uuid_t),
			       &map->pl_link, true);
	D_ASSERT(rc == 0);
	pl_map_decref(map); /* hash table has held the refcount */
out:
	D_RWLOCK_UNLOCK(&pl_rwlock);
	return rc;
}

/**
 * Drop the pool connection count of pl_map identified by \a uuid, evit it
 * from the placement map cache when connection count drops to zero.
 */
void
pl_map_disconnect(uuid_t uuid)
{
	d_list_t        *link;

	D_RWLOCK_WRLOCK(&pl_rwlock);
	link = d_hash_rec_find(&pl_htable, uuid, sizeof(uuid_t));
	if (link) {
		struct pl_map   *map;

		map = container_of(link, struct pl_map, pl_link);
		D_ASSERT(map->pl_connects > 0);
		map->pl_connects--;
		if (map->pl_connects == 0)
			d_hash_rec_delete_at(&pl_htable, link);

		/* Drop the reference held by above d_hash_rec_find(). */
		d_hash_rec_decref(&pl_htable, link);
	}
	D_RWLOCK_UNLOCK(&pl_rwlock);
}

/**
 * Find the placement map of the pool identified by \a uuid.
 */
struct pl_map *
pl_map_find(uuid_t uuid, daos_obj_id_t oid)
{
	d_list_t        *link;

	D_RWLOCK_RDLOCK(&pl_rwlock);
	link = d_hash_rec_find(&pl_htable, uuid, sizeof(uuid_t));
	D_RWLOCK_UNLOCK(&pl_rwlock);

	return link ? pl_link2map(link) : NULL;
}

void
pl_map_addref(struct pl_map *map)
{
	d_hash_rec_addref(&pl_htable, &map->pl_link);
}

void
pl_map_decref(struct pl_map *map)
{
	d_hash_rec_decref(&pl_htable, &map->pl_link);
}

uint32_t
pl_map_version(struct pl_map *map)
{
	return map->pl_poolmap ? pool_map_get_version(map->pl_poolmap) : 0;
}

/**
 * Select leader replica for the given object's shard.
 *
 * \param [IN]  oid             The object identifier.
 * \param [IN]  shard_idx       The shard index.
 * \param [IN]  grp_size        Group size of obj layout.
 * \param [IN]  for_tgt_id      Require leader target id or leader shard index.
 * \param [IN]  pl_get_shard    The callback function to parse out pl_obj_shard
 *                              from the given @data.
 * \param [IN]  data            The parameter used by the @pl_get_shard.
 *
 * \return                      The selected leader on success: its tgt_id or
 *                              shard index. Negative value if error.
 */
int
pl_select_leader(daos_obj_id_t oid, uint32_t shard_idx, uint32_t grp_size,
		 bool for_tgt_id, pl_get_shard_t pl_get_shard, void *data)
{
	struct pl_obj_shard             *shard;
	struct daos_oclass_attr         *oc_attr;
	uint32_t                         replicas;
	int                              preferred;
	int                              rdg_idx;
	int                              start;
	int                              pos;
	int                              off;
	int                              replica_idx;
	int                              i;

	oc_attr = daos_oclass_attr_find(oid);
	if (oc_attr->ca_resil != DAOS_RES_REPL) {
		/* For EC object, elect last shard in the group (must to be
		 * a parity node) as leader.
		 */
		shard = pl_get_shard(data,
				rounddown(shard_idx, grp_size) + grp_size - 1);
		if (for_tgt_id)
			return shard->po_target;

		return shard->po_shard;
	}

	replicas = oc_attr->u.rp.r_num;
	if (replicas == DAOS_OBJ_REPL_MAX)
		replicas = grp_size;

	if (replicas < 1)
		return -DER_INVAL;

	if (replicas == 1) {
		shard = pl_get_shard(data, shard_idx);
		if (shard->po_target == -1)
			return -DER_IO;

		/* Single replicated object will not rebuild. */
		D_ASSERT(!shard->po_rebuilding);
		/* During target adding, it will add some -1 targets
		 * into the object layout, so this assert is not right
		 * anymore. see pl_map_extend().
		 */
		/*D_ASSERT(shard->po_shard == shard_idx);*/

		if (for_tgt_id)
			return shard->po_target;

		return shard->po_shard;
	}

	/* XXX: The shards within [start, start + replicas) will search from
	 *      the same @preferred position, then they will have the same
	 *      leader. The shards (belonging to the same object) in
	 *      other redundancy group may get different leader node.
	 *
	 *      The one with the lowest f_seq will be elected as the leader
	 *      to avoid leader switch.
	 */
	rdg_idx = shard_idx / grp_size;
	start = rdg_idx * grp_size;
	replica_idx = (oid.lo + rdg_idx) % grp_size;
	preferred = start + replica_idx;

	for (i = 0, off = preferred, pos = -1; i < replicas;
	     i++, replica_idx = (replica_idx + 1) % replicas,
	     off = start + replica_idx) {
		shard = pl_get_shard(data, off);
		/*
		 * shard->po_shard != off is necessary because during
		 * reintegration we may have an extended layout and we don't
		 * want the extended target to be the leader.
		 */
		if (shard->po_target == -1 || shard->po_rebuilding
		    || shard->po_shard != off)
			continue;
		if (pos == -1 ||
		    pl_get_shard(data, pos)->po_fseq > shard->po_fseq)
			pos = off;
	}
	if (pos != -1) {
		D_ASSERT(pl_get_shard(data, pos)->po_shard == pos);

		if (for_tgt_id)
			return pl_get_shard(data, pos)->po_target;

		return pl_get_shard(data, pos)->po_shard;
	}

	/* If all the replicas are failed or in-rebuilding, then EIO. */
	return -DER_IO;
}

#define PL_HTABLE_BITS 7

/** Initialize the placement module. */
int pl_init(void)
{
	return d_hash_table_create_inplace(D_HASH_FT_NOLOCK, PL_HTABLE_BITS,
					   NULL, &pl_hash_ops, &pl_htable);
}

/** Finalize the placement module. */
void pl_fini(void)
{
	d_hash_table_destroy_inplace(&pl_htable, true /* force */);
}
