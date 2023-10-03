/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2020, Intel Corporation */

/*
 * container.h -- internal definitions for block containers
 */

#ifndef __DAOS_COMMON_CONTAINER_H
#define __DAOS_COMMON_CONTAINER_H 1

#include "memblock.h"

struct block_container {
	const struct block_container_ops *c_ops;
	struct palloc_heap *heap;
};

struct block_container_ops {
	/* inserts a new memory block into the container */
	int (*insert)(struct block_container *c, const struct memory_block *m);

	/* removes exact match memory block */
	int (*get_rm_exact)(struct block_container *c,
		const struct memory_block *m);

	/* removes and returns the best-fit memory block for size */
	int (*get_rm_bestfit)(struct block_container *c,
		struct memory_block *m);

	/* checks whether the container is empty */
	int (*is_empty)(struct block_container *c);

	/* removes all elements from the container */
	void (*rm_all)(struct block_container *c);

	/* deletes the container */
	void (*destroy)(struct block_container *c);
};

struct palloc_heap;
struct block_container *container_new_ravl(struct palloc_heap *heap);
struct block_container *container_new_seglists(struct palloc_heap *heap);

#endif /* __DAOS_COMMON_CONTAINER_H */
