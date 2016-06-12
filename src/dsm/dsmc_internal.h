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
#include <daos/pool_map.h>

/* hhash table for all of objects on daos client,
 * pool, container, object etc */
extern struct daos_hhash *dsmc_hhash;

enum {
	DSMC_GLOB_POOL = 1234,
	DSMC_GLOB_CO,
};

#define DSM_GLOB_HDL_MAGIC	(0x16da0386)

/* Client pool handle */
struct dsmc_pool {
	/* link to dsmc_hhash */
	struct daos_hlink	dp_hlink;
	/* container list of the pool */
	daos_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	struct pool_map	       *dp_map;
	struct pool_buf	       *dp_map_buf;	/* TODO: pool_map => pool_buf */
	uint32_t		dp_disconnecting:1,
				dp_slave:1; /* generated via g2l */
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
	uint32_t	  dc_closing:1,
			  dc_slave:1; /* generated via g2l */
};

struct	dsmc_hdl_glob_hdr {
	/* magic number, DSM_GLOB_HDL_MAGIC */
	uint32_t		hgh_magic;
	/* glob hdl type, must be DSMC_GLOB_POOL or DSMC_GLOB_CO */
	uint32_t		hgh_type;
};

/* Structure of global buffer for dmsc_pool */
struct dsmc_pool_glob {
	struct dsmc_hdl_glob_hdr	dpg_header;
	/* pool uuid and capas */
	uuid_t				dpg_pool;
	uuid_t				dpg_pool_hdl;
	uint64_t			dpg_capas;
	/* poolmap version */
	uint32_t			dpg_map_version;
	/* number of component of poolbuf, same as pool_buf::pb_nr */
	uint32_t			dpg_map_pb_nr;
	struct pool_buf		        dpg_map_buf[0];
};

/* Structure of global buffer for dmsc_container */
struct dsmc_container_glob {
	struct dsmc_hdl_glob_hdr	dcg_header;
	/* pool connection handle */
	uuid_t				dcg_pool_hdl;
	/* container uuid and capas */
	uuid_t				dcg_uuid;
	uuid_t				dcg_cont_hdl;
	uint64_t			dcg_capas;
};

static inline void dsmc_hdl_glob_hdr_init(struct dsmc_hdl_glob_hdr *hdr,
					  uint32_t type)
{
	D_ASSERT(hdr != NULL);
	D_ASSERT(type == DSMC_GLOB_POOL || type == DSMC_GLOB_CO);

	hdr->hgh_magic = DSM_GLOB_HDL_MAGIC;
	hdr->hgh_type = type;
}

static inline daos_size_t
dsmc_pool_glob_buf_size(unsigned int pb_nr)
{
	return offsetof(struct dsmc_pool_glob, dpg_map_buf) +
	       pool_buf_size(pb_nr);
}

static inline daos_size_t
dsmc_container_glob_buf_size()
{
	return sizeof(struct dsmc_container_glob);
}

/* dsmc object in dsm client cache */
struct dsmc_object {
	struct daos_hlink	do_hlink;
	/* rank of the target this object belongs to */
	daos_rank_t		do_rank;
	/* number of service threads running on the target */
	int			do_nr_srv;
	/* object id */
	daos_unit_oid_t		do_id;
	/* container handler of the object */
	daos_handle_t		do_co_hdl;
	/* list to the container */
	daos_list_t		do_co_list;
};

static inline int
dsmc_handle_type(daos_handle_t hdl)
{
	return daos_hhash_key_type(hdl.cookie);
}

static inline struct dsmc_container*
dsmc_handle2container(daos_handle_t hdl)
{
	struct daos_hlink *dlink;

	if (dsmc_handle_type(hdl) != DAOS_HTYPE_CO)
		return NULL;

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

static inline struct dsmc_pool *
dsmc_handle2pool(daos_handle_t poh)
{
	struct daos_hlink *dlink;

	if (dsmc_handle_type(poh) != DAOS_HTYPE_POOL)
		return NULL;

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

int dsmc_co_l2g(daos_handle_t loc, daos_iov_t *glob);

/**
 * Temporary solution for packing the tag into the hash out,
 * which will stay at 25-28 bytes of daos_hash_out_t->body
 */
#define DAOS_HASH_DSM_TAG_OFFSET 24
#define DAOS_HASH_DSM_TAG_LENGTH 4

static inline void
dsmc_hash_hkey_copy(daos_hash_out_t *dst, daos_hash_out_t *src)
{
	memcpy(&dst->body[DAOS_HASH_HKEY_START],
	       &src->body[DAOS_HASH_HKEY_START],
	       DAOS_HASH_HKEY_LENGTH);
}

static inline void
dsmc_hash_set_start(daos_hash_out_t *hash_out)
{
	memset(&hash_out->body[DAOS_HASH_HKEY_START], 0,
	       DAOS_HASH_HKEY_LENGTH);
}

static inline uint32_t
dsmc_hash_get_tag(daos_hash_out_t *anchor)
{
	uint32_t tag;

	D_CASSERT(DAOS_HASH_HKEY_START + DAOS_HASH_HKEY_LENGTH <
		  DAOS_HASH_DSM_TAG_OFFSET);
	memcpy(&tag, &anchor->body[DAOS_HASH_DSM_TAG_OFFSET],
		     DAOS_HASH_DSM_TAG_LENGTH);
	return tag;
}

static inline void
dsmc_hash_set_tag(daos_hash_out_t *anchor, uint32_t tag)
{
	memcpy(&anchor->body[DAOS_HASH_DSM_TAG_OFFSET], &tag,
	       DAOS_HASH_DSM_TAG_LENGTH);
}

#endif /* __DSMC_INTERNAL_H__ */
