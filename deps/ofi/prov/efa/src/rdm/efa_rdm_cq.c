/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa.h"
#include "efa_rdm_cq.h"
#include "ofi_util.h"
#include "efa_av.h"
#include "efa_cntr.h"
#include "efa_rdm_pke_cmd.h"
#include "efa_rdm_pke_utils.h"
#include "efa_rdm_pke_nonreq.h"
#include "efa_rdm_tracepoint.h"

static
const char *efa_rdm_cq_strerror(struct fid_cq *cq_fid, int prov_errno,
				const void *err_data, char *buf, size_t len)
{
	return err_data
		? (const char *) err_data
		: efa_strerror(prov_errno);
}

/**
 * @brief close a CQ of EFA RDM endpoint
 *
 * @param[in,out]	fid	fid of the CQ to be closed
 * @returns		0 on sucesss,
 * 			negative libfabric error code on error
 * @relates efa_rdm_cq
 */
static
int efa_rdm_cq_close(struct fid *fid)
{
	int ret, retv;
	struct efa_rdm_cq *cq;

	retv = 0;

	cq = container_of(fid, struct efa_rdm_cq, util_cq.cq_fid.fid);

	if (cq->ibv_cq.ibv_cq_ex) {
		ret = -ibv_destroy_cq(ibv_cq_ex_to_cq(cq->ibv_cq.ibv_cq_ex));
		if (ret) {
			EFA_WARN(FI_LOG_CQ, "Unable to close ibv cq: %s\n",
				fi_strerror(-ret));
			return ret;
		}
		cq->ibv_cq.ibv_cq_ex = NULL;
	}

	if (cq->shm_cq) {
		ret = fi_close(&cq->shm_cq->fid);
		if (ret) {
			EFA_WARN(FI_LOG_CQ, "Unable to close shm cq: %s\n", fi_strerror(-ret));
			retv = ret;
		}
	}

	ret = ofi_cq_cleanup(&cq->util_cq);
	if (ret)
		return ret;
	free(cq);
	return retv;
}

static struct fi_ops efa_rdm_cq_fi_ops = {
	.size = sizeof(struct fi_ops),
	.close = efa_rdm_cq_close,
	.bind = fi_no_bind,
	.control = fi_no_control,
	.ops_open = fi_no_ops_open,
};


#if HAVE_CAPS_UNSOLICITED_WRITE_RECV
/**
 * @brief Check whether a completion consumes recv buffer
 *
 * @param ibv_cq_ex extended ibv cq
 * @return true the wc consumes a recv buffer
 * @return false the wc doesn't consume a recv buffer
 */
static inline
bool efa_rdm_cq_wc_is_unsolicited(struct ibv_cq_ex *ibv_cq_ex)
{
	return efa_device_support_unsolicited_write_recv() ? efadv_wc_is_unsolicited(efadv_cq_from_ibv_cq_ex(ibv_cq_ex)) : false;
}

#else

static inline
bool efa_rdm_cq_wc_is_unsolicited(struct ibv_cq_ex *ibv_cq_ex)
{
	return false;
}

#endif
/**
 * @brief handle rdma-core CQ completion resulted from IBV_WRITE_WITH_IMM
 *
 * This function handles hardware-assisted RDMA writes with immediate data at
 * remote endpoint.  These do not have a packet context, nor do they have a
 * connid available.
 *
 * @param[in]		ibv_cq_ex	extended ibv cq
 * @param[in]		flags		flags (such as FI_REMOTE_CQ_DATA)
 * @param[in]		ep	        efa_rdm_ep
 * @param[in]		pkt_entry	packet entry
 */
static
void efa_rdm_cq_proc_ibv_recv_rdma_with_imm_completion(
						       struct ibv_cq_ex *ibv_cq_ex,
						       uint64_t flags,
						       struct efa_rdm_ep *ep,
						       struct efa_rdm_pke *pkt_entry
						       )
{
	struct util_cq *target_cq;
	int ret;
	fi_addr_t src_addr;
	struct efa_av *efa_av;
	uint32_t imm_data = ibv_wc_read_imm_data(ibv_cq_ex);
	uint32_t len = ibv_wc_read_byte_len(ibv_cq_ex);

	target_cq = ep->base_ep.util_ep.rx_cq;
	efa_av = ep->base_ep.av;

	if (ep->base_ep.util_ep.caps & FI_SOURCE) {
		src_addr = efa_av_reverse_lookup_rdm(efa_av,
						ibv_wc_read_slid(ibv_cq_ex),
						ibv_wc_read_src_qp(ibv_cq_ex),
						NULL);
		ret = ofi_cq_write_src(target_cq, NULL, flags, len, NULL, imm_data, 0, src_addr);
	} else {
		ret = ofi_cq_write(target_cq, NULL, flags, len, NULL, imm_data, 0);
	}

	if (OFI_UNLIKELY(ret)) {
		EFA_WARN(FI_LOG_CQ,
			"Unable to write a cq entry for remote for RECV_RDMA operation: %s\n",
			fi_strerror(-ret));
		efa_base_ep_write_eq_error(&ep->base_ep, -ret, FI_EFA_ERR_WRITE_SHM_CQ_ENTRY);
	}

	efa_cntr_report_rx_completion(&ep->base_ep.util_ep, flags);

	/**
	 * For unsolicited wc, pkt_entry can be NULL, so we can only
	 * access it for solicited wc.
	 */
	if (!efa_rdm_cq_wc_is_unsolicited(ibv_cq_ex)) {
		/**
		 * Recv with immediate will consume a pkt_entry, but the pkt is not
		 * filled, so free the pkt_entry and record we have one less posted
		 * packet now.
		 */
		assert(pkt_entry);
		ep->efa_rx_pkts_posted--;
		efa_rdm_pke_release_rx(pkt_entry);
	}
}

#if HAVE_EFADV_CQ_EX
/**
 * @brief Read peer raw address from EFA device and look up the peer address in AV.
 * This function should only be called if the peer AH is unknown.
 * @return Peer address, or FI_ADDR_NOTAVAIL if unavailable.
 */
static inline
fi_addr_t efa_rdm_cq_determine_peer_address_from_efadv(
						       struct ibv_cq_ex *ibv_cqx,
						       enum ibv_cq_ex_type ibv_cq_ex_type)
{
	struct efa_rdm_pke *pkt_entry;
	struct efa_rdm_ep *ep;
	struct efa_ep_addr efa_ep_addr = {0};
	fi_addr_t addr;
	union ibv_gid gid = {0};
	uint32_t *connid = NULL;

	if (ibv_cq_ex_type != EFADV_CQ) {
		/* EFA DV CQ is not supported. This could be due to old EFA kernel module versions. */
		return FI_ADDR_NOTAVAIL;
	}

	/* Attempt to read sgid from EFA firmware */
	if (efadv_wc_read_sgid(efadv_cq_from_ibv_cq_ex(ibv_cqx), &gid) < 0) {
		/* Return code is negative if the peer AH is known */
		return FI_ADDR_NOTAVAIL;
	}

	pkt_entry = (void *)(uintptr_t)ibv_cqx->wr_id;
	ep = pkt_entry->ep;
	assert(ep);

	connid = efa_rdm_pke_connid_ptr(pkt_entry);
	if (!connid) {
		return FI_ADDR_NOTAVAIL;
	}

	/*
	 * Use raw:qpn:connid as the key to lookup AV for peer's fi_addr
	 */
	memcpy(efa_ep_addr.raw, gid.raw, sizeof(efa_ep_addr.raw));
	efa_ep_addr.qpn = ibv_wc_read_src_qp(ibv_cqx);
	efa_ep_addr.qkey = *connid;
	addr = ofi_av_lookup_fi_addr(&ep->base_ep.av->util_av, &efa_ep_addr);
	if (addr != FI_ADDR_NOTAVAIL) {
		char gid_str_cdesc[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, gid.raw, gid_str_cdesc, INET6_ADDRSTRLEN);
		EFA_WARN(FI_LOG_AV,
				"Recovered peer fi_addr. [Raw]:[QPN]:[QKey] = [%s]:[%" PRIu16 "]:[%" PRIu32 "]\n",
				gid_str_cdesc, efa_ep_addr.qpn, efa_ep_addr.qkey);
	}

	return addr;
}

/**
 * @brief Determine peer address from ibv_cq_ex
 * Attempt to inject or determine peer address if not available. This usually
 * happens when the endpoint receives the first packet from a new peer.
 * There is an edge case for EFA endpoint - the device might lose the address
 * handle of a known peer due to a firmware bug and return FI_ADDR_NOTAVAIL.
 * The provider needs to look up the address using Raw address:QPN:QKey.
 * Note: This function introduces addtional overhead. It should only be called if
 * efa_av_lookup_address_rdm fails to find the peer address.
 * @param ep Pointer to RDM endpoint
 * @param ibv_cqx Pointer to CQ
 * @returns Peer address, or FI_ADDR_NOTAVAIL if unsuccessful.
 */
static inline fi_addr_t efa_rdm_cq_determine_addr_from_ibv_cq(struct ibv_cq_ex *ibv_cqx, enum ibv_cq_ex_type ibv_cq_ex_type)
{
	struct efa_rdm_pke *pkt_entry;
	fi_addr_t addr = FI_ADDR_NOTAVAIL;

	pkt_entry = (void *)(uintptr_t)ibv_cqx->wr_id;

	addr = efa_rdm_pke_determine_addr(pkt_entry);

	if (addr == FI_ADDR_NOTAVAIL) {
		addr = efa_rdm_cq_determine_peer_address_from_efadv(ibv_cqx, ibv_cq_ex_type);
	}

	return addr;
}
#else
/**
 * @brief Determine peer address from ibv_cq_ex
 * Attempt to inject peer address if not available. This usually
 * happens when the endpoint receives the first packet from a new peer.
 * Note: This function introduces addtional overhead. It should only be called if
 * efa_av_lookup_address_rdm fails to find the peer address.
 * @param ep Pointer to RDM endpoint
 * @param ibv_cqx Pointer to CQ
 * @returns Peer address, or FI_ADDR_NOTAVAIL if unsuccessful.
 */
static inline
fi_addr_t efa_rdm_cq_determine_addr_from_ibv_cq(struct ibv_cq_ex *ibv_cqx, enum ibv_cq_ex_type ibv_cq_ex_type)
{
	struct efa_rdm_pke *pkt_entry;

	pkt_entry = (void *)(uintptr_t)ibv_cqx->wr_id;

	return efa_rdm_pke_determine_addr(pkt_entry);
}
#endif

/**
 * @brief handle a received packet
 *
 * @param	ep[in,out]		endpoint
 * @param	pkt_entry[in,out]	received packet, will be released by this function
 */
static void efa_rdm_cq_handle_recv_completion(struct efa_ibv_cq *ibv_cq, struct efa_rdm_pke *pkt_entry, struct efa_rdm_ep *ep)
{
	int pkt_type;
	struct efa_rdm_peer *peer;
	struct efa_rdm_base_hdr *base_hdr;
	struct efa_av *efa_av = ep->base_ep.av;
	uint32_t imm_data = 0;
	bool has_imm_data = false;
	struct ibv_cq_ex *ibv_cq_ex = ibv_cq->ibv_cq_ex;

	if (pkt_entry->alloc_type == EFA_RDM_PKE_FROM_USER_RX_POOL) {
		assert(ep->user_rx_pkts_posted > 0);
		ep->user_rx_pkts_posted--;
	} else {
		assert(ep->efa_rx_pkts_posted > 0);
		ep->efa_rx_pkts_posted--;
	}

	pkt_entry->addr = efa_av_reverse_lookup_rdm(efa_av, ibv_wc_read_slid(ibv_cq_ex),
					ibv_wc_read_src_qp(ibv_cq_ex), pkt_entry);

	if (pkt_entry->addr == FI_ADDR_NOTAVAIL) {
		pkt_entry->addr = efa_rdm_cq_determine_addr_from_ibv_cq(ibv_cq_ex, ibv_cq->ibv_cq_ex_type);
	}

	pkt_entry->pkt_size = ibv_wc_read_byte_len(ibv_cq_ex);
	if (ibv_wc_read_wc_flags(ibv_cq_ex) & IBV_WC_WITH_IMM) {
		has_imm_data = true;
		imm_data = ibv_wc_read_imm_data(ibv_cq_ex);
	}

	/*
	 * Ignore packet if peer address cannot be determined. This ususally happens if
	 * we had prior communication with the peer, but
	 * application called fi_av_remove() to remove the address
	 * from address vector.
	 */
	if (pkt_entry->addr == FI_ADDR_NOTAVAIL) {
		EFA_WARN(FI_LOG_CQ,
			"Warning: ignoring a received packet from a removed address. packet type: %" PRIu8
			", packet flags: %x\n",
			efa_rdm_pke_get_base_hdr(pkt_entry)->type,
			efa_rdm_pke_get_base_hdr(pkt_entry)->flags);
		efa_rdm_pke_release_rx(pkt_entry);
		return;
	}

#if ENABLE_DEBUG
	dlist_remove(&pkt_entry->dbg_entry);
	dlist_insert_tail(&pkt_entry->dbg_entry, &ep->rx_pkt_list);
#ifdef ENABLE_EFA_RDM_PKE_DUMP
	efa_rdm_pke_print(pkt_entry, "Received");
#endif
#endif
	peer = efa_rdm_ep_get_peer(ep, pkt_entry->addr);
	assert(peer);
	if (peer->is_local) {
		/*
		 * This happens when the peer is on same instance, but chose to
		 * use EFA device to communicate with me. In this case, we respect
		 * that and will not use shm with the peer.
		 * TODO: decide whether to use shm through handshake packet.
		 */
		peer->is_local = 0;
	}

	efa_rdm_ep_post_handshake_or_queue(ep, peer);

	/**
	 * Data is already delivered to user posted pkt without pkt hdrs.
	 */
	if (pkt_entry->alloc_type == EFA_RDM_PKE_FROM_USER_RX_POOL) {
		assert(ep->base_ep.user_recv_qp);
		/* User recv pkts are only posted to the user recv qp */
		assert(ibv_wc_read_qp_num(ibv_cq->ibv_cq_ex) == ep->base_ep.user_recv_qp->qp_num);
		return efa_rdm_pke_proc_received_no_hdr(pkt_entry, has_imm_data, imm_data);
	}

	/* Proc receives with pkt hdrs (posted to ctrl QPs)*/
	base_hdr = efa_rdm_pke_get_base_hdr(pkt_entry);
	pkt_type = base_hdr->type;
	if (OFI_UNLIKELY(pkt_type >= EFA_RDM_EXTRA_REQ_PKT_END)) {
		EFA_WARN(FI_LOG_CQ,
			"Peer %d is requesting feature %d, which this EP does not support.\n",
			(int)pkt_entry->addr, base_hdr->type);

		assert(0 && "invalid REQ packet type");
		efa_base_ep_write_eq_error(&ep->base_ep, FI_EIO, FI_EFA_ERR_INVALID_PKT_TYPE);
		efa_rdm_pke_release_rx(pkt_entry);
		return;
	}

	/**
	 * When zero copy recv is turned on, the ep cannot
	 * handle rtm pkts delivered to the internal bounce buffer,
	 * because the user recv buffer has been posted to the other
	 * QP and we cannot cancel that.
	 */
	if (OFI_UNLIKELY(ep->use_zcpy_rx && efa_rdm_pkt_type_is_rtm(pkt_type))) {
		EFA_WARN(FI_LOG_CQ,
			"Invalid pkt type %d! Peer %d doesn't respect the request from this EP that"
			" RTM packets must be sent to the user recv QP.\n",
			base_hdr->type, (int)pkt_entry->addr);

		efa_base_ep_write_eq_error(&ep->base_ep, FI_EINVAL, FI_EFA_ERR_INVALID_PKT_TYPE);
		efa_rdm_pke_release_rx(pkt_entry);
		return;
	}

	efa_rdm_pke_proc_received(pkt_entry);
}


/**
 * @brief Get the vendor error code for an endpoint's CQ
 *
 * This function is essentially a wrapper for `ibv_wc_read_vendor_err()`; making
 * a best-effort attempt to promote the error code to a proprietary EFA
 * provider error code.
 *
 * @param[in]	ibv_cq_ex	IBV CQ
 * @return	EFA-specific error code
 * @sa		#EFA_PROV_ERRNOS
 *
 * @todo Currently, this only checks for unresponsive receiver
 * (#EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE) and attempts to promote it to
 * #FI_EFA_ERR_ESTABLISHED_RECV_UNRESP. This should be expanded to handle other
 * RDMA Core error codes (#EFA_IO_COMP_STATUSES) for the sake of more accurate
 * error reporting
 */
static int efa_rdm_cq_get_prov_errno(struct ibv_cq_ex *ibv_cq_ex) {
	uint32_t vendor_err = ibv_wc_read_vendor_err(ibv_cq_ex);
	struct efa_rdm_pke *pkt_entry = (void *) (uintptr_t) ibv_cq_ex->wr_id;
	struct efa_rdm_peer *peer;
	struct efa_rdm_ep *ep;

	if (OFI_LIKELY(pkt_entry && pkt_entry->addr)) {
		ep = pkt_entry->ep;
		peer = efa_rdm_ep_get_peer(ep, pkt_entry->addr);
	} else {
		return vendor_err;
	}

	switch (vendor_err) {
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE: {
		if (peer->flags & EFA_RDM_PEER_HANDSHAKE_RECEIVED)
			vendor_err = FI_EFA_ERR_ESTABLISHED_RECV_UNRESP;
		break;
	}
	default:
		break;
	}

	return vendor_err;
}

static int efa_rdm_cq_match_ep(struct dlist_entry *item, const void *ep)
{
	return (container_of(item, struct efa_rdm_ep, entry) == ep) ;
}

/**
 * @brief poll rdma-core cq and process the cq entry
 *
 * @param[in]	ep_poll	the RDM endpoint that polls ibv cq. Note this polling endpoint can be different
 * from the endpoint that the completed packet entry was posted from (pkt_entry->ep).
 * @param[in]	cqe_to_process	Max number of cq entry to poll and process. A negative number means to poll until cq empty
 */
void efa_rdm_cq_poll_ibv_cq(ssize_t cqe_to_process, struct efa_ibv_cq *ibv_cq)
{
	bool should_end_poll = false;
	/* Initialize an empty ibv_poll_cq_attr struct for ibv_start_poll.
	 * EFA expects .comp_mask = 0, or otherwise returns EINVAL.
	 */
	struct ibv_poll_cq_attr poll_cq_attr = {.comp_mask = 0};
	struct efa_rdm_pke *pkt_entry;
	ssize_t err;
	int opcode;
	size_t i = 0;
	int prov_errno;
	struct efa_rdm_ep *ep = NULL;
	struct fi_cq_err_entry err_entry;
	struct efa_rdm_cq *efa_rdm_cq;
	struct efa_domain *efa_domain;
	struct efa_qp *qp;
	struct dlist_entry rx_progressed_ep_list, *item;

	efa_rdm_cq = container_of(ibv_cq, struct efa_rdm_cq, ibv_cq);
	efa_domain = container_of(efa_rdm_cq->util_cq.domain, struct efa_domain, util_domain);
	dlist_init(&rx_progressed_ep_list);

	/* Call ibv_start_poll only once */
	err = ibv_start_poll(ibv_cq->ibv_cq_ex, &poll_cq_attr);
	should_end_poll = !err;

	while (!err) {
		pkt_entry = (void *)(uintptr_t)ibv_cq->ibv_cq_ex->wr_id;
		qp = efa_domain->qp_table[ibv_wc_read_qp_num(ibv_cq->ibv_cq_ex) & efa_domain->qp_table_sz_m1];
		ep = container_of(qp->base_ep, struct efa_rdm_ep, base_ep);
		efa_rdm_tracepoint(poll_cq, (size_t) ibv_cq->ibv_cq_ex->wr_id);
		opcode = ibv_wc_read_opcode(ibv_cq->ibv_cq_ex);
		if (ibv_cq->ibv_cq_ex->status) {
			prov_errno = efa_rdm_cq_get_prov_errno(ibv_cq->ibv_cq_ex);
			switch (opcode) {
			case IBV_WC_SEND: /* fall through */
			case IBV_WC_RDMA_WRITE: /* fall through */
			case IBV_WC_RDMA_READ:
				efa_rdm_pke_handle_tx_error(pkt_entry, prov_errno);
				break;
			case IBV_WC_RECV: /* fall through */
			case IBV_WC_RECV_RDMA_WITH_IMM:
				efa_rdm_pke_handle_rx_error(pkt_entry, prov_errno);
				break;
			default:
				EFA_WARN(FI_LOG_EP_CTRL, "Unhandled op code %d\n", opcode);
				assert(0 && "Unhandled op code");
			}
			break;
		}
		switch (opcode) {
		case IBV_WC_SEND:
#if ENABLE_DEBUG
			ep->send_comps++;
#endif
			efa_rdm_pke_handle_send_completion(pkt_entry);
			break;
		case IBV_WC_RECV:
			efa_rdm_cq_handle_recv_completion(ibv_cq, pkt_entry, ep);
#if ENABLE_DEBUG
			ep->recv_comps++;
#endif
			break;
		case IBV_WC_RDMA_READ:
		case IBV_WC_RDMA_WRITE:
			efa_rdm_pke_handle_rma_completion(pkt_entry);
			break;
		case IBV_WC_RECV_RDMA_WITH_IMM:
			efa_rdm_cq_proc_ibv_recv_rdma_with_imm_completion(
				ibv_cq->ibv_cq_ex,
				FI_REMOTE_CQ_DATA | FI_RMA | FI_REMOTE_WRITE,
				ep, pkt_entry);
			break;
		default:
			EFA_WARN(FI_LOG_EP_CTRL,
				"Unhandled cq type\n");
			assert(0 && "Unhandled cq type");
		}

		if (ep->efa_rx_pkts_to_post > 0 && !dlist_find_first_match(&rx_progressed_ep_list, &efa_rdm_cq_match_ep, ep))
			dlist_insert_tail(&ep->entry, &rx_progressed_ep_list);
		i++;
		if (i == cqe_to_process) {
			break;
		}

		/*
		 * ibv_next_poll MUST be call after the current WC is fully processed,
		 * which prevents later calls on ibv_cq_ex from reading the wrong WC.
		 */
		err = ibv_next_poll(ibv_cq->ibv_cq_ex);
	}

	if (err && err != ENOENT) {
		err = err > 0 ? err : -err;
		prov_errno = ibv_wc_read_vendor_err(ibv_cq->ibv_cq_ex);
		EFA_WARN(FI_LOG_CQ, "Unexpected error when polling ibv cq, err: %s (%zd) prov_errno: %s (%d)\n", fi_strerror(err), err, efa_strerror(prov_errno), prov_errno);
		efa_show_help(prov_errno);
		err_entry = (struct fi_cq_err_entry) {
			.err = err,
			.prov_errno = prov_errno,
			.op_context = NULL
		};
		ofi_cq_write_error(&efa_rdm_cq->util_cq, &err_entry);
	}

	if (should_end_poll)
		ibv_end_poll(ibv_cq->ibv_cq_ex);

	dlist_foreach(&rx_progressed_ep_list, item) {
		ep = container_of(item, struct efa_rdm_ep, entry);
		efa_rdm_ep_post_internal_rx_pkts(ep);
		dlist_remove(&ep->entry);
	}
	assert(dlist_empty(&rx_progressed_ep_list));

}

static ssize_t efa_rdm_cq_readfrom(struct fid_cq *cq_fid, void *buf, size_t count, fi_addr_t *src_addr)
{
	struct efa_rdm_cq *cq;
	ssize_t ret;
	struct efa_domain *domain;

	cq = container_of(cq_fid, struct efa_rdm_cq, util_cq.cq_fid.fid);

	domain = container_of(cq->util_cq.domain, struct efa_domain, util_domain);

	ofi_genlock_lock(&domain->srx_lock);

	if (cq->shm_cq) {
		fi_cq_read(cq->shm_cq, NULL, 0);

		/* 
		 * fi_cq_read(cq->shm_cq, NULL, 0) will progress shm ep and write
		 * completion to efa. Use ofi_cq_read_entries to get the number of
		 * shm completions without progressing efa ep again.
		 */
		ret = ofi_cq_read_entries(&cq->util_cq, buf, count, src_addr);

		if (ret > 0)
			goto out;
	}

	ret = ofi_cq_readfrom(&cq->util_cq.cq_fid, buf, count, src_addr);

out:
	ofi_genlock_unlock(&domain->srx_lock);

	return ret;
}

static struct fi_ops_cq efa_rdm_cq_ops = {
	.size = sizeof(struct fi_ops_cq),
	.read = ofi_cq_read,
	.readfrom = efa_rdm_cq_readfrom,
	.readerr = ofi_cq_readerr,
	.sread = fi_no_cq_sread,
	.sreadfrom = fi_no_cq_sreadfrom,
	.signal = fi_no_cq_signal,
	.strerror = efa_rdm_cq_strerror,
};

static void efa_rdm_cq_progress(struct util_cq *cq)
{
	struct dlist_entry *item;
	struct efa_rdm_cq *efa_rdm_cq;
	struct efa_ibv_cq_poll_list_entry *poll_list_entry;
	struct efa_domain *efa_domain;
	struct efa_rdm_ep *efa_rdm_ep;
	struct fid_list_entry *fid_entry;

	ofi_genlock_lock(&cq->ep_list_lock);
	efa_rdm_cq = container_of(cq, struct efa_rdm_cq, util_cq);
	efa_domain = container_of(efa_rdm_cq->util_cq.domain, struct efa_domain, util_domain);

	/**
	 * TODO: It's better to just post the initial batch of internal rx pkts during ep enable
	 * so we don't have to iterate cq->ep_list here.
	 * However, it is observed that doing that will hurt performance if application opens
	 * some idle endpoints and never poll completions for them. Move these initial posts to
	 * the first cq read call before having a long term fix.
	 */
	if (!efa_rdm_cq->initial_rx_to_all_eps_posted) {
		dlist_foreach(&cq->ep_list, item) {
			fid_entry = container_of(item, struct fid_list_entry, entry);
			efa_rdm_ep = container_of(fid_entry->fid, struct efa_rdm_ep, base_ep.util_ep.ep_fid.fid);
			efa_rdm_ep_post_internal_rx_pkts(efa_rdm_ep);
		}
		efa_rdm_cq->initial_rx_to_all_eps_posted = true;
	}

	dlist_foreach(&efa_rdm_cq->ibv_cq_poll_list, item) {
		poll_list_entry = container_of(item, struct efa_ibv_cq_poll_list_entry, entry);
		efa_rdm_cq_poll_ibv_cq(efa_env.efa_cq_read_size, poll_list_entry->cq);
	}
	efa_domain_progress_rdm_peers_and_queues(efa_domain);
	ofi_genlock_unlock(&cq->ep_list_lock);
}

/**
 * @brief create a CQ for EFA RDM provider
 *
 * Note that EFA RDM provider used the util_cq as its CQ
 *
 * @param[in]		domain		efa domain
 * @param[in]		attr		cq attribuite
 * @param[out]		cq_fid 		fid of the created cq
 * @param[in]		context 	currently EFA provider does not accept any context
 * @returns		0 on success
 * 			negative libfabric error code on error
 * @relates efa_rdm_cq
 */
int efa_rdm_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr,
		    struct fid_cq **cq_fid, void *context)
{
	int ret, retv;
	struct efa_rdm_cq *cq;
	struct efa_domain *efa_domain;
	struct fi_cq_attr shm_cq_attr = {0};
	struct fi_peer_cq_context peer_cq_context = {0};

	if (attr->wait_obj != FI_WAIT_NONE)
		return -FI_ENOSYS;

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return -FI_ENOMEM;

	efa_domain = container_of(domain, struct efa_domain,
				  util_domain.domain_fid);
	/* Override user cq size if it's less than recommended cq size */
	attr->size = MAX(efa_domain->rdm_cq_size, attr->size);

	dlist_init(&cq->ibv_cq_poll_list);
	cq->initial_rx_to_all_eps_posted = false;
	ret = ofi_cq_init(&efa_prov, domain, attr, &cq->util_cq,
			  &efa_rdm_cq_progress, context);

	if (ret)
		goto free;

	ret = efa_cq_ibv_cq_ex_open(attr, efa_domain->device->ibv_ctx, &cq->ibv_cq.ibv_cq_ex, &cq->ibv_cq.ibv_cq_ex_type);
	if (ret) {
		EFA_WARN(FI_LOG_CQ, "Unable to create extended CQ: %s\n", fi_strerror(ret));
		goto close_util_cq;
	}

	*cq_fid = &cq->util_cq.cq_fid;
	(*cq_fid)->fid.ops = &efa_rdm_cq_fi_ops;
	(*cq_fid)->ops = &efa_rdm_cq_ops;

	/* open shm cq as peer cq */
	if (efa_domain->shm_domain) {
		memcpy(&shm_cq_attr, attr, sizeof(*attr));
		/* Bind ep with shm provider's cq */
		shm_cq_attr.flags |= FI_PEER;
		peer_cq_context.size = sizeof(peer_cq_context);
		peer_cq_context.cq = cq->util_cq.peer_cq;
		ret = fi_cq_open(efa_domain->shm_domain, &shm_cq_attr,
				 &cq->shm_cq, &peer_cq_context);
		if (ret) {
			EFA_WARN(FI_LOG_CQ, "Unable to open shm cq: %s\n", fi_strerror(-ret));
			goto destroy_ibv_cq;
		}
	}

	return 0;
destroy_ibv_cq:
	retv = -ibv_destroy_cq(ibv_cq_ex_to_cq(cq->ibv_cq.ibv_cq_ex));
	if (retv)
		EFA_WARN(FI_LOG_CQ, "Unable to close ibv cq: %s\n",
			 fi_strerror(-retv));
close_util_cq:
	retv = ofi_cq_cleanup(&cq->util_cq);
	if (retv)
		EFA_WARN(FI_LOG_CQ, "Unable to close util cq: %s\n",
			 fi_strerror(-retv));
free:
	free(cq);
	return ret;
}
