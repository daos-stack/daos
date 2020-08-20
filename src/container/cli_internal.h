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
 * dc_cont: Container Client Internal Declarations
 */

#ifndef __CONTAINER_CLIENT_INTERNAL_H__
#define __CONTAINER_CLIENT_INTERNAL_H__


/* Client container handle */
struct dc_cont {
	/** link chain in the global handle hash table */
	struct d_hlink		dc_hlink;
	/* list to pool */
	d_list_t		dc_po_list;
	/* object list for this container */
	d_list_t		dc_obj_list;
	/* lock for list of dc_obj_list */
	pthread_rwlock_t	dc_obj_list_lock;
	/* uuid for this container */
	uuid_t			dc_uuid;
	uuid_t			dc_cont_hdl;
	uint64_t		dc_capas;
	/* pool handler of the container */
	daos_handle_t		dc_pool_hdl;
	struct daos_csummer    *dc_csummer;
	struct cont_props	dc_props;
	uint32_t		dc_closing:1,
				dc_slave:1; /* generated via g2l */
};

static inline struct dc_cont *
dc_hdl2cont(daos_handle_t coh)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(coh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_cont, dc_hlink);
}

static inline void
dc_cont2hdl(struct dc_cont *dc, daos_handle_t *hdl)
{
	daos_hhash_link_getref(&dc->dc_hlink);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

void dc_cont_hdl_link(struct dc_cont *dc);
void dc_cont_hdl_unlink(struct dc_cont *dc);

struct dc_cont *dc_cont_alloc(const uuid_t uuid);
void dc_cont_put(struct dc_cont *dc);
int dc_epoch_op(daos_handle_t coh, crt_opcode_t opc, daos_epoch_t *epoch,
		tse_task_t *task);

#endif /* __CONTAINER_CLIENT_INTERNAL_H__ */
