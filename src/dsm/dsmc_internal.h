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
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsmc: Internal Declarations
 */

#ifndef __DSMC_INTERNAL_H__
#define __DSMC_INTERNAL_H__

#include <daos/transport.h>
#include <daos/hash.h>

/* hhash table for all of objects on daos client,
 * pool, container, object etc */
extern struct daos_hhash *dsmc_hhash;

/*
 * Client pool handle
 *
 * This is called a connection to avoid potential confusion with the server
 * pool_hdl.
 */
struct pool_conn {
	struct daos_hlink pc_hlink;
	/* container list of the pool */
	daos_list_t	pc_co_list;
	/* lock for the container list */
	pthread_rwlock_t pc_co_list_lock;
	/* pool uuid */
	uuid_t		pc_pool;
	uuid_t		pc_pool_hdl;
	uint64_t	pc_capas;
	uint32_t	pc_disconnecting:1;
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
	/* pool handler of the container */
	daos_handle_t	  dc_pool_hdl;
	uint32_t	  dc_closing:1;
};

/* dsmc object in dsm client cache */
struct dsmc_object {
	struct daos_hlink do_hlink;
	/* object id */
	daos_unit_oid_t	  do_id;
	/* container handler of the object */
	daos_handle_t	  do_co_hdl;
	/* list to the container */
	daos_list_t	  do_co_list;
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
	/* add pool_conn to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &dc->dc_hlink, 0);
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

static inline struct pool_conn*
dsmc_handle2pool(daos_handle_t pch)
{
	struct daos_hlink *dlink;

	dlink = daos_hhash_link_lookup(dsmc_hhash, pch.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct pool_conn, pc_hlink);
}

static inline void
pool_conn_add_cache(struct pool_conn *pc, daos_handle_t *hdl)
{
	/* add pool_conn to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &pc->pc_hlink, 0);
	daos_hhash_link_key(&pc->pc_hlink, &hdl->cookie);
}

static inline void
pool_conn_del_cache(struct pool_conn *pc)
{
	daos_hhash_link_delete(dsmc_hhash, &pc->pc_hlink);
}

static inline void
pool_conn_put(struct pool_conn *pc)
{
	daos_hhash_link_putref(dsmc_hhash, &pc->pc_hlink);
}

static inline void
dsmc_object_add_cache(struct dsmc_object *dobj, daos_handle_t *hdl)
{
	/* add pool_conn to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &dobj->do_hlink, 0);
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
