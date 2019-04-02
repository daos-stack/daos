/**
 *
 *
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
 * src/placement/mapless_jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include "mapless_jump_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>

/**
 * This struct contains information used in determining
 * where the information is that denotes whether or not this
 * node has already been used.
 *
 * This structure must be updated every time there is a
 * change to the pool map.
 */
struct coll_map {
	/** number of nodes for this level (targets or domains) */
	unsigned int    do_node_cnt;
	/** child nodes for collision map */
	struct coll_map *do_children;
	/** number unused children domains */
	uint32_t        cnt_used_offset;
	/** the offset for the location in the "used" array */
	uint32_t        coll_offset;
};

struct remap_node {
	uint32_t rank;
	uint32_t shard_idx;
};

/**
 * This is the Mapless placement map structure used to place objects.
 * The map is returned as a struct pl_map and then converted back into a
 * pl_mapless_map once passed from the caller into the object placement
 * functions.
 */
struct pl_mapless_map {
	/** placement map interface */
	struct pl_map   mmp_map;
	/** the total length of the array used for bookkeeping */
	uint32_t        dom_used_length;

	uint32_t        cnt_used_length;
	/** the collision map used during placement to avoid collisions */
	struct coll_map *co_map_root;
};

/**
 * This function converts a generic pl_map pointer into the
 * proper placement map. This function assumes that the original
 * map was allocated as a pl_mapless_map with a pl_map as it's
 * first member.
 *
 * \param[in]   *map    A pointer to a pl_map which is the first
 *                      member of the specific placement map we're
 *                      converting to.
 *
 * \return      A pointer to the full pl_mapless_map used for
 *              object placement, rebuild, and reintegration
 */
static inline struct pl_mapless_map *
pl_map2mplmap(struct pl_map *map)
{
	return container_of(map, struct pl_mapless_map, mmp_map);
}

/**
 * This function recursively creates a collision map that is used to
 * keep track of which nodes in a given domain have been used. The collision
 * map mirrors the pool maps structure,
 *
 * \param[in]   *domain         A pointer to the root node of the pool map
 *                              domains.
 * \param[out]  *co_map         A pointer to the collision map that will
 *                              be created.
 * \param[in]   *offset_start   The index in the array of utilized targets
 *                              where the information related to this node is
 *                              located (Generally starts at n = number
 *                              of targets)
 *
 * \return      void
 */
static void
create_collision_tree(struct pool_domain *dom, struct coll_map *co_map,
		      uint32_t *used_currnt, uint32_t *cnt_currnt)
{
	/* There should always be targets in the pool map */
	D_ASSERT(dom->do_targets != NULL);

	/* these are the offsets into their respective book-keeping arrays */
	co_map->coll_offset = *used_currnt;
	co_map->cnt_used_offset = *cnt_currnt;

	/* Every domain has a count for how many node have been used */
	(*cnt_currnt)++;

	/* If this is non-target level domain */
	if (dom->do_children != NULL) {
		uint32_t i;

		D_ALLOC_ARRAY(co_map->do_children, dom->do_child_nr);

		/* Keep track of number of children in a single place */
		co_map->do_node_cnt = dom->do_child_nr;
		*used_currnt += dom->do_child_nr;

		/* Recursively build the collision map */
		for (i = 0; i < dom->do_child_nr; i++)
			create_collision_tree(&dom->do_children[i],
					      &co_map->do_children[i],
					      used_currnt, cnt_currnt);
	} else {
		/* We don't used a byte map for individual targets */
		co_map->coll_offset = 0;
		co_map->do_node_cnt = dom->do_target_nr;
	}
}
/**
 * This function recursively frees all the children in the collision tree.
 *
 * \param[in]   co_map         The collision tree to be freed.
 *
 * \return      void
 */
static void
free_collision_tree(struct coll_map *co_map)
{
	/* If this is non-target level domain */
	if (co_map->do_children != NULL) {
		uint32_t i;

		/* Recursively free the collision map */
		for (i = 0; i < co_map->do_node_cnt; i++)
			free_collision_tree(&co_map->do_children[i]);

		D_FREE(co_map->do_children);
	}
}


/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   curr_dom        The current domain that is being used to
 *                              determine the target location for this shard.
 *                              initially the root node of the pool map.
 * \param[out]  target          This variable is used when returning the
 *                              selected target for this shard.
 * \param[in]   co_map          The collision map used for book keeping
 *                              purposes when selecting targets. Contains
 *                              additional information about the pool map.
 * \param[in]   obj_key         a unique key generated using the object ID.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   next_dom        The "stack" used when iterating down the pool
 *                              map. This is required because there is a
 *                              chance we may need to iterate back up the pool
 *                              map.
 * \param[in]   next_co_map     A stack similar to the next dom variable.
 * \param[in]   dom_count       An array that contains the number of targets
 *                              previously used that will be cleared when all
 *                              the domains have been used. Important for the
 *                              case that we must place more shards than
 *                              domains in a particular fault domain.
 * \param[in]   used_targets    A list of the targets that have been used. We
 *                              iterate through this when selecting the next
 *                              target in a placement to determine if that
 *                              location is valid.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 *
 * \return      an int value indicating if the returned target is available (0)
 *              or failed (1)
 */
static void
get_target(struct pool_domain *curr_dom, struct pool_target **target,
	   struct coll_map *co_map, uint64_t obj_key, uint8_t *dom_used,
	   struct pool_domain **next_dom, struct coll_map **next_co_map,
	   uint32_t *dom_count, uint32_t *used_targets, int shard_num)
{
	uint8_t         found_target = 0;
	uint8_t         top = 0;
	uint32_t        fail_num = 0;
	uint32_t        selected_dom;
	uint64_t        total_tgts;

	/* Used to determine if there are fewer targets than shards */
	total_tgts = curr_dom->do_target_nr;

	/* top of the "call" stack */
	next_dom[top] = curr_dom;
	next_co_map[top] = co_map;

	do {
		uint32_t        i;
		uint32_t        num_doms;
		uint64_t        key;
		uint8_t         *coll_dom_start;

		curr_dom = next_dom[top];
		co_map = next_co_map[top];

		/* Retrieve number of nodes in this domain */
		num_doms = co_map->do_node_cnt;
		/* Get the start position of bookkeeping array for domain */
		coll_dom_start = &dom_used[co_map->coll_offset];

		/*
		 * If all of the nodes have been used for shards but we
		 * still have shards to place mark all nodes as unused
		 * so duplicates can be chosen
		 */
		if (dom_count[co_map->cnt_used_offset] == num_doms &&
		    curr_dom->do_children != NULL) {

			dom_count[co_map->cnt_used_offset] = 0;
			for (i = 0; i < num_doms; ++i)
				coll_dom_start[i] = 0;
		}

		key = obj_key;
		/* If choosing target in lowest fault domain level */
		if (curr_dom->do_children == NULL) {
			uint32_t dom_id;
			/*
			 * Number of repeats allowed when sharding is wider
			 * than stripe count
			 *
			 * 0 if placing fewer shards than available targets
			 */
			uint16_t num_repeats = (shard_num / total_tgts);

			do {
				uint16_t repeats = 0;

				/*
				 * Must crc key because jump consistent hash
				 * requires an even distribution or it will
				 * not work
				 */
				key = crc(key, fail_num++);

				/* Get target for shard */
				selected_dom = jump_consistent_hash(key,
								    num_doms);

				/* Retrieve actual target using index */
				*target = &curr_dom->do_targets[selected_dom];

				/* Get target id to check if target used */
				dom_id = (*target)->ta_comp.co_id;

				/*
				 * Check to see if this target is valid to use.
				 * You can reuse targets as long as there are
				 * fewer targets than shards and all targets
				 * have already been used
				 */
				for (i = 0; used_targets[i] != 0; ++i) {
					if (used_targets[i] == dom_id + 1)
						repeats++;
					if (repeats > num_repeats)
						break;
				}
			} while (used_targets[i]);

			/* Mark target as used */
			used_targets[i] = dom_id + 1;
			dom_count[co_map->cnt_used_offset]++;

			/* Found target (which may be available or not) */
			found_target = 1;
		} else {
			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {
				selected_dom = jump_consistent_hash(key,
								    num_doms);
				key = crc(key, fail_num);
			} while (coll_dom_start[selected_dom] == 1);

			/* Mark this domain as used */
			coll_dom_start[selected_dom] = 1;
			dom_count[co_map->cnt_used_offset]++;

			/* Add domain info to the stack */
			top++;
			next_dom[top] = &(curr_dom->do_children[selected_dom]);
			next_co_map[top] = &(co_map->do_children[selected_dom]);
			obj_key = crc(obj_key, top);
		}
	} while (!found_target);

}

/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   root            The top level domain that is being used to
 *                              determine the target location for this shard.
 *                              The root node of the pool map.
 * \param[in]   target_id       This is the ID of the original layout location
 *                              that was determined to be unavailable.
 * \param[in]   co_map          The collision map used for book keeping
 *                              purposes when selecting targets. contains
 *                              additional information about the pool map.
 * \param[in]   top_level_id    This is the top level below the root that the
 *                              shard was originally placed on. This is used in
 *                              order to attempt selecting the rebuild target
 *                              from any other domain first.
 * \param[in]   key             A unique key generated using the object ID.
 *                              This is the same key used during initial
 *                              placement.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   dom_count       An array that contains the number of targets
 *                              previously used that will be cleared when all
 *                              the domains have been used. Important for the
 *                              case that we must place more shards than
 *                              domains in a particular fault domain.
 *                              location is valid.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 */

static void
get_rebuild_target(struct pool_domain *root, uint32_t *target_id,
		   struct coll_map *co_map, uint32_t *top_level_id,
		   uint64_t key, uint8_t *dom_used, uint32_t *dom_count,
		   int shard_num)
{
	int                     i;
	uint8_t                 *coll_dom_start;
	uint8_t                 *used_tgts = NULL;
	uint32_t                selected_dom;
	uint32_t                fail_num = 0;
	uint32_t                try = 0;
	struct pool_target      *target;
	struct pool_domain      *target_selection;

	/* Get the top level start location for bookkeeping */
	coll_dom_start = &dom_used[co_map->coll_offset];
	while (1) {

		uint8_t skip = 0;
		uint32_t curr_id;
		uint32_t num_doms;

		num_doms = root->do_child_nr;

		/*
		 * If all domains have been tried and failed, clear the
		 * collision bookkeeping then choose domain  of original
		 * failure.
		 */
		if (dom_count[co_map->cnt_used_offset] + 1 >= num_doms) {
			dom_count[co_map->cnt_used_offset] = 0;
			top_level_id = NULL;
			for (i = 0; i < num_doms; ++i)
				coll_dom_start[i] = 0;
		}

		/*
		 * Choose domains using jump consistent hash until we find a
		 * suitable domains that has not already been used.
		 */
		do {
			key = crc(key, fail_num++);
			selected_dom = jump_consistent_hash(key, num_doms);
			target_selection = &(root->do_children[selected_dom]);
			curr_id = target_selection->do_comp.co_id;
		} while (top_level_id != NULL && curr_id  == *top_level_id);

		/* mark this domain as used */
		coll_dom_start[selected_dom] = 1;
		dom_count[co_map->cnt_used_offset]++;

		/* To find rebuild target we examine all targets */
		num_doms = target_selection->do_target_nr;

		D_ALLOC_ARRAY(used_tgts, num_doms);

		/*
		 * Attempt to choose a fallback target from all targets found
		 * in this top level domain.
		 */
		do {
			key = crc(key, try++);

			selected_dom = jump_consistent_hash(key, num_doms);
			target = &target_selection->do_targets[selected_dom];

			*target_id = target->ta_comp.co_id;
			/*
			 * keep track of what targets have been tried
			 * in case all targets in domain have failed
			 */
			if (used_tgts[selected_dom] == 0) {
				skip++;
				used_tgts[selected_dom] = 1;
			}
		} while (pool_target_unavail(target) && skip < num_doms);

		D_FREE(used_tgts);

		/* Use the last examined target if it's not unavailable */
		if (!pool_target_unavail(target))
			return;
	}

	/* Should not reach this point */
	D_ERROR("Unexpectedly reached end of placement loop without result");
	D_ASSERT(0);
}

/**
 * This function handles getting the initial layout for the object as well as
 * finding the rebuild targets if any of the targets are unavailable. If this
 * function is called from the rebuild interface then it also returns the list
 * of targets that need to be remapped.
 *
 * \param[in]   root            This is the root of the pool map tree structure
 *                              containing all the fault domain information.
 * \param[out]  layout          This structure will contained the ordered list
 *                              of targets to be used for the object layout.
 * \param[in]   com_map         This is the collision map used during the
 *                              placement process for bookkeeping and
 *                              determining which domains have been used.
 * \param[in]   group_cnt       This number of redundancy groups for this
 *                              objects layout.
 * \param[in]   group_size      The size of each redundancy group in the layout.
 *                              This will be multiplied with group_cnt to get
 *                              the total number of targets required for
 *                              placement.
 * \param[in]   tree_depth      The size the stack will need to be when calling
 *                              the iterative get_target function.
 * \param[in]   dom_map_size    The number of non-leaf nodes in the pool map.
 *                              This value will be used to initialize the array
 *                              that holds the statuses of whether the node has
 *                              been used or not.
 * \param[in]   cnt_map_size    The number of child domains used for a given
 *                              non-leaf node in the pool map. When this equals
 *                              the total number of child nodes we reset the
 *                              status showing the nodes have been used.
 * \param[out]  remap_list      The list of nodes that needs to be rebuilt
 *                              during the rebuild process. This is only
 *                              returned when this function is called from the
 *                              rebuild function and will otherwise be NULL.
 *
 * \return                      Returns the number of targets that need to be
 *                              rebuilt. This number will be ignored when called
 *                              from functions other than find rebuild.
 */
static int
get_target_layout(struct pool_domain *root, struct pl_obj_layout *layout,
		  struct coll_map *co_map, uint16_t group_cnt,
		  uint16_t group_size, daos_obj_id_t oid, uint8_t tree_depth,
		  uint32_t dom_map_size, uint32_t cnt_map_size,
		  struct remap_node *remap_list)
{
	struct pool_target *target;
	struct pool_domain *next_dom[tree_depth];
	struct coll_map *next_co_map[tree_depth];
	uint8_t dom_used[dom_map_size];
	uint32_t dom_cnt[cnt_map_size];
	uint32_t target_id;
	uint32_t rebuild_doms[group_size * group_cnt];
	uint8_t rebuild_shard_num[group_size * group_cnt];
	uint32_t used_targets[layout->ol_nr * 2];
	uint8_t rebuild_num = 0;
	int i;
	int j;
	int k;

	memset(dom_used, 0, sizeof(*dom_used) * dom_map_size);
	memset(dom_cnt, 0, sizeof(*dom_cnt) *  cnt_map_size);
	memset(used_targets, 0, (sizeof(*used_targets) * layout->ol_nr) * 2);

	for (i = 0, k = 0; i < group_cnt; i++) {
		for (j = 0; j < group_size; j++, k++) {
			uint32_t tgt_id;
			uint32_t ndom_id;

			get_target(root, &target, co_map, crc(oid.lo, k),
				   dom_used, next_dom, next_co_map,
				   dom_cnt, used_targets, k);

			tgt_id = target->ta_comp.co_id;
			ndom_id = next_dom[1]->do_comp.co_id;

			if (pool_target_unavail(target)) {
				rebuild_doms[rebuild_num] = ndom_id;
				rebuild_shard_num[rebuild_num] = k;

				uint32_t tgt_rank = target->ta_comp.co_rank;

				if (remap_list != NULL) {
					remap_list[rebuild_num].rank = tgt_rank;
					remap_list[rebuild_num].shard_idx = k;

				}
				rebuild_num++;
			}
			layout->ol_shards[k].po_target = tgt_id;
			layout->ol_shards[k].po_shard = k;
		}
	}

	memset(dom_cnt, 0, sizeof(*dom_cnt) *  cnt_map_size);

	for (i = 0; i < rebuild_num; ++i) {
		k = rebuild_shard_num[i];

		get_rebuild_target(root, &target_id, co_map, &(rebuild_doms[i]),
				   crc(oid.lo, k), dom_used, dom_cnt, k);

		layout->ol_shards[k].po_target = target_id;
		layout->ol_shards[k].po_shard = k;
	}

	return rebuild_num;
}

/**
 * This function allocates and initializes the placement map.
 *
 * \param[in]   poolmap The pool map to be used when calculating object
 *                      placement.
 * \param[in]   mia     placement map initialization values. Part of the
 *                      placement map API but currently not used in this map.
 * \param[in]   mapp    The placement map interface that will be passed out
 *                      and used when placing objects.
 *
 * \return              The error status of the function.
 */
static int
mapless_jump_map_create(struct pool_map *poolmap, struct pl_map_init_attr *mia,
			struct pl_map **mapp)
{
	struct pool_domain      **root;
	struct pl_mapless_map   *mplmap;
	int                     rc;


	D_ALLOC_PTR(mplmap);
	if (mplmap == NULL)
		return -DER_NOMEM;

	D_ALLOC(mplmap->co_map_root, sizeof(mplmap->co_map_root));
	if (mplmap->co_map_root == NULL)
		return -DER_NOMEM;

	D_ALLOC(root, sizeof(*root));
	if (root == NULL)
		return -DER_NOMEM;

	pool_map_addref(poolmap);
	mplmap->mmp_map.pl_poolmap = poolmap;


	rc = pool_map_find_domain(poolmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL,
				  root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		D_FREE(root);
		D_FREE(mplmap->co_map_root);
		D_FREE_PTR(mplmap);
		return -DER_NONEXIST;
	}

	mplmap->dom_used_length = 0;
	mplmap->cnt_used_length = 0;

	create_collision_tree(*root, mplmap->co_map_root,
			      &(mplmap->dom_used_length),
			      &(mplmap->cnt_used_length));

	*mapp = &mplmap->mmp_map;

	D_FREE(root);
	return DER_SUCCESS;
}

/**
 * Frees the placement map and the
 *
 * \param[in]   map     The placement map to be freed
 *
 * \return              void
 */
static void
mapless_jump_map_destroy(struct pl_map *map)
{
	struct pl_mapless_map   *mplmap;

	mplmap = pl_map2mplmap(map);

	free_collision_tree(mplmap->co_map_root);
	D_FREE(mplmap->co_map_root);

	D_FREE_PTR(mplmap);
}

static void
mapless_jump_map_print(struct pl_map *map)
{
	/* TODO: Print out the collision map */
}

/**
 * Determines the locations that a given object shard should be located.
 *
 * \param[in]   map             A reference to the placement map being used to
 *                              place the object shard.
 * \param[in]   md              The object metadata which contains data about
 *                              the object being placed such as the object ID.
 * \param[in]   shard_md
 * \param[out]  layout_pp       The layout generated for the object. Contains
 *                              references to the targets in the pool map where
 *                              the shards will be placed.
 *
 * \return                      An integer value containing the error
 *                              message or 0 if the  function returned
 *                              successfully.
 */
static int
mapless_obj_place(struct pl_map *map, struct daos_obj_md *md,
		  struct daos_obj_shard_md *shard_md,
		  struct pl_obj_layout **layout_pp)
{
	struct pl_mapless_map   *mplmap;
	struct pl_obj_layout    *layout;
	struct pool_map         *pmap;
	uint16_t                group_size;
	uint16_t                group_cnt;
	struct daos_oclass_attr *oc_attr;
	daos_obj_id_t           oid;
	struct pool_domain      *root;
	int rc;

	mplmap = pl_map2mplmap(map);

	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);

	/* Retrieve group size and count */
	group_size = daos_oclass_grp_size(oc_attr);
	group_cnt = daos_oclass_grp_nr(oc_attr, md);

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(group_size * group_cnt, &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}
	pmap = map->pl_poolmap;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(map);

	/* Get root node of pool map */
	pool_map_find_domain(pmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);
	get_target_layout(root, layout, mplmap->co_map_root, group_size,
			  group_cnt, oid, 10, mplmap->dom_used_length,
			  mplmap->cnt_used_length, NULL);

	*layout_pp = layout;
	return DER_SUCCESS;
}

/**
 *
 * \param[in]   map             The placement map to be used to generate the
 *                              placement layouts and to calculate rebuild
 *                              targets.
 * \param[in]   md              Metadata describing the object.
 * \param[in]   shard_md        Metadata describing how the shards.
 * \param[in]   rebuild_ver     Current Rebuild version
 * \param[out]   tgt_rank       The rank of the targets that need to be rebuild
 *                              will be stored in this array to be passed out
 *                              (this is allocated by the caller)
 * \param[out]   shard_id       The shard IDs of the shards that need to be
 *                              rebuilt (This is allocated by the caller)
 * \param[in]   array_size      The max size of the passed in arrays to store
 *                              info about the shards that need to be rebuilt.
 *
 * \return                      The number of shards that need to be rebuilt on
 *                              another target, Or 0 if none need to be rebuilt.
 */
static int
mapless_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
			 struct daos_obj_shard_md *shard_md,
			 uint32_t rebuild_ver, uint32_t *tgt_rank,
			 uint32_t *shard_id, unsigned int array_size,
			 int myrank)
{
	struct pl_mapless_map   *mplmap;
	struct pl_obj_layout    *layout;
	struct remap_node       remap_list[array_size];
	struct pool_map         *pmap;
	uint16_t                group_size;
	uint16_t                group_cnt;
	struct daos_oclass_attr *oc_attr;
	daos_obj_id_t           oid;
	struct pool_domain      *root;
	int failed_tgt_num;
	int i;
	int rc;

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < rebuild_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), rebuild_ver);
		return -DER_INVAL;
	}

	mplmap = pl_map2mplmap(map);

	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);

	/* Retrieve group size and count */
	group_size = daos_oclass_grp_size(oc_attr);
	group_cnt = daos_oclass_grp_nr(oc_attr, md);

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(group_size * group_cnt, &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}

	pmap = map->pl_poolmap;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(map);

	/* Get root node of pool map */
	pool_map_find_domain(pmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);

	failed_tgt_num = get_target_layout(root, layout, mplmap->co_map_root,
					   group_size, group_cnt, oid, 10,
					   mplmap->dom_used_length,
					   mplmap->cnt_used_length, remap_list);

	for (i = 0; i < failed_tgt_num; ++i) {
		struct pool_target	*target;
		int			leader;

		target = NULL;
		if (myrank != -1)
			goto add;

		leader = pl_select_leader(md->omd_id,
				remap_list[i].shard_idx, layout->ol_nr,
				true, pl_obj_get_shard, layout);

		if (leader < 0) {
			D_WARN("Not sure whether current shard "
				"is leader or not for obj "DF_OID
				", ver:%d, shard:%d, rc = %d\n",
				DP_OID(md->omd_id), rebuild_ver,
				remap_list[i].shard_idx, leader);
			goto add;
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
			D_DEBUG(DB_TRACE, "Current replica (%d)"
				"isn't the leader (%d) for obj "
				DF_OID", fseq:%d, status:%d, "
				"ver:%d, shard:%d, skip it\n",
				myrank, target->ta_comp.co_rank,
				DP_OID(md->omd_id),
				target->ta_comp.co_fseq,
				target->ta_comp.co_status,
				rebuild_ver, remap_list[i].shard_idx);
			continue;
		}

add:
		tgt_rank[i] = remap_list[i].rank;
		shard_id[i] = remap_list[i].shard_idx;
	}
	return failed_tgt_num;
}

static int
mapless_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
		       struct daos_obj_shard_md *shard_md,
		       struct pl_target_grp *tgp_reint, uint32_t *tgt_reint)
{
	D_ERROR("Unsupported\n");
	return -DER_NOSYS;
}

/** API for generic placement map functionality */
struct pl_map_ops       mapless_map_ops = {
	.o_create               = mapless_jump_map_create,
	.o_destroy              = mapless_jump_map_destroy,
	.o_print                = mapless_jump_map_print,
	.o_obj_place            = mapless_obj_place,
	.o_obj_find_rebuild     = mapless_obj_find_rebuild,
	.o_obj_find_reint       = mapless_obj_find_reint,
};
