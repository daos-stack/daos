/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "../ocf_priv.h"
#include "../ocf_core_priv.h"
#include "../ocf_ctx_priv.h"

void ocf_mngt_core_pool_init(ocf_ctx_t ctx)
{
	OCF_CHECK_NULL(ctx);
	INIT_LIST_HEAD(&ctx->core_pool.core_pool_head);
}

int ocf_mngt_core_pool_get_count(ocf_ctx_t ctx)
{
	int count;
	OCF_CHECK_NULL(ctx);
	env_rmutex_lock(&ctx->lock);
	count = ctx->core_pool.core_pool_count;
	env_rmutex_unlock(&ctx->lock);
	return count;
}

int ocf_mngt_core_pool_add(ocf_ctx_t ctx, ocf_uuid_t uuid, uint8_t type)
{
	ocf_volume_t volume;

	int result = 0;

	OCF_CHECK_NULL(ctx);

	result = ocf_ctx_volume_create(ctx, &volume, uuid, type);
	if (result)
		return result;

	result = ocf_volume_open(volume, NULL);
	if (result) {
		ocf_volume_deinit(volume);
		return result;
	}

	env_rmutex_lock(&ctx->lock);
	list_add(&volume->core_pool_item, &ctx->core_pool.core_pool_head);
	ctx->core_pool.core_pool_count++;
	env_rmutex_unlock(&ctx->lock);
	return result;
}

int ocf_mngt_core_pool_visit(ocf_ctx_t ctx,
		int (*visitor)(ocf_uuid_t, void *), void *visitor_ctx)
{
	int result = 0;
	ocf_volume_t svolume;

	OCF_CHECK_NULL(ctx);
	OCF_CHECK_NULL(visitor);

	env_rmutex_lock(&ctx->lock);
	list_for_each_entry(svolume, &ctx->core_pool.core_pool_head,
			core_pool_item) {
		result = visitor(&svolume->uuid, visitor_ctx);
		if (result)
			break;
	}
	env_rmutex_unlock(&ctx->lock);
	return result;
}

ocf_volume_t ocf_mngt_core_pool_lookup(ocf_ctx_t ctx, ocf_uuid_t uuid,
		ocf_volume_type_t type)
{
	ocf_volume_t svolume;

	OCF_CHECK_NULL(ctx);
	OCF_CHECK_NULL(uuid);
	OCF_CHECK_NULL(uuid->data);

	list_for_each_entry(svolume, &ctx->core_pool.core_pool_head,
			core_pool_item) {
		if (svolume->type == type && !env_strncmp(svolume->uuid.data,
			svolume->uuid.size, uuid->data, uuid->size)) {
			return svolume;
		}
	}

	return NULL;
}

void ocf_mngt_core_pool_remove(ocf_ctx_t ctx, ocf_volume_t volume)
{
	OCF_CHECK_NULL(ctx);
	OCF_CHECK_NULL(volume);
	env_rmutex_lock(&ctx->lock);
	ctx->core_pool.core_pool_count--;
	list_del(&volume->core_pool_item);
	env_rmutex_unlock(&ctx->lock);
	ocf_volume_destroy(volume);
}

void ocf_mngt_core_pool_deinit(ocf_ctx_t ctx)
{
	ocf_volume_t svolume, tvolume;

	OCF_CHECK_NULL(ctx);

	list_for_each_entry_safe(svolume, tvolume, &ctx->core_pool.core_pool_head,
			core_pool_item) {
		ocf_volume_close(svolume);
		ocf_mngt_core_pool_remove(ctx, svolume);
	}
}
