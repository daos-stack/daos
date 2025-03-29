/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_PLUGIN_H
#define NA_PLUGIN_H

#include "na.h"
#include "na_error.h"

#include "mercury_param.h"
#include "mercury_queue.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/* Private callback type for NA plugins */
typedef void (*na_plugin_cb_t)(void *arg);

/* Completion data stored in completion queue */
struct na_cb_completion_data {
    struct na_cb_info callback_info; /* Callback info struct */
    na_cb_t callback;                /* Pointer to function */
    na_plugin_cb_t plugin_callback;  /* Callback which will be called after
                                      * the user callback returns. */
    void *plugin_callback_args;      /* Argument to plugin_callback */
    STAILQ_ENTRY(na_cb_completion_data) entry; /* Completion queue entry */
};

/*****************/
/* Public Macros */
/*****************/

/* Remove warnings from variables that are only used for debug */
#ifdef NDEBUG
#    define NA_DEBUG_USED NA_UNUSED
#else
#    define NA_DEBUG_USED
#endif

#ifdef NA_HAS_DEBUG
#    define NA_DEBUG_LOG_USED
#else
#    define NA_DEBUG_LOG_USED NA_UNUSED
#endif

/* Make sure it executes first */
#define NA_CONSTRUCTOR HG_ATTR_CONSTRUCTOR

/* Destructor */
#define NA_DESTRUCTOR HG_ATTR_DESTRUCTOR

/**
 * container_of - cast a member of a structure out to the containing structure
 * \ptr:        the pointer to the member.
 * \type:       the type of the container struct this is embedded in.
 * \member:     the name of the member within the struct.
 *
 */
#if !defined(container_of)
#    define container_of(ptr, type, member)                                    \
        ((type *) ((char *) ptr - offsetof(type, member)))
#endif

/**
 * Plugin ops definition
 */
#define NA_PLUGIN_OPS(plugin_name) na_##plugin_name##_class_ops_g

/**
 * Encode type
 */
#define NA_TYPE_ENCODE(label, ret, buf_ptr, buf_size_left, data, size)         \
    do {                                                                       \
        NA_CHECK_ERROR(buf_size_left < size, label, ret, NA_OVERFLOW,          \
            "Buffer size too small (%zu)", buf_size_left);                     \
        memcpy(buf_ptr, data, size);                                           \
        buf_ptr += size;                                                       \
        buf_size_left -= size;                                                 \
    } while (0)

#define NA_ENCODE(label, ret, buf_ptr, buf_size_left, data, type)              \
    NA_TYPE_ENCODE(label, ret, buf_ptr, buf_size_left, data, sizeof(type))

#define NA_ENCODE_ARRAY(label, ret, buf_ptr, buf_size_left, data, type, count) \
    NA_TYPE_ENCODE(                                                            \
        label, ret, buf_ptr, buf_size_left, data, sizeof(type) * count)

/**
 * Decode type
 */
#define NA_TYPE_DECODE(label, ret, buf_ptr, buf_size_left, data, size)         \
    do {                                                                       \
        NA_CHECK_ERROR(buf_size_left < size, label, ret, NA_OVERFLOW,          \
            "Buffer size too small (%zu)", buf_size_left);                     \
        memcpy(data, buf_ptr, size);                                           \
        buf_ptr += size;                                                       \
        buf_size_left -= size;                                                 \
    } while (0)

#define NA_DECODE(label, ret, buf_ptr, buf_size_left, data, type)              \
    NA_TYPE_DECODE(label, ret, buf_ptr, buf_size_left, data, sizeof(type))

#define NA_DECODE_ARRAY(label, ret, buf_ptr, buf_size_left, data, type, count) \
    NA_TYPE_DECODE(                                                            \
        label, ret, buf_ptr, buf_size_left, data, sizeof(type) * count)

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/* Private routines for use inside NA plugins */

/**
 * Convert cb type to string (null terminated).
 *
 * \param cb_type [IN]          callback type
 *
 * \return String
 */
NA_PLUGIN_VISIBILITY const char *
na_cb_type_to_string(na_cb_type_t cb_type) NA_WARN_UNUSED_RESULT;

/**
 * Allocate protocol info entry.
 *
 * \param class_name [IN]       NA class name (e.g., ofi)
 * \param protocol_name [IN]    protocol name (e.g., tcp)
 * \param device_name [IN]      device name (e.g., eth0)
 *
 * \return Pointer to allocated entry or NULL in case of failure
 */
NA_PLUGIN_VISIBILITY struct na_protocol_info *
na_protocol_info_alloc(const char *class_name, const char *protocol_name,
    const char *device_name) NA_WARN_UNUSED_RESULT;

/**
 * Free protocol info entry.
 *
 * \param entry [IN/OUT]        pointer to protocol info entry
 */
NA_PLUGIN_VISIBILITY void
na_protocol_info_free(struct na_protocol_info *entry);

/**
 * Add callback to context completion queue.
 *
 * \param context [IN/OUT]              pointer to context of execution
 * \param na_cb_completion_data [IN]    pointer to completion data
 *
 */
NA_PLUGIN_VISIBILITY void
na_cb_completion_add(
    na_context_t *context, struct na_cb_completion_data *na_cb_completion_data);

/*********************/
/* Public Variables */
/*********************/

/* SM and MPI must remain in the library as they provide their own APIs */
#ifdef NA_HAS_SM
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(sm);
#endif
#ifndef NA_HAS_DYNAMIC_PLUGINS
#    ifdef NA_HAS_OFI
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(ofi);
#    endif
#    ifdef NA_HAS_UCX
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(ucx);
#    endif
#endif
#ifdef NA_HAS_BMI
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(bmi);
#endif
#ifdef NA_HAS_MPI
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(mpi);
#endif
#ifdef NA_HAS_PSM
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(psm);
#endif
#ifdef NA_HAS_PSM2
extern NA_PRIVATE const struct na_class_ops NA_PLUGIN_OPS(psm2);
#endif

#ifdef __cplusplus
}
#endif

#endif /* NA_PLUGIN_H */
