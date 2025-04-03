/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_H
#define MERCURY_H

#include "mercury_header.h"
#include "mercury_types.h"

#include "mercury_core.h"

#include <stdio.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/* See mercury_types.h */

/*****************/
/* Public Macros */
/*****************/

/* See mercury_types.h */

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get Mercury version number.
 *
 * \param major_p [OUT]         pointer to unsigned integer
 * \param minor_p [OUT]         pointer to unsigned integer
 * \param patch_p [OUT]         pointer to unsigned integer
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Version_get(
    unsigned int *major_p, unsigned int *minor_p, unsigned int *patch_p);

/**
 * Convert error return code to string (null terminated).
 *
 * \param errnum [IN]           error return code
 *
 * \return String
 */
HG_PUBLIC const char *
HG_Error_to_string(hg_return_t errnum) HG_WARN_UNUSED_RESULT;

/**
 * Get information on protocols that are supported by underlying NA plugins. If
 * \info_string is NULL, a list of all supported protocols by all plugins will
 * be returned. The returned list must be freed using
 * HG_Free_na_protocol_info().
 *
 * \param info_string [IN]          NULL or "<protocol>" or "<plugin+protocol>"
 * \param na_protocol_info_p [OUT]  linked-list of protocol infos
 *
 * \return HG_SUCCESS or corresponding NA error code
 */
static HG_INLINE hg_return_t
HG_Get_na_protocol_info(
    const char *info_string, struct na_protocol_info **na_protocol_info_p);

/**
 * Free protocol info.
 *
 * \param na_protocol_info [IN/OUT] linked-list of protocol infos
 */
static HG_INLINE void
HG_Free_na_protocol_info(struct na_protocol_info *na_protocol_info);

/**
 * Initialize the Mercury layer.
 * Must be finalized with HG_Finalize().
 *
 * \param na_info_string [IN]   host address with port number (e.g.,
 *                              "tcp://localhost:3344" or
 *                              "bmi+tcp://localhost:3344")
 * \param na_listen [IN]        listen for incoming connections
 *
 * \return Pointer to HG class or NULL in case of failure
 */
HG_PUBLIC hg_class_t *
HG_Init(const char *na_info_string, uint8_t na_listen) HG_WARN_UNUSED_RESULT;

/**
 * Initialize the Mercury layer with options provided by init_info.
 * Must be finalized with HG_Finalize(). Using this routine limits the info
 * struct version to 2.2 version. It is recommended to use \HG_Init_opt2() for
 * mercury versions >= 2.3.0.
 * \remark HG_Init_opt() may become HG_Init() in the future.
 *
 * \param na_info_string [IN]   host address with port number (e.g.,
 *                              "tcp://localhost:3344" or
 *                              "bmi+tcp://localhost:3344")
 * \param na_listen [IN]        listen for incoming connections
 * \param hg_init_info [IN]     (Optional) HG init info, NULL if no info
 *
 * \return Pointer to HG class or NULL in case of failure
 */
HG_PUBLIC hg_class_t *
HG_Init_opt(const char *na_info_string, uint8_t na_listen,
    const struct hg_init_info *hg_init_info) HG_WARN_UNUSED_RESULT;

/**
 * Initialize the Mercury layer with options provided by init_info.
 * Must be finalized with HG_Finalize().
 * \remark HG_Init_opt() may become HG_Init() in the future.
 *
 * \param na_info_string [IN]   host address with port number (e.g.,
 *                              "tcp://localhost:3344" or
 *                              "bmi+tcp://localhost:3344")
 * \param na_listen [IN]        listen for incoming connections
 * \param version [IN]          API version of the init info struct
 * \param hg_init_info [IN]     (Optional) HG init info, NULL if no info
 *
 * \return Pointer to HG class or NULL in case of failure
 */
HG_PUBLIC hg_class_t *
HG_Init_opt2(const char *na_info_string, uint8_t na_listen,
    unsigned int version,
    const struct hg_init_info *hg_init_info) HG_WARN_UNUSED_RESULT;

/**
 * Finalize the Mercury layer.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Finalize(hg_class_t *hg_class);

/**
 * Clean up all temporary files that were created in previous HG instances.
 * While temporary resources (e.g., tmp files) are cleaned up on a call
 * to HG_Finalize(), this routine gives a chance to programs that terminate
 * abnormally to easily clean up those resources.
 */
HG_PUBLIC void
HG_Cleanup(void);

/**
 * Set the log level for HG. That setting is valid for all HG classes.
 *
 * \param level [IN]            level string, valid values are:
 *                                "none", "error", "warning", "debug"
 */
HG_PUBLIC void
HG_Set_log_level(const char *level);

/**
 * Set the log sub-system for HG. That setting is valid for all HG classes.
 *
 * \param subsys [IN]           string of subsystems, format is:
 *                                subsys1,subsys2,subsys3,etc
 *                              subsystem can be turned off, e.g.:
 *                                ~subsys1
 */
HG_PUBLIC void
HG_Set_log_subsys(const char *subsys);

/**
 * Set the log function to use for HG. That setting is valid for all HG classes.
 *
 * \param log_func [IN]         function to use
 */
HG_PUBLIC void
HG_Set_log_func(int (*log_func)(FILE *stream, const char *format, ...));

/**
 * Set the file stream to use for logging output.
 * This setting is valid for all HG classes.
 *
 * \param level [IN]            level string, valid values are:
 *                                "error", "warning", "debug"
 * \param stream [IN]           file stream pointer
 */
HG_PUBLIC void
HG_Set_log_stream(const char *level, FILE *stream);

/**
 * Dump diagnostic counters into the existing log stream.
 */
HG_PUBLIC void
HG_Diag_dump_counters(void);

/**
 * Obtain the name of the given class.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the name of the class, or NULL if not a valid class
 */
static HG_INLINE const char *
HG_Class_get_name(const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Obtain the protocol of the given class.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the name of the class's transport, or NULL if not a valid class
 */
static HG_INLINE const char *
HG_Class_get_protocol(const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Test whether class is listening or not.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return true if listening or false if not, or not a valid class
 */
static HG_INLINE bool
HG_Class_is_listening(const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Obtain the maximum eager size for sending RPC inputs, for a given class.
 * NOTE: This doesn't currently work when using XDR encoding.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the maximum size, or 0 if hg_class is not a valid class or XDR is
 * being used
 */
static HG_INLINE hg_size_t
HG_Class_get_input_eager_size(const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Obtain the maximum eager size for sending RPC outputs, for a given class.
 * NOTE: This doesn't currently work when using XDR encoding.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the maximum size, or 0 if hg_class is not a valid class or XDR is
 * being used
 */
static HG_INLINE hg_size_t
HG_Class_get_output_eager_size(
    const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Set offset used for serializing / deserializing input. This allows upper
 * layers to manually define a reserved space that can be used for the
 * definition of custom headers. The actual input is encoded / decoded
 * using the defined offset. By default, no offset is set.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param offset [IN]           offset size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Class_set_input_offset(hg_class_t *hg_class, hg_size_t offset);

/**
 * Set offset used for serializing / deserializing output. This allows upper
 * layers to manually define a reserved space that can be used for the
 * definition of custom headers. The actual output is encoded / decoded
 * using the defined offset. By default, no offset is set.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param offset [IN]           offset size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Class_set_output_offset(hg_class_t *hg_class, hg_size_t offset);

/**
 * Associate user data to class. When HG_Finalize() is called,
 * free_callback (if defined) is called to free the associated data.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param data [IN]             pointer to user data
 * \param free_callback [IN]    pointer to function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Class_set_data(
    hg_class_t *hg_class, void *data, void (*free_callback)(void *));

/**
 * Retrieve previously associated data from a given class.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return Pointer to user data or NULL if not set or any error has occurred
 */
static HG_INLINE void *
HG_Class_get_data(const hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Get diagnostic counters associated to HG class.
 * (Requires debug enabled build)
 *
 * \param hg_class [IN]             pointer to HG class
 * \param diag_counters [IN/OUT]    pointer to counters struct
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Class_get_counters(
    const hg_class_t *hg_class, struct hg_diag_counters *diag_counters);

/**
 * Set callback to be called on HG handle creation. Handles are created
 * both on HG_Create() and HG_Context_create() calls. This allows upper layers
 * to create and attach data to a handle (using HG_Set_data()) and later
 * retrieve it using HG_Get_data().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Class_set_handle_create_callback(hg_class_t *hg_class,
    hg_return_t (*callback)(hg_handle_t, void *), void *arg);

/**
 * Create a new context. Must be destroyed by calling HG_Context_destroy().
 *
 * \remark This routine is internally equivalent to:
 *   - HG_Core_context_create()
 *   - If listening
 *       - HG_Core_context_post() with repost set to HG_TRUE
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return Pointer to HG context or NULL in case of failure
 */
HG_PUBLIC hg_context_t *
HG_Context_create(hg_class_t *hg_class) HG_WARN_UNUSED_RESULT;

/**
 * Create a new context with a user-defined context identifier. The context
 * identifier can be used to route RPC requests to specific contexts by using
 * HG_Set_target_id().
 * Context must be destroyed by calling HG_Context_destroy().
 *
 * \remark This routine is internally equivalent to:
 *   - HG_Core_context_create_id() with specified context ID
 *   - If listening
 *       - HG_Core_context_post() with repost set to HG_TRUE
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               user-defined context ID
 *
 * \return Pointer to HG context or NULL in case of failure
 */
HG_PUBLIC hg_context_t *
HG_Context_create_id(hg_class_t *hg_class, uint8_t id) HG_WARN_UNUSED_RESULT;

/**
 * Destroy a context created by HG_Context_create(). If listening and
 * HG_Context_unpost() has not already been called, also cancels previously
 * posted requests.
 *
 * \param context [IN]          pointer to HG context
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Context_destroy(hg_context_t *context);

/**
 * Unpost pre-posted requests if listening. This prevents any further RPCs
 * from being received by that context.
 *
 * \param context [IN]          pointer to HG context
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Context_unpost(hg_context_t *context);

/**
 * Retrieve the class used to create the given context.
 *
 * \param context [IN]          pointer to HG context
 *
 * \return Pointer to associated HG class or NULL if not a valid context
 */
static HG_INLINE hg_class_t *
HG_Context_get_class(const hg_context_t *context) HG_WARN_UNUSED_RESULT;

/**
 * Retrieve context ID from context (max value of 255).
 *
 * \param context [IN]          pointer to HG context
 *
 * \return Non-negative integer (max value of 255) or 0 if no ID has been set
 */
static HG_INLINE uint8_t
HG_Context_get_id(const hg_context_t *context) HG_WARN_UNUSED_RESULT;

/**
 * Associate user data to context. When HG_Context_destroy() is called,
 * free_callback (if defined) is called to free the associated data.
 *
 * \param context [IN]          pointer to HG context
 * \param data [IN]             pointer to user data
 * \param free_callback [IN]    pointer to function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Context_set_data(
    hg_context_t *context, void *data, void (*free_callback)(void *));

/**
 * Retrieve previously associated data from a given context.
 *
 * \param context [IN]          pointer to HG context
 *
 * \return Pointer to user data or NULL if not set or any error has occurred
 */
static HG_INLINE void *
HG_Context_get_data(const hg_context_t *context) HG_WARN_UNUSED_RESULT;

/**
 * Dynamically register a function func_name as an RPC as well as the
 * RPC callback executed when the RPC request ID associated to func_name is
 * received. Associate input and output proc to function ID, so that they can
 * be used to serialize and deserialize function parameters.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param func_name [IN]        unique name associated to function
 * \param in_proc_cb [IN]       pointer to input proc callback
 * \param out_proc_cb [IN]      pointer to output proc callback
 * \param rpc_cb [IN]           RPC callback
 *
 * \return unique ID associated to the registered function
 */
HG_PUBLIC hg_id_t
HG_Register_name(hg_class_t *hg_class, const char *func_name,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb,
    hg_rpc_cb_t rpc_cb) HG_WARN_UNUSED_RESULT;

/*
 * Indicate whether HG_Register_name() has been called for the RPC specified by
 * func_name.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param func_name [IN]        function name
 * \param id_p [OUT]            registered RPC ID
 * \param flag_p [OUT]          pointer to boolean
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Registered_name(hg_class_t *hg_class, const char *func_name, hg_id_t *id_p,
    uint8_t *flag_p);

/**
 * Dynamically register an RPC ID as well as the RPC callback executed when the
 * RPC request ID is received. Associate input and output proc to id, so that
 * they can be used to serialize and deserialize function parameters.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               ID to use to register RPC
 * \param in_proc_cb [IN]       pointer to input proc callback
 * \param out_proc_cb [IN]      pointer to output proc callback
 * \param rpc_cb [IN]           RPC callback
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Register(hg_class_t *hg_class, hg_id_t id, hg_proc_cb_t in_proc_cb,
    hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb);

/**
 * Deregister RPC ID. Further requests with RPC ID will return an error, it
 * is therefore up to the user to make sure that all requests for that RPC ID
 * have been treated before it is unregistered.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Deregister(hg_class_t *hg_class, hg_id_t id);

/**
 * Indicate whether HG_Register() has been called.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               function ID
 * \param flag_p [OUT]          pointer to boolean
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Registered(hg_class_t *hg_class, hg_id_t id, uint8_t *flag_p);

/**
 * Indicate whether HG_Register() has been called, and if so return pointers
 * to proc callback functions for the RPC.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               function ID
 * \param flag_p [OUT]          pointer to boolean
 * \param in_proc_cb_p [OUT]    pointer to input encoder cb
 * \param out_proc_cb_p [OUT]   pointer to output encoder cb
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Registered_proc_cb(hg_class_t *hg_class, hg_id_t id, uint8_t *flag_p,
    hg_proc_cb_t *in_proc_cb_p, hg_proc_cb_t *out_proc_cb_p);

/**
 * Register and associate user data to registered function. When HG_Finalize()
 * is called, free_callback (if defined) is called to free the registered
 * data.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 * \param data [IN]             pointer to data
 * \param free_callback [IN]    pointer to function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Register_data(hg_class_t *hg_class, hg_id_t id, void *data,
    void (*free_callback)(void *));

/**
 * Indicate whether HG_Register_data() has been called and return associated
 * data.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 *
 * \return Pointer to data or NULL
 */
HG_PUBLIC void *
HG_Registered_data(hg_class_t *hg_class, hg_id_t id) HG_WARN_UNUSED_RESULT;

/**
 * Disable response for a given RPC ID. This allows an origin process to send an
 * RPC to a target without waiting for a response. The RPC completes locally and
 * the callback on the origin is therefore pushed to the completion queue once
 * the RPC send is completed. By default, all RPCs expect a response to
 * be sent back.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 * \param disable [IN]          boolean (HG_TRUE to disable
 *                                       HG_FALSE to re-enable)
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Registered_disable_response(
    hg_class_t *hg_class, hg_id_t id, uint8_t disable);

/**
 * Check if response is disabled for a given RPC ID
 * (i.e., HG_Registered_disable_response() has been called for this RPC ID).
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 * \param disabled_p [OUT]      boolean (HG_TRUE if disabled
 *                                       HG_FALSE if enabled)
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Registered_disabled_response(
    hg_class_t *hg_class, hg_id_t id, uint8_t *disabled_p);

/**
 * Lookup an addr from a peer address/name. Addresses need to be
 * freed by calling HG_Addr_free(). After completion, user callback is
 * placed into a completion queue and can be triggered using HG_Trigger().
 *
 * \param context [IN]          pointer to context of execution
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param name [IN]             lookup name
 * \param op_id [OUT]           pointer to returned operation ID (unused)
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_lookup1(hg_context_t *context, hg_cb_t callback, void *arg,
    const char *name, hg_op_id_t *op_id_p);

/* This will map to HG_Addr_lookup2() in the future */
#ifndef HG_Addr_lookup
#    define HG_Addr_lookup HG_Addr_lookup1
#endif

/**
 * Lookup an addr from a peer address/name. Addresses need to be
 * freed by calling HG_Addr_free().
 *
 * \remark This is the immediate version of HG_Addr_lookup1().
 *
 * \param hg_class [IN/OUT]     pointer to HG class
 * \param name [IN]             lookup name
 * \param addr_p [OUT]          pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_lookup2(hg_class_t *hg_class, const char *name, hg_addr_t *addr_p);

/**
 * Free the addr.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_free(hg_class_t *hg_class, hg_addr_t addr);

/**
 * Hint that the address is no longer valid. This may happen if the peer is
 * no longer responding. This can be used to force removal of the
 * peer address from the list of the peers, before freeing it and reclaim
 * resources.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_set_remove(hg_class_t *hg_class, hg_addr_t addr);

/**
 * Access self address. Address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr_p [OUT]            pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_self(hg_class_t *hg_class, hg_addr_t *addr_p);

/**
 * Duplicate an existing HG abstract address. The duplicated address can be
 * stored for later use and the origin address be freed safely. The duplicated
 * address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 * \param new_addr_p [OUT]      pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_dup(hg_class_t *hg_class, hg_addr_t addr, hg_addr_t *new_addr_p);

/**
 * Compare two addresses.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr1 [IN]            abstract address
 * \param addr2 [IN]            abstract address
 *
 * \return HG_TRUE if addresses are determined to be equal, HG_FALSE otherwise
 */
HG_PUBLIC uint8_t
HG_Addr_cmp(hg_class_t *hg_class, hg_addr_t addr1,
    hg_addr_t addr2) HG_WARN_UNUSED_RESULT;

/**
 * Convert an addr to a string (returned string includes the terminating
 * null byte '\0'). If buf is NULL, the address is not converted and only
 * the required size of the buffer is returned. If the input value passed
 * through buf_size is too small, HG_SIZE_ERROR is returned and the buf_size
 * output is set to the minimum size required.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param buf [IN/OUT]          pointer to destination buffer
 * \param buf_size_p [IN/OUT]   pointer to buffer size
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Addr_to_string(
    hg_class_t *hg_class, char *buf, hg_size_t *buf_size_p, hg_addr_t addr);

/**
 * Initiate a new HG RPC using the specified function ID and the local/remote
 * target defined by addr. The HG handle created can be used to query input
 * and output, as well as issuing the RPC by calling HG_Forward().
 * After completion the handle must be freed using HG_Destroy().
 *
 * \param context [IN]          pointer to HG context
 * \param addr [IN]             abstract network address of destination
 * \param id [IN]               registered function ID
 * \param handle_p [OUT]        pointer to HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Create(
    hg_context_t *context, hg_addr_t addr, hg_id_t id, hg_handle_t *handle_p);

/**
 * Destroy HG handle. Decrement reference count, resources associated to the
 * handle are freed when the reference count is null.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Destroy(hg_handle_t handle);

/**
 * Reset an existing HG handle to make it reusable for RPC forwarding.
 * Both target address and RPC ID can be modified at this time.
 * Operations on that handle must be completed in order to reset that handle
 * safely.
 *
 * \param handle [IN]           HG handle
 * \param addr [IN]             abstract network address of destination
 * \param id [IN]               registered function ID
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Reset(hg_handle_t handle, hg_addr_t addr, hg_id_t id);

/**
 * Increment ref count on handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Ref_incr(hg_handle_t hg_handle);

/**
 * Retrieve ref count from handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return Non-negative value or negative if the handle is not valid
 */
static HG_INLINE int32_t
HG_Ref_get(hg_handle_t handle) HG_WARN_UNUSED_RESULT;

/**
 * Get info from handle.
 *
 * \remark Users must call HG_Addr_dup() to safely re-use the addr field.
 *
 * \param handle [IN]           HG handle
 *
 * \return Pointer to info or NULL in case of failure
 */
static HG_INLINE const struct hg_info *
HG_Get_info(hg_handle_t handle) HG_WARN_UNUSED_RESULT;

/**
 * Associate user data to handle. When HG_Destroy() is called,
 * free_callback (if defined) is called to free the associated data.
 *
 * \param handle [IN]           HG handle
 * \param data [IN]             pointer to user data
 * \param free_callback [IN]    pointer to function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Set_data(hg_handle_t handle, void *data, void (*free_callback)(void *));

/**
 * Retrieve previously associated data from a given handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return Pointer to user data or NULL if not set or any error has occurred
 */
static HG_INLINE void *
HG_Get_data(hg_handle_t handle) HG_WARN_UNUSED_RESULT;

/**
 * Retrieve input payload size from a given handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return Non-negative value or zero if no payload or the handle is not valid
 */
HG_PUBLIC hg_size_t
HG_Get_input_payload_size(hg_handle_t handle);

/**
 * Get input from handle (requires registration of input proc to deserialize
 * parameters). Input must be freed using HG_Free_input().
 *
 * \remark This is equivalent to:
 *   - HG_Core_get_input()
 *   - Call hg_proc to deserialize parameters
 *
 * \remark The input buffer may be released automatically after that call if the
 * release_input_early init info parameter has been set when initializing the
 * HG class.
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_input(hg_handle_t handle, void *in_struct);

/**
 * Free resources allocated when deserializing the input.
 * User may copy parameters contained in the input structure before calling
 * HG_Free_input().
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Free_input(hg_handle_t handle, void *in_struct);

/**
 * Retrieve output payload size from a given handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return Non-negative value or zero if no payload or the handle is not valid
 */
HG_PUBLIC hg_size_t
HG_Get_output_payload_size(hg_handle_t handle);

/**
 * Get output from handle (requires registration of output proc to deserialize
 * parameters). Output must be freed using HG_Free_output().
 *
 * \remark This is equivalent to:
 *   - HG_Core_get_output()
 *   - Call hg_proc to deserialize parameters
 *
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_output(hg_handle_t handle, void *out_struct);

/**
 * Free resources allocated when deserializing the output.
 * User may copy parameters contained in the output structure before calling
 * HG_Free_output().
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Free_output(hg_handle_t handle, void *out_struct);

/**
 * Get raw input buffer from handle that can be used for encoding and decoding
 * parameters.
 *
 * \remark Can be used for manual encoding / decoding when HG proc routines
 * cannot be automatically used or there is need for special handling before
 * HG_Get_input() can be called, for instance when using a custom header.
 * To use proc routines conjunctively, HG_Class_set_input_offset() can be used
 * to define the offset at which HG_Forward() / HG_Get_input() will start
 * encoding / decoding the input parameters.
 *
 * \remark in_buf_size argument will be ignored if NULL
 *
 * \param handle [IN]           HG handle
 * \param in_buf_p [OUT]        pointer to input buffer
 * \param in_buf_size_p [OUT]   pointer to input buffer size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_input_buf(hg_handle_t handle, void **in_buf_p, hg_size_t *in_buf_size_p);

/**
 * Release input buffer from handle so that it can be re-used early.
 *
 * \remark HG_Release_input_buf() may only be called when using
 * HG_Get_input_buf(). When using HG_Get_input(), the input buffer may
 * internally be released after HG_Get_input() is called. Note also that
 * if HG_Release_input_buf() is not called, the input buffer will later be
 * released when calling HG_Destroy().
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Release_input_buf(hg_handle_t handle);

/**
 * Get raw output buffer from handle that can be used for encoding and decoding
 * parameters.
 *
 * \remark Can be used for manual encoding / decoding when HG proc routines
 * cannot be automatically used or there is need for special handling before
 * HG_Get_output() can be called, for instance when using a custom header.
 * To use proc routines conjunctively, HG_Class_set_output_offset() can be used
 * to define the offset at which HG_Respond() / HG_Get_output() will start
 * encoding / decoding the output parameters.
 *
 * \remark out_buf_size argument will be ignored if NULL
 *
 * \param handle [IN]           HG handle
 * \param out_buf_p [OUT]       pointer to output buffer
 * \param out_buf_size_p [OUT]  pointer to output buffer size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_output_buf(
    hg_handle_t handle, void **out_buf_p, hg_size_t *out_buf_size_p);

/**
 * Get raw extra input buffer from handle that can be used for encoding and
 * decoding parameters. This buffer is only valid if the input payload is large
 * enough that it cannot fit into an eager buffer.
 *
 * \remark NULL pointer will be returned if there is no associated buffer.
 *
 * \remark in_buf_size argument will be ignored if NULL.
 *
 * \param handle [IN]           HG handle
 * \param in_buf_p [OUT]        pointer to input buffer
 * \param in_buf_size_p [OUT]   pointer to input buffer size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_input_extra_buf(
    hg_handle_t handle, void **in_buf_p, hg_size_t *in_buf_size_p);

/**
 * Get raw extra output buffer from handle that can be used for encoding and
 * decoding parameters. This buffer is only valid if the output payload is large
 * enough that it cannot fit into an eager buffer.
 *
 * \remark NULL pointer will be returned if there is no associated buffer.
 *
 * \remark out_buf_size argument will be ignored if NULL.
 *
 * \param handle [IN]           HG handle
 * \param out_buf_p [OUT]       pointer to output buffer
 * \param out_buf_size_p [OUT]  pointer to output buffer size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Get_output_extra_buf(
    hg_handle_t handle, void **out_buf_p, hg_size_t *out_buf_size_p);

/**
 * Set target context ID that will receive and process the RPC request
 * (ID is defined on target context creation, see HG_Context_create_id()).
 *
 * \param handle [IN]           HG handle
 * \param id [IN]               user-defined target context ID
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Set_target_id(hg_handle_t handle, uint8_t id);

/**
 * Forward a call to a local/remote target using an existing HG handle.
 * Input structure can be passed and parameters serialized using a previously
 * registered input proc. After completion, user callback is placed into a
 * completion queue and can be triggered using HG_Trigger(). RPC output can
 * be queried using HG_Get_output() and freed using HG_Free_output().
 *
 * \remark This routine is internally equivalent to:
 *   - HG_Core_get_input()
 *   - Call hg_proc to serialize parameters
 *   - HG_Core_forward()
 *
 * \param handle [IN]           HG handle
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param in_struct [IN]        pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Forward(hg_handle_t handle, hg_cb_t callback, void *arg, void *in_struct);

/**
 * Respond back to origin using an existing HG handle.
 * Output structure can be passed and parameters serialized using a previously
 * registered output proc. After completion, user callback is placed into a
 * completion queue and can be triggered using HG_Trigger().
 *
 * \remark This routine is internally equivalent to:
 *   - HG_Core_get_output()
 *   - Call hg_proc to serialize parameters
 *   - HG_Core_respond()
 *
 * \param handle [IN]           HG handle
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param out_struct [IN]       pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Respond(hg_handle_t handle, hg_cb_t callback, void *arg, void *out_struct);

/**
 * Cancel an ongoing operation.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or HG_CANCEL_ERROR or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Cancel(hg_handle_t handle);

/**
 * (Deprecated in favor of HG_Event_progress())
 * Try to progress RPC execution for at most timeout until timeout is reached or
 * any completion has occurred.
 * Progress should not be considered as wait, in the sense that it cannot be
 * assumed that completion of a specific operation will occur only when
 * progress is called.
 *
 * \param context [IN]          pointer to HG context
 * \param timeout [IN]          timeout (in milliseconds)
 *
 * \return HG_SUCCESS if any completion has occurred / HG error code otherwise
 */
HG_PUBLIC hg_return_t
HG_Progress(hg_context_t *context, unsigned int timeout);

/**
 * (Deprecated in favor of HG_Event_trigger())
 * Execute at most max_count callbacks. If timeout is non-zero, wait up to
 * timeout before returning. Function can return when at least one or more
 * callbacks are triggered (at most max_count).
 *
 * \param context [IN]          pointer to HG context
 * \param timeout [IN]          timeout (in milliseconds)
 * \param max_count [IN]        maximum number of callbacks triggered
 * \param actual_count_p [OUT]  actual number of callbacks triggered
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
HG_Trigger(hg_context_t *context, unsigned int timeout, unsigned int max_count,
    unsigned int *actual_count_p);

/**
 * Retrieve file descriptor from internal wait object when supported.
 * The descriptor can be used by upper layers for manual polling through the
 * usual OS select/poll/epoll calls.
 *
 * \param context [IN]          pointer to HG context
 *
 * \return Non-negative integer if supported and negative if not supported
 */
static HG_INLINE int
HG_Event_get_wait_fd(const hg_context_t *context) HG_WARN_UNUSED_RESULT;

/**
 * Used to signal when it is safe to block on the file descriptor of the
 * context's wait object or if there is already work that can be progressed.
 * Calling HG_Event_ready() is mandatory before any call to select/poll/epoll
 * (or equivalent) or the callee may not be signaled during these calls.
 *
 * \param context [IN/OUT]      pointer to HG context
 *
 * \return true if there is already work to be progressed or false otherwise
 */
static HG_INLINE bool
HG_Event_ready(hg_context_t *context) HG_WARN_UNUSED_RESULT;

/**
 * Progress communication by placing any completed RPC events into the
 * context's completion queue. Completed operations's callbacks can be triggered
 * by a call to HG_Event_trigger().
 *
 * \param context [IN/OUT]      pointer to HG context
 * \param count_p [OUT]         number of entries in context completion queue
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Event_progress(hg_context_t *context, unsigned int *count_p);

/**
 * Execute at most max_count callbacks.
 *
 * \param context [IN]          pointer to HG context
 * \param max_count [IN]        maximum number of callbacks triggered
 * \param actual_count_p [OUT]  actual number of callbacks triggered
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
static HG_INLINE hg_return_t
HG_Event_trigger(hg_context_t *context, unsigned int max_count,
    unsigned int *actual_count_p);

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* HG class */
struct hg_class {
    hg_core_class_t *core_class; /* Core class */
    hg_size_t in_offset;         /* Input offset */
    hg_size_t out_offset;        /* Output offset */
};

/* HG context */
struct hg_context {
    hg_core_context_t *core_context; /* Core context */
    hg_class_t *hg_class;            /* HG class */
};

/* HG handle */
struct hg_handle {
    struct hg_info info;                /* HG info */
    hg_core_handle_t core_handle;       /* Core handle */
    void *data;                         /* User data */
    void (*data_free_callback)(void *); /* User data free callback */
};

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Get_na_protocol_info(
    const char *info_string, struct na_protocol_info **na_protocol_info_p)
{
    return HG_Core_get_na_protocol_info(info_string, na_protocol_info_p);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
HG_Free_na_protocol_info(struct na_protocol_info *na_protocol_info)
{
    HG_Core_free_na_protocol_info(na_protocol_info);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE const char *
HG_Class_get_name(const hg_class_t *hg_class)
{
    return HG_Core_class_get_name(hg_class->core_class);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE const char *
HG_Class_get_protocol(const hg_class_t *hg_class)
{
    return HG_Core_class_get_protocol(hg_class->core_class);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE bool
HG_Class_is_listening(const hg_class_t *hg_class)
{
    return HG_Core_class_is_listening(hg_class->core_class);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_size_t
HG_Class_get_input_eager_size(const hg_class_t *hg_class)
{
    hg_size_t core = HG_Core_class_get_input_eager_size(hg_class->core_class),
              header = hg_header_get_size(HG_INPUT);

    return (core > header) ? core - header : 0;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_size_t
HG_Class_get_output_eager_size(const hg_class_t *hg_class)
{
    hg_size_t core = HG_Core_class_get_output_eager_size(hg_class->core_class),
              header = hg_header_get_size(HG_OUTPUT);

    return (core > header) ? core - header : 0;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Class_set_input_offset(hg_class_t *hg_class, hg_size_t offset)
{
    /* Extra input header must not be larger than eager size */
    if (offset > HG_Class_get_input_eager_size(hg_class))
        return HG_INVALID_ARG;

    hg_class->in_offset = offset;

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Class_set_output_offset(hg_class_t *hg_class, hg_size_t offset)
{
    /* Extra output header must not be larger than eager size */
    if (offset > HG_Class_get_output_eager_size(hg_class))
        return HG_INVALID_ARG;

    hg_class->out_offset = offset;

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Class_set_data(
    hg_class_t *hg_class, void *data, void (*free_callback)(void *))
{
    return HG_Core_class_set_data(hg_class->core_class, data, free_callback);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void *
HG_Class_get_data(const hg_class_t *hg_class)
{
    return HG_Core_class_get_data(hg_class->core_class);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_class_t *
HG_Context_get_class(const hg_context_t *context)
{
    return context->hg_class;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE uint8_t
HG_Context_get_id(const hg_context_t *context)
{
    return HG_Core_context_get_id(context->core_context);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Context_set_data(
    hg_context_t *context, void *data, void (*free_callback)(void *))
{
    return HG_Core_context_set_data(context->core_context, data, free_callback);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void *
HG_Context_get_data(const hg_context_t *context)
{
    return HG_Core_context_get_data(context->core_context);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Ref_incr(hg_handle_t handle)
{
    return HG_Core_ref_incr(handle->core_handle);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE int32_t
HG_Ref_get(hg_handle_t handle)
{
    return HG_Core_ref_get(handle->core_handle);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE const struct hg_info *
HG_Get_info(hg_handle_t handle)
{
    return &handle->info;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Set_data(hg_handle_t handle, void *data, void (*free_callback)(void *))
{
    handle->data = data;
    handle->data_free_callback = free_callback;

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void *
HG_Get_data(hg_handle_t handle)
{
    return handle->data;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Set_target_id(hg_handle_t handle, uint8_t id)
{
    handle->info.context_id = id;

    return HG_Core_set_target_id(handle->core_handle, id);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE int
HG_Event_get_wait_fd(const hg_context_t *context)
{
    return HG_Core_event_get_wait_fd(context->core_context);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE bool
HG_Event_ready(hg_context_t *context)
{
    return HG_Core_event_ready(context->core_context);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Event_progress(hg_context_t *context, unsigned int *count_p)
{
    return HG_Core_event_progress(context->core_context, count_p);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
HG_Event_trigger(
    hg_context_t *context, unsigned int max_count, unsigned int *actual_count_p)
{
    return HG_Core_event_trigger(
        context->core_context, max_count, actual_count_p);
}

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_H */
