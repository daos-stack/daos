/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static void thread_attr_set_stack(ABTI_global *p_global,
                                  ABTI_thread_attr *p_attr, void *stackaddr,
                                  size_t stacksize);

/** @defgroup ULT_ATTR ULT Attributes
 * ULT attributes are used to specify ULT behavior that is different from the
 * default.
 */

/**
 * @ingroup ULT_ATTR
 * @brief   Create a new ULT attribute.
 *
 * \c ABT_thread_attr_create() creates a ULT attribute with the default
 * attribute parameters and returns its handle through \c newattr.
 *
 * The default parameters are as follows:
 * - Using a memory pool for stack allocation if a memory pool is enabled.
 * - Default stack size, which can be set via \c ABT_THREAD_STACKSIZE.
 * - Migratable.
 * - Invoking no callback function on migration.
 *
 * \c newattr must be freed by \c ABT_thread_attr_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newattr, \c ABT_THREAD_ATTR_NULL}
 * @endchangev20
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
 * @param[out] newattr  ULT attribute handle
 * @return Error code
 */
int ABT_thread_attr_create(ABT_thread_attr *newattr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newattr);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newattr to NULL on error. */
    *newattr = ABT_THREAD_ATTR_NULL;
#endif
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_thread_attr *p_newattr;
    int abt_errno = ABTU_malloc(sizeof(ABTI_thread_attr), (void **)&p_newattr);
    ABTI_CHECK_ERROR(abt_errno);

    /* Default values */
    ABTI_thread_attr_init(p_newattr, NULL, p_global->thread_stacksize,
                          ABT_TRUE);
    *newattr = ABTI_thread_attr_get_handle(p_newattr);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Free a ULT attribute.
 *
 * \c ABT_thread_attr_free() deallocates the resource used for the ULT attribute
 * \c attr and sets \c attr to \c ABT_THREAD_ATTR_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_PTR{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c attr}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c attr}
 *
 * @param[in,out] attr  ULT attribute handle
 * @return Error code
 */
int ABT_thread_attr_free(ABT_thread_attr *attr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(attr);

    ABT_thread_attr h_attr = *attr;
    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(h_attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    /* Free the memory */
    ABTU_free(p_attr);
    *attr = ABT_THREAD_ATTR_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Set stack attributes in a ULT attribute.
 *
 * \c ABT_thread_attr_set_stack() sets the stack address and the stack size (in
 * bytes) in the ULT attribute \c attr.
 *
 * The memory pointed to by \c stackaddr will be used as the stack area for a
 * created ULT.
 *
 * - If \c stackaddr is \c NULL:
 *
 *   A stack with size \c stacksize will be allocated by Argobots on ULT
 *   creation.  This stack will be automatically freed by the Argobots runtime.
 *
 * - If \c stackaddr is not \c NULL:
 *
 *   \c stackaddr must be aligned with 8 bytes.  It is the user's responsibility
 *   to free the stack memory after the ULT, for which \c attr was used, is
 *   freed.
 *
 * @note
 * Sharing the same stack memory with multiple ULTs is not recommended because
 * it can easily corrupt function stacks and crash the program.
 *
 * @changev11
 * \DOC_DESC_V10_ERROR_CODE_CHANGE{\c ABT_ERR_OTHERS, \c ABT_ERR_INV_ARG,
 *                                 \c stackaddr is neither \c NULL nor a memory
 *                                 aligned with 8 bytes}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 * \DOC_ERROR_INV_ARG_INV_STACK{\c stackaddr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c attr}
 *
 * @param[in] attr       ULT attribute handle
 * @param[in] stackaddr  stack address
 * @param[in] stacksize  stack size in bytes
 * @return Error code
 */
int ABT_thread_attr_set_stack(ABT_thread_attr attr, void *stackaddr,
                              size_t stacksize)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);
    /* If stackaddr is not NULL, it must be aligned by 8 bytes. */
    ABTI_CHECK_TRUE(stackaddr == NULL || ((uintptr_t)stackaddr & 0x7) == 0,
                    ABT_ERR_INV_ARG);
    thread_attr_set_stack(p_global, p_attr, stackaddr, stacksize);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Get stack attributes from a ULT attribute.
 *
 * \c ABT_thread_attr_get_stack() retrieves the stack address and the stack size
 * (in bytes) from the ULT attribute \c attr and returns values through
 * \c stackaddr and \c stacksize, respectively.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c stackaddr}
 * \DOC_UNDEFINED_NULL_PTR{\c stacksize}
 *
 * @param[in]  attr       ULT attribute handle
 * @param[out] stackaddr  stack address
 * @param[out] stacksize  stack size in bytes
 * @return Error code
 */
int ABT_thread_attr_get_stack(ABT_thread_attr attr, void **stackaddr,
                              size_t *stacksize)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(stackaddr);
    ABTI_UB_ASSERT(stacksize);

    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    *stackaddr = p_attr->p_stack;
    *stacksize = p_attr->stacksize;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Set stack size in a ULT attribute.
 *
 * \c ABT_thread_attr_set_stacksize() sets the stack size \c stacksize (in
 * bytes) in the ULT attribute \c attr.  If the stack memory has already been
 * set by \c ABT_thread_attr_set_stack(), this routine updates the stack size
 * while keeping the stack memory in \c attr.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c attr}
 *
 * @param[in] attr       ULT attribute handle
 * @param[in] stacksize  stack size in bytes
 * @return Error code
 */
int ABT_thread_attr_set_stacksize(ABT_thread_attr attr, size_t stacksize)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    thread_attr_set_stack(p_global, p_attr, p_attr->p_stack, stacksize);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Get the stack size from a ULT attribute.
 *
 * \c ABT_thread_attr_get_stacksize() retrieves the stack size (in bytes) from
 * the ULT attribute \c attr and returns it through \c stacksize.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c stacksize}
 *
 * @param[in]  attr       ULT attribute handle
 * @param[out] stacksize  stack size in bytes
 * @return Error code
 */
int ABT_thread_attr_get_stacksize(ABT_thread_attr attr, size_t *stacksize)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(stacksize);

    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    *stacksize = p_attr->stacksize;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT_ATTR
 * @brief   Set a callback function and its argument in a ULT attribute.
 *
 * \c ABT_thread_attr_set_callback() sets the callback function \c cb_func and
 * its argument \c cb_arg in the ULT attribute \c attr.  If \c cb_func is
 * \c NULL, this routine unsets the callback function in \c attr.  Otherwise,
 * \c cb_func and \c cb_arg are set in \c attr.
 *
 * If the callback function is registered to a work unit, \c cb_func() will be
 * called every time when the corresponding work unit is migrated.  The first
 * argument of \c cb_func() is the handle of a migrated work unit.  The second
 * argument is \c cb_arg passed to this routine.  The caller of the callback
 * function is undefined, so a program that relies on the caller of \c cb_func()
 * is non-conforming.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_CHANGE_STATE{\c cb_func()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c attr}
 *
 * @param[in] attr     ULT attribute handle
 * @param[in] cb_func  callback function pointer
 * @param[in] cb_arg   argument for the callback function
 * @return Error code
 */
int ABT_thread_attr_set_callback(ABT_thread_attr attr,
                                 void (*cb_func)(ABT_thread thread,
                                                 void *cb_arg),
                                 void *cb_arg)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    /* Set the value */
    p_attr->f_cb = cb_func;
    p_attr->p_cb_arg = cb_arg;
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#endif
}

/**
 * @ingroup ULT_ATTR
 * @brief   Set the ULT's migratability in a ULT attribute.
 *
 * \c ABT_thread_attr_set_migratable() sets the ULT's migratability
 * \c is_migratable in the ULT attribute \c attr.  If \c is_migratable is
 * \c ABT_TRUE, the ULT created with this attribute is migratable.  If
 * \c is_migratable is \c ABT_FALSE, the ULT created with this attribute is not
 * migratable.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_BOOL{\c is_migratable}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c attr}
 *
 * @param[in] attr           ULT attribute handle
 * @param[in] is_migratable  flag (\c ABT_TRUE: migratable, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_thread_attr_set_migratable(ABT_thread_attr attr, ABT_bool is_migratable)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT_BOOL(is_migratable);

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);

    /* Set the value */
    p_attr->migratable = is_migratable;
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#endif
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

void ABTI_thread_attr_print(ABTI_thread_attr *p_attr, FILE *p_os, int indent)
{
    if (p_attr == NULL) {
        fprintf(p_os, "%*sULT attr: [NULL ATTR]\n", indent, "");
    } else {
#ifndef ABT_CONFIG_DISABLE_MIGRATION
        fprintf(p_os,
                "%*sULT attr: ["
                "stack:%p "
                "stacksize:%zu "
                "migratable:%s "
                "cb_arg:%p"
                "]\n",
                indent, "", p_attr->p_stack, p_attr->stacksize,
                (p_attr->migratable == ABT_TRUE ? "TRUE" : "FALSE"),
                p_attr->p_cb_arg);
#else
        fprintf(p_os,
                "%*sULT attr: ["
                "stack:%p "
                "stacksize:%zu "
                "]\n",
                indent, "", p_attr->p_stack, p_attr->stacksize);
#endif
    }
    fflush(p_os);
}

ABTU_ret_err int ABTI_thread_attr_dup(const ABTI_thread_attr *p_attr,
                                      ABTI_thread_attr **pp_dup_attr)
{
    ABTI_thread_attr *p_dup_attr;
    int abt_errno = ABTU_malloc(sizeof(ABTI_thread_attr), (void **)&p_dup_attr);
    ABTI_CHECK_ERROR(abt_errno);

    memcpy(p_dup_attr, p_attr, sizeof(ABTI_thread_attr));
    *pp_dup_attr = p_dup_attr;
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static void thread_attr_set_stack(ABTI_global *p_global,
                                  ABTI_thread_attr *p_attr, void *stackaddr,
                                  size_t stacksize)
{
    if (stackaddr != NULL) {
        /* This check must be done by the caller. */
        ABTI_ASSERT(((uintptr_t)stackaddr & 0x7) == 0);
        /* Only a descriptor will be allocated from a memory pool.  A stack
         * is given by the user. */
    }
    p_attr->p_stack = stackaddr;
    p_attr->stacksize = stacksize;
}
