/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup MUTEX_ATTR Mutex Attributes
 * This group is for Mutex Attributes.  Mutex attributes are used to specify
 * mutex behavior that is different from the default.
 */

/**
 * @ingroup MUTEX_ATTR
 * @brief   Create a new mutex attribute.
 *
 * \c ABT_mutex_attr_create() creates a new mutex attribute with default
 * attribute values and returns its handle through \c newattr.
 *
 * The default parameters are as follows:
 * - Not recursive.
 *
 * \c newattr must be freed by \c ABT_mutex_attr_free() after its use.
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
 * \DOC_UNDEFINED_NULL_PTR{\c newattr}
 *
 * @param[out] newattr  mutex attribute handle
 * @return Error code
 */
int ABT_mutex_attr_create(ABT_mutex_attr *newattr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newattr);

    ABTI_mutex_attr *p_newattr;

    int abt_errno = ABTU_malloc(sizeof(ABTI_mutex_attr), (void **)&p_newattr);
    ABTI_CHECK_ERROR(abt_errno);

    /* Default values */
    p_newattr->attrs = ABTI_MUTEX_ATTR_NONE;

    /* Return value */
    *newattr = ABTI_mutex_attr_get_handle(p_newattr);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX_ATTR
 * @brief   Free a mutex attribute.
 *
 * \c ABT_mutex_attr_free() deallocates the resource used for the mutex
 * attribute \c attr and sets \c attr to \c ABT_MUTEX_ATTR_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_ATTR_PTR{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c attr}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c attr}
 *
 * @param[in,out] attr  mutex attribute handle
 * @return Error code
 */
int ABT_mutex_attr_free(ABT_mutex_attr *attr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(attr);

    ABT_mutex_attr h_attr = *attr;
    ABTI_mutex_attr *p_attr = ABTI_mutex_attr_get_ptr(h_attr);
    ABTI_CHECK_NULL_MUTEX_ATTR_PTR(p_attr);

    /* Free the memory */
    ABTU_free(p_attr);
    /* Return value */
    *attr = ABT_MUTEX_ATTR_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX_ATTR
 * @brief   Set a recursive property in a mutex attribute.
 *
 * \c ABT_mutex_attr_set_recursive() sets the recursive property (i.e., whether
 * the mutex can be locked multiple times by the same owner or not) in the mutex
 * attribute \c attr.  If \c recursive is \c ABT_TRUE, the recursive flag of
 * \c attr is set.  Otherwise, the recursive flag of \c attr is unset.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_BOOL{\c recursive}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c attr}
 *
 * @param[in] attr       mutex attribute handle
 * @param[in] recursive  flag for a recursive locking support
 * @return Error code
 */
int ABT_mutex_attr_set_recursive(ABT_mutex_attr attr, ABT_bool recursive)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT_BOOL(recursive);

    ABTI_mutex_attr *p_attr = ABTI_mutex_attr_get_ptr(attr);
    ABTI_CHECK_NULL_MUTEX_ATTR_PTR(p_attr);

    /* Set the value */
    if (recursive == ABT_TRUE) {
        p_attr->attrs |= ABTI_MUTEX_ATTR_RECURSIVE;
    } else {
        p_attr->attrs &= ~ABTI_MUTEX_ATTR_RECURSIVE;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX_ATTR
 * @brief   Get a recursive property in a mutex attribute.
 *
 * \c ABT_mutex_attr_get_recursive() retrieves the recursive property (i.e.,
 * whether the mutex can be locked multiple times by the same owner or not) in
 * the mutex attribute \c attr.  If \c attr is configured to be recursive,
 * \c recursive is set to \c ABT_TRUE.  Otherwise, \c recursive is set to
 * \c ABT_FALSE.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c recursive}
 *
 * @param[in] attr       mutex attribute handle
 * @param[in] recursive  flag for a recursive locking support
 * @return Error code
 */
int ABT_mutex_attr_get_recursive(ABT_mutex_attr attr, ABT_bool *recursive)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(recursive);

    ABTI_mutex_attr *p_attr = ABTI_mutex_attr_get_ptr(attr);
    ABTI_CHECK_NULL_MUTEX_ATTR_PTR(p_attr);

    /* Get the value */
    if (p_attr->attrs & ABTI_MUTEX_ATTR_RECURSIVE) {
        *recursive = ABT_TRUE;
    } else {
        *recursive = ABT_FALSE;
    }
    return ABT_SUCCESS;
}
