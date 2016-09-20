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

#include <crt_errno.h>

#include <abt.h>

/**
 * Generic data type definition
 */
typedef uint64_t	crt_size_t;
typedef uint64_t	crt_off_t;

/** iovec for memory buffer */
typedef struct {
	/** buffer address */
	void	       *iov_buf;
	/** buffer length */
	crt_size_t	iov_buf_len;
	/** data length */
	crt_size_t	iov_len;
} crt_iov_t;

static inline void
crt_iov_set(crt_iov_t *iov, void *buf, crt_size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

/**
 * Server Identification & Addressing
 *
 * A server is identified by a group and a rank. A name (i.e. a string) is
 * associated with a group.
 */
typedef uint32_t	crt_rank_t;

typedef struct {
	/** input number */
	uint32_t	num;
	/** output/returned number */
	uint32_t	num_out;
} crt_nr_t;

typedef struct {
	/** number of ranks */
	crt_nr_t	 rl_nr;
	crt_rank_t	*rl_ranks;
} crt_rank_list_t;

typedef char		*crt_string_t;
typedef const char	*crt_const_string_t;

/* CRT uses a string as the group ID */
typedef crt_string_t	crt_group_id_t;
/* max length of the group ID string including the trailing '\0' */
#define CRT_GROUP_ID_MAX_LEN	(64)

typedef struct crt_group {
	/* the group ID of this group */
	crt_group_id_t		cg_grpid;
} crt_group_t;

/* transport endpoint identifier */
typedef struct {
	/* group handle, NULL means the primary group */
	crt_group_t	 *ep_grp;
	/* rank number within the group */
	crt_rank_t	 ep_rank;
	/* tag, now used as the context ID of the target rank */
	uint32_t	 ep_tag;
} crt_endpoint_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	crt_nr_t	 sg_nr;
	crt_iov_t	*sg_iovs;
} crt_sg_list_t;

/* CaRT context handle */
typedef void *crt_context_t;

/* Physical address string, e.g., "bmi+tcp://localhost:3344". */
typedef crt_string_t crt_phy_addr_t;
#define CRT_PHY_ADDR_ENV	"CRT_PHY_ADDR_STR"

/*
 * RPC is identified by opcode. All the opcodes with the highest 16 bits as 1
 * are reserved for internal usage, such as group maintenance etc. If user
 * defines its RPC using those reserved opcode, then undefined result is
 * expected.
 */
typedef uint32_t crt_opcode_t;
#define CRT_OPC_RESERVED_BITS	(0xFFFFU << 16)

/*
 * Check if the opcode is reserved by CRT internally.
 *
 * \param opc [IN]		opcode to be checked.
 *
 * \return			zero means legal opcode for user, non-zero means
 *				CRT internally reserved opcode.
 */
static inline int
crt_opcode_reserved(crt_opcode_t opc)
{
	return (opc & CRT_OPC_RESERVED_BITS) == CRT_OPC_RESERVED_BITS;
}

typedef void *crt_rpc_input_t;
typedef void *crt_rpc_output_t;

typedef void *crt_bulk_t; /* abstract bulk handle */

/**
 * max size of input/output parameters defined as 64M bytes, for larger length
 * the user should transfer by bulk.
 */
#define CRT_MAX_INPUT_SIZE	(0x4000000)
#define CRT_MAX_OUTPUT_SIZE	(0x4000000)

enum crt_rpc_flags {
	/*
	 * ignore timedout. Default behavior (no this flags) is resending
	 * request when timedout.
	 */
	CRT_RPC_FLAG_IGNORE_TIMEDOUT	= (1U << 0),
	/* destroy group when the bcast RPC finishes, only valid for corpc */
	CRT_CORPC_FLAG_GRP_DESTROY	= (1U << 31),
};

struct crt_rpc;

typedef int (*crt_req_callback_t)(struct crt_rpc *rpc);

/* Public RPC request/reply, exports to user */
typedef struct crt_rpc {
	crt_context_t		dr_ctx; /* CRT context of the RPC */
	crt_endpoint_t		dr_ep; /* endpoint ID */
	crt_opcode_t		dr_opc; /* opcode of the RPC */
	/* user passed in flags, \see enum crt_rpc_flags */
	enum crt_rpc_flags	dr_flags;
	crt_rpc_input_t		dr_input; /* input parameter struct */
	crt_rpc_output_t	dr_output; /* output parameter struct */
	crt_size_t		dr_input_size; /* size of input struct */
	crt_size_t		dr_output_size; /* size of output struct */
	/* optional bulk handle for collective RPC */
	crt_bulk_t		dr_co_bulk_hdl;
} crt_rpc_t;

/* Abstraction pack/unpack processor */
typedef void *crt_proc_t;
/* Proc callback for pack/unpack parameters */
typedef int (*crt_proc_cb_t)(crt_proc_t proc, void *data);

/* RPC message layout definitions */

enum dmf_flags {
	DMF_ARRAY_FLAG	= 1 << 0,
};

struct crt_msg_field {
	const char		*dmf_name;
	const uint32_t		dmf_flags;
	const uint32_t		dmf_size;
	crt_proc_cb_t		dmf_proc;
};

struct drf_field {
	uint32_t		drf_count;
	struct crt_msg_field	**drf_msg;
};

enum {
	CRT_IN = 0,
	CRT_OUT = 1,
};

struct crt_req_format {
	const char		*drf_name;
	uint32_t		drf_idx;
	struct drf_field	drf_fields[2];
};

struct crt_array {
	crt_size_t count;
	void	*arrays;
};

#define DEFINE_CRT_REQ_FMT_ARRAY(name, crt_in, in_size,		\
				 crt_out, out_size) {		\
	.drf_name	= name,					\
	.drf_fields	= {					\
		[CRT_IN] = {					\
			.drf_count = in_size,			\
			.drf_msg = crt_in,			\
		},						\
		[CRT_OUT] = {					\
			.drf_count = out_size,			\
			.drf_msg = crt_out			\
		}						\
	}							\
}

#define DEFINE_CRT_REQ_FMT(name, crt_in, crt_out)		\
DEFINE_CRT_REQ_FMT_ARRAY(name, crt_in, ARRAY_SIZE(crt_in),	\
			 crt_out, ARRAY_SIZE(crt_out))

#define DEFINE_CRT_MSG(name, flags, size, proc) {		\
	.dmf_name = (name),					\
	.dmf_flags = (flags),					\
	.dmf_size = (size),					\
	.dmf_proc = (crt_proc_cb_t)proc				\
}


/* Common request format type */
extern struct crt_msg_field DMF_UUID;
extern struct crt_msg_field DMF_GRP_ID;
extern struct crt_msg_field DMF_INT;
extern struct crt_msg_field DMF_UINT32;
extern struct crt_msg_field DMF_CRT_SIZE;
extern struct crt_msg_field DMF_UINT64;
extern struct crt_msg_field DMF_BULK;
extern struct crt_msg_field DMF_BOOL;
extern struct crt_msg_field DMF_STRING;
extern struct crt_msg_field DMF_PHY_ADDR;
extern struct crt_msg_field DMF_RANK;
extern struct crt_msg_field DMF_RANK_LIST;
extern struct crt_msg_field DMF_BULK_ARRAY;

extern struct crt_msg_field *crt_single_out_fields[];
struct crt_single_out {
	int	dso_ret;
};

typedef enum {
	CRT_BULK_PUT = 0x68,
	CRT_BULK_GET,
} crt_bulk_op_t;

typedef void *crt_bulk_opid_t;

typedef enum {
	/* read/write */
	CRT_BULK_RW = 0x88,
	/* read-only */
	CRT_BULK_RO,
	/* write-only */
	CRT_BULK_WO,
} crt_bulk_perm_t;

/* bulk transferring descriptor */
struct crt_bulk_desc {
	crt_rpc_t	*bd_rpc; /* original RPC request */
	crt_bulk_op_t	bd_bulk_op; /* CRT_BULK_PUT or CRT_BULK_GET */
	crt_bulk_t	bd_remote_hdl; /* remote bulk handle */
	crt_off_t	bd_remote_off; /* offset within remote bulk buffer */
	crt_bulk_t	bd_local_hdl; /* local bulk handle */
	crt_off_t	bd_local_off; /* offset within local bulk buffer */
	crt_size_t	bd_len; /* length of the bulk transferring */
};

struct crt_cb_info {
	crt_rpc_t		*dci_rpc; /* rpc struct */
	void			*dci_arg; /* User passed in arg */
	/* return code, will be set as:
	 * 0                     for succeed RPC request,
	 * -CER_TIMEDOUT         for timed out request,
	 * other negative value  for other possible failure. */
	int			dci_rc;
};

struct crt_bulk_cb_info {
	struct crt_bulk_desc	*bci_bulk_desc; /* bulk descriptor */
	void			*bci_arg; /* User passed in arg */
	int			bci_rc; /* return code */
};

/* server-side RPC handler */
typedef int (*crt_rpc_cb_t)(crt_rpc_t *rpc);

/**
 * completion callback for crt_req_send
 *
 * \param cb_info [IN]		pointer to call back info.
 *
 * \return			zero means success.
 *				in the case of RPC request timed out, user
 *				register complete_cb will be called (with
 *				cb_info->dci_rc set as -CER_TIMEDOUT).
 *				complete_cb returns -CER_AGAIN means resending
 *				the RPC request.
 */
typedef int (*crt_cb_t)(const struct crt_cb_info *cb_info);

/* completion callback for bulk transferring, i.e. crt_bulk_transfer() */
typedef int (*crt_bulk_cb_t)(const struct crt_bulk_cb_info *cb_info);

/**
 * Progress condition callback, /see crt_progress().
 *
 * \param arg [IN]              argument to cond_cb.
 *
 * \return			zero means continue progressing
 *				>0 means stopping progress and return success
 *				<0 means failure
 */
typedef int (*crt_progress_cond_cb_t)(void *args);

/**
 * Initialize CRT transport layer.
 *
 * \param server [IN]           zero means pure client, otherwise will enable
 *                              the server which listens for incoming connection
 *                              request.
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: crt_init() is a collective call which means every caller process
 *	  should make the call collectively, as now it will internally call
 *	  PMIx_Fence.
 */
int
crt_init(bool server);

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
 * Send rpc synchronously
 *
 * \param[IN] rpc	point to CRT request.
 * \param[IN] timeout	timeout (Milliseconds) to wait, if
 *                      timeout <= 0, it will wait infinitely.
 * \return		0 if rpc return successfuly.
 * \return		negative errno if sending fails or timeout.
 */
int
crt_sync_req(crt_rpc_t *rpc, uint64_t timeout);

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
 * \param arg		[IN]              argument to cond_cb.
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
	return rpc->dr_input;
};

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
	return rpc->dr_output;
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
crt_rpc_reg(crt_opcode_t opc, struct crt_req_format *drf);

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
crt_rpc_srv_reg(crt_opcode_t opc, struct crt_req_format *drf,
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
 *				the bulk (not implemented yet).
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

/*
 * Calculate the tree topology.
 *
 * \param tree_type [IN]	tree type
 * \param branch_ratio [IN]	branch ratio, be ignored for CRT_TREE_FLAT.
 *
 * \return			tree topology value on success,
 *				negative value if error.
 */
static inline int
crt_tree_topo(enum crt_tree_type tree_type, unsigned branch_ratio)
{
	if (tree_type < CRT_TREE_MIN || tree_type > CRT_TREE_MAX)
		return -CER_INVAL;

	return (tree_type << CRT_TREE_TYPE_SHIFT) |
	       (branch_ratio & ((1U << CRT_TREE_TYPE_SHIFT) - 1));
};

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
 * Lookup crt_group_t of one group ID. The group creation is initiated by one
 * node, after the group being populated user can query the crt_group_t on
 * other nodes.
 *
 * \param grp_id [IN]		unique group ID.
 *
 * \return			group handle on success, NULL if not found.
 */
crt_group_t *
crt_group_lookup(crt_group_id_t grp_id);

/*
 * Destroy a CRT group. Can either call this API or pass a special flag -
 * CRT_CORPC_FLAG_GRP_DESTROY to a broadcast RPC to destroy the group.
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
 * Create collective RPC request. Can reuse the crt_req_send to broadcast it.
 *
 * \param crt_ctx [IN]		CRT context
 * \param grp [IN]		CRT group for the collective RPC
 * \param excluded_ranks [IN]	optional excluded ranks, the RPC will be
 *				delivered to all members in the group except
 *				those in excluded_ranks.
 * \param opc [IN]		unique opcode for the RPC
 * \param co_bulk_hdl [IN]	collective bulk handle
 * \param priv [IN]		A private pointer associated with the request
 *				will be passed to crt_corpc_ops::co_aggregate as
 *				2nd parameter.
 * \param flags [IN]		collective RPC flags for example taking
 *				CRT_CORPC_FLAG_GRP_DESTROY to destroy the group
 *				when this bcast RPC finished.
 * \param tree_topo[IN]		tree topology for the collective propagation,
 *				can be calculated by crt_tree_topo().
 *				/see enum crt_tree_type,
 *				/see crt_tree_topo().
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
crt_corpc_reg(crt_opcode_t opc, struct crt_req_format *drf,
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
 * \param grp [IN]		CRT group handle, NULL mean the primary/global
 *				group
 * \param size[OUT]		result size (total number of ranks) of the group
 *
 * \return			zero on success, negative value if error
 */
int
crt_group_size(crt_group_t *grp, uint32_t *size);

/******************************************************************************
 * Proc data types, APIs and macros.
 ******************************************************************************/

typedef enum {
	CRT_ENCODE,  /* causes the type to be encoded into the stream */
	CRT_DECODE,  /* causes the type to be extracted from the stream */
	CRT_FREE     /* can be used to release the space allocated by an
		      * CRT_DECODE request */
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

#define crt_proc__Bool			crt_proc_bool
#define crt_proc_crt_size_t		crt_proc_uint64_t
#define crt_proc_crt_off_t		crt_proc_uint64_t
#define crt_proc_crt_rank_t		crt_proc_uint32_t
#define crt_proc_crt_opcode_t		crt_proc_uint32_t
#define crt_proc_int			crt_proc_int32_t
#define crt_proc_crt_group_id_t		crt_proc_crt_string_t
#define crt_proc_crt_phy_addr_t		crt_proc_crt_string_t

#endif /* __CRT_API_H__ */
