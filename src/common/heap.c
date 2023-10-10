/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * common/heap.c
 *
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>

#define DHA_INIT_SIZE		1024
#define DHA_ENLARGE_SIZE	1024
#define DHA_WATER_MARK		0

enum daos_heapify_options {
	DAOS_HEAPIFY_EACH_UPDATE = 1 << 0,
	DAOS_HEAPIFY_FORCE_SHRINK = 1 << 1,
};

struct daos_heap_array {
	void		**dha_array;
	unsigned int	dha_array_size;
	unsigned int	dha_array_alloc_size;
	daos_heap_ops_t *dha_ops;
	unsigned int	dha_options;
};

static int
dha_enlarge_array(struct daos_heap_array *dha)
{
	unsigned int	new_size = dha->dha_array_alloc_size + DHA_ENLARGE_SIZE;
	void		*new_array;

	D_REALLOC_ARRAY(new_array, dha->dha_array, dha->dha_array_alloc_size, new_size);
	if (new_array == NULL)
		return -DER_NOMEM;

	dha->dha_array_alloc_size = new_size;
	dha->dha_array = new_array;
	return 0;
}

/**
 * heapify after insertion, which will append the item at the
 * end of array, so heapify from bottom to top */
static void
dha_heapify_after_insertion(struct daos_heap_array *dha, int idx)
{
	int parent = (idx - 1)/2;

	if (dha->dha_array_size <= 1 || idx < 1)
		return;

	while (parent >= 0) {
		if (!dha->dha_ops->ho_cmp(dha->dha_array, parent, idx))
			break;

		dha->dha_ops->ho_swap(dha->dha_array, parent, idx);
		idx = parent;
		parent = (parent - 1)/2;
	}

	return;
}

/* heapify after deletion, which will append the item at the
 * end of array, so heapify from bottom to top */
static void
dha_heapify_after_deletion(struct daos_heap_array *dha, int idx)
{
	int left = idx * 2 + 1;
	int right = idx * 2 + 2;
	int size = dha->dha_array_size;

	while (left < size || right < size) {
		unsigned int orig_idx = idx;

		if (left < size &&
		    dha->dha_ops->ho_cmp(dha->dha_array, idx, left))
			idx = left;

		if (right < size &&
		    dha->dha_ops->ho_cmp(dha->dha_array, idx, right))
			idx = right;

		if (idx != orig_idx) {
			dha->dha_ops->ho_swap(dha->dha_array, orig_idx, idx);
			left = idx * 2 + 1;
			right = idx * 2 + 2;
		} else {
			break;
		}
	}
}

int
daos_heap_insert(void *heap, void *array)
{
	struct daos_heap_array *dha = heap;
	int rc;

	if (dha->dha_array_size + DHA_WATER_MARK >= dha->dha_array_alloc_size) {
		rc = dha_enlarge_array(dha);
		if (rc != 0)
			return rc;
	}

	dha->dha_array[dha->dha_array_size++] = array;
	dha->dha_ops->ho_set_key(array, dha->dha_array_size - 1);
	/* heapify from bottom to top after insertion */
	dha_heapify_after_insertion(heap, dha->dha_array_size - 1);
	return 0;
}

int
daos_heap_delete(void *heap, int idx)
{
	struct daos_heap_array	*dha = heap;

	if (idx > dha->dha_array_size - 1 || idx < 0)
		return 0;

	if (idx == dha->dha_array_size - 1) {
		dha->dha_array_size--;
		return 0;
	}

	/* Swith the last entry to with deleted entry, then heapify from
	 * that entry to the bottom. */
	dha->dha_ops->ho_swap(dha->dha_array, idx, dha->dha_array_size - 1);
	dha->dha_array_size--;
	if (idx == 0 || dha->dha_options & DAOS_HEAPIFY_EACH_UPDATE)
		dha_heapify_after_deletion(heap, idx);

	return 0;
}

void *
daos_heap_top(void *heap)
{
	struct daos_heap_array *dha = heap;

	if (dha->dha_array_size == 0)
		return NULL;

	return dha->dha_array[0];
}

int
daos_heap_create(daos_heap_ops_t *ops, unsigned int opt, void **dha)
{
	struct daos_heap_array *heap;

	D_ALLOC_PTR(heap);
	if (heap == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(heap->dha_array, DHA_INIT_SIZE);
	if (heap->dha_array == NULL) {
		D_FREE(heap);
		return -DER_NOMEM;
	}

	heap->dha_array_size = 0;
	heap->dha_array_alloc_size = DHA_INIT_SIZE;
	heap->dha_ops = ops;
	heap->dha_options = opt;
	*dha = heap;
	return 0;
}

void
daos_heap_destroy(void *dha)
{
	struct daos_heap_array *heap = dha;

	if (heap == NULL)
		return;

	if (heap->dha_array != NULL)
		D_FREE(heap->dha_array);

	D_FREE(heap);
}
