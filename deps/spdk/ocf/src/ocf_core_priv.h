/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_CORE_PRIV_H__
#define __OCF_CORE_PRIV_H__

#include "ocf/ocf.h"
#include "ocf_env.h"
#include "ocf_ctx_priv.h"
#include "ocf_volume_priv.h"
#include "ocf_seq_cutoff.h"

#define ocf_core_log_prefix(core, lvl, prefix, fmt, ...) \
	ocf_cache_log_prefix(ocf_core_get_cache(core), lvl, ".%s" prefix, \
			fmt, ocf_core_get_name(core), ##__VA_ARGS__)

#define ocf_core_log(core, lvl, fmt, ...) \
	ocf_core_log_prefix(core, lvl, ": ", fmt, ##__VA_ARGS__)

struct ocf_metadata_uuid {
	uint32_t size;
	uint8_t data[OCF_VOLUME_UUID_MAX_SIZE];
} __packed;

#define OCF_CORE_USER_DATA_SIZE 64

struct ocf_core_meta_config {
	char name[OCF_CORE_NAME_SIZE];

	uint8_t type;

	/* This bit means that object was saved in cache metadata */
	uint32_t valid : 1;

	/* Core sequence number used to correlate cache lines with cores
	 * when recovering from atomic device */
	ocf_seq_no_t seq_no;

	/* Sequential cutoff threshold (in bytes) */
	env_atomic seq_cutoff_threshold;

	/* Sequential cutoff policy */
	env_atomic seq_cutoff_policy;

	/* Sequential cutoff stream promotion request count */
	env_atomic seq_cutoff_promo_count;

	/* core object size in bytes */
	uint64_t length;

	uint8_t user_data[OCF_CORE_USER_DATA_SIZE];
};

struct ocf_core_meta_runtime {
	/* Number of blocks from that objects that currently are cached
	 * on the caching device.
	 */
	env_atomic cached_clines;
	env_atomic dirty_clines;
	env_atomic initial_dirty_clines;

	env_atomic64 dirty_since;

	struct {
		/* clines within lru list (?) */
		env_atomic cached_clines;
		/* dirty clines assigned to this specific partition within
		 * cache device
		 */
		env_atomic dirty_clines;
	} part_counters[OCF_USER_IO_CLASS_MAX];
};

struct ocf_core {
	struct ocf_volume front_volume;
	struct ocf_volume volume;

	struct ocf_core_meta_config *conf_meta;
	struct ocf_core_meta_runtime *runtime_meta;

	struct ocf_seq_cutoff *seq_cutoff;

	env_atomic flushed;

	/* This bit means that core volume is initialized */
	uint32_t has_volume : 1;
	/* This bit means that core volume is open */
	uint32_t opened : 1;
	/* This bit means that core is added into cache */
	uint32_t added : 1;

	struct ocf_counters_core *counters;

	void *priv;
};

bool ocf_core_is_valid(ocf_cache_t cache, ocf_core_id_t id);

ocf_core_id_t ocf_core_get_id(ocf_core_t core);

int ocf_core_volume_type_init(ocf_ctx_t ctx);

#endif /* __OCF_CORE_PRIV_H__ */
