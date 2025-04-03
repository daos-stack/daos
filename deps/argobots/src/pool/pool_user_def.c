/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/** @defgroup POOL_USER_DEF  Pool definition
 * This group is for Pool definition.
 */

/**
 * @ingroup POOL_USER_DEF
 * @brief   Create a new pool definition.
 *
 * \c ABT_pool_user_def_create() creates a new pool definition and returns its
 * handle through \c newdef.  \c p_create_unit, \c p_free_unit, \c p_is_empty,
 * \c p_pop, \c p_push are registered to \c newdef.
 *
 * \c newdef must be freed by \c ABT_pool_user_def_free() after its use.
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
 * \DOC_UNDEFINED_NULL_PTR{\c newdef}
 * \DOC_UNDEFINED_NULL_PTR{\c p_create_unit}
 * \DOC_UNDEFINED_NULL_PTR{\c p_free_unit}
 * \DOC_UNDEFINED_NULL_PTR{\c p_is_empty}
 * \DOC_UNDEFINED_NULL_PTR{\c p_pop}
 * \DOC_UNDEFINED_NULL_PTR{\c p_push}
 *
 * @param[in]  p_create_unit  unit creation function
 * @param[in]  p_free_unit    unit release function
 * @param[in]  p_is_empty     emptiness check function
 * @param[in]  p_pop          pop function
 * @param[in]  p_push         push function
 * @param[out] newdef         pool definition handle
 * @return Error code
 */
int ABT_pool_user_def_create(ABT_pool_user_create_unit_fn p_create_unit,
                             ABT_pool_user_free_unit_fn p_free_unit,
                             ABT_pool_user_is_empty_fn p_is_empty,
                             ABT_pool_user_pop_fn p_pop,
                             ABT_pool_user_push_fn p_push,
                             ABT_pool_user_def *newdef)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newdef);
    ABTI_UB_ASSERT(p_create_unit);
    ABTI_UB_ASSERT(p_free_unit);
    ABTI_UB_ASSERT(p_is_empty);
    ABTI_UB_ASSERT(p_pop);
    ABTI_UB_ASSERT(p_push);

    ABTI_pool_user_def *p_newdef;
    int abt_errno =
        ABTU_calloc(1, sizeof(ABTI_pool_user_def), (void **)&p_newdef);
    ABTI_CHECK_ERROR(abt_errno);

    ABTI_UB_ASSERT(p_newdef->symbol == NULL); /* This value must be NULL. */
    /* Set values */
    p_newdef->required_def.p_create_unit = p_create_unit;
    p_newdef->required_def.p_free_unit = p_free_unit;
    p_newdef->required_def.p_is_empty = p_is_empty;
    p_newdef->required_def.p_pop = p_pop;
    p_newdef->required_def.p_push = p_push;

    *newdef = ABTI_pool_user_def_get_handle(p_newdef);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Free a pool definition.
 *
 * \c ABT_pool_user_def_free() deallocates the resource used for the pool
 * definition \c def and sets \c def to \c ABT_POOL_USER_DEF_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_PTR{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c def}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in,out] def  pool definition handle
 * @return Error code
 */
int ABT_pool_user_def_free(ABT_pool_user_def *def)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(def);

    ABT_pool_user_def h_def = *def;
    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(h_def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    /* Free the memory */
    ABTU_free(p_def);
    *def = ABT_POOL_USER_DEF_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a pool initialization function to a pool definition.
 *
 * \c ABT_pool_user_def_set_init() registers the pool initialization function
 * \c p_init to a pool definition \c def.  If \c p_init is \c NULL, the
 * corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def     pool definition handle
 * @param[in] p_init  pool initialization function
 * @return Error code
 */
int ABT_pool_user_def_set_init(ABT_pool_user_def def,
                               ABT_pool_user_init_fn p_init)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_init = p_init;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a pool finalization function to a pool definition.
 *
 * \c ABT_pool_user_def_set_free() registers the pool finalization function
 * \c p_free to a pool definition \c def.  If \c p_free is \c NULL, the
 * corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def     pool definition handle
 * @param[in] p_free  pool finalization function
 * @return Error code
 */
int ABT_pool_user_def_set_free(ABT_pool_user_def def,
                               ABT_pool_user_free_fn p_free)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_free = p_free;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a size inquiry function to a pool definition.
 *
 * \c ABT_pool_user_def_set_get_size() registers the size inquiry function
 * \c p_get_size to a pool definition \c def.  If \c p_get_size is \c NULL, the
 * corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def         pool definition handle
 * @param[in] p_get_size  size inquiry function
 * @return Error code
 */
int ABT_pool_user_def_set_get_size(ABT_pool_user_def def,
                                   ABT_pool_user_get_size_fn p_get_size)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_get_size = p_get_size;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a pop-wait function to a pool definition.
 *
 * \c ABT_pool_user_def_set_pop_wait() registers the pop-wait function
 * \c p_pop_wait to a pool definition \c def.  If \c p_pop_wait is \c NULL, the
 * corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def         pool definition handle
 * @param[in] p_pop_wait  pop-wait function
 * @return Error code
 */
int ABT_pool_user_def_set_pop_wait(ABT_pool_user_def def,
                                   ABT_pool_user_pop_wait_fn p_pop_wait)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_pop_wait = p_pop_wait;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a pop-many function to a pool definition.
 *
 * \c ABT_pool_user_def_set_pop_many() registers the pop-many function
 * \c p_pop_many to a pool definition \c def.  If \c p_pop_many is \c NULL, the
 * corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def         pool definition handle
 * @param[in] p_pop_many  pop-many function
 * @return Error code
 */
int ABT_pool_user_def_set_pop_many(ABT_pool_user_def def,
                                   ABT_pool_user_pop_many_fn p_pop_many)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_pop_many = p_pop_many;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a push-many function to a pool definition.
 *
 * \c ABT_pool_user_def_set_push_many() registers the push-many function
 * \c p_push_many to a pool definition \c def.  If \c p_push_many is \c NULL,
 * the corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def          pool definition handle
 * @param[in] p_push_many  push-many function
 * @return Error code
 */
int ABT_pool_user_def_set_push_many(ABT_pool_user_def def,
                                    ABT_pool_user_push_many_fn p_push_many)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_push_many = p_push_many;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_USER_DEF
 * @brief   Register a print-all function to a pool definition.
 *
 * \c ABT_pool_user_def_set_print_all() registers the print-all function
 * \c p_print_all to a pool definition \c def.  If \c p_print_all is \c NULL,
 * the corresponding function is removed from \c def.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c def}
 *
 * @param[in] def          pool definition handle
 * @param[in] p_print_all  print-all function
 * @return Error code
 */
int ABT_pool_user_def_set_print_all(ABT_pool_user_def def,
                                    ABT_pool_user_print_all_fn p_print_all)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
    ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);

    p_def->optional_def.p_print_all = p_print_all;
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABT_bool ABTI_pool_user_def_is_new(const ABT_pool_user_def def)
{
    /* If def points to ABT_pool_def, this u_create_from_thread not be NULL.
     * Otherwise, it is "symbol", so it must be NULL. */
    return def->u_create_from_thread ? ABT_FALSE : ABT_TRUE;
}
