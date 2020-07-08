/**
 * (C) Copyright 2019 Intel Corporation.
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
 * vos/vos_gc.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos/mem.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"

enum {
	GC_CREDS_MIN	= 1,	/**< minimum credits for vos_gc_run */
	GC_CREDS_PRIV	= 256,	/**< credits for internal usage */
	GC_CREDS_MAX	= 4096,	/**< maximum credits for vos_gc_run */
};

/**
 * Default garbage bag size consumes <= 4K space
 * - header of vos_gc_bag_df is 64 bytes
 * - PMDK allocation overhead is 16 bytes,
 * - each item consumes 16 bytes, 250 * 16 = 4000 bytes
 * - together is 4080 bytes, reserve 16 bytes for future use
 */
static int gc_bag_size	= 250;

/** VOS garbage collector */
struct vos_gc {
	const char		 *gc_name;
	/** type of the GC, see @vos_gc_type */
	enum vos_gc_type	  gc_type;
	/**
	 * default execution credits for drain \a gc_drain.
	 * GC consumes user credits if this member is set to zero.
	 */
	const int		  gc_drain_creds;
	/**
	 * drain an item (release its children) collected by the current GC
	 * Release sub-items for @item, sub-item could be:
	 * - values of an akey
	 * - akeys of a dkey
	 * - dkeys of an object
	 * - objects of a container
	 *
	 * This function should return if @item has no more sub-items, or
	 * consumed all @credits (releasing a sub-item consumes a credit).
	 * @empty is set to true if all sub-items have been drained,
	 * otherwise it's set to false.
	 */
	int			(*gc_drain)(struct vos_gc *gc,
					    struct vos_pool *pool,
					    struct vos_gc_item *item,
					    int *credits, bool *empty);
	/**
	 * free an item collected by the current GC
	 * It is unused for now, but we might need it if we want to support
	 * GC_BIO, see commments in vos_gc_type.
	 */
	int			(*gc_free)(struct vos_gc *gc,
					   struct vos_pool *pool,
					   struct vos_gc_item *item);
};

static int gc_reclaim_pool(struct vos_pool *pool, int *credits,
			   bool *empty_ret);

/**
 * drain items stored in btree, this function returns when the btree is empty,
 * or all credits are consumed (releasing a leaf record consumes one credit)
 */
static int
gc_drain_btr(struct vos_gc *gc, struct vos_pool *pool,
	     struct btr_root *root, int *credits, bool *empty)
{
	daos_handle_t	toh;
	int		rc;

	rc = dbtree_open_inplace_ex(root, &pool->vp_uma, DAOS_HDL_INVAL,
				    pool, &toh);
	if (rc == -DER_NONEXIST) { /* empty tree */
		*empty = true;
		return 0;
	}
	if (rc)
		goto failed;

	D_DEBUG(DB_TRACE, "drain btree for %s, creds=%d\n",
		gc->gc_name, *credits);
	rc = dbtree_drain(toh, credits, NULL, empty);
	dbtree_close(toh);
	if (rc)
		goto failed;

	D_ASSERT(*credits >= 0);
	D_ASSERT(*empty || *credits == 0);
	D_DEBUG(DB_TRACE, "empty=%d, remainded creds=%d\n", *empty, *credits);
	return 0;
 failed:
	D_ERROR("Failed to drain %s btree: %s\n", gc->gc_name, d_errstr(rc));
	return rc;
}

/**
 * drain items stored in evtree, this function returns when the evtree is empty,
 * or all credits are consumed (releasing a leaf record consumes one credit)
 */
static int
gc_drain_evt(struct vos_gc *gc, struct vos_pool *pool,
	     struct evt_root *root, int *credits, bool *empty)
{
	struct evt_desc_cbs cbs;
	daos_handle_t	    toh;
	int		    rc;

	vos_evt_desc_cbs_init(&cbs, pool, DAOS_HDL_INVAL);
	rc = evt_open(root, &pool->vp_uma, &cbs, &toh);
	if (rc == -DER_NONEXIST) {
		*empty = true;
		return 0;
	}
	if (rc)
		goto failed;

	D_DEBUG(DB_TRACE, "drain %s evtree, creds=%d\n", gc->gc_name, *credits);
	rc = evt_drain(toh, credits, empty);
	evt_close(toh);
	if (rc)
		goto failed;

	D_ASSERT(*credits >= 0);
	D_ASSERT(*empty || *credits == 0);
	D_DEBUG(DB_TRACE, "empty=%d, remainded creds=%d\n", *empty, *credits);
	return 0;
 failed:
	D_ERROR("Failed to drain evtree %s: %s\n", gc->gc_name, d_errstr(rc));
	return rc;
}

/**
 * drain versioned values of a key, it returns when the value tree is empty,
 * or all credits are consumed (releasing a value consumes one credit)
 */
static int
gc_drain_key(struct vos_gc *gc, struct vos_pool *pool,
	     struct vos_gc_item *item, int *credits, bool *empty)
{
	struct vos_krec_df *key = umem_off2ptr(&pool->vp_umm, item->it_addr);
	int		    creds = *credits;
	int		    rc;

	if ((key->kr_bmap & KREC_BF_FLAT) && (gc->gc_type == GC_DKEY)) {
		*empty = true;
		return 0;
	}

	if (key->kr_bmap & KREC_BF_BTR) {
		rc = gc_drain_btr(gc, pool, &key->kr_btr, credits, empty);

	} else if (key->kr_bmap & KREC_BF_EVT) {
		D_ASSERT(gc->gc_type == GC_AKEY);
		rc = gc_drain_evt(gc, pool, &key->kr_evt, credits, empty);

	} else { /* empty key generated by punch */
		*empty = true;
		return 0;
	}

	if (rc) {
		D_ERROR("%s drain failed: "DF_RC"\n", gc->gc_name, DP_RC(rc));
		return rc;
	}

	if (gc->gc_type == GC_DKEY)
		return 0;

	/* gather value stats for akey */
	creds -= *credits;
	if (key->kr_bmap & KREC_BF_BTR)
		pool->vp_gc_stat.gs_singvs += creds;
	else
		pool->vp_gc_stat.gs_recxs += creds;
	return 0;
}

static int
gc_free_dkey(struct vos_gc *gc, struct vos_pool *pool, struct vos_gc_item *item)
{
	struct vos_krec_df *key = umem_off2ptr(&pool->vp_umm, item->it_addr);

	D_ASSERT(key->kr_bmap & KREC_BF_DKEY);
	if (key->kr_bmap & KREC_BF_FLAT)
		gc_add_item(pool, GC_AKEY, item->it_addr, item->it_args);
	else
		umem_free(&pool->vp_umm, item->it_addr);
	return 0;
}

/**
 * drain all keys stored in an object, it returns when the key tree is empty,
 * or all credits are consumed (releasing a key consumes one credit)
 */
static int
gc_drain_obj(struct vos_gc *gc, struct vos_pool *pool,
	     struct vos_gc_item *item, int *credits, bool *empty)
{
	struct vos_obj_df *obj = umem_off2ptr(&pool->vp_umm, item->it_addr);

	return gc_drain_btr(gc, pool, &obj->vo_tree, credits, empty);
}

/**
 * drain all objects stored in a container, it returns when the key tree is
 * empty, or all credits are consumed (releasing an object consumes one credit)
 */
static int
gc_drain_cont(struct vos_gc *gc, struct vos_pool *pool,
	      struct vos_gc_item *item, int *credits, bool *empty)
{
	struct vos_cont_df *cont = umem_off2ptr(&pool->vp_umm, item->it_addr);

	return gc_drain_btr(gc, pool, &cont->cd_obj_root, credits, empty);
}

static int
gc_free_cont(struct vos_gc *gc, struct vos_pool *pool, struct vos_gc_item *item)
{
	vos_dtx_table_destroy(&pool->vp_umm,
			      umem_off2ptr(&pool->vp_umm, item->it_addr));
	umem_free(&pool->vp_umm, item->it_addr);

	return 0;
}

static struct vos_gc	gc_table[] = {
	{
		.gc_name		= "akey",
		.gc_type		= GC_AKEY,
		.gc_drain_creds		= 0,	/* consume user credits */
		.gc_drain		= gc_drain_key,
		.gc_free		= NULL,
	},

	{
		.gc_name		= "dkey",
		.gc_type		= GC_DKEY,
		.gc_drain_creds		= 32,
		.gc_drain		= gc_drain_key,
		.gc_free		= gc_free_dkey,
	},
	{
		.gc_name		= "object",
		.gc_type		= GC_OBJ,
		.gc_drain_creds		= 8,
		.gc_drain		= gc_drain_obj,
		.gc_free		= NULL,
	},
	{
		.gc_name		= "container",
		.gc_type		= GC_CONT,
		.gc_drain_creds		= 1,
		.gc_drain		= gc_drain_cont,
		.gc_free		= gc_free_cont,
	},
	{
		.gc_name		= "unknown",
		.gc_type		= GC_MAX,
	},
};

static const char *
gc_type2name(enum vos_gc_type type)
{
	D_ASSERT(type < GC_MAX);
	return gc_table[type].gc_name;
}

struct vos_gc_bin_df *
gc_type2bin(struct vos_pool *pool, enum vos_gc_type type)
{
	D_ASSERT(type < GC_MAX);
	return &pool->vp_pool_df->pd_gc_bins[type];
}

/**
 * Free the first (oldest) garbage bag of a garbage bin unless it is also the
 * last (newest) bag.
 */
static int
gc_bin_free_bag(struct umem_instance *umm, struct vos_gc_bin_df *bin,
		umem_off_t bag_id)
{
	struct vos_gc_bag_df *bag = umem_off2ptr(umm, bag_id);

	D_ASSERT(bag_id == bin->bin_bag_first);
	if (bag_id == bin->bin_bag_last) {
		/* don't free the last bag, only reset it */
		D_ASSERT(bin->bin_bag_nr == 1);
		umem_tx_add_ptr(umm, bag, sizeof(*bag));
		bag->bag_item_first = bag->bag_item_last = 0;
		bag->bag_item_nr = 0;
		return 0;
	}

	D_ASSERT(bin->bin_bag_nr > 1);
	D_ASSERT(bag->bag_next != UMOFF_NULL);

	umem_tx_add_ptr(umm, bin, sizeof(*bin));
	bin->bin_bag_first = bag->bag_next;
	bin->bin_bag_nr--;

	umem_free(umm, bag_id);
	return 0;
}

/**
 * Returns the last (newest) garbage bag, it allocates a new bag if there
 * is no bag in the bin, or the last bag is full.
 */
struct vos_gc_bag_df *
gc_bin_find_bag(struct umem_instance *umm, struct vos_gc_bin_df *bin)
{
	struct vos_gc_bag_df *bag = NULL;
	umem_off_t	      bag_id;
	int		      size;

	if (!UMOFF_IS_NULL(bin->bin_bag_last)) {
		bag_id = bin->bin_bag_last;
		bag = umem_off2ptr(umm, bag_id);
		if (bag->bag_item_nr < bin->bin_bag_size)
			return bag;
	}

	/* allocate a new bag */
	size = offsetof(struct vos_gc_bag_df, bag_items[bin->bin_bag_size]);
	bag_id = umem_zalloc(umm, size);
	if (UMOFF_IS_NULL(bag_id))
		return NULL;

	umem_tx_add_ptr(umm, bin, sizeof(*bin));
	bin->bin_bag_last = bag_id;
	bin->bin_bag_nr++;
	if (bag) { /* the original last bag */
		umem_tx_add_ptr(umm, bag, sizeof(*bag));
		bag->bag_next = bag_id;
	} else {
		/* this is a new bin */
		bin->bin_bag_first = bag_id;
	}
	return umem_off2ptr(umm, bag_id);
}

static int
gc_bin_add_item(struct umem_instance *umm, struct vos_gc_bin_df *bin,
		struct vos_gc_item *item)
{
	struct vos_gc_bag_df *bag;
	struct vos_gc_item   *it;
	int		      last;

	bag = gc_bin_find_bag(umm, bin);
	if (!bag)
		return -DER_NOSPACE;

	D_ASSERT(bag->bag_item_nr < bin->bin_bag_size);
	/* NB: no umem_tx_add, this is totally safe because we never
	 * overwrite valid items
	 */
	it = &bag->bag_items[bag->bag_item_last];
	pmemobj_memcpy_persist(umm->umm_pool, it, item, sizeof(*it));

	last = bag->bag_item_last + 1;
	if (last == bin->bin_bag_size)
		last = 0;

	umem_tx_add_ptr(umm, bag, sizeof(*bag));
	bag->bag_item_last = last;
	bag->bag_item_nr += 1;
	return 0;
}

static struct vos_gc_item *
gc_get_item(struct vos_gc *gc, struct vos_pool *pool)
{
	struct vos_gc_bin_df	*bin = gc_type2bin(pool, gc->gc_type);
	struct vos_gc_bag_df	*bag;

	bag = umem_off2ptr(&pool->vp_umm, bin->bin_bag_first);
	if (bag == NULL) /* empty bin */
		return NULL;

	if (bag->bag_item_nr == 0) /* empty bag */
		return NULL;

	return &bag->bag_items[bag->bag_item_first];
}

static int
gc_drain_item(struct vos_gc *gc, struct vos_pool *pool,
	      struct vos_gc_item *item, int *credits, bool *empty)
{
	int	creds;
	int	rc;

	if (!gc->gc_drain) {
		/* NB: all the current GC types have drain function, but the
		 * future BIO GC may not have drain function.
		 */
		*empty = true;
		return 0;
	}

	if (gc->gc_type == GC_AKEY) {
		creds = *credits;
	} else {
		/* do not consume user credits, because this only flatten its
		 * subtree and wouldn't free any user key/data.
		 */
		creds = gc->gc_drain_creds;
	}

	D_ASSERT(item->it_addr != 0);
	rc = gc->gc_drain(gc, pool, item, &creds, empty);
	if (rc)
		return rc;

	if (gc->gc_type == GC_AKEY) {
		/* single value/recx tree wouldn't be flatterned (might be
		 * changed in the future), instead they are freed within
		 * dbtree/evtree_drain(), so user credits should be consumed.
		 */
		D_ASSERT(*credits >= creds);
		*credits = creds;
	}
	return 0;
}

static int
gc_free_item(struct vos_gc *gc, struct vos_pool *pool, struct vos_gc_item *item)
{
	struct vos_gc_bin_df *bin = gc_type2bin(pool, gc->gc_type);
	struct vos_gc_bag_df *bag;
	int		      first;
	int		      rc = 0;

	bag = umem_off2ptr(&pool->vp_umm, bin->bin_bag_first);
	D_ASSERT(bag && bag->bag_item_nr > 0);
	D_ASSERT(item == &bag->bag_items[bag->bag_item_first]);

	first = bag->bag_item_first + 1;
	if (first == bin->bin_bag_size)
		first = 0;

	if (first == bag->bag_item_last) {
		/* it's going to be a empty bag */
		D_ASSERT(bag->bag_item_nr == 1);
		rc = gc_bin_free_bag(&pool->vp_umm, bin, bin->bin_bag_first);
	} else {
		rc = umem_tx_add_ptr(&pool->vp_umm, bag, sizeof(*bag));
		if (rc)
			goto failed;

		bag->bag_item_first = first;
		bag->bag_item_nr--;
	}

	D_DEBUG(DB_TRACE, "GC released a %s\n", gc->gc_name);
	/* this is the real container|object|dkey|akey free */
	if (gc->gc_free)
		gc->gc_free(gc, pool, item);
	else
		umem_free(&pool->vp_umm, item->it_addr);

	switch (gc->gc_type) {
	default:
		D_ASSERT(0);
		break;
	case GC_AKEY:
		pool->vp_gc_stat.gs_akeys++;
		break;
	case GC_DKEY:
		pool->vp_gc_stat.gs_dkeys++;
		break;
	case GC_OBJ:
		pool->vp_gc_stat.gs_objs++;
		break;
	case GC_CONT:
		pool->vp_gc_stat.gs_conts++;
		break;
	}
failed:
	return rc;
}

/**
 * Add an item for garbage collection, this item and all its sub-items will
 * be freed by vos_gc_run.
 *
 * NB: this function must be called within pmdk transactoin.
 */
int
gc_add_item(struct vos_pool *pool, enum vos_gc_type type, umem_off_t item_off,
	    uint64_t args)
{
	struct vos_gc_bin_df *bin = gc_type2bin(pool, type);
	struct vos_gc_item    item;

	D_DEBUG(DB_TRACE, "Add %s addr="DF_X64"\n",
		gc_type2name(type), item_off);

	if (pool->vp_dying)
		return 0; /* OK to ignore because the pool is being deleted */

	item.it_addr = item_off;
	item.it_args = args;
	while (1) {
		int	  creds = GC_CREDS_PRIV;
		int	  rc;
		bool	  empty;

		rc = gc_bin_add_item(&pool->vp_umm, bin, &item);
		if (rc == 0) {
			if (!gc_have_pool(pool))
				gc_add_pool(pool);
			return 0;
		}

		/* this is unlikely, but if we cannot even queue more items
		 * for GC, it means we should reclaim space for this pool
		 * immediately.
		 */
		if (rc != -DER_NOSPACE) {
			D_CRIT("Failed to add item, pool="DF_UUID", rc=%s\n",
			       DP_UUID(pool->vp_id), d_errstr(rc));
			return rc;
		}

		if (d_list_empty(&pool->vp_gc_link)) {
			D_CRIT("Pool="DF_UUID" is full but nothing for GC\n",
			       DP_UUID(pool->vp_id));
			return rc;
		}

		rc = gc_reclaim_pool(pool, &creds, &empty);
		if (rc) {
			D_CRIT("Cannot run RC for pool="DF_UUID", rc=%s\n",
			       DP_UUID(pool->vp_id), d_errstr(rc));
			return rc;
		}

		if (creds == GC_CREDS_PRIV) { /* recliamed nothing? */
			D_CRIT("Failed to recliam space for pool="DF_UUID"\n",
			       DP_UUID(pool->vp_id));
			return -DER_NOSPACE;
		}
	}
}

/**
 * Run garbage collector for a pool, it returns if all @credits are consumed
 * or there is nothing to be reclaimed.
 */
static int
gc_reclaim_pool(struct vos_pool *pool, int *credits, bool *empty_ret)
{
	struct vos_gc	*gc    = &gc_table[0]; /* start from akey */
	int		 creds = *credits;
	int		 rc;

	if (pool->vp_dying) {
		*empty_ret = true;
		return 0;
	}

	rc = umem_tx_begin(&pool->vp_umm, NULL);
	if (rc) {
		D_ERROR("Failed to start transacton for "DF_UUID": %s\n",
			DP_UUID(pool->vp_id), d_errstr(rc));
		return rc;
	}

	*empty_ret = false;
	while (creds > 0) {
		struct vos_gc_item *item;
		bool		    empty = false;

		D_DEBUG(DB_TRACE, "GC=%s credits=%d/%d\n", gc->gc_name,
			creds, *credits);

		item = gc_get_item(gc, pool);
		if (item == NULL) {
			if (gc->gc_type == GC_CONT) { /* top level GC */
				D_DEBUG(DB_TRACE, "Nothing to reclaim\n");
				*empty_ret = true;
				break;
			}
			D_DEBUG(DB_TRACE, "GC=%s is empty\n", gc->gc_name);
			gc++; /* try upper level tree */
			continue;
		}

		rc = gc_drain_item(gc, pool, item, &creds, &empty);
		if (rc) {
			D_ERROR("GC=%s error=%s\n", gc->gc_name, d_errstr(rc));
			break;
		}

		if (empty && creds) {
			/* item can be released and removed from bin */
			gc_free_item(gc, pool, item);
			creds--;
		}

		D_DEBUG(DB_TRACE, "GC=%s credits=%d empty=%d\n",
			gc->gc_name, creds, empty);

		/* always try to free akeys and values because they are the
		 * items consuming most storage space.
		 */
		if (gc->gc_type == GC_AKEY)
			continue;

		/* should have flattened some items to the child GC, switch
		 * to the child GC.
		 */
		gc--;
	}
	D_DEBUG(DB_TRACE,
		"pool="DF_UUID", creds origin=%d, current=%d, rc=%s\n",
		DP_UUID(pool->vp_id), *credits, creds, d_errstr(rc));

	rc = umem_tx_end(&pool->vp_umm, rc);
	if (rc == 0)
		*credits = creds;

	return rc;
}

/**
 * Initialize garbage bins for a pool.
 *
 * NB: there is no need to free garbage bins, because destroy pool will free
 * them for free.
 */
int
gc_init_pool(struct umem_instance *umm, struct vos_pool_df *pd)
{
	int	i;

	D_DEBUG(DB_IO, "Init garbage bins for pool="DF_UUID"\n",
		DP_UUID(pd->pd_id));

	for (i = 0; i < GC_MAX; i++) {
		struct vos_gc_bin_df *bin = &pd->pd_gc_bins[i];

		bin->bin_bag_first = UMOFF_NULL;
		bin->bin_bag_last  = UMOFF_NULL;
		bin->bin_bag_size  = gc_bag_size;
		bin->bin_bag_nr	   = 0;
	}
	return 0;
}

/**
 * Attach a pool for GC, this function also pins the pool in open hash table.
 * GC will remove this pool from open hash if it has nothing left for GC and
 * user has already closed it.
 */
int
gc_add_pool(struct vos_pool *pool)
{
	struct vos_tls	   *tls = vos_tls_get();

	D_DEBUG(DB_TRACE, "Register pool="DF_UUID" for GC\n",
		DP_UUID(pool->vp_id));

	D_ASSERT(d_list_empty(&pool->vp_gc_link));

	pool->vp_opened++; /* pin the vos_pool in open-hash */
	vos_pool_addref(pool); /* +1 for the link */
	d_list_add_tail(&pool->vp_gc_link, &tls->vtl_gc_pools);
	return 0;
}

/**
 * Detach a pool for GC
 * NB: this function should NOT be called while closing a pool, it's called
 * when a pool is being destroyed
 */
void
gc_del_pool(struct vos_pool *pool)
{
	D_ASSERT(pool->vp_opened > 0);
	D_ASSERT(!d_list_empty(&pool->vp_gc_link));

	pool->vp_opened--;
	if (pool->vp_opened == 0)
		vos_pool_hash_del(pool); /* un-pin from open-hash */

	d_list_del_init(&pool->vp_gc_link);
	vos_pool_decref(pool); /* -1 for the link */
}

bool
gc_have_pool(struct vos_pool *pool)
{
	return !d_list_empty(&pool->vp_gc_link);
}

static void
gc_log_pool(struct vos_pool *pool)
{
	struct vos_gc_stat *stat = &pool->vp_gc_stat;

	D_DEBUG(DB_TRACE,
		"Pool="DF_UUID", GC reclaimed:\n"
		"  containers = "DF_U64"\n"
		"  objects    = "DF_U64"\n"
		"  dkeys      = "DF_U64"\n"
		"  akeys      = "DF_U64"\n"
		"  singvs     = "DF_U64"\n"
		"  recxs      = "DF_U64"\n",
		DP_UUID(pool->vp_id),
		stat->gs_conts, stat->gs_objs,
		stat->gs_dkeys, stat->gs_akeys,
		stat->gs_singvs, stat->gs_recxs);
}

/**
 * Resource reclaim for all opened VOS pool.
 * This function returns when there is nothing to reclaim or consumed all
 * credits. It returns the remainded credits.
 */
int
vos_gc_run(int *credits)
{
	struct vos_tls	*tls	 = vos_tls_get();
	d_list_t	*pools	 = &tls->vtl_gc_pools;
	int		 rc	 = 0;
	int		 checked = 0;
	int		 creds;

	creds = *credits;
	if (creds < GC_CREDS_MIN || creds > GC_CREDS_MAX) {
		D_ERROR("Invalid credits=%d\n", creds);
		return -DER_INVAL;
	}

	if (d_list_empty(pools)) {
		/* Garbage collection has nothing to do.  Just return without
		 * logging.  Otherwise, tests produce huge logs with little
		 * useful information when trace debug bit is set.
		 */
		return 0;
	}

	while (!d_list_empty(pools)) {
		struct vos_pool *pool;
		bool		 empty = false;

		pool = d_list_entry(pools->next, struct vos_pool, vp_gc_link);
		D_DEBUG(DB_TRACE, "GC pool="DF_UUID", creds=%d\n",
			DP_UUID(pool->vp_id), creds);

		rc = gc_reclaim_pool(pool, &creds, &empty);
		if (rc) {
			D_ERROR("GC pool="DF_UUID" error=%s\n",
				DP_UUID(pool->vp_id), d_errstr(rc));
			break;
		}
		checked++;
		if (empty) {
			D_DEBUG(DB_TRACE,
				"Deregister pool="DF_UUID", empty=%d\n",
				DP_UUID(pool->vp_id), empty);
			gc_log_pool(pool);
			gc_del_pool(pool);

		} else {
			D_DEBUG(DB_TRACE, "Re-add pool="DF_UUID", opened=%d\n",
				DP_UUID(pool->vp_id), pool->vp_opened);

			d_list_move_tail(&pool->vp_gc_link, pools);
		}

		if (creds == 0)
			break; /* consumed all credits */
	}
	D_DEBUG(DB_TRACE, "checked %d pools, consumed %d/%d credits\n",
		checked, *credits - creds, *credits);

	*credits = creds;
	return rc;
}

/**
 * Function for VOS standalone mode, it reclaims all the deleted items.
 */
void
gc_wait(void)
{
#if VOS_STANDALONE
	int total = 0;

	while (1) {
		int creds = GC_CREDS_PRIV;
		int rc;

		total += creds;
		rc = vos_gc_run(&creds);
		if (rc) {
			D_CRIT("GC failed %s\n", d_errstr(rc));
			return;
		}

		if (creds != 0) {
			D_DEBUG(DB_TRACE, "Consumed %d credits\n",
				total - creds);
			return;
		}
	}
#endif
}

/** public API to reclaim space for a opened pool */
int
vos_gc_pool(daos_handle_t poh, int *credits)
{
	struct vos_pool *pool = vos_hdl2pool(poh);
	bool		 empty;
	int		 total;
	int		 rc;

	if (!credits || *credits <= 0)
		return -DER_INVAL;

	if (!pool)
		return -DER_NO_HDL;

	if (d_list_empty(&pool->vp_gc_link))
		return 0; /* nothing to reclaim for this pool */

	total = *credits;
	rc = gc_reclaim_pool(pool, credits, &empty);
	if (rc) {
		D_CRIT("GC failed %s\n", d_errstr(rc));
		return 0; /* caller can't do anything for it */
	}
	total -= *credits; /* substract the remained credits */

	if (empty && total != 0) /* did something */
		gc_log_pool(pool);

	return 0;
}
