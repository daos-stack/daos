/**
 * (C) Copyright 2015 Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/placement/rim_map.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include "pl_map_internal.h"

extern daos_sort_ops_t cl_target_sort_ops;
extern daos_sort_ops_t cl_target_vsort_ops;

/* scratch buffer for reshuffling domains and targets */
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

static void rim_buf_destroy(rim_buf_t *buf);
static void rim_map_destroy(pl_map_t *map);

static inline pl_rim_map_t *
pl_map2rimap(pl_map_t *map)
{
	return container_of(map, pl_rim_map_t, rmp_map);
}

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

static daos_sort_ops_t rim_dom_vsort_ops = {
	.so_cmp		= rim_dom_cmp_ver,
	.so_swap	= rim_dom_swap,
};

static int
rim_dom_cmp_rank(void *array, int a, int b)
{
	struct rim_domain *rda = array;

	if (rda[a].rd_dom->cd_comp.co_rank > rda[b].rd_dom->cd_comp.co_rank)
		return 1;
	if (rda[a].rd_dom->cd_comp.co_rank < rda[b].rd_dom->cd_comp.co_rank)
		return -1;
	return 0;
}

static daos_sort_ops_t rim_dom_sort_ops = {
	.so_cmp		= rim_dom_cmp_rank,
	.so_swap	= rim_dom_swap,
};

/**
 * allocate scratch buffers for reshuffling
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

/** randomly reorg targets in this domain */
static void
rim_dom_reshuffle_targets(struct rim_domain *rdom, unsigned int seed,
			  unsigned int ntargets)
{
	cl_target_t **targets = rdom->rd_targets;;
	int	      start;
	int	      ver;
	int	      num;
	int	      i;
	int	      j;

	/* sort by version */
	D_DEBUG(DF_PL, "Sort %d targets of %s[%d] by version\n",
		rdom->rd_ntargets, cl_domain_name(rdom->rd_dom),
		rdom->rd_dom->cd_comp.co_rank);

	daos_array_sort(targets, rdom->rd_ntargets, false,
			&cl_target_vsort_ops);

	srand(seed + rdom->rd_dom->cd_comp.co_rank);
	ver = rdom->rd_dom->cd_comp.co_ver;
	for (i = start = 0; i < rdom->rd_ntargets && ntargets > 0; i++) {
		if (ver == targets[i]->co_ver &&
		    i < rdom->rd_ntargets - 1 && ntargets > 1)
			continue;

		num = i - start + (ver == targets[i]->co_ver);
		/* sort by rank within this version and make sure we always
		 * generate the same pseudo-random order */
		daos_array_sort(&targets[start], num, true,
				&cl_target_sort_ops);
		for (j = start; num > 0 && ntargets > 0;
		     j++, num--, ntargets--) {
			int off = rand() % num;

			if (off != 0) {
				cl_target_t  *tmp = targets[j + off];

				targets[j + off] = targets[j];
				targets[j] = tmp;
			}
		}

		if (ver != targets[i]->co_ver) {
			ver = targets[i]->co_ver;
			start = i;
		}
	}
}

/**
 * reshuffle an array of domains and targets in each domain
 */
static int
rim_buf_reshuffle(pl_rim_map_t *rimap, unsigned int seed,
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
		cl_domain_t	  *dom = buf->rb_doms[i].rd_dom;
		struct rim_domain *dst;
		struct rim_domain *dst2;
		int		   num;

		rim_dom_reshuffle_targets(&buf->rb_doms[i], seed, ntargets);
		if (ver == dom->cd_comp.co_ver && i < buf->rb_ndoms - 1)
			continue;

		srand(seed + start);
		num = i - start + (ver == dom->cd_comp.co_ver);
		daos_array_sort(&buf->rb_doms[start], num, true,
				&rim_dom_sort_ops);

		for (j = start; num > 0; j++, num--) {
			int off = rand() % num;

			if (off != 0) {
				struct rim_domain rd = buf->rb_doms[j + off];

				buf->rb_doms[j + off] = buf->rb_doms[j];
				buf->rb_doms[j] = rd;
			}
			/* D_PRINT("%s[%d]\n",
			 * 	   cl_domain_name(buf->rb_doms[j].rd_dom),
			 *	   buf->rb_doms[j].rd_dom->cd_comp.co_rank);
			 */
		}

		num = i - start + (ver == dom->cd_comp.co_ver);
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
 * build a rim with random seed \a seed
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

	rc = rim_buf_reshuffle(rimap, idx, ntargets, buf);
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

/* at least 10 bits per target */
#define PL_TARGET_BITS		10
/* 24 bits (16 million) for all domains */
#define PL_DOM_ALL_BITS		24
/* max to 1 million rims */
#define PL_RIM_ALL_BITS		20

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
	rimap->rmp_target_hbits = PL_DOM_ALL_BITS + PL_TARGET_BITS +
				  daos_power2_nbits(dom_ntgs);
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
	range = 1ULL << PL_RIM_ALL_BITS;
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

static pl_rim_t *
rim_hash(pl_rim_map_t *rimap, daos_obj_id_t id)
{
	unsigned index;

	/* select rim */
	index = daos_u32_hash(id.body[0] + id.body[1], PL_RIM_ALL_BITS);
	index = daos_chash_srch_u64(rimap->rmp_rim_hashes,
				    rimap->rmp_nrims, index);

	return &rimap->rmp_rims[index];
}

static unsigned int
rim_obj_hash(pl_rim_map_t *rimap, daos_obj_id_t id, pl_obj_attr_t *oa)
{
	uint64_t hash;
	int	 idx;
	int	 seq;

	hash = daos_u64_hash(id.body[0] + id.body[1], rimap->rmp_target_hbits);
	idx = daos_chash_srch_u64(rimap->rmp_target_hashes,
				  rimap->rmp_ntargets, hash);
	if (oa == NULL || oa->oa_cookie == -1)
		return idx;

	/* XXX This is for byte array only, cookie is sequence number of
	 * daos-m object. For KV object, it could be more complex */
	seq = oa->oa_cookie;
	idx += (seq / oa->oa_nstripes) * (oa->oa_rd_grp + oa->oa_nspares) +
	       (seq % oa->oa_nstripes);;
	return idx % rimap->rmp_ntargets;
}

/**
 * generate an array of target ranks for object
 */
static int
rim_map_obj_select(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		   unsigned int nranks, daos_rank_t *ranks)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	unsigned int	 ntargets;
	unsigned int	 start;
	int		 i;
	int		 j;

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	pts	 = rim_hash(rimap, id)->rim_targets;
	start	 = rim_obj_hash(rimap, id, oa);

	for (i = 0; i < oa->oa_nstripes && nranks > 0; i++) {
		int spare = start + oa->oa_rd_grp;
		int next  = spare + oa->oa_nspares;
		int pos;

		for (j = 0; j < oa->oa_rd_grp && nranks > 0;
		     j++, ranks++, nranks--) {
			pos = pts[(start + j) % ntargets].pt_pos;
			while (targets[pos].co_status != CL_COMP_ST_UP) {
				pos = spare++;
				pos %= ntargets;
			}
			*ranks = targets[pos].co_rank;
		}
		start = next;
	}
	return 0;
}

/**
 * If the object has data chunk on failed target, and current target(current)
 * is leader of redundancy group, this function will return rank of hotspare
 * target.
 */
bool
rim_map_obj_failover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		     daos_rank_t current, daos_rank_t failed,
		     daos_rank_t *failover)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	daos_rank_t	 result;
	unsigned int	 ntargets;
	unsigned int	 start;
	unsigned int	 leader;
	unsigned int	 found;
	int		 i;
	int		 j;

	/* XXX This is going to scan all stripes of an object, it is obviously
	 * not smart enough.
	 */
	D_DEBUG(DF_PL, "Select spare for %u (%d|%d)\n",
		(unsigned int)id.body[0], oa->oa_nstripes, oa->oa_rd_grp);

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	start	 = rim_obj_hash(rimap, id, NULL); /* find offset of target */
	pts	 = rim_hash(rimap, id)->rim_targets;
	leader	 = -1;
	for (i = found = 0; i < oa->oa_nstripes; i++, found = 0) {
		int	spare = start + oa->oa_rd_grp;

		for (j = 0; j < oa->oa_rd_grp && found < 2; j++) {
			int	pos = pts[(start + j) % ntargets].pt_pos;
			bool	found_failed = false;

			while (targets[pos].co_status != CL_COMP_ST_UP) {
				if (targets[pos].co_rank == failed)
					found_failed = true;

				pos = pts[spare % ntargets].pt_pos;
				spare++;
			}

			D_ASSERT(pos < ntargets);
			if (found_failed) {
				result = targets[pos].co_rank;
				found++;
			}

			if (targets[pos].co_rank == current)
				found++;

			/* the first non-spare node is leader
			 * XXX there can be multiple failures, and objects
			 * are on spare nodes.
			 */
			if (leader == -1 &&
			    pos == pts[(start + j) % ntargets].pt_pos)
				leader = pos;
		}

		/* current target and failed target are not in the same
		 * redundancy group? */
		if (found == 1) { /* NO */
			D_DEBUG(DF_PL, "ignore, not in the same group\n");
			return false;
		}

		if (found == 2) /* YES */
			break;
		/* continue to search */
		start += oa->oa_rd_grp + oa->oa_nspares;
		leader = -1;
	}

	if (found < 2) {
		D_DEBUG(DF_PL, "ignore, not match\n");
		return false;
	}

	if (leader == -1 || targets[leader].co_rank != current) {
		D_DEBUG(DF_PL, "ignore, not leader\n");
		return false; /* only group leader should handle this */
	}

	D_DEBUG(DF_PL, "spare for "DF_U64" (%d|%d) is %d\n",
		id.body[0], oa->oa_nstripes, oa->oa_rd_grp, result);

	*failover = result;
	return true;
}

/**
 * check if object \a id needs to recover for \a recovered
 */
bool
rim_map_obj_recover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		    daos_rank_t current, daos_rank_t recovered)
{
	pl_rim_map_t	*rimap = pl_map2rimap(map);
	cl_target_t	*targets;
	pl_target_t	*pts;
	unsigned int	 ntargets;
	unsigned int	 start;
	int		 i;
	int		 j;
	bool		 found = false;

	if (current == recovered)
		return false; /* don't check myself */

	targets	 = cl_map_targets(rimap->rmp_clmap);
	ntargets = cl_map_ntargets(rimap->rmp_clmap);
	start	 = rim_obj_hash(rimap, id, NULL); /* find offset of target */
	pts	 = rim_hash(rimap, id)->rim_targets;

	for (i = 0; i < oa->oa_nstripes; i++) {
		int spare = start + oa->oa_rd_grp;

		for (j = 0; j < oa->oa_rd_grp; j++) {
			int pos	= pts[(start + j) % ntargets].pt_pos;
			int k;

			if (targets[pos].co_rank == current)
				return false; /* it is my own object */

			if (targets[pos].co_rank != recovered)
				continue;

			/* this object is on recovered target */
			for (k = 0; k < oa->oa_nspares; k++) {
				spare += k;
				pos = pts[spare % ntargets].pt_pos;
				if (targets[pos].co_rank == current) {
					found = true;
					break;
				}
			}
		}
		/* continue to search */
		start += oa->oa_rd_grp + oa->oa_nspares;
	}

	return found;
}

pl_map_ops_t	rim_map_ops = {
	.o_create	= rim_map_create,
	.o_destroy	= rim_map_destroy,
	.o_print	= rim_map_print,
	.o_obj_select	= rim_map_obj_select,
	.o_obj_failover	= rim_map_obj_failover,
	.o_obj_recover	= rim_map_obj_recover,
};
