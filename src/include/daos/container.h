/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont: Container Client API
 */

#ifndef __DD_CONT_H__
#define __DD_CONT_H__

#include <daos/common.h>
#include <daos/pool_map.h>
#include <daos/tse.h>
#include <daos_types.h>
#include <daos/cont_props.h>
#include "checksum.h"

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
	daos_handle_t           dc_pool_hdl;
	struct daos_csummer    *dc_csummer;
	struct cont_props	dc_props;
	/* minimal pmap version */
	uint32_t		dc_min_ver;
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
dc_cont_put(struct dc_cont *dc)
{
	daos_hhash_link_putref(&dc->dc_hlink);
}

static inline void
dc_cont2hdl_noref(struct dc_cont *dc, daos_handle_t *hdl)
{
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

static inline void
dc_cont2hdl(struct dc_cont *dc, daos_handle_t *hdl)
{
	daos_hhash_link_getref(&dc->dc_hlink);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

int dc_cont_init(void);
void dc_cont_fini(void);

int dc_cont_tgt_idx2ptr(daos_handle_t coh, uint32_t tgt_idx,
			struct pool_target **tgt);
int dc_cont_node_id2ptr(daos_handle_t coh, uint32_t node_id,
			struct pool_domain **dom);
int dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *con_uuid);
daos_handle_t dc_cont_hdl2pool_hdl(daos_handle_t coh);
struct daos_csummer *dc_cont_hdl2csummer(daos_handle_t coh);
struct cont_props dc_cont_hdl2props(daos_handle_t coh);
int dc_cont_hdl2redunlvl(daos_handle_t coh, uint32_t *rl);
int dc_cont_hdl2redunfac(daos_handle_t coh, uint32_t *rf);

int dc_cont_local2global(daos_handle_t coh, d_iov_t *glob);
int dc_cont_global2local(daos_handle_t poh, d_iov_t glob,
			 daos_handle_t *coh);

int dc_cont_create(tse_task_t *task);
int dc_cont_open(tse_task_t *task);
int dc_cont_close(tse_task_t *task);
int dc_cont_destroy(tse_task_t *task);
int dc_cont_query(tse_task_t *task);
int dc_cont_set_prop(tse_task_t *task);
int dc_cont_update_acl(tse_task_t *task);
int dc_cont_delete_acl(tse_task_t *task);
int dc_cont_aggregate(tse_task_t *task);
int dc_cont_rollback(tse_task_t *task);
int dc_cont_subscribe(tse_task_t *task);
int dc_cont_list_attr(tse_task_t *task);
int dc_cont_get_attr(tse_task_t *task);
int dc_cont_set_attr(tse_task_t *task);
int dc_cont_del_attr(tse_task_t *task);
int dc_cont_alloc_oids(tse_task_t *task);
int dc_cont_list_snap(tse_task_t *task);
int dc_cont_create_snap(tse_task_t *task);
int dc_cont_destroy_snap(tse_task_t *task);

#endif /* __DD_CONT_H__ */
