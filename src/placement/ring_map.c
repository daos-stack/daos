/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of DSR
 *
 * src/placement/ring_map.c
 */
#define D_LOGFAC	DD_FAC(placement)

#include "pl_map.h"

/** placement ring */
struct pl_ring {
	/** all targets on the ring */
	struct pl_target	*ri_targets;
};

/** ring placement map, it can have multiple rings */
struct pl_ring_map {
	/** common body */
	struct pl_map		 rmp_map;
	/** number of rings, consistent hash ring size */
	unsigned int		 rmp_ring_nr;
	/** fault domain */
	pool_comp_type_t	 rmp_domain;
	/** number of domains */
	unsigned int		 rmp_domain_nr;
	/** total number of targets, consistent hash ring size */
	unsigned int		 rmp_target_nr;
	/** */
	unsigned int		 rmp_target_hbits;
	/** hash stride */
	double			 rmp_stride;
	/** array of rings */
	struct pl_ring		*rmp_rings;
	/** consistent hash ring of rings */
	uint64_t		*rmp_ring_hashes;
	/** consistent hash ring of targets */
	uint64_t		*rmp_target_hashes;
};

struct ring_target {
	/** pointer to pool_target::ta_comp */
	struct pool_component	*rt_comp;
};

struct ring_domain {
	/**
	 * Number of targets within this domain for the provided pool map
	 * version.
	 */
	unsigned int		  rd_target_nr;
	/** pointers to targets within this domain */
	struct ring_target	 *rd_targets;
	/** pointer to pool_domain::do_comp */
	struct pool_component	 *rd_comp;
};

/** helper structure for sorting/shuffling targets/domains */
struct ring_sorter {
	union {
		struct ring_domain	*rs_domains;
		struct ring_target	*rs_targets;
	};
	uint64_t		 rs_seed;
};

/** scratch buffer for shuffling domains and targets */
struct ring_buf {
	struct ring_domain	*rb_domains;
	/** total number of domains in this buffer */
	unsigned int		 rb_domain_nr;
	/** total number of targets in this buffer */
	unsigned int		 rb_target_nr;
};

static void ring_buf_destroy(struct ring_buf *buf);
static void ring_map_destroy(struct pl_map *map);

/** another prime for hash */
#define PL_GOLDEN_PRIME	0x9e37fffffffc0001ULL

static inline uint64_t
pl_hash64(uint64_t key, unsigned int nbits)
{
	return (key * PL_GOLDEN_PRIME) >> (64 - nbits);
}

static inline struct pl_ring_map *
pl_map2rimap(struct pl_map *map)
{
	return container_of(map, struct pl_ring_map, rmp_map);
}

/**
 * Helper functions to shuffle domains/targets and generate pseudo-random
 * rings.
 */

/** compare hashed component IDs */
static int
ring_comp_shuff_cmp(struct pool_component *comp_a,
		    struct pool_component *comp_b,
		    unsigned seed, unsigned int prime)
{
	uint64_t	key_a = comp_a->co_id;
	uint64_t	key_b = comp_b->co_id;

	key_a = d_hash_mix96(seed, key_a % prime, key_a);
	key_b = d_hash_mix96(seed, key_b % prime, key_b);

	if (key_a > key_b)
		return 1;
	if (key_a < key_b)
		return -1;

	if (comp_a->co_id > comp_b->co_id)
		return 1;
	if (comp_a->co_id < comp_b->co_id)
		return -1;

	D_ASSERT(0);
	return 0;
}

/** compare versions of two components */
static int
ring_comp_ver_cmp(struct pool_component *comp_a, struct pool_component *comp_b)
{
	if (comp_a->co_ver > comp_b->co_ver)
		return 1;

	if (comp_a->co_ver < comp_b->co_ver)
		return -1;

	return 0;
}

/** compare versoin of two targets */
static int
ring_target_ver_cmp(void *array, int a, int b)
{
	struct ring_sorter *sorter = (struct ring_sorter *)array;

	return ring_comp_ver_cmp(sorter->rs_targets[a].rt_comp,
				 sorter->rs_targets[b].rt_comp);
}

/** swap positions of two specified targets for a sorter */
static void
ring_target_swap(void *array, int a, int b)
{
	struct ring_target *targets;
	struct ring_target  tmp;

	targets = ((struct ring_sorter *)array)->rs_targets;

	tmp = targets[a];
	targets[a] = targets[b];
	targets[b] = tmp;
}

/** sort domains by version */
static daos_sort_ops_t ring_target_ver_sops = {
	.so_cmp		= ring_target_ver_cmp,
	.so_swap	= ring_target_swap,
};

/** compare hashed IDs of two targets */
static int
ring_target_shuff_cmp(void *array, int a, int b)
{
	struct ring_sorter *sorter = (struct ring_sorter *)array;

	return ring_comp_shuff_cmp(sorter->rs_targets[a].rt_comp,
				   sorter->rs_targets[b].rt_comp,
				   sorter->rs_seed, 13);
}

/** sort target by hashed rank */
static daos_sort_ops_t ring_target_shuff_sops = {
	.so_cmp		= ring_target_shuff_cmp,
	.so_swap	= ring_target_swap,
};

/** compare versoins of two domains */
static int
ring_domain_ver_cmp(void *array, int a, int b)
{
	struct ring_sorter *sorter = (struct ring_sorter *)array;

	return ring_comp_ver_cmp(sorter->rs_domains[a].rd_comp,
				 sorter->rs_domains[b].rd_comp);
}

/** swap positions of two specified domains for a sorter */
static void
ring_domain_swap(void *array, int a, int b)
{
	struct ring_domain *domains;
	struct ring_domain  tmp;

	domains = ((struct ring_sorter *)array)->rs_domains;

	tmp = domains[a];
	domains[a] = domains[b];
	domains[b] = tmp;
}

/** sort domains by version */
static daos_sort_ops_t ring_domain_ver_sops = {
	.so_cmp		= ring_domain_ver_cmp,
	.so_swap	= ring_domain_swap,
};

/** compare hashed rank of domains */
static int
ring_domain_shuff_cmp(void *array, int a, int b)
{
	struct ring_sorter *sorter = (struct ring_sorter *)array;

	return ring_comp_shuff_cmp(sorter->rs_domains[a].rd_comp,
				   sorter->rs_domains[b].rd_comp,
				   sorter->rs_seed, 23);
}

/** Sort domains by hashed ranks */
static daos_sort_ops_t ring_domain_shuff_sops = {
	.so_cmp		= ring_domain_shuff_cmp,
	.so_swap	= ring_domain_swap,
};

/**
 * allocate scratch buffer for shuffling domains/targets
 */
static int
ring_buf_create(struct pl_ring_map *rimap, struct ring_buf **buf_pp)
{
	struct ring_domain *rdom;
	struct pool_domain *doms;
	struct ring_buf	   *buf;
	unsigned int	    dom_nr;
	unsigned int	    ver;
	int		    i;
	int		    j;
	int		    rc;

	rc = pool_map_find_domain(rimap->rmp_map.pl_poolmap, rimap->rmp_domain,
				  PO_COMP_ID_ALL, &doms);
	if (rc <= 0)
		return rc == 0 ? -DER_INVAL : rc;

	dom_nr = rc;
	D_ALLOC_PTR(buf);
	if (buf == NULL)
		return -DER_NOMEM;

	ver = pl_map_version(&rimap->rmp_map);
	/** count domains that match the version */
	for (i = 0; i < dom_nr; i++) {
		if (doms[i].do_comp.co_ver <= ver)
			buf->rb_domain_nr++;
	}

	D_ALLOC_ARRAY(buf->rb_domains, buf->rb_domain_nr);
	if (buf->rb_domains == NULL) {
		rc = -DER_NOMEM;
		goto err_out;
	}

	rdom = &buf->rb_domains[0];
	for (i = 0; i < dom_nr; i++) {
		if (doms[i].do_comp.co_ver > ver)
			continue;

		if (doms[i].do_target_nr == 0)
			continue;

		rdom->rd_comp = &doms[i].do_comp;

		rdom->rd_target_nr = doms[i].do_target_nr;
		D_ALLOC_ARRAY(rdom->rd_targets, rdom->rd_target_nr);
		if (rdom->rd_targets == NULL) {
			rc = -DER_NOMEM;
			goto err_out;
		}

		for (j = 0; j < rdom->rd_target_nr; j++)
			rdom->rd_targets[j].rt_comp =
					&doms[i].do_targets[j].ta_comp;

		D_DEBUG(DB_PL, "Found %d targets for %s[%d]\n",
			rdom->rd_target_nr, pool_domain_name(&doms[i]),
			doms[i].do_comp.co_id);

		buf->rb_target_nr += rdom->rd_target_nr;
		rdom++;
	}
	*buf_pp = buf;
	return 0;
 err_out:
	ring_buf_destroy(buf);
	return rc;
}

/** free scratch buffer */
static void
ring_buf_destroy(struct ring_buf *buf)
{
	int	i;

	if (buf->rb_domains != NULL) {
		for (i = 0; i < buf->rb_domain_nr; i++) {
			struct ring_domain *rdom = &buf->rb_domains[i];

			if (rdom->rd_targets != NULL) {
				D_FREE(rdom->rd_targets);
			}
		}
		D_FREE(buf->rb_domains);
	}
	D_FREE(buf);
}

/**
 * Sort targets by by version, then pseudo-randomly shuffle targets in each
 * version. It can guarantee to generate the same pseudo-random order for
 * all versions.
 */
static void
ring_domain_shuffle(struct ring_domain *rdom, unsigned int seed)
{
	struct ring_target *rtargets = rdom->rd_targets;
	struct ring_sorter  sorter;
	int		    start;
	int		    ver;
	int		    nr;
	int		    i;

	D_DEBUG(DB_PL, "Sort %d targets of %s[%d] by version\n",
		rdom->rd_target_nr, pool_comp_name(rdom->rd_comp),
		rdom->rd_comp->co_id);

	sorter.rs_targets = rtargets;
	daos_array_sort(&sorter, rdom->rd_target_nr, false,
			&ring_target_ver_sops);

	for (i = start = 0, ver = rdom->rd_comp->co_ver;
	     i < rdom->rd_target_nr; i++) {

		if (ver == rtargets[i].rt_comp->co_ver &&
		    i < rdom->rd_target_nr - 1)
			continue;

		/* find a different version, or it is the last target */
		nr = i - start + (ver == rtargets[i].rt_comp->co_ver);
		sorter.rs_targets = &rtargets[start];
		sorter.rs_seed    = seed;

		daos_array_sort(&sorter, nr, true, &ring_target_shuff_sops);
		if (ver != rtargets[i].rt_comp->co_ver) {
			ver = rtargets[i].rt_comp->co_ver;
			start = i;
		}
	}
}

/**
 * shuffle an array of domains and targets in each domain
 */
static int
ring_buf_shuffle(struct pl_ring_map *rimap, unsigned int seed,
		 struct ring_buf *buf)
{
	struct ring_domain *scratch = NULL;
	struct ring_domain *merged;
	struct ring_sorter  sorter;
	int		    start;
	int		    ver;
	int		    i;
	int		    j;
	int		    k;

	D_ALLOC_ARRAY(scratch, buf->rb_domain_nr);
	if (scratch == NULL)
		return -DER_NOMEM;

	sorter.rs_domains = buf->rb_domains;
	D_DEBUG(DB_PL, "Sort domains by version\n");
	daos_array_sort(&sorter, buf->rb_domain_nr, false,
			&ring_domain_ver_sops);

	ver = buf->rb_domains[0].rd_comp->co_ver;
	merged = &scratch[buf->rb_domain_nr];

	for (i = start = 0; i < buf->rb_domain_nr; i++) {
		struct pool_component	 *comp;
		struct ring_domain	 *dst;
		struct ring_domain	 *dst2;
		int			  nr;

		comp = buf->rb_domains[i].rd_comp;

		ring_domain_shuffle(&buf->rb_domains[i], seed);
		if (ver == comp->co_ver && i < buf->rb_domain_nr - 1)
			continue;

		nr = i - start + (ver == comp->co_ver);
		sorter.rs_seed  = seed;
		sorter.rs_domains = &buf->rb_domains[start];
		daos_array_sort(&sorter, nr, true, &ring_domain_shuff_sops);

		dst = dst2 = merged - nr;
		for (j = k = 0; &scratch[buf->rb_domain_nr] - merged > 0 ||
				nr > 0; k++) {
			if (k % 2 == 0) {
				if (&scratch[buf->rb_domain_nr] - merged > 0) {
					*dst = *merged;
					dst++;
					merged++;
				}
			} else {
				if (nr > 0) {
					*dst = buf->rb_domains[start + j];
					dst++;
					nr--;
					j++;
				}
			}
		}
		merged = dst2;

		if (ver != comp->co_ver) {
			ver = comp->co_ver;
			start = i;
		}
	}

	D_DEBUG(DB_PL, "Copy scratch buffer\n");
	memcpy(buf->rb_domains, scratch, buf->rb_domain_nr * sizeof(*scratch));
	D_FREE(scratch);
	return 0;
}

/**
 * build a ring with pseudo-randomly ordered domains and targets
 */
static int
ring_create(struct pl_ring_map *rimap, unsigned int index,
	    struct ring_buf *buf)
{
	struct pl_ring	   *ring = &rimap->rmp_rings[index];
	struct pl_target   *plt;
	struct pool_target *first;
	int		    i;
	int		    j;
	int		    rc;

	D_DEBUG(DB_PL, "Create ring %d [%d targets] for rimap\n",
		index, rimap->rmp_target_nr);

	rc = ring_buf_shuffle(rimap, index + 1, buf);
	if (rc < 0)
		return rc;

	D_ALLOC_ARRAY(ring->ri_targets, rimap->rmp_target_nr);
	if (ring->ri_targets == NULL)
		return -DER_NOMEM;

	first = pool_map_targets(rimap->rmp_map.pl_poolmap);
	for (plt = &ring->ri_targets[0], i = 0;
	     plt < &ring->ri_targets[rimap->rmp_target_nr]; i++) {
		for (j = 0; j < buf->rb_domain_nr; j++) {
			struct ring_domain *rdom = &buf->rb_domains[j];
			struct pool_target *target;

			if (i >= rdom->rd_target_nr)
				continue;

			target = container_of(rdom->rd_targets[i].rt_comp,
					   struct pool_target, ta_comp);
			/* position (offset) of target in the pool map */
			plt->pt_pos = target - first;
			plt++;
		}
	}
	return 0;
}

static void
ring_free(struct pl_ring_map *rimap, struct pl_ring *ring)
{
	if (ring->ri_targets != NULL) {
		D_FREE(ring->ri_targets);
	}
}

static void
ring_print(struct pl_ring_map *rimap, int index)
{
	struct pl_ring	   *ring = &rimap->rmp_rings[index];
	struct pool_target *targets;
	int		    period;
	int		    i;
	int		    j;

	D_PRINT("ring[%d]\n", index);
	targets = pool_map_targets(rimap->rmp_map.pl_poolmap);

	for (i = j = period = 0; i < rimap->rmp_target_nr; i++) {
		int pos = ring->ri_targets[i].pt_pos;

		D_PRINT("%d ", targets[pos].ta_comp.co_id);
		j++;
		period++;
		if (period == rimap->rmp_domain_nr) {
			period = 0;
			D_PRINT("\n");
		}
	}
}

/** create rings for rimap */
static int
ring_map_build(struct pl_ring_map *rimap, struct pl_map_init_attr *mia)
{
	struct ring_buf	*buf;
	int		 i;
	int		 rc;

	D_ASSERT(rimap->rmp_map.pl_poolmap != NULL);
	rimap->rmp_domain  = mia->ia_ring.domain;
	rimap->rmp_ring_nr = mia->ia_ring.ring_nr;

	D_ALLOC_ARRAY(rimap->rmp_rings, rimap->rmp_ring_nr);
	if (rimap->rmp_rings == NULL)
		return -DER_NOMEM;

	rc = ring_buf_create(rimap, &buf);
	if (rc != 0)
		return rc;

	rimap->rmp_domain_nr = buf->rb_domain_nr;
	rimap->rmp_target_nr = buf->rb_target_nr;

	for (i = 0; i < rimap->rmp_ring_nr; i++) {
		rc = ring_create(rimap, i, buf);
		if (rc != 0)
			goto out;
	}

	D_DEBUG(DB_PL, "Built %d rings for placement map\n",
		rimap->rmp_ring_nr);
 out:
	if (buf != NULL)
		ring_buf_destroy(buf);
	return rc;
}

/* each target has at least 10 bits for the key range of consistent hash */
#define TARGET_BITS		10
/* one million for domains */
#define DOMAIN_BITS		20
/* maximum bits for a ring */
#define TARGET_HASH_BITS	45
/* max to 8 million rings */
#define RING_HASH_BITS		23

/** for comparision of float/double */
#define RING_PRECISION		0.00001

/**
 * create consistent hashes for rimap
 */
static int
ring_map_hash_build(struct pl_ring_map *rimap)
{
	uint64_t	range;
	double		stride;
	double		hash;
	int		i;
	unsigned	tg_per_dom;

	D_DEBUG(DB_PL, "Build consistent hash for ring map\n");
	D_ALLOC_ARRAY(rimap->rmp_target_hashes, rimap->rmp_target_nr);
	if (rimap->rmp_target_hashes == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(rimap->rmp_ring_hashes, rimap->rmp_ring_nr);
	if (rimap->rmp_ring_hashes == NULL)
		return -DER_NOMEM;

	tg_per_dom = rimap->rmp_target_nr / rimap->rmp_domain_nr;
	rimap->rmp_target_hbits = DOMAIN_BITS + TARGET_BITS +
				  daos_power2_nbits(tg_per_dom);
	if (rimap->rmp_target_hbits > TARGET_HASH_BITS)
		rimap->rmp_target_hbits = TARGET_HASH_BITS;

	range = 1ULL << rimap->rmp_target_hbits;

	D_DEBUG(DB_PL, "domanis %d, targets %d, hash range is 0-0x"DF_X64"\n",
		rimap->rmp_domain_nr, rimap->rmp_target_nr, range);

	/* create consistent hash for targets */
	stride = (double)range / rimap->rmp_target_nr;
	rimap->rmp_stride = stride;

	hash = 0;
	for (i = 0; i < rimap->rmp_target_nr; i++) {
		rimap->rmp_target_hashes[i] = hash;
		hash += stride;
	}

	/* create consistent hash for rings */
	range = 1ULL << RING_HASH_BITS;
	stride = (double)range / rimap->rmp_ring_nr;
	hash = 0;

	for (i = 0; i < rimap->rmp_ring_nr; i++) {
		rimap->rmp_ring_hashes[i] = hash;
		hash += stride;
	}
	return 0;
}

/**
 * Create a ring placement map
 */
static int
ring_map_create(struct pool_map *poolmap, struct pl_map_init_attr *mia,
		struct pl_map **mapp)
{
	struct pl_ring_map *rimap;
	int		    rc;

	D_ASSERT(mia->ia_ring.ring_nr > 0);
	D_DEBUG(DB_PL, "Create ring map: domain %s, ring_nr: %d\n",
		pool_comp_type2str(mia->ia_ring.domain),
		mia->ia_ring.ring_nr);

	D_ALLOC_PTR(rimap);
	if (rimap == NULL)
		return -DER_NOMEM;

	pool_map_addref(poolmap);
	rimap->rmp_map.pl_poolmap = poolmap;

	rc = ring_map_build(rimap, mia);
	if (rc != 0)
		goto err_out;

	rc = ring_map_hash_build(rimap);
	if (rc != 0)
		goto err_out;

	*mapp = &rimap->rmp_map;
	return 0;
 err_out:
	ring_map_destroy(&rimap->rmp_map);
	return rc;
}

/**
 * destroy a ring map and all its rings
 */
static void
ring_map_destroy(struct pl_map *map)
{
	struct pl_ring_map *rimap = pl_map2rimap(map);
	int		    i;

	if (rimap->rmp_ring_hashes != NULL) {
		D_FREE(rimap->rmp_ring_hashes);
	}

	if (rimap->rmp_target_hashes != NULL) {
		D_FREE(rimap->rmp_target_hashes);
	}

	if (rimap->rmp_rings != NULL) {
		for (i = 0; i < rimap->rmp_ring_nr; i++)
			ring_free(rimap, &rimap->rmp_rings[i]);

		D_FREE(rimap->rmp_rings);
	}
	if (rimap->rmp_map.pl_poolmap)
		pool_map_decref(rimap->rmp_map.pl_poolmap);

	D_FREE(rimap);
}

/**
 * print all rings of a rimap, it is for debug only
 */
static void
ring_map_print(struct pl_map *map)
{
	struct pl_ring_map *rimap = pl_map2rimap(map);
	int		    i;

	D_PRINT("ring map: ver %d, nrims %d, hash 0-"DF_X64"\n",
		pl_map_version(&rimap->rmp_map), rimap->rmp_ring_nr,
		(1UL << rimap->rmp_target_hbits));

	for (i = 0; i < rimap->rmp_ring_nr; i++)
		ring_print(rimap, i);
}

/** hash object ID, find a ring by consistent hash */
static struct pl_ring *
ring_oid2ring(struct pl_ring_map *rimap, daos_obj_id_t id)
{
	uint64_t hash;

	hash = pl_hash64(id.lo, RING_HASH_BITS);
	hash = d_hash_srch_u64(rimap->rmp_ring_hashes,
				rimap->rmp_ring_nr, hash);
	return &rimap->rmp_rings[hash];
}

/** hash object ID, find a target on a ring by consistent hash */
static unsigned int
ring_obj_place_begin(struct pl_ring_map *rimap, daos_obj_id_t oid)
{
	uint64_t hash;

	/* mix bits */
	hash  = oid.lo;
	hash ^= hash << 39;
	hash += hash << 9;
	hash -= hash << 17;

	hash  = daos_u64_hash(hash, TARGET_HASH_BITS);
	hash &= (1ULL << rimap->rmp_target_hbits) - 1;

	return d_hash_srch_u64(rimap->rmp_target_hashes,
				rimap->rmp_target_nr, hash);
}

/** calculate distance between to object shard */
static int
ring_obj_place_dist(struct pl_ring_map *rimap, daos_obj_id_t oid)
{
	/* XXX
	 * dist = shard->os_stride / rimap->rmp_stride + RING_PRECISION;
	 * D_ASSERT(dist > 0);
	 * return dist;
	 */
	return 1;
}

struct ring_obj_placement {
	unsigned int	rop_begin;
	unsigned int	rop_dist;
	unsigned int	rop_grp_size;
	unsigned int	rop_grp_nr;
	unsigned int	rop_shard_id;
};

static int
ring_obj_spec_place_begin(struct pl_ring_map *rimap, daos_obj_id_t oid,
			  unsigned int *begin)
{
	struct pool_target	*tgts;
	unsigned int		tgts_nr;
	struct pl_target	*plts;
	d_rank_t		rank;
	int			tgt;
	unsigned int		pos;
	unsigned int		i;

	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);

	/* locate rank in the pool map targets */
	tgts = pool_map_targets(rimap->rmp_map.pl_poolmap);
	tgts_nr = pool_map_target_nr(rimap->rmp_map.pl_poolmap);
	/* locate rank in the pool map targets */
	rank = daos_oclass_sr_get_rank(oid);
	tgt = daos_oclass_st_get_tgt(oid);
	for (pos = 0; pos < tgts_nr; pos++) {
		if (rank == tgts[pos].ta_comp.co_rank &&
		    (tgt == tgts[pos].ta_comp.co_index))
			break;
	}
	if (pos == tgts_nr)
		return -DER_INVAL;

	/* locate the target in the ring */
	plts = ring_oid2ring(rimap, oid)->ri_targets;
	for (i = 0; i < rimap->rmp_target_nr; i++) {
		if (plts[i].pt_pos == pos)
			break;
	}
	if (i == rimap->rmp_target_nr)
		return -DER_INVAL;


	D_DEBUG(DB_PL, "create obj with rank/tgt %d/%d pl pos %d\n",
		rank, tgt, i);
	*begin = i;

	return 0;
}

/** calculate the ring map placement for the object */
static int
ring_obj_placement_get(struct pl_ring_map *rimap, struct daos_obj_md *md,
		       struct daos_obj_shard_md *shard_md,
		       struct ring_obj_placement *rop)
{
	struct daos_oclass_attr	*oc_attr;
	daos_obj_id_t		oid;
	unsigned int		grp_dist;

	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);
	if (oc_attr == NULL) {
		D_ERROR("Can not find obj class, invlaid oid="DF_OID"\n",
			DP_OID(oid));
		return -DER_INVAL;
	}

	if (daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
	    daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
	    daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK) {
		int rc;

		rc = ring_obj_spec_place_begin(rimap, oid, &rop->rop_begin);
		if (rc) {
			D_ERROR("special oid "DF_OID" failed: rc %d\n",
				DP_OID(oid), rc);
			return rc;
		}
	} else {
		rop->rop_begin = ring_obj_place_begin(rimap, oid);
	}

	rop->rop_dist = ring_obj_place_dist(rimap, oid);

	rop->rop_grp_size = daos_oclass_grp_size(oc_attr);
	D_ASSERT(rop->rop_grp_size != 0);
	if (rop->rop_grp_size == DAOS_OBJ_REPL_MAX)
		rop->rop_grp_size = rimap->rmp_domain_nr;

	if (rop->rop_grp_size > rimap->rmp_domain_nr) {
		D_ERROR("obj="DF_OID": group size (%u) is larger than "
			"domain nr (%u)\n", DP_OID(oid),
			rop->rop_grp_size, rimap->rmp_domain_nr);
		return -DER_INVAL;
	}

	grp_dist = rop->rop_grp_size * rop->rop_dist;

	D_ASSERT(rimap->rmp_target_nr > 0);
	if (shard_md == NULL) {
		unsigned int grp_max = rimap->rmp_target_nr / rop->rop_grp_size;

		if (grp_max == 0)
			grp_max = 1;

		rop->rop_grp_nr	= daos_oclass_grp_nr(oc_attr, md);
		if (rop->rop_grp_nr > grp_max)
			rop->rop_grp_nr = grp_max;
		rop->rop_shard_id = 0;
	} else {
		rop->rop_grp_nr	= 1;
		rop->rop_shard_id = pl_obj_shard2grp_head(shard_md, oc_attr);
		rop->rop_begin += grp_dist *
			pl_obj_shard2grp_index(shard_md, oc_attr);
	}

	D_ASSERT(rop->rop_grp_nr > 0);
	D_ASSERT(rop->rop_grp_size > 0);

	D_DEBUG(DB_PL,
		"obj="DF_OID"/%u begin=%u dist=%u grp_size=%u grp_nr=%d\n",
		DP_OID(oid), rop->rop_shard_id, rop->rop_begin, rop->rop_dist,
		rop->rop_grp_size, rop->rop_grp_nr);

	return 0;
}

struct ring_failed_shard {
	d_list_t	rfs_list;
	uint32_t	rfs_shard_idx;
	uint32_t	rfs_fseq;
	uint32_t	rfs_tgt_id;
	uint8_t		rfs_status;
};

/** add one failed shard into remap list */
static void
ring_remap_add_one(d_list_t *remap_list, struct ring_failed_shard *f_new)
{
	struct ring_failed_shard *f_shard;
	d_list_t		 *tmp;

	/* All failed shards are sorted by fseq in ascending order */
	d_list_for_each_prev(tmp, remap_list) {
		f_shard = d_list_entry(tmp, struct ring_failed_shard, rfs_list);
		/*
		 * Since we can only reuild one target at a time, the
		 * target fseq should be assigned uniquely, even if all
		 * the targets of the same domain failed at same time.
		 */
		D_ASSERTF(f_new->rfs_fseq != f_shard->rfs_fseq,
			  "same fseq %u!\n", f_new->rfs_fseq);

		if (f_new->rfs_fseq < f_shard->rfs_fseq)
			continue;
		d_list_add(&f_new->rfs_list, tmp);
		return;
	}
	d_list_add(&f_new->rfs_list, remap_list);
}

/** allocate one failed shard then add it into remap list */
static int
ring_remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		     struct pool_target *tgt)
{
	struct ring_failed_shard *f_new;

	D_ALLOC_PTR(f_new);
	if (f_new == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&f_new->rfs_list);
	f_new->rfs_shard_idx = shard_idx;
	f_new->rfs_fseq = tgt->ta_comp.co_fseq;
	f_new->rfs_status = tgt->ta_comp.co_status;
	f_new->rfs_tgt_id = -1;

	ring_remap_add_one(remap_list, f_new);
	return 0;
}

/** free all elements in the remap list */
static void
ring_remap_free_all(d_list_t *remap_list)
{
	struct ring_failed_shard *f_shard, *f_tmp;

	d_list_for_each_entry_safe(f_shard, f_tmp, remap_list, rfs_list) {
		d_list_del_init(&f_shard->rfs_list);
		D_FREE(f_shard);
	}
}

/**
 * Given object placement @rop, calculate the next spare target start
 * from @spare_idx. Return false if no available spare, otherwise, return
 * true and next spare index in @spare_idx.
 */
static bool
ring_remap_next_spare(struct pl_ring_map *rimap,
		      struct ring_obj_placement *rop, unsigned int *spare_idx)
{
	unsigned int dist, max_dist, total_dist;

	D_ASSERTF(rop->rop_grp_size <= rimap->rmp_domain_nr,
		  "grp_size: %u > domain_nr: %u\n",
		  rop->rop_grp_size, rimap->rmp_domain_nr);

	/*
	 * Please be aware that we want to relocate the shard of
	 * non-replicated object (grp_size == 1) as well.
	 */
	if (rop->rop_grp_size == rimap->rmp_domain_nr &&
	    rop->rop_grp_size > 1)
		return false;

	/* Assume the ring consists of all pool targets. */
	total_dist = rimap->rmp_target_nr;

	/* The max distance from spare index to rop->rop_begin */
	max_dist = total_dist - rop->rop_grp_size * rop->rop_grp_nr;
	/* Current distance from spare index to rop->rop_begin */
	dist = (*spare_idx <= rop->rop_begin) ?
			rop->rop_begin - *spare_idx :
			rop->rop_begin + total_dist - *spare_idx;
	/*
	 * Move the spare index forward, skip the domains where
	 * the original shards located on. Revise this when the
	 * rop_dist can be other values rather than 1.
	 */
	if (!((dist + rop->rop_grp_size) % rimap->rmp_domain_nr))
		dist += rop->rop_grp_size;

	dist++;
	if (dist > max_dist)
		return false;

	/* Convert distance to spare index */
	*spare_idx = (rop->rop_begin >= dist) ?
			rop->rop_begin - dist :
			total_dist - (dist - rop->rop_begin);
	return true;
}

/** dump remap list, for debug only */
static void
ring_remap_dump(d_list_t *remap_list, struct daos_obj_md *md,
		char *comment)
{
	struct ring_failed_shard *f_shard;

	D_DEBUG(DB_PL, "remap list for "DF_OID", %s, ver %d\n",
		DP_OID(md->omd_id), comment, md->omd_ver);

	d_list_for_each_entry(f_shard, remap_list, rfs_list) {
		D_DEBUG(DB_PL, "fseq:%u, shard_idx:%u status:%u rank %d\n",
			f_shard->rfs_fseq, f_shard->rfs_shard_idx,
			f_shard->rfs_status, f_shard->rfs_tgt_id);
	}
}

#define DEBUG_DUMP_RING_MAP	0
/** dump ring map to log, for debug only. */
static void
ring_map_dump(struct pl_map *map, bool dump_rings)
{
	struct pl_ring_map *rimap = pl_map2rimap(map);
	struct pl_ring	   *ring;
	struct pool_target *targets;
	int		    index, period, i;

	if (DEBUG_DUMP_RING_MAP == 0)
		return;

	D_DEBUG(DB_PL, "ring map: ver %d, nrims %d, domain_nr %d, "
		"tgt_nr %d\n", pl_map_version(&rimap->rmp_map),
		rimap->rmp_ring_nr, rimap->rmp_domain_nr,
		rimap->rmp_target_nr);

	if (!dump_rings)
		return;

	targets = pool_map_targets(rimap->rmp_map.pl_poolmap);
	for (index = 0; index < rimap->rmp_ring_nr; index++) {
		ring = &rimap->rmp_rings[index];

		D_DEBUG(DB_PL, "ring[%d]\n", index);
		for (i = period = 0; i < rimap->rmp_target_nr; i++) {
			int pos = ring->ri_targets[i].pt_pos;

			D_DEBUG(DB_PL, "id:%d fseq:%d status:%d rank %d",
				targets[pos].ta_comp.co_id,
				targets[pos].ta_comp.co_fseq,
				targets[pos].ta_comp.co_status,
				targets[pos].ta_comp.co_rank);
			period++;
			if (period == rimap->rmp_domain_nr) {
				period = 0;
				D_DEBUG(DB_PL, "\n");
			}
		}
	}
}


/**
 * Try to remap all the failed shards in the @remap_list to proper
 * targets respectively. The new target id will be updated in the
 * @layout if the remap succeed, otherwise, corresponding shard id
 * and target id in @layout will be cleared as -1.
 */
static void
ring_obj_remap_shards(struct pl_ring_map *rimap, struct daos_obj_md *md,
		      struct pl_obj_layout *layout,
		      struct ring_obj_placement *rop, d_list_t *remap_list)
{
	struct ring_failed_shard *f_shard, *f_tmp;
	struct pool_map		 *map = rimap->rmp_map.pl_poolmap;
	struct pl_target	 *plts;
	struct pl_obj_shard	 *l_shard;
	struct pool_target	 *tgts;
	struct pool_target	 *spare_tgt;
	d_list_t		 *current;
	unsigned int		  spare_idx;
	bool			  spare_avail = true;

	ring_remap_dump(remap_list, md, "before remap:");

	plts = ring_oid2ring(rimap, md->omd_id)->ri_targets;
	tgts = pool_map_targets(map);
	current = remap_list->next;
	spare_idx = rop->rop_begin;

	while (current != remap_list) {
		f_shard = d_list_entry(current, struct ring_failed_shard,
				       rfs_list);
		l_shard = &layout->ol_shards[f_shard->rfs_shard_idx];

		spare_avail = ring_remap_next_spare(rimap, rop, &spare_idx);
		D_DEBUG(DB_PL, "obj:"DF_OID", select spare:%d grp_size:%u, "
			"grp_nr:%u, begin:%u, spare:%u spare id %d\n",
			DP_OID(md->omd_id), spare_avail, rop->rop_grp_size,
			rop->rop_grp_nr, rop->rop_begin, spare_idx,
			spare_avail ?
			tgts[plts[spare_idx].pt_pos].ta_comp.co_id : -1);
		if (!spare_avail) {
			ring_map_dump(&rimap->rmp_map, true);
			goto next_fail;
		}

		spare_tgt = &tgts[plts[spare_idx].pt_pos];

		/* The selected spare target is down as well */
		if (pool_target_unavail(spare_tgt)) {
			D_ASSERTF(spare_tgt->ta_comp.co_fseq !=
				  f_shard->rfs_fseq, "same fseq %u!\n",
				  f_shard->rfs_fseq);

			/* If the spare target fseq > the current object pool
			 * version, the current failure shard will be handled
			 * by the following rebuild.
			 */
			if (spare_tgt->ta_comp.co_fseq > md->omd_ver) {
				D_DEBUG(DB_PL, DF_OID", fseq %d rank %d"
					" ver %d\n", DP_OID(md->omd_id),
					spare_tgt->ta_comp.co_fseq,
					spare_tgt->ta_comp.co_rank,
					md->omd_ver);
				spare_avail = false;
				goto next_fail;
			}

			/*
			 * The selected spare is down prior to current failed
			 * one, then it can't be a valid spare, let's skip it
			 * and try next spare on the ring.
			 */
			if (spare_tgt->ta_comp.co_fseq < f_shard->rfs_fseq)
				continue; /* try next spare */

			/*
			 * If both failed target and spare target are down, then
			 * add the spare target to the fail list for remap, and
			 * try next spare on the ring.
			 */
			if (f_shard->rfs_status == PO_COMP_ST_DOWN)
				D_ASSERTF(spare_tgt->ta_comp.co_status !=
					  PO_COMP_ST_DOWNOUT,
					  "down fseq(%u) < downout fseq(%u)\n",
					  f_shard->rfs_fseq,
					  spare_tgt->ta_comp.co_fseq);

			f_shard->rfs_fseq = spare_tgt->ta_comp.co_fseq;
			f_shard->rfs_status = spare_tgt->ta_comp.co_status;

			current = current->next;
			d_list_del_init(&f_shard->rfs_list);
			ring_remap_add_one(remap_list, f_shard);

			/* Continue with the failed shard has minimal fseq */
			if (current == remap_list) {
				current = &f_shard->rfs_list;
			} else {
				f_tmp = d_list_entry(current,
						     struct ring_failed_shard,
						     rfs_list);
				if (f_shard->rfs_fseq < f_tmp->rfs_fseq)
					current = &f_shard->rfs_list;
			}
			continue; /* try next spare */
		}
next_fail:
		if (spare_avail) {
			/* The selected spare target is up and ready */
			l_shard->po_target = spare_tgt->ta_comp.co_id;

			/* XXX: Use pl_obj_shard::po_fseq to record the latest
			 *	failure sequence of the targets on the remap
			 *	chain for the given shard (@l_shard).
			 *
			 *	The f_shard->rfs_fseq is the snapshot of the
			 *	pool map version (that is incremental only)
			 *	when related spare (or the original target)
			 *	became down.
			 *
			 *	Currently, DAOS does not support the target
			 *	re-integration. So the failure sequence for
			 *	available spares will be the initial value
			 *	(the oldest one). So here, we only need to
			 *	consider those unavailable spares's failure
			 *	sequences to find out the latest (largest).
			 */
			l_shard->po_fseq = f_shard->rfs_fseq;

			/*
			 * Mark the shard as 'rebuilding' so that read will
			 * skip this shard.
			 */
			if (f_shard->rfs_status == PO_COMP_ST_DOWN) {
				l_shard->po_rebuilding = 1;
				f_shard->rfs_tgt_id = spare_tgt->ta_comp.co_id;
			}
		} else {
			l_shard->po_shard = -1;
			l_shard->po_target = -1;
		}
		current = current->next;
	}

	ring_remap_dump(remap_list, md, "after remap:");
}

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

static int
ring_obj_layout_fill(struct pl_map *map, struct daos_obj_md *md,
		     struct ring_obj_placement *rop,
		     struct pl_obj_layout *layout, d_list_t *remap_list)
{
	struct pl_ring_map	*rimap = pl_map2rimap(map);
	struct pool_target	*tgts;
	struct pl_target	*plts;
	unsigned int		 plts_nr, grp_dist, grp_start;
	unsigned int		 pos, i, j, k, rc = 0;

	layout->ol_ver = pl_map_version(map);
	layout->ol_grp_size = rop->rop_grp_size;
	layout->ol_grp_nr = rop->rop_grp_nr;

	plts = ring_oid2ring(rimap, md->omd_id)->ri_targets;
	plts_nr = rimap->rmp_target_nr;
	grp_dist = rop->rop_grp_size * rop->rop_dist;
	grp_start = rop->rop_begin;
	tgts = pool_map_targets(map->pl_poolmap);
	ring_map_dump(map, true);

	for (i = 0, k = 0; i < rop->rop_grp_nr; i++) {
		bool tgts_avail = (k + rop->rop_grp_size <= plts_nr);

		for (j = 0; j < rop->rop_grp_size; j++, k++) {
			struct pool_target *tgt;
			unsigned int	idx;

			/* No available targets for the whole group */
			if (!tgts_avail) {
				layout->ol_shards[k].po_shard = -1;
				layout->ol_shards[k].po_target = -1;
				continue;
			}

			idx = (grp_start + j * rop->rop_dist) % plts_nr;
			pos = plts[idx].pt_pos;

			tgt = &tgts[pos];
			layout->ol_shards[k].po_shard  = rop->rop_shard_id + k;
			layout->ol_shards[k].po_target = tgt->ta_comp.co_id;
			layout->ol_shards[k].po_fseq   = tgt->ta_comp.co_fseq;

			if (pool_target_unavail(tgt)) {
				rc = ring_remap_alloc_one(remap_list, k, tgt);
				if (rc)
					D_GOTO(out, rc);
			}
		}
		grp_start += grp_dist;
	}

	ring_obj_remap_shards(rimap, md, layout, rop, remap_list);

	obj_layout_dump(md->omd_id, layout);
out:
	if (rc) {
		D_ERROR("ring_obj_layout_fill failed, rc %d.\n", rc);
		ring_remap_free_all(remap_list);
	}
	return rc;
}

static int
ring_obj_place(struct pl_map *map, struct daos_obj_md *md,
	       struct daos_obj_shard_md *shard_md,
	       struct pl_obj_layout **layout_pp)
{
	struct ring_obj_placement  rop;
	struct pl_ring_map	  *rimap = pl_map2rimap(map);
	struct pl_obj_layout	  *layout;
	d_list_t		   remap_list;
	int			   rc;

	rc = ring_obj_placement_get(rimap, md, shard_md, &rop);
	if (rc) {
		D_ERROR("ring_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	rc = pl_obj_layout_alloc(rop.rop_grp_size * rop.rop_grp_nr, &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = ring_obj_layout_fill(map, md, &rop, layout, &remap_list);
	if (rc) {
		D_ERROR("ring_obj_layout_fill failed, rc %d.\n", rc);
		pl_obj_layout_free(layout);
		return rc;
	}

	*layout_pp = layout;
	ring_remap_free_all(&remap_list);
	return 0;
}

#define SHARDS_ON_STACK_COUNT	128
int
ring_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
		      struct daos_obj_shard_md *shard_md,
		      uint32_t rebuild_ver, uint32_t *tgt_id,
		      uint32_t *shard_idx, unsigned int array_size,
		      int myrank)
{
	struct ring_obj_placement  rop;
	struct pl_ring_map	  *rimap = pl_map2rimap(map);
	struct pl_obj_layout	  *layout;
	struct pl_obj_layout	   layout_on_stack;
	struct pl_obj_shard	   shards_on_stack[SHARDS_ON_STACK_COUNT];
	d_list_t		   remap_list;
	struct ring_failed_shard  *f_shard;
	struct pl_obj_shard	  *l_shard;
	unsigned int		   shards_count;
	int			   idx = 0;
	int			   rc;

	/* Caller should guarantee the pl_map is uptodate */
	if (pl_map_version(map) < rebuild_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), rebuild_ver);
		return -DER_INVAL;
	}

	rc = ring_obj_placement_get(rimap, md, shard_md, &rop);
	if (rc)
		return rc;

	if (rop.rop_grp_size == 1) {
		D_DEBUG(DB_PL, "Not replicated object "DF_OID"\n",
			DP_OID(md->omd_id));
		return 0;
	}

	shards_count = rop.rop_grp_size * rop.rop_grp_nr;
	if (shards_count > SHARDS_ON_STACK_COUNT) {
		rc = pl_obj_layout_alloc(shards_count, &layout);
		if (rc)
			return rc;
	} else {
		layout = &layout_on_stack;
		layout->ol_nr = shards_count;
		layout->ol_shards = shards_on_stack;
		memset(layout->ol_shards, 0,
		       sizeof(*layout->ol_shards) * layout->ol_nr);
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = ring_obj_layout_fill(map, md, &rop, layout, &remap_list);
	if (rc)
		goto out;

	d_list_for_each_entry(f_shard, &remap_list, rfs_list) {
		l_shard = &layout->ol_shards[f_shard->rfs_shard_idx];

		if (f_shard->rfs_fseq > rebuild_ver)
			break;

		if (f_shard->rfs_status == PO_COMP_ST_DOWN) {
			/*
			 * Target id is used for rw, but rank is used
			 * for rebuild, perhaps they should be unified.
			 */
			if (l_shard->po_shard != -1) {
				struct pool_target	*target;
				int			 leader;

				D_ASSERT(f_shard->rfs_tgt_id != -1);
				D_ASSERT(idx < array_size);

				/* If the caller does not care about DTX related
				 * things (myrank == -1), then fill it directly.
				 */
				if (myrank == -1)
					goto fill;

				leader = pl_select_leader(md->omd_id,
					l_shard->po_shard, layout->ol_grp_size,
					true, pl_obj_get_shard, layout);
				if (leader < 0) {
					D_WARN("Not sure whether current shard "
					       "is leader or not for obj "DF_OID
					       ", fseq:%d, status:%d, ver:%d, "
					       "shard:%d, rc = %d\n",
					       DP_OID(md->omd_id),
					       f_shard->rfs_fseq,
					       f_shard->rfs_status, rebuild_ver,
					       l_shard->po_shard, leader);
					goto fill;
				}

				rc = pool_map_find_target(map->pl_poolmap,
							  leader, &target);
				D_ASSERT(rc == 1);

				if (myrank != target->ta_comp.co_rank) {
					/* The leader shard is not on current
					 * server, then current server cannot
					 * know whether DTXs for current shard
					 * have been re-synced or not. So skip
					 * the shard that will be handled by
					 * the leader on another server.
					 */
					D_DEBUG(DB_PL, "Current replica (%d)"
						"isn't the leader (%d) for obj "
						DF_OID", fseq:%d, status:%d, "
						"ver:%d, shard:%d, skip it\n",
						myrank, target->ta_comp.co_rank,
						DP_OID(md->omd_id),
						f_shard->rfs_fseq,
						f_shard->rfs_status,
						rebuild_ver, l_shard->po_shard);
					continue;
				}

fill:
				D_DEBUG(DB_PL, "Current replica (%d) is the "
					"leader for obj "DF_OID", fseq:%d, "
					"ver:%d, shard:%d, to be rebuilt.\n",
					myrank, DP_OID(md->omd_id),
					f_shard->rfs_fseq,
					rebuild_ver, l_shard->po_shard);
				tgt_id[idx] = f_shard->rfs_tgt_id;
				shard_idx[idx] = l_shard->po_shard;
				idx++;
			}
		} else if (f_shard->rfs_tgt_id != -1) {
			rc = -DER_ALREADY;
			D_ERROR(""DF_OID" rebuild is done for "
				"fseq:%d(status:%d)? rbd_ver:%d rc %d\n",
				DP_OID(md->omd_id), f_shard->rfs_fseq,
				f_shard->rfs_status, rebuild_ver, rc);
		}
	}

out:
	ring_remap_free_all(&remap_list);
	if (shards_count > SHARDS_ON_STACK_COUNT)
		pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

/** see \a dsr_obj_find_reint */
int
ring_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
		    struct daos_obj_shard_md *shard_md,
		    struct pl_target_grp *tgp_reint,
		    uint32_t *tgt_reint)
{
	D_ERROR("Unsupported\n");
	return -DER_NOSYS;
}

struct pl_map_ops	ring_map_ops = {
	.o_create		= ring_map_create,
	.o_destroy		= ring_map_destroy,
	.o_print		= ring_map_print,
	.o_obj_place		= ring_obj_place,
	.o_obj_find_rebuild	= ring_obj_find_rebuild,
	.o_obj_find_reint	= ring_obj_find_reint,
};
