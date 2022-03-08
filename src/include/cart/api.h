/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 *
 * CaRT (Collective and RPC Transport) API. All functions in this API can be
 * called on both the server side and client side unless stated otherwise.
 */

#ifndef __CRT_API_H__
#define __CRT_API_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <uuid/uuid.h>

#include <cart/types.h>
#include <daos_errno.h>
#include <cart/iv.h>
#include <cart/swim.h>

#if defined(__cplusplus)
extern "C" {
#endif

#include <boost/preprocessor.hpp>

/** @defgroup CART CART API */

/** @addtogroup CART
 * @{
 */

/**
 * Initialize CRT transport layer. Must be called on both the server side and
 * the client side. This function is reference counted, it can be called
 * multiple times. Each call must be paired with a corresponding crt_finalize().
 *
 * \param[in] grpid            primary group ID, user can provide a NULL value
 *                             in that case will use the default group ID
 *                             CRT_DEFAULT_GRPID.
 * \param[in] flags            bit flags, see \ref crt_init_flag_bits.
 * \param[in] opt              additional init time options. If a NULL value
 *                             is provided, this call becomes identical to
 *                             crt_init().
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_init_opt(crt_group_id_t grpid, uint32_t flags, crt_init_options_t *opt);

/**
 * Initialize CRT transport layer. Must be called on both the server side and
 * the client side. This function is reference counted, it can be called
 * multiple times. Each call must be paired with a corresponding crt_finalize().
 *
 * \param[in] grpid            primary group ID, user can provide a NULL value
 *                             in that case will use the default group ID
 *                             CRT_DEFAULT_GRPID.
 * \param[in] flags            bit flags, see \ref crt_init_flag_bits.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
static inline int
crt_init(crt_group_id_t grpid, uint32_t flags)
{
	return crt_init_opt(grpid, flags, NULL);
}

/**
 * Create CRT transport context. Must be destroyed by crt_context_destroy()
 * before calling crt_finalize().
 *
 * \param[out] crt_ctx         created CRT transport context
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_context_create(crt_context_t *crt_ctx);

/**
 * Set the timeout value for all RPC requests created on the specified context.
 * Setting the timeout after crt_req_create() call will not affect already
 * created rpcs.
 *
 * This is an optional function.
 *
 * The precedence order of timeouts:
 * - crt_req_set_timeout()
 * - crt_context_set_timeout()
 * - CRT_TIMEOUT environment variable
 *
 * \param[in] crt_ctx          CaRT context
 * \param[in] timeout_sec      timeout value in seconds
 *                             value of zero will be treated as invalid
 *                             parameter.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_context_set_timeout(crt_context_t crt_ctx, uint32_t timeout_sec);

/**
 * Destroy CRT transport context.
 *
 * \param[in] crt_ctx          CRT transport context to be destroyed
 * \param[in] force             1) force == 0
 *                                 return as -EBUSY if there is any in-flight
 *                                 RPC request, so caller can wait its
 *                                 completion or timeout.
 *                              2) force != 0
 *                                 will cancel all in-flight RPC requests.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 * \note Currently there is no in-flight list/queue in mercury.
 */
int
crt_context_destroy(crt_context_t crt_ctx, int force);

/**
 * check if the endpoint associated with \a crt_ctx is empty i.e. has no pending
 * RPCs
 *
 * \param[in] crt_ctx           CRT transport context to check
 *
 * \return                      true if \a crt_ctx is empty, false if \a crt_ctx
 *                              is not empty
 */
bool
crt_context_ep_empty(crt_context_t crt_ctx);

/**
 * Flush pending RPCs associated with the specified context.
 *
 * \param[in] crt_ctx           CRT transport context to flush
 * \param[in] timeout           max time duration (in seconds) to try to flush.
 *                              0 means infinite timeout. After \a timeout
 *                              amount of time, this function will return even
 *                              if there are still RPCs pending.
 *
 * \return                      DER_SUCCESS if there are no more pending RPCs,
 *                              -DER_TIMEDOUT if time out is reached before all
 *                              RPCs are processed, other negative value on
 *                              error
 */
int
crt_context_flush(crt_context_t crt_ctx, uint64_t timeout);

/**
 * Query the index of the transport context, the index value ranges in
 * [0, ctx_num - 1].
 *
 * \param[in] crt_ctx          CRT transport context
 * \param[in] ctx_idx          pointer to the returned index
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_context_idx(crt_context_t crt_ctx, int *ctx_idx);

/**
 * Query the total number of the transport contexts.
 *
 * \param[out] ctx_num         pointer to the returned number
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_context_num(int *ctx_num);

/**
 * Finalize CRT transport layer. Must be called on both the server side and
 * client side before exit. This function is reference counted.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 */
int
crt_finalize(void);

/**
 * Progress RPC execution on a cart context \a crt_ctx for at most \a timeout
 * micro-seconds.
 * The progress call returns when the timeout is reached or any completion has
 * occurred.
 *
 * \param[in] crt_ctx          cart context
 * \param[in] timeout          how long is caller going to wait (micro-second)
 *                             at most for a completion to occur.
 *                             Can return when one or more operation progressed.
 *                             zero means no waiting.
 *
 * \return                     DER_SUCCESS on success
 *                             -DER_TIMEDOUT if exited after timeout has expired
 *                             negative value if other internal error
 */
int
crt_progress(crt_context_t crt_ctx, int64_t timeout);

/**
 * Progress RPC execution on a cart context with a callback function.
 * The callback function is regularly called internally. The progress call
 * returns when the callback returns a non-zero value or when the timeout
 * expires.
 *
 * \param[in] crt_ctx          cart context
 * \param[in] timeout          how long is the caller going to wait in
 *                             micro-second.
 *                             zero means no waiting and -1 waits indefinitely.
 * \param[in] cond_cb          progress condition callback.
 *                             cart internally calls this function, when it
 *                             returns non-zero then stops the progressing or
 *                             waiting and returns.
 * \param[in] arg              optional argument to cond_cb.
 *
 * \return                     DER_SUCCESS on success
 *                             -DER_TIMEDOUT if exited after timeout has expired
 *                             negative value if internal and \a conb_cb error
 */
int
crt_progress_cond(crt_context_t crt_ctx, int64_t timeout,
		  crt_progress_cond_cb_t cond_cb, void *arg);

/**
 * Create an RPC request.
 *
 * \param[in] crt_ctx          CRT transport context
 * \param[in] tgt_ep           RPC target endpoint
 * \param[in] opc              RPC request opcode
 * \param[out] req             pointer to created request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 * \note the crt_req_create will internally allocate zeroed buffers for input
 *        and output parameters (crt_rpc_t::cr_input/cr_output), and set the
 *        appropriate size (crt_rpc_t::cr_input_size/cr_output_size).
 *        User needs not to allocate extra input/output buffers. After the
 *        request created, user can directly fill input parameters into
 *        crt_rpc_t::cr_input and send the RPC request.
 *        When the RPC request finishes executing, CRT internally frees the
 *        RPC request and the input/output buffers, so user needs not to call
 *        crt_req_destroy (no such function exported) or free the
 *        input/output buffers.
 *        Similarly, on the RPC server-side, when an RPC request received, CRT
 *        internally allocates input/output buffers as well, and internally
 *        frees those buffers when the reply is sent out. So in user's RPC
 *        handler it needs not to allocate extra input/output buffers, and also
 *        needs not to free input/output buffers in the completion callback of
 *        crt_reply_send.
 *        tgt_ep may be NULL, in which case crt_req_set_endpoint() must be
 *        called for this req before crt_req_send().
 */
int
crt_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req);

/**
 * Set the endpoint for an RPC request.
 *
 * This is an optional function, it must be called before req_send() if an
 * endpoint was not provided to crt_req_create() however it will fail if there
 * is already an endpoint associated with the request.
 *
 * \param[in] req              pointer to RPC request
 * \param[in] tgt_ep           RPC target endpoint
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_req_set_endpoint(crt_rpc_t *req, crt_endpoint_t *tgt_ep);

/**
 * Set the timeout value for an RPC request.
 *
 * It is an optional function. If user does not call it, then will depend on
 * CRT_TIMEOUT ENV as timeout value (see the CRT_TIMEOUT section in
 * README.env). User can also explicitly set one RPC request's timeout value
 * by calling this function.
 *
 * \param[in] req              pointer to RPC request
 * \param[in] timeout_sec      timeout value in seconds. value of zero will be
 *                             treated as invalid parameter.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_req_set_timeout(crt_rpc_t *req, uint32_t timeout_sec);

/**
 * Get the timeout value of an RPC request.
 *
 * \param[in] req              pointer to RPC request
 * \param[out] timeout_sec     timeout value in seconds
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_req_get_timeout(crt_rpc_t *req, uint32_t *timeout_sec);

/**
 * Add reference of the RPC request.
 *
 * The typical usage is that user needs to do some asynchronous operations in
 * RPC handler and does not want to block in RPC handler, then it can call this
 * function to hold a reference and return. Later when that asynchronous
 * operation is done, it can release the reference (See \ref crt_req_decref).
 * CRT internally frees the resource of the RPC request when its reference
 * drops to zero.
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_req_addref(crt_rpc_t *req);

/**
 * Decrease reference of the RPC request. See \ref crt_req_addref.
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_req_decref(crt_rpc_t *req);

/**
 * Send an RPC request. In the case of sending failure, CRT internally destroy
 * the request \a req. In the case of succeed, the \a req will be internally
 * destroyed when the reply received. User needs not call crt_req_decref() to
 * destroy the request in either case.
 *
 * \param[in] req              pointer to RPC request
 * \param[in] complete_cb      optional completion callback, when it is
 *                             provided the completion result (success or
 *                             failure) will be reported by calling it in the
 *                             context of user's calling of crt_progress() or
 *                             crt_req_send().
 * \param[in] arg              arguments for the \a complete_cb
 *
 * \return                     if \a complete_cb provided (non-NULL), always
 *                             returns zero; otherwise returns DER_SUCCESS on
 *                             success, negative value if error.
 *
 * \note the crt_rpc_t is exported to user, caller should fill the
 *        crt_rpc_t::cr_input and before sending the RPC request.
 *        See \ref crt_req_create.
 */
int
crt_req_send(crt_rpc_t *req, crt_cb_t complete_cb, void *arg);

/**
 * Send an RPC reply. Only to be called on the server side.
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 * \note the crt_rpc_t is exported to user, caller should fill the
 *        crt_rpc_t::cr_output before sending the RPC reply.
 *        See \ref crt_req_create.
 */
int
crt_reply_send(crt_rpc_t *req);

/**
 * Return request buffer
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     pointer to request buffer
 */
static inline void *
crt_req_get(crt_rpc_t *rpc)
{
	return rpc->cr_input;
}

/**
 * Return originator/source rank
 *
 * \param[in] req              Pointer to RPC request
 * \param[out] rank            Returned rank
 *
 * \return                     DER_SUCCESS on success or error
 *                             on failure
 */
int
crt_req_src_rank_get(crt_rpc_t *req, d_rank_t *rank);

/**
 * Return destination rank
 *
 * \param[in] req              Pointer to RPC request
 * \param[out] rank            Returned rank
 *
 * \return                     DER_SUCCESS on success or error
 *                             on failure
 */
int
crt_req_dst_rank_get(crt_rpc_t *req, d_rank_t *rank);

/**
 * Return destination tag
 *
 * \param[in] req              Pointer to RPC request
 * \param[out] tag             Returned tag
 *
 * \return                     DER_SUCCESS on success or error
 *                             on failure
 */
int
crt_req_dst_tag_get(crt_rpc_t *req, uint32_t *tag);

/**
 * Return reply buffer
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     pointer to reply buffer
 */
static inline void *
crt_reply_get(crt_rpc_t *rpc)
{
	return rpc->cr_output;
}

/**
 * Return current HLC timestamp
 *
 * HLC timestamps are synchronized between nodes. They sends with each RPC for
 * different nodes and updated when received from different node. The HLC
 * timestamps synchronization will be called transparently at sending/receiving
 * RPC into the wire (when Mercury will encode/decode the packet). So, with
 * each call of this function you will get from it always last HLC timestamp
 * synchronized across all nodes involved in current communication.
 *
 * \return                     HLC timestamp
 */
uint64_t
crt_hlc_get(void);

/**
 * Sync HLC with remote message and get current HLC timestamp.
 *
 * \param[in] msg              remote HLC timestamp
 * \param[out] hlc_out         HLC timestamp
 * \param[out] offset          Returned observed clock offset.
 *
 * \return                     DER_SUCCESS on success or error
 *                             on failure
 * \retval -DER_HLC_SYNC       \a msg is too much higher than the local
 *                             physical clock
 */
int
crt_hlc_get_msg(uint64_t msg, uint64_t *hlc_out, uint64_t *offset);

/**
 * Return the nanosecond timestamp of hlc.
 *
 * \param[in] hlc              HLC timestamp
 *
 * \return                     Nanosecond timestamp
 */
uint64_t
crt_hlc2nsec(uint64_t hlc);

/** See crt_hlc2nsec. */
static inline uint64_t
crt_hlc2usec(uint64_t hlc)
{
	return crt_hlc2nsec(hlc) / 1000;
}

/** See crt_hlc2nsec. */
static inline uint64_t
crt_hlc2msec(uint64_t hlc)
{
	return crt_hlc2nsec(hlc) / (1000 * 1000);
}

/** See crt_hlc2nsec. */
static inline uint64_t
crt_hlc2sec(uint64_t hlc)
{
	return crt_hlc2nsec(hlc) / (1000 * 1000 * 1000);
}

/**
 * Return the HLC timestamp from nsec.
 *
 * \param[in] nsec             Nanosecond timestamp
 *
 * \return                     HLC timestamp
 */
uint64_t
crt_nsec2hlc(uint64_t nsec);

/** See crt_nsec2hlc. */
static inline uint64_t
crt_usec2hlc(uint64_t usec)
{
	return crt_nsec2hlc(usec * 1000);
}

/** See crt_nsec2hlc. */
static inline uint64_t
crt_msec2hlc(uint64_t msec)
{
	return crt_nsec2hlc(msec * 1000 * 1000);
}

/** See crt_nsec2hlc. */
static inline uint64_t
crt_sec2hlc(uint64_t sec)
{
	return crt_nsec2hlc(sec * 1000 * 1000 * 1000);
}

/**
 * Return the Unix nanosecond timestamp of hlc.
 *
 * \param[in] hlc              HLC timestamp
 *
 * \return                     Unix nanosecond timestamp
 */
uint64_t
crt_hlc2unixnsec(uint64_t hlc);

/**
 * Return the HLC timestamp of unixnsec in hlc.
 *
 * \param[in] unixnsec         Unix nanosecond timestamp
 *
 * \return                     HLC timestamp on success, or 0 when it is
 *                             impossible to convert unixnsec to hlc
 */
uint64_t
crt_unixnsec2hlc(uint64_t unixnsec);

/**
 * Set the maximum system clock offset.
 *
 * This is the maximum offset believed to be observable between the physical
 * clocks behind any two HLCs in the system. The format of the value represent
 * a nonnegative diff between two HLC timestamps. The value is rounded up to
 * the HLC physical resolution.
 *
 * \param[in] epsilon          Nonnegative HLC duration
 */
void
crt_hlc_epsilon_set(uint64_t epsilon);

/**
 * Get the maximum system clock offset. See crt_hlc_set_epsilon's API doc.
 *
 * \return                     Nonnegative HLC duration
 */
uint64_t
crt_hlc_epsilon_get(void);

/**
 * Get the upper bound of the HLC timestamp of an event happened before
 * (through out of band communication) the event at \a hlc.
 *
 * \param[in] hlc              HLC timestamp
 *
 * \return                     Upper bound HLC timestamp
 */
uint64_t
crt_hlc_epsilon_get_bound(uint64_t hlc);

/**
 * Abort an RPC request.
 *
 * \param[in] req              pointer to RPC request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *                             If the RPC has been sent out by crt_req_send,
 *                             the completion callback will be called with
 *                             DER_CANCELED set to crt_cb_info::cci_rc for a
 *                             successful aborting.
 */
int
crt_req_abort(crt_rpc_t *req);

/**
 * Abort all in-flight RPC requests targeting rank
 *
 * \param[in] rank             rank to cancel
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_rank_abort(d_rank_t rank);

/**
 * Abort all in-flight RPCs to all ranks in the group.
 *
 * \param[in] grp              group to cancel in. NULL for default group.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_rank_abort_all(crt_group_t *grp);

/**
 * DEPRECATED:
 *
 * Abort all in-flight RPC requests targeting to an endpoint.
 *
 * \param[in] ep               endpoint address
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_ep_abort(crt_endpoint_t *ep);

/**
 * CART provides a set of macros for RPC registration. Using the macro interface
 * to register RPCs is much simpler and reduces the opportunities for mistakes.
 *
 * public macros:
 *
 *     preparation:
 *         - CRT_RPC_DECLARE()
 *         - CRT_RPC_DEFINE()
 *
 *     registration:
 *         - CRT_RPC_REGISTER()
 *         - CRT_RPC_SRV_REGISTER()
 *
 * To register an RPC using macros:
 *     CRT_RPC_DECLARE(my_rpc_name, input_fields, output_fields)
 *     CRT_RPC_DEFINE(my_rpc_name, input_fields, output_fields)
 *     CRT_RPC_REGISTER(opcode, flags, my_rpc_name);
 *
 * The input/output structs can be accessed using the following pointers:
 *     struct my_rpc_name_in *rpc_in;
 *     struct my_rpc_name_out *rpc_out;
 */
/**
 * Prepare struct types and format description for the input/output of an RPC.
 * Supported types in the fields_in/fields_out list can be found in
 * include/cart/types.h
 *
 * Example usage:
 *
 * \#define CRT_ISEQ_MY_RPC
 *     ((int32_t)       (mr_arg_1)     CRT_VAR)
 *     ((uint32_t)      (mr_arg_2)     CRT_VAR)
 *     ((d_rank_t)      (mr_rank)      CRT_VAR)
 *     ((d_rank_list_t) (mr_rank_list) CRT_PTR)
 *     ((uuid_t)        (mr_array)     CRT_ARRAY)
 *     ((d_string_t)    (mr_name)      CRT_VAR)
 *
 * \#define CRT_OSEQ_MY_RPC
 *     ((int32_t)       (mr_ret)       CRT_VAR)
 *
 * CRT_RPC_DECLARE(my_rpc, CRT_ISEQ_MY_RPC, CRT_OSEQ_MY_RPC)
 * CRT_RPC_REGISTER(opcode, flags, my_rpc);
 *
 * these two macros above expands into:
 *
 * struct my_rpc_in {
 *     int32_t           mr_arg_1;
 *     uint32_t          mr_arg_2;
 *     d_rank_t          mr_rank;
 *     d_rank_list_t    *mr_rank_list;
 *     struct crt_array  mr_array;
 *     d_string_t        mr_name;
 * };
 *
 * struct my_rpc_out {
 *     int32_t           mr_ret;
 * };
 *
 * crt_register(opcode, flags, &CQF_my_rpc);
 *
 * the macros CRT_RPC_DEFINE(my_rpc, CRT_ISEQ_MY_RPC, CRT_OSEQ_MY_RPC) expands
 * into internal RPC definition which will be used in RPC registration.
 * The content of this macro expansion will be changed in the future.
 *
 * To use array types it's possible to define types as above, and then use the
 * same macros to declare types and proc structs for types, and then reference
 * the type directly in the RPC definition.
 *
 * CRT_GEN_STRUCT(struct, CRT_SEQ_MY_TYPE)
 # CRT_GEN_PROC_FUNC(struct, CRT_SEQ_MY_TYPE)
 *
 */
#define CRT_VAR		(0)
#define CRT_PTR		(1)
#define CRT_ARRAY	(2)
#define CRT_RAW		(3)

#define CRT_GEN_GET_TYPE(seq) BOOST_PP_SEQ_ELEM(0, seq)
#define CRT_GEN_GET_NAME(seq) BOOST_PP_SEQ_ELEM(1, seq)
#define CRT_GEN_GET_KIND(seq) BOOST_PP_SEQ_ELEM(2, seq)

/* convert constructed name into proper name */
#define crt_proc_struct BOOST_PP_RPAREN() BOOST_PP_CAT BOOST_PP_LPAREN() \
	crt_proc_struct_,

#define CRT_GEN_X(x) x
#define CRT_GEN_X2(x) CRT_GEN_X BOOST_PP_LPAREN() crt_proc_##x BOOST_PP_RPAREN()
#define CRT_GEN_GET_FUNC(seq) CRT_GEN_X2 BOOST_PP_SEQ_FIRST_N(1, seq)
#define CRT_GEN_X3(x) CRT_GEN_X BOOST_PP_LPAREN() fail_##x BOOST_PP_RPAREN()
#define CRT_GEN_FAIL_LABEL(seq) CRT_GEN_X3 BOOST_PP_SEQ_TAIL(BOOST_PP_SEQ_FIRST_N(2, seq))

#define CRT_GEN_STRUCT_FIELD(seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		struct {						\
			uint64_t		 ca_count;		\
			CRT_GEN_GET_TYPE(seq)	*ca_arrays;		\
		},							\
		CRT_GEN_GET_TYPE(seq))					\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_PTR, CRT_GEN_GET_KIND(seq)), \
		*CRT_GEN_GET_NAME(seq), CRT_GEN_GET_NAME(seq));)

#define CRT_GEN_STRUCT_FIELDS(z, n, seq)				\
	CRT_GEN_STRUCT_FIELD(BOOST_PP_SEQ_ELEM(n, seq))

#define CRT_GEN_STRUCT(struct_type_name, seq)				\
	struct struct_type_name {					\
		BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),			\
				CRT_GEN_STRUCT_FIELDS, seq)		\
	};

#define CRT_GEN_PROC_ARRAY(ptr, seq)					\
	do {								\
	CRT_GEN_GET_TYPE(seq) **e_ptrp = &ptr->CRT_GEN_GET_NAME(seq).ca_arrays;\
	CRT_GEN_GET_TYPE(seq) *e_ptr = ptr->CRT_GEN_GET_NAME(seq).ca_arrays; \
	uint64_t count = ptr->CRT_GEN_GET_NAME(seq).ca_count;		\
	uint64_t i;							\
	if (proc_op == CRT_PROC_DECODE)					\
		*e_ptrp = NULL;						\
	/* process the count of array first */				\
	rc = crt_proc_uint64_t(proc, proc_op, &count);			\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);				\
	ptr->CRT_GEN_GET_NAME(seq).ca_count = count;			\
	if (unlikely(count == 0))					\
		break; /* goto next field */				\
	if (proc_op == CRT_PROC_DECODE) {				\
		D_ALLOC_ARRAY(e_ptr, (int)count);			\
		if (unlikely(e_ptr == NULL)) {				\
			rc = -DER_NOMEM;				\
			goto CRT_GEN_FAIL_LABEL(seq);			\
		}							\
		*e_ptrp = e_ptr;					\
	}								\
	/* process the elements of array */				\
	for (i = 0; i < count && e_ptr != NULL; i++) {			\
		rc = CRT_GEN_GET_FUNC(seq)(proc, proc_op, &e_ptr[i]);	\
		if (unlikely(rc)) {					\
			if (proc_op == CRT_PROC_DECODE)			\
				ptr->CRT_GEN_GET_NAME(seq).ca_count = i;\
			goto CRT_GEN_FAIL_LABEL(seq);			\
		}							\
	}								\
	if (proc_op == CRT_PROC_FREE)					\
		D_FREE(*e_ptrp);					\
	} while (0);

#define CRT_GEN_FAIL_ARRAY(ptr, seq)					\
	CRT_GEN_FAIL_LABEL(seq):					\
	if (proc_op == CRT_PROC_DECODE) {				\
		CRT_GEN_GET_TYPE(seq) **e_ptrp = &ptr->CRT_GEN_GET_NAME(seq).ca_arrays;\
		CRT_GEN_GET_TYPE(seq) *e_ptr = ptr->CRT_GEN_GET_NAME(seq).ca_arrays; \
		uint64_t count = ptr->CRT_GEN_GET_NAME(seq).ca_count;	\
		uint64_t i;						\
		/* process the elements of array */			\
		for (i = 0; i < count && e_ptr != NULL; i++)		\
			(void)CRT_GEN_GET_FUNC(seq)(proc, CRT_PROC_FREE,\
						    &e_ptr[i]);		\
		D_FREE(*e_ptrp);					\
		ptr->CRT_GEN_GET_NAME(seq).ca_count = 0;		\
	}

#define CRT_GEN_PROC_RAW(ptr, seq)					\
	rc = crt_proc_memcpy(proc, proc_op, &ptr->CRT_GEN_GET_NAME(seq),\
			     sizeof(CRT_GEN_GET_TYPE(seq)));		\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);

#define CRT_GEN_FAIL_RAW(ptr, seq)					\
	CRT_GEN_FAIL_LABEL(seq):;

#define CRT_GEN_PROC_ALL(ptr, seq)					\
	rc = CRT_GEN_GET_FUNC(seq)(proc, proc_op,			\
				   &ptr->CRT_GEN_GET_NAME(seq));	\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);

#define CRT_GEN_FAIL_ALL(ptr, seq)					\
	(void)CRT_GEN_GET_FUNC(seq)(proc, CRT_PROC_FREE,		\
				    &ptr->CRT_GEN_GET_NAME(seq));	\
	CRT_GEN_FAIL_LABEL(seq):;

#define CRT_GEN_PROC_FIELD(ptr, seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		CRT_GEN_PROC_ARRAY(ptr, seq),				\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_RAW, CRT_GEN_GET_KIND(seq)), \
		CRT_GEN_PROC_RAW(ptr, seq),				\
		CRT_GEN_PROC_ALL(ptr, seq))))

#define CRT_GEN_FAIL_FIELD(ptr, seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		CRT_GEN_FAIL_ARRAY(ptr, seq),				\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_RAW, CRT_GEN_GET_KIND(seq)), \
		CRT_GEN_FAIL_RAW(ptr, seq),				\
		CRT_GEN_FAIL_ALL(ptr, seq))))

#define CRT_GEN_PROC_FIELDS(z, n, seq)					\
	CRT_GEN_PROC_FIELD(ptr, BOOST_PP_SEQ_ELEM(n, seq))

#define CRT_GEN_FAIL_FIELDS(z, n, seq)					\
	CRT_GEN_FAIL_FIELD(ptr, BOOST_PP_SEQ_ELEM(BOOST_PP_SUB(BOOST_PP_DEC(BOOST_PP_SEQ_SIZE(seq)), n), seq))

#define CRT_GEN_PROC_FUNC(type_name, seq)				\
	static int							\
	crt_proc_##type_name(crt_proc_t proc, struct type_name *ptr) {	\
		crt_proc_op_t proc_op;					\
		int rc;							\
		if (unlikely(proc == NULL || ptr == NULL))		\
			return -DER_INVAL;				\
		rc = crt_proc_get_op(proc, &proc_op);			\
		if (unlikely(rc))					\
			return rc;					\
		BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),			\
				CRT_GEN_PROC_FIELDS, seq)		\
		if (unlikely(rc)) {					\
			BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),		\
					CRT_GEN_FAIL_FIELDS, seq)	\
		}							\
		return rc;						\
	}

#define POP_BACK(seq) BOOST_PP_SEQ_HEAD(BOOST_PP_SEQ_REVERSE(seq))
#define FOFFSET(sname, seq)						\
	offsetof(struct sname, CRT_GEN_GET_NAME(POP_BACK(seq)))

#define CRT_RPC_DECLARE(rpc_name, fields_in, fields_out)		\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    CRT_GEN_STRUCT(rpc_name##_in, fields_in), )		\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    CRT_GEN_STRUCT(rpc_name##_out, fields_out), )	\
	/* Generate a packed struct and assert use the offset of the */	\
	/* last field to assert that there are no holes */		\
	_Pragma("pack(push, 1)")					\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		CRT_GEN_STRUCT(rpc_name##_in_packed, fields_in), )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		CRT_GEN_STRUCT(rpc_name##_out_packed, fields_out), )	\
	_Pragma("pack(pop)")						\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    static_assert(FOFFSET(rpc_name##_out_packed,	\
					  ((_) (_) (_)) fields_out) ==	\
				  FOFFSET(rpc_name##_out, ((_) (_) (_))	\
					  fields_out), #rpc_name	\
				  " output struct has a hole");, )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    static_assert(FOFFSET(rpc_name##_in_packed,		\
					  ((_) (_) (_)) fields_in) ==	\
				  FOFFSET(rpc_name##_in, ((_) (_) (_))	\
					  fields_in), #rpc_name		\
				  " input struct has a hole");, )	\
	extern struct crt_req_format CQF_##rpc_name;

/* warning was introduced in version 8 of GCC */
#if D_HAS_WARNING(8, "-Wsizeof-pointer-div")
#define CRT_DISABLE_SIZEOF_POINTER_DIV					\
	_Pragma("GCC diagnostic ignored \"-Wsizeof-pointer-div\"")
#else /* warning not available */
#define CRT_DISABLE_SIZEOF_POINTER_DIV
#endif /* warning is available */

#define CRT_RPC_DEFINE(rpc_name, fields_in, fields_out)			\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    CRT_GEN_PROC_FUNC(rpc_name##_in, fields_in), )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    CRT_GEN_PROC_FUNC(rpc_name##_out, fields_out), )	\
	_Pragma("GCC diagnostic push")					\
	CRT_DISABLE_SIZEOF_POINTER_DIV					\
	struct crt_req_format CQF_##rpc_name = {			\
		.crf_proc_in  = (crt_proc_cb_t)				\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),		\
			crt_proc_##rpc_name##_in, NULL),		\
		.crf_proc_out = (crt_proc_cb_t)				\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),		\
			crt_proc_##rpc_name##_out, NULL),		\
		.crf_size_in  =						\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),		\
			sizeof(struct rpc_name##_in), 0),		\
		.crf_size_out =						\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),		\
			sizeof(struct rpc_name##_out), 0)		\
	};								\
	_Pragma("GCC diagnostic pop")

#define CRT_RPC_CORPC_REGISTER(opcode, rpc_name, rpc_handler, co_ops)	\
	crt_corpc_register(opcode, &CQF_##rpc_name, rpc_handler, co_ops)

#define CRT_RPC_SRV_REGISTER(opcode, flags, rpc_name, rpc_handler)	\
	crt_rpc_srv_register(opcode, flags, &CQF_##rpc_name, rpc_handler)

#define CRT_RPC_REGISTER(opcode, flags, rpc_name)			\
	crt_rpc_register(opcode, flags, &CQF_##rpc_name)

/**
 * Dynamically register an RPC with features at client-side.
 *
 * \param[in] opc              unique opcode for the RPC
 * \param[in] flags            feature bits, now only supports
 *                             CRT_RPC_FEAT_NO_REPLY - disables reply when set,
 *                             re-enables reply when not set.
 *                             CRT_RPC_FEAT_NO_TIMEOUT - if it's set, the
 *                             elapsed time is reset to 0 on RPC timeout
 * \param[in] drf              pointer to the request format, which
 *                             describe the request format and provide
 *                             callback to pack/unpack each items in the
 *                             request.
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_rpc_register(crt_opcode_t opc, uint32_t flags, struct crt_req_format *drf);

/**
 * The RPC callback for the context, which will be called when the context
 * receives any RPC. In this callback, the handler can do sth specially for
 * the RPC on this context, for example create another ULT to handle it,
 * see DAOS.
 *
 * \param[in] ctx              The cart context.
 * \param[in] rpc_hdlr_arg     The argument of rpc_hdlr.
 * \param[in] rpc_hdlr         Real RPC handler.
 * \param[in] arg              Extra argument for the callback.
 *
 * \return                     0 for success, negative value if failed.
 *
 */
typedef int (*crt_rpc_task_t) (crt_context_t *ctx, void *rpc_hdlr_arg,
			       void (*rpc_hdlr)(void *), void *arg);
/**
 * Register RPC process callback for all RPCs this context received.
 * This callback enables the thread to modify how the rpc callbacks are
 * handled for this context. For example DAOS creates another argobot
 * ULT to handle it.
 *
 * \param[in] crt_ctx          The context to be registered.
 * \param[in] rpc_cb           The RPC process callback.
 * \param[in] iv_reps_cb       The IV response callback.
 * \param[in] arg              The argument for RPC process callback.
 *
 * \return                     DER_SUCCESS on success, negative value if error.
 */
int
crt_context_register_rpc_task(crt_context_t crt_ctx, crt_rpc_task_t rpc_cb,
			      crt_rpc_task_t iv_resp_cb, void *arg);

/**
 * Dynamically register an RPC with features at server-side.
 *
 * \param[in] opc              unique opcode for the RPC
 * \param[in] flags            feature bits, now only supports
 *                             \ref CRT_RPC_FEAT_NO_REPLY - disables reply when
 *                             set, re-enables reply when not set.
 *                             \ref CRT_RPC_FEAT_NO_TIMEOUT - if it's set, the
 *                             elapsed time is reset to 0 on RPC
 *                             timeout
 * \param[in] crf              pointer to the request format, which
 *                             describe the request format and provide
 *                             callback to pack/unpack each items in the
 *                             request.
 * \param[in] rpc_handler      pointer to RPC handler which will be triggered
 *                             when RPC request opcode associated with rpc_name
 *                             is received. Will return -DER_INVAL if pass in
 *                             NULL rpc_handler.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_rpc_srv_register(crt_opcode_t opc, uint32_t flags,
		     struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler);

/******************************************************************************
 * CRT bulk APIs.
 ******************************************************************************/

/**
 * Create a bulk handle
 *
 * \param[in] crt_ctx          CRT transport context
 * \param[in] sgl              pointer to buffer segment list
 * \param[in] bulk_perm        bulk permission, See \ref crt_bulk_perm_t
 * \param[out] bulk_hdl        created bulk handle
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_create(crt_context_t crt_ctx, d_sg_list_t *sgl,
		crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl);

/**
 * Bind bulk handle to local context, to associate the origin address of the
 * local context to the bulk handle.
 *
 * It can be used to forward/share the bulk handle from one server to another
 * server, in that case the origin address of the bulk handle can be serialized/
 * de-serialized on-the-fly. The example usage:
 * client sends a RPC request with a bulk handle embedded to server A,
 * server A forward the client-side bulk handle to another server B.
 * For that usage, client should call this API to bind the bulk handle with its
 * local context before sending the RPC to server A. So when server B gets the
 * de-serialized bulk handle forwarded by server A, the server B can know the
 * client-side origin address to do the bulk transferring.
 *
 * Users should note that binding a bulk handle adds an extra overhead on
 * serialization, therefore it is recommended to use it with care.
 * When binding a bulk handle on origin, crt_bulk_bind_transfer() should be
 * used since origin address information is embedded in the handle.
 *
 * \param[in] bulk_hdl		created bulk handle
 * \param[in] crt_ctx		CRT transport context
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_bind(crt_bulk_t bulk_hdl, crt_context_t crt_ctx);

/**
 * Add reference of the bulk handle.
 *
 * \param[in] bulk_hdl		bulk handle
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_addref(crt_bulk_t bulk_hdl);

/**
 * Access local bulk handle to retrieve the sgl (segment list) associated
 * with it.
 *
 * \param[in] bulk_hdl         bulk handle
 * \param[in,out] sgl          pointer to buffer segment list
 *                             Caller should provide a valid sgl pointer, if
 *                             sgl->sg_nr is too small, -DER_TRUNC will be
 *                             returned and the needed number of iovs be set at
 *                             sgl->sg_nr_out.
 *                             On success, sgl->sg_nr_out will be set as
 *                             the actual number of iovs.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_access(crt_bulk_t bulk_hdl, d_sg_list_t *sgl);

/**
 * Free a bulk handle
 *
 * \param[in] bulk_hdl         bulk handle to be freed
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_free(crt_bulk_t bulk_hdl);

/**
 * Start a bulk transferring (inside an RPC handler).
 *
 * \param[in] bulk_desc        pointer to bulk transferring descriptor
 *                             it is user's responsibility to allocate and free
 *                             it. Can free it after the calling returns.
 * \param[in] complete_cb      completion callback
 * \param[in] arg              arguments for the \a complete_cb
 * \param[out] opid            returned bulk opid which can be used to abort
 *                             the bulk. It is optional, can pass in NULL if
 *                             don't need it.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_transfer(struct crt_bulk_desc *bulk_desc, crt_bulk_cb_t complete_cb,
		  void *arg, crt_bulk_opid_t *opid);

/**
 * Start a bulk transferring by using the remote bulk handle bound address
 * rather than the RPC's origin address. It can be used for the case that the
 * origin address of bulk handle is different with RPC request, for example
 * DAOS' bulk handle forwarding for server-side I/O dispatching.
 *
 * \param[in] bulk_desc        pointer to bulk transferring descriptor
 *                             it is user's responsibility to allocate and free
 *                             it. Can free it after the calling returns.
 * \param[in] complete_cb      completion callback
 * \param[in] arg              arguments for the \a complete_cb
 * \param[out] opid            returned bulk opid which can be used to abort
 *                             the bulk. It is optional, can pass in NULL if
 *                             don't need it.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_bind_transfer(struct crt_bulk_desc *bulk_desc,
		       crt_bulk_cb_t complete_cb, void *arg,
		       crt_bulk_opid_t *opid);

/**
 * Get length (number of bytes) of data abstracted by bulk handle.
 *
 * \param[in] bulk_hdl         bulk handle
 * \param[out] bulk_len        length of the data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_get_len(crt_bulk_t bulk_hdl, size_t *bulk_len);

/**
 * Get the number of segments of data abstracted by bulk handle.
 *
 * \param[in] bulk_hdl         bulk handle
 * \param[out] bulk_sgnum      number of segments
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_bulk_get_sgnum(crt_bulk_t bulk_hdl, unsigned int *bulk_sgnum);

/*
 * Abort a bulk transferring.
 *
 * \param[in] crt_ctx          CRT transport context
 * \param[in] opid             bulk opid
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *                             If abort succeed, the bulk transfer's completion
 *                             callback will be called with DER_CANCELED set to
 *                             crt_bulk_cb_info::bci_rc.
 */
int
crt_bulk_abort(crt_context_t crt_ctx, crt_bulk_opid_t opid);

/******************************************************************************
 * CRT group definition and collective APIs.
 ******************************************************************************/

/* Types for tree topology */
enum crt_tree_type {
	CRT_TREE_INVALID	= 0,
	CRT_TREE_MIN		= 1,
	CRT_TREE_FLAT		= 1,
	CRT_TREE_KARY		= 2,
	CRT_TREE_KNOMIAL	= 3,
	CRT_TREE_MAX		= 3,
};

#define CRT_TREE_TYPE_SHIFT	(16U)
#define CRT_TREE_MAX_RATIO	(64)
#define CRT_TREE_MIN_RATIO	(2)

/*
 * Calculate the tree topology. Can only be called on the server side.
 *
 * \param[in] tree_type        tree type
 * \param[in] branch_ratio     branch ratio, be ignored for CRT_TREE_FLAT.
 *                             for KNOMIAL tree or KARY tree, the valid value
 *                             should within the range of
 *                             [CRT_TREE_MIN_RATIO, CRT_TREE_MAX_RATIO], or
 *                             will be treated as invalid parameter.
 *
 * \return                     tree topology value on success,
 *                             negative value if error.
 */
static inline int
crt_tree_topo(enum crt_tree_type tree_type, uint32_t branch_ratio)
{
	if (tree_type < CRT_TREE_MIN || tree_type > CRT_TREE_MAX)
		return -DER_INVAL;

	return (tree_type << CRT_TREE_TYPE_SHIFT) |
	       (branch_ratio & ((1U << CRT_TREE_TYPE_SHIFT) - 1));
}

struct crt_corpc_ops {
	/**
	 * collective RPC reply aggregating callback.
	 *
	 * \param[in] source		the rpc structure of aggregating source
	 * \param[in] result		the rpc structure of aggregating result
	 * \param[in] priv		the private pointer, valid only on
	 *				collective RPC initiator (same as the
	 *				priv pointer passed in for
	 *				crt_corpc_req_create).
	 *
	 * \return			DER_SUCCESS on success, negative value
	 *				if error
	 */
	int (*co_aggregate)(crt_rpc_t *source, crt_rpc_t *result, void *arg);

	/**
	 * Collective RPC pre-forward callback.
	 * This is an optional callback. If specified, it will execute prior
	 * to corpc request being forwarded.
	 *
	 * \param[in] rpc		the rpc structure
	 * \param[in] arg		the private pointer, valid only on
	 *				collective RPC initiator (same as the
	 *				priv pointer passed in for
	 *				crt_corpc_req_create).
	 *
	 * \retval			Any value other than DER_SUCCESS will
	 *				cause CORPC to abort.
	 */
	int (*co_pre_forward)(crt_rpc_t *rpc, void *arg);

	/**
	 * Collective RPC post-reply callback.
	 * This is an optional callback. If specified, it will execute after
	 * reply is sent to parent (after co_aggregate executes).
	 *
	 * \param[in] rpc		the rpc structure of the parent
	 * \param[in] arg		the private pointer, valid only on
	 *				collective RPC initiator (same as the
	 *				priv pointer passed in for
	 *				crt_corpc_req_create).
	 *				Can be used to share data between
	 *				co_aggregate and co_post_reply  when
	 *				executing those callbacks on the same
	 *				node.
	 */
	int (*co_post_reply)(crt_rpc_t *rpc, void *arg);
};

/*
 * Group destroy completion callback
 *
 * \param[in] arg              arguments pointer passed in for
 *                             crt_group_destroy.
 * \param[in] status           status code that indicates whether the group
 *                             has been destroyed successfully or not.
 *                             zero for success, negative value otherwise.
 *
 */
typedef int (*crt_grp_destroy_cb_t)(void *arg, int status);

/**
 * Lookup the group handle of one group ID (sub-group or primary group).
 *
 * The primary group can be queried using the group ID passed to crt_init.
 * Some special cases:
 * 1) If (grp_id == NULL), it means the default primary group ID
 *     CRT_DEFAULT_GRPID.
 *
 * \note user can cache the returned group handle to avoid the overhead of
 *          frequent lookup.
 *
 * \param[in] grp_id           unique group ID.
 *
 * \return                     group handle on success, NULL if not found.
 */
crt_group_t *
crt_group_lookup(crt_group_id_t grp_id);

/**
 * Destroy a CRT group.
 *
 * \param[in] grp              group handle to be destroyed.
 * \param[in] grp_destroy_cb   optional completion callback.
 * \param[in] arg              optional arg for \a grp_destroy_cb.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *arg);

/**
 * Attach to a primary service group.
 *
 * By calling this function to attach to service primary group, and set
 * crt_endpoint_t::ep_grp as the returned attached_grp to send RPC to it.
 *
 * For client, the first attached service primary group become its default
 * service primary group. For server, its default service primary group is
 * the primary group created in crt_init().
 * User can pass crt_endpoint_t::ep_grp pointer as NULL to send RPC to the
 * default service primary group.
 *
 * \param[in] srv_grpid        Primary service group ID to attach to.
 * \param[out] attached_grp    Returned attached group handle pointer.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 * \note Make sure cart context 0 exists when calling this function. cart
 * context 0 is created by the first call to crt_context_create().
 */
int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);

/**
 * Set an alternative directory to store/retrieve group attach info
 *
 * The default location is /tmp.   This allows client and server to
 * agree on a location where to store the information and to avoid
 * conflicts with other server groups that may be sharing the nodes
 *
 * \param[in] path             Path where to store attach info
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_config_path_set(const char *path);

/**
 * Dump the attach info for the specified group to a file. If not the local
 * primary service group, it must be an attached service group.
 * This must be invoked before any singleton can attach to the specified group.
 *
 * \param[in] grp              Primary service group attach info to save,
 *                             NULL indicates local primary group.
 * \param[in] forall           True to save all service ranks' uri addresses,
 *                             false to only save the calling rank's uri for
 *                             server, or the internal PSR of attached remote
 *                             service group for client.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_config_save(crt_group_t *grp, bool forall);

/**
 * Remove the attach info file for the specified group.
 *
 * \param[in] grp              Primary service group attach info to delete,
 *                             NULL indicates local primary group.
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_group_config_remove(crt_group_t *grp);

/**
 * Detach a primary service group which was attached previously.
 *
 * \param[in] attached_grp     attached primary service group handle.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_detach(crt_group_t *attached_grp);

/**
 * Convert a primary group rank to a local subgroup rank. Given a primary group
 * rank \p rank_in, find its rank number \p rank_out within a sub-group \p
 * subgrp.
 *
 * \param[in] subgrp           CRT subgroup handle. subgrp must be local, i.e.
 *                             not created by crt_group_attach()
 * \param[in] rank_in          primary group rank number.
 * \param[out] rank_out        the result rank number of the conversion.
 */
int
crt_group_rank_p2s(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out);

/**
 * Convert a local subgroup rank to a primary group rank. Given a sub-group \p
 * subgrp and rank \p rank_in within the sub-group, find out its primary group
 * rank number \p rank_out.
 *
 * \param[in] subgrp           CRT subgroup handle. subgrp must be local, i.e.
 *                             not created by crt_group_attach()
 * \param[in] rank_in          rank number within grp.
 * \param[out] rank_out        the result rank number of the conversion.
 */
int
crt_group_rank_s2p(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out);

/**
 * Create collective RPC request. Can reuse the crt_req_send to broadcast it.
 * Can only be called on the server side.
 *
 * \param[in] crt_ctx          CRT context
 * \param[in] grp              CRT group for the collective RPC
 * \param[in] filter_ranks     optional filter ranks. By default, the RPC
 *                             will be delivered to all members in the group
 *                             except those in \a filter_ranks. If \a flags
 *                             includes CRT_RPC_FLAG_FILTER_INVERT, the RPC
 *                             will be delivered to \a filter_ranks only.
 *                             The ranks are numbered in primary group.
 * \param[in] opc              unique opcode for the RPC
 * \param[in] co_bulk_hdl      collective bulk handle
 * \param[in] priv             A private pointer associated with the request
 *                             will be passed to crt_corpc_ops::co_aggregate as
 *                             2nd parameter.
 * \param[in] flags            collective RPC flags:
 *                             CRT_RPC_FLAG_FILTER_INVERT to send only to
 *                             \a filter_ranks.
 * \param[in] tree_topo        tree topology for the collective propagation,
 *                             can be calculated by crt_tree_topo().
 *                             See \a crt_tree_type,
 *                             \a crt_tree_topo().
 * \param req [out]            created collective RPC request
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     d_rank_list_t *filter_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req);

/**
 * Dynamically register a collective RPC. Can only be called on the server side.
 *
 * \param[in] opc              unique opcode for the RPC
 * \param[in] drf              pointer to the request format, which
 *                             describe the request format and provide
 *                             callback to pack/unpack each items in the
 *                             request.
 * \param[in] rpc_handler      pointer to RPC handler which will be triggered
 *                             when RPC request opcode associated with rpc_name
 *                             is received.
 * \param[in] co_ops           pointer to corpc ops table.
 *
 * \note
 * 1) User can use crt_rpc_srv_reg to register collective RPC if no reply
 *    aggregation needed.
 * 2) Can pass in a NULL drf or rpc_handler if it was registered already, this
 *    routine only overwrite if they are non-NULL.
 * 3) A NULL co_ops is allowed for the case that user does not need the corpc
 *    op table (the aggregating callback).
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_corpc_register(crt_opcode_t opc, struct crt_req_format *drf,
		   crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops);

/**
 * Query the caller's rank number within group.
 *
 * \param[in] grp              CRT group handle, NULL mean the primary/global
 *                             group
 * \param[out] rank            result rank number. In singleton mode always get
 *                             rank 0 for local group.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_rank(crt_group_t *grp, d_rank_t *rank);

/**
 * Query the group membership version
 *
 * \param[in] grp              CRT group handle, NULL means the local
 *                             primary/global group
 * \param[out] version         group membership version
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_group_version(crt_group_t *grp, uint32_t *version);

/**
 * Set the group membership version
 *
 * \param[in] grp              CRT group handle, NULL means the local
 *                             primary/global group
 * \param[in] version          New group membership version
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_group_version_set(crt_group_t *grp, uint32_t version);

/**
 * Query number of group members.
 *
 * \param[in] grp              CRT group handle, NULL mean the local
 *                             primary/global group
 * \param[out] size            size (total number of ranks) of the group.
 *                             In singleton mode always get size 1 for local
 *                             group.
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_group_size(crt_group_t *grp, uint32_t *size);

/******************************************************************************
 * Proc data types, APIs and macros.
 ******************************************************************************/

typedef enum {
	/** causes the type to be encoded into the stream */
	CRT_PROC_ENCODE,
	/** causes the type to be extracted from the stream */
	CRT_PROC_DECODE,
	/** can be used to release the space allocated by CRT_DECODE request */
	CRT_PROC_FREE
} crt_proc_op_t;

#define ENCODING(proc_op) (proc_op == CRT_PROC_ENCODE)
#define DECODING(proc_op) (proc_op == CRT_PROC_DECODE)
#define FREEING(proc_op)  (proc_op == CRT_PROC_FREE)

#define crt_proc_raw crt_proc_memcpy

/**
 * Get the operation type associated to the proc processor.
 *
 * \param[in] proc             abstract processor object
 * \param[out] proc_op         returned proc operation type
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *proc_op);

/**
 * Base proc routine using memcpy().
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 * \param[in] data_size        data size
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_memcpy(crt_proc_t proc, crt_proc_op_t proc_op,
		void *data, size_t data_size);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_int8_t(crt_proc_t proc, crt_proc_op_t proc_op, int8_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_uint8_t(crt_proc_t proc, crt_proc_op_t proc_op, uint8_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_int16_t(crt_proc_t proc, crt_proc_op_t proc_op, int16_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_uint16_t(crt_proc_t proc, crt_proc_op_t proc_op, uint16_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_int32_t(crt_proc_t proc, crt_proc_op_t proc_op, int32_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_uint32_t(crt_proc_t proc, crt_proc_op_t proc_op, uint32_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_int64_t(crt_proc_t proc, crt_proc_op_t proc_op, int64_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_uint64_t(crt_proc_t proc, crt_proc_op_t proc_op, uint64_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_bool(crt_proc_t proc, crt_proc_op_t proc_op, bool *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] bulk_hdl     pointer to bulk handle
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_crt_bulk_t(crt_proc_t proc, crt_proc_op_t proc_op,
		    crt_bulk_t *bulk_hdl);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_d_string_t(crt_proc_t proc, crt_proc_op_t proc_op, d_string_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_d_const_string_t(crt_proc_t proc, crt_proc_op_t proc_op,
			  d_const_string_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_uuid_t(crt_proc_t proc, crt_proc_op_t proc_op, uuid_t *data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         second level pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 *
 * \note
 * 1) here pass in the 2nd level pointer of d_rank_list_t, to make it
 *    possible to set it to NULL when decoding.
 * 2) if the rank_list is non-NULL, caller should firstly duplicate it and pass
 *    the duplicated rank list's 2nd level pointer as parameter, because this
 *    function will internally free the memory when freeing the input or output.
 */
int
crt_proc_d_rank_list_t(crt_proc_t proc, crt_proc_op_t proc_op,
		       d_rank_list_t **data);

/**
 * Generic processing routine.
 *
 * \param[in,out] proc         abstract processor object
 * \param[in] proc_op          proc operation type
 * \param[in,out] data         pointer to data
 *
 * \return                     DER_SUCCESS on success, negative value if error
 */
int
crt_proc_d_iov_t(crt_proc_t proc, crt_proc_op_t proc_op, d_iov_t *data);

typedef int64_t
(*crt_progress_cb) (crt_context_t ctx, int64_t timeout, void *arg);

/**
 * Register a callback function which will be called inside crt_progress()
 */
int
crt_register_progress_cb(crt_progress_cb cb, int ctx_idx, void *arg);

/**
 * Unregister a callback function. The pair of arguments (ctx_idx and arg)
 * should be same as the ones provided during registration.
 */
int
crt_unregister_progress_cb(crt_progress_cb cb, int ctx_idx, void *arg);

typedef void
(*crt_timeout_cb) (crt_context_t ctx, crt_rpc_t *rpc, void *arg);

int
crt_register_timeout_cb(crt_timeout_cb cb, void *arg);

typedef void
(*crt_eviction_cb) (crt_group_t *grp, d_rank_t rank, void *arg);

enum crt_event_source {
	CRT_EVS_UNKNOWN,
	/**< Event triggered by SWIM >*/
	CRT_EVS_SWIM,
	/**< Event triggered by Group changes >*/
	CRT_EVS_GRPMOD,
};

enum crt_event_type {
	CRT_EVT_ALIVE,
	CRT_EVT_DEAD,
};

/**
 * Event handler callback.
 *
 * \param[in] rank             rank this event is about
 * \param[in] incarnation      rank incarnation if \a src is CRT_EVS_SWIM
 * \param[in] src              event source
 * \param[in] type             event type
 * \param[in] arg              arg passed to crt_register_event_cb with this
 *                             callback
 */
typedef void
(*crt_event_cb) (d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
		 enum crt_event_type type, void *arg);

/**
 * This function registers an event handler for any changes in rank state.
 * There is not any modification about the rank in rank list at this point.
 * The decision about adding or eviction the rank should be made from
 * an information from this handler.
 *
 * Important:
 * The event should be processed in non-blocking mode because this
 * handler is called under lock which should not be held for long time.
 * Sleeping is also prohibited in this handler! Only quick reaction is
 * expected into this handler before return.
 *
 * \param[in] event_handler    event handler to register
 * \param[in] arg              arg to event_handler
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_register_event_cb(crt_event_cb event_handler, void *arg);

/**
 * Unregister an event handler. The pair of arguments (event_handler and arg)
 * should be same as the ones provided during registration.
 *
 * \param[in] event_handler    event handler to register
 * \param[in] arg              arg to event_handler
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_unregister_event_cb(crt_event_cb event_handler, void *arg);

typedef void
(*crt_hlc_error_cb) (void *arg);

/**
 * This function registers an event handler for hlc synchronization errors.
 * Only a single hlc error event handler can be registered at a time.
 *
 * \param[in] event_handler    event handler to register
 * \param[in] arg              arg to event_handler
 *
 * \return                     DER_SUCCESS on success, negative value on error
 */
int
crt_register_hlc_error_cb(crt_hlc_error_cb event_handler, void *arg);

/**
 * A protocol is a set of RPCs. A protocol has a base opcode and a version,
 * member RPCs have opcodes that are contiguous numbers starting from
 * (base opcode | version). For example, if the protocol has
 *
 * base opcode:    0x05000000
 * version number: 0x00030000,
 *
 * its member RPCs will have opcode
 *                 0x05030000
 *                 0x05030001
 *                 0x05030002 and so on
 *
 * base opcode mask    0xFF000000UL
 * version number mask 0x00FF0000UL
 *
 * The base opcode 0xFF000000UL is not allowed. This gives 255 protocols, 256
 * versions for each protocol, 65,536 RPCs per protocol.
 *
 * Mode of operation:
 *
 * The client and server have knowledge of all possibly supported protocols.
 * The protocol negotiation is just to let a client find out which ones are
 * actually registered on the server.
 *
 * 1) A server registers a protocol with base opcode MY_BASE_OPC and version
 * number MY_VER, with member RPC opcodes
 *                MY_OPC_0 = (MY_BASE_OPC | MY_VER),
 *                MY_OPC_1 = (MY_BASE_OPC | MY_VER) + 1,
 *                MY_OPC_2 = (MY_BASE_OPC | MY_VER) + 2,
 * 2) A client queries the server if MY_BASE_OPC with version number is
 *    registered, the server replies Yes.
 *
 * 3) The client registers MY_BASE_OPC with version number MY_VER, then starts
 *    sending RPCs using it's member opcodes.
 */

/**
 * 1) define crf for each member RPC. my_rpc_crf_1, my_rpc_crf_2
 *
 * 2) req_format array for member RPCs:
 * struct crt_req_format *my_crf_array[] = {
 *                &my_crf_1,
 *                &my_crf_2,
 *    };
 *
 * rpc handler array for member RPCs, one handler for each RPC:
 *        crt_rpc_cb_t hdlr[] = {
 *                my_hdlr_1,
 *                my_hdlr_2,
 *        };
 *
 * 3) define crt_proto_format.
 * struct crt_proto_format my_proto_fmt =
 *        DEFINE_CRT_PROTO_FMT("my-proto", ver, my_crf_array);
 *
 * which expands to:
 * {
 *        .cpf_name = "my-proto";
 *        .cpf_ver = ver;
 *        .cpf_crf = {
 *                &my_crf_1,
 *                &my_crf_2,
 *         };
 *         .cpf_hdlr = {
 *                my_hdlr_1,
 *                my_hdlr_2,
 *         };
 * }
 *
 */

/**
 * Register a protocol. Can be called on a server or a client. Re-registering
 * existing base_opc + version combination will result in -DER_EXIST error
 * being returned to the caller.
 *
 * \param[in] cpf              protocol format description. (See \ref
 *                             crt_proto_format)
 *
 * \return                     DER_SUCCESS on success, negative value
 *                             on failure.
 */
int
crt_proto_register(struct crt_proto_format *cpf);

/**
 * query tgt_ep if it has registered base_opc with version.
 *
 * \param[in] tgt_ep           the service rank to query
 * \param[in] base_opc         the base opcode for the protocol
 * \param[in] ver              array of protocol version
 * \param[in] count            number of elements in ver
 * \param[in] cb               completion callback. crt_proto_query() internally
 *                             sends an RPC to \a tgt_ep. \a cb will be called
 *                             upon completion of that RPC. The highest protocol
 *                             version supported by the target is available to
 *                             \a cb as cb_info->pq_ver. (See \ref
 *                             crt_proto_query_cb_t and \ref
 *                             crt_proto_query_cb_info)
 * \param[in,out] arg          argument for \a cb.
 *
 * \return                     DER_SUCCESS on success, negative value on
 *                             failure.
 */
int
crt_proto_query(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc,
		uint32_t *ver, int count, crt_proto_query_cb_t cb, void *arg);

/**
 * Set self rank.
 *
 * \param[in] rank              Rank to set on self.
 *
 * \return                      DER_SUCCESS on success, negative value on
 *                              failure.
 */
int
crt_rank_self_set(d_rank_t rank);

/**
 * Retrieve URI of the requested rank:tag pair.
 *
 * \param[in] grp               Group identifier
 * \param[in] rank              Rank to get uri for
 * \param[in] tag               Tag to get uri for
 * \param[out] uri              Returned uri string
 *
 * \note Returned uri string must be de-allocated by the user at some
 * point once the information is no longer needed.
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int
crt_rank_uri_get(crt_group_t *grp, d_rank_t rank, int tag, char **uri);

/**
 * Get rank SWIM state.
 *
 * \param[in]  grp              Group identifier
 * \param[in]  rank             Rank to get SWIM state for
 * \param[out] state            The pointer to store SWIM state
 *
 * \return                      DER_SUCCESS on success, negative value on
 *                              failure.
 */
int
crt_rank_state_get(crt_group_t *grp, d_rank_t rank,
		   struct swim_member_state *state);

/**
 * Remove specified rank from the group.
 *
 * \param[in] group             Group identifier
 * \param[in] rank              Rank to remove
 *
 * \return                      DER_SUCCESS on success, negative value on
 *                              failure.
 */
int
crt_group_rank_remove(crt_group_t *group, d_rank_t rank);

/**
 * Retrieve uri of self for the specified tag.  The uri must be freed by the
 * user using D_FREE().
 *
 * \param[in] tag               Tag to get uri for
 * \param[out] uri              Returned uri string This is a NULL terminated
 *                              string of size up to CRT_ADDR_STR_MAX_LEN
 *                              (including the trailing NULL). Must be freed by
 *                              the user.
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_self_uri_get(int tag, char **uri);

/**
 * Retrieve incarnation of self.
 *
 * \param[out] incarnation      Returned incarnation
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_self_incarnation_get(uint64_t *incarnation);

/**
 * Retrieve group information containing ranks and associated uris
 *
 * This call will allocate memory for buffers in passed \a grp_info.
 * User is responsible for freeing the memory once not needed anymore.
 *
 * Returned data in \a grp_info can be passed to crt_group_info_set
 * call in order to setup group on a different node.
 *
 * \param[in] group             Group identifier
 * \param[in] grp_info          group info to be filled.
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_info_get(crt_group_t *group, d_iov_t *grp_info);

/**
 * Sets group info (nodes and associated uris) baesd on passed
 * grp_info data. \a grp_info is to be retrieved via \a crt_group_info_get
 * call.
 *
 * \param[in] grp_info          Group information to set
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_info_set(d_iov_t *grp_info);

/**
 * Retrieve list of ranks that belong to the specified gorup.
 *
 * \param[in] group             Group identifier
 * \param[out] list             Rank list that gets filled with members
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure
 */
int crt_group_ranks_get(crt_group_t *group, d_rank_list_t **list);

/**
 * Create local group view and return a handle to a group.
 * This call is only supported for clients.
 *
 * \param[in] grp_id            Group id to create
 * \param[out] ret_grp          Returned group handle
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure
 */
int crt_group_view_create(crt_group_id_t grpid, crt_group_t **ret_grp);

/**
 * Destroy group handle previously created by \a crt_Group_view_create
 * This call is only supported for clients
 *
 * \param[in] grp               Group handle to destroy
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_view_destroy(crt_group_t *grp);

/**
 * Specify rank to be a PSR for the provided group
 *
 * \param[in] grp               Group handle
 * \param[in] rank              Rank to set as PSR
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_psr_set(crt_group_t *grp, d_rank_t rank);

/**
 * Specify list of ranks to be a PSRs for the provided group
 *
 * \param[in] grp               Group handle
 * \param[in] rank_list         Ranks to set as PSRs
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_psrs_set(crt_group_t *grp, d_rank_list_t *rank_list);

/**
 * Add rank to the specified primary group.
 *
 * Passed ctx will be used to determine a provider for which the uri is
 * being added in the case of the primary group.
 *
 * For primary groups when uri is specified, the uri is assumed
 * to be 'base' URI, corresponding to tag=0 of the node being added.
 *
 * \param[in] ctx               Associated cart context
 * \param[in] grp               Group handle
 * \param[in] primary_rank      Primary rank to be added
 * \param[in] uri               URI of the primary rank
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_primary_rank_add(crt_context_t ctx, crt_group_t *grp,
			d_rank_t primary_rank, char *uri);

/**
 * Add rank to the specified secondary group.
 *
 * \param[in] grp               Group handle
 * \param[in] secondary_rank    Secondary rank
 * \param[in] primary_rank      Primary rank to map to secondary
 *
 * \return                      DER_SUCCESS on success, negative value
 *                              on failure.
 */
int crt_group_secondary_rank_add(crt_group_t *grp, d_rank_t secondary_rank,
				d_rank_t primary_rank);
/**
 * Create a secondary group.
 *
 * \param[in] grp_name           Name of the group
 * \param[in] primary_grp        Primary group handle associated with this
 *                               group.
 * \param[in] ranks              Optional list of primary ranks
 * \param[out] ret_grp           Returned group handle for the secondary group
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 */
int crt_group_secondary_create(crt_group_id_t grp_name,
			crt_group_t *primary_grp, d_rank_list_t *ranks,
			crt_group_t **ret_grp);

/**
 * Enable auto-rank removal on secondary group. Only applicable for primary
 * groups.
 *
 * \param[in] grp               Group handle
 *
 * \param[in] enable		Flag to enable or disable the option
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 */
int crt_group_auto_rank_remove(crt_group_t *grp, bool enable);

/**
 * Destroy a secondary group.
 *
 * \param[in] grp                Group handle.
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 */
int crt_group_secondary_destroy(crt_group_t *grp);

/**
 * Perform a primary group modification in an atomic fashion based on the
 * operation specified. Currently supported operations are 'add', 'remove'
 * and 'replace'. This API allows multiple ranks to be added or removed
 * at the same time with a single call.
 *
 * Add: Ranks in the rank list are added to the group with corresponding uris
 * Remove: Ranks in the rank list are removed from the group
 * Replace:
 *     Ranks that exist in group and not in rank list get removed
 *     Ranks that exist in rank list and not in group get added
 *     Ranks that exist in both rank list and group are left unmodified
 *
 * \param[in] grp                Group handle
 * \param[in] ctxs               Array of contexts
 * \param[in] num_ctxs           Number of contexts
 * \param[in] ranks              Modification rank list
 * \param[in] uris               Array of URIs corresponding to contexts and
 *                               rank list
 * \param[in] op                 Modification operation.
 * \param[in] version            New group version
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 *
 * Note: \ref uris shall be an array of (ranks->rl_nr * num_ctxs) strings
 * In multi-provider case, where num_ctxs > 1, uris array should be
 * formed as follows:
 * [uri0 for provider0 identified by ctx0]
 * [uri1 for provider0]
 * ....
 * [uriX for provider0]
 *
 * [uri0 for provider1 identified by ctx1]
 * [uri1 for provider1]
 * ...
 * [uriX for provider1]
 *
 * [uri0 for provider2 identified by ctx2]
 * etc...
 */
int crt_group_primary_modify(crt_group_t *grp, crt_context_t *ctxs,
			int num_ctxs, d_rank_list_t *ranks, char **uris,
			crt_group_mod_op_t op, uint32_t version);

/**
 * Perform a secondary group modification in an atomic fashion based on the
 * operation type specified. Operations are the same as in
 * \ref crt_group_primary_modify API.
 *
 * \param[in] grp                Group handle
 * \param[in] sec_ranks          List of secondary ranks
 * \param[in] prim_ranks         List of primary ranks
 * \param[in] op                 Modification operation
 * \param[in] version            New group version
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 */
int crt_group_secondary_modify(crt_group_t *grp, d_rank_list_t *sec_ranks,
			d_rank_list_t *prim_ranks, crt_group_mod_op_t op,
			uint32_t version);

/**
 * Initialize swim on the specified context index.
 *
 * \param[in] crt_ctx_idx        Context index to initialize swim on
 *
 * \return                       DER_SUCCESS on success, negative value on
 *                               failure.
 */
int crt_swim_init(int crt_ctx_idx);

/** Finalize swim.
 */
void crt_swim_fini(void);

#define crt_proc__Bool			crt_proc_bool
#define crt_proc_d_rank_t		crt_proc_uint32_t
#define crt_proc_int			crt_proc_int32_t
#define crt_proc_crt_status_t		crt_proc_int32_t
#define crt_proc_crt_group_id_t		crt_proc_d_string_t
#define crt_proc_crt_phy_addr_t		crt_proc_d_string_t

/**
 * \a err is an error that ought to be logged at a less serious level than ERR.
 *
 * \param[in] err                an error
 *
 * \return                       true or false
 */
static inline bool
crt_quiet_error(int err)
{
	return err == -DER_GRPVER;
}

/** @}
 */

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_API_H__ */
