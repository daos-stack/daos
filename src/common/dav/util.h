/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2021, Intel Corporation */
/*
 * Copyright (c) 2016-2020, Microsoft Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * util.h -- internal definitions for util module
 */

#ifndef __DAOS_COMMON_UTIL_H
#define __DAOS_COMMON_UTIL_H 1

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <stdatomic.h>
#include <sys/param.h>

#if defined(__x86_64) || defined(_M_X64) || defined(__aarch64__) || \
	defined(__riscv)
#define PAGESIZE 4096
#elif defined(__PPC64__)
#define PAGESIZE 65536
#else
#error unable to recognize ISA at compile time
#endif

#if defined(__x86_64) || defined(_M_X64) || defined(__aarch64__) || \
	defined(__riscv)
#define CACHELINE_SIZE 64ULL
#elif defined(__PPC64__)
#define CACHELINE_SIZE 128ULL
#else
#error unable to recognize architecture at compile time
#endif

#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(size, align) ((size) & ~((align) - 1))

void util_init(void);
int util_is_zeroed(const void *addr, size_t len);
uint64_t util_checksum_compute(void *addr, size_t len, uint64_t *csump,
		size_t skip_off);
int util_checksum(void *addr, size_t len, uint64_t *csump,
		int insert, size_t skip_off);
uint64_t util_checksum_seq(const void *addr, size_t len, uint64_t csum);

#define force_inline __attribute__((always_inline)) inline

typedef uint64_t ua_uint64_t __attribute__((aligned(1)));
typedef uint32_t ua_uint32_t __attribute__((aligned(1)));
typedef uint16_t ua_uint16_t __attribute__((aligned(1)));

/*
 * util_div_ceil -- divides a by b and rounds up the result
 */
static force_inline unsigned
util_div_ceil(unsigned a, unsigned b)
{
	return (unsigned)(((unsigned long)a + b - 1) / b);
}

/*
 * util_bool_compare_and_swap -- perform an atomic compare and swap
 * util_fetch_and_* -- perform an operation atomically, return old value
 * util_popcount -- count number of set bits
 * util_lssb_index -- return index of least significant set bit,
 *			undefined on zero
 * util_mssb_index -- return index of most significant set bit
 *			undefined on zero
 *
 * XXX assertions needed on (value != 0) in both versions of bitscans
 *
 */

/*
 * ISO C11 -- 7.17.7.2 The atomic_load generic functions
 * Integer width specific versions as supplement for:
 *
 *
 * #include <stdatomic.h>
 * C atomic_load(volatile A *object);
 * C atomic_load_explicit(volatile A *object, memory_order order);
 *
 * The atomic_load interface doesn't return the loaded value, but instead
 * copies it to a specified address.
 *
 * void util_atomic_load64(volatile A *object, A *destination);
 * void util_atomic_load_explicit32(volatile A *object, A *destination,
 *                                  memory_order order);
 * void util_atomic_load_explicit64(volatile A *object, A *destination,
 *                                  memory_order order);
 * Also, instead of generic functions, two versions are available:
 * for 32 bit fundamental integers, and for 64 bit ones.
 */

#define util_atomic_load_explicit32 __atomic_load
#define util_atomic_load_explicit64 __atomic_load

/* ISO C11 -- 7.17.7.1 The atomic_store generic functions */
/*
 * ISO C11 -- 7.17.7.1 The atomic_store generic functions
 * Integer width specific versions as supplement for:
 *
 * #include <stdatomic.h>
 * void atomic_store(volatile A *object, C desired);
 * void atomic_store_explicit(volatile A *object, C desired,
 *                            memory_order order);
 */
#define util_atomic_store_explicit32 __atomic_store_n
#define util_atomic_store_explicit64 __atomic_store_n

/*
 * https://gcc.gnu.org/onlinedocs/gcc/_005f_005fsync-Builtins.html
 * https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
 * https://clang.llvm.org/docs/LanguageExtensions.html#builtin-functions
 */
#define util_bool_compare_and_swap64 __sync_bool_compare_and_swap
#define util_fetch_and_add64 __sync_fetch_and_add
#define util_fetch_and_sub64 __sync_fetch_and_sub
#define util_popcount64(value) ((unsigned char)__builtin_popcountll(value))

#define util_lssb_index64(value) ((unsigned char)__builtin_ctzll(value))
#define util_mssb_index64(value) ((unsigned char)(63 - __builtin_clzll(value)))

/* ISO C11 -- 7.17.7 Operations on atomic types */
#define util_atomic_load64(object, dest)\
	util_atomic_load_explicit64(object, dest, memory_order_seq_cst)

#define COMPILE_ERROR_ON(cond) ((void)sizeof(char[(cond) ? -1 : 1]))

/* macro for counting the number of varargs (up to 9) */
#define COUNT(...)\
	COUNT_11TH(_, ##__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define COUNT_11TH(_11, _10, _9, _8, _7, _6, _5, _4, _3, _2,  X, ...) X

/* concatenation macro */
#define GLUE(A, B) GLUE_I(A, B)
#define GLUE_I(A, B) A##B

/* macro for suppresing errors from unused variables (zero to 9) */
#define SUPPRESS_UNUSED(...)\
	GLUE(SUPPRESS_ARG_, COUNT(__VA_ARGS__))(__VA_ARGS__)
#define SUPPRESS_ARG_0(X)
#define SUPPRESS_ARG_1(X) ((void)(X))
#define SUPPRESS_ARG_2(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_1(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_3(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_2(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_4(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_3(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_5(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_4(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_6(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_5(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_7(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_6(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_8(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_7(__VA_ARGS__);\
} while (0)
#define SUPPRESS_ARG_9(X, ...) do {\
	SUPPRESS_ARG_1(X); SUPPRESS_ARG_8(__VA_ARGS__);\
} while (0)

#endif /* __DAOS_COMMON_UTIL_H */
