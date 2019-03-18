/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of daos_sr
 *
 * src/placement/mapless_jump_map.h
 */

#ifndef __MAPLESS_JUMP_MAP_H__
#define __MAPLESS_JUMP_MAP_H__

#include <inttypes.h>

/**
 * Jump Consistent Hash Algorithm that provides a bucket location
 * for the given key. This algorithm hashes a minimal (1/n) number
 * of keys to a new bucket when extending the number of buckets.
 *
 * \param[in]   key             A unique key representing the object that
 *                              will be placed in the bucket.
 * \param[in]   num_buckets     The total number of buckets the hashing
 *                              algorithm can choose from.
 *
 * \return                      Returns an index ranging from 0 to
 *                              num_buckets representing the bucket
 *                              the given key hashes to.
 */
static inline uint32_t
jump_consistent_hash(uint64_t key, uint32_t num_buckets)
{
	int64_t z = -1;
	int64_t y = 0;

	while (y < num_buckets) {
		z = y;
		key = key * 2862933555777941757ULL + 1;
		y = (z + 1) * ((double)(1LL << 31) /
			       ((double)((key >> 33) + 1)));
	}
	return z;
}

/**
 * Assembly instruction for fast 4-byte CRC computation on Intel X86
 * Copied from BSD-licensed Data Plane Development Kit (DPDK) rte_hash_crc.h
 *
 * \param[in] data              Primary input to hash function
 * \param[in] init_value        Acts like a seed to the CRC, useful to get
 *                              different results with the same data
 */
static inline uint32_t
crc32c_sse42_u32(uint32_t data, uint32_t init_val)
{
	__asm__ volatile(
		"crc32l %[data], %[init_val];"
		: [init_val] "+r"(init_val)
		: [data] "rm"(data));
	return init_val;
}

/**
 * Computes an 8-byte CRC using fast assembly instructions.
 *
 * This is useful for mapless placement to pseudorandomly permute input keys
 * that are similar to each other. This dramatically improves the even-ness of
 * the distribution of output placements.
 *
 * Since CRC is typically a 4-byte operation, this by computes a 4-byte CRC of
 * each half of the input data and concatenates them
 *
 * \param[in] data              Primary input to hash function
 * \param[in] init_value        Acts like a seed to the CRC, useful to get
 *                              different results with the same data
 */
static inline uint64_t
crc(uint64_t data, uint32_t init_val)
{
	return (uint64_t)crc32c_sse42_u32((data & 0xFFFFFFFF), init_val)
	       | ((uint64_t)crc32c_sse42_u32(((data >> 32) & 0xFFFFFFFF),
					     init_val) << 32);
}

#endif /* __MAPLESS_JUMP_MAP_H__ */
