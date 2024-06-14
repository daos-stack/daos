/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_pool: Pool Client Internal Declarations
 */

#ifndef __POOL_CLIENT_INTERNAL_H__
#define __POOL_CLIENT_INTERNAL_H__

void dc_pool_hdl_link(struct dc_pool *pool);
void dc_pool_hdl_unlink(struct dc_pool *pool);
struct dc_pool *dc_pool_alloc(unsigned int nr);

int dc_pool_map_update(struct dc_pool *pool, struct pool_map *map, bool connect);

struct dc_pool_tls {
	pthread_mutex_t dpc_metrics_list_lock;
	d_list_t        dpc_metrics_list;
};

extern struct daos_module_key dc_pool_module_key;

static inline struct dc_pool_tls *
dc_pool_tls_get()
{
	struct daos_thread_local_storage *dtls;

	dtls = dc_tls_get(dc_pool_module_key.dmk_tags);
	D_ASSERT(dtls != NULL);
	return daos_module_key_get(dtls, &dc_pool_module_key);
}
#endif /* __POOL_CLIENT_INTERNAL_H__ */
