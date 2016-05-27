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
 * dsmc: Internal Declarations
 */

#ifndef __DSMC_INTERNAL_H__
#define __DSMC_INTERNAL_H__

#include <daos/transport.h>
#include <daos/hash.h>

/* hhash table for all of objects on daos client,
 * pool, container, object etc */
extern struct daos_hhash *dsmc_hhash;

/* Client pool handle */
struct dsmc_pool {
	struct daos_hlink	dp_hlink;
	/* container list of the pool */
	daos_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	uint32_t		dp_disconnecting:1;
	struct pool_map	       *dp_map;
	struct pool_buf	       *dp_map_buf;	/* TODO: pool_map => pool_buf */
};

/* container in dsm client cache */
struct dsmc_container {
	struct daos_hlink dc_hlink;
	/* list to pool */
	daos_list_t	  dc_po_list;
	/* object list for this container */
	daos_list_t	  dc_obj_list;
	/* lock for list of dc_obj_list */
	pthread_rwlock_t  dc_obj_list_lock;
	/* uuid for this container */
	uuid_t		  dc_uuid;
	uuid_t		  dc_cont_hdl;
	uint64_t	  dc_capas;
	/* pool handler of the container */
	daos_handle_t	  dc_pool_hdl;
	uint32_t	  dc_closing:1;
};

/* dsmc object in dsm client cache */
struct dsmc_object {
	struct daos_hlink	do_hlink;
	/* rank of the target this object belongs to */
	daos_rank_t		do_rank;
	/* object id */
	daos_unit_oid_t		do_id;
	/* container handler of the object */
	daos_handle_t		do_co_hdl;
	/* list to the container */
	daos_list_t		do_co_list;
};

static inline struct dsmc_container*
dsmc_handle2container(daos_handle_t hdl)
{
	struct daos_hlink *dlink;

	dlink = daos_hhash_link_lookup(dsmc_hhash, hdl.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dsmc_container, dc_hlink);
}

static inline void
dsmc_container_add_cache(struct dsmc_container *dc, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &dc->dc_hlink, DAOS_HTYPE_CO);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

static inline void
dsmc_container_del_cache(struct dsmc_container *dc)
{
	daos_hhash_link_delete(dsmc_hhash, &dc->dc_hlink);
}

static inline void
dsmc_container_put(struct dsmc_container *dc)
{
	daos_hhash_link_putref(dsmc_hhash, &dc->dc_hlink);
}

static inline int
dsmc_handle_type(daos_handle_t hdl)
{
	return daos_hhash_key_type(hdl.cookie);
}

static inline struct dsmc_pool *
dsmc_handle2pool(daos_handle_t poh)
{
	struct daos_hlink *dlink;

	dlink = daos_hhash_link_lookup(dsmc_hhash, poh.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dsmc_pool, dp_hlink);
}

static inline void
dsmc_pool_add_cache(struct dsmc_pool *pool, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &pool->dp_hlink, DAOS_HTYPE_POOL);
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
}

static inline void
dsmc_pool_del_cache(struct dsmc_pool *pool)
{
	daos_hhash_link_delete(dsmc_hhash, &pool->dp_hlink);
}

static inline void
dsmc_pool_put(struct dsmc_pool *pool)
{
	daos_hhash_link_putref(dsmc_hhash, &pool->dp_hlink);
}

static inline void
dsmc_object_add_cache(struct dsmc_object *dobj, daos_handle_t *hdl)
{
	/* add obj to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &dobj->do_hlink, DAOS_HTYPE_OBJ);
	daos_hhash_link_key(&dobj->do_hlink, &hdl->cookie);
}

static inline void
dsmc_object_del_cache(struct dsmc_object *dobj)
{
	daos_hhash_link_delete(dsmc_hhash, &dobj->do_hlink);
}

static inline void
dsmc_object_put(struct dsmc_object *dobj)
{
	daos_hhash_link_putref(dsmc_hhash, &dobj->do_hlink);
}

#endif /* __DSMC_INTERNAL_H__ */
