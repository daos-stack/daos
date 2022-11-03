/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2020, Intel Corporation */

#ifndef LIBPMEMOBJ_PMEMOPS_H
#define LIBPMEMOBJ_PMEMOPS_H 1

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>

#include "out.h"

#define MEM_PAGESIZE (4*1024)


typedef int (*persist_fn)(void *base, const void *, size_t, unsigned);
typedef int (*flush_fn)(void *base, const void *, size_t, unsigned);
typedef void (*drain_fn)(void *base);

typedef void *(*memcpy_fn)(void *base, void *dest, const void *src, size_t len,
		unsigned flags);
typedef void *(*memmove_fn)(void *base, void *dest, const void *src, size_t len,
		unsigned flags);
typedef void *(*memset_fn)(void *base, void *dest, int c, size_t len,
		unsigned flags);

typedef int (*remote_read_fn)(void *ctx, uintptr_t base, void *dest, void *addr,
		size_t length);

struct pmem_ops {
	/* for 'master' replica: with or without data replication */
	persist_fn persist;	/* persist function */
	flush_fn flush;		/* flush function */
	drain_fn drain;		/* drain function */
	memcpy_fn memcpy; /* persistent memcpy function */
	memmove_fn memmove; /* persistent memmove function */
	memset_fn memset; /* persistent memset function */
	void *base;

	struct remote_ops {
		remote_read_fn read;

		void *ctx;
		uintptr_t base;
	} remote;
};

static force_inline int
pmemops_xpersist(const struct pmem_ops *p_ops, const void *d, size_t s,
		unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	SUPPRESS_UNUSED(d);
	SUPPRESS_UNUSED(s);
	SUPPRESS_UNUSED(flags);
	return 0;
}

static force_inline void
pmemops_persist(const struct pmem_ops *p_ops, const void *d, size_t s)
{
	(void) pmemops_xpersist(p_ops, d, s, 0);
}

static force_inline int
pmemops_xflush(const struct pmem_ops *p_ops, const void *d, size_t s,
		unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	SUPPRESS_UNUSED(flags, s, d);
	return 0;
}

static force_inline void
pmemops_flush(const struct pmem_ops *p_ops, const void *d, size_t s)
{
	(void) pmemops_xflush(p_ops, d, s, 0);
}

static force_inline void
pmemops_drain(const struct pmem_ops *p_ops)
{
	SUPPRESS_UNUSED(p_ops);
}

static force_inline void *
pmemops_memcpy(const struct pmem_ops *p_ops, void *dest,
		const void *src, size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memcpy(dest, src, len);
	pmemops_xflush(p_ops, dest, len, flags);
	return dest;
}

static force_inline void *
pmemops_memmove(const struct pmem_ops *p_ops, void *dest,
		const void *src, size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memmove(dest, src, len);
	pmemops_xflush(p_ops, dest, len, flags);
	return dest;
}

static force_inline void *
pmemops_memset(const struct pmem_ops *p_ops, void *dest, int c,
		size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memset(dest, c, len);
	pmemops_xflush(p_ops, dest, len, flags);
	return dest;
}

#endif
