/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * This file implements an alternate/external way for ULTs stacks allocation.
 * It is based on mmap() of MAP_STACK|MAP_GROWSDOWN regions, in order to
 * allow overrun detection along with automatic growth capability.
 */

#ifdef ULT_MMAP_STACK
#include <daos/common.h>
#include <daos/stack_mmap.h>
#include <errno.h>
#include <string.h>

/* ABT_key for mmap()'ed ULT stacks */
ABT_key stack_key;

/* engine's (ie including all XStreams/stack-pools) max number of mmap()'ed
 * ULTs stacks, to be based on vm.max_map_count minus an estimate of the
 * non-stacks mmap()'ed regions required (where malloc() itself will use
 * mmap() when allocating chunks of size > M_MMAP_THRESHOLD, and there is
 * a M_MMAP_MAX maximum for such number of chunks, both can be updated
 * dynamically using mallopt() !!...) for engine operations (including
 * pre-reqs ...).
 */
int max_nb_mmap_stacks;

/* engine's (ie including all XStreams/stack-pools) current number of mmap()'ed
 * ULTs stacks, to be [in,de]cremented atomically and compared to
 * max_nb_mmap_stacks
 */
ATOMIC int nb_mmap_stacks;

/* engine's (ie including all XStreams/stack-pools) current number of free/queued
 * mmap()'ed ULTs stacks, to be [in,de]cremented atomically and compared to
 * max_nb_mmap_stacks
 */
ATOMIC int nb_free_stacks;

/* mmap()'ed or Argobot's legacy/internal allocation method for ULT stacks ? */
bool daos_ult_mmap_stack = true;

/* one per supported ABT_thread_create[_...] API type */
enum AbtThreadCreateType {
	MAIN,
	ON_XSTREAM
};

static int call_abt_method(void *arg, enum AbtThreadCreateType flag,
			   void (*thread_func)(void *), void *thread_arg,
			   ABT_thread_attr attr, ABT_thread *newthread)
{
	int rc;

	if (flag == MAIN) {
		rc = ABT_thread_create((ABT_pool)arg, thread_func, thread_arg,
				       attr, newthread);
	} else if (flag == ON_XSTREAM) {
		rc = ABT_thread_create_on_xstream((ABT_xstream)arg, thread_func,
						  thread_arg, attr,
						  newthread);
	} else {
		rc = ABT_ERR_INV_ARG;
		D_ERROR("unsupported ABT_thread_create[_...]() API type");
	}
	return rc;
}

static int compare_stack_size(const void *arg1, const void *arg2)
{
	struct stack_pool_by_size *size1 = (struct stack_pool_by_size *)arg1;
	struct stack_pool_by_size *size2 = (struct stack_pool_by_size *)arg2;

	if (size1->stack_size < size2->stack_size)
		return -1;
	else if (size1->stack_size > size2->stack_size)
		return 1;
	else
		return 0;
}

void stack_pool_by_size_destroy(struct stack_pool *sp, struct stack_pool_by_size *spb)
{
	void *item;
	mmap_stack_desc_t *desc;

	D_ASSERT(sp->nb_sizes != 0 && (!d_list_empty(&sp->stack_size_list) || sp->nb_sizes == 1));
	while ((desc = d_list_pop_entry(&spb->stack_free_list, mmap_stack_desc_t,
					stack_list)) != NULL) {
		D_DEBUG(DB_MEM,
			"Draining a mmap()'ed stack at %p of size %zd, sub-pool=%p, pool=%p, remaining free stacks in pool="DF_U64"\n",
			desc->stack, desc->stack_size, spb, sp, sp->free_stacks);
		munmap(desc->stack, desc->stack_size);
		--sp->free_stacks;
		atomic_fetch_sub(&nb_mmap_stacks, 1);
		atomic_fetch_sub(&nb_free_stacks, 1);
	}
	D_INFO("%d remaining freed stacks, %d remaining allocated\n",
	       atomic_load_relaxed(&nb_free_stacks), atomic_load_relaxed(&nb_mmap_stacks));
	item = tdelete(spb, &sp->root, compare_stack_size);
	if (item == NULL)
		D_ERROR("Size %zu not found in stack_pool %p\n", spb->stack_size, sp);
	d_list_del(&spb->size_list);
	sp->nb_sizes--;
	D_FREE(spb);
}

int stack_pool_by_size_find_or_create(struct stack_pool *sp, struct stack_pool_by_size **spb,
				      size_t size)
{
	void *item;
	struct stack_pool_by_size dummy = {.stack_size = size};

	item = tfind((void *)&dummy, &sp->root, compare_stack_size);
	if (item != NULL) {
		*spb = *((void **)item);
		D_DEBUG(DB_MEM, "sub-pool by-size %p has been found in pool %p for size %zu\n",
			*spb, sp, size);
		return 0;
	}

	/* size not found, create it */
	D_ALLOC(*spb, sizeof(struct stack_pool_by_size));
	if (*spb == NULL)
		return -DER_NOMEM;
	D_INIT_LIST_HEAD(&(*spb)->stack_free_list);
	D_INIT_LIST_HEAD(&(*spb)->size_list);
	(*spb)->stack_size = size;
	item = tsearch((void *)(*spb), &sp->root, compare_stack_size);
	if (item == NULL) {
		return -DER_NOMEM;
	} else if (*((void **)item) == (void *)(*spb)) {
		/* new stack size has been added to the btree */
		sp->nb_sizes++;
		d_list_add_tail(&(*spb)->size_list, &sp->stack_size_list);
		D_DEBUG(DB_MEM, "sub-pool by-size %p has been created in pool %p for size %zu\n",
			*spb, sp, size);
	} else {
		/* this should not happen, size finally exists! so no need new/same one */
		D_DEBUG(DB_MEM,
			"sub-pool by-size %p of size %zu already exists in pool %p finally..., so freeing %p\n",
			*((void **)item), size, sp, *spb);
		D_FREE(*spb);
		*spb = *((void **)item);
	}

	return 0;
}

int mmap_stack_thread_create_common(struct stack_pool *sp, enum AbtThreadCreateType flag,
				    void *arg, void (*thread_func)(void *), void *thread_arg,
				    ABT_thread_attr attr, ABT_thread *newthread)
{
	ABT_thread_attr new_attr = ABT_THREAD_ATTR_NULL;
	int rc;
	void *stack;
	mmap_stack_desc_t *mmap_stack_desc = NULL;
	size_t stack_size = MMAPED_ULT_STACK_SIZE, usable_stack_size;
	struct stack_pool_by_size *spb = NULL;

	if (daos_ult_mmap_stack == false) {
		rc = call_abt_method(arg, flag, thread_func, thread_arg,
				     attr, newthread);
		return rc;
	}

	if (attr != ABT_THREAD_ATTR_NULL) {
		ABT_thread_attr_get_stack(attr, &stack, &stack_size);
		if (stack != NULL) {
			/* an other external stack allocation method is being
			 * used, nothing to do
			 */
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			return rc;
		}
		if (stack_size < MMAPED_ULT_STACK_SIZE)
			stack_size = MMAPED_ULT_STACK_SIZE;
	} else {
		rc = ABT_thread_attr_create(&new_attr);
		if (rc != ABT_SUCCESS) {
			D_ERROR("Create ABT thread attr failed: %d\n", rc);
			return rc;
		}
		attr = new_attr;
	}

	/* XXX a stack is allocated from the creating XStream's stack pool
	 * but will be freed on the running one ...
	 */

	rc = stack_pool_by_size_find_or_create(sp, &spb, stack_size);
	if (rc != 0) {
		D_ERROR("unable to find/create stack sub-pool in pool %p for size %zu : %d\n",
			sp, stack_size, rc);
		/* let's try Argobots standard way ... */
		rc = call_abt_method(arg, flag, thread_func, thread_arg,
				     attr, newthread);
		D_GOTO(out_err, rc);
	}

	if ((mmap_stack_desc = d_list_pop_entry(&spb->stack_free_list,
						mmap_stack_desc_t,
						stack_list)) != NULL) {
		D_ASSERT(sp->free_stacks != 0);
		--sp->free_stacks;
		atomic_fetch_sub(&nb_free_stacks, 1);
		stack = mmap_stack_desc->stack;
		stack_size = mmap_stack_desc->stack_size;
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd from free list, in pool=%p / sub-pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
			stack, stack_size, sp, spb, sp->free_stacks, sched_getcpu());
	} else {
		/* XXX this test is racy, but if max_nb_mmap_stacks value is
		 * high enough it does not matter as we do not expect so many
		 * concurrent ULTs creations during mmap() syscall to cause
		 * nb_mmap_stacks to significantly exceed max_nb_mmap_stacks ...
		 */
		if (nb_mmap_stacks >= max_nb_mmap_stacks) {
			/* use Argobots standard way !! */
			D_INFO("nb_mmap_stacks (%d) > max_nb_mmap_stacks (%d), so using Argobots standard method for stack allocation\n",
			       nb_mmap_stacks, max_nb_mmap_stacks);
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			D_GOTO(out_err, rc);
		}

		stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_GROWSDOWN,
			     -1, 0);
		if (stack == MAP_FAILED) {
			D_ERROR("Failed to mmap() stack of size %zd : %s, in pool=%p / sub-pool=%p, on CPU=%d\n",
				stack_size, strerror(errno), sp, spb, sched_getcpu());
			/* let's try Argobots standard way ... */
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			D_GOTO(out_err, rc);
		}

		atomic_fetch_add(&nb_mmap_stacks, 1);

		/* put descriptor at bottom of mmap()'ed stack */
		mmap_stack_desc = (mmap_stack_desc_t *)(stack + stack_size -
				  sizeof(mmap_stack_desc_t));

		/* start to fill descriptor */
		mmap_stack_desc->stack = stack;
		mmap_stack_desc->stack_size = stack_size;
		/* store target XStream */
		mmap_stack_desc->sp = sp;
		D_INIT_LIST_HEAD(&mmap_stack_desc->stack_list);
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd has been allocated, in pool=%p / sub-pool=%p, on CPU=%d\n",
			stack, stack_size, sp, spb, sched_getcpu());
	}

	/* continue to fill/update descriptor */
	mmap_stack_desc->thread_func = thread_func;
	mmap_stack_desc->thread_arg = thread_arg;

	/* usable stack size */
	usable_stack_size = stack_size - sizeof(mmap_stack_desc_t);

	rc = ABT_thread_attr_set_stack(attr, stack, usable_stack_size);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to set stack attrs : %d\n", rc);
		D_GOTO(out_err, rc);
	}

	/* XXX if newthread is set, we may need to use
	 * ABT_thread_set_specific() ??
	 */
	rc = call_abt_method(arg, flag, mmap_stack_wrapper, mmap_stack_desc,
			     attr, newthread);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to create ULT : %d\n", rc);
		D_GOTO(out_err, rc);
	}
out_err:
	if (rc && mmap_stack_desc != NULL)
		free_stack(mmap_stack_desc);
	/* free local attr if used */
	if (new_attr != ABT_THREAD_ATTR_NULL)
		ABT_thread_attr_free(&new_attr);
	return rc;
}

/* XXX
 * presently ABT_thread_create_[to,many]() are not used in DAOS code, but if it
 * becomes we will also have to introduce a corresponding wrapper
 */

int mmap_stack_thread_create(struct stack_pool *sp, ABT_pool pool,
			     void (*thread_func)(void *), void *thread_arg,
			     ABT_thread_attr attr, ABT_thread *newthread)
{
	return mmap_stack_thread_create_common(sp, MAIN, (void *)pool, thread_func,
					       thread_arg, attr, newthread);
}

int mmap_stack_thread_create_on_xstream(struct stack_pool *sp, ABT_xstream xstream,
					void (*thread_func)(void *), void *thread_arg,
					ABT_thread_attr attr, ABT_thread *newthread)
{
	return mmap_stack_thread_create_common(sp, ON_XSTREAM, (void *)xstream, thread_func,
					       thread_arg, attr, newthread);
}

int stack_pool_create(struct stack_pool **sp)
{
	D_ALLOC(*sp, sizeof(struct stack_pool));
	if (*sp == NULL) {
		D_DEBUG(DB_MEM, "unable to allocate a stack pool\n");
		return -DER_NOMEM;
	}
	(*sp)->root = NULL;
	(*sp)->nb_sizes = 0;
	(*sp)->free_stacks = 0;
	D_INIT_LIST_HEAD(&(*sp)->stack_size_list);
	D_DEBUG(DB_MEM, "pool %p has been allocated\n", *sp);
	return 0;
}

/* simplified version of stack_pool_by_size_destroy() as no (struct stack_pool *)
 * is available ...
 */
void free_stack_pool_by_size(void *arg)
{
	struct stack_pool_by_size *spb = (struct stack_pool_by_size *)arg;
	mmap_stack_desc_t *desc;

	D_ERROR("orphan sub-pool %p found\n", spb);
	/* unmapping its free stacks in pool anyway */

	while ((desc = d_list_pop_entry(&spb->stack_free_list, mmap_stack_desc_t, stack_list)) != NULL) {
		D_DEBUG(DB_MEM,
			"Draining a mmap()'ed stack at %p of size %zd, sub-pool=%p\n",
			desc->stack, desc->stack_size, spb);
		munmap(desc->stack, desc->stack_size);
		atomic_fetch_sub(&nb_mmap_stacks, 1);
		atomic_fetch_sub(&nb_free_stacks, 1);
	}
	D_INFO("%d remaining freed stacks, %d remaining allocated\n",
	       atomic_load_relaxed(&nb_free_stacks), atomic_load_relaxed(&nb_mmap_stacks));
	d_list_del(&spb->size_list);
	D_FREE(spb);
}

void stack_pool_destroy(struct stack_pool *sp)
{
	struct stack_pool_by_size *spb;

	while ((spb = d_list_pop_entry(&sp->stack_size_list, struct stack_pool_by_size, size_list)) != NULL)
		stack_pool_by_size_destroy(sp, spb);
	/* destroy btree, should be empty after calling stack_pool_by_size_destroy() for each
	 * size.
	 */
	tdestroy(sp->root, free_stack_pool_by_size);
	D_ASSERT(sp->nb_sizes == 0 && d_list_empty(&sp->stack_size_list) && sp->root == NULL);
	D_DEBUG(DB_MEM, "pool %p has been freed\n", sp);
	D_FREE(sp);
}

void free_stack_in_pool(mmap_stack_desc_t *desc, struct stack_pool *sp)
{
	bool do_munmap = false;
	struct stack_pool_by_size *spb = NULL;
	int rc;

	/* XXX
	 * We may need to reevaluate stack size since a growth may
	 * have occurred during previous context life time, if initial
	 * stack size has overflowed when there was no previous mapping
	 * in address space to trigger Kernel's stack guard gap
	 * (stack_guard_gap) ? This for both munmap() or queuing in
	 * free pool cases.
	 */

	/* too many free stacks in pool ? */
	if (sp->free_stacks > MAX_NUMBER_FREE_STACKS &&
	    sp->free_stacks / nb_mmap_stacks * 100 > MAX_PERCENT_FREE_STACKS) {
		do_munmap = true;
		atomic_fetch_sub(&nb_mmap_stacks, 1);
	} else {
		rc = stack_pool_by_size_find_or_create(sp, &spb, desc->stack_size);
		if (rc != 0) {
			D_ERROR("unable to find/create stack sub-pool in pool %p for size %zu : %d\n",
				sp, desc->stack_size, rc);
			/* thus munmap() it ... */
			do_munmap = true;
			atomic_fetch_sub(&nb_mmap_stacks, 1);
		} else {
			d_list_add_tail(&desc->stack_list, &spb->stack_free_list);
			++sp->free_stacks;
			atomic_fetch_add(&nb_free_stacks, 1);
		}
	}
	if (do_munmap) {
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd munmap()'ed, in pool=%p / sub-pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
			desc->stack, desc->stack_size, sp, spb, sp->free_stacks,
			sched_getcpu());
		rc = munmap(desc->stack, desc->stack_size);
		/* XXX
		 * should we re-queue it on free list instead to leak it ?
		 */
		if (rc != 0)
			D_ERROR("Failed to munmap() %p stack of size %zd : %s\n",
				desc->stack, desc->stack_size, strerror(errno));
	} else {
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd on free list, in pool=%p / sub-pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
			desc->stack, desc->stack_size, sp, spb, sp->free_stacks,
			sched_getcpu());
	}
}
#endif
