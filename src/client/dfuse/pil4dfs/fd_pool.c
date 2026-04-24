/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)

#include <unistd.h>
#include <errno.h>

#include "pil4dfs_int.h"

#define INVALID_IDX (-1)

int
fd_pool_create(int size, fd_pool_t *fd_pool)
{
	int i;

	if (fd_pool == NULL || size == 0)
		return EINVAL;

	fd_pool->size     = 0;
	fd_pool->capacity = size;
	fd_pool->head     = 0;
	fd_pool->next     = malloc(sizeof(int) * size);
	if (fd_pool->next == NULL)
		return ENOMEM;

	/* initialize linked list */
	fd_pool->next[size - 1] = INVALID_IDX;
	for (i = 0; i < (size - 1); i++)
		fd_pool->next[i] = i + 1;

	return 0;
}

int
fd_pool_alloc(fd_pool_t *fd_pool, int *idx)
{
	if (fd_pool == NULL || idx == NULL)
		return EINVAL;

	if (fd_pool->head < 0)
		/* free list is empty. */
		return EMFILE;
	/* return the head of free list */
	*idx = fd_pool->head;
	/* update the head of free list to point to the next node */
	fd_pool->head = fd_pool->next[*idx];
	fd_pool->size++;

	return 0;
}

int
fd_pool_free(fd_pool_t *fd_pool, int idx)
{
	if (fd_pool == NULL || (idx < 0) || (idx >= fd_pool->capacity))
		return EINVAL;

	/* set the freed node as the head node */
	fd_pool->next[idx] = fd_pool->head;
	/* update head pointer to the newly freed node */
	fd_pool->head = idx;
	fd_pool->size--;

	return 0;
}

int
fd_pool_destroy(fd_pool_t *fd_pool)
{
	if (fd_pool == NULL)
		return EINVAL;

	free(fd_pool->next);
	return 0;
}

#undef INVALID_IDX
