/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define POOL_CONFIG_HTABLE_SIZE 8
typedef struct {
    ABT_pool_config_type type; /* Element type. */
    union {
        int v_int;
        double v_double;
        void *v_ptr;
    } val;
} pool_config_element;

static void pool_config_create_element_int(pool_config_element *p_elem,
                                           int val);
static void pool_config_create_element_double(pool_config_element *p_elem,
                                              double val);
static void pool_config_create_element_ptr(pool_config_element *p_elem,
                                           void *ptr);
ABTU_ret_err static int
pool_config_create_element_typed(pool_config_element *p_elem,
                                 ABT_pool_config_type type, const void *p_val);
static void pool_config_read_element(const pool_config_element *p_elem,
                                     void *ptr);

/** @defgroup POOL_CONFIG Pool config
 * This group is for Pool config.
 */

/* Global configurable parameters */
const ABT_pool_config_var ABT_pool_config_automatic = {
    .key = -2, .type = ABT_POOL_CONFIG_INT
};

/**
 * @ingroup POOL_CONFIG
 * @brief   Create a new pool configuration.
 *
 * \c ABT_pool_config_create() creates a new empty pool configuration and
 * returns its handle through \c config.
 *
 * Currently, Argobots supports the following hints:
 *
 * - \c ABT_pool_config_automatic:
 *
 *   Whether the pool is automatically freed or not.  If the value is
 *   \c ABT_TRUE, the pool is automatically freed when all schedulers associated
 *   with the pool are freed.  If this hint is not specified, the default value
 *   of each pool creation routine is used for pool creation.
 *
 * @note
 * \DOC_NOTE_DEFAULT_POOL_AUTOMATIC
 *
 * \c config must be freed by \c ABT_pool_config_free() after its use.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_POOL_CONFIG_TYPE
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c config}
 *
 * @param[out] config   pool configuration handle
 * @return Error code
 */
int ABT_pool_config_create(ABT_pool_config *config)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    int abt_errno;
    ABTI_pool_config *p_config;

    abt_errno = ABTU_calloc(1, sizeof(ABTI_pool_config), (void **)&p_config);
    ABTI_CHECK_ERROR(abt_errno);
    abt_errno =
        ABTU_hashtable_create(POOL_CONFIG_HTABLE_SIZE,
                              sizeof(pool_config_element), &p_config->p_table);
    if (abt_errno != ABT_SUCCESS) {
        ABTU_free(p_config);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    *config = ABTI_pool_config_get_handle(p_config);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_CONFIG
 * @brief   Free a pool configuration.
 *
 * \c ABT_pool_config_free() deallocates the resource used for the pool
 * configuration \c pool_config and sets \c pool_config to
 * \c ABT_POOL_CONFIG_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_CONFIG_PTR{\c config}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c config}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c config}
 *
 * @param[in,out] config  pool configuration handle
 * @return Error code
 */
int ABT_pool_config_free(ABT_pool_config *config)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(config);

    ABTI_pool_config *p_config = ABTI_pool_config_get_ptr(*config);
    ABTI_CHECK_NULL_POOL_CONFIG_PTR(p_config);

    ABTU_hashtable_free(p_config->p_table);
    ABTU_free(p_config);

    *config = ABT_POOL_CONFIG_NULL;

    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_CONFIG
 * @brief   Register a value to a pool configuration.
 *
 * \c ABT_pool_config_set() associated a value pointed to by the value \c val
 * with the index \c key in the pool configuration \c config.  This routine
 * overwrites a value and its type if a value has already been associated with
 * \c key.
 *
 * @note
 * For example, this routine can be called as follows to set a value that is
 * corresponding to \c key = \a 1.
 * @code{.c}
 * const ABT_pool_config_var var = { 1, ABT_POOL_CONFIG_INT };
 * int val = 10;
 * ABT_pool_config_set(&config, var.key, var.type, &val);
 * @endcode
 *
 * If \c value is \c NULL, this routine deletes a value associated with \c key
 * if such exists.
 *
 * @note
 * This routine returns \c ABT_SUCCESS even if \c value is \c NULL but no value
 * is associated with \c key.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_POOL_CONFIG_TYPE{\c type}
 * \DOC_ERROR_INV_POOL_CONFIG_HANDLE{\c config}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c config}
 *
 * @param[in]  config  pool configuration handle
 * @param[in]  key     index of a target value
 * @param[in]  type    type of a target value
 * @param[in]  val     target value
 * @return Error code
 */
int ABT_pool_config_set(ABT_pool_config config, int key,
                        ABT_pool_config_type type, const void *val)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_config *p_config = ABTI_pool_config_get_ptr(config);
    ABTI_CHECK_NULL_POOL_CONFIG_PTR(p_config);
    if (val) {
        /* Add a value. */
        pool_config_element data;
        int abt_errno;
        abt_errno = pool_config_create_element_typed(&data, type, val);
        ABTI_CHECK_ERROR(abt_errno);
        abt_errno = ABTU_hashtable_set(p_config->p_table, key, &data, NULL);
        ABTI_CHECK_ERROR(abt_errno);
    } else {
        /* Delete a value. */
        ABTU_hashtable_delete(p_config->p_table, key, NULL);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL_CONFIG
 * @brief   Retrieve a value from a pool configuration.
 *
 * \c ABT_pool_config_get() reads a value associated with the index \c key of
 * \c ABT_pool_config_var from the pool configuration \c config.  If \c val is
 * not \c NULL, \c val is set to the value.  If \c type is not \c NULL, \c type
 * is set to the type of the value.
 *
 * @note
 * For example, this routine can be called as follows to get a value that is
 * corresponding to \c key = \a 1.
 * @code{.c}
 * const ABT_pool_config_var var = { 1, ABT_POOL_CONFIG_INT };
 * int val;
 * ABT_pool_config_type type;
 * ABT_pool_config_get(&config, var.key, &type, &val);
 * assert(type == var.type);
 * @endcode
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_POOL_CONFIG_INDEX{\c config, \c key}
 * \DOC_ERROR_INV_POOL_CONFIG_HANDLE{\c config}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c config}
 *
 * @param[in]  config  pool configuration handle
 * @param[in]  key     index of a target value
 * @param[out] type    type of a target value
 * @param[out] val     target value
 * @return Error code
 */
int ABT_pool_config_get(ABT_pool_config config, int key,
                        ABT_pool_config_type *type, void *val)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool_config *p_config = ABTI_pool_config_get_ptr(config);
    ABTI_CHECK_NULL_POOL_CONFIG_PTR(p_config);
    pool_config_element data;
    int found;
    ABTU_hashtable_get(p_config->p_table, key, &data, &found);
    if (found) {
        if (val) {
            pool_config_read_element(&data, val);
        }
        if (type) {
            *type = data.type;
        }
    } else {
        ABTI_HANDLE_ERROR(ABT_ERR_INV_ARG);
    }
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABTU_ret_err int ABTI_pool_config_read(const ABTI_pool_config *p_config,
                                       int key, void *p_val)
{
    int found;
    pool_config_element data;
    ABTU_hashtable_get(p_config->p_table, key, &data, &found);
    if (found) {
        if (p_val) {
            pool_config_read_element(&data, p_val);
        }
        return ABT_SUCCESS;
    } else {
        return ABT_ERR_INV_ARG;
    }
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static void pool_config_create_element_int(pool_config_element *p_elem, int val)
{
    memset(p_elem, 0, sizeof(pool_config_element));
    p_elem->type = ABT_POOL_CONFIG_INT;
    p_elem->val.v_int = val;
}

static void pool_config_create_element_double(pool_config_element *p_elem,
                                              double val)
{
    memset(p_elem, 0, sizeof(pool_config_element));
    p_elem->type = ABT_POOL_CONFIG_DOUBLE;
    p_elem->val.v_double = val;
}

static void pool_config_create_element_ptr(pool_config_element *p_elem,
                                           void *ptr)
{
    memset(p_elem, 0, sizeof(pool_config_element));
    p_elem->type = ABT_POOL_CONFIG_PTR;
    p_elem->val.v_ptr = ptr;
}

ABTU_ret_err static int
pool_config_create_element_typed(pool_config_element *p_elem,
                                 ABT_pool_config_type type, const void *p_val)
{
    switch (type) {
        case ABT_POOL_CONFIG_INT: {
            pool_config_create_element_int(p_elem, *(const int *)p_val);
            break;
        }
        case ABT_POOL_CONFIG_DOUBLE: {
            pool_config_create_element_double(p_elem, *(const double *)p_val);
            break;
        }
        case ABT_POOL_CONFIG_PTR: {
            pool_config_create_element_ptr(p_elem, *(void *const *)p_val);
            break;
        }
        default:
            return ABT_ERR_INV_ARG;
    }
    return ABT_SUCCESS;
}

static void pool_config_read_element(const pool_config_element *p_elem,
                                     void *ptr)
{
    switch (p_elem->type) {
        case ABT_POOL_CONFIG_INT: {
            *((int *)ptr) = p_elem->val.v_int;
            break;
        }
        case ABT_POOL_CONFIG_DOUBLE: {
            *((double *)ptr) = p_elem->val.v_double;
            break;
        }
        case ABT_POOL_CONFIG_PTR: {
            *((void **)ptr) = p_elem->val.v_ptr;
            break;
        }
        default:
            ABTI_ASSERT(0);
    }
}
