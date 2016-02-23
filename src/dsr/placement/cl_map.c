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
 * dsr/placement/cl_map.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <cl_map.h>

static int cl_map_create_internal(cl_buf_t *buf, bool scratch, cl_map_t **mapp);

struct cl_comp_state_helper {
	cl_comp_state_t	 csh_state;
	char		*csh_name;
};

static struct cl_comp_state_helper cl_comp_state_helpers[] = {
	{
		.csh_state	= CL_COMP_ST_UP,
		.csh_name	= "UP",
	},
	{
		.csh_state	= CL_COMP_ST_DOWN,
		.csh_name	= "DOWN",
	},
	{
		.csh_state	= CL_COMP_ST_NEW,
		.csh_name	= "NEW",
	},
	{
		.csh_state	= CL_COMP_ST_UNKNOWN,
		.csh_name	= "UNKNOWN",
	},
};

struct cl_comp_type_helper {
	cl_comp_type_t	 cch_type;
	char		 cch_abbr;
	char		*cch_name;
};

static struct cl_comp_type_helper cl_comp_type_helpers[] = {
	{
		.cch_type	= CL_COMP_TARGET,
		.cch_abbr	= 't',
		.cch_name	= "target",
	},
	{
		.cch_type	= CL_COMP_NODE,
		.cch_abbr	= 'n',
		.cch_name	= "node",
	},
	{
		.cch_type	= CL_COMP_BOARD,
		.cch_abbr	= 'b',
		.cch_name	= "board",
	},
	{
		.cch_type	= CL_COMP_BLADE,
		.cch_abbr	= 'l',
		.cch_name	= "blade",
	},
	{
		.cch_type	= CL_COMP_RACK,
		.cch_abbr	= 'r',
		.cch_name	= "rack",
	},
	{
		.cch_type	= CL_COMP_ROOT,
		.cch_abbr	= 'o',
		.cch_name	= "root",
	},
	{
		.cch_type	= CL_COMP_DUMMY,
		.cch_abbr	= 'y',
		.cch_name	= "unknown",
	},
};

char *
cl_comp_state2name(cl_comp_state_t state)
{
	struct cl_comp_state_helper *csh = &cl_comp_state_helpers[0];

	for (; csh->csh_state != CL_COMP_ST_UNKNOWN; csh++) {
		if (csh->csh_state == state)
			return csh->csh_name;
	}
	return csh->csh_name; /* unknown */
}

cl_comp_state_t
cl_comp_name2state(char *name)
{
	struct cl_comp_state_helper *csh = &cl_comp_state_helpers[0];

	for (; csh->csh_state != CL_COMP_ST_UNKNOWN; csh++) {
		if (strcasecmp(name, csh->csh_name) == 0)
			return csh->csh_state;
	}
	return csh->csh_state; /* unknown */
}

char *
cl_comp_type2name(cl_comp_type_t type)
{
	struct cl_comp_type_helper *cch = &cl_comp_type_helpers[0];

	for (; cch->cch_type != CL_COMP_DUMMY; cch++) {
		if (cch->cch_type == type)
			return cch->cch_name;
	}
	return cch->cch_name; /* unknown */
}

cl_comp_type_t
cl_comp_name2type(char *name)
{
	struct cl_comp_type_helper *cch = &cl_comp_type_helpers[0];

	for (; cch->cch_type != CL_COMP_DUMMY; cch++) {
		if (strcasecmp(name, cch->cch_name) == 0)
			return cch->cch_type;
	}
	return cch->cch_type; /* unknown */
}

cl_comp_type_t
cl_comp_abbr2type(char abbr)
{
	struct cl_comp_type_helper *cch = &cl_comp_type_helpers[0];

	abbr = tolower(abbr);
	for (; cch->cch_type != CL_COMP_DUMMY; cch++) {
		if (abbr == cch->cch_abbr)
			return cch->cch_type;
	}
	return cch->cch_type; /* unknown */
}

/** Count number of domains, targets, and layers of domains etc. */
void
cl_buf_count(cl_buf_t *buf, cl_buf_count_t *cntr)
{
	cl_domain_t	*doms = buf;
	unsigned	 ndoms;

	memset(cntr, 0, sizeof(*cntr));
	if (doms[0].cd_children != NULL) {
		ndoms = doms[0].cd_children - doms;
	} else {
		D_ASSERT(doms[0].cd_targets != NULL);
		ndoms = (cl_domain_t *)doms[0].cd_targets - doms;
	}

	cntr->cc_ndoms_top = ndoms;
	cntr->cc_ndoms = ndoms;

	for (doms = buf; doms != NULL;
	     doms = doms[0].cd_children, cntr->cc_nlayers++) {
		int      num;
		int      i;

		D_DEBUG(0, "%s, ndoms = %d\n",
			cl_domain_name(&doms[0]), ndoms);
		for (i = num = 0; i < ndoms; i++) {
			if (doms[i].cd_children != NULL) {
				cntr->cc_ndoms += doms[i].cd_nchildren;
				num += doms[i].cd_nchildren;
			} else {
				cntr->cc_ntargets += doms[i].cd_ntargets;
			}
		}
		ndoms = num;
	}
}

unsigned int
cl_buf_size(cl_buf_t *buf)
{
	cl_buf_count_t cntr;

	cl_buf_count(buf, &cntr);
	return sizeof(cl_target_t) * cntr.cc_ntargets +
	       sizeof(cl_domain_t) * cntr.cc_ndoms;
}

/** check if component buffer is sane */
bool
cl_buf_sane(cl_buf_t *buf)
{
	cl_domain_t	*doms;
	cl_domain_t	*parent;
	cl_target_t	*targets;
	cl_buf_count_t	 cntr;
	int		 ndoms;
	int		 i;

	D_DEBUG(DF_CL, "Sanity check of component buffer\n");
	cl_buf_count(buf, &cntr);
	if (cntr.cc_ntargets == 0) {
		D_DEBUG(DF_CL, "Buffer has no target\n");
		return false;
	}

	ndoms = cntr.cc_ndoms_top;
	for (doms = buf, parent = NULL;
	     doms != NULL; doms = doms[0].cd_children) {
		cl_domain_t  *prev = &doms[0];
		int	      num  = 0;
		int	      i;

		if (parent != NULL &&
		    parent->cd_comp.co_type >= doms[0].cd_comp.co_type) {
			D_DEBUG(DF_CL,
				"Type of parent domain %d(%s) should be "
				"smaller than child domain %d(%s)\n",
				parent->cd_comp.co_type, cl_domain_name(parent),
				doms[0].cd_comp.co_type,
				cl_domain_name(&doms[0]));
			return false;
		}

		for (i = 0; i < ndoms; i++) {
			if (prev->cd_comp.co_type != doms[i].cd_comp.co_type) {
				D_DEBUG(DF_CL, "Unmatched domain type %d/%d\n",
					doms[i].cd_comp.co_type,
					prev->cd_comp.co_type);
				return false;
			}

			if ((doms[i].cd_children == NULL) ^
			    (doms[i].cd_nchildren == 0)) {
				D_DEBUG(DF_CL, "Invalid children\n");
				return false;
			}

			if (doms[i].cd_targets == NULL ||
			    doms[i].cd_ntargets == 0) {
				D_DEBUG(DF_CL, "No target found\n");
				return false; /* always has targets */
			}

			if ((prev->cd_children == NULL) ^
			    (doms[i].cd_children == NULL)) {
				D_DEBUG(DF_CL, "Invalid child tree\n");
				return false;
			}

			if ((prev->cd_targets == NULL) ^
			    (doms[i].cd_targets == NULL)) {
				D_DEBUG(DF_CL, "Invalid target tree\n");
				return false;
			}

			if (prev != &doms[i] &&
			    prev->cd_children != NULL &&
			    prev->cd_children + prev->cd_nchildren !=
			    doms[i].cd_children) {
				D_DEBUG(DF_CL, "Invalid children pointer\n");
				return false;
			}

			if (prev != &doms[i] &&
			    prev->cd_targets != NULL &&
			    prev->cd_targets + prev->cd_ntargets !=
			    doms[i].cd_targets) {
				D_DEBUG(DF_CL, "Invalid children pointer\n");
				return false;
			}

			if (doms[i].cd_nchildren != 0)
				num += doms[i].cd_nchildren;

			prev = &doms[i];
		}
		parent = &doms[0];
		ndoms = num;
	}

	doms = buf;
	targets = doms[0].cd_targets;
	for (i = 0; i < cntr.cc_ntargets; i++) {
		if (targets[i].co_type != CL_COMP_TARGET) {
			D_DEBUG(DF_CL, "Invalid leaf type %d(%s)\n",
				targets[i].co_type, cl_comp_name(&targets[i]));
			return false;
		}
	}
	D_DEBUG(DF_CL, "Component buffer is sane\n");
	return true;
}

/** rebuild pointers for component buffer */
void
cl_buf_rebuild(cl_buf_t *buf, cl_buf_count_t *cntr)
{
	cl_domain_t	*doms;
	cl_target_t	*targets;
	int		 ndoms;

	D_DEBUG(DF_CL, "Layers %d, top domains %d, domains %d, targets %d\n",
		cntr->cc_nlayers, cntr->cc_ndoms_top, cntr->cc_ndoms,
		cntr->cc_ntargets);

	targets = (void *)buf + cntr->cc_ndoms * sizeof(cl_domain_t);

	for (doms = buf, ndoms = cntr->cc_ndoms_top;
	     doms != NULL; doms = doms[0].cd_children) {
		cl_domain_t *children = &doms[ndoms];
		cl_target_t *tgs = targets;
		int	     num = 0;
		int	     i;

		for (i = 0; i < ndoms; i++) {
			if (doms[i].cd_children != NULL) {
				doms[i].cd_children = children;
				num += doms[i].cd_nchildren;
				children += doms[i].cd_nchildren;
			}
			doms[i].cd_targets = tgs;
			tgs += doms[i].cd_ntargets;
		}
		ndoms = num;
	}
}

/** copy components buffer */
void
cl_buf_copy(cl_buf_t *dst, cl_buf_t *src)
{
	cl_buf_count_t	cntr;

	memcpy(dst, src, cl_buf_size(src));

	cl_buf_count(src, &cntr);
	cl_buf_rebuild(dst, &cntr);
}

/** duplicate component buffer */
cl_buf_t *
cl_buf_dup(cl_buf_t *buf)
{
	cl_buf_t *dst;

	dst = calloc(1, cl_buf_size(buf));
	if (dst == NULL)
		return NULL;

	cl_buf_copy(dst, buf);
	return dst;
}

/**
 * check if a component buffer is compatible with a cluster map, it returns
 * true if components in \a buf can be merged into \a map, otherwise returns
 * false.
 */
bool
cl_buf_compat(cl_buf_t *buf, cl_map_t *map)
{
	cl_buf_t	*map_buf;
	cl_domain_t	*map_doms;
	cl_domain_t	*parent;
	cl_domain_t	*doms;
	cl_buf_count_t   cntr;
	int		 ndoms;

	doms = buf;
	if (cl_map_empty(map)) {
		D_DEBUG(DF_CL, "empty map, type of buffer root is %s\n",
			cl_domain_name(&doms[0]));
		return true;
	}

	if (doms[0].cd_comp.co_type != CL_COMP_ROOT &&
	    doms[0].cd_comp.co_type != CL_COMP_DUMMY)
		return false;

	cl_map_find_buf(map, doms[1].cd_comp.co_type, &map_buf);
	map_doms = map_buf;

	parent = NULL;
	if (doms[0].cd_comp.co_type == CL_COMP_ROOT) {
		if (map_doms - cl_map_buf(map) != 1) {
			D_DEBUG(DF_CL, "Invalid buffer\n");
			return false;
		}
		parent = &doms[0];
	}

	D_DEBUG(DF_CL, "Check if buffer is compatible with cluster map\n");

	doms++; /* skip root or dummy */
	cl_buf_count(buf + 1, &cntr);

	for (ndoms = cntr.cc_ndoms_top; doms != NULL;
	     doms = doms[0].cd_children, map_doms = map_doms[0].cd_children) {
		int     nchildren_sum = 0;
		int	nchildren = 0;
		int     i;
		int	j;

		if (map_doms == NULL) {
			D_DEBUG(DF_CL, "Buffer has more layers than map\n");
			return false;
		}

		D_DEBUG(DF_CL, "checking %s/%s\n", cl_domain_name(&doms[0]),
			cl_domain_name(&map_doms[0]));
		for (i = 0; i < ndoms; i++) {
			cl_component_t	*com = &doms[i].cd_comp;
			cl_target_t	*tg;

			if (com->co_type != map_doms[0].cd_comp.co_type) {
				D_DEBUG(DF_CL,
					"domain type not match %s(%u) %s(%u)\n",
					cl_comp_name(com), com->co_type,
					cl_domain_name(&map_doms[0]),
					map_doms[0].cd_comp.co_type);
				return false;
			}

			if (!cl_domain_find(map, com->co_type, com->co_rank)) {
				/* parent of new domain should exist */
				if (parent == NULL) {
					D_DEBUG(DF_CL, "Need specified parent "
						       "for new component\n");
					return false;
				}
				com->co_status = CL_COMP_ST_NEW;
			}

			if (doms[i].cd_children != NULL) {
				nchildren_sum += doms[i].cd_nchildren;
			} else {
				/* the last layer domain */
				if (map_doms[0].cd_children != NULL) {
					D_DEBUG(DF_CL, "unmatched tree\n");
					return false;
				}

				for (j = 0; j < doms[i].cd_ntargets; j++) {
					tg = &doms[i].cd_targets[j];

					if (!cl_target_find(map, tg->co_rank)) {
						tg->co_status = CL_COMP_ST_NEW;

					} else if (cl_comp_is_new(com)) {
						D_DEBUG(DF_CL,
							"can't move target\n");
						return false;
					}
				}
			}

			if (parent == NULL)
				continue;

			if (cl_comp_is_new(&parent->cd_comp) &&
			    !cl_comp_is_new(com)) {
				D_DEBUG(DF_CL, "can't move component\n");
				return false;
			}

			if (parent->cd_nchildren == ++nchildren) {
				parent++;
				nchildren = 0;
			}
		}
		ndoms = nchildren_sum;
		parent = &doms[0];
	}
	return true;
}

static void
cl_target_swap(void *array, int a, int b)
{
	cl_target_t	**ta = array;
	cl_target_t	 *tmp;

	tmp = ta[a];
	ta[a] = ta[b];
	ta[b] = tmp;
}

static int
cl_target_cmp(void *array, int a, int b)
{
	cl_target_t	**ta = array;

	if (ta[a]->co_rank > ta[b]->co_rank)
		return 1;
	if (ta[a]->co_rank < ta[b]->co_rank)
		return -1;
	return 0;
}

static int
cl_target_cmp_key(void *array, int i, uint64_t key)
{
	cl_target_t	**ta = array;
	daos_rank_t	  rank = (daos_rank_t)key;

	if (ta[i]->co_rank > rank)
		return 1;
	if (ta[i]->co_rank < rank)
		return -1;
	return 0;
}

/** rank based sort and search for target */
daos_sort_ops_t cl_target_sort_ops = {
	.so_swap	= cl_target_swap,
	.so_cmp		= cl_target_cmp,
	.so_cmp_key	= cl_target_cmp_key,
};

static int
cl_target_vcmp(void *array, int a, int b)
{
	cl_target_t	**ta = array;

	if (ta[a]->co_ver > ta[b]->co_ver)
		return 1;
	if (ta[a]->co_ver < ta[b]->co_ver)
		return -1;
	return 0;
}

/** version based sort */
daos_sort_ops_t cl_target_vsort_ops = {
	.so_swap	= cl_target_swap,
	.so_cmp		= cl_target_vcmp,
};

static void
cl_domain_swap(void *array, int a, int b)
{
	cl_domain_t	**da = array;
	cl_domain_t	 *tmp;

	tmp = da[a];
	da[a] = da[b];
	da[b] = tmp;
}

static int
cl_domain_cmp(void *array, int a, int b)
{
	cl_domain_t	**da = array;

	if (da[a]->cd_comp.co_rank > da[b]->cd_comp.co_rank)
		return 1;
	if (da[a]->cd_comp.co_rank < da[b]->cd_comp.co_rank)
		return -1;
	return 0;
}

static int
cl_domain_cmp_key(void *array, int i, uint64_t key)
{
	cl_domain_t	**da = array;
	daos_rank_t	  rank = (daos_rank_t)key;

	if (da[i]->cd_comp.co_rank > rank)
		return 1;
	if (da[i]->cd_comp.co_rank < rank)
		return -1;
	return 0;
}

/** rank based sort and search for domains */
daos_sort_ops_t cl_domain_sort_ops = {
	.so_swap	= cl_domain_swap,
	.so_cmp		= cl_domain_cmp,
	.so_cmp_key	= cl_domain_cmp_key,
};

static int
cl_domain_tcmp_key(void *array, int i, uint64_t key)
{
	cl_domain_t	 ***domspp = array;
	cl_comp_type_t      type = (cl_comp_type_t)key;

	D_ASSERT(domspp[i] != NULL);
	if (domspp[i][0]->cd_comp.co_type > type)
		return 1;
	if (domspp[i][0]->cd_comp.co_type < type)
		return -1;
	return 0;
}

/** type based sort and search for domains */
static daos_sort_ops_t cl_domain_tsort_ops = {
	.so_cmp_key	= cl_domain_tcmp_key,
};

/**
 * Sort all domains and targets in a clustre map by combsort.
 */
static int
cl_map_sort(cl_map_t *map)
{
	int	i;
	int	rc;

	/* sort all targets */
	D_ASSERT(map->clm_targets != NULL);

	rc = daos_array_sort(map->clm_targets, map->clm_ntargets, true,
			     &cl_target_sort_ops);
	if (rc < 0)
		return rc;

	/* sort all domains */
	D_ASSERT(map->clm_doms != NULL);
	D_ASSERT(map->clm_ndoms != NULL);

	for (i = 0; i < map->clm_nlayers; i++) {
		rc = daos_array_sort(map->clm_doms[i], map->clm_ndoms[i], true,
				     &cl_domain_sort_ops);
		if (rc < 0)
			return rc;
	}
	return 0;
}

/** free buffers of cluster map */
static void
cl_map_cleanup(cl_map_t *map)
{
	D_DEBUG(DF_CL, "Release buffers for cluster map\n");

	if (map->clm_targets != NULL) {
		free(map->clm_targets);
		map->clm_targets  = NULL;
		map->clm_ntargets = 0;
	}

	if (map->clm_doms != NULL) {
		D_ASSERT(map->clm_ndoms != NULL);
		D_ASSERT(map->clm_nlayers != 0);

		/* all arrays share the same buffer */
		if (map->clm_doms[0] != NULL)
			free(map->clm_doms[0]);

		free(map->clm_doms);
		free(map->clm_ndoms);

		map->clm_doms	   = NULL;
		map->clm_ndoms	   = NULL;
		map->clm_ndoms_sum = 0;
		map->clm_nlayers   = 0;
	}

	if (map->clm_root != NULL) {
		free(map->clm_root);
		map->clm_root = NULL;
	}
}

/**
 * Install component buffer to a cluster map
 * \param version	new version of cluster map, -1 means it is a
 *			scratch cluster map for internal use.
 */
static int
cl_map_setup(cl_map_t *map, unsigned int version, cl_buf_t *buf)
{
	cl_domain_t	**dompp;
	cl_domain_t	 *doms;
	cl_buf_count_t	  cntr;
	int		  i;
	int		  rc = 0;

	D_ASSERT(cl_map_empty(map));

	doms = buf;
	if (doms[0].cd_comp.co_type == CL_COMP_DUMMY) {
		version = -1;

	} else if (doms[0].cd_comp.co_type != CL_COMP_ROOT) {
		D_DEBUG(DF_CL, "Top domain must be root or dummy %s/%d\n",
			cl_domain_name(&doms[0]), doms[0].cd_comp.co_type);
		return -EINVAL;
	}


	map->clm_root = &doms[0];

	cl_buf_count(buf, &cntr);
	map->clm_nlayers = cntr.cc_nlayers;
	map->clm_ntargets = cntr.cc_ntargets;
	map->clm_ndoms_sum = cntr.cc_ndoms;

	/* cl_map_print(map); */
	D_DEBUG(DF_CL, "Setup nlayers %d, ndomains %d, ntargets %d\n",
		cntr.cc_nlayers, cntr.cc_ndoms, cntr.cc_ntargets);

	map->clm_ndoms = calloc(map->clm_nlayers, sizeof(*map->clm_ndoms));
	if (map->clm_ndoms == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	map->clm_doms = calloc(map->clm_nlayers, sizeof(*map->clm_doms));
	if (map->clm_doms == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	D_DEBUG(DF_CL2, "Allocate binary search array for domains\n");
	dompp = calloc(cntr.cc_ndoms, sizeof(*dompp));
	if (dompp == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* pointer arrays for binary search of domains */
	for (i = 0; i < map->clm_nlayers; i++) {
		unsigned int j;

		cl_buf_count(buf, &cntr);
		map->clm_ndoms_sum += cntr.cc_ndoms_top;
		map->clm_ndoms[i]   = cntr.cc_ndoms_top;
		map->clm_doms[i]    = dompp;
		dompp += cntr.cc_ndoms_top;

		doms = buf;
		D_DEBUG(DF_CL, "domain %s, ndomains %d\n",
			cl_domain_name(&doms[0]), cntr.cc_ndoms_top);

		for (j = 0; j < cntr.cc_ndoms_top; j++) {
			if (version != -1 &&
			    (cl_comp_is_new(&doms[j].cd_comp) ||
			     cl_comp_is_unknown(&doms[j].cd_comp))) {
				doms[j].cd_comp.co_status = CL_COMP_ST_UP;
				doms[j].cd_comp.co_ver = version;
			}
			map->clm_doms[i][j] = &doms[j];
		}
		buf = &doms[cntr.cc_ndoms_top];
	}

	D_DEBUG(DF_CL2, "Allocate binary search array for targets\n");

	/* pointer array for binary search of target */
	map->clm_targets = calloc(map->clm_ntargets, sizeof(*map->clm_targets));
	if (map->clm_targets == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < map->clm_ntargets; i++) {
		map->clm_targets[i] = &map->clm_root->cd_targets[i];
		if (version != -1 &&
		    (cl_comp_is_new(map->clm_targets[i]) ||
		     cl_comp_is_unknown(map->clm_targets[i]))) {
			map->clm_targets[i]->co_status = CL_COMP_ST_UP;
			map->clm_targets[i]->co_ver = version;
		}
	}

	D_DEBUG(DF_CL, "Sort domains and targets\n");
	rc = cl_map_sort(map);
	if (rc != 0)
		goto out;

	D_ASSERT(map->clm_ver == 0 || map->clm_ver < version);
	map->clm_ver = version;
 out:
	if (rc != 0) {
		D_DEBUG(DF_PL, "Failed to setup cluster map %d\n", rc);
		cl_map_cleanup(map);
	}
	return rc;
}

/**
 * Merge all new components from \a src_buf into \a map.
 * Already existent components will be ignored.
 */
static int
cl_map_merge(cl_map_t *map, cl_buf_t *src_buf)
{
	cl_buf_t	*cur_buf;  /* buffer of current cluster map */
	cl_buf_t	*dst_buf;  /* destination buffer for merging */
	cl_domain_t	*dst_doms;
	cl_domain_t	*cur_doms;
	cl_map_t	*src_map;
	void		*addr;
	cl_buf_count_t   cntr;
	unsigned	 version;
	unsigned	 dst_ndoms;
	int		 size;
	int		 i;
	int		 rc;

	D_ASSERT(!cl_map_empty(map));
	if (!cl_buf_compat(src_buf, map)) {
		D_DEBUG(DF_CL, "Buffer is incompatible with cluster map\n");
		return -EINVAL;
	}

	/* create scratch map for merging */
	rc = cl_map_create_internal(src_buf, true, &src_map);
	if (rc != 0) {
		D_DEBUG(DF_CL, "Failed to create scratch map for buffer\n");
		return rc;
	}
	src_buf = cl_map_buf(src_map);

	/* destination buffer could has larger than the actually needed space,
	 * but it is not big deal. */
	cur_buf = cl_map_buf(map);
	size = cl_buf_size(cur_buf) + cl_buf_size(src_buf);
	dst_buf = calloc(1, size);
	if (dst_buf == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	/* copy current cluster map to destination buffer */
	cl_buf_copy(dst_buf, cur_buf);

	/* Merging process starts from the top layer domains in the buffer
	 * which should exist in cluster map. See cl_buf_compat for details.
	 * Skip dummy because it's not a valid domain layer.
	 */
	if (src_buf[0].cd_comp.co_type == CL_COMP_DUMMY)
		cl_map_find_buf(map, src_buf[1].cd_comp.co_type, &cur_buf);
	else
		cl_map_find_buf(map, src_buf[0].cd_comp.co_type, &cur_buf);
	cur_doms = cur_buf;

	dst_doms = dst_buf;
	dst_doms += cur_doms - map->clm_root;
	cl_buf_count((cl_buf_t *)dst_doms, &cntr);
	dst_ndoms = cntr.cc_ndoms_top;

	/* overwrite the components after the top layer domains */
	addr = (void *)&dst_doms[dst_ndoms];
	version = map->clm_ver + 1;
	cl_buf_count(dst_buf, &cntr);

	/* complex buffer manipulating... */
	for (; dst_doms != NULL; dst_doms = dst_doms[0].cd_children) {
		cl_domain_t *cdom = &cur_doms[0];
		int	     nchildren = 0;

		for (i = 0; i < dst_ndoms; i++) {
			cl_domain_t *ddom = &dst_doms[i];
			cl_domain_t *sdom;
			int	     nb;
			int	     j;

			if (ddom->cd_comp.co_ver == version) {
				ddom->cd_children  = NULL;
				ddom->cd_targets   = NULL;
				ddom->cd_nchildren = 0;
				ddom->cd_ntargets  = 0;
				D_DEBUG(DF_CL, "Add new domain %s %d\n",
					cl_domain_name(cdom), dst_ndoms);
			} else {
				/* Domain existed, copy its children/targets
				 * from current cluster map.
				 */
				D_ASSERT(ddom->cd_comp.co_rank ==
					 cdom->cd_comp.co_rank);

				if (cdom->cd_children != NULL) {
					ddom->cd_children = addr;
					ddom->cd_nchildren = cdom->cd_nchildren;
					nb = cdom->cd_nchildren *
					     sizeof(cl_domain_t);
					memcpy(addr, cdom->cd_children, nb);
				} else {
					ddom->cd_targets = addr;
					ddom->cd_ntargets = cdom->cd_ntargets;
					nb = cdom->cd_ntargets *
					     sizeof(cl_target_t);
					memcpy(addr, cdom->cd_targets, nb);
				}
				addr += nb;
				cdom++;
			}

			D_DEBUG(DF_CL, "Check changes for %s[%d]\n",
				cl_domain_name(ddom), ddom->cd_comp.co_rank);

			sdom = cl_domain_find(src_map, ddom->cd_comp.co_type,
					      ddom->cd_comp.co_rank);
			if (sdom == NULL) {
				nchildren += ddom->cd_nchildren;
				continue; /* no change for this domain */
			}

			/* new buffer may have changes for this domain */
			if (sdom->cd_children != NULL) {
				cl_domain_t *child = addr;

				D_DEBUG(DF_CL, "Scan children of %s[%d]\n",
					cl_domain_name(ddom),
					ddom->cd_comp.co_rank);

				if (ddom->cd_children == NULL)
					ddom->cd_children = child;

				/* copy new child domains to dest buffer */
				for (j = 0; j < sdom->cd_nchildren; j++) {
					cl_component_t *com;

					com = &sdom->cd_children[j].cd_comp;
					/* ignore existent children */
					if (com->co_status != CL_COMP_ST_NEW)
						continue;

					D_DEBUG(DF_CL2, "New %s[%d]\n",
						cl_comp_type2name(com->co_type),
						com->co_rank);

					com->co_status = CL_COMP_ST_UP;
					com->co_ver = version;
					*child = sdom->cd_children[j];

					ddom->cd_nchildren++;
					cntr.cc_ndoms++;
					child++;
				}
				addr = child;
			} else {
				cl_target_t *target = addr;

				D_DEBUG(DF_CL, "Scan targets of %s[%d]\n",
					cl_domain_name(ddom),
					ddom->cd_comp.co_rank);

				if (ddom->cd_targets == NULL)
					ddom->cd_targets = target;

				/* copy new targets to destination buffer */
				for (j = 0; j < sdom->cd_ntargets; j++) {
					cl_target_t *tg;

					tg = &sdom->cd_targets[j];
					if (tg->co_status != CL_COMP_ST_NEW)
						continue;

					D_DEBUG(DF_CL2, "New target[%d]\n",
						tg->co_rank);

					tg->co_status = CL_COMP_ST_UP;
					tg->co_ver = version;
					*target = *tg;

					ddom->cd_ntargets++;
					cntr.cc_ntargets++;
					target++;
				}
				addr = target;
			}
			nchildren += ddom->cd_nchildren;
		}
		dst_ndoms = nchildren;
		cur_doms = cur_doms[0].cd_children;
	}
	D_ASSERT(addr - (void *)dst_buf <= size);
	D_DEBUG(DF_CL, "Merged all components\n");
	/* At this point, I only have valid children pointers for the last
	 * layer domains, and need to rebuild target pointers for all layers.
	 */
	cl_buf_rebuild(dst_buf, &cntr);

	/* release old buffers of cluster map */
	cl_map_cleanup(map);

	/* install new buffer for cluster map */
	rc = cl_map_setup(map, version, dst_buf);
	D_ASSERT(rc == 0 || rc == -ENOMEM);
 failed:
	cl_map_destroy(src_map);
	return rc;
}

int
cl_map_extend(cl_map_t *map, cl_buf_t *buf)
{
	int	rc;

	if (!cl_buf_sane(buf)) {
		D_DEBUG(DF_CL, "Insane buffer format\n");
		return -EINVAL;
	}

	D_DEBUG(DF_CL, "Merge buffer with already existent cluster map\n");
	rc = cl_map_merge(map, buf);
	return rc;
}

static int
cl_map_create_internal(cl_buf_t *buf, bool scratch, cl_map_t **mapp)
{
	cl_map_t *map;
	cl_buf_t *tmp;
	int	  rc;

	if (!cl_buf_sane(buf)) {
		D_DEBUG(DF_CL, "Insane buffer format\n");
		return -EINVAL;
	}

	map = calloc(1, sizeof(*map));
	if (map == NULL)
		return -ENOMEM;

	tmp = cl_buf_dup(buf);
	if (tmp == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	rc = cl_map_setup(map, scratch ? -1 : 0, tmp);
	if (rc != 0)
		goto failed;

	*mapp = map;
	return 0;
 failed:
	if (tmp != NULL)
		free(tmp);
	if (map != NULL)
		free(map);
	return rc;
}

int
cl_map_create(cl_buf_t *buf, cl_map_t **mapp)
{
	return cl_map_create_internal(buf, false, mapp);
}

void
cl_map_destroy(cl_map_t *map)
{
	cl_map_cleanup(map);
	free(map);
}

int
cl_map_find_buf(cl_map_t *map, cl_comp_type_t type, cl_buf_t **buf_p)
{
	cl_domain_t	*doms;

	if (buf_p != NULL)
		*buf_p = NULL;

	/* all other domains under root are stored in contiguous buffer */
	for (doms = map->clm_root; doms != NULL; doms = doms->cd_children) {
		if (doms[0].cd_comp.co_type == type)
			break;
	}

	if (doms == NULL) {
		D_DEBUG(DF_CL, "can't find domain type %d/%s\n",
			type, cl_comp_type2name(type));
		return -ENOENT;
	}

	if (buf_p != NULL)
		*buf_p = doms;

	if (doms[0].cd_children != NULL)
		return doms[0].cd_children - doms;

	if (doms[0].cd_targets != NULL)
		return (cl_domain_t *)doms[0].cd_targets - doms;

	D_DEBUG(DF_CL, "Invalid buffer format\n");
	return -EINVAL;
}

/**
 * Find a domain with type \a type and rank \a rank.
 * It is a binary search.
 */
cl_domain_t *
cl_domain_find(cl_map_t *map, cl_comp_type_t type, daos_rank_t rank)
{
	int	tpos;
	int	dpos;

	if (map->clm_doms == NULL) {
		D_ASSERT(cl_map_empty(map));
		return NULL;
	}

	D_ASSERT(map->clm_ndoms != NULL);
	D_ASSERT(map->clm_nlayers > 0);

	/* find domain type, domain types are in descending order */
	tpos = daos_array_find(map->clm_doms, map->clm_nlayers, type,
			       &cl_domain_tsort_ops);
	if (tpos < 0) {
		D_DEBUG(DF_CL, "Can't find domain type %s(%d)\n",
			cl_comp_type2name(type), type);
		return NULL;
	}

	dpos = daos_array_find(map->clm_doms[tpos], map->clm_ndoms[tpos],
			       rank, &cl_domain_sort_ops);
	if (dpos < 0) {
		D_DEBUG(DF_CL, "Can't find domain rank %s(%d)\n",
			cl_comp_type2name(type), rank);
		return NULL;
	}
	return map->clm_doms[tpos][dpos];
}

/**
 * Find target with rank \a rank in cluster map \a map.
 * It is a binary search.
 */
cl_target_t *
cl_target_find(cl_map_t *map, daos_rank_t rank)
{
	cl_target_t **targets = map->clm_targets;
	int	      cur;

	if (targets == NULL) {
		D_ASSERT(cl_map_empty(map));
		return NULL; /* emply cluste map */
	}

	D_ASSERT(map->clm_ntargets > 0);
	cur = daos_array_find(map->clm_targets, map->clm_ntargets, rank,
			      &cl_target_sort_ops);

	D_DEBUG(DF_CL, "Search rank %d in %d targets, %s\n",
		rank, map->clm_ntargets, cur < 0 ? "not found" : "found");

	return cur < 0 ? NULL : map->clm_targets[cur];
}

/** change state of a component */
int
cl_comp_set_state(cl_map_t *map, cl_comp_type_t type, daos_rank_t rank,
		  cl_comp_state_t state)
{
	cl_component_t *comp;

	if (type == CL_COMP_TARGET) {
		comp = cl_target_find(map, rank);
	} else {
		cl_domain_t *dom = cl_domain_find(map, type, rank);

		comp = dom != NULL ? &dom->cd_comp : NULL;
	}

	if (comp == NULL) {
		D_DEBUG(DF_CL, "Cannot find rank %d of %s(%d)\n",
			rank, cl_comp_type2name(type), type);
		return -ENOENT;
	}

	if (comp->co_status == state)
		return 0;

	comp->co_status = state;
	if (state == CL_COMP_ST_UP)
		comp->co_fseq = 0;
	else if (state == CL_COMP_ST_DOWN)
		comp->co_fseq = ++map->clm_fseq;
	return 0;
}

static void
cl_print_indent(int dep)
{
	int	i;

	for (i = 0; i < dep * 8; i++)
		D_PRINT(" ");
}

static void
cl_domain_print(cl_domain_t *domain, int dep)
{
	cl_target_t	*targets;
	int		 i;

	cl_print_indent(dep);
	D_PRINT("%s[%d] %d\n", cl_domain_name(domain),
		domain->cd_comp.co_rank, domain->cd_comp.co_ver);

	D_ASSERT(domain->cd_targets != NULL);

	if (domain->cd_children != NULL) {
		for (i = 0; i < domain->cd_nchildren; i++)
			cl_domain_print(&domain->cd_children[i], dep + 1);
		return;
	}

	targets = domain->cd_targets;

	for (i = 0; i < domain->cd_ntargets; i++) {
		D_ASSERTF(targets[i].co_type == CL_COMP_TARGET,
			  "%s\n", cl_comp_type2name(targets[i].co_type));

		cl_print_indent(dep + 1);
		D_PRINT("%s[%d] %d\n",
			cl_comp_type2name(targets[i].co_type),
			targets[i].co_rank, targets[i].co_ver);
	}
}

void
cl_map_print(cl_map_t *map)
{
	D_PRINT("Cluster map version %d\n", map->clm_ver);
	if (map->clm_root != NULL)
		cl_domain_print(map->clm_root, 0);
}
