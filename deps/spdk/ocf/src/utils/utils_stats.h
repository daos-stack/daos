/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_STATS_H_
#define UTILS_STATS_H_

#define _ocf_stats_zero(stats) \
    do { \
        if (stats) { \
            typeof(*stats) zero = { { 0 } }; \
            *stats = zero; \
        } \
    } while (0)

static inline uint64_t _fraction(uint64_t numerator, uint64_t denominator)
{
    uint64_t result;
    if (denominator) {
        result = 10000 * numerator / denominator;
    } else {
        result = 0;
    }
    return result;
}

static inline uint64_t _lines4k(uint64_t size,
        ocf_cache_line_size_t cache_line_size)
{
    long unsigned int result;

    result = size * (cache_line_size / 4096);

    return result;
}

static inline uint64_t _bytes4k(uint64_t bytes)
{
    return (bytes + 4095UL) >> 12;
}

static inline void _set(struct ocf_stat *stat, uint64_t value,
		uint64_t denominator)
{
    stat->value = value;
    stat->fraction = _fraction(value, denominator);
}

#endif
