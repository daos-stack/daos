/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

#ifndef __IOF_ATOMIC_H__
#define __IOF_ATOMIC_H__

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

#endif /* __IOF_ATOMIC_H__ */
