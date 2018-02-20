/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
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
#include <gurt/errno.h>
#include <cart/iv.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialize CRT transport layer. Must be called on both the server side and
 * the client side. This function is reference counted, it can be called
 * multiple times. Each call must be paired with a corresponding crt_finalize().
 *
 * \param grpid [IN]		primary group ID, user can provide a NULL value
 *				in that case will use the default group ID,
 *				CRT_DEFAULT_CLI_GRPID for client and
 *				CRT_DEFAULT_SRV_GRPID for server.
 * \param flags [IN]		bit flags, /see enum crt_init_flag_bits.
 *
 * \return			zero on success, negative value if error
 *
 * Notes: crt_init() is a collective call which means every caller process
 *	  should make the call collectively, as now it will internally call
 *	  PMIx_Fence.
 */
int
crt_init(crt_group_id_t grpid, uint32_t flags);

/**
 * Create CRT transport context. Must be destroyed by crt_context_destroy()
 * before calling crt_finalize().
 *
 * \param crt_ctx [OUT]		created CRT transport context
 *
 * \return			zero on success, negative value if error
 */
int
crt_context_create(crt_context_t *crt_ctx);

/**
 * Destroy CRT transport context.
 *
 * \param crt_ctx [IN]          CRT transport context to be destroyed
 * \param force [IN]            1) force == 0
 *                                 return as -EBUSY if there is any in-flight
 *                                 RPC request, so caller can wait its
 *                                 completion or timeout.
 *                              2) force != 0
 *                                 will cancel all in-flight RPC requests.
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: currently there is no in-flight list/queue in mercury.
 */
int
crt_context_destroy(crt_context_t crt_ctx, int force);

/**
 * Query the index of the transport context, the index value ranges in
 * [0, ctx_num - 1].
 *
 * \param crt_ctx [IN]          CRT transport context
 * \param ctx_idx [OUT]         pointer to the returned index
 *
 * \return                      zero on success, negative value if error
 */
int
crt_context_idx(crt_context_t crt_ctx, int *ctx_idx);

/**
 * Query the total number of the transport contexts.
 *
 * \param ctx_num [OUT]         pointer to the returned number
 *
 * \return                      zero on success, negative value if error
 */
int
crt_context_num(int *ctx_num);

/**
 * Finalize CRT transport layer. Must be called on both the server side and
 * client side before exit. This function is reference counted.
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: crt_finalize() is a collective call which means every caller process
 *	  should make the call collectively, as now it will internally call
 *	  PMIx_Fence.
 */
int
crt_finalize(void);

/**
 * Progress CRT transport layer.
 *
 * \param crt_ctx	[IN]	CRT transport context
 * \param timeout	[IN]	how long is caller going to wait (micro-second)
 *				if \a timeout > 0 when there is no operation to
 *				progress. Can return when one or more operation
 *				progressed.
 *				zero means no waiting and -1 waits indefinitely.
 * \param cond_cb	[IN]	optional progress condition callback.
 *				CRT internally calls this function, when it
 *				returns non-zero then stops the progressing or
 *				waiting and returns.
 * \param arg		[IN]	argument to cond_cb.
 *
 * \return			zero on success, negative value if error
 */
int
crt_progress(crt_context_t crt_ctx, int64_t timeout,
	     crt_progress_cond_cb_t cond_cb, void *arg);

/**
 * Create an RPC request.
 *
 * \param crt_ctx [IN]          CRT transport context
 * \param tgt_ep [IN]           RPC target endpoint
 * \param opc [IN]              RPC request opcode
 * \param req [OUT]             pointer to created request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the crt_req_create will internally allocate zeroed buffers for input
 *        and output parameters (crt_rpc_t::dr_input/dr_output), and set the
 *        appropriate size (crt_rpc_t::dr_input_size/dr_output_size).
 *        User needs not to allocate extra input/output buffers. After the
 *        request created, user can directly fill input parameters into
 *        crt_rpc_t::dr_input and send the RPC request.
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
 * \param req [IN]              pointer to RPC request
 * \param tgt_ep [IN]           RPC target endpoint
 *
 * \return                      zero on success, negative value if error
 */
int
crt_req_set_endpoint(crt_rpc_t *req, crt_endpoint_t *tgt_ep);

/**
 * Set the timeout value for an RPC request.
 *
 * It is an optional function. If user does not call it, then will depend on
 * CRT_TIMEOUT ENV as timeout value (see the CRT_TIMEOUT section in README.env).
 * User can also explicitly set one RPC request's timeout value by calling this
 * function.
 *
 * \param req [IN]              pointer to RPC request
 * \param timeout_sec [IN]      timeout value in seconds
 *                              value of zero will be treated as invalid
 *                              parameter.
 *
 * \return                      zero on success, negative value if error
 */
int
crt_req_set_timeout(crt_rpc_t *req, uint32_t timeout_sec);

/**
 * Add reference of the RPC request.
 *
 * The typical usage is that user needs to do some asynchronous operations in
 * RPC handler and does not want to block in RPC handler, then it can call this
 * function to hold a reference and return. Later when that asynchronous
 * operation is done, it can release the reference (/see crt_req_decref). CRT
 * internally frees the resource of the RPC request when its reference drops to
 * zero.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 */
int
crt_req_addref(crt_rpc_t *req);

/**
 * Decrease reference of the RPC request. /see crt_req_addref.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 */
int
crt_req_decref(crt_rpc_t *req);

/**
 * Send an RPC request. In the case of sending failure, CRT internally destroy
 * the request \a req. In the case of succeed, the \a req will be internally
 * destroyed when the reply received. User needs not call crt_req_decref() to
 * destroy the request in either case.
 *
 * \param req [IN]		pointer to RPC request
 * \param complete_cb [IN]	optional completion callback, when it is
 *				provided the completion result (success or
 *				failure) will be reported by calling it in the
 *				context of user's calling of crt_progress() or
 *				crt_req_send().
 * \param arg [IN]		arguments for the \a complete_cb
 *
 * \return			if \a complete_cb provided (non-NULL), always
 *				returns zero; otherwise returns zero on success,
 *				negative value if error.
 *
 * Notes: the crt_rpc_t is exported to user, caller should fill the
 *        crt_rpc_t::dr_input and before sending the RPC request.
 *        \see crt_req_create.
 */
int
crt_req_send(crt_rpc_t *req, crt_cb_t complete_cb, void *arg);

/**
 * Send an RPC reply. Only to be called on the server side.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the crt_rpc_t is exported to user, caller should fill the
 *        crt_rpc_t::dr_output before sending the RPC reply.
 *        \see crt_req_create.
 */
int
crt_reply_send(crt_rpc_t *req);

/**
 * Return request buffer
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      pointer to request buffer
 */
static inline void *
crt_req_get(crt_rpc_t *rpc)
{
	return rpc->cr_input;
}

/**
 * Return reply buffer
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      pointer to reply buffer
 */
static inline void *
crt_reply_get(crt_rpc_t *rpc)
{
	return rpc->cr_output;
}

/**
 * Abort an RPC request.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 *                              If the RPC has been sent out by crt_req_send,
 *                              the completion callback will be called with
 *                              DER_CANCELED set to crt_cb_info::cci_rc for a
 *                              successful aborting.
 */
int
crt_req_abort(crt_rpc_t *req);

/**
 * Abort all in-flight RPC requests targeting to an endpoint.
 *
 * \param ep [IN]		endpoint address
 *
 * \return			zero on success, negative value if error
 */
int
crt_ep_abort(crt_endpoint_t *ep);

/**
 * Dynamically register an RPC with features at client-side.
 *
 * \param opc [IN]              unique opcode for the RPC
 * \param flags [IN]		feature bits, now only supports
 *				CRT_RPC_FEAT_NO_REPLY - disables reply when set,
 *					re-enables reply when not set.
 *				CRT_RPC_FEAT_NO_TIMEOUT - if it's set, the
 *					elapsed time is reset to 0 on RPC
 *					timeout
 * \param drf [IN]		pointer to the request format, which
 *                              describe the request format and provide
 *                              callback to pack/unpack each items in the
 *                              request.
 * \return                      zero on success, negative value if error
 */
int
crt_rpc_register(crt_opcode_t opc, uint32_t flags, struct crt_req_format *drf);

/**
 * The RPC callback for the context, which will be called when the context
 * receives any RPC. In this callback, the handler can do sth specially for
 * the RPC on this context, for example create another ULT to handle it,
 * see DAOS.
 *
 * \param ctx [IN]		The cart context.
 * \param rpc [IN]		RPC received.
 * \param rpc_hdlr [IN]		Real RPC handler.
 * \param arg [IN]		The argument for the RPC handler.
 *
 * \return			0 for success, negative value if failed.
 *
 */
typedef int (*crt_rpc_task_t) (crt_context_t *ctx, crt_rpc_t *rpc,
			       void (*rpc_hdlr)(void *), void *arg);
/**
 * Register RPC process callback for all RPCs this context received.
 * This callback enables the thread to modify how the rpc callbacks are
 * handled for this context. For example DAOS creates another argobot
 * ULT to handle it.
 *
 * \param crt_ctx [IN]		The context to be registered.
 * \param rpc_cb [IN]		The RPC process callback.
 * \param arg [IN]		The argument for RPC process callback.
 *
 * \return			zero on success, negative value if error.
 */
int
crt_context_register_rpc_task(crt_context_t crt_ctx,
			      crt_rpc_task_t rpc_cb, void *arg);

/**
 * Dynamically register an RPC with features at server-side.
 *
 * \param opc [IN]		unique opcode for the RPC
 * \param flags [IN]		feature bits, now only supports
 *				CRT_RPC_FEAT_NO_REPLY - disables reply when set,
 *					re-enables reply when not set.
 *				CRT_RPC_FEAT_NO_TIMEOUT - if it's set, the
 *					elapsed time is reset to 0 on RPC
 *					timeout
 * \param crf [IN]		pointer to the request format, which
 *				describe the request format and provide
 *				callback to pack/unpack each items in the
 *				request.
 * \param rpc_handler [IN]	pointer to RPC handler which will be triggered
 *				when RPC request opcode associated with rpc_name
 *				is received. Will return -DER_INVAL if pass in
 *				NULL rpc_handler.
 *
 * \return			zero on success, negative value if error
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
 * \param crt_ctx [IN]          CRT transport context
 * \param sgl [IN]              pointer to buffer segment list
 * \param bulk_perm [IN]        bulk permission, \see crt_bulk_perm_t
 * \param bulk_hdl [OUT]        created bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_create(crt_context_t crt_ctx, d_sg_list_t *sgl,
		crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl);

/**
 * Access local bulk handle to retrieve the sgl (segment list) associated
 * with it.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param sgl[IN/OUT]           pointer to buffer segment list
 *                              Caller should provide a valid sgl pointer, if
 *                              sgl->sg_nr is too small, -DER_TRUNC will be
 *                              returned and the needed number of iovs be set at
 *                              sgl->sg_nr_out.
 *                              On success, sgl->sg_nr_out will be set as
 *                              the actual number of iovs.
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_access(crt_bulk_t bulk_hdl, d_sg_list_t *sgl);

/**
 * Free a bulk handle
 *
 * \param bulk_hdl [IN]         bulk handle to be freed
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_free(crt_bulk_t bulk_hdl);

/**
 * Start a bulk transferring (inside an RPC handler).
 *
 * \param bulk_desc [IN]        pointer to bulk transferring descriptor
 *				it is user's responsibility to allocate and free
 *				it. Can free it after the calling returns.
 * \param complete_cb [IN]      completion callback
 * \param arg [IN]              arguments for the \a complete_cb
 * \param opid [OUT]            returned bulk opid which can be used to abort
 *				the bulk. It is optional, can pass in NULL if
 *				don't need it.
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_transfer(struct crt_bulk_desc *bulk_desc, crt_bulk_cb_t complete_cb,
		  void *arg, crt_bulk_opid_t *opid);

/**
 * Get length (number of bytes) of data abstracted by bulk handle.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param bulk_len [OUT]        length of the data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_get_len(crt_bulk_t bulk_hdl, size_t *bulk_len);

/**
 * Get the number of segments of data abstracted by bulk handle.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param bulk_sgnum [OUT]      number of segments
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_get_sgnum(crt_bulk_t bulk_hdl, unsigned int *bulk_sgnum);

/*
 * Abort a bulk transferring.
 *
 * \param crt_ctx [IN]          CRT transport context
 * \param opid [IN]             bulk opid
 *
 * \return                      zero on success, negative value if error
 *                              If abort succeed, the bulk transfer's completion
 *                              callback will be called with DER_CANCELED set to
 *                              crt_bulk_cb_info::bci_rc.
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
 * \param tree_type [IN]	tree type
 * \param branch_ratio [IN]	branch ratio, be ignored for CRT_TREE_FLAT.
 *				for KNOMIAL tree or KARY tree, the valid value
 *				should within the range of
 *				[CRT_TREE_MIN_RATIO, CRT_TREE_MAX_RATIO], or
 *				will be treated as invalid parameter.
 *
 * \return			tree topology value on success,
 *				negative value if error.
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
	/*
	 * collective RPC reply aggregating callback.
	 *
	 * \param source [IN]		the rpc structure of aggregating source
	 * \param result[IN]		the rpc structure of aggregating result
	 * \param priv [IN]		the private pointer, valid only on
	 *				collective RPC initiator (same as the
	 *				priv pointer passed in for
	 *				crt_corpc_req_create).
	 *
	 * \return			zero on success, negative value if error
	 */
	int (*co_aggregate)(crt_rpc_t *source, crt_rpc_t *result, void *arg);
};

/*
 * Group create completion callback
 *
 * \param grp [IN]		group handle, valid only when the group has been
 *				created successfully.
 * \param priv [IN]		A private pointer associated with the group
 *				(passed in for crt_group_create).
 * \param status [IN]		status code that indicates whether the group has
 *				been created successfully or not.
 *				zero for success, negative value otherwise.
 */
typedef int (*crt_grp_create_cb_t)(crt_group_t *grp, void *priv, int status);

/*
 * Group destroy completion callback
 *
 * \param arg [IN]		arguments pointer passed in for
 *				crt_group_destroy.
 * \param status [IN]		status code that indicates whether the group
 *				has been destroyed successfully or not.
 *				zero for success, negative value otherwise.
 *
 */
typedef int (*crt_grp_destroy_cb_t)(void *arg, int status);

/*
 * Create CRT sub-group (a subset of the primary group). Can only be called on
 * the server side.
 *
 * \param grp_id [IN]		unique group ID.
 * \param member_ranks [IN]	rank list of members for the group.
 *				Can-only create the group on the node which is
 *				one member of the group, otherwise -DER_OOG will
 *				be returned.
 * \param populate_now [IN]	True if the group should be populated now;
 *				otherwise, group population will be later
 *				piggybacked on the first broadcast over the
 *				group.
 * \param grp_create_cb [IN]	Callback function to notify completion of the
 *				group creation process,
 *				\see crt_grp_create_cb_t.
 * \param priv [IN]		A private pointer associated with the group.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_create(crt_group_id_t grp_id, d_rank_list_t *member_ranks,
		 bool populate_now, crt_grp_create_cb_t grp_create_cb,
		 void *arg);

/*
 * Lookup the group handle of one group ID (sub-group or primary group).
 *
 * For sub-group, its creation is initiated by one node, after the group being
 * populated (internally performed inside crt_group_create) user can query the
 * group handle (crt_group_t *) on other nodes.
 *
 * The primary group can be queried using the group ID passed to crt_init.
 * Some special cases:
 * 1) If (grp_id == NULL), it means the default local primary group ID, i.e.
 *    the CRT_DEFAULT_CLI_GRPID for client and CRT_DEFAULT_SRV_GRPID for server.
 * 2) To query attached remote service primary group, can pass in its group ID.
 *    For the client-side, if it passed in NULL as crt_init's srv_grpid
 *    parameter, then can use CRT_DEFAULT_SRV_GRPID to lookup the attached
 *    service primary group handle.
 *
 * Notes: user can cache the returned group handle to avoid the overhead of
 *	  frequent lookup.
 *
 * \param grp_id [IN]		unique group ID.
 *
 * \return			group handle on success, NULL if not found.
 */
crt_group_t *
crt_group_lookup(crt_group_id_t grp_id);

/*
 * Destroy a CRT group. Can either call this function or pass a special flag -
 * CRT_RPC_FLAG_GRP_DESTROY to a broadcast RPC to destroy the subgroup. Can only
 * be called on the server side.
 *
 * \param grp [IN]		group handle to be destroyed.
 * \param grp_destroy_cb [IN]	optional completion callback.
 * \param arg [IN]		optional arg for \a grp_destroy_cb.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *arg);

/*
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
 * \param srv_grpid [IN]	Primary service group ID to attach to.
 * \param attached_grp [OUT]	Returned attached group handle pointer.
 *
 * \return			zero on success, negative value if error
 *
 * Note: Make sure cart context 0 exists when calling this function. cart
 * context 0 is created by the first call to crt_context_create().
 */
int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);

/*
 * Set an alternative directory to store/retrieve group attach info
 *
 * The default location is /tmp.   This allows client and server to
 * agree on a location where to store the information and to avoid
 * conflicts with other server groups that may be sharing the nodes
 *
 * \param path [IN]	Path where to store attach info
 *
 * \return		zero on success, negative value if error
 */
int
crt_group_config_path_set(const char *path);

/*
 * Dump the attach info for the specified group to a file. If not the local
 * primary service group, it must be an attached service group.
 * This must be invoked before any singleton can attach to the specified group.
 *
 * \param grp [IN]		Primary service group attach info to save,
 *				NULL indicates local primary group.
 * \param forall [IN]		True to save all service ranks' uri addresses,
 *				false to only save the calling rank's uri for
 *				server, or the internal PSR of attached remote
 *				service group for client.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_config_save(crt_group_t *grp, bool forall);

/*
 * Detach a primary service group which was attached previously.
 *
 * \param attached_grp [IN]	attached primary service group handle.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_detach(crt_group_t *attached_grp);


/*
 * Convert a primary group rank to a local subgroup rank. Given a primary group
 * rank \p rank_in, find its rank number \p rank_out within a sub-group \p
 * subgrp.
 *
 * \param subgrp [IN]		CRT subgroup handle. subgrp must be local, i.e.
 *				not created by crt_group_attach()
 * \param rank_in [IN]		primary group rank number.
 * \param rank_out [OUT]	the result rank number of the conversion.
 */
int
crt_group_rank_p2s(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out);

/*
 * Convert a local subgroup rank to a primary group rank. Given a sub-group \p
 * subgrp and rank \p rank_in within the sub-group, find out its primary group
 * rank number \p rank_out.
 *
 * \param subgrp [IN]		CRT subgroup handle. subgrp must be local, i.e.
 *				not created by crt_group_attach()
 * \param rank_in [IN]		rank number witin grp.
 * \param rank_out [OUT]	the result rank number of the conversion.
 */
int
crt_group_rank_s2p(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out);

/*
 * Create collective RPC request. Can reuse the crt_req_send to broadcast it.
 * Can only be called on the server side.
 *
 * \param crt_ctx [IN]		CRT context
 * \param grp [IN]		CRT group for the collective RPC
 * \param excluded_ranks [IN]	optional excluded ranks, the RPC will be
 *				delivered to all members in the group except
 *				those in excluded_ranks.
 *				the ranks in excluded_ranks are numbered in
 *				primary group.
 * \param opc [IN]		unique opcode for the RPC
 * \param co_bulk_hdl [IN]	collective bulk handle
 * \param priv [IN]		A private pointer associated with the request
 *				will be passed to crt_corpc_ops::co_aggregate as
 *				2nd parameter.
 * \param flags [IN]		collective RPC flags for example taking
 *				CRT_RPC_FLAG_GRP_DESTROY to destroy the subgroup
 *				when this bcast RPC successfully finished.
 * \param tree_topo[IN]		tree topology for the collective propagation,
 *				can be calculated by crt_tree_topo().
 *				/see enum crt_tree_type, /see crt_tree_topo().
 * \param req [out]		created collective RPC request
 *
 * \return			zero on success, negative value if error
 */
int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     d_rank_list_t *excluded_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req);

/**
 * Dynamically register a collective RPC. Can only be called on the server side.
 *
 * \param opc [IN]		unique opcode for the RPC
 * \param drf [IN]		pointer to the request format, which
 *				describe the request format and provide
 *				callback to pack/unpack each items in the
 *				request.
 * \param rpc_handler [IN]	pointer to RPC handler which will be triggered
 *				when RPC request opcode associated with rpc_name
 *				is received.
 * \param co_ops [IN]		pointer to corpc ops table.
 *
 * Notes:
 * 1) User can use crt_rpc_srv_reg to register collective RPC if no reply
 *    aggregation needed.
 * 2) Can pass in a NULL drf or rpc_handler if it was registered already, this
 *    routine only overwrite if they are non-NULL.
 * 3) A NULL co_ops is allowed for the case that user does not need the corpc
 *    op table (the aggregating callback).
 *
 * \return			zero on success, negative value if error
 */
int
crt_corpc_register(crt_opcode_t opc, struct crt_req_format *drf,
		   crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops);

/**
 * Start execution of the next available barrier.  If this function
 * returns an error, no internal state is changed. Can only be called on the
 * server side.
 *
 * \param grp [IN]		CRT group handle [for future use].   Only the
 *                              primary service group is presently supported
 *                              and it may be indicated by passing NULL.
 * \param complete_cb [IN]	Required callback to be executed when barrier
 *                              is complete
 * \param cb_arg [IN]		Optional argument passed to completion callback
 *
 * \return			zero on success
 *                              -DER_BUSY if a barrier slot isn't available
 *                              suggesting that prior barriers need to complete
 *                              before trying again.
 *                              Other negative error codes are possible if
 *                              grp doesn't exist or complete_cb is invalid.
 *
 * When the rank that is responsible to notify other members of the set of
 * barrier events hits an unrecoverable error (e.g. unable to communicate with
 * any other ranks), the completion callback will be invoked with an error.
 */
int
crt_barrier(crt_group_t *grp, crt_barrier_cb_t complete_cb, void *cb_arg);

/**
 * Query the caller's rank number within group.
 *
 * \param grp [IN]		CRT group handle, NULL mean the primary/global
 *				group
 * \param rank[OUT]		result rank number. In singleton mode always get
 *				rank 0 for local group.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_rank(crt_group_t *grp, d_rank_t *rank);

/**
 * Query the group membership version
 *
 * \param grp [IN]		CRT group handle, NULL means the local
 *				primary/global group
 * \param version [OUT]		group membership version
 *
 * \return			zero on success, negative value on error
 */
int
crt_group_version(crt_group_t *grp, uint32_t *version);

/**
 * Query number of group members.
 *
 * \param grp [IN]		CRT group handle, NULL mean the local
 *				primary/global group
 * \param size[OUT]		size (total number of ranks) of the group.
 *				In singleton mode always get size 1 for local
 *				group.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_size(crt_group_t *grp, uint32_t *size);

/******************************************************************************
 * Proc data types, APIs and macros.
 ******************************************************************************/

typedef enum {
	/* causes the type to be encoded into the stream */
	CRT_PROC_ENCODE,
	/* causes the type to be extracted from the stream */
	CRT_PROC_DECODE,
	/* can be used to release the space allocated by CRT_DECODE request */
	CRT_PROC_FREE
} crt_proc_op_t;

/**
 * Get the operation type associated to the proc processor.
 *
 * \param proc [IN]             abstract processor object
 * \param proc_op [OUT]         returned proc operation type
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *proc_op);

/**
 * Base proc routine using memcpy().
 * Only uses memcpy() / use crt_proc_raw() for encoding raw buffers.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 * \param data_size [IN]        data size
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_memcpy(crt_proc_t proc, void *data, size_t data_size);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_int8_t(crt_proc_t proc, int8_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_uint8_t(crt_proc_t proc, uint8_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_int16_t(crt_proc_t proc, int16_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_uint16_t(crt_proc_t proc, uint16_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_int32_t(crt_proc_t proc, int32_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_uint32_t(crt_proc_t proc, uint32_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_int64_t(crt_proc_t proc, int64_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_uint64_t(crt_proc_t proc, uint64_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_bool(crt_proc_t proc, bool *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param buf [IN/OUT]          pointer to buffer
 * \param buf_size [IN]         buffer size
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_raw(crt_proc_t proc, void *buf, size_t buf_size);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param bulk_hdl [IN/OUT]     pointer to bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_bulk_t(crt_proc_t proc, crt_bulk_t *bulk_hdl);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_string_t(crt_proc_t proc, d_string_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_const_string_t(crt_proc_t proc, d_const_string_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_uuid_t(crt_proc_t proc, uuid_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         second level pointer to data
 *
 * \return                      zero on success, negative value if error
 *
 * Notes:
 * 1) here pass in the 2nd level pointer of d_rank_list_t, to make it
 *    possible to set it to NULL when decoding.
 * 2) if the rank_list is non-NULL, caller should firstly duplicate it and pass
 *    the duplicated rank list's 2nd level pointer as parameter, because this
 *    function will internally free the memory when freeing the input or output.
 */
int
crt_proc_crt_rank_list_t(crt_proc_t proc, d_rank_list_t **data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_iov_t(crt_proc_t proc, d_iov_t *data);

/**
 * Local operation. Evict rank from the local membership list of grp.
 * \param grp [IN]		Must be a primary service group. Can be local or
 *				remote.  NULL means the local primary group.
 * \param rank [IN]		the rank within the \a grp to evict.
 *
 * \return			Zero on success, negative on error
 */
int
crt_rank_evict(crt_group_t *grp, d_rank_t rank);

typedef void
(*crt_event_cb) (d_rank_t rank, void *arg);

/**
 * This function registers an event handler for process failures. If the calling
 * process has not yet registered a PMIx event handler for the
 * PMIX_ERR_PROC_ABORTED event, this function will do so.  When the external RAS
 * notifies the current process with a process failure event, event_handler()
 * will be executed. Invocation of event_handler() does not mean the rank has
 * been evicted.
 *
 * \param event_handler [IN]	event handler to register
 * \param arg [IN]		arg to event_handler
 */
int
crt_register_event_cb(crt_event_cb event_handler, void *arg);

typedef void
(*crt_progress_cb) (crt_context_t ctx, void *arg);

/**
 * Register a callback function which will be called inside crt_progress()
 */
int
crt_register_progress_cb(crt_progress_cb cb, void *arg);

typedef void
(*crt_timeout_cb) (crt_context_t ctx, crt_rpc_t *rpc, void *arg);

int
crt_register_timeout_cb(crt_timeout_cb cb, void *arg);

typedef void
(*crt_eviction_cb) (crt_group_t *grp, d_rank_t rank, void *arg);

/**
 * Register a callback function which will be upon the completion of
 * crt_rank_evict().
 */
int
crt_register_eviction_cb(crt_eviction_cb cb, void *arg);

/**
 * Retrieve the PSR candidate list for \a tgt_grp.
 * \param tgt_grp [IN]		The remote group
 * \param psr_cand [OUT]	The PSR candidate list for \a tgt_grp. The first
 *				entry of psr_cand is the current PSR. The rest
 *				of the list are backup PSRs. User should call
 *				crt_rank_list_free() to free the memory after
 *				using it.
 * \return			0 on success, negative value on error.
 */
int
crt_lm_group_psr(crt_group_t *tgt_grp, d_rank_list_t **psr_cand);

/**
 * Initialize a lm_grp_priv struct for the remote group tgt_grp then append the
 * struct to a global list. This function sends RPCs to the default PSR to
 * lookup the URIs of the backup PSRs. This function also enables the resample
 * on timeout feature. This is a non-blocking function. completion_cb will be
 * called when crt_lm_attach() finishes. User needs to call crt_progress() to
 * make progress.
 *
 * \param tgt_grp [IN]		the remote group
 * \param completion_cb [IN]	callback which will be called when
 *				crt_lm_attach completes.
 * \param arg [IN]		user data pointer which is available in
 *				completion_cb. See the definition of
 *				struct crt_lm_attach_cb_info.
 *
 * \return			DER_SUCCESS on success, negative value on
 *				failure.
 */
int
crt_lm_attach(crt_group_t *tgt_grp, crt_lm_attach_cb_t completion_cb,
	      void *arg);

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
 *		MY_OPC_0 = (MY_BASE_OPC | MY_VER),
 *		MY_OPC_1 = (MY_BASE_OPC | MY_VER) + 1,
 *		MY_OPC_2 = (MY_BASE_OPC | MY_VER) + 2,
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
 *		&my_crf_1,
 *		&my_crf_2,
 *    };
 *
 * rpc handler array for member RPCs, one handler for each RPC:
 *	crt_rpc_cb_t hdlr[] = {
 *		my_hdlr_1,
 *		my_hdlr_2,
 *	};
 *
 * 3) define crt_proto_format.
 * struct crt_proto_format my_proto_fmt =
 *	DEFINE_CRT_PROTO_FMT("my-proto", ver, my_crf_array);
 *
 * which expands to:
 * {
 *	.cpf_name = "my-proto";
 *	.cpf_ver = ver;
 *	.cpf_crf = {
 *		&my_crf_1,
 *		&my_crf_2,
 *	 };
 *	 .cpf_hdlr = {
 *		my_hdlr_1,
 *		my_hdlr_2,
 *	 };
 * }
 *
 */

/**
 * Register a protocol. Can be called on a server or a client. This
 * function calls crt_rpc_register() to register each member RPC.
 *
 * \param base_opc [IN]		protocol base opcode
 * \param cpf [IN]		array of crt_proto_formats. Each
 *				crt_proto_format specifies a version
 *
 * \return			DER_SUCCESS on success, negative value
 *				on failure.
 */
int
crt_proto_register(crt_opcode_t base_opc, struct crt_proto_format *cpf);

/**
 * query tgt_ep if it has registered base_opc with version
 *
 * \param tgt_ep [IN]		the service rank to query
 * \param base_opc [IN]		the base opcode for the protocol
 * \param ver [IN]		array of protocol version
 * \param count [IN]		number of elements in ver
 * \param high_ver [OUT]	the highest protocol version supported by the
 *				target
 *
 * \return			DER_SUCCESS on success, negative value
 *				on failure.
 */
int
crt_proto_registered(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc, int *ver,
		     int count, int *high_ver);



#define crt_proc__Bool			crt_proc_bool
#define crt_proc_crt_rank_t		crt_proc_uint32_t
#define crt_proc_int			crt_proc_int32_t
#define crt_proc_crt_group_id_t		crt_proc_crt_string_t
#define crt_proc_crt_phy_addr_t		crt_proc_crt_string_t

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_API_H__ */
