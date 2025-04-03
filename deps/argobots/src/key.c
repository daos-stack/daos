/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup KEY Work-Unit-Specific Data
 * This group is for work-unit-specific data, which can be described as
 * work-unit local storage (which is similar to "thread-local storage" or TLS).
 */

static ABTD_atomic_uint32 g_key_id =
    ABTD_ATOMIC_UINT32_STATIC_INITIALIZER(ABTI_KEY_ID_END_);

/**
 * @ingroup KEY
 * @brief   Create a new work-unit-specific data key.
 *
 * \c ABT_key_create() creates a new work-unit-specific data key visible and
 * returns its handle through \c newkey.  Although the same key may be used by
 * different work units, the values bound to the key set by \c ABT_key_set() are
 * maintained on a per-work-unit and persist for the life of each work unit.
 *
 * Upon key creation, the value \c NULL will be associated with \c newkey in all
 * existing work units.  Upon work-unit creation, the value \c NULL will be
 * associated with all the defined keys in the new work unit.
 *
 * An optional destructor function \c destructor() may be registered to
 * \c newkey.  When a work unit is freed, if a key has a non-\c NULL destructor
 * and the work unit has a non-\c NULL value associated with that key, the value
 * of the key is set to \c NULL, and then \c destructor() is called with the
 * last associated value as its sole argument \c value.  The destructor is
 * called before the work unit is freed.  The order of destructor calls is
 * undefined if more than one destructor exists for a work unit when it is
 * freed.
 *
 * @note
 * \c destructor() is called when a work unit is \b freed (e.g.,
 * \c ABT_thread_free()), not \b joined (e.g., \c ABT_thread_join()).
 *
 * Unlike other implementations (e.g., \c pthread_key_create()), \c destructor()
 * is not called by the associated work-unit, so a program that relies on a
 * caller of \c destructor() is non-conforming.
 *
 * \c destructor() is called even if the associated key has already been freed
 * by \c ABT_key_free().
 *
 * The created key must be freed by \c ABT_key_free() after its use.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newkey}
 *
 * @param[in]  destructor  destructor function called when a work unit is freed
 * @param[out] newkey      work-unit-specific data key handle
 * @return Error code
 */
int ABT_key_create(void (*destructor)(void *value), ABT_key *newkey)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newkey);

    ABTI_key *p_newkey;
    int abt_errno = ABTU_malloc(sizeof(ABTI_key), (void **)&p_newkey);
    ABTI_CHECK_ERROR(abt_errno);

    p_newkey->f_destructor = destructor;
    p_newkey->id = ABTD_atomic_fetch_add_uint32(&g_key_id, 1);
    /* Return value */
    *newkey = ABTI_key_get_handle(p_newkey);
    return ABT_SUCCESS;
}

/**
 * @ingroup KEY
 * @brief   Free a work-unit-specific data key.
 *
 * \c ABT_key_free() deallocates the resource used for the work-unit-specific
 * data key \c key and sets \c key to \c ABT_KEY_NULL.
 *
 * It is the user's responsibility to free memory for values associated with the
 * deleted key.
 *
 * The user is allowed to delete a key before terminating all work units that
 * have non-\c NULL values associated with \c key.  The user cannot refer to a
 * value via the deleted key, but the destructor of the deleted key is called
 * when a work unit is freed.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_KEY_PTR{\c key}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c key}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c key}
 *
 * @param[in,out] key  work-unit-specific data key handle
 * @return Error code
 */
int ABT_key_free(ABT_key *key)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(key);

    ABT_key h_key = *key;
    ABTI_key *p_key = ABTI_key_get_ptr(h_key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);
    ABTU_free(p_key);

    /* Return value */
    *key = ABT_KEY_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup KEY
 * @brief   Associate a value with a work-unit-specific data key in the calling
 *          work unit.
 *
 * \c ABT_key_set() associates a value \c value with the work-unit-specific data
 * key \c key in the calling work unit.  Different work units may bind different
 * values to the same key.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_set_specific()}
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[in] key    work-unit-specific data key handle
 * @param[in] value  value associated with \c key
 * @return Error code
 */
int ABT_key_set(ABT_key key, void *value)
{
#ifdef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_UB_ASSERT(ABTI_initialized());
#endif

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Obtain the key-value table pointer. */
    int abt_errno =
        ABTI_ktable_set(p_global, ABTI_xstream_get_local(p_local_xstream),
                        &p_local_xstream->p_thread->p_keytable, p_key, value);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup KEY
 * @brief   Get a value associated with a work-unit-specific data key in the
 *          calling work unit.
 *
 * \c ABT_key_get() returns the value in the caller associated with the
 * work-unit-specific data key \c key in the calling work unit through \c value.
 * If the caller has never set a value for \c key, this routine sets \c value
 * to \c NULL.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_specific()}
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c value}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[in]  key    work-unit-specific data key handle
 * @param[out] value  value associated with \c key
 * @return Error code
 */
int ABT_key_get(ABT_key key, void **value)
{
    ABTI_UB_ASSERT(value);
#ifdef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_UB_ASSERT(ABTI_initialized());
#endif

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    /* We don't allow an external thread to call this routine. */
    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Obtain the key-value table pointer */
    *value = ABTI_ktable_get(&p_local_xstream->p_thread->p_keytable, p_key);
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

void ABTI_ktable_free(ABTI_global *p_global, ABTI_local *p_local,
                      ABTI_ktable *p_ktable)
{
    ABTI_ktelem *p_elem;
    int i;

    for (i = 0; i < p_ktable->size; i++) {
        p_elem =
            (ABTI_ktelem *)ABTD_atomic_relaxed_load_ptr(&p_ktable->p_elems[i]);
        while (p_elem) {
            /* Call the destructor if it exists and the value is not null. */
            if (p_elem->f_destructor && p_elem->value) {
                p_elem->f_destructor(p_elem->value);
            }
            p_elem =
                (ABTI_ktelem *)ABTD_atomic_relaxed_load_ptr(&p_elem->p_next);
        }
    }
    ABTI_ktable_mem_header *p_header =
        (ABTI_ktable_mem_header *)p_ktable->p_used_mem;
    while (p_header) {
        ABTI_ktable_mem_header *p_next = p_header->p_next;
        if (ABTU_likely(p_header->is_from_mempool)) {
            ABTI_mem_free_desc(p_global, p_local, (void *)p_header);
        } else {
            ABTU_free(p_header);
        }
        p_header = p_next;
    }
}
