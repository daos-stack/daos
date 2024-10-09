/**
 * (C) Copyright 2021-2024 Intel Corporation.
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

#define D_LOGFAC DD_FAC(stack)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <search.h>
#include <sys/mman.h>

#include <daos/common.h>
#include <daos_srv/daos_engine.h>
#include <daos/daos_abt.h>
#include <daos/ult_stack_mmap.h>

#define MMAP_ULT_STACK_PROT     PROT_READ | PROT_WRITE
#define MMAP_ULT_STACK_FLAGS    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE
#define MMAP_ULT_STACK_FD       -1
#define MMAP_ULT_STACK_OFFSET   0

#define MREMAP_ULT_STACK_PROT   PROT_READ | PROT_WRITE
#define MREMAP_ULT_STACK_FLAGS  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_FIXED | MAP_NORESERVE
#define MREMAP_ULT_STACK_FD     -1
#define MREMAP_ULT_STACK_OFFSET 0

#define MMAP_GUARD_PAGE_PROT    PROT_NONE
#define MMAP_GUARD_PAGE_FLAGS   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE
#define MMAP_GUARD_PAGE_FD      -1
#define MMAP_GUARD_PAGE_OFFSET  0

/** Methods of ULT thread creation */
enum thread_create_flags {
	/** Create a new ULT and schedule it in a given pool */
	TCF_ON_POOL = (1 << 0),
	/** Create a new ULT associated with an execution stream */
	TCF_ON_XSTREAM = (1 << 1)
};

/** Pool of mmap ULT free stack */
struct stack_mmap_pool {
	/** Binary tree holding list of ULT stacks */
	void      *smp_rec_btree;
	/** Lock protecting the binary tree smp_rec_btree */
	ABT_rwlock smp_rec_btree_rwlock;
	/** ABT_key used for calling the callback free_mmap_cb() */
	ABT_key    smp_free_mmap_key;
	/** ABT thread default stack size */
	size_t     smp_thread_stack_size;
	/** Size of memory page */
	size_t     smp_page_size;
};

/** Record of a btree holding a list of mmap ULT free stack with a given size */
struct stack_mmap_rec {
	/** List of free stacks */
	d_list_t  smr_desc_list;
	/** Mutex protecting the list of available stack */
	ABT_mutex smr_desc_list_mutex;
	/** Size of the ULT mmap()'ed stack size */
	size_t    smr_stack_size;
	/** Size of the mmap */
	size_t    smr_mmap_size;
};

/**
 * Descriptor of an Argobots ULT mmap stack.
 *
 * \note: since being allocated before start of stack its size must be aligned.
 */
struct stack_mmap_desc {
	/** Argobots ULT primary function */
	void (*smd_thread_func)(void *);
	/** Argobots ULT arg */
	void                  *smd_thread_arg;
	/** Argobots ULT thread attribute */
	ABT_thread_attr        smd_thread_attr;
	/** starting address of the ULT mmap()'ed stack */
	void                  *smd_thread_stack;
	/** starting address of the stack page guard */
	void                  *smd_guard_page;
	/** Btree record holding this stack */
	struct stack_mmap_rec *smd_rec;
	/** Entry in the list of available mmap()'ed stack */
	d_list_t               smd_entry;
} __attribute__((aligned(sizeof(max_align_t))));

/** Arguments of the thread_create_common() function */
struct thread_args {
	/** ULT thread method creation */
	enum thread_create_flags ta_flags;
	union {
		/** Pool handle of ULT threads */
		ABT_pool    ta_pool;
		/** Execution stream running the ULT thread */
		ABT_xstream ta_xstream;
	};
	/** Function to be executed by the new ULT */
	void (*ta_thread_func)(void *);
	/** Argumemt to the function to be executed */
	void           *ta_thread_arg;
	/** ULT attribute */
	ABT_thread_attr ta_thread_attr;
	/** ULT handle */
	ABT_thread     *ta_newthread;
};

/** Pool of ULT mmap()'ed stack */
static struct stack_mmap_pool g_smp = {0};

static inline size_t
stack_size2mmap_size(size_t stack_size)
{
	return sizeof(struct stack_mmap_desc) + stack_size;
}

static int
stack_mmap_rec_cmp(const void *arg1, const void *arg2)
{
	const struct stack_mmap_rec *rec1 = (const struct stack_mmap_rec *)arg1;
	const struct stack_mmap_rec *rec2 = (const struct stack_mmap_rec *)arg2;

	return rec1->smr_stack_size - rec2->smr_stack_size;
}

static void
free_mmap_cb(void *arg)
{
	struct stack_mmap_desc *desc;
	struct stack_mmap_rec  *rec;
	int                     rc;

	desc = (struct stack_mmap_desc *)arg;
	rec  = desc->smd_rec;

	rc = ABT_thread_attr_free(&desc->smd_thread_attr);
	D_ASSERT(rc == ABT_SUCCESS);

	rc = ABT_mutex_lock(rec->smr_desc_list_mutex);
	D_ASSERT(rc == ABT_SUCCESS);
	d_list_add_tail(&desc->smd_entry, &rec->smr_desc_list);
	rc = ABT_mutex_unlock(rec->smr_desc_list_mutex);
	D_ASSERT(rc == ABT_SUCCESS);
	D_DEBUG(DB_MEM, "Recycling stack %p (desc %p) of size %zu\n", desc->smd_thread_stack, desc,
		desc->smd_rec->smr_stack_size);
}

static void
ult_unnamed_wrapper(void *arg)
{
	struct stack_mmap_desc *desc;
	int                     rc;

	desc = (struct stack_mmap_desc *)arg;

	rc = ABT_key_set(g_smp.smp_free_mmap_key, desc);
	D_ASSERT(rc == ABT_SUCCESS);

	D_DEBUG(DB_MEM, "New unnamed ULT with stack %p (desc %p) running on CPU=%d\n",
		desc->smd_thread_stack, desc, sched_getcpu());
	desc->smd_thread_func(desc->smd_thread_arg);
}

static void
bt_node_destroy_cb(void *node)
{
	struct stack_mmap_rec  *rec;
	struct stack_mmap_desc *desc;

	rec = (struct stack_mmap_rec *)node;
	while ((desc = d_list_pop_entry(&rec->smr_desc_list, struct stack_mmap_desc, smd_entry)) !=
	       NULL) {
		int rc;

		rc = munmap(desc->smd_guard_page, g_smp.smp_page_size);
		if (unlikely(rc != 0))
			DS_ERROR(errno,
				 "Failed to unmap ULT stack guard page at %p: desc=%p, "
				 "mmap_size=%zu, stack_size=%zu, page_size=%zu",
				 desc->smd_guard_page, desc, desc->smd_rec->smr_mmap_size,
				 desc->smd_rec->smr_stack_size, g_smp.smp_page_size);

		rc = munmap(desc->smd_thread_stack, desc->smd_rec->smr_mmap_size);
		if (unlikely(rc != 0))
			DS_ERROR(errno,
				 "Failed to unmap ULT stack at %p: desc=%p, mmap_size=%zu, "
				 "stack_size=%zu",
				 desc->smd_thread_stack, desc, desc->smd_rec->smr_mmap_size,
				 desc->smd_rec->smr_stack_size);
	}
	ABT_mutex_free(&rec->smr_desc_list_mutex);
	D_FREE(rec);
};

static int
smd_find_insert_rec(size_t stack_size, struct stack_mmap_rec **rec)
{
	struct stack_mmap_rec  bt_key  = {.smr_stack_size = stack_size};
	struct stack_mmap_rec *rec_tmp = NULL;
	void                  *tmp;
	int                    rc;

	rc = ABT_rwlock_rdlock(g_smp.smp_rec_btree_rwlock);
	D_ASSERT(rc == ABT_SUCCESS);
	tmp = tfind((void *)&bt_key, &g_smp.smp_rec_btree, stack_mmap_rec_cmp);
	rc  = ABT_rwlock_unlock(g_smp.smp_rec_btree_rwlock);
	D_ASSERT(rc == ABT_SUCCESS);
	if (tmp != NULL) {
		*rec = *(struct stack_mmap_rec **)tmp;
		D_GOTO(out, rc = -DER_SUCCESS);
	}

	D_ALLOC(rec_tmp, sizeof(struct stack_mmap_rec));
	if (unlikely(rec_tmp == NULL))
		D_GOTO(out, rc = -DER_NOMEM);

	rec_tmp->smr_stack_size = stack_size;
	rec_tmp->smr_mmap_size  = stack_size2mmap_size(stack_size);
	D_INIT_LIST_HEAD(&rec_tmp->smr_desc_list);
	rc = ABT_mutex_create(&rec_tmp->smr_desc_list_mutex);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to create ABT mutex: " AF_RC "\n", AP_RC(rc));
		D_GOTO(error_rec_tmp, rc = dss_abterr2der(rc));
	}

	rc = ABT_rwlock_wrlock(g_smp.smp_rec_btree_rwlock);
	D_ASSERT(rc == ABT_SUCCESS);
	tmp = (struct stack_mmap_rec *)tsearch(rec_tmp, &g_smp.smp_rec_btree, stack_mmap_rec_cmp);
	rc  = ABT_rwlock_unlock(g_smp.smp_rec_btree_rwlock);
	D_ASSERT(rc == ABT_SUCCESS);

	if (unlikely(tmp == NULL)) {
		DL_ERROR(-DER_NOMEM, "Failed to create new btree node");
		D_GOTO(error_mutex, rc = -DER_NOMEM);
	}

	if (unlikely(*(struct stack_mmap_rec **)tmp != rec_tmp)) {
		ABT_mutex_free(&rec_tmp->smr_desc_list_mutex);
		D_FREE(rec_tmp);
		rec_tmp = *(struct stack_mmap_rec **)tmp;
	}

	*rec = rec_tmp;
	D_DEBUG(DB_MEM, "New btree record %p of size %zu (mmap size %zu)\n", *rec,
		(*rec)->smr_stack_size, (*rec)->smr_mmap_size);
	D_GOTO(out, rc = -DER_SUCCESS);

error_mutex:
	ABT_mutex_free(&rec_tmp->smr_desc_list_mutex);
error_rec_tmp:
	D_FREE(rec_tmp);
out:
	return rc;
}

static int
smd_find_insert_desc(size_t stack_size, struct stack_mmap_desc **desc)
{
	struct stack_mmap_rec  *rec;
	struct stack_mmap_desc *tmp;
	void                   *guard_page = NULL;
	void                   *buf;
	size_t                  buf_size;
	int                     rc;

	rc = smd_find_insert_rec(stack_size, &rec);
	if (unlikely(rc != 0))
		D_GOTO(out, rc);

	rc = ABT_mutex_lock(rec->smr_desc_list_mutex);
	D_ASSERT(rc == ABT_SUCCESS);
	tmp = d_list_pop_entry(&rec->smr_desc_list, struct stack_mmap_desc, smd_entry);
	rc  = ABT_mutex_unlock(rec->smr_desc_list_mutex);
	D_ASSERT(rc == ABT_SUCCESS);

	if (tmp != NULL) {
		D_DEBUG(DB_MEM, "Reuse recycled stack %p (desc %p) of size %zu\n",
			tmp->smd_thread_stack, tmp, stack_size);
		*desc = tmp;
		D_GOTO(out, rc = -DER_SUCCESS);
	}

	buf_size = rec->smr_mmap_size + g_smp.smp_page_size;
	buf = mmap(NULL, buf_size, MMAP_ULT_STACK_PROT, MMAP_ULT_STACK_FLAGS, MMAP_ULT_STACK_FD,
		   MMAP_ULT_STACK_OFFSET);
	if (unlikely(buf == MAP_FAILED)) {
		rc = daos_errno2der(errno);
		DS_ERROR(errno, "Failed to mmap() stack of size %zu (mmap size %zu)", stack_size,
			 rec->smr_mmap_size);
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_MEM, "Reserved mmap()'ed stack at %p (mmap size %zu)\n", buf, buf_size);

	guard_page = mmap(buf, g_smp.smp_page_size, MMAP_GUARD_PAGE_PROT, MMAP_GUARD_PAGE_FLAGS,
			  MMAP_GUARD_PAGE_FD, MMAP_GUARD_PAGE_OFFSET);
	if (unlikely(guard_page == MAP_FAILED)) {
		rc = daos_errno2der(errno);
		DS_ERROR(errno, "Failed to mmap() guard page at %p", buf);
		D_GOTO(error_buf, rc);
	}
	D_ASSERT(guard_page == buf);
	D_DEBUG(DB_MEM, "Remap guard page at %p (page size %zu)\n", guard_page,
		g_smp.smp_page_size);

	buf_size -= g_smp.smp_page_size;
	buf = (char *)buf + g_smp.smp_page_size;
	buf = mmap(buf, buf_size, MREMAP_ULT_STACK_PROT, MREMAP_ULT_STACK_FLAGS,
		   MREMAP_ULT_STACK_FD, MREMAP_ULT_STACK_OFFSET);
	if (unlikely(buf == MAP_FAILED)) {
		rc = daos_errno2der(errno);
		DS_ERROR(errno,
			 "Failed to remap guarded stack of size %zu (mmap size %zu) from %p to %p",
			 stack_size, rec->smr_mmap_size, guard_page, buf);
		D_GOTO(error_guard_page, rc);
	}
	D_ASSERT((char *)guard_page + g_smp.smp_page_size == buf);
	D_DEBUG(DB_MEM, "Remap mmap()'ed stack at %p (mmap size %zu)\n", buf, buf_size);

	tmp = (struct stack_mmap_desc *)(buf + rec->smr_mmap_size - sizeof(struct stack_mmap_desc));
	tmp->smd_rec          = rec;
	tmp->smd_thread_stack = buf;
	tmp->smd_guard_page   = guard_page;
	D_INIT_LIST_HEAD(&tmp->smd_entry);

	*desc = tmp;
	D_DEBUG(DB_MEM, "Created new mmap()'ed stack %p (desc %p) of size %zu (mmap size %zu)\n",
		(*desc)->smd_thread_stack, *desc, rec->smr_stack_size, rec->smr_mmap_size);
	D_GOTO(out, rc = -DER_SUCCESS);

error_guard_page:
	munmap(guard_page, g_smp.smp_page_size);
error_buf:
	munmap(buf, buf_size);
out:
	return rc;
}

static int
abt_thread_create_common(struct thread_args *args)
{
	int rc;

	switch (args->ta_flags) {
	case TCF_ON_POOL:
		rc = ABT_thread_create(args->ta_pool, args->ta_thread_func, args->ta_thread_arg,
				       args->ta_thread_attr, args->ta_newthread);
		break;
	case TCF_ON_XSTREAM:
		rc = ABT_thread_create_on_xstream(args->ta_xstream, args->ta_thread_func,
						  args->ta_thread_arg, args->ta_thread_attr,
						  args->ta_newthread);
		break;
	default:
		D_ERROR("Not using mmap stack ULT: "
			"Unsupported type of thread creation (type=0x%x)\n",
			args->ta_flags);
		D_ASSERT(false);
		break;
	}

	return rc;
}

static int
thread_create_common(struct thread_args *args)
{
	const bool is_unnamed              = (args->ta_newthread == NULL);
	void (*thread_func)(void *)        = args->ta_thread_func;
	void                   *thread_arg = args->ta_thread_arg;
	ABT_thread_attr         attr       = ABT_THREAD_ATTR_NULL;
	size_t                  stack_size = g_smp.smp_thread_stack_size;
	struct stack_mmap_desc *desc       = NULL;
	int                     rc;

	if (args->ta_thread_attr != ABT_THREAD_ATTR_NULL) {
		void *stack = NULL;

		rc = ABT_thread_attr_get_stack(args->ta_thread_attr, &stack, &stack_size);
		if (unlikely(rc != ABT_SUCCESS)) {
			D_ERROR("Failed to retrieve ULT stack attributes: " AF_RC "\n", AP_RC(rc));
			D_GOTO(out, rc);
		}
		if (stack != NULL) {
			D_INFO("Not using mmap stack ULT: using dedicated stack allocator.\n");
			rc = abt_thread_create_common(args);
			D_GOTO(out, rc);
		}
	}

	/** FIXME migrable and callback properties of the thread attribute will not be duplicated as
	 * it is not possible to get info on it */
	rc = ABT_thread_attr_create(&attr);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to create ABT thread attr: " AF_RC "\n", AP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = smd_find_insert_desc(stack_size, &desc);
	if (unlikely(rc != 0)) {
		DL_ERROR(rc, "Not using mmap stack ULT: Failed to find/create stack of size %zu",
			 stack_size);
		D_GOTO(error_attr, rc);
	}
	D_ASSERT(desc != NULL);

	desc->smd_thread_func = args->ta_thread_func;
	desc->smd_thread_arg  = args->ta_thread_arg;
	desc->smd_thread_attr = attr;
	rc                    = ABT_thread_attr_set_stack(attr, desc->smd_thread_stack, stack_size);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to set stack thread attributes : " AF_RC "\n", AP_RC(rc));
		D_GOTO(error_desc, rc);
	}

	if (is_unnamed) {
		thread_func = ult_unnamed_wrapper;
		thread_arg  = desc;
	}

	switch (args->ta_flags) {
	case TCF_ON_POOL:
		rc = ABT_thread_create(args->ta_pool, thread_func, thread_arg, attr,
				       args->ta_newthread);
		break;
	case TCF_ON_XSTREAM:
		rc = ABT_thread_create_on_xstream(args->ta_xstream, thread_func, thread_arg, attr,
						  args->ta_newthread);
		break;
	default:
		D_ERROR("Not using mmap stack ULT: "
			"Unsupported type of thread creation (type=0x%x)\n",
			args->ta_flags);
		D_ASSERT(false);
		break;
	}
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to create ULT : " AF_RC "\n", AP_RC(rc));
		D_GOTO(error_desc, rc);
	}

	if (!is_unnamed) {
		rc = ABT_thread_set_specific(*args->ta_newthread, g_smp.smp_free_mmap_key, desc);
		if (unlikely(rc != ABT_SUCCESS)) {
			D_ERROR("Failed to set ULT stack free callback: " AF_RC "\n", AP_RC(rc));
			D_GOTO(error_thread, rc);
		}
	}

	D_DEBUG(DB_MEM, "Created new %s ULT with mmap'ed() stack %p (stack size=%zu)\n",
		is_unnamed ? "unnamed" : "named", desc->smd_thread_stack, stack_size);
	D_GOTO(out, rc = ABT_SUCCESS);

error_thread:
	ABT_thread_cancel(*args->ta_newthread);
	ABT_thread_join(*args->ta_newthread);
error_desc:
	free_mmap_cb(desc);
	attr = ABT_THREAD_ATTR_NULL;
error_attr:
	if (attr != ABT_THREAD_ATTR_NULL)
		ABT_thread_attr_free(&attr);
out:
	return rc;
}

int
usm_initialize(void)
{
	int rc;

	g_smp.smp_rec_btree = NULL;
	g_smp.smp_page_size = (size_t)getpagesize();

	rc = ABT_info_query_config(ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE,
				   &g_smp.smp_thread_stack_size);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Init of ULT mmap()'ed stack allocation failed: "
			"Unable to retrieve default ULT stack size: " AF_RC "\n",
			AP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = ABT_key_create(free_mmap_cb, &g_smp.smp_free_mmap_key);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Init of ULT mmap()'ed stack allocation failed: "
			"Creation of ABT key for calling free_mmap_cb() failed: " AF_RC "\n",
			AP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = ABT_rwlock_create(&g_smp.smp_rec_btree_rwlock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Init of ULT mmap()'ed stack allocation failed: "
			"Creation of btree's ABT lock failed: " AF_RC "\n",
			AP_RC(rc));
		D_GOTO(error_key, rc);
	}

	D_GOTO(out, rc = ABT_SUCCESS);

error_key:
	ABT_key_free(&g_smp.smp_free_mmap_key);
out:
	return rc;
}

void
usm_finalize(void)
{
	ABT_rwlock_free(&g_smp.smp_rec_btree_rwlock);
	ABT_key_free(&g_smp.smp_free_mmap_key);
	tdestroy(g_smp.smp_rec_btree, bt_node_destroy_cb);
}

int
usm_thread_create_on_pool(ABT_pool pool, void (*thread_func)(void *), void *thread_arg,
			  ABT_thread_attr attr, ABT_thread *newthread)
{
	struct thread_args args = {.ta_flags       = TCF_ON_POOL,
				   .ta_pool        = pool,
				   .ta_thread_func = thread_func,
				   .ta_thread_arg  = thread_arg,
				   .ta_thread_attr = attr,
				   .ta_newthread   = newthread};

	return thread_create_common(&args);
}

int
usm_thread_create_on_xstream(ABT_xstream xstream, void (*thread_func)(void *), void *thread_arg,
			     ABT_thread_attr attr, ABT_thread *newthread)
{
	struct thread_args args = {.ta_flags       = TCF_ON_XSTREAM,
				   .ta_xstream     = xstream,
				   .ta_thread_func = thread_func,
				   .ta_thread_arg  = thread_arg,
				   .ta_thread_attr = attr,
				   .ta_newthread   = newthread};

	return thread_create_common(&args);
}

int
usm_thread_get_func(ABT_thread thread, void (**func)(void *))
{
	ABT_bool                is_unnamed;
	struct stack_mmap_desc *desc;
	int                     rc;

	rc = ABT_thread_is_unnamed(thread, &is_unnamed);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to get ULT thread type: " AF_RC "\n", AP_RC(rc));
		return rc;
	}

	if (is_unnamed == ABT_FALSE)
		return ABT_thread_get_thread_func(thread, func);

	rc = ABT_thread_get_arg(thread, (void **)&desc);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to get ULT thread arg: " AF_RC "\n", AP_RC(rc));
		return rc;
	}

	*func = desc->smd_thread_func;
	rc    = ABT_SUCCESS;

	return rc;
}

int
usm_thread_get_arg(ABT_thread thread, void **arg)
{
	ABT_bool                is_unnamed;
	struct stack_mmap_desc *desc;
	int                     rc;

	rc = ABT_thread_is_unnamed(thread, &is_unnamed);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to get ULT thread type: " AF_RC "\n", AP_RC(rc));
		return rc;
	}

	if (is_unnamed == ABT_FALSE)
		return ABT_thread_get_arg(thread, arg);

	rc = ABT_thread_get_arg(thread, (void **)&desc);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to get ULT thread arg: " AF_RC "\n", AP_RC(rc));
		return rc;
	}

	*arg = desc->smd_thread_arg;
	rc   = ABT_SUCCESS;

	return rc;
}
