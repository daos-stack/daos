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
 * dc_cont: Container Client Internal Declarations
 */

#ifndef __CONTAINER_CLIENT_INTERNAL_H__
#define __CONTAINER_CLIENT_INTERNAL_H__

#include <daos/client.h>
#include <daos/pool_map.h>

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

#define DC_CONT_GLOB_MAGIC	(0x16ca0387)

/* Structure of global buffer for dmsc_container */
struct dsmc_container_glob {
	/* magic number, DC_CONT_GLOB_MAGIC */
	uint32_t	dcg_magic;
	uint32_t	dcg_padding;
	/* pool connection handle */
	uuid_t		dcg_pool_hdl;
	/* container uuid and capas */
	uuid_t		dcg_uuid;
	uuid_t		dcg_cont_hdl;
	uint64_t	dcg_capas;
};

static inline daos_size_t
dsmc_container_glob_buf_size()
{
       return sizeof(struct dsmc_container_glob);
}

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

	dlink = daos_hhash_link_lookup(daos_client_hhash, hdl.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dsmc_container, dc_hlink);
}

static inline void
dsmc_container_add_cache(struct dsmc_container *dc, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(daos_client_hhash, &dc->dc_hlink, DAOS_HTYPE_CO);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

static inline void
dsmc_container_del_cache(struct dsmc_container *dc)
{
	daos_hhash_link_delete(daos_client_hhash, &dc->dc_hlink);
}

static inline void
dsmc_container_put(struct dsmc_container *dc)
{
	daos_hhash_link_putref(daos_client_hhash, &dc->dc_hlink);
}

int dsmc_co_l2g(daos_handle_t loc, daos_iov_t *glob);

#endif /* __CONTAINER_CLIENT_INTERNAL_H__ */
