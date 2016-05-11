/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/**
 * DTP (DAOS TransPort) APIs.
 */
#ifndef __DAOS_TRANSPORT_H__
#define __DAOS_TRANSPORT_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_errno.h>

/* for proc macros */
#include <boost/preprocessor.hpp>

/* dtp context handle */
typedef void *dtp_context_t;

/* Physical address string, e.g., "bmi+tcp://localhost:3344". */
typedef char *dtp_phy_addr_t;
#define DTP_PHY_ADDR_ENV	"DTP_PHY_ADDR_STR"

typedef char *dtp_string_t;
typedef const char *dtp_const_string_t;

typedef uuid_t dtp_group_id_t;

/* all ranks in the group */
#define DTP_RANK_ALL		((daos_rank_t)-1)

/* transport endpoint identifier */
typedef struct {
	/* to be moved to:
	char		*ep_set; */
	dtp_group_id_t	 ep_grp_id;
	daos_rank_t	 ep_rank;
	uint32_t	 ep_tag; /* optional tag */
} dtp_endpoint_t;

typedef uint32_t dtp_opcode_t;
typedef uint32_t dtp_version_t;

typedef void *dtp_rpc_input_t;
typedef void *dtp_rpc_output_t;

/**
 * max size of input/output parameters defined as 64M bytes, for larger length
 * the user should transfer by bulk.
 */
#define DTP_MAX_INPUT_SIZE	(0x4000000)
#define DTP_MAX_OUTPUT_SIZE	(0x4000000)

enum dtp_rpc_flags {
	DTP_IGNORE_TIMEDOUT = 0x0001,
};

/* Public RPC request/reply, exports to user */
typedef struct dtp_rpc {
	dtp_context_t		dr_ctx; /* DTP context of the RPC */
	dtp_endpoint_t		dr_ep; /* endpoint ID */
	dtp_opcode_t		dr_opc; /* opcode of the RPC */
	/* user passed in flags, such as:
	 * DTP_IGNORE_TIMEDOUT		ignore timedout. Default behavior
	 *				(no this flags) is resending request
	 *				when timedout. */
	enum dtp_rpc_flags	dr_flags;
	dtp_rpc_input_t		dr_input; /* input parameter struct */
	dtp_rpc_output_t	dr_output; /* output parameter struct */
	daos_size_t		dr_input_size; /* size of input struct */
	daos_size_t		dr_output_size; /* size of output struct */
} dtp_rpc_t;

/* Abstraction pack/unpack processor */
typedef void *dtp_proc_t;
/* Proc callback for pack/unpack parameters */
typedef int (*dtp_proc_cb_t)(dtp_proc_t proc, void *data);

/* RPC message layout definitions */
struct dtp_msg_field {
	const char		*dmf_name;
	const uint32_t		dmf_flags;
	const uint32_t		dmf_size;
	dtp_proc_cb_t		dmf_proc;
};

struct drf_field {
	uint32_t		drf_count;
	struct dtp_msg_field	**drf_msg;
};

enum {
	DTP_IN = 0,
	DTP_OUT = 1,
};

struct dtp_req_format {
	const char		*drf_name;
	uint32_t		drf_idx;
	struct drf_field	drf_fields[2];
};

#define DEFINE_DTP_REQ_FMT(name, dtp_in, dtp_out) {		\
	.drf_name	= name,					\
	.drf_fields	= {					\
		[DTP_IN] = {					\
			.drf_count = ARRAY_SIZE(dtp_in),	\
			.drf_msg = dtp_in,			\
		},						\
		[DTP_OUT] = {					\
			.drf_count = ARRAY_SIZE(dtp_out),	\
			.drf_msg = dtp_out			\
		}						\
	}							\
}

#define DEFINE_DTP_MSG(name, flags, size, proc) {		\
	.dmf_name = (name),					\
	.dmf_flags = (flags),					\
	.dmf_size = (size),					\
	.dmf_proc = (dtp_proc_cb_t)proc				\
}

/* Common request format type */
extern struct dtp_msg_field DMF_UUID;
extern struct dtp_msg_field DMF_INT;
extern struct dtp_msg_field DMF_UINT32;
extern struct dtp_msg_field DMF_DAOS_SIZE;
extern struct dtp_msg_field DMF_UINT64;
extern struct dtp_msg_field DMF_BULK;
extern struct dtp_msg_field DMF_BOOL;
extern struct dtp_msg_field DMF_STRING;
extern struct dtp_msg_field DMF_RANK_LIST;

typedef void *dtp_bulk_t; /* abstract bulk handle */

typedef enum {
	DTP_BULK_PUT = 0x68,
	DTP_BULK_GET,
} dtp_bulk_op_t;

typedef void *dtp_bulk_opid_t;

typedef enum {
	/* read/write */
	DTP_BULK_RW = 0x88,
	/* read-only */
	DTP_BULK_RO,
	/* write-only */
	DTP_BULK_WO,
} dtp_bulk_perm_t;

/* bulk transferring descriptor */
struct dtp_bulk_desc {
	dtp_rpc_t	*bd_rpc; /* original RPC request */
	dtp_bulk_op_t	bd_bulk_op; /* DTP_BULK_PUT or DTP_BULK_GET */
	dtp_bulk_t	bd_remote_hdl; /* remote bulk handle */
	dtp_bulk_t	bd_local_hdl; /* local bulk handle */
	daos_size_t	bd_len; /* length of the bulk transferring */
};

struct dtp_cb_info {
	dtp_rpc_t		*dci_rpc; /* rpc struct */
	void			*dci_arg; /* User passed in arg */
	/* return code, will be set as:
	 * 0                     for succeed RPC request,
	 * -DER_TIMEDOUT         for timed out request,
	 * other negative value  for other possible failure. */
	int		dci_rc;
};

struct dtp_bulk_cb_info {
	struct dtp_bulk_desc	*bci_bulk_desc; /* bulk descriptor */
	void			*bci_arg; /* User passed in arg */
	int			bci_rc; /* return code */
};

/* server-side RPC handler */
typedef int (*dtp_rpc_cb_t)(dtp_rpc_t *rpc);

/**
 * completion callback for dtp_req_send
 *
 * \param cb_info [IN]		pointer to call back info.
 *
 * \return			zero means success.
 *				in the case of RPC request timed out, user
 *				register complete_cb will be called (with
 *				cb_info->dci_rc set as -DER_TIMEDOUT).
 *				complete_cb returns -DER_AGAIN means resending
 *				the RPC request.
 */
typedef int (*dtp_cb_t)(const struct dtp_cb_info *cb_info);

/* completion callback for bulk transferring, i.e. dtp_bulk_transfer() */
typedef int (*dtp_bulk_cb_t)(const struct dtp_bulk_cb_info *cb_info);

/**
 * Progress condition callback, /see dtp_progress().
 *
 * \param arg [IN]              argument to cond_cb.
 *
 * \return			zero means continue progressing
 *				>0 means stopping progress and return success
 *				<0 means failure
 */
typedef int (*dtp_progress_cond_cb_t)(void *args);

/**
 * Initialize DAOS transport layer.
 *
 * \param server [IN]           zero means pure client, otherwise will enable
 *                              the server which listens for incoming connection
 *                              request.
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: upper layer may don't know the addr... can change it after the
 *        bootstrapping mechanism be more clear
 */
int
dtp_init(bool server);

/**
 * Create DAOS transport context.
 *
 * \param arg [IN]              input argument
 * \param dtp_ctx [OUT]         created DAOS transport context
 *
 * Notes: related with core affinity, currently define the argument as a void *.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_context_create(void *arg, dtp_context_t *dtp_ctx);

/**
 * Destroy DAOS transport context.
 *
 * \param dtp_ctx [IN]          DAOS transport context to be destroyed
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
dtp_context_destroy(dtp_context_t dtp_ctx, int force);

/**
 * Query the index of the transport context, the index value ranges in
 * [0, ctx_num - 1].
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param ctx_idx [OUT]         pointer to the returned index
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_context_idx(dtp_context_t dtp_ctx, int *ctx_idx);

/**
 * Query the total number of the transport contexts.
 *
 * \param ctx_num [OUT]         pointer to the returned number
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_context_num(int *ctx_num);

/**
 * Finalize DAOS transport layer.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_finalize(void);

/**
 * Send rpc synchronously
 *
 * \param[IN] rpc	point to DTP request.
 * \param[IN] timeout	timeout (Milliseconds) to wait, if
 *                      timeout <= 0, it will wait infinitely.
 * \return		0 if rpc return successfuly.
 * \return		negative errno if sending fails or timeout.
 */
int
dtp_sync_req(dtp_rpc_t *rpc, uint64_t timeout);

/**
 * Progress DAOS transport layer.
 *
 * \param dtp_ctx	[IN]	DAOS transport context
 * \param timeout	[IN]	how long is caller going to wait (micro-second)
 *				if \a timeout > 0 when there is no operation to
 *				progress. Can return when one or more operation
 *				progressed.
 *				zero means no waiting and -1 waits indefinitely.
 * \param cond_cb	[IN]	optional progress condition callback.
 *				DTP internally calls this function, when it
 *				returns non-zero then stops the progressing or
 *				waiting and returns.
 * \param arg		[IN]              argument to cond_cb.
 *
 * \return			zero on success, negative value if error
 */
int
dtp_progress(dtp_context_t dtp_ctx, int64_t timeout,
	     dtp_progress_cond_cb_t cond_cb, void *arg);

/**
 * Query the caller's rank number within group.
 *
 * \param grp_id [IN]           DAOS group id
 * \param rank[OUT]             result rank number
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_group_rank(dtp_group_id_t grp_id, daos_rank_t *rank);

/**
 * Query number of group members.
 *
 * \param grp_id [IN]           DAOS group id
 * \param size[OUT]             result size (total number of ranks) of the group
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_group_size(dtp_group_id_t grp_id, uint32_t *size);

/**
 * Create a RPC request.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param tgt_ep [IN]           RPC target endpoint
 * \param opc [IN]              RPC request opcode
 * \param req [OUT]             pointer to created request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the dtp_req_create will internally allocate buffers for input and
 *        output parameters (dtp_rpc_t::dr_input and dtp_rpc_t::dr_output), and
 *        sets the appropriate size (dtp_rpc_t::dr_input_size/dr_output_size).
 *        User needs not to allocate extra input/output buffers. After the
 *        request created, user can directly fill input parameters into
 *        dtp_rpc_t::dr_input and send the RPC request.
 *        When the RPC request finishes executing, DTP internally frees the
 *        RPC request and the input/output buffers, so user needs not to call
 *        dtp_req_destroy (no such API exported) or free the input/output
 *        buffers.
 *        Similarly, on the RPC server-side, when a RPC request received, DTP
 *        internally allocates input/output buffers as well, and internally
 *        frees those buffers when the reply is sent out. So in user's RPC
 *        handler it needs not to allocate extra input/output buffers, and also
 *        needs not to free input/output buffers in the completion callback of
 *        dtp_reply_send.
 */
int
dtp_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep, dtp_opcode_t opc,
	       dtp_rpc_t **req);

/**
 * Add reference of the RPC request.
 *
 * The typical usage is that user needs to do some asynchronous operations in
 * RPC handler and does not want to block in RPC handler, then it can call this
 * API to hold a reference and return. Later when that asynchronous operation is
 * done, it can release the reference (/see dtp_req_decref). DTP internally
 * frees the resource of the RPC request when its reference drops to zero.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_req_addref(dtp_rpc_t *req);

/**
 * Decrease reference of the RPC request. /see dtp_req_addref.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_req_decref(dtp_rpc_t *req);

/**
 * Send a RPC request. In the case of sending failure, DTP internally destroy
 * the request \a req. In the case of succeed, the \a req will be internally
 * destroyed when the reply received. User needs not call dtp_req_decref() to
 * destroy the request in either case.
 *
 * \param req [IN]              pointer to RPC request
 * \param complete_cb [IN]      completion callback, will be triggered when the
 *                              RPC request's reply arrives, in the context of
 *                              user's calling of dtp_progress().
 * \param arg [IN]              arguments for the \a complete_cb
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the dtp_rpc_t is exported to user, caller should fill the
 *        dtp_rpc_t::dr_input and before sending the RPC request.
 *        \see dtp_req_create.
 */
int
dtp_req_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg);

/**
 * Send a RPC reply.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the dtp_rpc_t is exported to user, caller should fill the
 *        dtp_rpc_t::dr_output before sending the RPC reply.
 *        \see dtp_req_create.
 */
int
dtp_reply_send(dtp_rpc_t *req);

/**
 * Return request buffer
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      pointer to request buffer
 */
static inline void *
dtp_req_get(dtp_rpc_t *rpc)
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
dtp_reply_get(dtp_rpc_t *rpc)
{
	return rpc->dr_output;
}

/**
 * Abort a RPC request.
 *
 * \param req [IN]              pointer to RPC request
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: now HG_Cancel() is not fully implemented.
 */
int
dtp_req_abort(dtp_rpc_t *req);

/**
 * Abort all in-flight RPC requests targeting to an endpoint.
 *
 * \param ep [IN]		endpoint address
 *
 * \return			zero on success, negative value if error
 *
 * Notes: now HG_Cancel() is not fully implemented.
 */
int
dtp_ep_abort(dtp_endpoint_t ep);

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
dtp_rpc_reg(dtp_opcode_t opc, struct dtp_req_format *drf);

/**
 * Dynamically register a RPC at server-side.
 *
 * Compared to dtp_rpc_register, one more input argument needed at server-side:
 * \param opc [IN]              unique opcode for the RPC
 * \param drf [IN]		pointer to the request format, which
 *                              describe the request format and provide
 *                              callback to pack/unpack each items in the
 *                              request.
 * \param rpc_handler [IN]      pointer to RPC handler which will be triggered
 *                              when RPC request opcode associated with rpc_name
 *                              is received.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_rpc_srv_reg(dtp_opcode_t opc, struct dtp_req_format *drf,
		dtp_rpc_cb_t rpc_handler);

/**
 * Create a bulk handle
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param sgl [IN]              pointer to buffer segment list
 * \param bulk_perm [IN]        bulk permission, \see dtp_bulk_perm_t
 * \param bulk_hdl [OUT]        created bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_create(dtp_context_t dtp_ctx, daos_sg_list_t *sgl,
		dtp_bulk_perm_t bulk_perm, dtp_bulk_t *bulk_hdl);

/**
 * Free a bulk handle
 *
 * \param bulk_hdl [IN]         bulk handle to be freed
 *
 * \return                      zero on success, negative value if error
 */
int dtp_bulk_free(dtp_bulk_t bulk_hdl);

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
dtp_bulk_transfer(struct dtp_bulk_desc *bulk_desc, dtp_bulk_cb_t complete_cb,
		  void *arg, dtp_bulk_opid_t *opid);

/**
 * Get length (number of bytes) of data abstracted by bulk handle.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param bulk_len [OUT]        length of the data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_get_len(dtp_bulk_t bulk_hdl, daos_size_t *bulk_len);

/**
 * Get the number of segments of data abstracted by bulk handle.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param bulk_sgnum [OUT]      number of segments
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_get_sgnum(dtp_bulk_t bulk_hdl, unsigned int *bulk_sgnum);

/*
 * Abort a bulk transferring.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param opid [IN]             bulk opid
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: now HG_Bulk_cancel() is not implemented by mercury.
 */
int
dtp_bulk_abort(dtp_context_t dtp_ctx, dtp_bulk_opid_t opid);


/******************************************************************************
 * Proc data types, APIs and macros.
 ******************************************************************************/

typedef enum {
	DTP_ENCODE,  /* causes the type to be encoded into the stream */
	DTP_DECODE,  /* causes the type to be extracted from the stream */
	DTP_FREE     /* can be used to release the space allocated by an
		      * DTP_DECODE request */
} dtp_proc_op_t;

/**
 * Get the operation type associated to the proc processor.
 *
 * \param proc [IN]             abstract processor object
 * \param proc_op [OUT]         returned proc operation type
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_get_op(dtp_proc_t proc, dtp_proc_op_t *proc_op);

/**
 * Base proc routine using memcpy().
 * Only uses memcpy() / use dtp_proc_raw() for encoding raw buffers.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 * \param data_size [IN]        data size
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_memcpy(dtp_proc_t proc, void *data, daos_size_t data_size);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_int8_t(dtp_proc_t proc, int8_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_uint8_t(dtp_proc_t proc, uint8_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_int16_t(dtp_proc_t proc, int16_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_uint16_t(dtp_proc_t proc, uint16_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_int32_t(dtp_proc_t proc, int32_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_uint32_t(dtp_proc_t proc, uint32_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_int64_t(dtp_proc_t proc, int64_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_uint64_t(dtp_proc_t proc, uint64_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_bool(dtp_proc_t proc, bool *data);

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
dtp_proc_raw(dtp_proc_t proc, void *buf, daos_size_t buf_size);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param bulk_hdl [IN/OUT]     pointer to bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_dtp_bulk_t(dtp_proc_t proc, dtp_bulk_t *bulk_hdl);

#define dtp_proc__Bool			dtp_proc_bool
#define dtp_proc_daos_size_t		dtp_proc_uint64_t
#define dtp_proc_daos_off_t		dtp_proc_uint64_t
#define dtp_proc_daos_rank_t		dtp_proc_uint32_t
#define dtp_proc_dtp_opcode_t		dtp_proc_uint32_t
#define dtp_proc_int			dtp_proc_int32_t
#define dtp_proc_dtp_group_id_t		dtp_proc_uuid_t

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_dtp_string_t(dtp_proc_t proc, dtp_string_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_dtp_const_string_t(dtp_proc_t proc, dtp_const_string_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         pointer to data
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_proc_uuid_t(dtp_proc_t proc, uuid_t *data);

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param data [IN/OUT]         second level pointer to data
 *
 * \return                      zero on success, negative value if error
 *
 * Notes:
 * 1) here pass in the 2nd level pointer of daos_rank_list_t, to make it
 *    possible to set it to NULL when decoding.
 * 2) if the rank_list is non-NULL, caller should firstly duplicate it and pass
 *    the duplicated rank list's 2nd level pointer as parameter, because this
 *    function will internally free the memory when freeing the input or output.
 */
int
dtp_proc_daos_rank_list_t(dtp_proc_t proc, daos_rank_list_t **data);


#endif /* __DAOS_TRANSPORT_H__ */
