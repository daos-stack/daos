/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Atomic directives
 */
#ifndef __GURT_ATOMIC_H__
#define __GURT_ATOMIC_H__

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

#endif /* __GURT_ATOMIC_H__ */
