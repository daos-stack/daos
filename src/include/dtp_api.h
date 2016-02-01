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
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport
 *
 * include/dtp_api.h
 *
 * Author: Xuezhao Liu <xuezhao.liu@intel.com>
 */
#ifndef __DTP_API_H__
#define __DTP_API_H__

#include <dtp_types.h>

/**
 * Initialize DAOS transport layer.
 *
 * \param addr[IN]              physical host address.
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
dtp_init(const dtp_phy_addr_t addr, bool server);

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
 * Finalize DAOS transport layer.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_finalize(void);

/**
 * Progress DAOS transport layer.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param timeout [IN]          how long is caller going to wait (millisecond)
 *                              if \a timeout > 0 when there is no operation to
 *                              progress.
 *                              it can also be DTP_PROGRESS_NOWAIT or
 *                              DTP_PROGRESS_MAXWAIT.
 * \param credits [IN/OUT]      input parameter as the caller specified number
 *                              of credits it wants to progress;
 *                              output parameter as the number of credits
 *                              remaining.
 * \param cond_cb [IN]          progress condition callback. DTP internally
 *                              calls this function, when it returns non-zero
 *                              then stops the progressing or waiting and
 *                              returns.
 *
 * Notes: one credit corresponds to one RPC request or one HG internal
 *        operation, currently mercury cannot ensure the precise number of
 *        requests progressed and does not know the number of credits remaining.
 *        And when HG_Progress blocks it possibly can-only be waked up by low
 *        level BMI/OFI etc, i.e. might cannot return when user change cond_cb's
 *        behavior.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_progress(dtp_context_t dtp_ctx, unsigned int timeout,
	     unsigned int *credits, dtp_progress_cond_cb_t cond_cb, void *arg);

/**
 * Create a RPC request.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param tgt_ep [IN]           RPC target endpoint
 * \param opc [IN]              RPC request opcode
 * \param req [OUT]             pointer to created request
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep, dtp_opcode_t opc,
	       dtp_rpc_t **req);

/**
 * Send a RPC request.
 *
 * \param req [IN]              pointer to RPC request
 * \param complete_cb [IN]      completion callback, will be triggered when the
 *                              RPC request's reply arrives, in the context of
 *                              user's calling of dtp_progress().
 * \param arg [IN]              arguments for the \a complete_cb
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the dtp_rpc_t is exported to user, caller should set the
 *        dtp_rpc_t::dr_input before sending the RPC request.
 */
int
dtp_req_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg);

/**
 * Send a RPC reply.
 *
 * \param req [IN]              pointer to RPC request
 * \param complete_cb [IN]      completion callback, will be triggered when the
 *                              RPC reply is sent out, in the context of user's
 *                              calling of dtp_progress().
 * \param arg [IN]              arguments for the \a complete_cb
 *
 * \return                      zero on success, negative value if error
 *
 * Notes: the dtp_rpc_t is exported to user, caller should set the
 *        dtp_rpc_t::dr_output before sending the RPC reply.
 */
int
dtp_reply_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg);

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
 * Dynamically register a RPC at client-side.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param rpc_name [IN]         the name string of the RPC
 * \param in_proc_cb [IN]       pointer to input proc function
 *                              the pack/unpack function of input parameters
 * \param out_proc_cb [IN]      pointer to output proc function
 *                              the pack/unpack function of output parameters
 * \param opc [OUT]             unique opcode associated to the rpc_name
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_rpc_reg(dtp_context_t dtp_ctx, const char *rpc_name,
	    dtp_proc_cb_t in_proc_cb, dtp_proc_cb_t out_proc_cb,
	    dtp_opcode_t *opc);

/**
 * Dynamically register a RPC at server-side.
 *
 * Compared to dtp_rpc_register, one more input argument needed at server-side:
 * \param rpc_handler [IN]      pointer to RPC handler which will be triggered
 *                              when RPC request opcode associated with rpc_name
 *                              is received.
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_rpc_srv_reg(dtp_context_t dtp_ctx, const char *rpc_name,
		dtp_proc_cb_t in_proc_cb, dtp_proc_cb_t out_proc_cb,
		dtp_rpc_cb_t rpc_handler, dtp_opcode_t *opc);

/**
 * Create a bulk handle
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param mem_sgs [IN]          pointer to buffer segment list
 * \param bulk_perm [IN]        bulk permission, \see dtp_bulk_perm_t
 * \param bulk_hdl [OUT]        created bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_create(dtp_context_t dtp_ctx, daos_sg_list_t *mem_sgs,
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
 * Start a bulk transferring
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param bulk_desc [IN]        pointer to bulk transferring descriptor
 * \param complete_cb [IN]      completion callback
 * \param arg [IN]              arguments for the \a complete_cb
 * \param opid [OUT]            bulk opid
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_transfer(dtp_context_t dtp_ctx, dtp_bulk_desc_t *bulk_desc,
		  dtp_bulk_cb_t complete_cb, void *arg, dtp_bulk_opid_t *opid);

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
dtp_bulk_get_sgnum(dtp_bulk_t bulk_hdl, unsigned long *bulk_sgnum);

/**
 * Get length required to pack the bulk handle.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param len [OUT]             length of buffer required
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_get_pack_len(dtp_bulk_t bulk_hdl, daos_size_t *len);

/**
 * Pack the bulk handle to a buffer.
 *
 * \param bulk_hdl [IN]         bulk handle
 * \param buf [IN/OUT]          buffer address to pack the bulk handle
 * \param buf_len [IN]          length of the buffer
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_pack(dtp_bulk_t bulk_hdl, void *buf, daos_size_t buf_len);

/**
 * Unpack the bulk handle from buffer.
 *
 * \param dtp_ctx [IN]          DAOS transport context
 * \param buf [IN]              buffer address to pack the bulk handle
 * \param buf_len [IN]          length of the buffer
 * \param bulk_hdl [OUT]        unpacked bulk handle
 *
 * \return                      zero on success, negative value if error
 */
int
dtp_bulk_unpack(dtp_context_t dtp_ctx, void *buf, daos_size_t buf_len,
		dtp_bulk_t *bulk_hdl);

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

#endif /* __DTP_API_H__ */
