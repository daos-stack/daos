/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf_priv.h"
#include "ocf_cache_priv.h"
#include "utils/utils_cache_line.h"

static inline ocf_cache_line_t ocf_atomic_addr2line(
		struct ocf_cache *cache, uint64_t addr)
{
	addr -= cache->device->metadata_offset;
	addr = ocf_bytes_2_lines(cache, addr);
	return ocf_metadata_map_phy2lg(cache, addr);
}

static inline uint8_t ocf_atomic_addr2pos(struct ocf_cache *cache,
		uint64_t addr)
{
	addr -= cache->device->metadata_offset;
	addr = BYTES_TO_SECTORS(addr);
	addr %= ocf_line_sectors(cache);

	return addr;
}

int ocf_metadata_get_atomic_entry(ocf_cache_t cache,
		uint64_t addr, struct ocf_atomic_metadata *entry)
{
	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(entry);

	if (addr > ocf_volume_get_length(&cache->device->volume))
		return -OCF_ERR_INVAL;

	if (addr < cache->device->metadata_offset) {
		/* Metadata IO of OCF */
		ENV_BUG_ON(env_memset(entry, sizeof(*entry), 0));
	} else {
		ocf_cache_line_t line = ocf_atomic_addr2line(cache, addr);
		uint8_t pos = ocf_atomic_addr2pos(cache, addr);
		ocf_core_id_t core_id = OCF_CORE_MAX;
		ocf_core_t core;
		uint64_t core_line = 0;

		ocf_metadata_get_core_info(cache, line, &core_id, &core_line);
		core = ocf_cache_get_core(cache, core_id);

		entry->core_seq_no = core->conf_meta->seq_no;
		entry->core_line = core_line;

		entry->valid = metadata_test_valid_one(cache, line, pos);
		entry->dirty = metadata_test_dirty_one(cache, line, pos);
	}

	return 0;
}

int ocf_metadata_check_invalid_before(ocf_cache_t cache, uint64_t addr)
{
	ocf_cache_line_t line;
	uint8_t pos;
	int i;

	OCF_CHECK_NULL(cache);

	line = ocf_atomic_addr2line(cache, addr);
	pos = ocf_atomic_addr2pos(cache, addr);

	if (!pos || addr < cache->device->metadata_offset)
		return 0;

	for (i = 0; i < pos; i++) {
		if (metadata_test_valid_one(cache, line, i))
			return 0;
	}

	return i;
}

int ocf_metadata_check_invalid_after(ocf_cache_t cache, uint64_t addr,
		uint32_t bytes)
{
	ocf_cache_line_t line;
	uint8_t pos;
	int i, count = 0;

	OCF_CHECK_NULL(cache);

	line = ocf_atomic_addr2line(cache, addr + bytes);
	pos = ocf_atomic_addr2pos(cache, addr + bytes);

	if (!pos || addr < cache->device->metadata_offset)
		return 0;

	for (i = pos; i < ocf_line_sectors(cache); i++) {
		if (metadata_test_valid_one(cache, line, i))
			return 0;

		count++;
	}

	return count;
}
