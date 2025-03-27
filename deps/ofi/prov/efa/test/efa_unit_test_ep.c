/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"
#include "rdm/efa_rdm_cq.h"
#include "efa_rdm_pke_utils.h"

/**
 * @brief Verify the EFA RDM endpoint correctly parses the host id string
 * @param[in]	state		cmocka state variable
 * @param[in]	file_exists	Toggle whether the host id file exists
 * @param[in]	raw_id		The host id string that is written in the host id file.
 * @param[in]	expect_id	Expected parsed host id integer
 */
void test_efa_rdm_ep_host_id(struct efa_resource **state, bool file_exists, char *raw_id, uint64_t expect_id)
{
	int fd = -1;
	ssize_t written_len;
	char host_id_file[] = "XXXXXXXXXX";
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;

	efa_env.host_id_file = NULL;

	if (file_exists) {
		fd = mkstemp(host_id_file);
		if (fd < 0) {
			fail();
		}

		written_len = write(fd, raw_id, strlen(raw_id));
		if (written_len != strlen(raw_id)) {
			close(fd);
			fail();
		}

		efa_env.host_id_file = host_id_file;
	}

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

	/* Remove the temporary file */
	if (efa_env.host_id_file) {
		unlink(efa_env.host_id_file);
		close(fd);
	}

	assert_int_equal(efa_rdm_ep->host_id, expect_id);
}

/**
 * @brief Verify the EFA RDM endpoint ignores non-existent host id file
 */
void test_efa_rdm_ep_ignore_missing_host_id_file(struct efa_resource **state)
{
	test_efa_rdm_ep_host_id(state, false, NULL, 0);
}

/**
 * @brief Verify the EFA RDM endpoint correctly parses a valid host id string
 */
void test_efa_rdm_ep_has_valid_host_id(struct efa_resource **state)
{
	test_efa_rdm_ep_host_id(state, true, "i-01234567812345678", 0x1234567812345678);
}

/**
 * @brief Verify the EFA RDM endpoint ignores a short (<16 char) host id string
 */
void test_efa_rdm_ep_ignore_short_host_id(struct efa_resource **state)
{
	test_efa_rdm_ep_host_id(state, true, "i-012345678", 0);
}

/**
 * @brief Verify the EFA RDM endpoint ignores a malformatted host id string
 */
void test_efa_rdm_ep_ignore_non_hex_host_id(struct efa_resource **state)
{
	test_efa_rdm_ep_host_id(state, true, "i-0abcdefghabcdefgh", 0);
}

#if HAVE_EFADV_CQ_EX
/**
 * @brief Verify the EFA RDM endpoint correctly processes and responds to a handshake packet
 *	Upon receiving a handshake packet from a new remote peer, the endpoint should inspect
 *	the packet header and set the peer host id if HOST_ID_HDR is turned on.
 *	Then the endpoint should respond with a handshake packet, and include the local host id
 *	if and only if it is non-zero.
 *
 * @param[in]	state		cmocka state variable
 * @param[in]	local_host_id	The local host id
 * @param[in]	peer_host_id	The remote peer host id
 * @param[in]	include_connid	Toggle whether connid should be included in handshake packet
 */
void test_efa_rdm_ep_handshake_exchange_host_id(struct efa_resource **state, uint64_t local_host_id, uint64_t peer_host_id, bool include_connid)
{
	fi_addr_t peer_addr = 0;
	int cq_read_recv_ret, cq_read_send_ret;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(raw_addr);
	struct efa_rdm_peer *peer;
	struct efa_resource *resource = *state;
	struct efa_unit_test_handshake_pkt_attr pkt_attr = {0};
	struct fi_cq_data_entry cq_entry;
	struct ibv_qp_ex *ibv_qp;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_pke *pkt_entry;
	uint64_t actual_peer_host_id = UINT64_MAX;
	int ret;
	struct efa_rdm_cq *efa_rdm_cq;

	g_efa_unit_test_mocks.local_host_id = local_host_id;
	g_efa_unit_test_mocks.peer_host_id = peer_host_id;

	assert_false(actual_peer_host_id == g_efa_unit_test_mocks.peer_host_id);

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);

	efa_rdm_ep->host_id = g_efa_unit_test_mocks.local_host_id;
	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		ret = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(ret, 0);
		efa_rdm_ep->shm_ep = NULL;
	}


	/* Create and register a fake peer */
	assert_int_equal(fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len), 0);
	raw_addr.qpn = 0;
	raw_addr.qkey = 0x1234;

	assert_int_equal(fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL), 1);

	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	assert_non_null(peer);
	/* Peer host id is uninitialized before handshake */
	assert_int_equal(peer->host_id, 0);
	assert_int_not_equal(peer->flags & EFA_RDM_PEER_HANDSHAKE_SENT, EFA_RDM_PEER_HANDSHAKE_SENT);

	/*
	 * The rx pkt entry should only be allocated and posted by the progress engine.
	 * However, to mock a receive completion, we have to allocate an rx entry
	 * and modify it out of band. The proess engine grow the rx pool in the first
	 * call and set efa_rdm_ep->efa_rx_pkts_posted as the rx pool size. Here we
	 * follow the progress engine to set the efa_rx_pkts_posted counter manually
	 * TODO: modify the rx pkt as part of the ibv cq poll mock so we don't have to
	 * allocate pkt entry and hack the pkt counters.
	 */
	pkt_entry = efa_rdm_pke_alloc(efa_rdm_ep, efa_rdm_ep->efa_rx_pkt_pool, EFA_RDM_PKE_FROM_EFA_RX_POOL);
	assert_non_null(pkt_entry);
	efa_rdm_ep->efa_rx_pkts_posted = efa_rdm_ep_get_rx_pool_size(efa_rdm_ep);

	pkt_attr.connid = include_connid ? raw_addr.qkey : 0;
	pkt_attr.host_id = g_efa_unit_test_mocks.peer_host_id;
	pkt_attr.device_version = 0xefa0;
	efa_unit_test_handshake_pkt_construct(pkt_entry, &pkt_attr);

	ibv_qp = efa_rdm_ep->base_ep.qp->ibv_qp_ex;
	ibv_qp->wr_start = &efa_mock_ibv_wr_start_no_op;
	/* this mock will save the send work request (wr) in a global array */
	ibv_qp->wr_send = &efa_mock_ibv_wr_send_verify_handshake_pkt_local_host_id_and_save_wr;
	ibv_qp->wr_set_inline_data_list = &efa_mock_ibv_wr_set_inline_data_list_no_op;
	ibv_qp->wr_set_sge_list = &efa_mock_ibv_wr_set_sge_list_no_op;
	ibv_qp->wr_set_ud_addr = &efa_mock_ibv_wr_set_ud_addr_no_op;
	ibv_qp->wr_complete = &efa_mock_ibv_wr_complete_no_op;
	expect_function_call(efa_mock_ibv_wr_send_verify_handshake_pkt_local_host_id_and_save_wr);

	/* Setup CQ */
	efa_rdm_cq->ibv_cq.ibv_cq_ex->end_poll = &efa_mock_ibv_end_poll_check_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->next_poll = &efa_mock_ibv_next_poll_check_function_called_and_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_byte_len = &efa_mock_ibv_read_byte_len_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_opcode = &efa_mock_ibv_read_opcode_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_slid = &efa_mock_ibv_read_slid_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_src_qp = &efa_mock_ibv_read_src_qp_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_qp_num = &efa_mock_ibv_read_qp_num_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_wc_flags = &efa_mock_ibv_read_wc_flags_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_vendor_err = &efa_mock_ibv_read_vendor_err_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->start_poll = &efa_mock_ibv_start_poll_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->status = IBV_WC_SUCCESS;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->wr_id = (uintptr_t)pkt_entry;
	expect_function_call(efa_mock_ibv_next_poll_check_function_called_and_return_mock);

	/* Receive handshake packet */
	will_return(efa_mock_ibv_end_poll_check_mock, NULL);
	will_return(efa_mock_ibv_next_poll_check_function_called_and_return_mock, ENOENT);
	will_return(efa_mock_ibv_read_byte_len_return_mock, pkt_entry->pkt_size);
	will_return(efa_mock_ibv_read_opcode_return_mock, IBV_WC_RECV);
	will_return(efa_mock_ibv_read_qp_num_return_mock, 0);
	will_return(efa_mock_ibv_read_wc_flags_return_mock, 0);
	will_return(efa_mock_ibv_read_slid_return_mock, efa_rdm_ep_get_peer_ahn(efa_rdm_ep, peer_addr));
	will_return(efa_mock_ibv_read_src_qp_return_mock, raw_addr.qpn);
	will_return(efa_mock_ibv_start_poll_return_mock, IBV_WC_SUCCESS);

	/**
	 * Fire away handshake packet.
	 * Because we don't care if it fails(there is no receiver!), mark it as failed to make mocking simpler.
	 */
	will_return(efa_mock_ibv_end_poll_check_mock, NULL);
	will_return(efa_mock_ibv_read_opcode_return_mock, IBV_WC_SEND);
	will_return(efa_mock_ibv_read_qp_num_return_mock, 0);
	will_return(efa_mock_ibv_read_vendor_err_return_mock, FI_EFA_ERR_OTHER);
	will_return(efa_mock_ibv_start_poll_return_mock, IBV_WC_SUCCESS);

	/* Progress the recv wr first to process the received handshake packet. */
	cq_read_recv_ret = fi_cq_read(resource->cq, &cq_entry, 1);

	actual_peer_host_id = peer->host_id;

	/**
	 * We need to poll the CQ twice explicitly to point the CQE
	 * to the saved send wr in handshake
	 */
	efa_rdm_cq->ibv_cq.ibv_cq_ex->status = IBV_WC_GENERAL_ERR;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->wr_id = (uintptr_t)g_ibv_submitted_wr_id_vec[0];

	/* Progress the send wr to clean up outstanding tx ops */
	cq_read_send_ret = fi_cq_read(resource->cq, &cq_entry, 1);

	/* HANDSHAKE packet does not generate completion entry */
	assert_int_equal(cq_read_recv_ret, -FI_EAGAIN);
	assert_int_equal(cq_read_send_ret, -FI_EAGAIN);

	/* Peer host id is set after handshake */
	assert_true(actual_peer_host_id == g_efa_unit_test_mocks.peer_host_id);

	/* Device version should be stored after handshake */
        assert_int_equal(peer->device_version, 0xefa0);
}
#else
void test_efa_rdm_ep_handshake_exchange_host_id() {
	skip();
}
#endif

void test_efa_rdm_ep_handshake_receive_and_send_valid_host_ids_with_connid(struct efa_resource **state)
{
	test_efa_rdm_ep_handshake_exchange_host_id(state, 0x1234567812345678, 0x8765432187654321, true);
}

void test_efa_rdm_ep_handshake_receive_and_send_valid_host_ids_without_connid(struct efa_resource **state)
{
	test_efa_rdm_ep_handshake_exchange_host_id(state, 0x1234567812345678, 0x8765432187654321, false);
}

void test_efa_rdm_ep_handshake_receive_valid_peer_host_id_and_do_not_send_local_host_id(struct efa_resource **state)
{
	test_efa_rdm_ep_handshake_exchange_host_id(state, 0x0, 0x8765432187654321, true);
}

void test_efa_rdm_ep_handshake_receive_without_peer_host_id_and_do_not_send_local_host_id(struct efa_resource **state)
{
	test_efa_rdm_ep_handshake_exchange_host_id(state, 0x0, 0x0, true);
}

static void check_ep_pkt_pool_flags(struct fid_ep *ep, int expected_flags)
{
       struct efa_rdm_ep *efa_rdm_ep;

       efa_rdm_ep = container_of(ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
       assert_int_equal(efa_rdm_ep->efa_tx_pkt_pool->attr.flags, expected_flags);
       assert_int_equal(efa_rdm_ep->efa_rx_pkt_pool->attr.flags, expected_flags);
}

/**
 * @brief Test the pkt pool flags in efa_rdm_ep
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_pkt_pool_flags(struct efa_resource **state) {
	struct efa_resource *resource = *state;

	efa_env.huge_page_setting = EFA_ENV_HUGE_PAGE_DISABLED;
	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	check_ep_pkt_pool_flags(resource->ep, OFI_BUFPOOL_NONSHARED);
}

/**
 * @brief When the buf pool is created with OFI_BUFPOOL_NONSHARED,
 * test if the allocated memory is page aligned.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_pkt_pool_page_alignment(struct efa_resource **state)
{
	int ret;
	struct efa_rdm_pke *pkt_entry;
	struct fid_ep *ep;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_resource *resource = *state;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	efa_env.huge_page_setting = EFA_ENV_HUGE_PAGE_DISABLED;
	ret = fi_endpoint(resource->domain, resource->info, &ep, NULL);
	assert_int_equal(ret, 0);
	efa_rdm_ep = container_of(ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	assert_int_equal(efa_rdm_ep->efa_rx_pkt_pool->attr.flags, OFI_BUFPOOL_NONSHARED);

	pkt_entry = efa_rdm_pke_alloc(efa_rdm_ep, efa_rdm_ep->efa_rx_pkt_pool, EFA_RDM_PKE_FROM_EFA_RX_POOL);
	assert_non_null(pkt_entry);
	assert_true(((uintptr_t)ofi_buf_region(pkt_entry)->alloc_region % ofi_get_page_size()) == 0);
	efa_rdm_pke_release_rx(pkt_entry);

	fi_close(&ep->fid);
}

/**
 * @brief When using LL128 protocol, test the packet allocated from read_copy_pkt_pool
 *  is 128 byte aligned.
 * 
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_read_copy_pkt_pool_128_alignment(struct efa_resource **state)
{
	int ret;
	struct efa_rdm_pke *pkt_entry;
	struct fid_ep *ep;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_resource *resource = *state;
	struct efa_domain *efa_domain = NULL;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* rx_readcopy_pkt_pool is only created when application requested FI_HMEM */
	efa_domain = container_of(resource->domain, struct efa_domain,
				  util_domain.domain_fid);
	efa_domain->util_domain.mr_mode |= FI_MR_HMEM;

	ret = fi_endpoint(resource->domain, resource->info, &ep, NULL);
	assert_int_equal(ret, 0);
	efa_rdm_ep = container_of(ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;

	pkt_entry =
		efa_rdm_pke_alloc(efa_rdm_ep, efa_rdm_ep->rx_readcopy_pkt_pool,
				  EFA_RDM_PKE_FROM_READ_COPY_POOL);
	assert_non_null(pkt_entry);
	efa_rdm_ep->rx_readcopy_pkt_pool_used++;
	assert(ofi_is_addr_aligned((void *) pkt_entry->wiredata,
				   EFA_RDM_IN_ORDER_ALIGNMENT));
	efa_rdm_pke_release_rx(pkt_entry);

	fi_close(&ep->fid);
}

/**
 * @brief When using LL128 protocol, the copy method is local read. 
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_pke_get_available_copy_methods_align128(struct efa_resource **state)
{
	int ret;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_mr efa_mr;
	struct efa_resource *resource = *state;
	bool local_read_available, gdrcopy_available, cuda_memcpy_available;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_mr.peer.iface = FI_HMEM_CUDA;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;
	
	/* p2p is available */
	efa_rdm_ep_domain(efa_rdm_ep)->hmem_info[FI_HMEM_CUDA].p2p_supported_by_device = true;
	efa_rdm_ep->hmem_p2p_opt = FI_HMEM_P2P_ENABLED;

	/* RDMA read is supported */
	efa_rdm_ep->use_device_rdma = true;
	uint64_t caps = efa_rdm_ep_domain(efa_rdm_ep)->device->device_caps;
	efa_rdm_ep_domain(efa_rdm_ep)->device->device_caps |=
		EFADV_DEVICE_ATTR_CAPS_RDMA_READ;

	ret = efa_rdm_pke_get_available_copy_methods(
		efa_rdm_ep, &efa_mr, &local_read_available,
		&cuda_memcpy_available, &gdrcopy_available);

	efa_rdm_ep_domain(efa_rdm_ep)->device->device_caps = caps;

	assert_int_equal(ret, 0);
	assert_true(local_read_available);
	assert_false(cuda_memcpy_available);
	assert_false(gdrcopy_available);
}

/**
 * @brief when delivery complete atomic was used and handshake packet has not been received
 * verify the txe is queued
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_dc_atomic_queue_before_handshake(struct efa_resource **state)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_peer *peer;
	struct fi_ioc ioc = {0};
	struct fi_rma_ioc rma_ioc = {0};
	struct fi_msg_atomic msg = {0};
	struct efa_resource *resource = *state;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int buf[1] = {0}, err, numaddr;
	struct efa_rdm_ope *txe;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	numaddr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(numaddr, 1);

	msg.addr = peer_addr;

	ioc.addr = buf;
	ioc.count = 1;
	msg.msg_iov = &ioc;
	msg.iov_count = 1;

	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.datatype = FI_INT32;
	msg.op = FI_SUM;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		err = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(err, 0);
		efa_rdm_ep->shm_ep = NULL;
	}
	/* set peer->flag to EFA_RDM_PEER_REQ_SENT will make efa_rdm_atomic() think
	 * a REQ packet has been sent to the peer (so no need to send again)
	 * handshake has not been received, so we do not know whether the peer support DC
	 */
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	peer->flags = EFA_RDM_PEER_REQ_SENT;
	peer->is_local = false;

	assert_true(dlist_empty(&efa_rdm_ep->txe_list));
	err = fi_atomicmsg(resource->ep, &msg, FI_DELIVERY_COMPLETE);
	/* DC has been reuquested, but ep do not know whether peer supports it, therefore
	 * the ope has been queued to domain->ope_queued_list
	 */
	assert_int_equal(err, 0);
	assert_int_equal(efa_unit_test_get_dlist_length(&efa_rdm_ep->txe_list),  1);
	assert_int_equal(efa_unit_test_get_dlist_length(&(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list)), 1);
	txe = container_of(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list.next, struct efa_rdm_ope, queued_entry);
	assert_true((txe->op == ofi_op_atomic));
	assert_true(txe->internal_flags & EFA_RDM_OPE_QUEUED_BEFORE_HANDSHAKE);
}

/**
 * @brief when delivery complete send was used and handshake packet has not been received
 * verify the txe is queued
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_dc_send_queue_before_handshake(struct efa_resource **state)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_peer *peer;
	struct fi_msg msg = {0};
	struct iovec iov;
	struct efa_resource *resource = *state;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int err, numaddr;
	struct efa_rdm_ope *txe;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	numaddr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(numaddr, 1);

	msg.addr = peer_addr;
	msg.iov_count = 1;
	iov.iov_base = NULL;
	iov.iov_len = 0;
	msg.msg_iov = &iov;
	msg.desc = NULL;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		err = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(err, 0);
		efa_rdm_ep->shm_ep = NULL;
	}
	/* set peer->flag to EFA_RDM_PEER_REQ_SENT will make efa_rdm_atomic() think
	 * a REQ packet has been sent to the peer (so no need to send again)
	 * handshake has not been received, so we do not know whether the peer support DC
	 */
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	peer->flags = EFA_RDM_PEER_REQ_SENT;
	peer->is_local = false;

	assert_true(dlist_empty(&efa_rdm_ep->txe_list));
	err = fi_sendmsg(resource->ep, &msg, FI_DELIVERY_COMPLETE);
	/* DC has been reuquested, but ep do not know whether peer supports it, therefore
	 * the ope has been queued to domain->ope_queued_list
	 */
	assert_int_equal(err, 0);
	assert_int_equal(efa_unit_test_get_dlist_length(&efa_rdm_ep->txe_list),  1);
	assert_int_equal(efa_unit_test_get_dlist_length(&(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list)), 1);
	txe = container_of(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list.next, struct efa_rdm_ope, queued_entry);
	assert_true((txe->op == ofi_op_msg));
	assert_true(txe->internal_flags & EFA_RDM_OPE_QUEUED_BEFORE_HANDSHAKE);
}

/**
 * @brief when delivery complete send was used and handshake packet has not been received
 * verify the txes are queued before the number of requests reach EFA_RDM_MAX_QUEUED_OPE_BEFORE_HANDSHAKE.
 * After reaching the limit, fi_send should return -FI_EAGAIN
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_dc_send_queue_limit_before_handshake(struct efa_resource **state)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_peer *peer;
	struct fi_msg msg = {0};
	struct iovec iov;
	struct efa_resource *resource = *state;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int err, numaddr;
	int i;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	numaddr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(numaddr, 1);

	msg.addr = peer_addr;
	msg.iov_count = 1;
	iov.iov_base = NULL;
	iov.iov_len = 0;
	msg.msg_iov = &iov;
	msg.desc = NULL;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		err = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(err, 0);
		efa_rdm_ep->shm_ep = NULL;
	}
	/* set peer->flag to EFA_RDM_PEER_REQ_SENT will make efa_rdm_atomic() think
	 * a REQ packet has been sent to the peer (so no need to send again)
	 * handshake has not been received, so we do not know whether the peer support DC
	 */
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	peer->flags = EFA_RDM_PEER_REQ_SENT;
	peer->is_local = false;

	assert_true(dlist_empty(&efa_rdm_ep->txe_list));

	for (i = 0; i < EFA_RDM_MAX_QUEUED_OPE_BEFORE_HANDSHAKE; i++) {
		err = fi_sendmsg(resource->ep, &msg, FI_DELIVERY_COMPLETE);
		assert_int_equal(err, 0);
	}

	assert_true(efa_rdm_ep->ope_queued_before_handshake_cnt == EFA_RDM_MAX_QUEUED_OPE_BEFORE_HANDSHAKE);
	err = fi_sendmsg(resource->ep, &msg, FI_DELIVERY_COMPLETE);
	assert_int_equal(err, -FI_EAGAIN);
}

/**
 * @brief verify tx entry is queued for rma (read or write) request before handshake is made.
 *
 * @param[in] state	struct efa_resource that is managed by the framework
 * @param[in] op op code
 */
void test_efa_rdm_ep_rma_queue_before_handshake(struct efa_resource **state, int op)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int num_addr;
	const int buf_len = 8;
	char buf[8] = {0};
	int err;
	uint64_t rma_addr, rma_key;
	struct efa_rdm_ope *txe;
	struct efa_rdm_peer *peer;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	resource->hints->caps |= FI_MSG | FI_TAGGED | FI_RMA;
	resource->hints->domain_attr->mr_mode = FI_MR_BASIC;
	efa_unit_test_resource_construct_with_hints(resource, FI_EP_RDM, FI_VERSION(1, 14),
	                                            resource->hints, true, true);

	/* ensure we don't have RMA capability. */
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	num_addr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(num_addr, 1);

	/* create a fake rma_key and address.  fi_read should return before
	 * they are needed. */
	rma_key = 0x1234;
	rma_addr = (uint64_t) &buf;

	/* set peer->flag to EFA_RDM_PEER_REQ_SENT will make efa_rdm_atomic() think
	 * a REQ packet has been sent to the peer (so no need to send again)
	 * handshake has not been received, so we do not know whether the peer support DC
	 */
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	peer->flags = EFA_RDM_PEER_REQ_SENT;
	peer->is_local = false;

	assert_true(dlist_empty(&efa_rdm_ep->txe_list));

	if (op == ofi_op_read_req) {
		err = fi_read(resource->ep, buf, buf_len,
				NULL, /* desc, not required */
				peer_addr,
				rma_addr,
				rma_key,
				NULL); /* context */
	} else if (op == ofi_op_write) {
		err = fi_write(resource->ep, buf, buf_len,
				NULL, /* desc, not required */
				peer_addr,
				rma_addr,
				rma_key,
				NULL); /* context */
	} else {
		fprintf(stderr, "Unknown op code %d\n", op);
		fail();
	}
	assert_int_equal(err, 0);
	assert_int_equal(efa_unit_test_get_dlist_length(&efa_rdm_ep->txe_list),  1);
	assert_int_equal(efa_unit_test_get_dlist_length(&(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list)), 1);
	txe = container_of(efa_rdm_ep_domain(efa_rdm_ep)->ope_queued_list.next, struct efa_rdm_ope, queued_entry);
	assert_true((txe->op == op));
	assert_true(txe->internal_flags & EFA_RDM_OPE_QUEUED_BEFORE_HANDSHAKE);
}

void test_efa_rdm_ep_write_queue_before_handshake(struct efa_resource **state)
{
	test_efa_rdm_ep_rma_queue_before_handshake(state, ofi_op_write);
}

void test_efa_rdm_ep_read_queue_before_handshake(struct efa_resource **state)
{
	test_efa_rdm_ep_rma_queue_before_handshake(state, ofi_op_read_req);
}

/**
 * @brief verify that when shm was used to send a small message (<4k), no copy was performed.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_send_with_shm_no_copy(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int num_addr;
	int buff_len = 8;
	char buff[8] = {0};
	int err;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	num_addr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(num_addr, 1);

	g_ofi_copy_from_hmem_iov_call_counter = 0;
	g_efa_unit_test_mocks.ofi_copy_from_hmem_iov = efa_mock_ofi_copy_from_hmem_iov_inc_counter;

	err = fi_send(resource->ep, buff, buff_len,
		      NULL /* desc, which is not required by shm */,
		      peer_addr,
		      NULL /* context */);

	assert_int_equal(g_ofi_copy_from_hmem_iov_call_counter, 0);
}

/**
 * @brief verify error is generated for RMA on non-RMA-enabled EP.
 *
 * @param[in] state	struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_rma_without_caps(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int num_addr;
	const int buf_len = 8;
	char buf[8] = {0};
	int err;
	uint64_t rma_addr, rma_key;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	resource->hints->caps |= FI_MSG | FI_TAGGED;
	resource->hints->caps &= ~FI_RMA;
	resource->hints->domain_attr->mr_mode = FI_MR_BASIC;
	efa_unit_test_resource_construct_with_hints(resource, FI_EP_RDM, FI_VERSION(1, 14),
	                                            resource->hints, true, true);

	/* ensure we don't have RMA capability. */
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	assert_int_equal( efa_rdm_ep->user_info->caps & FI_RMA, 0);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	num_addr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(num_addr, 1);

	/* create a fake rma_key and address.  fi_read should return before
	 * they are needed. */
	rma_key = 0x1234;
	rma_addr = (uint64_t) &buf;
	err = fi_read(resource->ep, buf, buf_len,
		      NULL, /* desc, not required */
		      peer_addr,
		      rma_addr,
		      rma_key,
		      NULL); /* context */

	assert_int_equal(err, -FI_EOPNOTSUPP);
}

/**
 * @brief verify error is generated for Atomic operations on non-Atomic-enabled EP.
 *
 * @param[in] state	struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_atomic_without_caps(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	fi_addr_t peer_addr;
	int num_addr;
	const int buf_len = 8;
	char buf[8] = {0};
	int err;
	uint64_t rma_addr, rma_key;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	resource->hints->caps |= FI_MSG | FI_TAGGED;
	resource->hints->caps &= ~FI_ATOMIC;
	resource->hints->domain_attr->mr_mode = FI_MR_BASIC;
	efa_unit_test_resource_construct_with_hints(resource, FI_EP_RDM, FI_VERSION(1, 14),
	                                            resource->hints, true, true);

	/* ensure we don't have ATOMIC capability. */
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	assert_int_equal( efa_rdm_ep->user_info->caps & FI_ATOMIC, 0);

	/* create a fake peer */
	err = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(err, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	num_addr = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(num_addr, 1);

	/* create a fake rma_key and address.  fi_atomic should return before
	 * they are needed. */
	rma_key = 0x1234;
	rma_addr = (uint64_t) &buf;
	err = fi_atomic(resource->ep, buf, buf_len,
			NULL, /* desc, not required */
			peer_addr,
			rma_addr,
			rma_key,
			FI_INT32,
			FI_SUM,
			NULL); /* context */

	assert_int_equal(err, -FI_EOPNOTSUPP);
}

/*
 * Check fi_getopt return with different input opt_len
 */
void test_efa_rdm_ep_getopt(struct efa_resource **state, size_t opt_len, int expected_return)
{
	struct efa_resource *resource = *state;
	size_t opt_val;
	size_t opt_len_temp;
	size_t i;
	int ret;
	int opt_names[] = {
		FI_OPT_MIN_MULTI_RECV,
		FI_OPT_EFA_RNR_RETRY,
		FI_OPT_FI_HMEM_P2P,
		FI_OPT_EFA_EMULATED_READ,
		FI_OPT_EFA_EMULATED_WRITE,
		FI_OPT_EFA_EMULATED_ATOMICS,
		FI_OPT_EFA_USE_DEVICE_RDMA,
		FI_OPT_EFA_SENDRECV_IN_ORDER_ALIGNED_128_BYTES,
		FI_OPT_EFA_WRITE_IN_ORDER_ALIGNED_128_BYTES
	};
	size_t num_opt_names = sizeof(opt_names) / sizeof(int);

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	for (i = 0; i < num_opt_names; i++) {
		opt_len_temp = opt_len;
		ret = fi_getopt(&resource->ep->fid, FI_OPT_ENDPOINT, opt_names[i], &opt_val, &opt_len_temp);
		assert_int_equal(ret, expected_return);
	}
}

/* undersized optlen should return -FI_ETOOSMALL */
void test_efa_rdm_ep_getopt_undersized_optlen(struct efa_resource **state)
{
	test_efa_rdm_ep_getopt(state, 0, -FI_ETOOSMALL);
}

/* oversized optlen should return FI_SUCCESS */
void test_efa_rdm_ep_getopt_oversized_optlen(struct efa_resource **state)
{
	test_efa_rdm_ep_getopt(state, 16, FI_SUCCESS);
}

void test_efa_rdm_ep_setopt_shared_memory_permitted(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *ep;
	bool optval = false;

	efa_unit_test_resource_construct_ep_not_enabled(resource, FI_EP_RDM);

	ep = container_of(resource->ep, struct efa_rdm_ep,
			  base_ep.util_ep.ep_fid);

	assert_int_equal(fi_setopt(&resource->ep->fid, FI_OPT_ENDPOINT,
				   FI_OPT_SHARED_MEMORY_PERMITTED, &optval,
				   sizeof(optval)),
			 0);

	assert_int_equal(fi_enable(resource->ep), 0);

	assert_null(ep->shm_ep);
}

/**
 * @brief Test fi_enable with different optval of fi_setopt for
 * FI_OPT_EFA_WRITE_IN_ORDER_ALIGNED_128_BYTES optname.
 * @param state struct efa_resource that is managed by the framework
 * @param expected_status expected return status of fi_enable
 * @param optval the optval passed to fi_setopt
 */
void test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_common(struct efa_resource **state, int expected_status, bool optval)
{
	struct efa_resource *resource = *state;

	efa_unit_test_resource_construct_ep_not_enabled(resource, FI_EP_RDM);

	/* fi_setopt should always succeed */
	assert_int_equal(fi_setopt(&resource->ep->fid, FI_OPT_ENDPOINT,
				   FI_OPT_EFA_WRITE_IN_ORDER_ALIGNED_128_BYTES, &optval,
				   sizeof(optval)), expected_status);
}

#if HAVE_EFA_DATA_IN_ORDER_ALIGNED_128_BYTES
/**
 * @brief Test the case where fi_enable should return success
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_good(struct efa_resource **state)
{
	/* mock ibv_query_qp_data_in_order to return required capability */
	g_efa_unit_test_mocks.ibv_query_qp_data_in_order = &efa_mock_ibv_query_qp_data_in_order_return_in_order_aligned_128_bytes;
	test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_common(state, FI_SUCCESS, true);
}

/**
 * @brief Test the case where fi_enable should return -FI_EOPNOTSUPP
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_bad(struct efa_resource **state)
{
	/* mock ibv_query_qp_data_in_order to return zero capability */
	g_efa_unit_test_mocks.ibv_query_qp_data_in_order = &efa_mock_ibv_query_qp_data_in_order_return_0;
	test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_common(state, -FI_EOPNOTSUPP, true);
}

#else

void test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_good(struct efa_resource **state)
{
	test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_common(state, FI_SUCCESS, false);
}

void test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_bad(struct efa_resource **state)
{
	test_efa_rdm_ep_enable_qp_in_order_aligned_128_bytes_common(state, -FI_EOPNOTSUPP, true);
}

#endif

static void
test_efa_rdm_ep_use_zcpy_rx_impl(struct efa_resource *resource, bool expected_use_zcpy_rx) {
	struct efa_rdm_ep *ep;
	size_t max_msg_size = 1000;

	efa_unit_test_resource_construct_with_hints(resource, FI_EP_RDM, FI_VERSION(1, 14),
	                                            resource->hints, false, true);

	ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

	/* Set sufficiently small max_msg_size */
	assert_int_equal(fi_setopt(&resource->ep->fid, FI_OPT_ENDPOINT, FI_OPT_MAX_MSG_SIZE,
			&max_msg_size, sizeof max_msg_size), 0);
	assert_true(ep->max_msg_size == max_msg_size);
	assert_int_equal(fi_enable(resource->ep), 0);
	assert_true(ep->use_zcpy_rx == expected_use_zcpy_rx);
}

/**
 * @brief Verify zcpy_rx is enabled when the following requirements are met:
 * 1. app doesn't require FI_ORDER_SAS in tx or rx's msg_order
 * 2. app uses FI_MSG_PREFIX mode
 * 3. app's max msg size is smaller than mtu_size - prefix_size
 * 4. app doesn't use FI_DIRECTED_RECV, FI_TAGGED, FI_ATOMIC capability
 */
void test_efa_rdm_ep_user_zcpy_rx_happy(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->rx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->mode = FI_MSG_PREFIX;
	resource->hints->caps = FI_MSG;

	test_efa_rdm_ep_use_zcpy_rx_impl(resource, true);
}

/**
 * @brief When sas is requested for either tx or rx. zcpy will be disabled
 */
void test_efa_rdm_ep_user_zcpy_rx_unhappy_due_to_sas(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_SAS;
	resource->hints->rx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->mode = FI_MSG_PREFIX;
	resource->hints->caps = FI_MSG;

	test_efa_rdm_ep_use_zcpy_rx_impl(resource, false);
}

void test_efa_rdm_ep_close_discard_posted_recv(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	char buf[16];

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* Post recv and then close ep */
	assert_int_equal(fi_recv(resource->ep, (void *) buf, 16, NULL, FI_ADDR_UNSPEC, NULL), 0);

	assert_int_equal(fi_close(&resource->ep->fid), 0);

	/* CQ should be empty and no err entry */
	assert_int_equal(fi_cq_read(resource->cq, NULL, 1), -FI_EAGAIN);

	/* Reset to NULL to avoid test reaper closing again */
	resource->ep = NULL;
}

void test_efa_rdm_ep_zcpy_recv_cancel(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct fi_context cancel_context = {0};
	struct fi_cq_err_entry cq_err_entry = {0};
	struct efa_unit_test_buff recv_buff;
	struct efa_rdm_pke *pke;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_ope *rxe;

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);

	resource->hints->tx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->rx_attr->msg_order = FI_ORDER_NONE;
	resource->hints->caps = FI_MSG;

	/* enable zero-copy recv mode in ep */
	test_efa_rdm_ep_use_zcpy_rx_impl(resource, true);

	/* Construct a recv buffer with mr */
	efa_unit_test_buff_construct(&recv_buff, resource, 16);

	assert_int_equal(fi_recv(resource->ep, recv_buff.buff, recv_buff.size, fi_mr_desc(recv_buff.mr), FI_ADDR_UNSPEC, &cancel_context), 0);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

	assert_int_equal(efa_unit_test_get_dlist_length(&efa_rdm_ep->user_recv_rxe_list),  1);

	rxe = container_of(efa_rdm_ep->user_recv_rxe_list.next, struct efa_rdm_ope, entry);
	pke = rxe->user_rx_pkt;

	assert_int_equal(fi_cancel((struct fid *)resource->ep, &cancel_context), 0);

	assert_true(pke->flags & EFA_RDM_PKE_USER_RECV_CANCEL);

	assert_int_equal(fi_cq_read(resource->cq, NULL, 1), -FI_EAVAIL);

	assert_int_equal(fi_cq_readerr(resource->cq, &cq_err_entry, 0), 1);

	assert_int_equal(cq_err_entry.err, FI_ECANCELED);

	assert_int_equal(cq_err_entry.prov_errno, -FI_ECANCELED);

	assert_true(cq_err_entry.op_context == (void *) &cancel_context);

	/**
	 * the buf is still posted to rdma-core, so unregistering mr can
	 * return non-zero. Currently ignore this failure.
	 */
	(void) fi_close(&recv_buff.mr->fid);
	free(recv_buff.buff);
}
