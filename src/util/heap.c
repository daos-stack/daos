/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of cart, it implements the crt bin heap functions.
 */

#include <pthread.h>
#include <crt_util/common.h>
#include <crt_util/heap.h>

static void
cbh_lock_init(struct crt_binheap *h)
{
	if (h->cbh_feats & CBH_FT_NOLOCK)
		return;

	if (h->cbh_feats & CBH_FT_RWLOCK)
		pthread_rwlock_init(&h->cbh_rwlock, NULL);
	else
		pthread_mutex_init(&h->cbh_mutex, NULL);
}

static void
cbh_lock_fini(struct crt_binheap *h)
{
	if (h->cbh_feats & CBH_FT_NOLOCK)
		return;

	if (h->cbh_feats & CBH_FT_RWLOCK)
		pthread_rwlock_destroy(&h->cbh_rwlock);
	else
		pthread_mutex_destroy(&h->cbh_mutex);
}

/** lock the bin heap */
static void
cbh_lock(struct crt_binheap *h, bool read_only)
{
	if (h->cbh_feats & CBH_FT_NOLOCK)
		return;

	if (h->cbh_feats & CBH_FT_RWLOCK) {
		if (read_only)
			pthread_rwlock_rdlock(&h->cbh_rwlock);
		else
			pthread_rwlock_wrlock(&h->cbh_rwlock);
	} else {
		pthread_mutex_lock(&h->cbh_mutex);
	}
}

/** unlock the bin heap */
static void
cbh_unlock(struct crt_binheap *h, bool read_only)
{
	if (h->cbh_feats & CBH_FT_NOLOCK)
		return;

	if (h->cbh_feats & CBH_FT_RWLOCK)
		pthread_rwlock_unlock(&h->cbh_rwlock);
	else
		pthread_mutex_unlock(&h->cbh_mutex);
}

/** Grows the capacity of a binary heap */
static int
crt_binheap_grow(struct crt_binheap *h)
{
	struct crt_binheap_node		***frag1 = NULL;
	struct crt_binheap_node		 **frag2;
	uint32_t			   hwm;

	C_ASSERT(h != NULL);
	hwm = h->cbh_hwm;

	/* need a whole new chunk of pointers */
	C_ASSERT((h->cbh_hwm & CBH_MASK) == 0);

	if (hwm == 0) {
		/* first use of single indirect */
		C_ALLOC(h->cbh_nodes1, CBH_NOB);
		if (h->cbh_nodes1 == NULL)
			return -CER_NOMEM;

		goto out;
	}

	hwm -= CBH_SIZE;
	if (hwm < CBH_SIZE * CBH_SIZE) {
		/* not filled double indirect */
		C_ALLOC(frag2, CBH_NOB);
		if (frag2 == NULL)
			return -CER_NOMEM;

		if (hwm == 0) {
			/* first use of double indirect */
			C_ALLOC(h->cbh_nodes2, CBH_NOB);
			if (h->cbh_nodes2 == NULL) {
				C_FREE(frag2, CBH_NOB);
				return -CER_NOMEM;
			}
		}

		h->cbh_nodes2[hwm >> CBH_SHIFT] = frag2;
		goto out;
	}

	hwm -= CBH_SIZE * CBH_SIZE;
#if (CBH_SHIFT * 3 < 32)
	if (hwm >= CBH_SIZE * CBH_SIZE * CBH_SIZE) {
		/* filled triple indirect */
		return -CER_NOMEM;
	}
#endif
	C_ALLOC(frag2, CBH_NOB);
	if (frag2 == NULL)
		return -CER_NOMEM;

	if (((hwm >> CBH_SHIFT) & CBH_MASK) == 0) {
		/* first use of this 2nd level index */
		C_ALLOC(frag1, CBH_NOB);
		if (frag1 == NULL) {
			C_FREE(frag2, CBH_NOB);
			return -CER_NOMEM;
		}
	}

	if (hwm == 0) {
		/* first use of triple indirect */
		C_ALLOC(h->cbh_nodes3, CBH_NOB);
		if (h->cbh_nodes3 == NULL) {
			C_FREE(frag2, CBH_NOB);
			C_FREE(frag1, CBH_NOB);
			return -CER_NOMEM;
		}
	}

	if (frag1 != NULL) {
		C_ASSERT(h->cbh_nodes3[hwm >> (2 * CBH_SHIFT)] == NULL);
		h->cbh_nodes3[hwm >> (2 * CBH_SHIFT)] = frag1;
	} else {
		frag1 = h->cbh_nodes3[hwm >> (2 * CBH_SHIFT)];
		C_ASSERT(frag1 != NULL);
	}

	frag1[(hwm >> CBH_SHIFT) & CBH_MASK] = frag2;

 out:
	h->cbh_hwm += CBH_SIZE;
	return 0;
}

int
crt_binheap_create_inplace(uint32_t feats, uint32_t count, void *priv,
			   struct crt_binheap_ops *ops, struct crt_binheap *h)
{
	int	rc;

	if (ops == NULL || ops->hop_compare == NULL) {
		C_ERROR("invalid parameter, should pass in valid ops table.\n");
		return -CER_INVAL;
	}
	if (h == NULL) {
		C_ERROR("invalid parameter of NULL heap pointer.\n");
		return -CER_INVAL;
	}

	h->cbh_ops	  = ops;
	h->cbh_nodes_cnt  = 0;
	h->cbh_hwm	  = 0;
	h->cbh_priv	  = priv;
	h->cbh_feats	  = feats;

	while (h->cbh_hwm < count) { /* preallocate */
	rc = crt_binheap_grow(h);
		if (rc != 0) {
			C_ERROR("crt_binheap_grow failed, rc: %d.\n", rc);
			crt_binheap_destroy_inplace(h);
			return rc;
		}
	}

	cbh_lock_init(h);

	return 0;
}

int
crt_binheap_create(uint32_t feats, uint32_t count, void *priv,
		   struct crt_binheap_ops *ops, struct crt_binheap **h)
{
	struct crt_binheap	*bh_created;
	int			 rc;

	if (ops == NULL || ops->hop_compare == NULL) {
		C_ERROR("invalid parameter, should pass in valid ops table.\n");
		return -CER_INVAL;
	}
	if (h == NULL) {
		C_ERROR("invalid parameter of NULL heap 2nd level pointer.\n");
		return -CER_INVAL;
	}

	C_ALLOC_PTR(bh_created);
	if (bh_created == NULL)
		return -CER_NOMEM;

	rc = crt_binheap_create_inplace(feats, count, priv, ops, bh_created);
	if (rc != 0) {
		C_ERROR("crt_binheap_create_inplace failed, rc: %d.\n", rc);
		C_FREE_PTR(bh_created);
		return rc;
	}

	*h = bh_created;

	return rc;
}

void
crt_binheap_destroy_inplace(struct crt_binheap *h)
{
	uint32_t	idx0, idx1, n;

	if (h == NULL) {
		C_ERROR("ignore invalid parameter of NULL heap.\n");
		return;
	}

	n = h->cbh_hwm;

	if (n > 0) {
		C_FREE(h->cbh_nodes1, CBH_NOB);
		n -= CBH_SIZE;
	}

	if (n > 0) {
		for (idx0 = 0; idx0 < CBH_SIZE && n > 0; idx0++) {
			C_FREE(h->cbh_nodes2[idx0], CBH_NOB);
			n -= CBH_SIZE;
		}

		C_FREE(h->cbh_nodes2, CBH_NOB);
	}

	if (n > 0) {
		for (idx0 = 0; idx0 < CBH_SIZE && n > 0; idx0++) {

			for (idx1 = 0; idx1 < CBH_SIZE && n > 0; idx1++) {
				C_FREE(h->cbh_nodes3[idx0][idx1], CBH_NOB);
				n -= CBH_SIZE;
			}

			C_FREE(h->cbh_nodes3[idx0], CBH_NOB);
		}

		C_FREE(h->cbh_nodes3, CBH_NOB);
	}

	cbh_lock_fini(h);

	memset(h, 0, sizeof(*h));
}

void
crt_binheap_destroy(struct crt_binheap *h)
{
	if (h == NULL) {
		C_ERROR("ignore invalid parameter of NULL heap.\n");
		return;
	}

	crt_binheap_destroy_inplace(h);
	C_FREE_PTR(h);
}

/**
 * Obtains a double pointer to a heap node, given its index into the binary
 * tree.
 *
 * \param h [in]	The binary heap instance
 * \param idx [in]	The requested node's index
 *
 * \return		valid-pointer A double pointer to a heap pointer entry
 */
static struct crt_binheap_node **
crt_binheap_pointer(struct crt_binheap *h, uint32_t idx)
{
	if (idx < CBH_SIZE)
		return &(h->cbh_nodes1[idx]);

	idx -= CBH_SIZE;
	if (idx < CBH_SIZE * CBH_SIZE)
		return &(h->cbh_nodes2[idx >> CBH_SHIFT][idx & CBH_MASK]);

	idx -= CBH_SIZE * CBH_SIZE;
	return &(h->cbh_nodes3[idx >> (2 * CBH_SHIFT)]
				 [(idx >> CBH_SHIFT) & CBH_MASK]
				 [idx & CBH_MASK]);
}

static struct crt_binheap_node *
crt_binheap_find_locked(struct crt_binheap *h, uint32_t idx)
{
	struct crt_binheap_node **node;

	if (h == NULL) {
		C_ERROR("ignore NULL heap.\n");
		return NULL;
	}

	if (idx >= h->cbh_nodes_cnt)
		return NULL;

	node = crt_binheap_pointer(h, idx);

	return *node;
}

struct crt_binheap_node *
crt_binheap_find(struct crt_binheap *h, uint32_t idx)
{
	struct crt_binheap_node *node;

	cbh_lock(h, true /* read-only */);
	node = crt_binheap_find_locked(h, idx);
	cbh_unlock(h, true /* read-only */);

	return node;
}

/**
 * Moves a node upwards, towards the root of the binary tree.
 *
 * \param h [in]	The heap
 * \param e [in]	The node
 *
 * \return		1 the position of \a e in the tree was changed at least
 *			once;
 *			0 the position of \a e in the tree was not changed
 */
static int
crt_binheap_bubble(struct crt_binheap *h, struct crt_binheap_node *e)
{
	struct crt_binheap_node		**cur_ptr;
	struct crt_binheap_node		**parent_ptr;
	uint32_t			  cur_idx;
	uint32_t			  parent_idx;
	int				  did_sth = 0;

	C_ASSERT(h != NULL && e != NULL);
	cur_idx = e->chn_idx;
	cur_ptr = crt_binheap_pointer(h, cur_idx);
	C_ASSERT(*cur_ptr == e);

	while (cur_idx > 0) {
		parent_idx = (cur_idx - 1) >> 1;

		parent_ptr = crt_binheap_pointer(h, parent_idx);
		C_ASSERT((*parent_ptr)->chn_idx == parent_idx);

		if (h->cbh_ops->hop_compare(*parent_ptr, e))
			break;

		(*parent_ptr)->chn_idx = cur_idx;
		*cur_ptr = *parent_ptr;
		cur_ptr = parent_ptr;
		cur_idx = parent_idx;
		did_sth = 1;
	}

	e->chn_idx = cur_idx;
	*cur_ptr = e;

	return did_sth;
}

/**
 * Moves a node downwards, towards the last level of the binary tree.
 *
 * \param h [IN]	The heap
 * \param e [IN]	The node
 *
 * \return		1 The position of \a e in the tree was changed at least
 *			once;
 *			0 The position of \a e in the tree was not changed.
 */
static int
crt_binheap_sink(struct crt_binheap *h, struct crt_binheap_node *e)
{
	struct crt_binheap_node		**child_ptr;
	struct crt_binheap_node		 *child;
	struct crt_binheap_node		**child2_ptr;
	struct crt_binheap_node		  *child2;
	struct crt_binheap_node		**cur_ptr;
	uint32_t			  child2_idx;
	uint32_t			  child_idx;
	uint32_t			  cur_idx;
	uint32_t			  n;
	int				  did_sth = 0;

	C_ASSERT(h != NULL && e != NULL);

	n = h->cbh_nodes_cnt;
	cur_idx = e->chn_idx;
	cur_ptr = crt_binheap_pointer(h, cur_idx);
	C_ASSERT(*cur_ptr == e);

	while (cur_idx < n) {
		child_idx = (cur_idx << 1) + 1;
		if (child_idx >= n)
			break;

		child_ptr = crt_binheap_pointer(h, child_idx);
		child = *child_ptr;

		child2_idx = child_idx + 1;
		if (child2_idx < n) {
			child2_ptr = crt_binheap_pointer(h, child2_idx);
			child2 = *child2_ptr;

			if (h->cbh_ops->hop_compare(child2, child)) {
				child_idx = child2_idx;
				child_ptr = child2_ptr;
				child = child2;
			}
		}

		C_ASSERT(child->chn_idx == child_idx);

		if (h->cbh_ops->hop_compare(e, child))
			break;

		child->chn_idx = cur_idx;
		*cur_ptr = child;
		cur_ptr = child_ptr;
		cur_idx = child_idx;
		did_sth = 1;
	}

	e->chn_idx = cur_idx;
	*cur_ptr = e;

	return did_sth;
}

int
crt_binheap_insert(struct crt_binheap *h, struct crt_binheap_node *e)
{
	struct crt_binheap_node		**new_ptr;
	uint32_t			  new_idx;
	int				  rc;

	if (h == NULL || e == NULL) {
		C_ERROR("invalid parameter of NULL h or e.\n");
		return -CER_INVAL;
	}

	cbh_lock(h, false /* read-only */);

	new_idx = h->cbh_nodes_cnt;
	C_ASSERT(new_idx <= h->cbh_hwm);
	if (new_idx == h->cbh_hwm) {
		rc = crt_binheap_grow(h);
		if (rc != 0) {
			C_ERROR("crt_binheap_grow failed, rc: %d.\n", rc);
			cbh_unlock(h, false /* read-only */);
			return rc;
		}
	}

	if (h->cbh_ops->hop_enter) {
		rc = h->cbh_ops->hop_enter(h, e);
		if (rc != 0) {
			C_ERROR("cbh_ops->hop_enter failed, rc: %d.\n", rc);
			cbh_unlock(h, false /* read-only */);
			return rc;
		}
	}

	e->chn_idx = new_idx;
	new_ptr = crt_binheap_pointer(h, new_idx);
	h->cbh_nodes_cnt++;
	*new_ptr = e;

	crt_binheap_bubble(h, e);

	cbh_unlock(h, false /* read-only */);

	return 0;
}

static void
crt_binheap_remove_locked(struct crt_binheap *h, struct crt_binheap_node *e)
{
	struct crt_binheap_node		**cur_ptr;
	struct crt_binheap_node		 *last;
	uint32_t			  cur_idx;
	uint32_t			  n;

	if (h == NULL || e == NULL) {
		C_ERROR("invalid parameter of NULL h or e.\n");
		return;
	}

	n = h->cbh_nodes_cnt;
	cur_idx = e->chn_idx;

	C_ASSERT(cur_idx != CBH_POISON);
	C_ASSERT(cur_idx < n);

	cur_ptr = crt_binheap_pointer(h, cur_idx);
	C_ASSERT(*cur_ptr == e);

	n--;
	last = *crt_binheap_pointer(h, n);
	h->cbh_nodes_cnt = n;
	if (last == e)
		goto out;

	last->chn_idx = cur_idx;
	*cur_ptr = last;
	if (!crt_binheap_bubble(h, *cur_ptr))
		crt_binheap_sink(h, *cur_ptr);

out:
	e->chn_idx = CBH_POISON;
	if (h->cbh_ops->hop_exit)
		h->cbh_ops->hop_exit(h, e);
}

void
crt_binheap_remove(struct crt_binheap *h, struct crt_binheap_node *e)
{
	cbh_lock(h, false /* read-only */);
	crt_binheap_remove_locked(h, e);
	cbh_unlock(h, false /* read-only */);
}

struct crt_binheap_node *
crt_binheap_remove_root(struct crt_binheap *h)
{
	struct crt_binheap_node *e;

	if (h == NULL) {
		C_ERROR("ignore NULL heap.\n");
		return NULL;
	}

	cbh_lock(h, false /* read-only */);

	e = crt_binheap_find_locked(h, 0);
	if (e != NULL)
		crt_binheap_remove_locked(h, e);

	cbh_unlock(h, false /* read-only */);

	return e;
}
