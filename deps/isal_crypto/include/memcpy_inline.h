/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/


/**
 *  @file  memcpy_inline.h
 *  @brief Defines intrinsic memcpy functions used by the new hashing API
 *
 */

#ifndef _MEMCPY_H_
#define _MEMCPY_H_

#if defined(__i386__) || defined(__x86_64__) || defined( _M_X64) \
	|| defined(_M_IX86)
#include "intrinreg.h"
#endif
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__i386__) || defined(__x86_64__) || defined( _M_X64) \
	|| defined(_M_IX86)

#define memcpy_varlen   memcpy_sse_varlen
#define memcpy_fixedlen memcpy_sse_fixedlen

#define memclr_varlen   memclr_sse_varlen
#define memclr_fixedlen memclr_sse_fixedlen

static inline void memcpy_lte32_sse_fixedlen(void* dst, const void* src, size_t nbytes);
static inline void memcpy_gte16_sse_fixedlen(void* dst, const void* src, size_t nbytes);
static inline void memcpy_sse_fixedlen      (void* dst, const void* src, size_t nbytes);

static inline void memcpy_lte32_sse_varlen  (void* dst, const void* src, size_t nbytes);
static inline void memcpy_gte16_sse_varlen  (void* dst, const void* src, size_t nbytes);
static inline void memcpy_sse_varlen        (void* dst, const void* src, size_t nbytes);


static inline void memclr_lte32_sse_fixedlen(void* dst, size_t nbytes);
static inline void memclr_gte16_sse_fixedlen(void* dst, size_t nbytes);
static inline void memclr_sse_fixedlen      (void* dst, size_t nbytes);

static inline void memclr_lte32_sse_varlen  (void* dst, size_t nbytes);
static inline void memclr_gte16_sse_varlen  (void* dst, size_t nbytes);
static inline void memclr_sse_varlen        (void* dst, size_t nbytes);

#define MEMCPY_BETWEEN_N_AND_2N_BYTES(N, fixedwidth, dst, src, nbytes) \
	do { \
		intrinreg##N head; \
		intrinreg##N tail; \
		assert(N <= nbytes && nbytes <= 2*N); \
		if(N == 1 || (fixedwidth && nbytes==N) ) { \
			head = load_intrinreg##N(src); \
			store_intrinreg##N(dst, head); \
		} \
		else { \
			head = load_intrinreg##N(src); \
			tail = load_intrinreg##N((const void*)((const char*)src + (nbytes - N))); \
			store_intrinreg##N(dst, head); \
			store_intrinreg##N((void*)((char*)dst + (nbytes - N)), tail); \
		} \
	} while(0)

#define MEMCLR_BETWEEN_N_AND_2N_BYTES(N, fixedwidth, dst, nbytes) \
	do { \
		const intrinreg##N zero = {0}; \
		assert(N <= nbytes && nbytes <= 2*N); \
		if(N == 1 || (fixedwidth && nbytes==N) ) { \
			store_intrinreg##N(dst, zero); \
		} \
		else { \
			store_intrinreg##N(dst, zero); \
			store_intrinreg##N((void*)((char*)dst + (nbytes - N)), zero); \
		} \
	} while(0)

// Define load/store functions uniformly.

#define load_intrinreg16(src)  _mm_loadu_ps((const float*) src)
#define store_intrinreg16(dst,val) _mm_storeu_ps((float*) dst, val)

static inline intrinreg8 load_intrinreg8(const void *src)
{
	return *(intrinreg8 *) src;
}

static inline void store_intrinreg8(void *dst, intrinreg8 val)
{
	*(intrinreg8 *) dst = val;
}

static inline intrinreg4 load_intrinreg4(const void *src)
{
	return *(intrinreg4 *) src;
}

static inline void store_intrinreg4(void *dst, intrinreg4 val)
{
	*(intrinreg4 *) dst = val;
}

static inline intrinreg2 load_intrinreg2(const void *src)
{
	return *(intrinreg2 *) src;
}

static inline void store_intrinreg2(void *dst, intrinreg2 val)
{
	*(intrinreg2 *) dst = val;
}

static inline intrinreg1 load_intrinreg1(const void *src)
{
	return *(intrinreg1 *) src;
}

static inline void store_intrinreg1(void *dst, intrinreg1 val)
{
	*(intrinreg1 *) dst = val;
}

static inline void memcpy_gte16_sse_fixedlen(void *dst, const void *src, size_t nbytes)
{
	size_t i;
	size_t j;
	intrinreg16 pool[4];
	size_t remaining_moves;
	size_t tail_offset;
	int do_tail;
	assert(nbytes >= 16);

	for (i = 0; i + 16 * 4 <= nbytes; i += 16 * 4) {
		for (j = 0; j < 4; j++)
			pool[j] =
			    load_intrinreg16((const void *)((const char *)src + i + 16 * j));
		for (j = 0; j < 4; j++)
			store_intrinreg16((void *)((char *)dst + i + 16 * j), pool[j]);
	}

	remaining_moves = (nbytes - i) / 16;
	tail_offset = nbytes - 16;
	do_tail = (tail_offset & (16 - 1));

	for (j = 0; j < remaining_moves; j++)
		pool[j] = load_intrinreg16((const void *)((const char *)src + i + 16 * j));

	if (do_tail)
		pool[j] = load_intrinreg16((const void *)((const char *)src + tail_offset));

	for (j = 0; j < remaining_moves; j++)
		store_intrinreg16((void *)((char *)dst + i + 16 * j), pool[j]);

	if (do_tail)
		store_intrinreg16((void *)((char *)dst + tail_offset), pool[j]);
}

static inline void memclr_gte16_sse_fixedlen(void *dst, size_t nbytes)
{
	size_t i;
	size_t j;
	const intrinreg16 zero = { 0 };
	size_t remaining_moves;
	size_t tail_offset;
	int do_tail;
	assert(nbytes >= 16);

	for (i = 0; i + 16 * 4 <= nbytes; i += 16 * 4)
		for (j = 0; j < 4; j++)
			store_intrinreg16((void *)((char *)dst + i + 16 * j), zero);

	remaining_moves = (nbytes - i) / 16;
	tail_offset = nbytes - 16;
	do_tail = (tail_offset & (16 - 1));

	for (j = 0; j < remaining_moves; j++)
		store_intrinreg16((void *)((char *)dst + i + 16 * j), zero);

	if (do_tail)
		store_intrinreg16((void *)((char *)dst + tail_offset), zero);
}

static inline void memcpy_lte32_sse_fixedlen(void *dst, const void *src, size_t nbytes)
{
	assert(nbytes <= 32);
	if (nbytes >= 16)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(16, 1, dst, src, nbytes);
	else if (nbytes >= 8)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(8, 1, dst, src, nbytes);
	else if (nbytes >= 4)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(4, 1, dst, src, nbytes);
	else if (nbytes >= 2)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(2, 1, dst, src, nbytes);
	else if (nbytes >= 1)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(1, 1, dst, src, nbytes);
}

static inline void memclr_lte32_sse_fixedlen(void *dst, size_t nbytes)
{
	assert(nbytes <= 32);
	if (nbytes >= 16)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(16, 1, dst, nbytes);
	else if (nbytes >= 8)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(8, 1, dst, nbytes);
	else if (nbytes >= 4)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(4, 1, dst, nbytes);
	else if (nbytes >= 2)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(2, 1, dst, nbytes);
	else if (nbytes >= 1)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(1, 1, dst, nbytes);
}

static inline void memcpy_lte32_sse_varlen(void *dst, const void *src, size_t nbytes)
{
	assert(nbytes <= 32);
	if (nbytes >= 16)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(16, 0, dst, src, nbytes);
	else if (nbytes >= 8)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(8, 0, dst, src, nbytes);
	else if (nbytes >= 4)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(4, 0, dst, src, nbytes);
	else if (nbytes >= 2)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(2, 0, dst, src, nbytes);
	else if (nbytes >= 1)
		MEMCPY_BETWEEN_N_AND_2N_BYTES(1, 0, dst, src, nbytes);
}

static inline void memclr_lte32_sse_varlen(void *dst, size_t nbytes)
{
	assert(nbytes <= 32);
	if (nbytes >= 16)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(16, 0, dst, nbytes);
	else if (nbytes >= 8)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(8, 0, dst, nbytes);
	else if (nbytes >= 4)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(4, 0, dst, nbytes);
	else if (nbytes >= 2)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(2, 0, dst, nbytes);
	else if (nbytes >= 1)
		MEMCLR_BETWEEN_N_AND_2N_BYTES(1, 0, dst, nbytes);
}

static inline void memcpy_gte16_sse_varlen(void *dst, const void *src, size_t nbytes)
{
	size_t i = 0;
	intrinreg16 tail;

	assert(nbytes >= 16);

	while (i + 128 <= nbytes) {
		memcpy_gte16_sse_fixedlen((void *)((char *)dst + i),
					  (const void *)((const char *)src + i), 128);
		i += 128;
	}
	if (i + 64 <= nbytes) {
		memcpy_gte16_sse_fixedlen((void *)((char *)dst + i),
					  (const void *)((const char *)src + i), 64);
		i += 64;
	}
	if (i + 32 <= nbytes) {
		memcpy_gte16_sse_fixedlen((void *)((char *)dst + i),
					  (const void *)((const char *)src + i), 32);
		i += 32;
	}
	if (i + 16 <= nbytes) {
		memcpy_gte16_sse_fixedlen((void *)((char *)dst + i),
					  (const void *)((const char *)src + i), 16);
	}

	i = nbytes - 16;
	tail = load_intrinreg16((const void *)((const char *)src + i));
	store_intrinreg16((void *)((char *)dst + i), tail);
}

static inline void memclr_gte16_sse_varlen(void *dst, size_t nbytes)
{
	size_t i = 0;
	const intrinreg16 zero = { 0 };

	assert(nbytes >= 16);

	while (i + 128 <= nbytes) {
		memclr_gte16_sse_fixedlen((void *)((char *)dst + i), 128);
		i += 128;
	}
	if (i + 64 <= nbytes) {
		memclr_gte16_sse_fixedlen((void *)((char *)dst + i), 64);
		i += 64;
	}
	if (i + 32 <= nbytes) {
		memclr_gte16_sse_fixedlen((void *)((char *)dst + i), 32);
		i += 32;
	}
	if (i + 16 <= nbytes) {
		memclr_gte16_sse_fixedlen((void *)((char *)dst + i), 16);
	}

	i = nbytes - 16;
	store_intrinreg16((void *)((char *)dst + i), zero);
}

static inline void memcpy_sse_fixedlen(void *dst, const void *src, size_t nbytes)
{
	if (nbytes >= 16)
		memcpy_gte16_sse_fixedlen(dst, src, nbytes);
	else
		memcpy_lte32_sse_fixedlen(dst, src, nbytes);
}

static inline void memclr_sse_fixedlen(void *dst, size_t nbytes)
{
	if (nbytes >= 16)
		memclr_gte16_sse_fixedlen(dst, nbytes);
	else
		memclr_lte32_sse_fixedlen(dst, nbytes);
}

static inline void memcpy_sse_varlen(void *dst, const void *src, size_t nbytes)
{
	if (nbytes >= 16)
		memcpy_gte16_sse_varlen(dst, src, nbytes);
	else
		memcpy_lte32_sse_varlen(dst, src, nbytes);
}

static inline void memclr_sse_varlen(void *dst, size_t nbytes)
{
	if (nbytes >= 16)
		memclr_gte16_sse_varlen(dst, nbytes);
	else
		memclr_lte32_sse_varlen(dst, nbytes);
}
#else
#define memcpy_varlen   memcpy
#define memcpy_fixedlen memcpy

#define memclr_varlen(dst,n)   memset(dst,0,n)
#define memclr_fixedlen(dst,n) memset(dst,0,n)

#endif

#ifdef __cplusplus
}
#endif

#endif // __MEMCPY_H
