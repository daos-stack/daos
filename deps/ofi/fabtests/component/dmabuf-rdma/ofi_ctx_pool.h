/*
 * Copyright (c) 2022 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __OFI_CTX_POOL_H__
#define __OFI_CTX_POOL_H__

/*
 * Context pool to support FI_CONTEXT mode
 */

struct context_list {
	struct fi_context context;
	struct context_list *next;
};

struct context_pool {
	struct context_list *head;
	struct context_list *tail;
	struct context_list list[0];
};

static inline struct context_pool *init_context_pool(size_t pool_size)
{
	struct context_pool *pool;
	int i;

	pool = calloc(1, sizeof(*pool) +
				pool_size * sizeof(struct context_list));
	if (!pool)
		return NULL;
	
	pool->head = &pool->list[0];
	pool->tail = &pool->list[pool_size - 1];
	for (i = 0; i < pool_size; i++)
		pool->list[i].next = &pool->list[i+1];

	return pool;
}

static inline struct fi_context *get_context(struct context_pool *pool)
{
	struct context_list *entry;

	if (pool->head == pool->tail)
		return NULL;

	entry = pool->head;
	pool->head = pool->head->next;

	entry->next = NULL;
	return &entry->context;
}

static inline void put_context(struct context_pool *pool,
			       struct fi_context *ctxt)
{
	struct context_list *entry;

	if (!ctxt)
		return;

	entry = container_of(ctxt, struct context_list, context);
	entry->next = NULL;
	pool->tail->next = entry;
	pool->tail = entry;
}

#endif /* __OFI_CTX_POOL_H__ */

