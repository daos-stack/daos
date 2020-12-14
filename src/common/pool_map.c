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
 * This file is part of daos
 *
 * src/common/pool_map.c
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/pool_map.h>

/** counters for component (sub)tree */
struct pool_comp_cntr {
	/** # of domains in the top level */
	unsigned int		 cc_top_doms;
	/** # of all domains */
	unsigned int		 cc_domains;
	/** # of targets */
	unsigned int		 cc_targets;
	/** # of buffer layers */
	unsigned int		 cc_layers;
};

/** component state dictionary */
struct pool_comp_state_dict {
	/** component state */
	pool_comp_state_t	 sd_state;
	/** string name for the state */
	char			*sd_name;
};

/** component type dictionary */
struct pool_comp_type_dict {
	/** type of component */
	pool_comp_type_t	 td_type;
	/** abbreviation for the type */
	char			 td_abbr;
	/** string name for the type */
	char			*td_name;
};

/** data structure to help binary search of components */
struct pool_comp_sorter {
	/** type of component */
	pool_comp_type_t	  cs_type;
	/** number of components */
	unsigned int		  cs_nr;
	/** pointer array for binary search */
	struct pool_component	**cs_comps;
};

/** In memory data structure for pool map */
struct pool_map {
	/** protect the refcount */
	pthread_mutex_t		 po_lock;
	/** Current version of pool map */
	uint32_t		 po_version;
	/** refcount on the pool map */
	int			 po_ref;
	/** # domain layers */
	unsigned int		 po_domain_layers;
	/**
	 * Sorters for the binary search of different domain types.
	 * These sorters are in ascending order for binary search of sorters.
	 */
	struct pool_comp_sorter	*po_domain_sorters;
	/** sorter for binary search of target */
	struct pool_comp_sorter	 po_target_sorter;
	/**
	 * Tree root of all components.
	 * NB: All components must be stored in contiguous buffer.
	 */
	struct pool_domain	*po_tree;
	/**
	 * number of currently failed pool components of each type
	 * of component found in the pool
	 */
	struct pool_fail_comp	*po_comp_fail_cnts;

};

static struct pool_comp_state_dict comp_state_dict[] = {
	{
		.sd_state	= PO_COMP_ST_UP,
		.sd_name	= "UP",
	},
	{
		.sd_state	= PO_COMP_ST_UPIN,
		.sd_name	= "UP_IN",
	},
	{
		.sd_state	= PO_COMP_ST_DOWN,
		.sd_name	= "DOWN",
	},
	{
		.sd_state	= PO_COMP_ST_DOWNOUT,
		.sd_name	= "DOWN_OUT",
	},
	{
		.sd_state	= PO_COMP_ST_NEW,
		.sd_name	= "NEW",
	},
	{
		.sd_state	= PO_COMP_ST_UNKNOWN,
		.sd_name	= "UNKNOWN",
	},
};

#define comp_state_for_each(d)		\
	for (d = &comp_state_dict[0]; d->sd_state != PO_COMP_ST_UNKNOWN; d++)

static struct pool_comp_type_dict comp_type_dict[] = {
	{
		.td_type	= PO_COMP_TP_TARGET,
		.td_abbr	= 't',
		.td_name	= "target",
	},
	{
		.td_type	= PO_COMP_TP_NODE,
		.td_abbr	= 'n',
		.td_name	= "node",
	},
	{
		.td_type	= PO_COMP_TP_BOARD,
		.td_abbr	= 'b',
		.td_name	= "board",
	},
	{
		.td_type	= PO_COMP_TP_BLADE,
		.td_abbr	= 'l',
		.td_name	= "blade",
	},
	{
		.td_type	= PO_COMP_TP_RACK,
		.td_abbr	= 'r',
		.td_name	= "rack",
	},
	{
		.td_type	= PO_COMP_TP_ROOT,
		.td_abbr	= 'o',
		.td_name	= "root",
	},
	{
		.td_type	= PO_COMP_TP_UNKNOWN,
		.td_abbr	= 'u',
		.td_name	= "unknown",
	},
};

#define comp_type_for_each(d)		\
	for (d = &comp_type_dict[0]; d->td_type != PO_COMP_TP_UNKNOWN; d++)

/**
 * struct used to keep track of failed domain count
 * keeps track of each domain separately for lookup.
 */
struct pool_fail_comp {
	uint32_t fail_cnt;
	pool_comp_type_t comp_type;
};

static void pool_map_destroy(struct pool_map *map);
static bool pool_map_empty(struct pool_map *map);
static void pool_tree_count(struct pool_domain *tree,
			    struct pool_comp_cntr *cntr);

const char *
pool_comp_state2str(pool_comp_state_t state)
{
	struct pool_comp_state_dict *dict;

	comp_state_for_each(dict) {
		if (dict->sd_state == state)
			break;
	}
	return dict->sd_name;
}

pool_comp_state_t
pool_comp_str2state(const char *name)
{
	struct pool_comp_state_dict *dict;

	comp_state_for_each(dict) {
		if (strcasecmp(name, dict->sd_name) == 0)
			break;
	}
	return dict->sd_state;
}

const char *
pool_comp_type2str(pool_comp_type_t type)
{
	struct pool_comp_type_dict *dict;

	comp_type_for_each(dict) {
		if (dict->td_type == type)
			break;
	}
	return dict->td_name;
}

pool_comp_type_t
pool_comp_str2type(const char *name)
{
	struct pool_comp_type_dict *dict;

	comp_type_for_each(dict) {
		if (strcasecmp(name, dict->td_name) == 0)
			break;
	}
	return dict->td_type;
}

pool_comp_type_t
pool_comp_abbr2type(char abbr)
{
	struct pool_comp_type_dict *dict;

	abbr = tolower(abbr);
	comp_type_for_each(dict) {
		if (abbr == dict->td_abbr)
			break;
	}
	return dict->td_type;
}

static bool
target_exist(struct pool_map *map, uint32_t id)
{
	return pool_map_find_target(map, id, NULL) != 0;
}

static bool
domain_exist(struct pool_map *map, pool_comp_type_t type, uint32_t id)
{
	return pool_map_find_domain(map, type, id, NULL) != 0;
}

static void
comp_sort_op_swap(void *array, int a, int b)
{
	struct pool_component **comps = (struct pool_component **)array;
	struct pool_component  *tmp;

	tmp = comps[a];
	comps[a] = comps[b];
	comps[b] = tmp;
}

static int
comp_sort_op_cmp(void *array, int a, int b)
{
	struct pool_component **comps = (struct pool_component **)array;

	if (comps[a]->co_id > comps[b]->co_id)
		return 1;
	if (comps[a]->co_id < comps[b]->co_id)
		return -1;
	return 0;
}

static int
comp_sort_op_cmp_key(void *array, int i, uint64_t key)
{
	struct pool_component **comps = (struct pool_component **)array;
	uint32_t		id	= (uint32_t)key;

	if (comps[i]->co_id > id)
		return 1;
	if (comps[i]->co_id < id)
		return -1;
	return 0;
}

/** ID based sort and lookup for components */
static daos_sort_ops_t comp_sort_ops = {
	.so_swap	= comp_sort_op_swap,
	.so_cmp		= comp_sort_op_cmp,
	.so_cmp_key	= comp_sort_op_cmp_key,
};

static int
comp_sorter_init(struct pool_comp_sorter *sorter, int nr,
		 pool_comp_type_t type)
{
	D_DEBUG(DB_TRACE, "Initialize sorter for %s, nr %d\n",
		pool_comp_type2str(type), nr);

	D_ALLOC_ARRAY(sorter->cs_comps, nr);
	if (sorter->cs_comps == NULL)
		return -DER_NOMEM;

	sorter->cs_type	= type;
	sorter->cs_nr	= nr;
	return 0;
}

static void
comp_sorter_fini(struct pool_comp_sorter *sorter)
{
	if (sorter->cs_comps != NULL) {
		D_DEBUG(DB_TRACE, "Finalise sorter for %s\n",
			pool_comp_type2str(sorter->cs_type));

		D_FREE(sorter->cs_comps);
		sorter->cs_nr = 0;
	}
}

static struct pool_domain *
comp_sorter_find_domain(struct pool_comp_sorter *sorter, unsigned int id)
{
	int	at;

	D_ASSERT(sorter->cs_type < PO_COMP_TP_TARGET);
	at = daos_array_find(sorter->cs_comps, sorter->cs_nr, id,
			     &comp_sort_ops);
	return at < 0 ? NULL :
	       container_of(sorter->cs_comps[at], struct pool_domain, do_comp);
}

static struct pool_target *
comp_sorter_find_target(struct pool_comp_sorter *sorter, unsigned int id)
{
	int	at;

	D_ASSERT(sorter->cs_type == PO_COMP_TP_TARGET);
	at = daos_array_find(sorter->cs_comps, sorter->cs_nr, id,
			     &comp_sort_ops);
	return at < 0 ? NULL :
	       container_of(sorter->cs_comps[at], struct pool_target, ta_comp);
}

static int
comp_sorter_sort(struct pool_comp_sorter *sorter)
{
	return daos_array_sort(sorter->cs_comps, sorter->cs_nr, true,
			       &comp_sort_ops);
}

/** create a new pool buffer which can store \a nr components */
struct pool_buf *
pool_buf_alloc(unsigned int nr)
{
	struct pool_buf *buf;

	D_ALLOC(buf, pool_buf_size(nr));
	if (buf != NULL)
		buf->pb_nr = nr;

	return buf;
}

/** duplicate a new pool buffer, will internally allocate memory */
struct pool_buf *
pool_buf_dup(struct pool_buf *buf)
{
	struct pool_buf *buf_alloc;

	D_ASSERT(buf != NULL);

	buf_alloc = pool_buf_alloc(buf->pb_nr);
	if (buf_alloc == NULL)
		return NULL;

	memcpy(buf_alloc, buf, pool_buf_size(buf->pb_nr));

	return buf_alloc;
}

/** free the pool buffer */
void
pool_buf_free(struct pool_buf *buf)
{
	D_FREE(buf);
}

/**
 * Add an array of components to the pool buffer.
 * The caller should always attach domains before targets, and attach high
 * level domains before low level domains.
 *
 * TODO: add more description about pool map format.
 */
int
pool_buf_attach(struct pool_buf *buf, struct pool_component *comps,
		unsigned int comp_nr)
{
	unsigned int	nr = buf->pb_domain_nr + buf->pb_node_nr +
			     buf->pb_target_nr;

	if (buf->pb_nr < nr + comp_nr)
		return -DER_NOSPACE;

	D_DEBUG(DB_TRACE, "Attaching %d components\n", comp_nr);
	for (; comp_nr > 0; comp_nr--, comps++, nr++) {
		struct pool_component *prev;

		prev = nr == 0 ? NULL : &buf->pb_comps[nr - 1];
		if (prev != NULL && prev->co_type > comps[0].co_type)
			return -DER_INVAL;

		if (comps[0].co_type == PO_COMP_TP_TARGET)
			buf->pb_target_nr++;
		else if (comps[0].co_type == PO_COMP_TP_NODE)
			buf->pb_node_nr++;
		else if (comps[0].co_type == PO_COMP_TP_RACK)
			buf->pb_domain_nr++;
		else
			D_ASSERTF(0, "invalid type %d\n", comps[0].co_type);

		buf->pb_comps[nr] = comps[0];

		D_DEBUG(DB_TRACE, "nr %d %s\n", nr,
			pool_comp_type2str(comps[0].co_type));
	}
	return 0;
}

int
pool_buf_pack(struct pool_buf *buf)
{
	if (buf->pb_nr != buf->pb_target_nr + buf->pb_domain_nr +
			  buf->pb_node_nr)
		return -DER_INVAL;

	/* TODO: checksum, swab... */
	return 0;
}

int
pool_buf_unpack(struct pool_buf *buf)
{
	/* TODO: swab, verify checksum */
	return 0;
}

/**
 * Parse pool buffer and construct domain+target array (tree) based on
 * the information in pool buffer.
 *
 * \param buf		[IN]	pool buffer to be parsed
 * \param tree_pp	[OUT]	the returned domain+target tree.
 */
static int
pool_buf_parse(struct pool_buf *buf, struct pool_domain **tree_pp)
{
	struct pool_domain *tree;
	struct pool_domain *domain;
	struct pool_domain *parent;
	struct pool_target *targets;
	pool_comp_type_t    type;
	int		    size;
	int		    i;
	int		    rc = 0;

	if (buf->pb_target_nr == 0 || buf->pb_node_nr == 0 ||
	    buf->pb_domain_nr + buf->pb_node_nr + buf->pb_target_nr !=
								buf->pb_nr) {
		D_DEBUG(DB_MGMT, "Invalid number of components: %d/%d/%d/%d\n",
			buf->pb_nr, buf->pb_domain_nr, buf->pb_node_nr,
			buf->pb_target_nr);
		return -DER_INVAL;
	}

	size = sizeof(struct pool_domain) * (buf->pb_domain_nr + 1) + /* root */
	       sizeof(struct pool_domain) * (buf->pb_node_nr) +
	       sizeof(struct pool_target) * (buf->pb_target_nr);

	D_DEBUG(DB_TRACE, "domain %d node %d target %d\n", buf->pb_domain_nr,
		buf->pb_node_nr, buf->pb_target_nr);

	D_ALLOC(tree, size);
	if (tree == NULL)
		return -DER_NOMEM;

	targets	= (struct pool_target *)&tree[buf->pb_domain_nr +
					      buf->pb_node_nr + 1];
	for (i = 0; i < buf->pb_target_nr; i++)
		targets[i].ta_comp = buf->pb_comps[buf->pb_domain_nr +
						buf->pb_node_nr + i];

	/* Initialize the root */
	parent = &tree[0]; /* root */
	parent->do_comp.co_type   = PO_COMP_TP_ROOT;
	parent->do_comp.co_status = PO_COMP_ST_UPIN;
	if (buf->pb_domain_nr == 0) {
		/* nodes are directly attached under the root */
		parent->do_target_nr = buf->pb_node_nr;
		parent->do_child_nr = buf->pb_node_nr;
	} else {
		parent->do_child_nr = buf->pb_domain_nr;
	}
	parent->do_children = &tree[1];

	parent++;
	type = buf->pb_comps[0].co_type;

	for (i = 1;; i++) {
		struct pool_component *comp = &tree[i].do_comp;
		int		       nr = 0;

		*comp = buf->pb_comps[i - 1];
		if (comp->co_type > PO_COMP_TP_TARGET) {
			D_DEBUG(DB_MGMT, "Invalid type %d/%d\n",
				type, comp->co_type);
			rc = -DER_INVAL;
			goto out;
		}

		D_DEBUG(DB_TRACE, "Parse %s[%d] i %d nr %d\n",
			pool_comp_type2str(comp->co_type), comp->co_id,
			i, comp->co_nr);

		if (comp->co_type == type)
			continue;

		type = comp->co_type;

		for (; parent < &tree[i]; parent++) {
			if (type != PO_COMP_TP_TARGET) {
				D_DEBUG(DB_TRACE, "Setup children for %s[%d]"
					" child nr %d\n",
					pool_domain_name(parent),
					parent->do_comp.co_id,
					parent->do_child_nr);

				parent->do_children = &tree[i + nr];
				nr += parent->do_child_nr;
			} else {
				/* parent is the last level domain */
				D_DEBUG(DB_TRACE, "Setup targets for %s[%d]\n",
					pool_domain_name(parent),
					parent->do_comp.co_id);

				parent->do_target_nr  = parent->do_comp.co_nr;
				parent->do_comp.co_nr = 0;
				parent->do_targets    = targets;
				targets += parent->do_target_nr;

				D_DEBUG(DB_TRACE, "%s[%d] has %d targets\n",
					pool_domain_name(parent),
					parent->do_comp.co_id,
					parent->do_target_nr);
			}
		}

		if (type == PO_COMP_TP_TARGET)
			break;
	}

	D_DEBUG(DB_TRACE, "Build children and targets pointers\n");

	for (domain = &tree[0]; domain->do_targets == NULL;
	     domain = &tree[0]) {
		while (domain->do_targets == NULL) {
			parent = domain;
			D_ASSERTF(domain->do_children != NULL,
				  "%s[%d]: %d/%d\n",
				  pool_domain_name(domain),
				  domain->do_comp.co_id,
				  domain->do_child_nr, domain->do_target_nr);
			domain = &domain->do_children[0];
		}

		type = parent->do_comp.co_type;
		for (; parent->do_comp.co_type == type; parent++) {
			parent->do_targets = domain->do_targets;
			for (i = 0; i < parent->do_child_nr; i++, domain++)
				parent->do_target_nr += domain->do_target_nr;

			D_DEBUG(DB_TRACE, "Set %d target for %s[%d]\n",
				parent->do_target_nr,
				pool_comp_type2str(parent->do_comp.co_type),
				parent->do_comp.co_id);
		}
	}

out:
	if (rc)
		D_FREE(tree);

	*tree_pp = &tree[0];
	return rc;
}

/**
 * Extract pool buffer from a pool map.
 *
 * \param map		[IN]	The pool map to extract from.
 * \param buf_pp	[OUT]	The returned pool buffer, should be freed
 *				by pool_buf_free.
 */
int
pool_buf_extract(struct pool_map *map, struct pool_buf **buf_pp)
{
	struct pool_buf		*buf;
	struct pool_domain	*tree;
	struct pool_comp_cntr	 cntr;
	unsigned int		 dom_nr;
	int			 i;
	int			 rc;

	D_ASSERT(map->po_tree != NULL);
	tree = &map->po_tree[1]; /* skip the root */
	pool_tree_count(tree, &cntr);

	if (cntr.cc_domains + cntr.cc_targets == 0) {
		D_DEBUG(DB_MGMT, "Empty pool map.\n");
		return -DER_NONEXIST;
	}

	buf = pool_buf_alloc(cntr.cc_domains + cntr.cc_targets);
	if (buf == NULL)
		return -DER_NOMEM;

	for (dom_nr = cntr.cc_top_doms; dom_nr != 0;
	     tree = tree[0].do_children) {
		int     child_nr;

		for (i = child_nr = 0; i < dom_nr; i++) {
			struct pool_component	comp;

			comp = tree[i].do_comp;
			if (tree[i].do_children != NULL) {
				/* intermediate domain */
				child_nr += tree[i].do_child_nr;
			} else {
				/* the last level domain */
				comp.co_nr = tree[i].do_target_nr;
			}
			pool_buf_attach(buf, &comp, 1);
		}
		dom_nr = child_nr;
	}

	tree = &map->po_tree[0];
	for (i = 0; i < cntr.cc_targets; i++)
		pool_buf_attach(buf, &tree->do_targets[i].ta_comp, 1);

	if (buf->pb_nr != buf->pb_target_nr + buf->pb_domain_nr +
			  buf->pb_node_nr) {
		D_DEBUG(DB_MGMT, "Invalid pool map format.\n");
		D_GOTO(failed, rc = -DER_INVAL);
	}

	*buf_pp = buf;
	return 0;
 failed:
	pool_buf_free(buf);
	return rc;
}

/**
 * Count number of domains, targets, and layers of domains etc in the
 * component tree.
 */
static void
pool_tree_count(struct pool_domain *tree, struct pool_comp_cntr *cntr)
{
	unsigned int	dom_nr;

	if (tree[0].do_children != NULL) {
		dom_nr = tree[0].do_children - tree;
	} else {
		D_ASSERT(tree[0].do_targets != NULL);
		dom_nr = (struct pool_domain *)tree[0].do_targets - tree;
	}

	cntr->cc_top_doms = dom_nr;
	cntr->cc_domains  = dom_nr;
	cntr->cc_targets  = 0;
	cntr->cc_layers   = 0;

	for (; tree != NULL; tree = tree[0].do_children, cntr->cc_layers++) {
		int      child_nr;
		int      i;

		D_DEBUG(DB_TRACE, "%s, nr = %d\n", pool_domain_name(&tree[0]),
			dom_nr);
		for (i = child_nr = 0; i < dom_nr; i++) {
			if (tree[i].do_children != NULL) {
				cntr->cc_domains += tree[i].do_child_nr;
				child_nr += tree[i].do_child_nr;
			} else {
				cntr->cc_targets += tree[i].do_target_nr;
			}
		}
		dom_nr = child_nr;
	}
}

int
pool_map_comp_cnt(struct pool_map *map)
{
	struct pool_comp_cntr cntr = {0};

	D_ASSERT(map->po_tree != NULL);
	pool_tree_count(&map->po_tree[1], &cntr);

	return cntr.cc_domains + cntr.cc_targets;
}

/**
 * Calculate memory size of the component tree.
 */
static unsigned int
pool_tree_size(struct pool_domain *tree)
{
	struct pool_comp_cntr cntr;

	pool_tree_count(tree, &cntr);
	return sizeof(struct pool_target) * cntr.cc_targets +
	       sizeof(struct pool_domain) * cntr.cc_domains;
}

/**
 * Rebuild pointers for the component tree
 */
static void
pool_tree_build_ptrs(struct pool_domain *tree, struct pool_comp_cntr *cntr)
{
	struct pool_target *targets;
	int		    dom_nr;

	D_DEBUG(DB_TRACE, "Layers %d, top domains %d, domains %d, targets %d\n",
		cntr->cc_layers, cntr->cc_top_doms, cntr->cc_domains,
		cntr->cc_targets);

	targets = (struct pool_target *)&tree[cntr->cc_domains];

	for (dom_nr = cntr->cc_top_doms; tree != NULL;
	     tree = tree[0].do_children) {
		struct pool_domain *children = &tree[dom_nr];
		struct pool_target *tgs	     = targets;
		int		    child_nr = 0;
		int		    i;

		for (i = 0; i < dom_nr; i++) {
			if (tree[i].do_children != NULL) {
				tree[i].do_children = children;
				child_nr += tree[i].do_child_nr;
				children += tree[i].do_child_nr;
			}
			tree[i].do_targets = tgs;
			tgs += tree[i].do_target_nr;
		}
		dom_nr = child_nr;
	}
}

/** Free the component tree */
static void
pool_tree_free(struct pool_domain *tree)
{
	D_FREE(tree);
}

/** Check if component buffer is sane */
static bool
pool_tree_sane(struct pool_domain *tree, uint32_t version)
{
	struct pool_domain	*parent = NULL;
	struct pool_target	*targets = tree[0].do_targets;
	struct pool_comp_cntr	 cntr;
	int			 dom_nr;
	int			 i;

	D_DEBUG(DB_TRACE, "Sanity check of component buffer\n");
	pool_tree_count(tree, &cntr);
	if (cntr.cc_targets == 0) {
		D_DEBUG(DB_MGMT, "Buffer has no target\n");
		return false;
	}

	for (dom_nr = cntr.cc_top_doms; tree != NULL;
	     tree = tree[0].do_children) {
		struct pool_domain *prev = &tree[0];
		int		    child_nr = 0;

		if (parent != NULL &&
		    parent->do_comp.co_type >= tree[0].do_comp.co_type) {
			D_DEBUG(DB_MGMT,
				"Type of parent domain %d(%s) should be "
				"smaller than child domain %d(%s)\n",
				parent->do_comp.co_type,
				pool_domain_name(parent),
				tree[0].do_comp.co_type,
				pool_domain_name(&tree[0]));
			return false;
		}

		for (i = 0; i < dom_nr; i++) {
			if (tree[i].do_comp.co_ver > version) {
				D_DEBUG(DB_MGMT, "Invalid version %u/%u\n",
					tree[i].do_comp.co_ver, version);
				return false;
			}

			if (prev->do_comp.co_type != tree[i].do_comp.co_type) {
				D_DEBUG(DB_MGMT,
					"Unmatched domain type %d/%d\n",
					tree[i].do_comp.co_type,
					prev->do_comp.co_type);
				return false;
			}

			if (tree[i].do_targets == NULL ||
			    tree[i].do_target_nr == 0) {
				D_DEBUG(DB_MGMT, "No target found\n");
				return false; /* always has targets */
			}

			if ((prev->do_children == NULL) ^
			    (tree[i].do_children == NULL)) {
				D_DEBUG(DB_MGMT, "Invalid child tree\n");
				return false;
			}

			if ((prev->do_targets == NULL) ^
			    (tree[i].do_targets == NULL)) {
				D_DEBUG(DB_MGMT, "Invalid target tree\n");
				return false;
			}

			if (prev != &tree[i] &&
			    prev->do_children != NULL &&
			    prev->do_children + prev->do_child_nr !=
			    tree[i].do_children) {
				D_DEBUG(DB_MGMT, "Invalid children pointer\n");
				return false;
			}

			if (prev != &tree[i] &&
			    prev->do_targets != NULL &&
			    prev->do_targets + prev->do_target_nr !=
			    tree[i].do_targets) {
				D_DEBUG(DB_MGMT, "Invalid children pointer i"
					" %d target nr %d\n", i,
					prev->do_target_nr);
				return false;
			}

			if (tree[i].do_child_nr != 0)
				child_nr += tree[i].do_child_nr;

			prev = &tree[i];
		}
		parent = &tree[0];
		dom_nr = child_nr;
	}

	for (i = 0; i < cntr.cc_targets; i++) {
		if (targets[i].ta_comp.co_type != PO_COMP_TP_TARGET) {
			D_DEBUG(DB_MGMT, "Invalid leaf type %d(%s) i %d\n",
				targets[i].ta_comp.co_type,
				pool_comp_name(&targets[i].ta_comp), i);
			return false;
		}

		if (targets[i].ta_comp.co_ver > version) {
			D_DEBUG(DB_MGMT, "Invalid version %u/%u i %d\n",
				targets[i].ta_comp.co_ver, version, i);
			return false;
		}
	}
	D_DEBUG(DB_TRACE, "Component buffer is sane\n");
	return true;
}

/** copy a components tree */
static void
pool_tree_copy(struct pool_domain *dst, struct pool_domain *src)
{
	struct pool_comp_cntr	cntr;

	memcpy(dst, src, pool_tree_size(src));
	pool_tree_count(src, &cntr);
	pool_tree_build_ptrs(dst, &cntr);
}

/** free data members of a pool map */
static void
pool_map_finalise(struct pool_map *map)
{
	int	i;

	D_DEBUG(DB_TRACE, "Release buffers for pool map\n");

	comp_sorter_fini(&map->po_target_sorter);

	if (map->po_comp_fail_cnts != NULL)
		D_FREE(map->po_comp_fail_cnts);

	if (map->po_domain_sorters != NULL) {
		D_ASSERT(map->po_domain_layers != 0);
		for (i = 0; i < map->po_domain_layers; i++)
			comp_sorter_fini(&map->po_domain_sorters[i]);

		D_FREE(map->po_domain_sorters);

		map->po_domain_sorters = NULL;
		map->po_domain_layers = 0;
	}

	if (map->po_tree != NULL) {
		pool_tree_free(map->po_tree);
		map->po_tree = NULL;
	}
	D_MUTEX_DESTROY(&map->po_lock);
}

/**
 * Install a component tree to a pool map.
 *
 * \param map		[IN]	The pool map to be initialized.
 * \param tree		[IN]	Component tree for the pool map.
 */
static int
pool_map_initialise(struct pool_map *map, struct pool_domain *tree)
{
	struct pool_comp_cntr	 cntr;
	int			 i;
	int			 rc = 0;

	D_ASSERT(pool_map_empty(map));
	rc = D_MUTEX_INIT(&map->po_lock, NULL);
	if (rc != 0)
		return rc;

	if (tree[0].do_comp.co_type != PO_COMP_TP_ROOT) {
		D_DEBUG(DB_TRACE, "Invalid tree format: %s/%d\n",
			pool_domain_name(&tree[0]), tree[0].do_comp.co_type);
		rc = -DER_INVAL;
		goto failed;
	}

	map->po_tree = tree;
	pool_tree_count(tree, &cntr);

	/* po_map_print(map); */
	D_DEBUG(DB_TRACE, "Setup nlayers %d, ndomains %d, ntargets %d\n",
		cntr.cc_layers, cntr.cc_domains, cntr.cc_targets);

	map->po_domain_layers = cntr.cc_layers;

	D_ALLOC_ARRAY(map->po_comp_fail_cnts, map->po_domain_layers);
	if (map->po_comp_fail_cnts == NULL) {
		rc = -DER_NOMEM;
		goto failed;
	}

	D_ALLOC_ARRAY(map->po_domain_sorters, map->po_domain_layers);
	if (map->po_domain_sorters == NULL) {
		rc = -DER_NOMEM;
		goto failed;
	}

	/* pointer arrays for binary search of domains */
	for (i = 0; i < map->po_domain_layers; i++) {
		struct pool_comp_sorter	*sorter = &map->po_domain_sorters[i];
		unsigned int		 j;

		D_ASSERT(tree[0].do_comp.co_type != PO_COMP_TP_TARGET);
		pool_tree_count(tree, &cntr);
		rc = comp_sorter_init(sorter, cntr.cc_top_doms,
				      tree[0].do_comp.co_type);
		if (rc != 0)
			goto failed;

		D_DEBUG(DB_TRACE, "domain %s, ndomains %d\n",
			pool_domain_name(&tree[0]), sorter->cs_nr);

		for (j = 0; j < sorter->cs_nr; j++)
			sorter->cs_comps[j] = &tree[j].do_comp;

		rc = comp_sorter_sort(sorter);
		if (rc != 0)
			goto failed;

		tree = &tree[sorter->cs_nr];
	}

	rc = comp_sorter_init(&map->po_target_sorter, cntr.cc_targets,
			      PO_COMP_TP_TARGET);
	if (rc != 0)
		goto failed;

	for (i = 0; i < cntr.cc_targets; i++) {
		struct pool_target *ta;

		ta = &map->po_tree->do_targets[i];
		map->po_target_sorter.cs_comps[i] = &ta->ta_comp;
	}

	rc = comp_sorter_sort(&map->po_target_sorter);
	if (rc != 0)
		goto failed;

	return 0;
 failed:
	D_DEBUG(DB_MGMT, "Failed to setup pool map "DF_RC"\n", DP_RC(rc));
	D_MUTEX_DESTROY(&map->po_lock);
	pool_map_finalise(map);
	return rc;
}

/**
 * Check if a component tree is compatible with a pool map, it returns 0
 * if components in \a tree can be merged into \a map, otherwise returns
 * error code.
 */
static int
pool_map_compat(struct pool_map *map, uint32_t version,
		struct pool_domain *tree)
{
	struct pool_domain	*parent;
	struct pool_domain	*doms;
	int			 dom_nr;
	int			 rc;

	if (pool_map_empty(map)) {
		D_DEBUG(DB_MGMT, "empty map, type of buffer root is %s\n",
			pool_domain_name(&tree[0]));
		return 0;
	}

	if (map->po_version >= version)
		return -DER_NO_PERM;

	/* pool_buf_parse should always generate root */
	if (tree[0].do_comp.co_type != PO_COMP_TP_ROOT)
		return -DER_INVAL;

	rc = pool_map_find_domain(map, tree[1].do_comp.co_type,
				  PO_COMP_ID_ALL, &doms);
	if (rc == 0)
		return -DER_INVAL;

	if (doms - map->po_tree == 1) {
		/* the first component is indeed under the root */
		parent = &tree[0];
	} else {
		/* root of the new tree is dummy */
		parent = NULL;
	}

	D_DEBUG(DB_TRACE, "Check if buffer is compatible with pool map\n");

	dom_nr = tree[0].do_child_nr;
	for (tree++; tree != NULL; parent = &tree[0],
				   tree = tree[0].do_children,
				   doms = doms[0].do_children) {
		int     child_nr = 0;
		int	nr = 0;
		int     i;
		int	j;

		if (doms == NULL) {
			D_DEBUG(DB_MGMT, "tree has more layers than the map\n");
			return -DER_INVAL;
		}

		D_DEBUG(DB_TRACE, "checking %s/%s\n",
			pool_domain_name(&tree[0]), pool_domain_name(&doms[0]));

		for (i = 0; i < dom_nr; i++) {
			struct pool_component *dc = &tree[i].do_comp;
			bool		       existed;

			if (dc->co_type != doms[0].do_comp.co_type) {
				D_DEBUG(DB_MGMT,
					"domain type not match %s(%u) %s(%u)\n",
					pool_comp_name(dc), dc->co_type,
					pool_domain_name(&doms[0]),
					doms[0].do_comp.co_type);
				return -DER_INVAL;
			}

			existed = domain_exist(map, dc->co_type, dc->co_id);
			if (dc->co_status == PO_COMP_ST_NEW) {
				if (parent == NULL)
					return -DER_INVAL;
				if (existed)
					return -DER_NO_PERM;

			} else if (dc->co_status == PO_COMP_ST_UPIN) {
				if (!existed)
					return -DER_INVAL;

				D_ASSERT(parent != NULL);
				if (parent->do_comp.co_status == PO_COMP_ST_NEW)
					return -DER_INVAL;

			} else {
				return -DER_INVAL;
			}

			if (tree[i].do_children != NULL) {
				child_nr += tree[i].do_child_nr;
			} else {
				/* the last layer domain */
				if (doms[0].do_children != NULL) {
					D_DEBUG(DB_MGMT, "unmatched tree\n");
					return -DER_INVAL;
				}

				for (j = 0; j < tree[i].do_target_nr; j++) {
					struct pool_component *tc;

					tc = &tree[i].do_targets[j].ta_comp;
					if (tc->co_status != PO_COMP_ST_NEW ||
					    target_exist(map, tc->co_id))
						return -DER_INVAL;
				}
			}

			nr++;
			D_ASSERT(parent != NULL);
			if (parent->do_child_nr == nr) {
				parent++;
				nr = 0;
			}
		}
		dom_nr = child_nr;
	}
	return 0;
}

/**
 * Merge all new components from \a tree into \a map.
 * Already existent components will be ignored.
 */
static int
pool_map_merge(struct pool_map *map, uint32_t version,
	       struct pool_domain *tree)
{
	struct pool_map		*src_map;
	struct pool_domain	*dst_tree;
	struct pool_domain	*dst_doms;
	struct pool_domain	*cur_doms;
	void			*addr;
	struct pool_comp_cntr    cntr;
	unsigned int		 dom_nr;
	unsigned int		 size;
	int			 i;
	int			 rc;

	/* create scratch map for merging */
	D_ALLOC_PTR(src_map);
	if (src_map == NULL)
		return -DER_NOMEM;

	rc = pool_map_initialise(src_map, tree);
	if (rc != 0) {
		D_DEBUG(DB_MGMT, "Failed to create scratch map for buffer\n");
		goto failed;
	}

	/* destination buffer could has larger than the actually needed space,
	 * but it is not big deal.
	 */
	size = pool_tree_size(map->po_tree) + pool_tree_size(tree);
	D_ALLOC(dst_tree, size);
	if (dst_tree == NULL) {
		rc = -DER_NOMEM;
		goto failed;
	}

	/* copy current pool map to destination buffer */
	pool_tree_copy(dst_tree, map->po_tree);

	if (src_map->po_domain_layers != map->po_domain_layers) {
		/* source map may have less levels because it could be in
		 * a subtree, skip the fake root in this case.
		 */
		D_ASSERT(src_map->po_domain_layers < map->po_domain_layers);
		rc = pool_map_find_domain(map, tree[1].do_comp.co_type,
					  PO_COMP_ID_ALL, &cur_doms);
	} else {
		rc = pool_map_find_domain(map, tree[0].do_comp.co_type,
					  PO_COMP_ID_ALL, &cur_doms);
	}
	if (rc == 0)
		goto failed;

	dst_doms = dst_tree;
	dst_doms += cur_doms - map->po_tree;
	pool_tree_count(dst_doms, &cntr);
	dom_nr = cntr.cc_top_doms;

	/* overwrite the components after the top layer domains */
	addr = (void *)&dst_doms[dom_nr];
	pool_tree_count(dst_tree, &cntr);

	/* complex buffer manipulating... */
	for (; dst_doms != NULL;
	       dst_doms = dst_doms[0].do_children,
	       cur_doms = cur_doms[0].do_children) {
		struct pool_domain *cdom = &cur_doms[0];
		int		    child_nr = 0;

		for (i = 0; i < dom_nr; i++) {
			struct pool_domain *ddom = &dst_doms[i];
			struct pool_domain *sdom;
			int		    nb;
			int		    j;

			if (ddom->do_comp.co_ver == version) {
				ddom->do_children  = NULL;
				ddom->do_targets   = NULL;
				ddom->do_child_nr  = 0;
				ddom->do_target_nr = 0;
				D_DEBUG(DB_TRACE, "Add new domain %s %d\n",
					pool_domain_name(cdom), dom_nr);
			} else {
				/* Domain existed, copy its children/targets
				 * from current pool map.
				 */
				D_ASSERT(ddom->do_comp.co_ver < version);
				D_ASSERT(ddom->do_comp.co_id ==
					 cdom->do_comp.co_id);

				if (cdom->do_children != NULL) {
					ddom->do_children = addr;
					ddom->do_child_nr = cdom->do_child_nr;
					nb = cdom->do_child_nr *
					     sizeof(struct pool_domain);
					memcpy(addr, cdom->do_children, nb);
				} else {
					ddom->do_targets = addr;
					ddom->do_target_nr = cdom->do_target_nr;
					nb = cdom->do_target_nr *
					     sizeof(struct pool_target);
					memcpy(addr, cdom->do_targets, nb);
				}
				addr += nb;
				cdom++;
			}

			D_DEBUG(DB_TRACE, "Check changes for %s[%d]\n",
				pool_domain_name(ddom), ddom->do_comp.co_id);

			rc = pool_map_find_domain(src_map,
						  ddom->do_comp.co_type,
						  ddom->do_comp.co_id, &sdom);
			if (rc == 0) {
				child_nr += ddom->do_child_nr;
				continue; /* no change for this domain */
			}

			/* new buffer may have changes for this domain */
			if (sdom->do_children != NULL) {
				struct pool_domain *child = addr;

				D_DEBUG(DB_TRACE, "Scan children of %s[%d]\n",
					pool_domain_name(ddom),
					ddom->do_comp.co_id);

				if (ddom->do_children == NULL)
					ddom->do_children = child;

				/* copy new child domains to dest buffer */
				for (j = 0; j < sdom->do_child_nr; j++) {
					struct pool_component *dc;

					dc = &sdom->do_children[j].do_comp;
					/* ignore existent children */
					if (dc->co_status != PO_COMP_ST_NEW)
						continue;

					D_DEBUG(DB_TRACE, "New %s[%d]\n",
						pool_comp_type2str(dc->co_type),
						dc->co_id);

					*child = sdom->do_children[j];
					child++;

					ddom->do_child_nr++;
					cntr.cc_domains++;
				}
				addr = child;
			} else {
				struct pool_target *target = addr;

				D_DEBUG(DB_TRACE, "Scan targets of %s[%d]\n",
					pool_domain_name(ddom),
					ddom->do_comp.co_id);

				if (ddom->do_targets == NULL)
					ddom->do_targets = target;

				/* copy new targets to destination buffer */
				for (j = 0; j < sdom->do_target_nr; j++) {
					struct pool_component *tc;

					tc = &sdom->do_targets[j].ta_comp;

					if (tc->co_status != PO_COMP_ST_NEW)
						continue;

					D_DEBUG(DB_TRACE, "New target[%d]\n",
						tc->co_id);

					*target = sdom->do_targets[j];
					target++;

					ddom->do_target_nr++;
					cntr.cc_targets++;
				}
				addr = target;
			}
			child_nr += ddom->do_child_nr;
		}
		dom_nr = child_nr;
	}
	D_ASSERT(addr - (void *)dst_tree <= size);
	D_DEBUG(DB_TRACE, "Merged all components\n");
	/* At this point, I only have valid children pointers for the last
	 * layer domains, and need to build target pointers for all layers.
	 */
	pool_tree_build_ptrs(dst_tree, &cntr);

	/* release old buffers of pool map */
	pool_map_finalise(map);

	/* install new buffer for pool map */
	rc = pool_map_initialise(map, dst_tree);
	D_ASSERT(rc == 0 || rc == -DER_NOMEM);

	map->po_version = version;
 failed:
	pool_map_destroy(src_map);
	return rc;
}

static int
uuid_compare_cb(const void *a, const void *b)
{
	uuid_t *ua = (uuid_t *)a;
	uuid_t *ub = (uuid_t *)b;

	return uuid_compare(*ua, *ub);
}

int
gen_pool_buf(struct pool_map *map, struct pool_buf **map_buf_out,
		int map_version, int ndomains, int nnodes, int ntargets,
		const int32_t *domains, uuid_t target_uuids[],
		const d_rank_list_t *target_addrs, uuid_t **uuids_out,
		uint32_t dss_tgt_nr)
{
	struct pool_component	map_comp;
	struct pool_buf		*map_buf;
	struct pool_domain      *found_dom;
	uuid_t		        *uuids = NULL;
	uint32_t		num_comps;
	uint8_t			new_status;
	bool			updated;
	int i, rc;

	updated = false;

	/* Prepare the pool map attribute buffers. */
	map_buf = pool_buf_alloc(ndomains + nnodes + ntargets);
	if (map_buf == NULL)
		D_GOTO(out_map_buf, rc = -DER_NOMEM);

	/* Make a sorted target UUID array to determine target IDs. */
	D_ALLOC_ARRAY(uuids, nnodes);
	if (uuids == NULL)
		D_GOTO(out_map_buf, rc = -DER_NOMEM);
	memcpy(uuids, target_uuids, sizeof(uuid_t) * nnodes);
	qsort(uuids, nnodes, sizeof(uuid_t), uuid_compare_cb);

	if (map != NULL) {
		new_status = PO_COMP_ST_NEW;
		num_comps = pool_map_find_domain(map, PO_COMP_TP_RACK,
						 PO_COMP_ID_ALL, NULL);
	} else {
		new_status = PO_COMP_ST_UPIN;
		num_comps = 0;
	}
	/* fill racks */
	for (i = 0; i < ndomains; i++) {
		map_comp.co_type = PO_COMP_TP_RACK;	/* TODO */
		map_comp.co_status = new_status;
		map_comp.co_index = i + num_comps;
		map_comp.co_id = i + num_comps;
		map_comp.co_rank = 0;
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = domains[i];

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0)
			D_GOTO(out_map_buf, rc);
	}

	if (map != NULL)
		num_comps = pool_map_find_domain(map, PO_COMP_TP_NODE,
						 PO_COMP_ID_ALL, NULL);
	else
		num_comps = 0;

	/* fill nodes */
	for (i = 0; i < nnodes; i++) {
		uuid_t *p = bsearch(target_uuids[i], uuids, nnodes,
				    sizeof(uuid_t), uuid_compare_cb);

		if (map) {
			found_dom = pool_map_find_node_by_rank(map,
					target_addrs->rl_ranks[i]);
			if (found_dom)
				continue;
		}

		updated = true;
		map_comp.co_type = PO_COMP_TP_NODE;
		map_comp.co_status = new_status;
		map_comp.co_index = i + num_comps;
		map_comp.co_id = (p - uuids) + num_comps;
		map_comp.co_rank = target_addrs->rl_ranks[i];
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = dss_tgt_nr;

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0)
			D_GOTO(out_map_buf, rc);
	}

	if (!updated)
		D_GOTO(out_map_buf, rc = -DER_ALREADY);

	if (map != NULL)
		num_comps = pool_map_find_target(map, PO_COMP_ID_ALL, NULL);
	else
		num_comps = 0;

	/* fill targets */
	for (i = 0; i < nnodes; i++) {
		int j;

		for (j = 0; j < dss_tgt_nr; j++) {
			map_comp.co_type = PO_COMP_TP_TARGET;
			map_comp.co_status = new_status;
			map_comp.co_index = j;
			map_comp.co_id = (i * dss_tgt_nr + j) + num_comps;
			map_comp.co_rank = target_addrs->rl_ranks[i];
			map_comp.co_ver = map_version;
			map_comp.co_fseq = 1;
			map_comp.co_nr = 1;

			rc = pool_buf_attach(map_buf, &map_comp, 1);
			if (rc != 0)
				D_GOTO(out_map_buf, rc);
		}
	}
	if (uuids_out)
		*uuids_out = uuids;
	else
		D_FREE(uuids);

	*map_buf_out = map_buf;
	return 0;

out_map_buf:
	pool_buf_free(map_buf);
	D_FREE(uuids);
	return rc;
}


int
pool_map_extend(struct pool_map *map, uint32_t version, struct pool_buf *buf)
{
	struct pool_domain *tree = NULL;
	int		    rc;

	rc = pool_buf_parse(buf, &tree);
	if (rc != 0)
		return rc;

	if (!pool_tree_sane(tree, version)) {
		D_DEBUG(DB_MGMT, "Insane buffer format\n");
		rc = -DER_INVAL;
		goto error_tree;
	}

	rc = pool_map_compat(map, version, tree);
	if (rc != 0) {
		D_DEBUG(DB_MGMT, "Buffer is incompatible with pool map\n");
		goto error_tree;
	}

	D_DEBUG(DB_TRACE, "Merge buffer with already existent pool map\n");
	rc = pool_map_merge(map, version, tree);
	if(rc != 0)
		goto error_tree;

	return rc;

error_tree:
	pool_tree_free(tree);
	return rc;
}

/**
 * Create a pool map from components stored in \a buf.
 *
 * \param buf		[IN]	The buffer to input pool components.
 * \param version	[IN]	Version for the new created pool map.
 * \param mapp		[OUT]	The returned pool map.
 */
int
pool_map_create(struct pool_buf *buf, uint32_t version, struct pool_map **mapp)
{
	struct pool_domain *tree = NULL;
	struct pool_map	   *map = NULL;
	int		    rc;

	rc = pool_buf_parse(buf, &tree);
	if (rc != 0) {
		D_ERROR("pool_buf_parse failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (!pool_tree_sane(tree, version)) {
		rc = -DER_INVAL;
		D_ERROR("pool_tree_sane failed, rc "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	D_ALLOC_PTR(map);
	if (map == NULL) {
		rc = -DER_NOMEM;
		goto failed;
	}

	rc = pool_map_initialise(map, tree);
	if (rc != 0) {
		D_ERROR("pool_map_initialise failed, rc "DF_RC"\n", DP_RC(rc));
		/* pool_tree_free() did in pool_map_initialise */
		tree = NULL;
		goto failed;
	}

	/** Record the initial failed domain counts */
	rc = pool_map_update_failed_cnt(map);
	if (rc != 0) {
		D_ERROR("could not update number of failed targets, rc %d.\n",
				rc);
		goto failed;
	}

	map->po_version = version;
	map->po_ref = 1; /* 1 for caller */
	*mapp = map;
	return 0;
 failed:
	if (tree != NULL)
		pool_tree_free(tree);
	if (map != NULL)
		D_FREE(map);
	return rc;
}

/**
 * Destroy a pool map.
 */
static void
pool_map_destroy(struct pool_map *map)
{
	pool_map_finalise(map);
	D_FREE(map);
}

/** Take a refcount on a pool map */
void
pool_map_addref(struct pool_map *map)
{
	D_MUTEX_LOCK(&map->po_lock);
	map->po_ref++;
	D_MUTEX_UNLOCK(&map->po_lock);
}

/**
 * Release refcount on a pool map, this pool map will be destroyed if it
 * is the last refcount
 */
void
pool_map_decref(struct pool_map *map)
{
	bool free;

	D_MUTEX_LOCK(&map->po_lock);
	D_ASSERT(map->po_ref > 0);
	map->po_ref--;
	free = (map->po_ref == 0);
	D_MUTEX_UNLOCK(&map->po_lock);

	if (free)
		pool_map_destroy(map);
}

/**
 * Find a domain whose type equals to \a type and id equals to \a id.
 * If id is PO_COMP_ID_ALL, it returns the first element of the contiguously
 * stored domain array to \a domain_pp.
 *
 * The return value of this function is the number of domains, so it is zero
 * on failure, and it is always one if a particular id is found.
 */
int
pool_map_find_domain(struct pool_map *map, pool_comp_type_t type, uint32_t id,
		     struct pool_domain **domain_pp)
{
	struct pool_comp_sorter *sorter;
	struct pool_domain	*tmp;
	int			 i;

	if (pool_map_empty(map)) {
		D_ERROR("Uninitialized pool map\n");
		return 0;
	}

	D_ASSERT(map->po_domain_layers > 0);
	/* all other domains under root are stored in contiguous buffer */
	for (tmp = map->po_tree, i = 0; tmp != NULL;
	     tmp = tmp->do_children, i++) {
		if (tmp[0].do_comp.co_type == type)
			break;
	}

	D_ASSERT(i <= map->po_domain_layers);
	if (i == map->po_domain_layers) {
		D_DEBUG(DB_MGMT, "Can't find domain type %s(%d)\n",
			pool_comp_type2str(type), type);
		return 0;
	}

	sorter = &map->po_domain_sorters[i];
	D_ASSERT(sorter->cs_type == type);

	if (id == PO_COMP_ID_ALL) {
		if (domain_pp != NULL)
			*domain_pp = tmp;
		return sorter->cs_nr;
	}

	tmp = comp_sorter_find_domain(sorter, id);
	if (tmp == NULL)
		return 0;

	if (domain_pp != NULL)
		*domain_pp = tmp;
	return 1;
}

/**
 * Find all nodes in the pool map.
 *
 * \param map	[IN]	pool map to search.
 * \param id	[IN]	id to search.
 * \param domain_pp [OUT] returned node domain address.
 *
 * \return		number of the node domains.
 *                      0 if none.
 */
int
pool_map_find_nodes(struct pool_map *map, uint32_t id,
		    struct pool_domain **domain_pp)
{
	return pool_map_find_domain(map, PO_COMP_TP_NODE, id,
				    domain_pp);
}

/**
 * Find a target whose id equals to \a id by the binary search.
 * If id is PO_COMP_ID_ALL, it returns the contiguously stored target array
 * to \a target_pp.
 *
 * The return value of this function is the number of targets, so it is zero
 * on failure, and it is always one if a particular id is found.
 *
 * \param map	[IN]		The pool map to search
 * \param id	[IN]		Target ID to search
 * \param target_pp [OUT]	Returned target address
 */
int
pool_map_find_target(struct pool_map *map, uint32_t id,
		     struct pool_target **target_pp)
{
	struct pool_comp_sorter *sorter = &map->po_target_sorter;
	struct pool_target	*target;

	if (pool_map_empty(map)) {
		D_ERROR("Uninitialized pool map\n");
		return 0;
	}

	if (id == PO_COMP_ID_ALL) {
		if (target_pp != NULL)
			*target_pp = map->po_tree[0].do_targets;
		return map->po_tree[0].do_target_nr;
	}

	target = comp_sorter_find_target(sorter, id);
	if (target == NULL)
		return 0;

	if (target_pp != NULL)
		*target_pp = target;
	return 1;
}

/**
 * Find pool domain node by rank in the pool map.
 * \params [IN] map	pool map to find the node by rank.
 * \params [IN] rank	rank to use to search the pool domain.
 *
 * \return              domain found by rank.
 */
struct pool_domain *
pool_map_find_node_by_rank(struct pool_map *map, uint32_t rank)
{
	struct pool_domain	*doms;
	struct pool_domain	*found = NULL;
	int			doms_cnt;
	int			i;

	doms_cnt = pool_map_find_nodes(map, PO_COMP_ID_ALL, &doms);
	if (doms_cnt <= 0)
		return NULL;

	for (i = 0; i < doms_cnt; i++) {
		/* FIXME add rank sorter to the pool map */
		if (doms[i].do_comp.co_rank == rank) {
			found = &doms[i];
			break;
		}
	}

	return found;
}

/**
 * Find all targets belonging to a given list of ranks
 *
 * \param map		[IN]	pool map to find the target.
 * \param rank_list	[IN]	rank to be used to find target.
 * \param tgts		[OUT]	found targets.
 *
 * \return		number of targets.
 *                      negative errno if failed.
 *                      Caller is responsible for pool_target_id_list_free
 */
int
pool_map_find_targets_on_ranks(struct pool_map *map, d_rank_list_t *rank_list,
			       struct pool_target_id_list *tgts)
{
	uint32_t count = 0;
	uint32_t i;
	uint32_t j;
	int rc;

	tgts->pti_ids = NULL;
	tgts->pti_number = 0;

	for (i = 0; i < rank_list->rl_nr; i++) {
		struct pool_domain *dom;

		dom = pool_map_find_node_by_rank(map, rank_list->rl_ranks[i]);
		if (dom == NULL) {
			pool_target_id_list_free(tgts);
			return 0;
		}

		for (j = 0; j < dom->do_target_nr; j++) {
			struct pool_target_id id = {0};

			id.pti_id = dom->do_targets[j].ta_comp.co_id;

			rc = pool_target_id_list_append(tgts, &id);
			if (rc != 0) {
				pool_target_id_list_free(tgts);
				return 0;
			}

			count++;
		}
	}

	return count;
}

/**
 * Find the target by rank & idx.
 *
 * \param map	[IN]	pool map to find the target.
 * \param rank	[IN]	rank to be used to find target.
 * \param tgt_idx [IN]	tgt_idx to be used to find target.
 * \param tgts	[OUT]	targets found by rank/tgt_idx.
 *
 * \return		number of targets.
 *                      negative errno if failed.
 */
int
pool_map_find_target_by_rank_idx(struct pool_map *map, uint32_t rank,
				 uint32_t tgt_idx, struct pool_target **tgts)
{
	struct pool_domain	*dom;

	dom = pool_map_find_node_by_rank(map, rank);
	if (dom == NULL)
		return 0;

	if (tgt_idx == -1) {
		*tgts = dom->do_targets;
		return dom->do_target_nr;
	}

	if (tgt_idx >= dom->do_target_nr)
		return 0;

	*tgts = &dom->do_targets[tgt_idx];

	return 1;
}

static int
activate_new_target(struct pool_domain *domain, uint32_t id)
{
	int i;

	D_ASSERT(domain->do_targets != NULL);

	/*
	 * If this component has children, recurse over them.
	 *
	 * If the target ID is found in any of the children, activate
	 * this component and abort the search
	 */
	if (domain->do_children != NULL) {
		for (i = 0; i < domain->do_child_nr; i++) {
			int found = activate_new_target(&domain->do_children[i],
							id);
			if (found) {
				domain->do_comp.co_status = PO_COMP_ST_UPIN;
				return found;
			}
		}
	}

	/*
	 * Check the targets in this domain to see if they match
	 *
	 * If they do, activate them and activate the current domain
	 */
	for (i = 0; i < domain->do_target_nr; i++) {
		struct pool_component *comp = &domain->do_targets[i].ta_comp;

		if (comp->co_id == id && (comp->co_status == PO_COMP_ST_NEW ||
					  comp->co_status == PO_COMP_ST_UP)) {
			comp->co_status = PO_COMP_ST_UPIN;
			domain->do_comp.co_status = PO_COMP_ST_UPIN;
			return 1;
		}
	}

	return 0;
}

/**
 * Activate (move to UPIN) a NEW or UP target and all of its parent domains
 *
 * \param map	[IN]		The pool map to search
 * \param id	[IN]		Target ID to search
 *
 * \return		0 if target was not found or not in NEW state
 *                      1 if target was found and activated
 */
int
pool_map_activate_new_target(struct pool_map *map, uint32_t id)
{
	if (map->po_tree != NULL)
		return activate_new_target(map->po_tree, id);
	return 0;
}


/**
 * Check if all targets under one node matching the status.
 * \params [IN] dom	node domain to be checked.
 * \param [IN] status	status to be checked.
 *
 * \return		true if matches, otherwise false.
 */
bool
pool_map_node_status_match(struct pool_domain *dom, unsigned int status)
{
	int i;

	for (i = 0; i < dom->do_target_nr; i++) {
		if (!(dom->do_targets[i].ta_comp.co_status & status))
			return false;
	}

	return true;
}

static void
fseq_sort_op_swap(void *array, int a, int b)
{
	struct pool_component *comps = (struct pool_component *)array;
	struct pool_component  tmp;

	tmp = comps[a];
	comps[a] = comps[b];
	comps[b] = tmp;
}

static int
fseq_sort_op_cmp(void *array, int a, int b)
{
	struct pool_component *comps = (struct pool_component *)array;

	if (comps[a].co_fseq > comps[b].co_fseq)
		return 1;
	if (comps[a].co_fseq < comps[b].co_fseq)
		return -1;
	return 0;
}

static int
fseq_sort_op_cmp_key(void *array, int i, uint64_t key)
{
	struct pool_component *comps = (struct pool_component *)array;
	uint32_t		fseq = (uint32_t)key;

	if (comps[i].co_fseq > fseq)
		return 1;
	if (comps[i].co_fseq < fseq)
		return -1;
	return 0;
}

/** fseq based sort and lookup for components */
static daos_sort_ops_t fseq_sort_ops = {
	.so_swap	= fseq_sort_op_swap,
	.so_cmp		= fseq_sort_op_cmp,
	.so_cmp_key	= fseq_sort_op_cmp_key,
};

struct find_tgts_param {
	uint32_t	ftp_max_fseq;
	uint32_t	ftp_min_fseq;
	uint8_t		ftp_status;
	unsigned long	ftp_chk_max_fseq:1,
			ftp_chk_min_fseq:1,
			ftp_chk_status:1;
};

static bool
matched_criteria(struct find_tgts_param *param,
		 struct pool_target *tgt)
{
	if (param->ftp_chk_status &&
	    !(param->ftp_status & tgt->ta_comp.co_status))
		return false;

	if (param->ftp_chk_max_fseq &&
	    param->ftp_max_fseq < tgt->ta_comp.co_fseq)
		return false;

	if (param->ftp_chk_min_fseq &&
	    param->ftp_min_fseq > tgt->ta_comp.co_fseq)
		return false;

	return true;
}

/**
 * Find array of targets which match the query criteria. Caller is
 * responsible for freeing the target array.
 *
 * \param map     [IN]	The pool map to search
 * \param param   [IN]	Criteria to be checked
 * \param sorter  [IN]	Sorter for the output targets array
 * \param tgt_pp  [OUT]	The output target array, if tgt_pp == NULL, it only
 *                      needs to get the tgt count, otherwise it will
 *                      allocate the tgts array.
 * \param tgt_cnt [OUT]	The size of target array
 *
 * \return	0 on success, negative values on errors.
 */
static int
pool_map_find_tgts(struct pool_map *map, struct find_tgts_param *param,
		   daos_sort_ops_t *sorter, struct pool_target **tgt_pp,
		   unsigned int *tgt_cnt)
{
	struct pool_target *targets;
	int i, total_cnt, idx = 0;

	if (tgt_pp != NULL)
		*tgt_pp = NULL;
	*tgt_cnt = 0;

	if (pool_map_empty(map)) {
		D_ERROR("Uninitialized pool map\n");
		return 0;
	}

	/* pool map won't be changed between the two scans */
	total_cnt = pool_map_target_nr(map);
	targets = pool_map_targets(map);
rescan:
	for (i = 0; i < total_cnt; i++) {
		if (matched_criteria(param, &targets[i])) {
			if (tgt_pp == NULL || *tgt_pp == NULL)
				(*tgt_cnt)++;
			else
				(*(tgt_pp))[idx++] = targets[i];
		}
	}

	if (*tgt_cnt == 0 || tgt_pp == NULL)
		return 0;

	if (*tgt_pp == NULL) {
		D_ALLOC_ARRAY(*tgt_pp, *tgt_cnt);
		if (*tgt_pp == NULL)
			return -DER_NOMEM;
		goto rescan;
	} else if (sorter != NULL) {
		daos_array_sort(*tgt_pp, *tgt_cnt, false, sorter);
	}

	return 0;
}

/**
 * This function recursively scans the pool_map and records how many failures
 * each domain contains. A domain is considered to have a failure if there are
 * ANY failed targets within that domain. This is used to determine whether a
 * pool meets a containers redundancy requirements when opening.
 *
 * \param dom		[in] The pool domain currently being scanned.
 * \param fail_cnts	[in] The array used to track failures for each domain.
 * \param domain_level	[in] the current domain level used to index fail_cnts.
 *
 * \return	returns the number of downstream failures found in "dom".
 */
static int
update_failed_cnt_helper(struct pool_domain *dom,
		struct pool_fail_comp *fail_cnts, int domain_level)
{
	struct pool_domain *next_dom;
	int i;
	int failed_children;
	int num_failed = 0;

	if (dom == NULL)
		return 0;

	if (dom->do_children == NULL) {
		for (i = 0; i < dom->do_target_nr; ++i) {
			if (pool_target_unavail(&dom->do_targets[i], false))
				num_failed++;
		}
	} else {
		for (i = 0; i < dom->do_child_nr; ++i) {
			next_dom = &dom->do_children[i];

			failed_children = update_failed_cnt_helper(next_dom,
					fail_cnts, domain_level + 1);
			if (failed_children > 0)
				num_failed++;
		}

	}

	if (num_failed > 0)
		fail_cnts[domain_level].fail_cnt++;
	fail_cnts[domain_level].comp_type = dom->do_comp.co_type;

	return num_failed;
}

/**
 * Update the failed target count for the pool map.
 * This should be called anytime the pool map is updated.
 */
int
pool_map_update_failed_cnt(struct pool_map *map)
{
	int rc;
	struct pool_domain *root;
	struct pool_fail_comp *fail_cnts = map->po_comp_fail_cnts;

	memset(fail_cnts, 0, sizeof(*fail_cnts) * map->po_domain_layers);

	rc = pool_map_find_domain(map, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);
	if (rc == 0)
		return -DER_INVAL;

	update_failed_cnt_helper(root, fail_cnts, 0);
	return 0;
}

/**
 * Find all targets with @status in specific rank. Note: &tgt_pp will be
 * allocated and the caller is responsible to free it.
 */
int
pool_map_find_by_rank_status(struct pool_map *map,
			     struct pool_target ***tgt_ppp,
			     unsigned int *tgt_cnt, unsigned int status,
			     d_rank_t rank)
{
	struct pool_domain	*dom;
	int			i;

	*tgt_ppp = NULL;
	*tgt_cnt = 0;
	dom = pool_map_find_node_by_rank(map, rank);
	if (dom == NULL)
		return 0;

	for (i = 0; i < dom->do_target_nr; i++) {
		if (dom->do_targets[i].ta_comp.co_status & status) {
			if (*tgt_ppp == NULL) {
				D_ALLOC_ARRAY(*tgt_ppp,	dom->do_target_nr);
				if (*tgt_ppp == NULL)
					return -DER_NOMEM;
			}
			(*tgt_ppp)[(*tgt_cnt)++] = &dom->do_targets[i];
		}
	}
	return 0;
}

/**
 * Find all targets with DOWN|DOWNOUT state in specific rank.
 */
int
pool_map_find_failed_tgts_by_rank(struct pool_map *map,
				  struct pool_target ***tgt_ppp,
				  unsigned int *tgt_cnt, d_rank_t rank)
{
	unsigned int status;

	status = PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT | PO_COMP_ST_DRAIN;
	return pool_map_find_by_rank_status(map, tgt_ppp, tgt_cnt, status,
					    rank);
}

int
pool_map_find_tgts_by_state(struct pool_map *map,
			    pool_comp_state_t match_states,
			    struct pool_target **tgt_pp, unsigned int *tgt_cnt)
{
	struct find_tgts_param param;

	param.ftp_max_fseq = 0;
	param.ftp_min_fseq = 0;
	param.ftp_status = match_states;
	param.ftp_chk_max_fseq = 0;
	param.ftp_chk_min_fseq = 0;
	param.ftp_chk_status = 1;

	return pool_map_find_tgts(map, &param, &fseq_sort_ops, tgt_pp, tgt_cnt);
}

/**
 * Find all targets in UP state. (but not included in the pool for active I/O
 * i.e. UP_IN). Raft leader can use it drive target reintegration/addition.
 */
int
pool_map_find_up_tgts(struct pool_map *map, struct pool_target **tgt_pp,
		      unsigned int *tgt_cnt)
{
	return pool_map_find_tgts_by_state(map,
					   PO_COMP_ST_UP,
					   tgt_pp, tgt_cnt);
}

/**
 * Find all targets in DOWN state. Raft leader can use it drive target
 * rebuild one by one.
 */
int
pool_map_find_down_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			unsigned int *tgt_cnt)
{
	return pool_map_find_tgts_by_state(map,
					   PO_COMP_ST_DOWN,
					   tgt_pp, tgt_cnt);
}

/**
 * Find all targets in DOWN|DOWNOUT state.
 *
 * Note that this does not return DRAIN targets, because those are still healthy
 * while they are draining
 */
int
pool_map_find_failed_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			  unsigned int *tgt_cnt)
{
	return pool_map_find_tgts_by_state(map,
					   PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT,
					   tgt_pp, tgt_cnt);
}

/**
 * Find all targets in UPIN state (included in the pool for active I/O).
 */
int
pool_map_find_upin_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			unsigned int *tgt_cnt)
{
	return pool_map_find_tgts_by_state(map,
					   PO_COMP_ST_UPIN,
					   tgt_pp, tgt_cnt);
}

static void
pool_domain_print(struct pool_domain *domain, int dep)
{
	int		i;

	D_PRINT("%*s%s[%d] %d %s\n", dep * 8, "", pool_domain_name(domain),
		domain->do_comp.co_id, domain->do_comp.co_ver,
		pool_comp_state2str(domain->do_comp.co_status));

	D_ASSERT(domain->do_targets != NULL);

	if (domain->do_children != NULL) {
		for (i = 0; i < domain->do_child_nr; i++)
			pool_domain_print(&domain->do_children[i], dep + 1);
		return;
	}

	for (i = 0; i < domain->do_target_nr; i++) {
		struct pool_component *comp = &domain->do_targets[i].ta_comp;

		D_ASSERTF(comp->co_type == PO_COMP_TP_TARGET,
			  "%s\n", pool_comp_type2str(comp->co_type));

		D_PRINT("%*s%s[%d] %d %s\n", (dep + 1) * 8, "",
			pool_comp_type2str(comp->co_type),
			comp->co_id, comp->co_ver,
			pool_comp_state2str(comp->co_status));
	}
}

/**
 * Print all components of the pool map, this is a debug function.
 */
void
pool_map_print(struct pool_map *map)
{
	D_PRINT("Cluster map version %d\n", map->po_version);
	if (map->po_tree != NULL)
		pool_domain_print(map->po_tree, 0);
}

/**
 * Return the version of the pool map.
 */
unsigned int
pool_map_get_version(struct pool_map *map)
{
	D_DEBUG(DB_TRACE, "Fetch pool map version %u\n", map->po_version);
	D_ASSERT(map != NULL);
	return map->po_version;
}

/**
 * Update the version of the pool map.
 */
int
pool_map_set_version(struct pool_map *map, uint32_t version)
{
	if (map->po_version > version) {
		D_ERROR("Cannot decrease pool map version %u/%u\n",
			map->po_version, version);
		return -DER_NO_PERM;
	}

	if (map->po_version == version)
		return 0;

	D_DEBUG(DB_TRACE, "Update pool map version %u->%u\n",
		map->po_version, version);

	map->po_version = version;
	return 0;
}

int
pool_map_get_failed_cnt(struct pool_map *map, pool_comp_type_t type)
{
	int i;
	int fail_cnt = -1;

	for (i = 0; i < map->po_domain_layers; ++i) {
		if (map->po_comp_fail_cnts[i].comp_type == type) {
			fail_cnt = map->po_comp_fail_cnts[i].fail_cnt;
			break;
		}
	}

	if (fail_cnt == -1)
		return -DER_NONEXIST;

	return fail_cnt;
}
/**
 * check if the pool map is empty
 */
static bool
pool_map_empty(struct pool_map *map)
{
	return map->po_tree == NULL;
}

static bool
pool_target_id_found(struct pool_target_id_list *id_list,
		     struct pool_target_id *tgt)
{
	int i;

	for (i = 0; i < id_list->pti_number; i++)
		if (id_list->pti_ids[i].pti_id == tgt->pti_id)
			return true;
	return false;
}

int
pool_target_id_list_append(struct pool_target_id_list *id_list,
			   struct pool_target_id *id)
{
	struct pool_target_id *new_ids;
	int rc = 0;

	if (pool_target_id_found(id_list, id))
		return 0;

	D_REALLOC_ARRAY(new_ids, id_list->pti_ids, id_list->pti_number + 1);
	if (new_ids == NULL)
		return -DER_NOMEM;

	new_ids[id_list->pti_number] = *id;
	id_list->pti_ids = new_ids;
	id_list->pti_number++;

	return rc;
}

int
pool_target_id_list_merge(struct pool_target_id_list *dst_list,
			  struct pool_target_id_list *src_list)
{
	int i;
	int rc = 0;

	for (i = 0; i < src_list->pti_number; i++) {
		rc = pool_target_id_list_append(dst_list,
						&src_list->pti_ids[i]);
		if (rc)
			break;
	}

	return rc;
}

int
pool_target_id_list_alloc(unsigned int num,
			  struct pool_target_id_list *id_list)
{
	D_ALLOC_ARRAY(id_list->pti_ids,	num);
	if (id_list->pti_ids == NULL)
		return -DER_NOMEM;

	id_list->pti_number = num;

	return 0;
}

void
pool_target_id_list_free(struct pool_target_id_list *id_list)
{
	if (id_list == NULL)
		return;

	if (id_list->pti_ids)
		D_FREE(id_list->pti_ids);
}
