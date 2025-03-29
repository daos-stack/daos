/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static void unit_init_hash_table(ABTI_global *p_global);
static void unit_finalize_hash_table(ABTI_global *p_global);
ABTU_ret_err static inline int
unit_map_thread(ABTI_global *p_global, ABT_unit unit, ABTI_thread *p_thread);
static inline void unit_unmap_thread(ABTI_global *p_global, ABT_unit unit);
static inline ABTI_thread *
unit_get_thread_from_user_defined_unit(ABTI_global *p_global, ABT_unit unit);

/** @defgroup UNIT  Work Unit
 * This group is for work units.
 */

/**
 * @ingroup UNIT
 * @brief   No operation.
 *
 * \c ABT_unit_set_associated_pool() does nothing.  This routine was originally
 * provided for setting the associated pool for a work unit, but Argobots 1.1
 * manages this mapping when \c ABT_pool_push() or \c ABT_xstream_run_unit() is
 * called.  This routine is for the backward compatibility.
 *
 * @changev11
 * \DOC_DESC_V10_UNIT_POOL_MAPPING
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_UNIT_HANDLE{\c unit}
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] unit  work unit handle
 * @param[in] pool  pool handle
 * @return Error code
 */
int ABT_unit_set_associated_pool(ABT_unit unit, ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(unit != ABT_UNIT_NULL, ABT_ERR_INV_UNIT);
    return ABT_SUCCESS;
}

/**
 * @ingroup UNIT
 * @brief   Get a thread handle of the target work unit.
 *
 * \c ABT_unit_get_thread() returns the \c ABT_thread handle associated with the
 * work unit \c unit through \c thread.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_UNIT_HANDLE{\c unit}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 *
 * @param[in]  unit    work unit handle
 * @param[out] thread  work unit handle
 * @return Error code
 */
int ABT_unit_get_thread(ABT_unit unit, ABT_thread *thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread);

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_CHECK_TRUE(unit != ABT_UNIT_NULL, ABT_ERR_INV_UNIT);
    ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
    *thread = ABTI_thread_get_handle(p_thread);
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

void ABTI_unit_init_hash_table(ABTI_global *p_global)
{
    unit_init_hash_table(p_global);
}

void ABTI_unit_finalize_hash_table(ABTI_global *p_global)
{
    unit_finalize_hash_table(p_global);
}

ABTU_ret_err int ABTI_unit_map_thread(ABTI_global *p_global, ABT_unit unit,
                                      ABTI_thread *p_thread)
{
    return unit_map_thread(p_global, unit, p_thread);
}

void ABTI_unit_unmap_thread(ABTI_global *p_global, ABT_unit unit)
{
    unit_unmap_thread(p_global, unit);
}

ABTI_thread *ABTI_unit_get_thread_from_user_defined_unit(ABTI_global *p_global,
                                                         ABT_unit unit)
{
    return unit_get_thread_from_user_defined_unit(p_global, unit);
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static inline size_t unit_get_hash_index(ABT_unit unit)
{
    size_t val = (uintptr_t)unit;
    /* Let's ignore the first 3 bits and use the next 29 bits. */
    size_t base_val = val >> 3;
#if ABTI_UNIT_HASH_TABLE_SIZE_EXP <= 14
    base_val += val >> (ABTI_UNIT_HASH_TABLE_SIZE_EXP + 3);
#endif
#if ABTI_UNIT_HASH_TABLE_SIZE_EXP <= 9
    base_val += val >> (ABTI_UNIT_HASH_TABLE_SIZE_EXP * 2 + 3);
#endif
    return base_val & (ABTI_UNIT_HASH_TABLE_SIZE - 1);
}

typedef struct atomic_unit {
    ABTD_atomic_ptr val;
} atomic_unit;

static inline ABT_unit atomic_relaxed_load_unit(const atomic_unit *p_ptr)
{
    return (ABT_unit)ABTD_atomic_relaxed_load_ptr(&p_ptr->val);
}

static inline void atomic_relaxed_store_unit(atomic_unit *p_ptr, ABT_unit val)
{
    ABTD_atomic_relaxed_store_ptr(&p_ptr->val, (void *)val);
}

typedef struct unit_to_thread {
    /* This is updated in a relaxed manner.  Relaxed access is fine since the
     * semantics guarantees that all operations that "hit" are performed after
     * map() from the memory order viewpoint; we just need to guarantee that the
     * other parallel entities that call unmap() and get() (consequently, they
     * do not "hit") do not see a corrupted value that is neither a new ABT_unit
     * handle nor ABT_UNIT_NULL. */
    atomic_unit unit;
    ABTI_thread *p_thread;
    struct unit_to_thread *p_next;
} unit_to_thread;

static inline unit_to_thread *
atomic_acquire_load_unit_to_thread(const ABTI_atomic_unit_to_thread *p_ptr)
{
    return (unit_to_thread *)ABTD_atomic_acquire_load_ptr(&p_ptr->val);
}

static inline unit_to_thread *
atomic_relaxed_load_unit_to_thread(const ABTI_atomic_unit_to_thread *p_ptr)
{
    return (unit_to_thread *)ABTD_atomic_relaxed_load_ptr(&p_ptr->val);
}

static inline void
atomic_release_store_unit_to_thread(ABTI_atomic_unit_to_thread *p_ptr,
                                    unit_to_thread *val)
{
    ABTD_atomic_release_store_ptr(&p_ptr->val, (void *)val);
}

static inline void
atomic_relaxed_store_unit_to_thread(ABTI_atomic_unit_to_thread *p_ptr,
                                    unit_to_thread *val)
{
    ABTD_atomic_relaxed_store_ptr(&p_ptr->val, (void *)val);
}

static void unit_init_hash_table(ABTI_global *p_global)
{
    int i;
    for (i = 0; i < (int)ABTI_UNIT_HASH_TABLE_SIZE; i++) {
        atomic_relaxed_store_unit_to_thread(&p_global->unit_to_thread_entires[i]
                                                 .list,
                                            NULL);
        ABTD_spinlock_clear(&p_global->unit_to_thread_entires[i].lock);
    }
}

static void unit_finalize_hash_table(ABTI_global *p_global)
{
    int i;
    for (i = 0; i < (int)ABTI_UNIT_HASH_TABLE_SIZE; i++) {
        ABTI_ASSERT(!ABTD_spinlock_is_locked(
            &p_global->unit_to_thread_entires[i].lock));
        unit_to_thread *p_cur = atomic_relaxed_load_unit_to_thread(
            &p_global->unit_to_thread_entires[i].list);
        while (p_cur) {
            ABTI_ASSERT(atomic_relaxed_load_unit(&p_cur->unit) ==
                        ABT_UNIT_NULL);
            unit_to_thread *p_next = p_cur->p_next;
            ABTU_free(p_cur);
            p_cur = p_next;
        }
    }
}

ABTU_ret_err static inline int
unit_map_thread(ABTI_global *p_global, ABT_unit unit, ABTI_thread *p_thread)
{
    ABTI_ASSERT(!ABTI_unit_is_builtin(unit));
    size_t hash_index = unit_get_hash_index(unit);
    ABTI_unit_to_thread_entry *p_entry =
        &p_global->unit_to_thread_entires[hash_index];

    ABTD_spinlock_acquire(&p_entry->lock);
    unit_to_thread *p_cur = atomic_relaxed_load_unit_to_thread(&p_entry->list);
    while (p_cur) {
        if (atomic_relaxed_load_unit(&p_cur->unit) == ABT_UNIT_NULL) {
            /* Empty element has been found.  Let's use this. */
            atomic_relaxed_store_unit(&p_cur->unit, unit);
            /* p_cur is associated with this unit. */
            p_cur->p_thread = p_thread;
            ABTD_spinlock_release(&p_entry->lock);
            return ABT_SUCCESS;
        }
        p_cur = p_cur->p_next;
    }
    /* It seems that all the elements are in use.  Let's allocate a new one. */
    unit_to_thread *p_new;
    p_cur = atomic_relaxed_load_unit_to_thread(&p_entry->list);
    /* Let's dynamically allocate memory. */
    int ret = ABTU_malloc(sizeof(unit_to_thread), (void **)&p_new);
    if (ret != ABT_SUCCESS) {
        ABTD_spinlock_release(&p_entry->lock);
        return ret;
    }
    /* Initialize the new unit. */
    atomic_relaxed_store_unit(&p_new->unit, unit);
    p_new->p_thread = p_thread;
    p_new->p_next = p_cur;
    atomic_release_store_unit_to_thread(&p_entry->list, p_new);
    ABTD_spinlock_release(&p_entry->lock);
    return ABT_SUCCESS;
}

static inline void unit_unmap_thread(ABTI_global *p_global, ABT_unit unit)
{
    ABTI_ASSERT(!ABTI_unit_is_builtin(unit));
    size_t hash_index = unit_get_hash_index(unit);
    ABTI_unit_to_thread_entry *p_entry =
        &p_global->unit_to_thread_entires[hash_index];

    ABTD_spinlock_acquire(&p_entry->lock);
    unit_to_thread *p_cur = atomic_relaxed_load_unit_to_thread(&p_entry->list);
    /* Update the corresponding unit to "NULL". */
    while (1) {
        if (atomic_relaxed_load_unit(&p_cur->unit) == unit) {
            atomic_relaxed_store_unit(&p_cur->unit, ABT_UNIT_NULL);
            break;
        }
        p_cur = p_cur->p_next;
        ABTI_ASSERT(p_cur); /* unmap() must succeed. */
    }
    ABTD_spinlock_release(&p_entry->lock);
}

static inline ABTI_thread *
unit_get_thread_from_user_defined_unit(ABTI_global *p_global, ABT_unit unit)
{
    ABTI_ASSERT(!ABTI_unit_is_builtin(unit));
    /* Find an element. */
    size_t hash_index = unit_get_hash_index(unit);
    ABTI_unit_to_thread_entry *p_entry =
        &p_global->unit_to_thread_entires[hash_index];
    /* The first element must be accessed in a release-acquire manner.  The new
     * element is release-stored to the head, so acquire-load can always get a
     * valid linked-list chain. */
    unit_to_thread *p_cur = atomic_acquire_load_unit_to_thread(&p_entry->list);
    while (1) {
        ABTI_ASSERT(p_cur); /* get() must succeed. */
        if (atomic_relaxed_load_unit(&p_cur->unit) == unit) {
            return p_cur->p_thread;
        }
        p_cur = p_cur->p_next;
    }
}
