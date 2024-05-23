/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_TEST_PERF_H
#define DAOS_TEST_PERF_H

static inline void
noop(void)
{
}

/*
 * Measure time spent on a function. The first argument is the full function,
 * parameters and all. The second and third arguments are setup/teardown needed for the
 * target function. If no setup/teardown is needed, the noop() function can be used.
 *
 * Example:
 *
 * MEASURE_TIME(daos_csummer_alloc_iods_csums(csummer, iods, iod_nr, false, NULL, &iod_csums),
 *              noop(),
 *              daos_csummer_free_ic(csummer, &iod_csums));
 *
 * The function being measured is daos_csummer_alloc_iods_csums. There is no setup needed, but
 * because daos_csummer_alloc_iods_csums allocates memory, it should be freed after each call.
 *
 */
#define MEASURE_TIME(fn, pre, post)                                                                \
	do {                                                                                       \
		uint64_t        __i, __elapsed_ns = 0;                                             \
		const uint32_t  __iterations = 10000;                                              \
		struct timespec __start_time, __end_time;                                          \
		for (__i = 0; __i < __iterations; __i++) {                                         \
			pre;                                                                       \
			d_gettime(&__start_time);                                                  \
			fn;                                                                        \
			d_gettime(&__end_time);                                                    \
			__elapsed_ns += d_timediff_ns(&__start_time, &__end_time);                 \
			post;                                                                      \
		}                                                                                  \
		D_PRINT(#fn ":\t%lu ns\n", __elapsed_ns / __iterations);                           \
	} while (0)

#endif /* DAOS_TEST_PERF_H */
