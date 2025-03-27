/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "../ocf_priv.h"
#include "../metadata/metadata.h"
#include "../engine/cache_engine.h"
#include "../ocf_ctx_priv.h"

uint32_t ocf_mngt_cache_get_count(ocf_ctx_t ctx)
{
	struct ocf_cache *cache;
	uint32_t count = 0;

	OCF_CHECK_NULL(ctx);

	env_rmutex_lock(&ctx->lock);

	/* currently, there are no macros in list.h to get list size.*/
	list_for_each_entry(cache, &ctx->caches, list)
		count++;

	env_rmutex_unlock(&ctx->lock);

	return count;
}
