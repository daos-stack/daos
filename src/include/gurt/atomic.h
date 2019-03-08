/* Copyright (C) 2017-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Atomic directives
 */
#ifndef __GURT_ATOMIC_H__
#define __GURT_ATOMIC_H__

#if HAVE_STDATOMIC

#include <stdatomic.h>
#define ATOMIC _Atomic
/* stdatomic interface for compare_and_exchange doesn't quite align */
#define atomic_compare_exchange(ptr, oldvalue, newvalue) \
	atomic_compare_exchange_weak(ptr, &oldvalue, newvalue)
#define atomic_store_release(ptr, value) \
	atomic_store_explicit(ptr, value, memory_order_release)

#define atomic_add(ptr, value) atomic_fetch_add_explicit(ptr,		\
							 value,		\
							 memory_order_relaxed)
#define atomic_load_consume(ptr) \
	atomic_load_explicit(ptr, memory_order_consume)

#define atomic_dec_release(ptr) \
	atomic_fetch_sub_explicit(ptr, 1, memory_order_release)

#else

#define atomic_fetch_sub __sync_fetch_and_sub
#define atomic_fetch_add __sync_fetch_and_add
#define atomic_compare_exchange __sync_bool_compare_and_swap
#define atomic_store_release(ptr, value) \
	do {                             \
		__sync_synchronize();    \
		*(ptr) = (value);        \
	} while (0)
/* There doesn't seem to be a great option here to mimic just
 * consume.  Adding 0 should suffice for the load side.  If
 * the compiler is smart, it could potentially avoid the
 * actual synchronization after the store as the store isn't
 * required.
 */
#define atomic_load_consume(ptr) atomic_fetch_add(ptr, 0)
#define atomic_dec_release(ptr) __sync_fetch_and_sub(ptr, 1)
#define ATOMIC

#define atomic_add(ptr, value) atomic_fetch_add(ptr, value)

#endif

#define atomic_inc(ptr) atomic_add(ptr, 1)

#endif /* __GURT_ATOMIC_H__ */
