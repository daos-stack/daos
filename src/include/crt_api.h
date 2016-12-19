/* Copyright (C) 2016 Intel Corporation
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
 * CaRT (Collective and RPC Transport) APIs.
 */

#ifndef __CRT_API_H__
#define __CRT_API_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <uuid/uuid.h>

#include <crt_types.h>
#include <crt_errno.h>
#include <crt_iv.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialize CRT transport layer.
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
 * Create CRT transport context.
 *
 * \param arg [IN]		input argument, now the only usage is passing
 *				argobots pool pointer. If user does not use
 *				argobots it should pass in NULL.
 * \param crt_ctx [OUT]		created CRT transport context
 *
 * \return			zero on success, negative value if error
 */
int
crt_context_create(void *arg, crt_context_t *crt_ctx);

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
 * Finalize CRT transport layer.
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
 * Create a RPC request.
 *
 * \param crt_ctx [IN]          CRT transport context
 * \param tgt_ep [IN]           RPC target endpoint
 * \param opc [IN]              RPC request opcode
 * \param req [OUT]             pointer to created request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the crt_req_create will internally allocate buffers for input and
 *        output parameters (crt_rpc_t::dr_input and crt_rpc_t::dr_output), and
 *        sets the appropriate size (crt_rpc_t::dr_input_size/dr_output_size).
 *        User needs not to allocate extra input/output buffers. After the
 *        request created, user can directly fill input parameters into
 *        crt_rpc_t::dr_input and send the RPC request.
 *        When the RPC request finishes executing, CRT internally frees the
 *        RPC request and the input/output buffers, so user needs not to call
 *        crt_req_destroy (no such API exported) or free the input/output
 *        buffers.
 *        Similarly, on the RPC server-side, when a RPC request received, CRT
 *        internally allocates input/output buffers as well, and internally
 *        frees those buffers when the reply is sent out. So in user's RPC
 *        handler it needs not to allocate extra input/output buffers, and also
 *        needs not to free input/output buffers in the completion callback of
 *        crt_reply_send.
 */
int
crt_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req);

/**
 * Add reference of the RPC request.
 *
 * The typical usage is that user needs to do some asynchronous operations in
 * RPC handler and does not want to block in RPC handler, then it can call this
 * API to hold a reference and return. Later when that asynchronous operation is
 * done, it can release the reference (/see crt_req_decref). CRT internally
 * frees the resource of the RPC request when its reference drops to zero.
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
 * Send a RPC request. In the case of sending failure, CRT internally destroy
 * the request \a req. In the case of succeed, the \a req will be internally
 * destroyed when the reply received. User needs not call crt_req_decref() to
 * destroy the request in either case.
 *
 * \param req [IN]              pointer to RPC request
 * \param complete_cb [IN]      completion callback, will be triggered when the
 *                              RPC request's reply arrives, in the context of
 *                              user's calling of crt_progress().
 * \param arg [IN]              arguments for the \a complete_cb
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the crt_rpc_t is exported to user, caller should fill the
 *        crt_rpc_t::dr_input and before sending the RPC request.
 *        \see crt_req_create.
 */
int
crt_req_send(crt_rpc_t *req, crt_cb_t complete_cb, void *arg);

/**
 * Send a RPC reply.
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
 * Abort a RPC request.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 *                              If the RPC has been sent out by crt_req_send,
 *                              the completion callback will be called with
 *                              CER_CANCELED set to crt_cb_info::dci_rc for a
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
crt_ep_abort(crt_endpoint_t ep);

/**
 * Dynamically register a RPC at client-side.
 *
 * \param opc [IN]              unique opcode for the RPC
 * \param drf [IN]		pointer to the request format, which
 *                              describe the request format and provide
 *                              callback to pack/unpack each items in the
 *                              request.
 * \return                      zero on success, negative value if error
 */
int
crt_rpc_register(crt_opcode_t opc, struct crt_req_format *drf);

/**
 * Dynamically register a RPC at server-side.
 *
 * \param opc [IN]              unique opcode for the RPC
 * \param drf [IN]		pointer to the request format, which
 *                              describe the request format and provide
 *                              callback to pack/unpack each items in the
 *                              request.
 * \param rpc_handler [IN]      pointer to RPC handler which will be triggered
 *                              when RPC request opcode associated with rpc_name
 *                              is received. Will return -CER_INVAL if pass in
 *                              NULL rpc_handler.
 *
 * \return                      zero on success, negative value if error
 */
int
crt_rpc_srv_register(crt_opcode_t opc, struct crt_req_format *drf,
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
crt_bulk_create(crt_context_t crt_ctx, crt_sg_list_t *sgl,
		crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl);

/**
 * Access local bulk handle to retrieve the sgl (segment list) associated
 * with it.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param sgl[IN/OUT]           pointer to buffer segment list
 *                              Caller should provide a valid sgl pointer, if
 *                              sgl->sg_nr.num is too small, -CER_TRUNC will be
 *                              returned and the needed number of iovs be set at
 *                              sgl->sg_nr.num_out.
 *                              On success, sgl->sg_nr.num_out will be set as
 *                              the actual number of iovs.
 *
 * \return                      zero on success, negative value if error
 */
int
crt_bulk_access(crt_bulk_t bulk_hdl, crt_sg_list_t *sgl);

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
 * Start a bulk transferring (inside a RPC handler).
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
crt_bulk_get_len(crt_bulk_t bulk_hdl, crt_size_t *bulk_len);

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
 *                              callback will be called with CER_CANCELED set to
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
 * Calculate the tree topology.
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
		return -CER_INVAL;

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
	int (*co_aggregate)(crt_rpc_t *source, crt_rpc_t *result, void *priv);
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
 * \param args [IN]		arguments pointer passed in for
 *				crt_group_destroy.
 * \param status [IN]		status code that indicates whether the group
 *				has been destroyed successfully or not.
 *				zero for success, negative value otherwise.
 *
 */
typedef int (*crt_grp_destroy_cb_t)(void *args, int status);

/*
 * Create CRT sub-group (a subset of the primary group).
 *
 * \param grp_id [IN]		unique group ID.
 * \param member_ranks [IN]	rank list of members for the group.
 *				Can-only create the group on the node which is
 *				one member of the group, otherwise -CER_OOG will
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
crt_group_create(crt_group_id_t grp_id, crt_rank_list_t *member_ranks,
		 bool populate_now, crt_grp_create_cb_t grp_create_cb,
		 void *priv);

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
 * Destroy a CRT group. Can either call this API or pass a special flag -
 * CRT_RPC_FLAG_GRP_DESTROY to a broadcast RPC to destroy the group.
 *
 * \param grp [IN]		group handle to be destroyed.
 * \param grp_destroy_cb [IN]	optional completion callback.
 * \param args [IN]		optional args for \a grp_destroy_cb.
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *args);

/*
 * Attach to a primary service group.
 *
 * By calling this API to attach to service primary group, and set
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
 */
int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);

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
 * Create collective RPC request. Can reuse the crt_req_send to broadcast it.
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
 *				CRT_RPC_FLAG_GRP_DESTROY to destroy the group
 *				when this bcast RPC finished.
 * \param tree_topo[IN]		tree topology for the collective propagation,
 *				can be calculated by crt_tree_topo().
 *				/see enum crt_tree_type, /see crt_tree_topo().
 * \param req [out]		created collective RPC request
 *
 * \return			zero on success, negative value if error
 */
int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     crt_rank_list_t *excluded_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req);

/**
 * Dynamically register a collective RPC.
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
 * 3) A NULL co_ops will be treated as invalid argument.
 *
 * \return			zero on success, negative value if error
 */
int
crt_corpc_register(crt_opcode_t opc, struct crt_req_format *drf,
		   crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops);

/**
 * Query the caller's rank number within group.
 *
 * \param grp [IN]		CRT group handle, NULL mean the primary/global
 *				group
 * \param rank[OUT]		result rank number
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_rank(crt_group_t *grp, crt_rank_t *rank);

/**
 * Query number of group members.
 *
 * \param grp [IN]		CRT group handle, NULL mean the local
 *				primary/global group
 * \param size[OUT]		size (total number of ranks) of the group
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
crt_proc_memcpy(crt_proc_t proc, void *data, crt_size_t data_size);

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
crt_proc_raw(crt_proc_t proc, void *buf, crt_size_t buf_size);

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
crt_proc_crt_string_t(crt_proc_t proc, crt_string_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_const_string_t(crt_proc_t proc, crt_const_string_t *data);

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
 * 1) here pass in the 2nd level pointer of crt_rank_list_t, to make it
 *    possible to set it to NULL when decoding.
 * 2) if the rank_list is non-NULL, caller should firstly duplicate it and pass
 *    the duplicated rank list's 2nd level pointer as parameter, because this
 *    function will internally free the memory when freeing the input or output.
 */
int
crt_proc_crt_rank_list_t(crt_proc_t proc, crt_rank_list_t **data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
crt_proc_crt_iov_t(crt_proc_t proc, crt_iov_t *data);

/**
 * Local operation. Delete rank from the local membership list of grp. This
 * function will fail if the version is lower than the current membership
 * version.
 */
int
crt_evict_rank(crt_group_t *grp, int version, crt_rank_t rank);

typedef void
(*crt_event_cb) (crt_rank_t rank, void *args);

/**
 * This function registers an event handler for a number of events specified by
 * codes and ncodes. When the external RAS notifies the current process with any
 * event code in -codes-, event_handler() will be executed.
 *
 * Example:
 * event_handler_01(crt_rank_t rank, void *args)
 * {
 *	fprintf("Received event code %d\n", rank);
 * }
 *
 * int main()
 * {
 *	int codes[] = {0, 1};
 *	int ncodes = 2;
 *
 *	crt_register_event_handler(codes, ncodes, event_handler_01);
 *	crt_init();
 * }
 *
 * The example above registers two event codes 0 and 1 to external RAS. When the
 * external RAS notifies the current process with an event code 0 or 1,
 * event_handler_01() be executed.
 *
 * \param codes [IN]		event codes to monitor
 * \param ncodes [IN]		number of codes to monitor
 * \param event_handler [IN]	event handler to register
 */
int
crt_register_event_handler(int codes[], size_t ncodes,
			      crt_event_cb event_handler);

typedef void
(*crt_progress_cb) (crt_context_t ctx, void *args);

/**
 * Register a callback function which will be called inside crt_progress()
 */
int
crt_register_progress_cb(crt_progress_cb cb, void *args);

typedef void
(*crt_timeout_cb) (crt_context_t ctx, crt_rpc_t *rpc, void *args);

int
crt_register_timeout_cb(crt_timeout_cb cb, void *args);

#define crt_proc__Bool			crt_proc_bool
#define crt_proc_crt_size_t		crt_proc_uint64_t
#define crt_proc_crt_off_t		crt_proc_uint64_t
#define crt_proc_crt_rank_t		crt_proc_uint32_t
#define crt_proc_crt_opcode_t		crt_proc_uint32_t
#define crt_proc_int			crt_proc_int32_t
#define crt_proc_crt_group_id_t		crt_proc_crt_string_t
#define crt_proc_crt_phy_addr_t		crt_proc_crt_string_t

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_API_H__ */
