/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_H_INCLUDED
#define ABTI_MEM_H_INCLUDED

/* Memory allocation */

/* Round desc_size up to the cacheline size.  The last four bytes will be
 * used to determine whether the descriptor is allocated externally (i.e.,
 * malloc()) or taken from a memory pool. */
#define ABTI_MEM_POOL_DESC_ELEM_SIZE                                           \
    ABTU_roundup_size(sizeof(ABTI_ythread), ABT_CONFIG_STATIC_CACHELINE_SIZE)

enum {
    ABTI_MEM_LP_MALLOC = 0,
    ABTI_MEM_LP_MMAP_RP,
    ABTI_MEM_LP_MMAP_HP_RP,
    ABTI_MEM_LP_MMAP_HP_THP,
    ABTI_MEM_LP_THP
};

ABTU_ret_err int ABTI_mem_init(ABTI_global *p_global);
ABTU_ret_err int ABTI_mem_init_local(ABTI_global *p_global,
                                     ABTI_xstream *p_local_xstream);
void ABTI_mem_finalize(ABTI_global *p_global);
void ABTI_mem_finalize_local(ABTI_xstream *p_local_xstream);
int ABTI_mem_check_lp_alloc(ABTI_global *p_global, int lp_alloc);

#define ABTI_STACK_CANARY_VALUE ((uint64_t)0xbaadc0debaadc0de)

/* Inline functions */
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
static inline void ABTI_mem_write_stack_canary(void *p_stack)
{
    /* Write down stack canary. */
    uint64_t i;
    for (i = 0; i < ABTU_roundup_uint64(ABT_CONFIG_STACK_CHECK_CANARY_SIZE, 8);
         i += sizeof(uint64_t)) {
        ((uint64_t *)p_stack)[i] = ABTI_STACK_CANARY_VALUE;
    }
}

static inline void ABTI_mem_check_stack_canary(void *p_stack)
{
    uint64_t i;
    for (i = 0; i < ABTU_roundup_uint64(ABT_CONFIG_STACK_CHECK_CANARY_SIZE, 8);
         i += sizeof(uint64_t)) {
        ABTI_ASSERT(((uint64_t *)p_stack)[i] == ABTI_STACK_CANARY_VALUE);
    }
}
#endif

/* p_stack can be NULL. */
static inline void ABTI_mem_register_stack(const ABTI_global *p_global,
                                           void *p_stacktop, size_t stacksize,
                                           ABT_bool mprotect_if_needed)
{
    void *p_stack = (void *)(((char *)p_stacktop) - stacksize);
    if (mprotect_if_needed) {
        if (p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT ||
            p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT_STRICT) {
            if (p_stack) {
                int abt_errno =
                    ABTU_mprotect(ABTU_roundup_ptr(p_stack,
                                                   p_global->sys_page_size),
                                  p_global->sys_page_size, ABT_TRUE);
                if (p_global->stack_guard_kind ==
                    ABTI_STACK_GUARD_MPROTECT_STRICT) {
                    ABTI_ASSERT(abt_errno == ABT_SUCCESS);
                }
            }
        } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
            if (p_stack) {
                ABTI_mem_write_stack_canary(p_stack);
            }
#endif
        }
    } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
        if (!(p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT ||
              p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT_STRICT) &&
            p_stack) {
            ABTI_mem_write_stack_canary(p_stack);
        }
#endif
    }
    ABTI_VALGRIND_REGISTER_STACK(p_stack, stacksize);
}

static inline void ABTI_mem_unregister_stack(const ABTI_global *p_global,
                                             void *p_stacktop, size_t stacksize,
                                             ABT_bool mprotect_if_needed)
{
    void *p_stack = (void *)(((char *)p_stacktop) - stacksize);
    if (mprotect_if_needed) {
        if (p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT ||
            p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT_STRICT) {
            if (p_stack) {
                int abt_errno =
                    ABTU_mprotect(ABTU_roundup_ptr(p_stack,
                                                   p_global->sys_page_size),
                                  p_global->sys_page_size, ABT_FALSE);
                /* This should not fail since otherwise we cannot free this
                 * memory. */
                ABTI_ASSERT(abt_errno == ABT_SUCCESS);
            }
        } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
            if (p_stack) {
                ABTI_mem_check_stack_canary(p_stack);
            }
#endif
        }
    } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
        if (!(p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT ||
              p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT_STRICT) &&
            p_stack) {
            ABTI_mem_check_stack_canary(p_stack);
        }
#endif
    }
    ABTI_VALGRIND_UNREGISTER_STACK(p_stack);
}

ABTU_ret_err static inline int ABTI_mem_alloc_nythread(ABTI_local *p_local,
                                                       ABTI_thread **pp_thread)
{
    ABTI_STATIC_ASSERT(sizeof(ABTI_thread) <= ABTI_MEM_POOL_DESC_ELEM_SIZE);
    ABTI_thread *p_thread;
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
    if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream) {
        /* It's not called on an external thread.  Use a memory pool. */
        int abt_errno = ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_desc,
                                            (void **)&p_thread);
        ABTI_CHECK_ERROR(abt_errno);
        p_thread->type = ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC;
    } else
#endif
    {
        int abt_errno =
            ABTU_malloc(ABTI_MEM_POOL_DESC_ELEM_SIZE, (void **)&p_thread);
        ABTI_CHECK_ERROR(abt_errno);
        p_thread->type = ABTI_THREAD_TYPE_MEM_MALLOC_DESC;
    }
    *pp_thread = p_thread;
    return ABT_SUCCESS;
}

static inline void ABTI_mem_free_nythread_mempool_impl(ABTI_global *p_global,
                                                       ABTI_local *p_local,
                                                       ABTI_thread *p_thread)
{
    /* Return a descriptor. */
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
#ifdef ABT_CONFIG_DISABLE_EXT_THREAD
    /* Came from a memory pool. */
    ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_thread);
#else
    if (p_local_xstream) {
        /* Came from a memory pool. */
        ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_thread);
    } else {
        /* Return a stack to the global pool. */
        ABTD_spinlock_acquire(&p_global->mem_pool_desc_lock);
        ABTI_mem_pool_free(&p_global->mem_pool_desc_ext, p_thread);
        ABTD_spinlock_release(&p_global->mem_pool_desc_lock);
    }
#endif
#else /* !ABT_CONFIG_USE_MEM_POOL */
    /* If a memory pool is disabled, this function should not be called. */
    ABTI_ASSERT(0);
#endif
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_desc_impl(ABTI_local *p_local, ABT_bool use_lazy_stack,
                                 ABTI_ythread **pp_ythread)
{
    ABTI_STATIC_ASSERT(sizeof(ABTI_ythread) <= ABTI_MEM_POOL_DESC_ELEM_SIZE);
    ABTI_ythread *p_ythread;
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
    if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream) {
        /* It's not called on an external thread.  Use a memory pool. */
        int abt_errno = ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_desc,
                                            (void **)&p_ythread);
        ABTI_CHECK_ERROR(abt_errno);
        p_ythread->thread.type =
            use_lazy_stack
                ? ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_MEMPOOL_LAZY_STACK
                : ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC;
    } else
#endif
    {
        int abt_errno =
            ABTU_malloc(ABTI_MEM_POOL_DESC_ELEM_SIZE, (void **)&p_ythread);
        ABTI_CHECK_ERROR(abt_errno);
        p_ythread->thread.type =
            use_lazy_stack ? ABTI_THREAD_TYPE_MEM_MALLOC_DESC_MEMPOOL_LAZY_STACK
                           : ABTI_THREAD_TYPE_MEM_MALLOC_DESC;
    }
    *pp_ythread = p_ythread;
    return ABT_SUCCESS;
}

static inline void ABTI_mem_free_ythread_desc_mempool_impl(
    ABTI_global *p_global, ABTI_local *p_local, ABTI_ythread *p_ythread)
{
    /* Return a descriptor. */
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
#ifdef ABT_CONFIG_DISABLE_EXT_THREAD
    /* Came from a memory pool. */
    ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_ythread);
#else
    if (p_local_xstream) {
        /* Came from a memory pool. */
        ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_ythread);
    } else {
        /* Return a stack to the global pool. */
        ABTD_spinlock_acquire(&p_global->mem_pool_desc_lock);
        ABTI_mem_pool_free(&p_global->mem_pool_desc_ext, p_ythread);
        ABTD_spinlock_release(&p_global->mem_pool_desc_lock);
    }
#endif
#else /* !ABT_CONFIG_USE_MEM_POOL */
    /* If a memory pool is disabled, this function should not be called. */
    ABTI_ASSERT(0);
#endif
}

#ifdef ABT_CONFIG_USE_MEM_POOL
ABTU_ret_err static inline int ABTI_mem_alloc_ythread_mempool_desc_stack_impl(
    ABTI_mem_pool_local_pool *p_mem_pool_stack, size_t stacksize,
    ABTI_ythread **pp_ythread, void **pp_stacktop)
{
    /* stacksize must be a multiple of ABT_CONFIG_STATIC_CACHELINE_SIZE. */
    ABTI_ASSERT((stacksize & (ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)) == 0);
    void *p_ythread;
    int abt_errno = ABTI_mem_pool_alloc(p_mem_pool_stack, &p_ythread);
    ABTI_CHECK_ERROR(abt_errno);

    *pp_stacktop = (void *)p_ythread;
    *pp_ythread = (ABTI_ythread *)p_ythread;
    return ABT_SUCCESS;
}
#endif

ABTU_ret_err static inline int ABTI_mem_alloc_ythread_malloc_desc_stack_impl(
    size_t stacksize, ABTI_ythread **pp_ythread, void **pp_stacktop)
{
    /* stacksize must be a multiple of ABT_CONFIG_STATIC_CACHELINE_SIZE. */
    size_t alloc_stacksize =
        ABTU_roundup_size(stacksize, ABT_CONFIG_STATIC_CACHELINE_SIZE);
    char *p_stack;
    int abt_errno =
        ABTU_malloc(alloc_stacksize + sizeof(ABTI_ythread), (void **)&p_stack);
    ABTI_CHECK_ERROR(abt_errno);

    *pp_stacktop = (void *)(p_stack + alloc_stacksize);
    *pp_ythread = (ABTI_ythread *)(p_stack + alloc_stacksize);
    return ABT_SUCCESS;
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_mempool_desc_stack(ABTI_global *p_global,
                                          ABTI_local *p_local, size_t stacksize,
                                          ABTI_ythread **pp_ythread)
{
    ABTI_UB_ASSERT(stacksize == p_global->thread_stacksize);
    ABTI_ythread *p_ythread;
#ifdef ABT_CONFIG_USE_MEM_POOL
#ifdef ABT_CONFIG_DISABLE_LAZY_STACK_ALLOC
    const ABT_bool use_lazy_stack = ABT_FALSE;
#else
    const ABT_bool use_lazy_stack = ABT_TRUE;
#endif
    if (use_lazy_stack) {
        /* Only allocate a descriptor here. */
        int abt_errno =
            ABTI_mem_alloc_ythread_desc_impl(p_local, ABT_TRUE, &p_ythread);
        ABTI_CHECK_ERROR(abt_errno);
        /* Initialize the context. */
        ABTD_ythread_context_init_lazy(&p_ythread->ctx, stacksize);
        *pp_ythread = p_ythread;
        return ABT_SUCCESS;
    } else {
        void *p_stacktop;
        /* Allocate a ULT stack and a descriptor together. */
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
        if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream) {
            int abt_errno = ABTI_mem_alloc_ythread_mempool_desc_stack_impl(
                &p_local_xstream->mem_pool_stack, stacksize, &p_ythread,
                &p_stacktop);
            ABTI_CHECK_ERROR(abt_errno);
            p_ythread->thread.type = ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_STACK;
            ABTI_mem_register_stack(p_global, p_stacktop, stacksize, ABT_FALSE);
        } else {
            /* If an external thread allocates a stack, we use ABTU_malloc. */
            int abt_errno =
                ABTI_mem_alloc_ythread_malloc_desc_stack_impl(stacksize,
                                                              &p_ythread,
                                                              &p_stacktop);
            ABTI_CHECK_ERROR(abt_errno);
            p_ythread->thread.type = ABTI_THREAD_TYPE_MEM_MALLOC_DESC_STACK;
            ABTI_mem_register_stack(p_global, p_stacktop, stacksize, ABT_TRUE);
        }
        /* Initialize the context. */
        ABTD_ythread_context_init(&p_ythread->ctx, p_stacktop, stacksize);
        *pp_ythread = p_ythread;
        return ABT_SUCCESS;
    }
#else
    void *p_stacktop;
    int abt_errno =
        ABTI_mem_alloc_ythread_malloc_desc_stack_impl(stacksize, &p_ythread,
                                                      &p_stacktop);
    ABTI_CHECK_ERROR(abt_errno);
    p_ythread->thread.type = ABTI_THREAD_TYPE_MEM_MALLOC_DESC_STACK;
    ABTI_mem_register_stack(p_global, p_stacktop, stacksize, ABT_TRUE);
    /* Initialize the context. */
    ABTD_ythread_context_init(&p_ythread->ctx, p_stacktop, stacksize);
    *pp_ythread = p_ythread;
    return ABT_SUCCESS;
#endif
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_default(ABTI_global *p_global, ABTI_local *p_local,
                               ABTI_ythread **pp_ythread)
{
    size_t stacksize = p_global->thread_stacksize;
    return ABTI_mem_alloc_ythread_mempool_desc_stack(p_global, p_local,
                                                     stacksize, pp_ythread);
}

ABTU_ret_err static inline int ABTI_mem_alloc_ythread_malloc_desc_stack(
    ABTI_global *p_global, size_t stacksize, ABTI_ythread **pp_ythread)
{
    ABTI_ythread *p_ythread;
    void *p_stacktop;
    int abt_errno =
        ABTI_mem_alloc_ythread_malloc_desc_stack_impl(stacksize, &p_ythread,
                                                      &p_stacktop);
    ABTI_CHECK_ERROR(abt_errno);

    /* Initialize the context. */
    p_ythread->thread.type = ABTI_THREAD_TYPE_MEM_MALLOC_DESC_STACK;
    ABTD_ythread_context_init(&p_ythread->ctx, p_stacktop, stacksize);
    ABTI_mem_register_stack(p_global, p_stacktop, stacksize, ABT_TRUE);
    *pp_ythread = p_ythread;
    return ABT_SUCCESS;
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_mempool_desc(ABTI_global *p_global, ABTI_local *p_local,
                                    size_t stacksize, void *p_stacktop,
                                    ABTI_ythread **pp_ythread)
{
    ABTI_ythread *p_ythread;

    /* Use a descriptor pool for ABT_ythread. */
    ABTI_STATIC_ASSERT(sizeof(ABTI_ythread) <= ABTI_MEM_POOL_DESC_ELEM_SIZE);
    ABTI_STATIC_ASSERT(offsetof(ABTI_ythread, thread) == 0);
    int abt_errno =
        ABTI_mem_alloc_nythread(p_local, (ABTI_thread **)&p_ythread);
    ABTI_CHECK_ERROR(abt_errno);
    /* Initialize the context. */
    ABTD_ythread_context_init(&p_ythread->ctx, p_stacktop, stacksize);
    ABTI_mem_register_stack(p_global, p_stacktop, stacksize, ABT_TRUE);
    *pp_ythread = p_ythread;
    return ABT_SUCCESS;
}

static inline void ABTI_mem_free_thread(ABTI_global *p_global,
                                        ABTI_local *p_local,
                                        ABTI_thread *p_thread)
{
    /* Return stack. */
#ifdef ABT_CONFIG_USE_MEM_POOL
    if (p_thread->type & ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_STACK) {
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        ABTI_mem_unregister_stack(p_global,
                                  ABTD_ythread_context_get_stacktop(
                                      &p_ythread->ctx),
                                  ABTD_ythread_context_get_stacksize(
                                      &p_ythread->ctx),
                                  ABT_FALSE);

        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
        /* Came from a memory pool. */
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
        if (p_local_xstream == NULL) {
            /* Return a stack to the global pool. */
            ABTD_spinlock_acquire(&p_global->mem_pool_stack_lock);
            ABTI_mem_pool_free(&p_global->mem_pool_stack_ext, p_ythread);
            ABTD_spinlock_release(&p_global->mem_pool_stack_lock);
            return;
        }
#endif
        ABTI_mem_pool_free(&p_local_xstream->mem_pool_stack, p_ythread);
    } else
#endif
        if (p_thread->type &
            ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_MEMPOOL_LAZY_STACK) {
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        /* If it is a lazy stack ULT, it should not have a stack. */
        ABTI_UB_ASSERT(!ABTD_ythread_context_has_stack(&p_ythread->ctx));
        ABTI_mem_free_ythread_desc_mempool_impl(p_global, p_local, p_ythread);
    } else if (p_thread->type & ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC) {
        /* Non-yieldable thread or yieldable thread without stack.  */
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
        if (p_ythread) {
            ABTI_mem_unregister_stack(p_global,
                                      ABTD_ythread_context_get_stacktop(
                                          &p_ythread->ctx),
                                      ABTD_ythread_context_get_stacksize(
                                          &p_ythread->ctx),
                                      ABT_TRUE);
            ABTI_mem_free_ythread_desc_mempool_impl(p_global, p_local,
                                                    p_ythread);
        } else {
            ABTI_mem_free_nythread_mempool_impl(p_global, p_local, p_thread);
        }
    } else if (p_thread->type & ABTI_THREAD_TYPE_MEM_MALLOC_DESC_STACK) {
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        void *p_stacktop = ABTD_ythread_context_get_stacktop(&p_ythread->ctx);
        size_t stacksize = ABTD_ythread_context_get_stacksize(&p_ythread->ctx);
        ABTI_mem_unregister_stack(p_global, p_stacktop, stacksize, ABT_TRUE);
        void *p_stack = (void *)(((char *)p_stacktop) - stacksize);
        ABTU_free(p_stack);
    } else if (p_thread->type &
               ABTI_THREAD_TYPE_MEM_MALLOC_DESC_MEMPOOL_LAZY_STACK) {
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        /* If it is a lazy stack ULT, it should not have a stack. */
        ABTI_UB_ASSERT(!ABTD_ythread_context_has_stack(&p_ythread->ctx));
        ABTU_free(p_ythread);
    } else {
        ABTI_ASSERT(p_thread->type & ABTI_THREAD_TYPE_MEM_MALLOC_DESC);
        ABTI_STATIC_ASSERT(offsetof(ABTI_ythread, thread) == 0);
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
        if (p_ythread)
            ABTI_mem_unregister_stack(p_global,
                                      ABTD_ythread_context_get_stacktop(
                                          &p_ythread->ctx),
                                      ABTD_ythread_context_get_stacksize(
                                          &p_ythread->ctx),
                                      ABT_TRUE);
        ABTU_free(p_thread);
    }
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_mempool_stack(ABTI_xstream *p_local_xstream,
                                     ABTI_ythread *p_ythread)
{
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_UB_ASSERT(p_ythread->thread.type &
                   (ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_MEMPOOL_LAZY_STACK |
                    ABTI_THREAD_TYPE_MEM_MALLOC_DESC_MEMPOOL_LAZY_STACK));
    void *p_stacktop;
    int abt_errno =
        ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_stack, &p_stacktop);
    ABTI_CHECK_ERROR(abt_errno);
    ABTD_ythread_context_lazy_set_stack(&p_ythread->ctx, p_stacktop);
    return ABT_SUCCESS;
#else
    /* This function should not be called. */
    ABTI_ASSERT(0);
    return 0;
#endif
}

static inline void
ABTI_mem_free_ythread_mempool_stack(ABTI_xstream *p_local_xstream,
                                    ABTI_ythread *p_ythread)
{
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_UB_ASSERT(p_ythread->thread.type &
                   (ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_MEMPOOL_LAZY_STACK |
                    ABTI_THREAD_TYPE_MEM_MALLOC_DESC_MEMPOOL_LAZY_STACK));
    void *p_stacktop = ABTD_ythread_context_get_stacktop(&p_ythread->ctx);
    ABTD_ythread_context_lazy_unset_stack(&p_ythread->ctx);
    ABTI_mem_pool_free(&p_local_xstream->mem_pool_stack, p_stacktop);
#else
    /* This function should not be called. */
    ABTI_ASSERT(0);
#endif
}

/* Generic scalable memory pools.  It uses a memory pool for ABTI_thread.
 * The last four bytes will be used to determine whether the descriptor is
 * allocated externally (i.e., malloc()) or taken from a memory pool. */
#define ABTI_MEM_POOL_DESC_SIZE (ABTI_MEM_POOL_DESC_ELEM_SIZE - 4)

ABTU_ret_err static inline int ABTI_mem_alloc_desc(ABTI_local *p_local,
                                                   void **pp_desc)
{
#ifndef ABT_CONFIG_USE_MEM_POOL
    return ABTU_malloc(ABTI_MEM_POOL_DESC_SIZE, pp_desc);
#else
    void *p_desc;
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
    if (ABTI_IS_EXT_THREAD_ENABLED && p_local_xstream == NULL) {
        /* For external threads */
        int abt_errno = ABTU_malloc(ABTI_MEM_POOL_DESC_SIZE, &p_desc);
        ABTI_CHECK_ERROR(abt_errno);
        *(uint32_t *)(((char *)p_desc) + ABTI_MEM_POOL_DESC_SIZE) = 1;
        *pp_desc = p_desc;
        return ABT_SUCCESS;
    } else {
        /* Find the page that has an empty block */
        int abt_errno =
            ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_desc, &p_desc);
        ABTI_CHECK_ERROR(abt_errno);
        /* To distinguish it from a malloc'ed case, assign non-NULL value. */
        *(uint32_t *)(((char *)p_desc) + ABTI_MEM_POOL_DESC_SIZE) = 0;
        *pp_desc = p_desc;
        return ABT_SUCCESS;
    }
#endif
}

static inline void ABTI_mem_free_desc(ABTI_global *p_global,
                                      ABTI_local *p_local, void *p_desc)
{
#ifndef ABT_CONFIG_USE_MEM_POOL
    ABTU_free(p_desc);
#else
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (*(uint32_t *)(((char *)p_desc) + ABTI_MEM_POOL_DESC_SIZE)) {
        /* This was allocated by an external thread. */
        ABTU_free(p_desc);
        return;
    } else if (!p_local_xstream) {
        /* Return a stack and a descriptor to their global pools. */
        ABTD_spinlock_acquire(&p_global->mem_pool_desc_lock);
        ABTI_mem_pool_free(&p_global->mem_pool_desc_ext, p_desc);
        ABTD_spinlock_release(&p_global->mem_pool_desc_lock);
        return;
    }
#endif
    ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_desc);
#endif
}

#endif /* ABTI_MEM_H_INCLUDED */
