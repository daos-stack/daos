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
 * This file is part of DSR
 *
 * src/placement/ring_map.c
 */
#define DD_SUBSYS	DD_FAC(placement)
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
	/** reference to cluster map */
	struct pool_map		*rmp_poolmap;
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

	key_a = daos_hash_mix96(seed, key_a % prime, key_a);
	key_b = daos_hash_mix96(seed, key_b % prime, key_b);

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
	int		    k;
	int		    rc;

	rc = pool_map_find_domain(rimap->rmp_poolmap, rimap->rmp_domain,
				  PO_COMP_ID_ALL, &doms);
	if (rc <= 0)
		return rc == 0 ? -DER_INVAL : rc;

	dom_nr = rc;
	D_ALLOC_PTR(buf);
	if (buf == NULL)
		return -DER_NOMEM;

	ver = rimap->rmp_map.pl_ver;
	/** count domains that match the version */
	for (i = 0; i < dom_nr; i++) {
		if (doms[i].do_comp.co_ver <= ver)
			buf->rb_domain_nr++;
	}

	D_ALLOC(buf->rb_domains, buf->rb_domain_nr * sizeof(*buf->rb_domains));
	if (buf->rb_domains == NULL) {
		rc = -DER_NOMEM;
		goto err_out;
	}

	rdom = &buf->rb_domains[0];
	for (i = 0; i < dom_nr; i++) {
		if (doms[i].do_comp.co_ver > ver)
			continue;

		rdom->rd_comp = &doms[i].do_comp;
		for (j = 0; j < doms[i].do_target_nr; j++) {
			if (doms[i].do_targets[j].ta_comp.co_ver <= ver)
				rdom->rd_target_nr++;
		}

		D_ALLOC(rdom->rd_targets,
			rdom->rd_target_nr * sizeof(*rdom->rd_targets));
		if (rdom->rd_targets == NULL) {
			rc = -DER_NOMEM;
			goto err_out;
		}

		for (j = k = 0; j < doms[i].do_target_nr; j++) {
			if (doms[i].do_targets[j].ta_comp.co_ver <= ver) {
				struct ring_target *rt;

				rt = &rdom->rd_targets[k++];
				rt->rt_comp = &doms[i].do_targets[j].ta_comp;
			}
		}

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
				D_FREE(rdom->rd_targets,
				       rdom->rd_target_nr *
				       sizeof(*rdom->rd_targets));
			}
		}
		D_FREE(buf->rb_domains,
		       buf->rb_domain_nr * sizeof(*buf->rb_domains));
	}
	D_FREE_PTR(buf);
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

	D_ALLOC(scratch, buf->rb_domain_nr * sizeof(*scratch));
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
	D_FREE(scratch, buf->rb_domain_nr * sizeof(*scratch));
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

	D_ALLOC(ring->ri_targets,
		rimap->rmp_target_nr * sizeof(struct pl_target));
	if (ring->ri_targets == NULL)
		return -DER_NOMEM;

	first = pool_map_targets(rimap->rmp_poolmap);

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
		D_FREE(ring->ri_targets,
		       rimap->rmp_target_nr * sizeof(*ring->ri_targets));
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
	targets = pool_map_targets(rimap->rmp_poolmap);

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
	struct pool_map *poolmap = rimap->rmp_poolmap;
	struct ring_buf	*buf;
	int		 i;
	int		 rc;

	D_ASSERT(rimap->rmp_poolmap != NULL);
	if (mia->ia_ver > pool_map_get_version(poolmap))
		return -DER_INVAL;

	if (mia->ia_ver == 0)
		rimap->rmp_map.pl_ver = pool_map_get_version(poolmap);
	else
		rimap->rmp_map.pl_ver = mia->ia_ver;

	rimap->rmp_domain  = mia->ia_ring.domain;
	rimap->rmp_ring_nr = mia->ia_ring.ring_nr;

	D_ALLOC(rimap->rmp_rings, rimap->rmp_ring_nr * sizeof(struct pl_ring));
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
	D_ALLOC(rimap->rmp_target_hashes,
		rimap->rmp_target_nr * sizeof(*rimap->rmp_target_hashes));
	if (rimap->rmp_target_hashes == NULL)
		return -DER_NOMEM;

	D_ALLOC(rimap->rmp_ring_hashes,
		rimap->rmp_ring_nr * sizeof(*rimap->rmp_ring_hashes));
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

	rimap->rmp_poolmap = poolmap;

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
		D_FREE(rimap->rmp_ring_hashes,
		       rimap->rmp_ring_nr *
		       sizeof(*rimap->rmp_ring_hashes));
	}

	if (rimap->rmp_target_hashes != NULL) {
		D_FREE(rimap->rmp_target_hashes,
		       rimap->rmp_target_nr *
		       sizeof(*rimap->rmp_target_hashes));
	}

	if (rimap->rmp_rings != NULL) {
		for (i = 0; i < rimap->rmp_ring_nr; i++)
			ring_free(rimap, &rimap->rmp_rings[i]);

		D_FREE(rimap->rmp_rings,
		       rimap->rmp_ring_nr * sizeof(*rimap->rmp_rings));
	}
	D_FREE_PTR(rimap);
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
		rimap->rmp_map.pl_ver, rimap->rmp_ring_nr,
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
	hash = daos_chash_srch_u64(rimap->rmp_ring_hashes,
				   rimap->rmp_ring_nr, hash);
	return &rimap->rmp_rings[hash];
}

/** hash object ID, find a target on a ring by consistent hash */
static unsigned int
ring_obj_place_begin(struct pl_ring_map *rimap, daos_obj_id_t oid)
{
	uint64_t hash = oid.lo ^ oid.mid;

	/* mix bits */
	hash  = oid.lo;
	hash ^= hash << 39;
	hash += hash << 9;
	hash -= hash << 17;
	hash ^= oid.mid;

	hash  = daos_u64_hash(hash, TARGET_HASH_BITS);
	hash &= (1ULL << rimap->rmp_target_hbits) - 1;

	return daos_chash_srch_u64(rimap->rmp_target_hashes,
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

/** calculate the ring map for the object */
static int
ring_obj_placement_get(struct pl_ring_map *rimap, struct daos_obj_md *md,
		       struct daos_obj_shard_md *shard_md, unsigned int *begin,
		       unsigned int *dist, unsigned *shard,
		       unsigned int *grp_nr, unsigned int *grp_size)
{
	struct daos_oclass_attr	*oc_attr;
	daos_obj_id_t		oid;
	unsigned int		grp_dist;

	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);
	D_ASSERT(oc_attr != NULL);

	*begin	 = ring_obj_place_begin(rimap, oid);
	*dist	 = ring_obj_place_dist(rimap, oid);

	*grp_size = daos_oclass_grp_size(oc_attr);
	D_ASSERT(*grp_size != 0);
	if (*grp_size == DAOS_OBJ_REPL_MAX)
		*grp_size = rimap->rmp_target_nr;

	grp_dist = *grp_size * *dist;

	if (shard_md == NULL) {
		unsigned int grp_max = rimap->rmp_target_nr / *grp_size;

		if (grp_max == 0)
			grp_max = 1;

		*grp_nr	= daos_oclass_grp_nr(oc_attr, md);
		if (*grp_nr > grp_max)
			*grp_nr = grp_max;
		*shard	= 0;
	} else {
		*grp_nr	= 1;
		*shard	= pl_obj_shard2grp_head(shard_md, oc_attr);
		*begin	+= grp_dist * pl_obj_shard2grp_index(shard_md, oc_attr);
	}

	D_ASSERT(*grp_nr > 0);
	D_ASSERT(*grp_size > 0);

	D_DEBUG(DB_PL,
		"obj="DF_OID"/%u begin=%u dist=%u grp_size=%u grp_nr=%d\n",
		DP_OID(oid), *shard, *begin, *dist, *grp_size, *grp_nr);

	return 0;
}

static bool
target_in_layout(unsigned int tgt_id, struct pl_obj_layout *layout,
		 unsigned int *idx)
{
	bool	found = false;
	int	i;

	for (i = 0; i < layout->ol_nr; i++) {
		if (layout->ol_shards[i].po_target == tgt_id) {
			found = true;
			*idx = i;
			break;
		}
	}
	return found;
}

static int
ring_obj_get_rebuild_idx(unsigned int current, unsigned int ring_num,
			 unsigned int dist)
{
	unsigned int next;

	if (current >= dist)
		next = current - dist;
	else
		next = ring_num + current - dist;

	return next;
}

static void
ring_layout_print(struct pl_target *plts, struct pool_target *tgs,
		  int nr, unsigned int mask)
{
	int i;

	for (i = 0; i < nr; i++) {
		unsigned int pos = plts[i].pt_pos;

		D_DEBUG(mask, "k %d tgt_off %d rank %d id %d status %d\n", i,
			pos, tgs[pos].ta_comp.co_rank, tgs[pos].ta_comp.co_id,
			tgs[pos].ta_comp.co_status);
	}
}

static int
ring_obj_layout_fill(struct pl_map *map, struct daos_obj_md *md,
		     unsigned int ver, unsigned int begin, unsigned int dist,
		     unsigned int shard, unsigned int grp_nr,
		     unsigned int grp_size, struct pl_obj_layout *layout,
		     unsigned int *min_idx)
{
	struct pl_ring_map	*rimap = pl_map2rimap(map);
	struct pl_target	*plts;
	struct pool_target	*tgs;
	struct pool_target	*failed_tgts;
	unsigned int		failed_tgts_num = 0;
	unsigned int		plts_nr;
	unsigned int		grp_dist;
	unsigned int		grp_start;
	unsigned int		i;
	unsigned int		j;
	unsigned int		k;
	unsigned int		end;
	unsigned int		start;
	unsigned int		pos;
	int			rc;

	layout->ol_ver = map->pl_ver;
	plts = ring_oid2ring(rimap, md->omd_id)->ri_targets;
	plts_nr = rimap->rmp_target_nr;
	grp_dist = grp_size * dist;
	grp_start = begin;
	tgs = pool_map_targets(rimap->rmp_poolmap);
	ring_layout_print(plts, tgs, plts_nr, DB_TRACE);
	for (i = 0, k = 0; i < grp_nr; i++) {
		for (j = 0; j < grp_size; j++, k++) {
			unsigned int	idx;

			if (k >= plts_nr) {
				/* If group size is larger than the target
				 * number, let's disable the further shards
				 * of the obj on this group.
				 *
				 * NB: pl_obj_layout_alloc can guarantee this
				 * is always the last group.
				 */
				layout->ol_shards[k].po_shard = -1;
				layout->ol_shards[k].po_target = -1;
				continue;
			}

			idx = (grp_start + j * dist) % plts_nr;

			pos = plts[idx].pt_pos;
			layout->ol_shards[k].po_shard  = shard + k;
			layout->ol_shards[k].po_target = tgs[pos].ta_comp.co_id;
			D_DEBUG(DB_PL, "set layout %d rank/plt_idx/tg_pos"
				" %d/%d/%d\n", k, tgs[pos].ta_comp.co_rank,
				idx, pos);
		}
		grp_start += grp_dist;
	}

	rc = pool_map_failed_tgts_get(rimap->rmp_poolmap, ver, &failed_tgts,
				      &failed_tgts_num);
	if (rc != 0) {
		D_ERROR("failed to create failed tgt list rc %d\n", rc);
		return rc;
	}

	start = begin % plts_nr;
	end = (begin + (grp_size * grp_nr * dist - 1)) % plts_nr;
	/* Walk through the failed targets */
	for (i = 0; i < failed_tgts_num; i++) {
		unsigned int tgt_id = failed_tgts[i].ta_comp.co_id;
		unsigned int idx;

		/* Check if any targets failed, then try to replace the
		 * target with rebuild target.
		 */
		if (!target_in_layout(tgt_id, layout, &idx))
			continue;

		if (failed_tgts[i].ta_comp.co_status == PO_COMP_ST_DOWN &&
		    !layout->ol_shards[idx].po_rebuilding)
			layout->ol_shards[idx].po_rebuilding = 1;

		/* try to find next rebuild target */
		while (start != end) {

			start = ring_obj_get_rebuild_idx(start, plts_nr, dist);
			pos = plts[start].pt_pos;
			if (tgs[pos].ta_comp.co_status == PO_COMP_ST_UP ||
			    tgs[pos].ta_comp.co_fseq >
					failed_tgts[i].ta_comp.co_fseq) {
				layout->ol_shards[idx].po_target =
							tgs[pos].ta_comp.co_id;
				break;
			}

			if (tgs[pos].ta_comp.co_status == PO_COMP_ST_DOWN) {
				if (layout->ol_shards[idx].po_rebuilding) {
					/* If both current and previous target
					 * are failed and not finish rebuilding,
					 * then let's stop for this shard
					 * recovery, and rely on the next
					 * rebuild.
					 */
					layout->ol_shards[idx].po_shard = -1;
					break;
				}
				layout->ol_shards[idx].po_rebuilding = 1;
				continue;
			}
		}

		if (start == end) {
			/* Sigh no spare targets */
			layout->ol_shards[idx].po_shard = -1;
			layout->ol_shards[idx].po_target = -1;
		}
	}

	if (min_idx != NULL)
		*min_idx = start;

	for (i = 0; i < layout->ol_nr; i++)
		D_DEBUG(DB_TRACE, "layout %d tgtid %d shard %d rebuilding %d\n",
			i, layout->ol_shards[i].po_target,
			layout->ol_shards[i].po_shard,
			layout->ol_shards[i].po_rebuilding);

	return 0;
}

/** see \a dsr_obj_place */
static int
ring_obj_place(struct pl_map *map, struct daos_obj_md *md,
	       struct daos_obj_shard_md *shard_md,
	       struct pl_obj_layout **layout_pp)
{
	struct pl_ring_map	*rimap = pl_map2rimap(map);
	struct pl_obj_layout	*layout;
	unsigned int		 grp_size;
	unsigned int		 grp_nr;
	unsigned int		 shard_nr;
	unsigned int		 shard;
	unsigned int		 begin;
	unsigned int		 dist;
	int			 rc;

	rc = ring_obj_placement_get(rimap, md, shard_md, &begin, &dist, &shard,
				    &grp_nr, &grp_size);
	if (rc)
		return rc;

	D_ASSERT(rimap->rmp_target_nr > 0);

	shard_nr = grp_size * grp_nr;
	rc = pl_obj_layout_alloc(shard_nr, &layout);
	if (rc)
		return rc;

	rc = ring_obj_layout_fill(map, md,
			pool_map_get_version(rimap->rmp_poolmap), begin, dist,
			shard, grp_nr, grp_size, layout, NULL);
	if (rc) {
		pl_obj_layout_free(layout);
		return rc;
	}

	*layout_pp = layout;
	return 0;
}

#define LOCAL_LAYOUT_NR	6
int
ring_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
		      struct daos_obj_shard_md *shard_md,
		      struct pl_target_grp *tgp_failed,
		      uint32_t *rebuild_rank, uint32_t *failed_shard)
{
	struct pl_ring_map	*rimap = pl_map2rimap(map);
	struct pl_obj_shard	tmp_shards[LOCAL_LAYOUT_NR];
	struct pl_obj_layout	layout;
	struct pool_target	*tgs;
	struct pl_target	*plts;
	unsigned int		plts_nr;
	unsigned int		grp_size;
	unsigned int		grp_nr;
	unsigned int		shard;
	unsigned int		begin;
	unsigned int		dist;
	bool			impacted = false;
	unsigned int		layout_end;
	unsigned int		layout_start;
	unsigned int		i;
	unsigned int		failed_version;
	int			rc;

	/* If the object version > pool map version, then the object
	 * might already be rebuilt. */
	if (md->omd_ver > map->pl_ver) {
		D_DEBUG(DB_PL, "md version %u pl ver %u\n",
			md->omd_ver, map->pl_ver);
		return 0;
	}

	rc = ring_obj_placement_get(rimap, md, shard_md, &begin, &dist,
				    &shard, &grp_nr, &grp_size);
	if (rc)
		return rc;

	if (grp_size <= 1) {
		D_DEBUG(DB_PL, "No replicate shard for "DF_OID"\n",
			DP_OID(md->omd_id));
		return 0;
	}

	tgs   = pool_map_targets(rimap->rmp_poolmap);

	plts = ring_oid2ring(rimap, md->omd_id)->ri_targets;
	plts_nr = rimap->rmp_target_nr;

	memset(&layout, 0, sizeof(layout));
	if (grp_nr * grp_size > LOCAL_LAYOUT_NR) {
		layout.ol_nr = grp_nr * grp_size;
		D_ALLOC(layout.ol_shards,
			layout.ol_nr * sizeof(*layout.ol_shards));

		if (layout.ol_shards == NULL)
			return -DER_NOMEM;
	} else {
		memset(tmp_shards, 0,
		       LOCAL_LAYOUT_NR * sizeof(*tmp_shards));
		layout.ol_shards = tmp_shards;
		layout.ol_nr = grp_nr * grp_size;
	}

	layout_end = (begin + (grp_size * grp_nr * dist - 1)) % plts_nr;
	failed_version = tgp_failed->tg_ver > 0 ? tgp_failed->tg_ver - 1 : 0;
	rc = ring_obj_layout_fill(map, md, failed_version,
				  begin, dist, shard, grp_nr, grp_size,
				  &layout, &layout_start);
	if (rc)
		D_GOTO(free, rc);

	/* Only support single target failure now */
	D_ASSERT(tgp_failed->tg_target_nr == 1);
	for (i = 0; i < tgp_failed->tg_target_nr; i++) {
		unsigned int	pos = tgp_failed->tg_targets[i].pt_pos;
		bool		rebuilding = false;
		unsigned int	tgt_id;
		unsigned int	layout_idx;
		unsigned int	tgt_idx;

		tgt_id = tgs[pos].ta_comp.co_id;
		if (!target_in_layout(tgt_id, &layout, &layout_idx))
			continue;

		D_DEBUG(DB_PL, "failed tgt ver/id/rank %d/%d/%d on layout %d\n",
			tgp_failed->tg_ver, tgt_id, tgs[pos].ta_comp.co_rank,
			layout_idx);
		tgt_idx = ring_obj_get_rebuild_idx(layout_start, plts_nr, dist);
		if (tgt_idx == layout_end)
			D_GOTO(free, rc);

		while (tgt_idx != layout_end) {
			pos = plts[tgt_idx].pt_pos;
			if (tgs[pos].ta_comp.co_status == PO_COMP_ST_UP ||
			    tgs[pos].ta_comp.co_status == PO_COMP_ST_UPIN) {
				impacted = true;
				*rebuild_rank = tgs[pos].ta_comp.co_rank;
				*failed_shard = layout_idx;
				D_DEBUG(DB_TRACE, "failed pos %d id %d rank %d"
					" rebuild %d\n", pos, tgt_id,
					*rebuild_rank, *failed_shard);
				break;
			} else if (tgs[pos].ta_comp.co_status ==
							PO_COMP_ST_DOWN) {
				if (rebuilding) {
					D_DEBUG(DB_TRACE, "pos %d idx %d\n",
						pos, layout_idx);
					break;
				}
				rebuilding = true;
			}
			tgt_idx = ring_obj_get_rebuild_idx(tgt_idx, plts_nr,
							   dist);
		}
	}
free:
	if (layout.ol_shards != tmp_shards)
		D_FREE(layout.ol_shards,
		       layout.ol_nr * sizeof(*layout.ol_shards));

	if (rc == 0 && impacted)
		rc = 1;
	return rc;
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
