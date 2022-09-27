/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2019-2022, Intel Corporation */

#ifndef COMMON_ALLOC_H
#define COMMON_ALLOC_H

#include <gurt/common.h>

static inline void *
Zalloc(size_t sz)
{
	void *ptr;

	D_ALLOC(ptr, sz);
	return ptr;
}

static inline void *
Malloc(size_t sz)
{
	void *ptr;

	D_ALLOC_NZ(ptr, sz);
	return ptr;
}

static inline void *
Realloc(void *ptr, size_t size)
{
	void *new_ptr;

	D_REALLOC_NZ(new_ptr, ptr, size);
	return new_ptr;
}

#define	Free(ptr)	D_FREE(ptr)

#endif
