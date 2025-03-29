/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_UNIT_H_INCLUDED
#define ABTI_UNIT_H_INCLUDED

/* A hash table is heavy.  It should be avoided as much as possible. */
#define ABTI_UNIT_BUILTIN_POOL_BIT ((uintptr_t)0x1)

static inline ABT_bool ABTI_unit_is_builtin(ABT_unit unit)
{
    if (((uintptr_t)unit) & ABTI_UNIT_BUILTIN_POOL_BIT) {
        /* This must happen only when unit is associated with a built-in pool.
         * See ABT_pool_def's u_create_from_thread() for details. */
        return ABT_TRUE;
    } else {
        return ABT_FALSE;
    }
}

static inline ABT_unit ABTI_unit_get_builtin_unit(ABTI_thread *p_thread)
{
    ABTI_ASSERT(!(((uintptr_t)p_thread) & ABTI_UNIT_BUILTIN_POOL_BIT));
    return (ABT_unit)(((uintptr_t)p_thread) | ABTI_UNIT_BUILTIN_POOL_BIT);
}

static inline void ABTI_unit_init_builtin(ABTI_thread *p_thread)
{
    p_thread->p_prev = NULL;
    p_thread->p_next = NULL;
    ABTD_atomic_relaxed_store_int(&p_thread->is_in_pool, 0);
    p_thread->unit = ABTI_unit_get_builtin_unit(p_thread);
}

static inline ABTI_thread *ABTI_unit_get_thread_from_builtin_unit(ABT_unit unit)
{
    ABTI_ASSERT(ABTI_unit_is_builtin(unit));
    return (ABTI_thread *)(((uintptr_t)unit) & (~ABTI_UNIT_BUILTIN_POOL_BIT));
}

static inline ABTI_thread *ABTI_unit_get_thread(ABTI_global *p_global,
                                                ABT_unit unit)
{
    if (ABTU_likely(ABTI_unit_is_builtin(unit))) {
        /* This unit is associated with a built-in pool. */
        return ABTI_unit_get_thread_from_builtin_unit(unit);
    } else {
        return ABTI_unit_get_thread_from_user_defined_unit(p_global, unit);
    }
}

ABTU_ret_err static inline int
ABTI_unit_set_associated_pool(ABTI_global *p_global, ABT_unit unit,
                              ABTI_pool *p_pool, ABTI_thread **pp_thread)
{
    if (ABTU_likely(ABTI_unit_is_builtin(unit))) {
        ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
        if (ABTU_likely(p_pool->is_builtin)) {
            /* Do nothing since built-in pools share the implementation of
             * ABT_unit. */
            p_thread->p_pool = p_pool;
            *pp_thread = p_thread;
            return ABT_SUCCESS;
        } else {
            /* The new pool is a user-defined pool. */
            ABT_pool pool = ABTI_pool_get_handle(p_pool);
            ABT_unit new_unit =
                p_pool->required_def.p_create_unit(pool, ABTI_thread_get_handle(
                                                             p_thread));
            if (new_unit == ABT_UNIT_NULL)
                return ABT_ERR_OTHER;
            int ret = ABTI_unit_map_thread(p_global, new_unit, p_thread);
            if (ret != ABT_SUCCESS) {
                p_pool->required_def.p_free_unit(pool, new_unit);
                return ret;
            }
            p_thread->unit = new_unit;
            p_thread->p_pool = p_pool;
            *pp_thread = p_thread;
            return ABT_SUCCESS;
        }
    } else {
        /* Currently, unit is associated with a user-defined pool. */
        ABTI_thread *p_thread =
            ABTI_unit_get_thread_from_user_defined_unit(p_global, unit);
        if (p_pool->is_builtin) {
            /* The old unit is associated with a custom pool.  Remove the
             * existing mapping. */
            ABTI_unit_unmap_thread(p_global, unit);
            ABT_pool old_pool = ABTI_pool_get_handle(p_thread->p_pool);
            p_thread->p_pool->required_def.p_free_unit(old_pool, unit);
            ABTI_unit_init_builtin(p_thread);
            p_thread->p_pool = p_pool;
            *pp_thread = p_thread;
            return ABT_SUCCESS;
        } else if (p_thread->p_pool == p_pool) {
            /* Both are associated with the same custom pool. */
            *pp_thread = p_thread;
            return ABT_SUCCESS;
        } else {
            /* Both are associated with different custom pools. */
            ABT_pool pool = ABTI_pool_get_handle(p_pool);
            ABT_unit new_unit =
                p_pool->required_def.p_create_unit(pool, ABTI_thread_get_handle(
                                                             p_thread));
            if (new_unit == ABT_UNIT_NULL)
                return ABT_ERR_OTHER;
            int ret = ABTI_unit_map_thread(p_global, new_unit, p_thread);
            if (ret != ABT_SUCCESS) {
                p_pool->required_def.p_free_unit(pool, new_unit);
                return ret;
            }
            ABTI_unit_unmap_thread(p_global, unit);
            ABT_pool old_pool = ABTI_pool_get_handle(p_thread->p_pool);
            p_thread->p_pool->required_def.p_free_unit(old_pool, unit);
            p_thread->unit = new_unit;
            p_thread->p_pool = p_pool;
            *pp_thread = p_thread;
            return ABT_SUCCESS;
        }
    }
}

/* The following functions have ABTI_thread prefix, but they mainly manage
 * thread-unit mapping, so they are placed in this header file so far. */
ABTU_ret_err static inline int ABTI_thread_init_pool(ABTI_global *p_global,
                                                     ABTI_thread *p_thread,
                                                     ABTI_pool *p_pool)
{
    if (ABTU_likely(p_pool->is_builtin)) {
        ABTI_unit_init_builtin(p_thread);
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    } else {
        ABT_pool pool = ABTI_pool_get_handle(p_pool);
        ABT_unit new_unit =
            p_pool->required_def.p_create_unit(pool, ABTI_thread_get_handle(
                                                         p_thread));
        if (new_unit == ABT_UNIT_NULL)
            return ABT_ERR_OTHER;
        int ret = ABTI_unit_map_thread(p_global, new_unit, p_thread);
        if (ret != ABT_SUCCESS) {
            p_pool->required_def.p_free_unit(pool, new_unit);
            return ret;
        }
        p_thread->unit = new_unit;
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    }
}

ABTU_ret_err static inline int
ABTI_thread_set_associated_pool(ABTI_global *p_global, ABTI_thread *p_thread,
                                ABTI_pool *p_pool)
{
    ABT_unit unit = p_thread->unit;
    if (ABTU_likely(ABTI_unit_is_builtin(unit) && p_pool->is_builtin)) {
        /* Do nothing since built-in pools share the implementation of
         * ABT_unit. */
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    } else if (ABTI_unit_is_builtin(unit)) {
        /* The new unit is associated with a custom pool.  Add a new mapping. */
        ABT_pool pool = ABTI_pool_get_handle(p_pool);
        ABT_unit new_unit =
            p_pool->required_def.p_create_unit(pool, ABTI_thread_get_handle(
                                                         p_thread));
        if (new_unit == ABT_UNIT_NULL)
            return ABT_ERR_OTHER;
        int ret = ABTI_unit_map_thread(p_global, new_unit, p_thread);
        if (ret != ABT_SUCCESS) {
            p_pool->required_def.p_free_unit(pool, new_unit);
            return ret;
        }
        p_thread->unit = new_unit;
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    } else if (p_pool->is_builtin) {
        /* The old unit is associated with a custom pool.  Remove the existing
         * mapping. */
        ABTI_unit_unmap_thread(p_global, unit);
        ABT_pool old_pool = ABTI_pool_get_handle(p_thread->p_pool);
        p_thread->p_pool->required_def.p_free_unit(old_pool, unit);
        ABTI_unit_init_builtin(p_thread);
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    } else if (p_thread->p_pool == p_pool) {
        /* Both are associated with the same custom pool. */
        return ABT_SUCCESS;
    } else {
        /* Both are associated with different custom pools. */
        ABT_pool pool = ABTI_pool_get_handle(p_pool);
        ABT_unit new_unit =
            p_pool->required_def.p_create_unit(pool, ABTI_thread_get_handle(
                                                         p_thread));
        if (new_unit == ABT_UNIT_NULL)
            return ABT_ERR_OTHER;
        int ret = ABTI_unit_map_thread(p_global, new_unit, p_thread);
        if (ret != ABT_SUCCESS) {
            p_pool->required_def.p_free_unit(pool, new_unit);
            return ret;
        }
        ABTI_unit_unmap_thread(p_global, unit);
        ABT_pool old_pool = ABTI_pool_get_handle(p_thread->p_pool);
        p_thread->p_pool->required_def.p_free_unit(old_pool, unit);
        p_thread->unit = new_unit;
        p_thread->p_pool = p_pool;
        return ABT_SUCCESS;
    }
}

static inline void ABTI_thread_unset_associated_pool(ABTI_global *p_global,
                                                     ABTI_thread *p_thread)
{
    ABT_unit unit = p_thread->unit;
    if (ABTU_unlikely(!ABTI_unit_is_builtin(unit))) {
        ABTI_unit_unmap_thread(p_global, unit);
        ABT_pool old_pool = ABTI_pool_get_handle(p_thread->p_pool);
        p_thread->p_pool->required_def.p_free_unit(old_pool, unit);
    }
#if ABTI_IS_ERROR_CHECK_ENABLED
    p_thread->unit = ABT_UNIT_NULL;
    p_thread->p_pool = NULL;
#endif
}

#endif /* ABTI_UNIT_H_INCLUDED */
