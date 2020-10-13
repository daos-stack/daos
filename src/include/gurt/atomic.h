/*
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * Atomic directives
 */
#ifndef __GURT_ATOMIC_H__
#define __GURT_ATOMIC_H__

#if HAVE_STDATOMIC

#include <stdatomic.h>

#ifdef __INTEL_COMPILER
#define ATOMIC volatile
#else
#define ATOMIC _Atomic
#endif

/* stdatomic interface for compare_and_exchange doesn't quite align */
#define atomic_compare_exchange(ptr, oldvalue, newvalue) \
	atomic_compare_exchange_weak_explicit(ptr, &oldvalue, newvalue, \
				memory_order_relaxed, memory_order_relaxed)
#define atomic_store_release(ptr, value) \
	atomic_store_explicit(ptr, value, memory_order_release)

#define atomic_store_relaxed(ptr, value) \
	atomic_store_explicit(ptr, value, memory_order_relaxed)

#define atomic_load_relaxed(ptr) atomic_load_explicit(ptr, memory_order_relaxed)

#define atomic_fetch_sub_relaxed(ptr, value)			\
	atomic_fetch_sub_explicit(ptr, value, memory_order_relaxed)

#define atomic_fetch_add_relaxed(ptr, value)			\
	atomic_fetch_add_explicit(ptr, value, memory_order_relaxed)

#else

#define atomic_fetch_sub __sync_fetch_and_sub
#define atomic_fetch_add __sync_fetch_and_add
#define atomic_fetch_sub_relaxed __sync_fetch_and_sub
#define atomic_fetch_add_relaxed __sync_fetch_and_add
#define atomic_fetch_or_relaxed __sync_fetch_and_or
#define atomic_fetch_and_relaxed __sync_fetch_and_and
#define atomic_compare_exchange __sync_bool_compare_and_swap
#define atomic_store_release(ptr, value) \
	do {                             \
		__sync_synchronize();    \
		*(ptr) = (value);        \
	} while (0)
#define atomic_store_relaxed(ptr, value) atomic_store_release(ptr, value)
/* There doesn't seem to be a great option here to mimic just
 * consume.  Adding 0 should suffice for the load side.  If
 * the compiler is smart, it could potentially avoid the
 * actual synchronization after the store as the store isn't
 * required.
 */
#define atomic_load_consume(ptr) atomic_fetch_add(ptr, 0)
#define atomic_load_relaxed(ptr) atomic_fetch_add(ptr, 0)
#define ATOMIC

#endif

#endif /* __GURT_ATOMIC_H__ */
