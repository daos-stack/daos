/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define SCHED_CONFIG_HTABLE_SIZE 8
typedef struct {
    ABT_sched_config_type type; /* Element type. */
    union {
        int v_int;
        double v_double;
        void *v_ptr;
    } val;
} sched_config_element;

static void sched_config_create_element_int(sched_config_element *p_elem,
                                            int val);
static void sched_config_create_element_double(sched_config_element *p_elem,
                                               double val);
static void sched_config_create_element_ptr(sched_config_element *p_elem,
                                            void *ptr);
ABTU_ret_err static int
sched_config_create_element_typed(sched_config_element *p_elem,
                                  ABT_sched_config_type type,
                                  const void *p_val);
static void sched_config_read_element(const sched_config_element *p_elem,
                                      void *ptr);

/** @defgroup SCHED_CONFIG Scheduler config
 * This group is for Scheduler config.
 */

/* Global configurable parameters */
ABT_sched_config_var ABT_sched_config_var_end = { .idx = -1,
                                                  .type =
                                                      ABT_SCHED_CONFIG_INT };

ABT_sched_config_var ABT_sched_config_access = { .idx = -2,
                                                 .type = ABT_SCHED_CONFIG_INT };

ABT_sched_config_var ABT_sched_config_automatic = { .idx = -3,
                                                    .type =
                                                        ABT_SCHED_CONFIG_INT };

ABT_sched_config_var ABT_sched_basic_freq = { .idx = -4,
                                              .type = ABT_SCHED_CONFIG_INT };

/**
 * @ingroup SCHED_CONFIG
 * @brief   Create a new scheduler configuration.
 *
 * \c ABT_sched_config_create() creates a new scheduler configuration and
 * returns its handle through \c config.
 *
 * The variadic arguments are an array of tuples composed of a variable of type
 * \c ABT_sched_config_var and a value for this variable.  The array must end
 * with a single value \c ABT_sched_config_var_end.
 *
 * Currently, Argobots supports the following hints:
 *
 * - \c ABT_sched_basic_freq:
 *
 *   The frequency of event checks of the predefined scheduler.  A smaller value
 *   indicates more frequent check.  If this is not specified, the default value
 *   is used for scheduler creation.
 *
 * - \c ABT_sched_config_automatic:
 *
 *   Whether the scheduler is automatically freed or not.  If the value is
 *   \c ABT_TRUE, the scheduler is automatically freed when a work unit
 *   associated with the scheduler is freed.  If this is not specified, the
 *   default value of each scheduler creation routine is used for scheduler
 *   creation.
 *
 * - \c ABT_sched_config_access:
 *
 *   This is deprecated and ignored.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED_AUTOMATIC
 *
 * \c config must be freed by \c ABT_sched_config_free() after its use.
 *
 * @note
 * For example, this routine can be called as follows to configure the
 * predefined scheduler to have a frequency for checking events equal to \a 5:
 * @code{.c}
 * ABT_sched_config config;
 * ABT_sched_config_create(&config, ABT_sched_basic_freq, 5,
 *                         ABT_sched_config_var_end);
 * @endcode
 *
 * If the array contains multiple tuples that have the same \c idx of
 * \c ABT_sched_config_var, \c idx is mapped to a corrupted value.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_SCHED_CONFIG_TYPE
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_SCHED_CONFIG_CREATE_UNFORMATTED
 * \DOC_UNDEFINED_NULL_PTR{\c config}
 *
 * @param[out] config   scheduler configuration handle
 * @param[in]  ...      array of arguments
 * @return Error code
 */
int ABT_sched_config_create(ABT_sched_config *config, ...)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    int abt_errno;
    ABTI_sched_config *p_config;

    abt_errno = ABTU_calloc(1, sizeof(ABTI_sched_config), (void **)&p_config);
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno =
        ABTU_hashtable_create(SCHED_CONFIG_HTABLE_SIZE,
                              sizeof(sched_config_element), &p_config->p_table);
    if (abt_errno != ABT_SUCCESS) {
        ABTU_free(p_config);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    va_list varg_list;
    va_start(varg_list, config);

    /* We read (var, value) until we find ABT_sched_config_var_end */
    while (1) {
        ABT_sched_config_var var = va_arg(varg_list, ABT_sched_config_var);
        int idx = var.idx;
        if (idx == ABT_sched_config_var_end.idx)
            break;
        /* Add the argument */
        sched_config_element data;
        switch (var.type) {
            case ABT_SCHED_CONFIG_INT: {
                sched_config_create_element_int(&data, va_arg(varg_list, int));
                break;
            }
            case ABT_SCHED_CONFIG_DOUBLE: {
                sched_config_create_element_double(&data,
                                                   va_arg(varg_list, double));
                break;
            }
            case ABT_SCHED_CONFIG_PTR: {
                sched_config_create_element_ptr(&data,
                                                va_arg(varg_list, void *));
                break;
            }
            default:
                abt_errno = ABT_ERR_INV_ARG;
        }
        if (abt_errno == ABT_SUCCESS) {
            abt_errno = ABTU_hashtable_set(p_config->p_table, idx, &data, NULL);
        }
        if (abt_errno != ABT_SUCCESS) {
            ABTU_hashtable_free(p_config->p_table);
            ABTU_free(p_config);
            va_end(varg_list);
            ABTI_HANDLE_ERROR(abt_errno);
        }
    }
    va_end(varg_list);

    *config = ABTI_sched_config_get_handle(p_config);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED_CONFIG
 * @brief   Retrieve values from a scheduler configuration.
 *
 * \c ABT_sched_config_read() reads values from the scheduler configuration
 * \c config and sets the values to variables given as the variadic arguments
 * that contain at least \c num_vars pointers.  This routine sets the \a i th
 * argument where \a i starts from 0 to a value mapped to a tuple that has
 * \c ABT_sched_config_var with its \c idx = \a i.  Each argument needs to be a
 * pointer of a type specified by a corresponding \c type of
 * \c ABT_sched_config_var.  If the \a i th argument is \c NULL, a value
 * associated with \c idx = \a i is not copied.  If a value associated with
 * \c idx = \a i does not exist, the \a i th argument is not updated.
 *
 * @note
 * For example, this routine can be called as follows to get a value that is
 * corresponding to \c idx = \a 1.
 * @code{.c}
 * // ABT_sched_config_var var = { 1, ABT_SCHED_CONFIG_INT };
 * int val;
 * ABT_sched_config_read(&config, 2, NULL, &val);
 * @endcode
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c num_vars}
 * \DOC_ERROR_INV_SCHED_CONFIG_HANDLE{\c config}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in]  config    scheduler configuration handle
 * @param[in]  num_vars  number of variable pointers in \c ...
 * @param[out] ...       array of variable pointers
 * @return Error code
 */
int ABT_sched_config_read(ABT_sched_config config, int num_vars, ...)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    int idx;
    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);
    ABTI_CHECK_NULL_SCHED_CONFIG_PTR(p_config);

    va_list varg_list;
    va_start(varg_list, num_vars);
    for (idx = 0; idx < num_vars; idx++) {
        void *ptr = va_arg(varg_list, void *);
        if (ptr) {
            sched_config_element data;
            int found;
            ABTU_hashtable_get(p_config->p_table, idx, &data, &found);
            if (found) {
                sched_config_read_element(&data, ptr);
            }
        }
    }
    va_end(varg_list);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED_CONFIG
 * @brief   Free a scheduler configuration.
 *
 * \c ABT_sched_config_free() deallocates the resource used for the scheduler
 * configuration \c sched_config and sets \c sched_config to
 * \c ABT_SCHED_CONFIG_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_CONFIG_PTR{\c config}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c config}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c config}
 *
 * @param[in,out] config  scheduler configuration handle
 * @return Error code
 */
int ABT_sched_config_free(ABT_sched_config *config)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(*config);
    ABTI_CHECK_NULL_SCHED_CONFIG_PTR(p_config);

    ABTU_hashtable_free(p_config->p_table);
    ABTU_free(p_config);

    *config = ABT_SCHED_CONFIG_NULL;

    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED_CONFIG
 * @brief   Register a value to a scheduler configuration.
 *
 * \c ABT_sched_config_set() associated a value pointed to by the value \c val
 * with the index \c idx in the scheduler configuration \c config.  This routine
 * overwrites a value and its type if a value has already been associated with
 * \c idx.
 *
 * @note
 * For example, this routine can be called as follows to set a value that is
 * corresponding to \c idx = \a 1.
 * @code{.c}
 * const ABT_sched_config_var var = { 1, ABT_SCHED_CONFIG_INT };
 * int val = 10;
 * ABT_sched_config_set(&config, var.idx, var.type, &val);
 * @endcode
 *
 * If \c value is \c NULL, this routine deletes a value associated with \c idx
 * if such exists.
 *
 * @note
 * This routine returns \c ABT_SUCCESS even if \c value is \c NULL but no value
 * is associated with \c idx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_SCHED_CONFIG_TYPE{\c type}
 * \DOC_ERROR_INV_SCHED_CONFIG_HANDLE{\c config}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c config}
 *
 * @param[in]  config  scheduler configuration handle
 * @param[in]  idx     index of a target value
 * @param[in]  type    type of a target value
 * @param[in]  val     target value
 * @return Error code
 */
int ABT_sched_config_set(ABT_sched_config config, int idx,
                         ABT_sched_config_type type, const void *val)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);
    ABTI_CHECK_NULL_SCHED_CONFIG_PTR(p_config);
    if (val) {
        /* Add a value. */
        sched_config_element data;
        int abt_errno;
        abt_errno = sched_config_create_element_typed(&data, type, val);
        ABTI_CHECK_ERROR(abt_errno);
        abt_errno = ABTU_hashtable_set(p_config->p_table, idx, &data, NULL);
        ABTI_CHECK_ERROR(abt_errno);
    } else {
        /* Delete a value. */
        ABTU_hashtable_delete(p_config->p_table, idx, NULL);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED_CONFIG
 * @brief   Retrieve a value from a scheduler configuration.
 *
 * \c ABT_sched_config_get() reads a value associated with the index \c idx of
 * \c ABT_sched_config_var from the scheduler configuration \c config.  If
 * \c val is not \c NULL, \c val is set to the value.  If \c type is not
 * \c NULL, \c type is set to the type of the value.
 *
 * @note
 * For example, this routine can be called as follows to get a value that is
 * corresponding to \c idx = \a 1.
 * @code{.c}
 * const ABT_sched_config_var var = { 1, ABT_SCHED_CONFIG_INT };
 * int val;
 * ABT_sched_config_type type;
 * ABT_sched_config_get(&config, var.idx, &type, &val);
 * assert(type == var.type);
 * @endcode
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_SCHED_CONFIG_INDEX{\c config, \c idx}
 * \DOC_ERROR_INV_SCHED_CONFIG_HANDLE{\c config}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c config}
 *
 * @param[in]  config  scheduler configuration handle
 * @param[in]  idx     index of a target value
 * @param[out] type    type of a target value
 * @param[out] val     target value
 * @return Error code
 */
int ABT_sched_config_get(ABT_sched_config config, int idx,
                         ABT_sched_config_type *type, void *val)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);
    ABTI_CHECK_NULL_SCHED_CONFIG_PTR(p_config);
    sched_config_element data;
    int found;
    ABTU_hashtable_get(p_config->p_table, idx, &data, &found);
    if (found) {
        if (val) {
            sched_config_read_element(&data, val);
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

ABTU_ret_err int ABTI_sched_config_read(const ABTI_sched_config *p_config,
                                        int idx, void *p_val)
{
    int found;
    sched_config_element data;
    ABTU_hashtable_get(p_config->p_table, idx, &data, &found);
    if (found) {
        if (p_val) {
            sched_config_read_element(&data, p_val);
        }
        return ABT_SUCCESS;
    } else {
        return ABT_ERR_INV_ARG;
    }
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static void sched_config_create_element_int(sched_config_element *p_elem,
                                            int val)
{
    memset(p_elem, 0, sizeof(sched_config_element));
    p_elem->type = ABT_SCHED_CONFIG_INT;
    p_elem->val.v_int = val;
}

static void sched_config_create_element_double(sched_config_element *p_elem,
                                               double val)
{
    memset(p_elem, 0, sizeof(sched_config_element));
    p_elem->type = ABT_SCHED_CONFIG_DOUBLE;
    p_elem->val.v_double = val;
}

static void sched_config_create_element_ptr(sched_config_element *p_elem,
                                            void *ptr)
{
    memset(p_elem, 0, sizeof(sched_config_element));
    p_elem->type = ABT_SCHED_CONFIG_PTR;
    p_elem->val.v_ptr = ptr;
}

ABTU_ret_err static int
sched_config_create_element_typed(sched_config_element *p_elem,
                                  ABT_sched_config_type type, const void *p_val)
{
    switch (type) {
        case ABT_SCHED_CONFIG_INT: {
            sched_config_create_element_int(p_elem, *(const int *)p_val);
            break;
        }
        case ABT_SCHED_CONFIG_DOUBLE: {
            sched_config_create_element_double(p_elem, *(const double *)p_val);
            break;
        }
        case ABT_SCHED_CONFIG_PTR: {
            sched_config_create_element_ptr(p_elem, *(void *const *)p_val);
            break;
        }
        default:
            return ABT_ERR_INV_ARG;
    }
    return ABT_SUCCESS;
}

static void sched_config_read_element(const sched_config_element *p_elem,
                                      void *ptr)
{
    switch (p_elem->type) {
        case ABT_SCHED_CONFIG_INT: {
            *((int *)ptr) = p_elem->val.v_int;
            break;
        }
        case ABT_SCHED_CONFIG_DOUBLE: {
            *((double *)ptr) = p_elem->val.v_double;
            break;
        }
        case ABT_SCHED_CONFIG_PTR: {
            *((void **)ptr) = p_elem->val.v_ptr;
            break;
        }
        default:
            ABTI_ASSERT(0);
    }
}
