/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2020 Intel Corporation
 */
/*
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/** Copied from DPDK (arch/x86/rte_mem{cpy,cmp}.h) */

#ifndef __GURT_MEM_H__
#define __GURT_MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

/**
 * \file
 * Optimized mem{cmp,cpy} functions
 */

/** @addtogroup GURT_MEM
 * @{
 */

#ifdef __AVX2__

inline __attribute__((always_inline)) int
d_cmp32(const void *src_1, const void *src_2)
{
	__m256i    ff = _mm256_set1_epi32(-1);
	__m256i    idx = _mm256_setr_epi8(
			15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
			15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
	__m256i    sign = _mm256_set1_epi32(0x80000000);
	__m256i    mm11, mm21;
	__m256i    eq, gt0, gt1;

	mm11 = _mm256_lddqu_si256((const __m256i *)src_1);
	mm21 = _mm256_lddqu_si256((const __m256i *)src_2);

	eq = _mm256_cmpeq_epi32(mm11, mm21);
	/* Not equal */
	if (!_mm256_testc_si256(eq, ff)) {
		mm11 = _mm256_shuffle_epi8(mm11, idx);
		mm21 = _mm256_shuffle_epi8(mm21, idx);

		mm11 = _mm256_xor_si256(mm11, sign);
		mm21 = _mm256_xor_si256(mm21, sign);
		mm11 = _mm256_permute2f128_si256(mm11, mm11, 0x01);
		mm21 = _mm256_permute2f128_si256(mm21, mm21, 0x01);

		gt0 = _mm256_cmpgt_epi32(mm11, mm21);
		gt1 = _mm256_cmpgt_epi32(mm21, mm11);
		return _mm256_movemask_ps(_mm256_castsi256_ps(gt0)) -
		       _mm256_movemask_ps(_mm256_castsi256_ps(gt1));
	}

	return 0;
}

/**
 * Compare 48 bytes between two locations.
 * Locations should not overlap.
 */
inline __attribute__((always_inline)) int
d_cmp48(const void *src_1, const void *src_2)
{
	int ret;

	ret = d_cmp32((const uint8_t *)src_1 + 0 * 32,
		      (const uint8_t *)src_2 + 0 * 32);

	if (unlikely(ret != 0))
		return ret;

	ret = d_cmp16((const uint8_t *)src_1 + 1 * 32,
		      (const uint8_t *)src_2 + 1 * 32);
	return ret;
}

/**
 * Compare 64 bytes between two locations.
 * Locations should not overlap.
 */
inline __attribute__((always_inline)) int
d_cmp64(const void *src_1, const void *src_2)
{
	const __m256i *src1 = (const __m256i *)src_1;
	const __m256i *src2 = (const __m256i *)src_2;

	__m256i mm11 = _mm256_lddqu_si256(src1);
	__m256i mm12 = _mm256_lddqu_si256(src1 + 1);
	__m256i mm21 = _mm256_lddqu_si256(src2);
	__m256i mm22 = _mm256_lddqu_si256(src2 + 1);

	__m256i mm1 = _mm256_xor_si256(mm11, mm21);
	__m256i mm2 = _mm256_xor_si256(mm12, mm22);
	__m256i mm = _mm256_or_si256(mm1, mm2);

	if (unlikely(!_mm256_testz_si256(mm, mm))) {
		__m256i idx = _mm256_setr_epi8(
			15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
			15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
		__m256i sign = _mm256_set1_epi32(0x80000000);
		__m256i gt0, gt1;

		/*
		 * Find out which of the two 32-byte blocks
		 * are different.
		 */
		if (_mm256_testz_si256(mm1, mm1)) {
			mm11 = mm12;
			mm21 = mm22;
			mm1 = mm2;
		}

		mm11 = _mm256_shuffle_epi8(mm11, idx);
		mm21 = _mm256_shuffle_epi8(mm21, idx);

		mm11 = _mm256_xor_si256(mm11, sign);
		mm21 = _mm256_xor_si256(mm21, sign);
		mm11 = _mm256_permute2f128_si256(mm11, mm11, 0x01);
		mm21 = _mm256_permute2f128_si256(mm21, mm21, 0x01);

		gt0 = _mm256_cmpgt_epi32(mm11, mm21);
		gt1 = _mm256_cmpgt_epi32(mm21, mm11);
		return _mm256_movemask_ps(_mm256_castsi256_ps(gt0)) -
		       _mm256_movemask_ps(_mm256_castsi256_ps(gt1));
	}

	return 0;
}

/**
 * Compare 128 bytes between two locations.
 * Locations should not overlap.
 */
inline __attribute__((always_inline)) int
d_cmp128(const void *src_1, const void *src_2)
{
	int ret;

	ret = d_cmp64((const uint8_t *)src_1 + 0 * 64,
		      (const uint8_t *)src_2 + 0 * 64);

	if (unlikely(ret != 0))
		return ret;

	return d_cmp64((const uint8_t *)src_1 + 1 * 64,
		       (const uint8_t *)src_2 + 1 * 64);
}

/**
 * Compare 256 bytes between two locations.
 * Locations should not overlap.
 */
inline __attribute__((always_inline)) int
d_cmp256(const void *src_1, const void *src_2)
{
	int ret;

	ret = d_cmp64((const uint8_t *)src_1 + 0 * 64,
		      (const uint8_t *)src_2 + 0 * 64);

	if (unlikely(ret != 0))
		return ret;

	ret = d_cmp64((const uint8_t *)src_1 + 1 * 64,
		      (const uint8_t *)src_2 + 1 * 64);

	if (unlikely(ret != 0))
		return ret;

	ret = d_cmp64((const uint8_t *)src_1 + 2 * 64,
		      (const uint8_t *)src_2 + 2 * 64);

	if (unlikely(ret != 0))
		return ret;

	return d_cmp64((const uint8_t *)src_1 + 3 * 64,
		       (const uint8_t *)src_2 + 3 * 64);
}

/**
 * Compare bytes between two locations. The locations must not overlap.
 *
 * @param src_1
 *   Pointer to the first source of the data.
 * @param src_2
 *   Pointer to the second source of the data.
 * @param n
 *   Number of bytes to compare.
 * @return
 *   zero if src_1 equal src_2
 *   -ve if src_1 less than src_2
 *   +ve if src_1 greater than src_2
 */
inline __attribute__((always_inline)) int
d_memcmp(const void *_src_1, const void *_src_2, size_t n)
{
	const uint8_t	*src_1 = (const uint8_t *)_src_1;
	const uint8_t	*src_2 = (const uint8_t *)_src_2;
	int		ret = 0;

	/**
	 * Compare less than 16 bytes
	 */
	if (n < 16)
		return memcmp(src_1, src_2, n);

	if (n <= 32) {
		ret = d_cmp16(src_1, src_2);
		if (unlikely(ret != 0))
			return ret;

		return d_cmp16(src_1 - 16 + n, src_2 - 16 + n);
	}

	if (n <= 48) {
		ret = d_cmp32(src_1, src_2);
		if (unlikely(ret != 0))
			return ret;

		return d_cmp16(src_1 - 16 + n, src_2 - 16 + n);
	}

	if (n <= 64) {
		ret = d_cmp32(src_1, src_2);
		if (unlikely(ret != 0))
			return ret;

		ret = d_cmp16(src_1 + 32, src_2 + 32);

		if (unlikely(ret != 0))
			return ret;

		return d_cmp16(src_1 - 16 + n, src_2 - 16 + n);
	}

CMP_BLOCK_LESS_THAN_512:
	if (n <= 512) {
		if (n >= 256) {
			ret = d_cmp256(src_1, src_2);
			if (unlikely(ret != 0))
				return ret;
			src_1 = src_1 + 256;
			src_2 = src_2 + 256;
			n -= 256;
		}
		if (n >= 128) {
			ret = d_cmp128(src_1, src_2);
			if (unlikely(ret != 0))
				return ret;
			src_1 = src_1 + 128;
			src_2 = src_2 + 128;
			n -= 128;
		}
		if (n >= 64) {
			n -= 64;
			ret = d_cmp64(src_1, src_2);
			if (unlikely(ret != 0))
				return ret;
			src_1 = src_1 + 64;
			src_2 = src_2 + 64;
		}
		if (n > 32) {
			ret = d_cmp32(src_1, src_2);
			if (unlikely(ret != 0))
				return ret;
			ret = d_cmp32(src_1 - 32 + n, src_2 - 32 + n);
			return ret;
		}
		if (n > 0)
			ret = d_cmp32(src_1 - 32 + n, src_2 - 32 + n);

		return ret;
	}

	while (n > 512) {
		ret = d_cmp256(src_1 + 0 * 256, src_2 + 0 * 256);
		if (unlikely(ret != 0))
			return ret;

		ret = d_cmp256(src_1 + 1 * 256, src_2 + 1 * 256);
		if (unlikely(ret != 0))
			return ret;

		src_1 = src_1 + 512;
		src_2 = src_2 + 512;
		n -= 512;
	}
	goto CMP_BLOCK_LESS_THAN_512;
}

#define ALIGNMENT_MASK 0x1F

/**
 * Copy 16 bytes from one location to another,
 * locations should not overlap.
 */
inline __attribute__((always_inline)) void
d_mov16(uint8_t *dst, const uint8_t *src)
{
	__m128i xmm0;

	xmm0 = _mm_loadu_si128((const __m128i *)src);
	_mm_storeu_si128((__m128i *)dst, xmm0);
}

/**
 * Copy 32 bytes from one location to another,
 * locations should not overlap.
 */
inline __attribute__((always_inline)) void
d_mov32(uint8_t *dst, const uint8_t *src)
{
	__m256i ymm0;

	ymm0 = _mm256_loadu_si256((const __m256i *)src);
	_mm256_storeu_si256((__m256i *)dst, ymm0);
}

/**
 * Copy 64 bytes from one location to another,
 * locations should not overlap.
 */
inline __attribute__((always_inline)) void
d_mov64(uint8_t *dst, const uint8_t *src)
{
	d_mov32((uint8_t *)dst + 0 * 32, (const uint8_t *)src + 0 * 32);
	d_mov32((uint8_t *)dst + 1 * 32, (const uint8_t *)src + 1 * 32);
}

/**
 * Copy 128 bytes from one location to another,
 * locations should not overlap.
 */
inline __attribute__((always_inline)) void
d_mov128(uint8_t *dst, const uint8_t *src)
{
	d_mov32((uint8_t *)dst + 0 * 32, (const uint8_t *)src + 0 * 32);
	d_mov32((uint8_t *)dst + 1 * 32, (const uint8_t *)src + 1 * 32);
	d_mov32((uint8_t *)dst + 2 * 32, (const uint8_t *)src + 2 * 32);
	d_mov32((uint8_t *)dst + 3 * 32, (const uint8_t *)src + 3 * 32);
}

/**
 * Copy 128-byte blocks from one location to another,
 * locations should not overlap.
 */
inline __attribute__((always_inline)) void
d_mov128blocks(uint8_t *dst, const uint8_t *src, size_t n)
{
	__m256i ymm0, ymm1, ymm2, ymm3;

	while (n >= 128) {
		ymm0 = _mm256_loadu_si256((const __m256i *)
					  ((const uint8_t *)src + 0 * 32));
		n -= 128;
		ymm1 = _mm256_loadu_si256((const __m256i *)
					  ((const uint8_t *)src + 1 * 32));
		ymm2 = _mm256_loadu_si256((const __m256i *)
					  ((const uint8_t *)src + 2 * 32));
		ymm3 = _mm256_loadu_si256((const __m256i *)
					  ((const uint8_t *)src + 3 * 32));
		src = (const uint8_t *)src + 128;
		_mm256_storeu_si256((__m256i *)((uint8_t *)dst + 0 * 32), ymm0);
		_mm256_storeu_si256((__m256i *)((uint8_t *)dst + 1 * 32), ymm1);
		_mm256_storeu_si256((__m256i *)((uint8_t *)dst + 2 * 32), ymm2);
		_mm256_storeu_si256((__m256i *)((uint8_t *)dst + 3 * 32), ymm3);
		dst = (uint8_t *)dst + 128;
	}
}

inline __attribute__((always_inline)) void *
d_memcpy_generic(void *dst, const void *src, size_t n)
{
	uintptr_t	dstu = (uintptr_t)dst;
	uintptr_t	srcu = (uintptr_t)src;
	void		*ret = dst;
	size_t		dstofss;
	size_t		bits;

	/**
	 * Copy less than 16 bytes
	 */
	if (n < 16) {
		if (n & 0x01) {
			*(uint8_t *)dstu = *(const uint8_t *)srcu;
			srcu = (uintptr_t)((const uint8_t *)srcu + 1);
			dstu = (uintptr_t)((uint8_t *)dstu + 1);
		}
		if (n & 0x02) {
			*(uint16_t *)dstu = *(const uint16_t *)srcu;
			srcu = (uintptr_t)((const uint16_t *)srcu + 1);
			dstu = (uintptr_t)((uint16_t *)dstu + 1);
		}
		if (n & 0x04) {
			*(uint32_t *)dstu = *(const uint32_t *)srcu;
			srcu = (uintptr_t)((const uint32_t *)srcu + 1);
			dstu = (uintptr_t)((uint32_t *)dstu + 1);
		}
		if (n & 0x08) {
			*(uint64_t *)dstu = *(const uint64_t *)srcu;
		}
		return ret;
	}

	/**
	 * Fast way when copy size doesn't exceed 256 bytes
	 */
	if (n <= 32) {
		d_mov16((uint8_t *)dst, (const uint8_t *)src);
		d_mov16((uint8_t *)dst - 16 + n,
			(const uint8_t *)src - 16 + n);
		return ret;
	}
	if (n <= 48) {
		d_mov16((uint8_t *)dst, (const uint8_t *)src);
		d_mov16((uint8_t *)dst + 16, (const uint8_t *)src + 16);
		d_mov16((uint8_t *)dst - 16 + n,
			(const uint8_t *)src - 16 + n);
		return ret;
	}
	if (n <= 64) {
		d_mov32((uint8_t *)dst, (const uint8_t *)src);
		d_mov32((uint8_t *)dst - 32 + n,
			(const uint8_t *)src - 32 + n);
		return ret;
	}
	if (n <= 256) {
		if (n >= 128) {
			n -= 128;
			d_mov128((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 128;
			dst = (uint8_t *)dst + 128;
		}
COPY_BLOCK_128_BACK31:
		if (n >= 64) {
			n -= 64;
			d_mov64((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 64;
			dst = (uint8_t *)dst + 64;
		}
		if (n > 32) {
			d_mov32((uint8_t *)dst, (const uint8_t *)src);
			d_mov32((uint8_t *)dst - 32 + n,
				(const uint8_t *)src - 32 + n);
			return ret;
		}
		if (n > 0) {
			d_mov32((uint8_t *)dst - 32 + n,
				(const uint8_t *)src - 32 + n);
		}
		return ret;
	}

	/**
	 * Make store aligned when copy size exceeds 256 bytes
	 */
	dstofss = (uintptr_t)dst & 0x1F;
	if (dstofss > 0) {
		dstofss = 32 - dstofss;
		n -= dstofss;
		d_mov32((uint8_t *)dst, (const uint8_t *)src);
		src = (const uint8_t *)src + dstofss;
		dst = (uint8_t *)dst + dstofss;
	}

	/**
	 * Copy 128-byte blocks
	 */
	d_mov128blocks((uint8_t *)dst, (const uint8_t *)src, n);
	bits = n;
	n = n & 127;
	bits -= n;
	src = (const uint8_t *)src + bits;
	dst = (uint8_t *)dst + bits;

	/**
	 * Copy whatever left
	 */
	goto COPY_BLOCK_128_BACK31;
}

inline __attribute__((always_inline)) void *
d_memcpy_aligned(void *dst, const void *src, size_t n)
{
	void *ret = dst;

	/* Copy size <= 16 bytes */
	if (n < 16) {
		if (n & 0x01) {
			*(uint8_t *)dst = *(const uint8_t *)src;
			src = (const uint8_t *)src + 1;
			dst = (uint8_t *)dst + 1;
		}
		if (n & 0x02) {
			*(uint16_t *)dst = *(const uint16_t *)src;
			src = (const uint16_t *)src + 1;
			dst = (uint16_t *)dst + 1;
		}
		if (n & 0x04) {
			*(uint32_t *)dst = *(const uint32_t *)src;
			src = (const uint32_t *)src + 1;
			dst = (uint32_t *)dst + 1;
		}
		if (n & 0x08)
			*(uint64_t *)dst = *(const uint64_t *)src;

		return ret;
	}

	/* Copy 16 <= size <= 32 bytes */
	if (n <= 32) {
		d_mov16((uint8_t *)dst, (const uint8_t *)src);
		d_mov16((uint8_t *)dst - 16 + n,
			(const uint8_t *)src - 16 + n);

		return ret;
	}

	/* Copy 32 < size <= 64 bytes */
	if (n <= 64) {
		d_mov32((uint8_t *)dst, (const uint8_t *)src);
		d_mov32((uint8_t *)dst - 32 + n,
			(const uint8_t *)src - 32 + n);

		return ret;
	}

	/* Copy 64 bytes blocks */
	for (; n >= 64; n -= 64) {
		d_mov64((uint8_t *)dst, (const uint8_t *)src);
		dst = (uint8_t *)dst + 64;
		src = (const uint8_t *)src + 64;
	}

	/* Copy whatever left */
	d_mov64((uint8_t *)dst - 64 + n,
		(const uint8_t *)src - 64 + n);

	return ret;
}

inline __attribute__((always_inline)) void *
d_memcpy(void *dst, const void *src, size_t n)
{
	if (!(((uintptr_t)dst | (uintptr_t)src) & ALIGNMENT_MASK))
		return d_memcpy_aligned(dst, src, n);
	else
		return d_memcpy_generic(dst, src, n);
}

#else /**!__AVX2__ */

#define d_memcpy	memcpy
#define d_memcmp	memcmp

#endif

/** @}
 */

#ifdef __cplusplus
}
#endif
#endif /* __GURT_MEM_H__ */
