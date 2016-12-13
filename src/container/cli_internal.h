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

/* Client container handle */
struct dc_cont {
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

static inline struct dc_cont *
dc_cont_lookup(daos_handle_t coh)
{
	struct daos_hlink *dlink;

	if (daos_hhash_key_type(coh.cookie) != DAOS_HTYPE_CO)
		return NULL;

	dlink = daos_hhash_link_lookup(daos_client_hhash, coh.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dc_cont, dc_hlink);
}

static inline void
dc_cont_add_cache(struct dc_cont *dc, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(daos_client_hhash, &dc->dc_hlink, DAOS_HTYPE_CO);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

static inline void
dc_cont_del_cache(struct dc_cont *dc)
{
	daos_hhash_link_delete(daos_client_hhash, &dc->dc_hlink);
}

static inline void
dc_cont_put(struct dc_cont *dc)
{
	daos_hhash_link_putref(daos_client_hhash, &dc->dc_hlink);
}

#endif /* __CONTAINER_CLIENT_INTERNAL_H__ */
