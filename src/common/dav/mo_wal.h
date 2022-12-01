/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2020, Intel Corporation */

#ifndef __DAOS_COMMON_MO_WAL_H
#define __DAOS_COMMON_MO_WAL_H 1

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>

#include "out.h"
#include "wal_tx.h"

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

struct mo_ops {
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

static force_inline void
mo_wal_persist(const struct mo_ops *p_ops, void *d, size_t s)
{
	dav_wal_tx_snap(p_ops->base, d, s, d, 0);
}

static force_inline void
mo_wal_flush(const struct mo_ops *p_ops, void *d, size_t s, int flags)
{
	dav_wal_tx_snap(p_ops->base, d, s, d, flags);
}

static force_inline void
mo_wal_drain(const struct mo_ops *p_ops)
{
	SUPPRESS_UNUSED(p_ops);
}

static force_inline void *
mo_wal_memcpy(const struct mo_ops *p_ops, void *dest,
		const void *src, size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memcpy(dest, src, len);
	mo_wal_flush(p_ops, dest, len, 0);
	return dest;
}

static force_inline void *
mo_wal_memmove(const struct mo_ops *p_ops, void *dest,
		const void *src, size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memmove(dest, src, len);
	mo_wal_flush(p_ops, dest, len, 0);
	return dest;
}

static force_inline void *
mo_wal_memset(const struct mo_ops *p_ops, void *dest, int c,
		size_t len, unsigned flags)
{
	SUPPRESS_UNUSED(p_ops);
	memset(dest, c, len);
	dav_wal_tx_set(p_ops->base, dest, c, len);
	return dest;
}

#endif /* __DAOS_COMMON_MO_WAL_H */
