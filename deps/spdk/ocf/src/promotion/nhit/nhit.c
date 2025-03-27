/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "nhit_hash.h"
#include "../../metadata/metadata.h"
#include "../../ocf_priv.h"
#include "../../engine/engine_common.h"

#include "nhit.h"
#include "../ops.h"

#define NHIT_MAPPING_RATIO 2

struct nhit_policy_context {
	nhit_hash_t hash_map;
};

void nhit_setup(ocf_cache_t cache)
{
	struct nhit_promotion_policy_config *cfg;

	cfg = (void *) &cache->conf_meta->promotion[ocf_promotion_nhit].data;

	cfg->insertion_threshold = OCF_NHIT_THRESHOLD_DEFAULT;
	cfg->trigger_threshold = OCF_NHIT_TRIGGER_DEFAULT;
}

static uint64_t nhit_sizeof(ocf_cache_t cache)
{
	uint64_t size = 0;

	size += sizeof(struct nhit_policy_context);
	size += nhit_hash_sizeof(ocf_metadata_get_cachelines_count(cache) *
			NHIT_MAPPING_RATIO);

	return size;
}

ocf_error_t nhit_init(ocf_cache_t cache)
{
	struct nhit_policy_context *ctx;
	int result = 0;
	uint64_t available, size;

	size = nhit_sizeof(cache);
	available = env_get_free_memory();

	if (size >= available) {
		ocf_cache_log(cache, log_err, "Not enough memory to "
				"initialize 'nhit' promotion policy! "
				"Required %lu, available %lu\n",
				(long unsigned)size,
				(long unsigned)available);

		return -OCF_ERR_NO_FREE_RAM;
	}

	ctx = env_vmalloc(sizeof(*ctx));
	if (!ctx) {
		result = -OCF_ERR_NO_MEM;
		goto exit;
	}

	result = nhit_hash_init(ocf_metadata_get_cachelines_count(cache) *
			NHIT_MAPPING_RATIO, &ctx->hash_map);
	if (result)
		goto dealloc_ctx;

	cache->promotion_policy->ctx = ctx;
	cache->promotion_policy->config =
		(void *) &cache->conf_meta->promotion[ocf_promotion_nhit].data;

	return 0;

dealloc_ctx:
	env_vfree(ctx);
exit:
	ocf_cache_log(cache, log_err, "Error initializing nhit promotion policy\n");
	return result;
}

void nhit_deinit(ocf_promotion_policy_t policy)
{
	struct nhit_policy_context *ctx = policy->ctx;

	nhit_hash_deinit(ctx->hash_map);

	env_vfree(ctx);
	policy->ctx = NULL;
}

ocf_error_t nhit_set_param(ocf_cache_t cache, uint8_t param_id,
		uint32_t param_value)
{
	struct nhit_promotion_policy_config *cfg;
	ocf_error_t result = 0;

	cfg = (void *) &cache->conf_meta->promotion[ocf_promotion_nhit].data;

	switch (param_id) {
	case ocf_nhit_insertion_threshold:
		if (param_value >= OCF_NHIT_MIN_THRESHOLD &&
				param_value <= OCF_NHIT_MAX_THRESHOLD) {
			cfg->insertion_threshold = param_value;
			ocf_cache_log(cache, log_info,
					"Nhit PP insertion threshold value set to %u",
					param_value);
		} else {
			ocf_cache_log(cache, log_err, "Invalid nhit "
					"promotion policy insertion threshold!\n");
			result = -OCF_ERR_INVAL;
		}
		break;

	case ocf_nhit_trigger_threshold:
		if (param_value >= OCF_NHIT_MIN_TRIGGER &&
				param_value <= OCF_NHIT_MAX_TRIGGER) {
			cfg->trigger_threshold = param_value;
			ocf_cache_log(cache, log_info,
					"Nhit PP trigger threshold value set to %u%%\n",
					param_value);
		} else {
			ocf_cache_log(cache, log_err, "Invalid nhit "
					"promotion policy insertion trigger "
					"threshold!\n");
			result = -OCF_ERR_INVAL;
		}
		break;

	default:
		ocf_cache_log(cache, log_err, "Invalid nhit "
				"promotion policy parameter (%u)!\n",
				param_id);
		result = -OCF_ERR_INVAL;

		break;
	}

	return result;
}

ocf_error_t nhit_get_param(ocf_cache_t cache, uint8_t param_id,
		uint32_t *param_value)
{
	struct nhit_promotion_policy_config *cfg;
	ocf_error_t result = 0;

	cfg = (void *) &cache->conf_meta->promotion[ocf_promotion_nhit].data;

	OCF_CHECK_NULL(param_value);

	switch (param_id) {
	case ocf_nhit_insertion_threshold:
		*param_value = cfg->insertion_threshold;
		break;
	case ocf_nhit_trigger_threshold:
		*param_value = cfg->trigger_threshold;
		break;
	default:
		ocf_cache_log(cache, log_err, "Invalid nhit "
				"promotion policy parameter (%u)!\n",
				param_id);
		result = -OCF_ERR_INVAL;

		break;
	}

	return result;
}

static void core_line_purge(struct nhit_policy_context *ctx, ocf_core_id_t core_id,
		uint64_t core_lba)
{
	nhit_hash_set_occurences(ctx->hash_map, core_id, core_lba, 0);
}

void nhit_req_purge(ocf_promotion_policy_t policy,
		struct ocf_request *req)
{
	struct nhit_policy_context *ctx = policy->ctx;
	uint32_t i;
	uint64_t core_line;

	for (i = 0, core_line = req->core_line_first;
			core_line <= req->core_line_last; core_line++, i++) {
		struct ocf_map_info *entry = &(req->map[i]);

		core_line_purge(ctx, entry->core_id, entry->core_line);
	}
}

static bool core_line_should_promote(ocf_promotion_policy_t policy,
		ocf_core_id_t core_id, uint64_t core_lba)
{
	struct nhit_promotion_policy_config *cfg;
	struct nhit_policy_context *ctx;
	bool hit;
	int32_t counter;

	cfg = (struct nhit_promotion_policy_config*)policy->config;
	ctx = policy->ctx;

	hit = nhit_hash_query(ctx->hash_map, core_id, core_lba, &counter);
	if (hit) {
		/* we have a hit, return now */
		return cfg->insertion_threshold <= counter;
	}

	nhit_hash_insert(ctx->hash_map, core_id, core_lba);

	return false;
}

bool nhit_req_should_promote(ocf_promotion_policy_t policy,
		struct ocf_request *req)
{
	struct nhit_promotion_policy_config *cfg;
	bool result = true;
	uint32_t i;
	uint64_t core_line;
	uint64_t occupied_cachelines =
		ocf_metadata_collision_table_entries(policy->owner) -
		ocf_lru_num_free(policy->owner);

	cfg = (struct nhit_promotion_policy_config*)policy->config;

	if (occupied_cachelines < OCF_DIV_ROUND_UP(
			((uint64_t)cfg->trigger_threshold *
			ocf_metadata_get_cachelines_count(policy->owner)), 100)) {
		return true;
	}

	for (i = 0, core_line = req->core_line_first;
			core_line <= req->core_line_last; core_line++, i++) {
		struct ocf_map_info *entry = &(req->map[i]);

		if (!core_line_should_promote(policy, entry->core_id,
					entry->core_line)) {
			result = false;
		}
	}

	/* We don't want to reject even partially hit requests - this way we
	 * could trigger passthrough and invalidation. Let's let it in! */
	return result || ocf_engine_mapped_count(req);
}

