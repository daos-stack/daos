/**
 * (C) Copyright 2021-2022 Intel Corporation.
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
#define D_LOGFAC DD_FAC(stack)

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

static int
call_abt_method(void *arg, enum AbtThreadCreateType flag,
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
		D_ERROR("unsupported ABT_thread_create[_...]() API type\n");
	}
	return rc;
}

/* wrapper for ULT main function, mainly to register mmap()'ed stack
 * descriptor as ABT_key to ensure stack pooling or munmap() upon ULT exit
 */
void mmap_stack_wrapper(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;

	ABT_key_set(stack_key, desc);

	D_DEBUG(DB_MEM,
		"New ULT with stack_desc %p running on CPU=%d\n",
		desc, sched_getcpu());
	desc->thread_func(desc->thread_arg);
}

static int
mmap_stack_thread_create_common(struct stack_pool *sp_alloc, void (*free_stack_cb)(void *),
				enum AbtThreadCreateType flag, void *arg,
				void (*thread_func)(void *), void *thread_arg,
				ABT_thread_attr attr, ABT_thread *newthread)
{
	ABT_thread_attr new_attr = ABT_THREAD_ATTR_NULL;
	int rc;
	void *stack;
	mmap_stack_desc_t *mmap_stack_desc = NULL;
	size_t stack_size = MMAPED_ULT_STACK_SIZE, usable_stack_size;

	if (daos_ult_mmap_stack == false) {
		/* let's use Argobots standard way ... */
		rc = call_abt_method(arg, flag, thread_func, thread_arg,
				     attr, newthread);
		if (unlikely(rc != ABT_SUCCESS))
			D_ERROR("Failed to create ULT : %d\n", rc);
		D_GOTO(out_err, rc);
	}

	if (attr != ABT_THREAD_ATTR_NULL) {
		ABT_thread_attr_get_stack(attr, &stack, &stack_size);
		if (stack != NULL) {
			/* an other external stack allocation method is being
			 * used, nothing to do, let's try Argobots standard way ...
			 */
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			if (unlikely(rc != ABT_SUCCESS))
				D_ERROR("Failed to create ULT : %d\n", rc);
			D_GOTO(out_err, rc);
		}
		/* only one mmap()'ed stack size allowed presently */
		if (stack_size > MMAPED_ULT_STACK_SIZE)
			D_WARN("We do not support stacks > %u\n", MMAPED_ULT_STACK_SIZE);
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

	if ((mmap_stack_desc = d_list_pop_entry(&sp_alloc->sp_stack_free_list,
						mmap_stack_desc_t,
						stack_list)) != NULL) {
		D_ASSERT(sp_alloc->sp_free_stacks != 0);
		--sp_alloc->sp_free_stacks;
		atomic_fetch_sub(&nb_free_stacks, 1);
		stack = mmap_stack_desc->stack;
		stack_size = mmap_stack_desc->stack_size;
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd from free list, in pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
			stack, stack_size, sp_alloc, sp_alloc->sp_free_stacks, sched_getcpu());
	} else {
		/* XXX this test is racy, but if max_nb_mmap_stacks value is
		 * high enough it does not matter as we do not expect so many
		 * concurrent ULTs creations during mmap() syscall to cause
		 * nb_mmap_stacks to significantly exceed max_nb_mmap_stacks ...
		 */
		if (nb_mmap_stacks >= max_nb_mmap_stacks) {
			D_INFO("nb_mmap_stacks (%d) > max_nb_mmap_stacks (%d), so using Argobots standard method for stack allocation\n",
			       nb_mmap_stacks, max_nb_mmap_stacks);
			/* let's try Argobots standard way ... */
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			if (unlikely(rc != ABT_SUCCESS))
				D_ERROR("Failed to create ULT : %d\n", rc);
			D_GOTO(out_err, rc);
		}

		stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_GROWSDOWN,
			     -1, 0);
		if (stack == MAP_FAILED) {
			D_ERROR("Failed to mmap() stack of size %zd : %s, in pool=%p, on CPU=%d\n",
				stack_size, strerror(errno), sp_alloc, sched_getcpu());
			/* let's try Argobots standard way ... */
			rc = call_abt_method(arg, flag, thread_func, thread_arg,
					     attr, newthread);
			if (unlikely(rc != ABT_SUCCESS))
				D_ERROR("Failed to create ULT : %d\n", rc);
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
		mmap_stack_desc->sp = sp_alloc;
		D_INIT_LIST_HEAD(&mmap_stack_desc->stack_list);
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd has been allocated, in pool=%p, on CPU=%d\n",
			stack, stack_size, sp_alloc, sched_getcpu());
	}

	/* continue to fill/update descriptor */
	mmap_stack_desc->thread_func = thread_func;
	mmap_stack_desc->thread_arg = thread_arg;
	mmap_stack_desc->free_stack_cb = free_stack_cb;

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
	if (unlikely(rc != ABT_SUCCESS)) {
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

int
mmap_stack_thread_create(struct stack_pool *sp_alloc, void (*free_stack_cb)(void *),
			 ABT_pool pool, void (*thread_func)(void *), void *thread_arg,
			 ABT_thread_attr attr, ABT_thread *newthread)
{
	return mmap_stack_thread_create_common(sp_alloc, free_stack_cb, MAIN, (void *)pool, thread_func,
					       thread_arg, attr, newthread);
}

int
mmap_stack_thread_create_on_xstream(struct stack_pool *sp_alloc, void (*free_stack_cb)(void *),
				    ABT_xstream xstream, void (*thread_func)(void *),
				    void *thread_arg, ABT_thread_attr attr,
				    ABT_thread *newthread)
{
	return mmap_stack_thread_create_common(sp_alloc, free_stack_cb, ON_XSTREAM, (void *)xstream,
					       thread_func, thread_arg, attr, newthread);
}

/* callback to free stack upon ULT exit during stack_key deregister */
void
free_stack(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;
	struct stack_pool *sp;
	int rc;

	if (desc->free_stack_cb != NULL)
		desc->free_stack_cb(arg);

	/* callback may have re-evaluated pool where to free stack */
	sp = desc->sp;

	/* XXX
	 * We may need to reevaluate stack size since a growth may
	 * have occurred during previous context life time, if initial
	 * stack size has overflowed when there was no previous mapping
	 * in address space to trigger Kernel's stack guard gap
	 * (stack_guard_gap) ? This for both munmap() or queuing in
	 * free pool cases.
	 */

	/* too many free stacks in pool ? */
	if (sp->sp_free_stacks > MAX_NUMBER_FREE_STACKS &&
	    sp->sp_free_stacks / nb_mmap_stacks * 100 > MAX_PERCENT_FREE_STACKS) {
		rc = munmap(desc->stack, desc->stack_size);
		if (rc != 0) {
			D_ERROR("Failed to munmap() %p stack of size %zd : %s\n",
				desc->stack, desc->stack_size, strerror(errno));
			/* re-queue it on free list instead to leak it */
			d_list_add_tail(&desc->stack_list, &sp->sp_stack_free_list);
			++sp->sp_free_stacks;
			atomic_fetch_add(&nb_free_stacks, 1);
		} else {
			atomic_fetch_sub(&nb_mmap_stacks, 1);
			D_DEBUG(DB_MEM,
				"mmap()'ed stack %p of size %zd munmap()'ed, in pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
				desc->stack, desc->stack_size, sp, sp->sp_free_stacks,
				sched_getcpu());
		}
	} else {
		d_list_add_tail(&desc->stack_list, &sp->sp_stack_free_list);
		++sp->sp_free_stacks;
		atomic_fetch_add(&nb_free_stacks, 1);
		D_DEBUG(DB_MEM,
			"mmap()'ed stack %p of size %zd on free list, in pool=%p, remaining free stacks in pool="DF_U64", on CPU=%d\n",
			desc->stack, desc->stack_size, sp, sp->sp_free_stacks,
			sched_getcpu());
	}
}

int
stack_pool_create(struct stack_pool **sp)
{
	D_ALLOC(*sp, sizeof(struct stack_pool));
	if (*sp == NULL) {
		D_DEBUG(DB_MEM, "unable to allocate a stack pool\n");
		return -DER_NOMEM;
	}
	(*sp)->sp_free_stacks = 0;
	D_INIT_LIST_HEAD(&(*sp)->sp_stack_free_list);
	D_DEBUG(DB_MEM, "pool %p has been allocated\n", *sp);
	return 0;
}

void stack_pool_destroy(struct stack_pool *sp)
{
	mmap_stack_desc_t *desc;
	int rc;

	while ((desc = d_list_pop_entry(&sp->sp_stack_free_list, mmap_stack_desc_t, stack_list)) != NULL) {
		D_DEBUG(DB_MEM, "munmap() of pool %p, desc %p, stack %p of size %zu, ",
			sp, desc, desc->stack, desc->stack_size);
		rc = munmap(desc->stack, desc->stack_size);
		D_DEBUG(DB_MEM, "has been %ssuccessfully munmap()'ed%s%s\n",
			(rc ? "un" : ""), (rc ? " : " : ""), (rc ? strerror(errno) : ""));
		--sp->sp_free_stacks;
		atomic_fetch_sub(&nb_mmap_stacks, 1);
		atomic_fetch_sub(&nb_free_stacks, 1);
	}
	D_ASSERT(sp->sp_free_stacks == 0);
	D_DEBUG(DB_MEM, "pool %p has been freed\n", sp);
	D_FREE(sp);
}
#endif
