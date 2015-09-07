/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/placement/rim_map.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include "pl_map_internal.h"

extern daos_sort_ops_t cl_target_vsort_ops;

/** scratch buffer for reshuffling domains and targets */
typedef struct {
	/** total number of domains in this buffer */
	unsigned int		  rb_ndoms;
	/** total number of targets in this buffer */
	unsigned int		  rb_ntargets;
	struct rim_domain {
		/** number of target within this domain */
		unsigned int	  rd_ntargets;
		/** pointers to targets within this domain */
		cl_target_t	**rd_targets;
		/** pointer to cluster domain */
		cl_domain_t	 *rd_dom;
	}			 *rb_doms;
} rim_buf_t;

/** helper structure for shuffling domains */
typedef struct rim_dom_shuffler {
	struct rim_domain	*rs_rdoms;
	uint64_t		 rs_seed;
} rim_dom_shuffler_t;

/** helper structure for shuffling targets */
typedef struct rim_target_shuffler {
	cl_target_t		**ts_targets;
	uint64_t		  ts_seed;
} rim_target_shuffler_t;

static void rim_buf_destroy(rim_buf_t *buf);
static void rim_map_destroy(pl_map_t *map);

static inline pl_rim_map_t *
pl_map2rimap(pl_map_t *map)
{
	return container_of(map, pl_rim_map_t, rmp_map);
}

/** compare versoin of two domains */
static int
rim_dom_cmp_ver(void *array, int a, int b)
{
	struct rim_domain *rda = array;

	if (rda[a].rd_dom->cd_comp.co_ver > rda[b].rd_dom->cd_comp.co_ver)
		return 1;
	if (rda[a].rd_dom->cd_comp.co_ver < rda[b].rd_dom->cd_comp.co_ver)
		return -1;
	return 0;
}

static void
rim_dom_swap(void *array, int a, int b)
{
	struct rim_domain *rda = array;
	struct rim_domain  tmp = rda[a];

	rda[a] = rda[b];
	rda[b] = tmp;
}

/** sort domains by version */
static daos_sort_ops_t rim_dom_vsort_ops = {
	.so_cmp		= rim_dom_cmp_ver,
	.so_swap	= rim_dom_swap,
};

/**
 * allocate scratch buffer for shuffling domains/targets
 */
static int
rim_buf_create(pl_rim_map_t *rimap, rim_buf_t **buf_p)
{
	struct rim_domain *rdom;
	cl_domain_t	  *doms;
	rim_buf_t	  *buf;
	unsigned	   ver;
	unsigned	   ndoms;
	int		   i;
	int		   j;
	int		   k;
	int		   rc;

	ndoms = cl_map_find_buf(rimap->rmp_clmap, rimap->rmp_domain, &doms);
	if (ndoms <= 0)
		return -EINVAL;

	buf = calloc(1, sizeof(*buf));
	if (buf == NULL)
		return -ENOMEM;

	ver = rimap->rmp_map.pm_ver;
	for (i = 0; i < ndoms; i++) {
		if (doms[i].cd_comp.co_ver <= ver)
			buf->rb_ndoms++;
	}

	buf->rb_doms = calloc(buf->rb_ndoms, sizeof(*buf->rb_doms));
	if (buf->rb_doms == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	rdom = &buf->rb_doms[0];
	for (i = 0; i < ndoms; i++) {
		if (doms[i].cd_comp.co_ver > ver)
			continue;

		rdom->rd_dom = &doms[i];
		for (j = 0; j < doms[i].cd_ntargets; j++) {
			if (doms[i].cd_targets[j].co_ver <= ver)
				rdom->rd_ntargets++;
		}

		rdom->rd_targets = calloc(rdom->rd_ntargets,
					  sizeof(*rdom->rd_targets));
		if (rdom->rd_targets == NULL) {
			rc = -ENOMEM;
			goto err_out;
		}

		for (j = k = 0; j < doms[i].cd_ntargets; j++) {
			if (doms[i].cd_targets[j].co_ver <= ver)
				rdom->rd_targets[k++] = &doms[i].cd_targets[j];
		}

		D_DEBUG(DF_PL, "Found %d targets for %s[%d]\n",
			rdom->rd_ntargets, cl_domain_name(&doms[i]),
			doms[i].cd_comp.co_rank);

		buf->rb_ntargets += rdom->rd_ntargets;
		rdom++;
	}
	*buf_p = buf;
	return 0;
 err_out:
	rim_buf_destroy(buf);
	return rc;
}

/** free scratch buffer */
static void
rim_buf_destroy(rim_buf_t *buf)
{
	int	i;

	if (buf->rb_doms != NULL) {
		for (i = 0; i < buf->rb_ndoms; i++) {
			if (buf->rb_doms[i].rd_targets != NULL)
				free(buf->rb_doms[i].rd_targets);
		}
		free(buf->rb_doms);
	}
	free(buf);
}

/** compare hashed rank of targets */
static int
rim_target_shuffler_cmp(void *array, int a, int b)
{
	rim_target_shuffler_t	 *ts = array;
	cl_target_t		**targets = ts->ts_targets;
	uint64_t		  buf[2];
	uint64_t		  ka;
	uint64_t		  kb;

	buf[0]  = targets[a]->co_rank;
	buf[1]  = ts->ts_seed;
	buf[0] ^= buf[0] << 22;
	buf[1] ^= buf[1] << 28;
	ka = daos_u64_hash(buf[1] + buf[0], 37);

	buf[0]  = targets[b]->co_rank;
	buf[1]  = ts->ts_seed;
	buf[0] ^= buf[0] << 22;
	buf[1] ^= buf[1] << 28;
	kb = daos_u64_hash(buf[1] + buf[0], 37);

	if (ka > kb)
		return 1;
	if (ka < kb)
		return -1;

	if (targets[a]->co_rank > targets[b]->co_rank)
		return 1;
	if (targets[a]->co_rank < targets[b]->co_rank)
		return 1;

	D_ASSERT(0);
	return 0;
}

static void
rim_target_shuffler_swap(void *array, int a, int b)
{
	cl_target_t **targets = ((rim_target_shuffler_t *)array)->ts_targets;
	cl_target_t  *tmp = targets[a];

	targets[a] = targets[b];
	targets[b] = tmp;
}

/** sort target by hashed rank */
static daos_sort_ops_t rim_target_shuffler_ops = {
	.so_cmp		= rim_target_shuffler_cmp,
	.so_swap	= rim_target_shuffler_swap,
};

/**
 * Sort targets by hashed rank version by version.
 * It can guarantee to generate the same pseudo-random order for all versions.
 */
static void
rim_dom_shuffle_targets(struct rim_domain *rdom, unsigned int seed,
			unsigned int ntargets)
{
	cl_target_t **targets = rdom->rd_targets;;
	int	      start;
	int	      ver;
	int	      num;
	int	      i;

	D_DEBUG(DF_PL, "Sort %d targets of %s[%d] by version\n",
		rdom->rd_ntargets, cl_domain_name(rdom->rd_dom),
		rdom->rd_dom->cd_comp.co_rank);

	daos_array_sort(targets, rdom->rd_ntargets, false,
			&cl_target_vsort_ops);

	ver = rdom->rd_dom->cd_comp.co_ver;
	for (i = start = 0; i < rdom->rd_ntargets && ntargets > 0; i++) {
		rim_target_shuffler_t ts;

		if (ver == targets[i]->co_ver &&
		    i < rdom->rd_ntargets - 1 && ntargets > 1)
			continue;

		/* find a different version, or it is the last target */
		num = i - start + (ver == targets[i]->co_ver);
		ts.ts_targets = &targets[start];
		ts.ts_seed    = seed;

		daos_array_sort(&ts, num, true, &rim_target_shuffler_ops);
		if (ver != targets[i]->co_ver) {
			ver = targets[i]->co_ver;
			start = i;
		}
	}
}

/** compare hashed rank of domains */
static int
rim_dom_shuffler_cmp(void *array, int a, int b)
{
	rim_dom_shuffler_t	*rs = array;
	struct rim_domain	*rdoms = rs->rs_rdoms;
	uint64_t		 buf[2];
	uint64_t		 ka;
	uint64_t		 kb;

	buf[0]  = rdoms[a].rd_dom->cd_comp.co_rank;
	buf[1]  = rs->rs_seed;
	buf[0] ^= buf[0] << 26;
	buf[1] ^= buf[1] << 26;
	ka = daos_u64_hash(buf[1] + buf[0], 37);

	buf[0]  = rdoms[b].rd_dom->cd_comp.co_rank;
	buf[1]  = rs->rs_seed;
	buf[0] ^= buf[0] << 26;
	buf[1] ^= buf[1] << 26;
	kb = daos_u64_hash(buf[1] + buf[0], 37);

	if (ka > kb)
		return 1;
	if (ka < kb)
		return -1;

	if (rdoms[a].rd_dom->cd_comp.co_rank >
	    rdoms[b].rd_dom->cd_comp.co_rank)
		return 1;

	if (rdoms[a].rd_dom->cd_comp.co_rank <
	    rdoms[b].rd_dom->cd_comp.co_rank)
		return 1;

	D_ASSERT(0);
	return 0;
}

static void
rim_dom_shuffler_swap(void *array, int a, int b)
{
	struct rim_domain *rdoms = ((rim_dom_shuffler_t *)array)->rs_rdoms;
	struct rim_domain  tmp = rdoms[a];

	rdoms[a] = rdoms[b];
	rdoms[b] = tmp;
}

/** Sort domains by hashed ranks */
static daos_sort_ops_t rim_dom_shuffler_ops = {
	.so_cmp		= rim_dom_shuffler_cmp,
	.so_swap	= rim_dom_shuffler_swap,
};

/**
 * shuffle an array of domains and targets in each domain
 */
static int
rim_buf_shuffle(pl_rim_map_t *rimap, unsigned int seed,
		unsigned int ntargets, rim_buf_t *buf)
{
	struct rim_domain *scratch = NULL;
	struct rim_domain *merged;
	int		   start;
	int		   ver;
	int		   i;
	int		   j;
	int		   k;

	scratch = calloc(buf->rb_ndoms, sizeof(*scratch));
	if (scratch == NULL)
		return -ENOMEM;

	D_DEBUG(DF_PL, "Sort domains by version\n");
	daos_array_sort(buf->rb_doms, buf->rb_ndoms, false, &rim_dom_vsort_ops);

	ver = buf->rb_doms[0].rd_dom->cd_comp.co_ver;
	merged = &scratch[buf->rb_ndoms];

	for (i = start = 0; i < buf->rb_ndoms; i++) {
		cl_domain_t	   *dom = buf->rb_doms[i].rd_dom;
		struct rim_domain  *dst;
		struct rim_domain  *dst2;
		int		    num;
		rim_dom_shuffler_t  rs;

		rim_dom_shuffle_targets(&buf->rb_doms[i], seed, ntargets);
		if (ver == dom->cd_comp.co_ver && i < buf->rb_ndoms - 1)
			continue;

		num = i - start + (ver == dom->cd_comp.co_ver);
		rs.rs_seed  = seed;
		rs.rs_rdoms = &buf->rb_doms[start];
		daos_array_sort(&rs, num, true, &rim_dom_shuffler_ops);

		dst = dst2 = merged - num;
		for (j = k = 0; &scratch[buf->rb_ndoms] - merged > 0 || num > 0;
		     k++) {
			if (k % 2 == 0) {
				if (&scratch[buf->rb_ndoms] - merged > 0) {
					*dst = *merged;
					dst++;
					merged++;
				}
			} else {
				if (num > 0) {
					*dst = buf->rb_doms[start + j];
					dst++;
					num--;
					j++;
				}
			}
		}
		merged = dst2;

		if (ver != dom->cd_comp.co_ver) {
			ver = dom->cd_comp.co_ver;
			start = i;
		}
	}

	D_DEBUG(DF_PL, "Copy scratch buffer\n");
	memcpy(buf->rb_doms, scratch, buf->rb_ndoms * sizeof(*scratch));
	free(scratch);
	return 0;
}

/**
 * build a rim with pseudo-randomly ordered domains and targets
 */
static int
rim_generate(pl_rim_map_t *rimap, unsigned int idx, unsigned int ntargets,
	     rim_buf_t *buf)
{
	pl_rim_t	*rim = &rimap->rmp_rims[idx];
	pl_target_t	*target;
	int		 i;
	int		 j;
	int		 rc;

	D_DEBUG(DF_PL, "Create rim %d [%d targets] for rimap\n",
		idx, rimap->rmp_ntargets);

	rc = rim_buf_shuffle(rimap, idx, ntargets, buf);
	if (rc < 0)
		return rc;

	D_ASSERT(ntargets == -1 || ntargets == rimap->rmp_ntargets);

	rim->rim_targets = calloc(rimap->rmp_ntargets, sizeof(pl_target_t));
	if (rim->rim_targets == NULL)
		return -ENOMEM;

	target = &rim->rim_targets[0];
	for (i = 0; target < &rim->rim_targets[rimap->rmp_ntargets]; i++) {
		for (j = 0; j < buf->rb_ndoms; j++) {
			if (i >= buf->rb_doms[j].rd_ntargets)
				continue;

			/* position (offset) of target in cluster map */
			target->pt_pos = buf->rb_doms[j].rd_targets[i] -
					 cl_map_targets(rimap->rmp_clmap);
			target++;
		}
	}
	return 0;
}

static void
rim_release(pl_rim_map_t *rimap, pl_rim_t *rim)
{
	if (rim->rim_targets != NULL)
		free(rim->rim_targets);
}

static void
rim_print(pl_rim_map_t *rimap, int rim_idx)
{
	pl_rim_t	*rim = &rimap->rmp_rims[rim_idx];
	cl_target_t	*targets = cl_map_targets(rimap->rmp_clmap);;
	int		 i;
	int		 j;
	int		 period;

	D_PRINT("rim[%d]\n", rim_idx);
	for (i = j = period = 0; i < rimap->rmp_ntargets; i++) {
		D_PRINT("%d ", targets[rim->rim_targets[i].pt_pos].co_rank);

		j++;
		period++;
		if (period == rimap->rmp_ndomains) {
			period = 0;
			D_PRINT("\n");
		}
	}
}

/** create rims for rimap */
static int
rim_map_build(pl_rim_map_t *rimap, unsigned int version, unsigned int ntargets,
	      cl_comp_type_t domain)
{
	rim_buf_t	*buf;
	int		 i;
	int		 rc;

	if (version > rimap->rmp_clmap->clm_ver ||
	    version < rimap->rmp_clmap->clm_ver_old)
		return -EINVAL;

	rimap->rmp_domain	= domain;
	rimap->rmp_map.pm_ver	= version;
	rimap->rmp_map.pm_type	= PL_TYPE_RIM;

	rimap->rmp_rims = calloc(rimap->rmp_nrims, sizeof(pl_rim_t));
	if (rimap->rmp_rims == NULL)
		return -ENOMEM;

	rc = rim_buf_create(rimap, &buf);
	if (rc != 0)
		return rc;

	rimap->rmp_ndomains = buf->rb_ndoms;
	rimap->rmp_ntargets = ntargets == -1 ? buf->rb_ntargets : ntargets;

	for (i = 0; i < rimap->rmp_nrims; i++) {
		rc = rim_generate(rimap, i, ntargets, buf);
		if (rc != 0)
			goto out;
	}

	D_DEBUG(DF_PL, "Built %d rims for placement map\n", rimap->rmp_nrims);
 out:
	if (buf != NULL)
		rim_buf_destroy(buf);
	return rc;
}

/* each target has at least 10 bits for the key range of consistent hash */
#define TARGET_BITS		10
/* one million for domains */
#define DOMAIN_BITS		20
/* maximum bits for a rim */
#define TARGET_HASH_BITS	45
/* max to 8 million rims */
#define RIM_HASH_BITS		23

/** for comparision of float/double */
#define RIM_PRECISION		0.00001
/** 128K > RIM_PRECISION_FACTOR */
#define RIM_PRECISION_BITS	17
/** should be less than 128K */
#define RIM_PRECISION_FACTOR	100000

/**
 * create consistent hashes for rimap
 */
static int
rim_map_hash_build(pl_rim_map_t *rimap)
{
	uint64_t	range;
	double		stride;
	double		hash;
	unsigned	dom_ntgs;
	int		i;

	D_DEBUG(DF_PL, "Build consistent hash for rim map\n");
	rimap->rmp_target_hashes = calloc(rimap->rmp_ntargets,
					  sizeof(*rimap->rmp_target_hashes));
	if (rimap->rmp_target_hashes == NULL)
		return -ENOMEM;

	rimap->rmp_rim_hashes = calloc(rimap->rmp_nrims,
				       sizeof(*rimap->rmp_rim_hashes));
	if (rimap->rmp_rim_hashes == NULL)
		return -ENOMEM;

	dom_ntgs = rimap->rmp_ntargets / rimap->rmp_ndomains;
	rimap->rmp_target_hbits = DOMAIN_BITS + TARGET_BITS +
				  daos_power2_nbits(dom_ntgs);
	if (rimap->rmp_target_hbits > TARGET_HASH_BITS)
		rimap->rmp_target_hbits = TARGET_HASH_BITS;

	range = 1ULL << rimap->rmp_target_hbits;

	D_DEBUG(DF_PL, "domanis %d, targets %d, hash range is 0-0x"DF_X64"\n",
		rimap->rmp_ndomains, rimap->rmp_ntargets, range);

	/* create consistent hash for targets */
	stride = (double)range / rimap->rmp_ntargets;
	rimap->rmp_stride = stride;

	hash = 0;
	for (i = 0; i < rimap->rmp_ntargets; i++) {
		rimap->rmp_target_hashes[i] = hash;
		hash += stride;
	}

	/* create consistent hash for rims */
	range = 1ULL << RIM_HASH_BITS;
	stride = (double)range / rimap->rmp_nrims;
	hash = 0;

	for (i = 0; i < rimap->rmp_nrims; i++) {
		rimap->rmp_rim_hashes[i] = hash;
		hash += stride;
	}
	return 0;
}

/**
 * Create a rim placement map
 */
static int
rim_map_create(cl_map_t *cl_map, pl_map_attr_t *ma, pl_map_t **mapp)
{
	pl_rim_map_t	*rimap;
	int		 rc;

	D_CASSERT(TARGET_HASH_BITS + RIM_PRECISION_BITS < 64);

	D_ASSERT(ma->u.rim.ra_nrims > 0);
	D_DEBUG(DF_PL, "Create rim map: domain %s, nrim: %d\n",
		cl_comp_type2name(ma->u.rim.ra_domain), ma->u.rim.ra_nrims);

	rimap = calloc(1, sizeof(*rimap));
	if (rimap == NULL)
		return -ENOMEM;

	rimap->rmp_nrims = ma->u.rim.ra_nrims;
	rimap->rmp_clmap = cl_map;

	rc = rim_map_build(rimap, ma->ma_version, -1, ma->u.rim.ra_domain);
	if (rc != 0)
		goto err_out;

	rc = rim_map_hash_build(rimap);
	if (rc != 0)
		goto err_out;

	*mapp = &rimap->rmp_map;
	return 0;
 err_out:
	rim_map_destroy(&rimap->rmp_map);
	return rc;
}

/**
 * destroy a rim map and all its rims
 */
static void
rim_map_destroy(pl_map_t *map)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	int		 i;

	if (rimap->rmp_rim_hashes != NULL)
		free(rimap->rmp_rim_hashes);

	if (rimap->rmp_target_hashes != NULL)
		free(rimap->rmp_target_hashes);

	if (rimap->rmp_rims != NULL) {
		for (i = 0; i < rimap->rmp_nrims; i++)
			rim_release(rimap, &rimap->rmp_rims[i]);

		free(rimap->rmp_rims);
	}
	free(rimap);
}

/**
 * print all rims of a rimap, it is for debug only
 */
static void
rim_map_print(pl_map_t *map)
{
	pl_rim_map_t *rimap = pl_map2rimap(map);
	int	      i;

	D_PRINT("rim map: ver %d, nrims %d, hash 0-"DF_X64"\n",
		rimap->rmp_map.pm_ver, rimap->rmp_nrims,
		(1ULL << rimap->rmp_target_hbits));

	for (i = 0; i < rimap->rmp_nrims; i++)
		rim_print(rimap, i);
}

/** hash object ID, find a rim by consistent hash */
static pl_rim_t *
rim_oid2rim(pl_rim_map_t *rimap, daos_obj_id_t id)
{
	uint64_t key = id.body[0] + id.body[1];
	uint64_t hash;

	/* mix bits */
	hash  = (key >> 32) << 32;
	hash |= (key >> 8) & 0xff;
	hash |= (key & 0xff) << 8;
	hash |= ((key >> 16) & 0xff) << 24;
	hash |= ((key >> 24) & 0xff) << 16;

	hash = daos_u32_hash(hash, RIM_HASH_BITS);
	hash = daos_chash_srch_u64(rimap->rmp_rim_hashes,
				   rimap->rmp_nrims, hash);
	return &rimap->rmp_rims[hash];
}

/** hash object ID, find a target on a rim by consistent hash */
static unsigned int
rim_oid2index(pl_rim_map_t *rimap, daos_obj_id_t id)
{
	uint64_t hash;

	/* mix bits */
	hash  = id.body[0];
	hash ^= hash << 29;
	hash += hash << 11;
	hash -= id.body[1];
	hash  = daos_u64_hash(hash, TARGET_HASH_BITS);
	hash &= (1ULL << rimap->rmp_target_hbits) - 1;

	return daos_chash_srch_u64(rimap->rmp_target_hashes,
				   rimap->rmp_ntargets, hash);
}

static unsigned int
rim_obj_sid2stripe(uint32_t sid, pl_obj_attr_t *oa)
{
	/* XXX This is for byte array only, sid is daos-m object
	 * shard index which is sequential.
	 * For KV object, it could be more complex.
	 */
	return sid / oa->oa_rd_grp;
}

/** another prime for hash */
#define PL_GOLDEN_PRIME	0x9e37fffffffc0001ULL

/**
 * select spare node for a redundancy group, \a first is the rim offset of
 * the first target of the redundancy group
 */
static int
rim_select_spare(daos_obj_id_t id, int first, int dist, int ntargets,
		 pl_obj_attr_t *oa)
{
	uint64_t	hash;
	unsigned	skip;
	int		sign;
	int		i;

	hash = id.body[0] ^ id.body[1];
	hash *= PL_GOLDEN_PRIME;
	skip = hash % (oa->oa_spare_skip + 1);

	sign = (hash & 1) == 0 ? -1 : 1;
	for (i = 0; i < skip; i++)
		first += sign * dist * (oa->oa_rd_grp + oa->oa_nspares);

	if (sign > 0)
		first += oa->oa_rd_grp * dist;
	else
		first -= oa->oa_nspares * dist;

	if (first > ntargets)
		return first - ntargets;
	if (first < 0)
		return first + ntargets;

	return first;
}

static int
rim_next_spare(daos_obj_id_t id, int spare, int dist, int ntargets)
{
	spare += dist;
	return spare >= ntargets ? spare - ntargets : spare;
}

/** convert double number to u64 which can be stored in target */
static uint64_t
rim_stride2u64(double stride)
{
	D_ASSERT(stride < (1ULL << (64 - RIM_PRECISION_BITS)));

	return (uint64_t)(stride * RIM_PRECISION_FACTOR);
}

/** convert u64 to double number */
static double
rim_u642stride(uint64_t stride)
{
	return (double)stride / RIM_PRECISION_FACTOR;
}

/** calculate distance between to object shard */
static int
rim_obj_shard_dist(pl_rim_map_t *rimap, pl_obj_shard_t *obs)
{
	double	stride;
	int	dist;

	stride = rim_u642stride(obs->os_stride);
	D_ASSERT(stride > 0);
	dist = stride / rimap->rmp_stride + RIM_PRECISION;
	D_ASSERT(dist > 0);

	return dist;
}

/**
 * (Re)compute distribution for the input object shard.
 * See *\a pl_map_obj_select() for details.
 */
static int
rim_map_obj_select(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		   pl_select_opc_t select, unsigned int obs_arr_len,
		   pl_obj_shard_t *obs_arr)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	double		 stride;
	unsigned int	 ntargets;
	unsigned int	 nstripes;
	unsigned int	 index;
	uint32_t	 sid;
	int		 stripe;
	int		 dist;
	int		 grp_dist;
	int		 nobss;
	int		 i;
	int		 j;

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	pts	 = rim_oid2rim(rimap, obs->os_id)->rim_targets;
	sid	 = obs->os_sid;
	nstripes = oa->oa_nstripes;

	if (obs->os_stride == 0) {
		stride = rimap->rmp_stride;
		obs->os_stride = rim_stride2u64(stride);
	} else {
		stride = rim_u642stride(obs->os_stride);
	}

	if (oa->oa_start == -1)
		index = rim_oid2index(rimap, obs->os_id);
	else
		index = oa->oa_start;

	if (sid == -1) {
		i = j = sid = stripe = 0;
	} else {
		stripe = rim_obj_sid2stripe(sid, oa);
		j = sid % oa->oa_rd_grp;

		switch (select) {
		default:
		case PL_SEL_GRP_PREV:	/* TODO */
		case PL_SEL_GRP_SPLIT:	/* TODO */
			return -EINVAL;
		case PL_SEL_ALL:
			break;
		case PL_SEL_CUR:
			obs_arr_len = 1;
			break;
		case PL_SEL_GRP_CUR:
			obs_arr_len = MIN(obs_arr_len, oa->oa_rd_grp);
			sid -= j;
			j = 0;
			break;
		case PL_SEL_GRP_NEXT:
			obs_arr_len = MIN(obs_arr_len, oa->oa_rd_grp);
			sid += oa->oa_rd_grp - j;
			stripe++;
			j = 0;
			break;
		}
	}

	dist = rim_obj_shard_dist(rimap, obs);
	grp_dist = (oa->oa_rd_grp + oa->oa_nspares) * dist;
	index += stripe * grp_dist;

	for (i = stripe, nobss = 0; i < nstripes && obs_arr_len > 0; i++) {
		int spare;

		spare = rim_select_spare(obs->os_id, index, dist, ntargets, oa);

		for (; j < oa->oa_rd_grp && obs_arr_len > 0;
		     j++, obs_arr_len--) {
			int pos = pts[(index + j * dist) % ntargets].pt_pos;

			while (targets[pos].co_status != CL_COMP_ST_UP) {
				pos = pts[spare].pt_pos;
				spare = rim_next_spare(obs->os_id, spare,
						       dist, ntargets);
			}

			obs_arr[nobss].os_rank	 = targets[pos].co_rank;
			obs_arr[nobss].os_stride = rim_stride2u64(stride);
			obs_arr[nobss].os_sid	 = sid++;
			nobss++;
		}
		index += grp_dist;
		j = 0;
	}
	return nobss;
}

/**
 * Check if object rebuilding should be triggered for the failed target
 * identified by \a failed. See \a pl_map_obj_rebuild for details.
 */
bool
rim_map_obj_rebuild(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		    daos_rank_t failed, pl_obj_shard_t *obs_rbd)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	daos_rank_t	 rank;
	unsigned int	 ntargets;
	unsigned int	 stripe;
	bool		 coordinator;
	int		 found;
	int		 dist;
	int		 spare;
	int		 sid;
	int		 index;
	int		 i;

	/* XXX This is going to scan all stripes of an object, it is obviously
	 * not smart enough.
	 */
	D_DEBUG(DF_PL, "Select spare for "DF_U64".%u stripe %d, rd %d\n",
		obs->os_id.body[0], obs->os_sid, oa->oa_nstripes,
		oa->oa_rd_grp);

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	index	 = rim_oid2index(rimap, obs->os_id);
	pts	 = rim_oid2rim(rimap, obs->os_id)->rim_targets;

	dist	 = rim_obj_shard_dist(rimap, obs);
	stripe	 = rim_obj_sid2stripe(obs->os_sid, oa);
	index   += stripe * dist * (oa->oa_rd_grp + oa->oa_nspares);
	spare	 = rim_select_spare(obs->os_id, index, dist, ntargets, oa);
	sid	 = obs->os_sid - obs->os_sid % oa->oa_rd_grp;

	found	= 0;
	coordinator = false;
	for (i = 0; i < oa->oa_rd_grp; i++) {
		int pos = pts[(index + i * dist) % ntargets].pt_pos;

		/* XXX: For the time being, I assume the first alive object
		 * shard is the redundancy group leader. It could be changed
		 * and depend on the object schema.
		 */
		if (targets[pos].co_status == CL_COMP_ST_UP) {
			if (!coordinator) {
				/* I'm not the group coordinator, just return
				 * because only the group coordinator is
				 * responsible for rebuilding.
				 */
				if (obs->os_rank != targets[pos].co_rank)
					return false;

				/* This object shard is the group coordinator */
				coordinator = true;
			}

		} else { /* target is down */
			while (targets[pos].co_status != CL_COMP_ST_UP) {
				if (targets[pos].co_rank == failed) {
					D_ASSERT(found == 0);
					found++;
				}

				pos = pts[spare].pt_pos;
				spare = rim_next_spare(obs->os_id, spare, dist,
						       ntargets);
			}

			if (found == 1) {
				rank = targets[pos].co_rank;
				sid += i;
				found++;
			}
		}

		if (!found) /* continue to search for the failed shard */
			continue;

		if (!coordinator)
			continue;

		/* I'm the coordinator of my redundancy group, and my group
		 * indeed has an object shard in the failed target */
		obs_rbd->os_id	   = obs->os_id;
		obs_rbd->os_stride = obs->os_stride;
		obs_rbd->os_rank   = rank;
		obs_rbd->os_sid	   = sid;
		return true;
	}
	return false;
}

/**
 * Check if an object shard \a obs needs to be recovered for (moved back to)
 * the recovered target identified by \a recovered.
 */
bool
rim_map_obj_recover(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		    daos_rank_t recovered)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	unsigned int	 ntargets;
	int		 index;
	int		 stripe;
	int		 dist;

	D_DEBUG(DF_PL, "Check recover "DF_U64".%u stripe %d, rd %d\n",
		obs->os_id.body[0], obs->os_sid, oa->oa_nstripes,
		oa->oa_rd_grp);

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	index	 = rim_oid2index(rimap, obs->os_id);
	pts	 = rim_oid2rim(rimap, obs->os_id)->rim_targets;

	dist	 = rim_obj_shard_dist(rimap, obs);
	stripe	 = rim_obj_sid2stripe(obs->os_sid, oa);
	index	+= (stripe * (oa->oa_rd_grp + oa->oa_nspares) +
		    obs->os_sid % oa->oa_rd_grp) * dist;

	index = pts[index % ntargets].pt_pos;
	return targets[index].co_rank == recovered;
}

pl_map_ops_t	rim_map_ops = {
	.o_create	= rim_map_create,
	.o_destroy	= rim_map_destroy,
	.o_print	= rim_map_print,
	.o_obj_select	= rim_map_obj_select,
	.o_obj_rebuild	= rim_map_obj_rebuild,
	.o_obj_recover	= rim_map_obj_recover,
};
