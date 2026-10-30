/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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

/*
 * d_vlog() silently truncates a log line at DLOG_TBSIZ (1024) bytes including its header, so
 * dc_pool_log_ranks() logs a long rank range string in pieces of at most this many bytes,
 * split after a ','.
 */
#define DC_POOL_RANKS_LOG_CHUNK 512

/* Log non-NULL \a ranks as a rank range string at DB_MD debug level, if enabled. */
void
dc_pool_log_ranks(uuid_t pool_uuid, d_rank_list_t *ranks, bool enabled);

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
