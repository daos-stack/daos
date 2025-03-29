/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_POOL_H_INCLUDED
#define ABTI_POOL_H_INCLUDED

/* Inlined functions for Pool */

static inline ABTI_pool *ABTI_pool_get_ptr(ABT_pool pool)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_pool *p_pool;
    if (pool == ABT_POOL_NULL) {
        p_pool = NULL;
    } else {
        p_pool = (ABTI_pool *)pool;
    }
    return p_pool;
#else
    return (ABTI_pool *)pool;
#endif
}

static inline ABT_pool ABTI_pool_get_handle(ABTI_pool *p_pool)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_pool h_pool;
    if (p_pool == NULL) {
        h_pool = ABT_POOL_NULL;
    } else {
        h_pool = (ABT_pool)p_pool;
    }
    return h_pool;
#else
    return (ABT_pool)p_pool;
#endif
}

/* A ULT is blocked and is waiting for going back to this pool */
static inline void ABTI_pool_inc_num_blocked(ABTI_pool *p_pool)
{
    ABTD_atomic_fetch_add_int32(&p_pool->num_blocked, 1);
}

/* A blocked ULT is back in the pool */
static inline void ABTI_pool_dec_num_blocked(ABTI_pool *p_pool)
{
    ABTD_atomic_fetch_sub_int32(&p_pool->num_blocked, 1);
}

static inline void ABTI_pool_push(ABTI_pool *p_pool, ABT_unit unit,
                                  ABT_pool_context context)
{
    /* Push unit into pool */
    LOG_DEBUG_POOL_PUSH(p_pool, unit);
    p_pool->required_def.p_push(ABTI_pool_get_handle(p_pool), unit, context);
}

static inline void ABTI_pool_add_thread(ABTI_thread *p_thread,
                                        ABT_pool_context context)
{
    /* Set the ULT's state as READY. The relaxed version is used since the state
     * is synchronized by the following pool operation. */
    ABTD_atomic_relaxed_store_int(&p_thread->state, ABT_THREAD_STATE_READY);
    /* Add the ULT to the associated pool */
    ABTI_pool_push(p_thread->p_pool, p_thread->unit, context);
}

ABTU_ret_err static inline int ABTI_pool_remove(ABTI_pool *p_pool,
                                                ABT_unit unit)
{
    LOG_DEBUG_POOL_REMOVE(p_pool, unit);
    ABTI_UB_ASSERT(p_pool->deprecated_def.p_remove);
    return p_pool->deprecated_def.p_remove(ABTI_pool_get_handle(p_pool), unit);
}

static inline ABT_thread ABTI_pool_pop_wait(ABTI_pool *p_pool, double time_secs,
                                            ABT_pool_context context)
{
    ABTI_UB_ASSERT(p_pool->optional_def.p_pop_wait);
    ABT_thread thread =
        p_pool->optional_def.p_pop_wait(ABTI_pool_get_handle(p_pool), time_secs,
                                        context);
    LOG_DEBUG_POOL_POP(p_pool, thread);
    return thread;
}

/* Defined in pool.c */
ABT_thread ABTI_pool_pop_timedwait(ABTI_pool *p_pool, double abstime_secs);

static inline ABT_thread ABTI_pool_pop(ABTI_pool *p_pool,
                                       ABT_pool_context context)
{
    ABT_thread thread =
        p_pool->required_def.p_pop(ABTI_pool_get_handle(p_pool), context);
    LOG_DEBUG_POOL_POP(p_pool, thread);
    return thread;
}

static inline void ABTI_pool_pop_many(ABTI_pool *p_pool, ABT_thread *threads,
                                      size_t len, size_t *num,
                                      ABT_pool_context context)
{
    ABTI_UB_ASSERT(p_pool->optional_def.p_pop_many);
    p_pool->optional_def.p_pop_many(ABTI_pool_get_handle(p_pool), threads, len,
                                    num, context);
    LOG_DEBUG_POOL_POP_MANY(p_pool, threads, *num);
}

static inline void ABTI_pool_push_many(ABTI_pool *p_pool, const ABT_unit *units,
                                       size_t num, ABT_pool_context context)
{
    ABTI_UB_ASSERT(p_pool->optional_def.p_push_many);
    p_pool->optional_def.p_push_many(ABTI_pool_get_handle(p_pool), units, num,
                                     context);
    LOG_DEBUG_POOL_PUSH_MANY(p_pool, units, num);
}

/* Increase num_scheds to mark the pool as having another scheduler. If the
 * pool is not available, it returns ABT_ERR_INV_POOL_ACCESS.  */
static inline void ABTI_pool_retain(ABTI_pool *p_pool)
{
    ABTD_atomic_fetch_add_int32(&p_pool->num_scheds, 1);
}

/* Decrease the num_scheds to release this pool from a scheduler. Call when
 * the pool is removed from a scheduler or when it stops. */
static inline int32_t ABTI_pool_release(ABTI_pool *p_pool)
{
    ABTI_ASSERT(ABTD_atomic_acquire_load_int32(&p_pool->num_scheds) > 0);
    return ABTD_atomic_fetch_sub_int32(&p_pool->num_scheds, 1) - 1;
}

static inline ABT_bool ABTI_pool_is_empty(ABTI_pool *p_pool)
{
    return p_pool->required_def.p_is_empty(ABTI_pool_get_handle(p_pool));
}

static inline size_t ABTI_pool_get_size(ABTI_pool *p_pool)
{
    ABTI_UB_ASSERT(p_pool->optional_def.p_get_size);
    return p_pool->optional_def.p_get_size(ABTI_pool_get_handle(p_pool));
}

static inline size_t ABTI_pool_get_total_size(ABTI_pool *p_pool)
{
    size_t total_size;
    total_size = ABTI_pool_get_size(p_pool);
    total_size += ABTD_atomic_acquire_load_int32(&p_pool->num_blocked);
    return total_size;
}

#endif /* ABTI_POOL_H_INCLUDED */
