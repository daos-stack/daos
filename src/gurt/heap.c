/*
 * (C) Copyright 2011,2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of gurt, it implements the gurt bin heap functions.
 */
#define D_LOGFAC	DD_FAC(mem)

#include <pthread.h>
#include <gurt/common.h>
#include <gurt/heap.h>

static int
dbh_lock_init(struct d_binheap *h)
{
	if (h->d_bh_feats & DBH_FT_NOLOCK)
		return 0;

	if (h->d_bh_feats & DBH_FT_RWLOCK)
		return D_RWLOCK_INIT(&h->d_bh_rwlock, NULL);
	else
		return D_MUTEX_INIT(&h->d_bh_mutex, NULL);
}

static void
dbh_lock_fini(struct d_binheap *h)
{
	if (h->d_bh_feats & DBH_FT_NOLOCK)
		return;

	if (h->d_bh_feats & DBH_FT_RWLOCK)
		D_RWLOCK_DESTROY(&h->d_bh_rwlock);
	else
		D_MUTEX_DESTROY(&h->d_bh_mutex);
}

/** lock the bin heap */
static void
dbh_lock(struct d_binheap *h, bool read_only)
{
	if (h->d_bh_feats & DBH_FT_NOLOCK)
		return;

	if (h->d_bh_feats & DBH_FT_RWLOCK) {
		if (read_only)
			D_RWLOCK_RDLOCK(&h->d_bh_rwlock);
		else
			D_RWLOCK_WRLOCK(&h->d_bh_rwlock);
	} else {
		D_MUTEX_LOCK(&h->d_bh_mutex);
	}
}

/** unlock the bin heap */
static void
dbh_unlock(struct d_binheap *h, bool read_only)
{
	if (h->d_bh_feats & DBH_FT_NOLOCK)
		return;

	if (h->d_bh_feats & DBH_FT_RWLOCK)
		D_RWLOCK_UNLOCK(&h->d_bh_rwlock);
	else
		D_MUTEX_UNLOCK(&h->d_bh_mutex);
}

/** Grows the capacity of a binary heap */
static int
d_binheap_grow(struct d_binheap *h)
{
	struct d_binheap_node		***frag1 = NULL;
	struct d_binheap_node		 **frag2;
	uint32_t			   hwm;

	D_ASSERT(h != NULL);
	hwm = h->d_bh_hwm;

	/* need a whole new chunk of pointers */
	D_ASSERT((h->d_bh_hwm & DBH_MASK) == 0);

	if (hwm == 0) {
		/* first use of single indirect */
		D_ALLOC(h->d_bh_nodes1, DBH_NOB);
		if (h->d_bh_nodes1 == NULL)
			return -DER_NOMEM;

		goto out;
	}

	hwm -= DBH_SIZE;
	if (hwm < DBH_SIZE * DBH_SIZE) {
		/* not filled double indirect */
		D_ALLOC(frag2, DBH_NOB);
		if (frag2 == NULL)
			return -DER_NOMEM;

		if (hwm == 0) {
			/* first use of double indirect */
			D_ALLOC(h->d_bh_nodes2, DBH_NOB);
			if (h->d_bh_nodes2 == NULL) {
				D_FREE(frag2);
				return -DER_NOMEM;
			}
		}

		h->d_bh_nodes2[hwm >> DBH_SHIFT] = frag2;
		goto out;
	}

	hwm -= DBH_SIZE * DBH_SIZE;
#if (DBH_SHIFT * 3 < 32)
	if (hwm >= DBH_SIZE * DBH_SIZE * DBH_SIZE) {
		/* filled triple indirect */
		return -DER_NOMEM;
	}
#endif
	D_ALLOC(frag2, DBH_NOB);
	if (frag2 == NULL)
		return -DER_NOMEM;

	if (((hwm >> DBH_SHIFT) & DBH_MASK) == 0) {
		/* first use of this 2nd level index */
		D_ALLOC(frag1, DBH_NOB);
		if (frag1 == NULL) {
			D_FREE(frag2);
			return -DER_NOMEM;
		}
	}

	if (hwm == 0) {
		/* first use of triple indirect */
		D_ALLOC(h->d_bh_nodes3, DBH_NOB);
		if (h->d_bh_nodes3 == NULL) {
			D_FREE(frag2);
			D_FREE(frag1);
			return -DER_NOMEM;
		}
	}

	if (frag1 != NULL) {
		D_ASSERT(h->d_bh_nodes3[hwm >> (2 * DBH_SHIFT)] == NULL);
		h->d_bh_nodes3[hwm >> (2 * DBH_SHIFT)] = frag1;
	} else {
		frag1 = h->d_bh_nodes3[hwm >> (2 * DBH_SHIFT)];
		D_ASSERT(frag1 != NULL);
	}

	frag1[(hwm >> DBH_SHIFT) & DBH_MASK] = frag2;

 out:
	h->d_bh_hwm += DBH_SIZE;
	return 0;
}

int
d_binheap_create_inplace(uint32_t feats, uint32_t count, void *priv,
			 struct d_binheap_ops *ops, struct d_binheap *h)
{
	int	rc;

	if (ops == NULL || ops->hop_compare == NULL) {
		D_ERROR("invalid parameter, should pass in valid ops table.\n");
		return -DER_INVAL;
	}
	if (h == NULL) {
		D_ERROR("invalid parameter of NULL heap pointer.\n");
		return -DER_INVAL;
	}

	h->d_bh_ops	  = ops;
	h->d_bh_nodes_cnt  = 0;
	h->d_bh_hwm	  = 0;
	h->d_bh_priv	  = priv;
	h->d_bh_feats	  = feats;

	while (h->d_bh_hwm < count) { /* preallocate */
	rc = d_binheap_grow(h);
		if (rc != 0) {
			D_ERROR("d_binheap_grow() failed, " DF_RC "\n",
				DP_RC(rc));
			d_binheap_destroy_inplace(h);
			return rc;
		}
	}

	rc = dbh_lock_init(h);
	if (rc != 0) {
		D_ERROR("dbg_lock_init() failed, " DF_RC "\n", DP_RC(rc));
		d_binheap_destroy_inplace(h);
	}

	return rc;
}

int
d_binheap_create(uint32_t feats, uint32_t count, void *priv,
		 struct d_binheap_ops *ops, struct d_binheap **h)
{
	struct d_binheap	*bh_created;
	int			 rc;

	if (ops == NULL || ops->hop_compare == NULL) {
		D_ERROR("invalid parameter, should pass in valid ops table.\n");
		return -DER_INVAL;
	}
	if (h == NULL) {
		D_ERROR("invalid parameter of NULL heap 2nd level pointer.\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(bh_created);
	if (bh_created == NULL)
		return -DER_NOMEM;

	rc = d_binheap_create_inplace(feats, count, priv, ops, bh_created);
	if (rc != 0) {
		D_ERROR("d_binheap_create() failed, " DF_RC "\n", DP_RC(rc));
		D_FREE_PTR(bh_created);
		return rc;
	}

	*h = bh_created;

	return rc;
}

void
d_binheap_destroy_inplace(struct d_binheap *h)
{
	uint32_t	idx0, idx1, n;

	if (h == NULL) {
		D_ERROR("ignore invalid parameter of NULL heap.\n");
		return;
	}

	n = h->d_bh_hwm;

	if (n > 0) {
		D_FREE(h->d_bh_nodes1);
		n -= DBH_SIZE;
	}

	if (n > 0) {
		for (idx0 = 0; idx0 < DBH_SIZE && n > 0; idx0++) {
			D_FREE(h->d_bh_nodes2[idx0]);
			n -= DBH_SIZE;
		}

		D_FREE(h->d_bh_nodes2);
	}

	if (n > 0) {
		for (idx0 = 0; idx0 < DBH_SIZE && n > 0; idx0++) {

			for (idx1 = 0; idx1 < DBH_SIZE && n > 0; idx1++) {
				D_FREE(h->d_bh_nodes3[idx0][idx1]);
				n -= DBH_SIZE;
			}

			D_FREE(h->d_bh_nodes3[idx0]);
		}

		D_FREE(h->d_bh_nodes3);
	}

	dbh_lock_fini(h);

	memset(h, 0, sizeof(*h));
}

void
d_binheap_destroy(struct d_binheap *h)
{
	if (h == NULL) {
		D_ERROR("ignore invalid parameter of NULL heap.\n");
		return;
	}

	d_binheap_destroy_inplace(h);
	D_FREE_PTR(h);
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
static struct d_binheap_node **
d_binheap_pointer(struct d_binheap *h, uint32_t idx)
{
	if (idx < DBH_SIZE)
		return &(h->d_bh_nodes1[idx]);

	idx -= DBH_SIZE;
	if (idx < DBH_SIZE * DBH_SIZE)
		return &(h->d_bh_nodes2[idx >> DBH_SHIFT][idx & DBH_MASK]);

	idx -= DBH_SIZE * DBH_SIZE;
	return &(h->d_bh_nodes3[idx >> (2 * DBH_SHIFT)]
				 [(idx >> DBH_SHIFT) & DBH_MASK]
				 [idx & DBH_MASK]);
}

static struct d_binheap_node *
d_binheap_find_locked(struct d_binheap *h, uint32_t idx)
{
	struct d_binheap_node **node;

	if (h == NULL) {
		D_ERROR("ignore NULL heap.\n");
		return NULL;
	}

	if (idx >= h->d_bh_nodes_cnt)
		return NULL;

	node = d_binheap_pointer(h, idx);

	return *node;
}

struct d_binheap_node *
d_binheap_find(struct d_binheap *h, uint32_t idx)
{
	struct d_binheap_node *node;

	dbh_lock(h, true /* read-only */);
	node = d_binheap_find_locked(h, idx);
	dbh_unlock(h, true /* read-only */);

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
d_binheap_bubble(struct d_binheap *h, struct d_binheap_node *e)
{
	struct d_binheap_node		**cur_ptr;
	struct d_binheap_node		**parent_ptr;
	uint32_t			  cur_idx;
	uint32_t			  parent_idx;
	int				  did_sth = 0;

	D_ASSERT(h != NULL && e != NULL);
	cur_idx = e->chn_idx;
	cur_ptr = d_binheap_pointer(h, cur_idx);
	D_ASSERT(*cur_ptr == e);

	while (cur_idx > 0) {
		parent_idx = (cur_idx - 1) >> 1;

		parent_ptr = d_binheap_pointer(h, parent_idx);
		D_ASSERT((*parent_ptr)->chn_idx == parent_idx);

		if (h->d_bh_ops->hop_compare(*parent_ptr, e))
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
d_binheap_sink(struct d_binheap *h, struct d_binheap_node *e)
{
	struct d_binheap_node		**child_ptr;
	struct d_binheap_node		 *child;
	struct d_binheap_node		**child2_ptr;
	struct d_binheap_node		  *child2;
	struct d_binheap_node		**cur_ptr;
	uint32_t			  child2_idx;
	uint32_t			  child_idx;
	uint32_t			  cur_idx;
	uint32_t			  n;
	int				  did_sth = 0;

	D_ASSERT(h != NULL && e != NULL);

	n = h->d_bh_nodes_cnt;
	cur_idx = e->chn_idx;
	cur_ptr = d_binheap_pointer(h, cur_idx);
	D_ASSERT(*cur_ptr == e);

	while (cur_idx < n) {
		child_idx = (cur_idx << 1) + 1;
		if (child_idx >= n)
			break;

		child_ptr = d_binheap_pointer(h, child_idx);
		child = *child_ptr;

		child2_idx = child_idx + 1;
		if (child2_idx < n) {
			child2_ptr = d_binheap_pointer(h, child2_idx);
			child2 = *child2_ptr;

			if (h->d_bh_ops->hop_compare(child2, child)) {
				child_idx = child2_idx;
				child_ptr = child2_ptr;
				child = child2;
			}
		}

		D_ASSERT(child->chn_idx == child_idx);

		if (h->d_bh_ops->hop_compare(e, child))
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
d_binheap_insert(struct d_binheap *h, struct d_binheap_node *e)
{
	struct d_binheap_node		**new_ptr;
	uint32_t			  new_idx;
	int				  rc;

	if (h == NULL || e == NULL) {
		D_ERROR("invalid parameter of NULL h or e.\n");
		return -DER_INVAL;
	}

	dbh_lock(h, false /* read-only */);

	new_idx = h->d_bh_nodes_cnt;
	D_ASSERT(new_idx <= h->d_bh_hwm);
	if (new_idx == h->d_bh_hwm) {
		rc = d_binheap_grow(h);
		if (rc != 0) {
			D_ERROR("d_binheap_grow() failed, " DF_RC "\n",
				DP_RC(rc));
			dbh_unlock(h, false /* read-only */);
			return rc;
		}
	}

	if (h->d_bh_ops->hop_enter) {
		rc = h->d_bh_ops->hop_enter(h, e);
		if (rc != 0) {
			D_ERROR("d_bh_ops->hop_enter() failed, " DF_RC "\n",
				DP_RC(rc));
			dbh_unlock(h, false /* read-only */);
			return rc;
		}
	}

	e->chn_idx = new_idx;
	new_ptr = d_binheap_pointer(h, new_idx);
	h->d_bh_nodes_cnt++;
	*new_ptr = e;

	d_binheap_bubble(h, e);

	dbh_unlock(h, false /* read-only */);

	return 0;
}

static void
d_binheap_remove_locked(struct d_binheap *h, struct d_binheap_node *e)
{
	struct d_binheap_node		**cur_ptr;
	struct d_binheap_node		 *last;
	uint32_t			  cur_idx;
	uint32_t			  n;

	if (h == NULL || e == NULL) {
		D_ERROR("invalid parameter of NULL h or e.\n");
		return;
	}

	n = h->d_bh_nodes_cnt;
	cur_idx = e->chn_idx;

	D_ASSERT(cur_idx != DBH_POISON);
	D_ASSERT(cur_idx < n);

	cur_ptr = d_binheap_pointer(h, cur_idx);
	D_ASSERT(*cur_ptr == e);

	n--;
	last = *d_binheap_pointer(h, n);
	h->d_bh_nodes_cnt = n;
	if (last == e)
		goto out;

	last->chn_idx = cur_idx;
	*cur_ptr = last;
	if (!d_binheap_bubble(h, *cur_ptr))
		d_binheap_sink(h, *cur_ptr);

out:
	e->chn_idx = DBH_POISON;
	if (h->d_bh_ops->hop_exit)
		h->d_bh_ops->hop_exit(h, e);
}

void
d_binheap_remove(struct d_binheap *h, struct d_binheap_node *e)
{
	dbh_lock(h, false /* read-only */);
	d_binheap_remove_locked(h, e);
	dbh_unlock(h, false /* read-only */);
}

struct d_binheap_node *
d_binheap_remove_root(struct d_binheap *h)
{
	struct d_binheap_node *e;

	if (h == NULL) {
		D_ERROR("ignore NULL heap.\n");
		return NULL;
	}

	dbh_lock(h, false /* read-only */);

	e = d_binheap_find_locked(h, 0);
	if (e != NULL)
		d_binheap_remove_locked(h, e);

	dbh_unlock(h, false /* read-only */);

	return e;
}
