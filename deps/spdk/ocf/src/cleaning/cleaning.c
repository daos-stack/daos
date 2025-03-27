/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cleaning.h"
#include "alru.h"
#include "nop.h"
#include "acp.h"
#include "../ocf_priv.h"
#include "../ocf_cache_priv.h"
#include "../ocf_ctx_priv.h"
#include "../mngt/ocf_mngt_common.h"
#include "../metadata/metadata.h"
#include "../ocf_queue_priv.h"
#include "cleaning_ops.h"

int ocf_start_cleaner(ocf_cache_t cache)
{
	return ctx_cleaner_init(cache->owner, &cache->cleaner);
}

void ocf_stop_cleaner(ocf_cache_t cache)
{
	ctx_cleaner_stop(cache->owner, &cache->cleaner);
}

void ocf_kick_cleaner(ocf_cache_t cache)
{
	ctx_cleaner_kick(cache->owner, &cache->cleaner);
}

void ocf_cleaner_set_cmpl(ocf_cleaner_t cleaner, ocf_cleaner_end_t fn)
{
	cleaner->end = fn;
}

void ocf_cleaner_set_priv(ocf_cleaner_t c, void *priv)
{
	OCF_CHECK_NULL(c);
	c->priv = priv;
}

void *ocf_cleaner_get_priv(ocf_cleaner_t c)
{
	OCF_CHECK_NULL(c);
	return c->priv;
}

ocf_cache_t ocf_cleaner_get_cache(ocf_cleaner_t c)
{
	OCF_CHECK_NULL(c);
	return container_of(c, struct ocf_cache, cleaner);
}

static int _ocf_cleaner_run_check_dirty_inactive(ocf_cache_t cache)
{
	ocf_core_t core;
	ocf_core_id_t core_id;

	if (!env_bit_test(ocf_cache_state_incomplete, &cache->cache_state))
		return 0;

	for_each_core(cache, core, core_id) {
		if (core->opened && ocf_mngt_core_is_dirty(core)) {
			return 0;
		}
	}

	return 1;
}

static void ocf_cleaner_run_complete(ocf_cleaner_t cleaner, uint32_t interval)
{
	ocf_cache_t cache = ocf_cleaner_get_cache(cleaner);

	ocf_mngt_cache_unlock(cache);
	ocf_queue_put(cleaner->io_queue);
	cleaner->end(cleaner, interval);
}

void ocf_cleaner_run(ocf_cleaner_t cleaner, ocf_queue_t queue)
{
	ocf_cache_t cache;

	OCF_CHECK_NULL(cleaner);
	OCF_CHECK_NULL(queue);

	cache = ocf_cleaner_get_cache(cleaner);

	/* Do not involve cleaning when cache is not running
	 * (error, etc.).
	 */
	if (!env_bit_test(ocf_cache_state_running, &cache->cache_state) ||
			ocf_mngt_cache_is_locked(cache)) {
		cleaner->end(cleaner, SLEEP_TIME_MS);
		return;
	}

	/* Sleep in case there is management operation in progress. */
	if (ocf_mngt_cache_trylock(cache)) {
		cleaner->end(cleaner, SLEEP_TIME_MS);
		return;
	}

	if (_ocf_cleaner_run_check_dirty_inactive(cache)) {
		ocf_mngt_cache_unlock(cache);
		cleaner->end(cleaner, SLEEP_TIME_MS);
		return;
	}

	ocf_queue_get(queue);
	cleaner->io_queue = queue;

	ocf_cleaning_perform_cleaning(cache, ocf_cleaner_run_complete);
}
