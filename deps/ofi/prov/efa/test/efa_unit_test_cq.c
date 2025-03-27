/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"
#include "dgram/efa_dgram_ep.h"
#include "dgram/efa_dgram_cq.h"
#include "rdm/efa_rdm_cq.h"

/**
 * @brief implementation of test cases for fi_cq_read() works with empty device CQ for given endpoint type
 *
 * When CQ is empty, fi_cq_read() should return -FI_EAGAIN.
 *
 * @param[in]		resource	struct efa_resource that is managed by the framework
 * @param[in]		ep_type		endpoint type, can be FI_EP_DGRAM or FI_EP_RDM
 */
static
void test_impl_cq_read_empty_cq(struct efa_resource *resource, enum fi_ep_type ep_type)
{
	struct ibv_cq_ex *ibv_cqx;
	struct fi_cq_data_entry cq_entry;
	int ret;

	efa_unit_test_resource_construct(resource, ep_type);

	if (ep_type == FI_EP_DGRAM) {
		struct efa_dgram_ep *efa_dgram_ep;

		efa_dgram_ep = container_of(resource->ep, struct efa_dgram_ep, base_ep.util_ep.ep_fid);
		ibv_cqx = efa_dgram_ep->rcq->ibv_cq_ex;
	} else {
		struct efa_rdm_ep *efa_rdm_ep;

		efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
		assert(efa_rdm_ep->base_ep.util_ep.rx_cq);
		ibv_cqx = container_of(efa_rdm_ep->base_ep.util_ep.rx_cq, struct efa_rdm_cq, util_cq)->ibv_cq.ibv_cq_ex;
	}

	ibv_cqx->start_poll = &efa_mock_ibv_start_poll_return_mock;

	/* ibv_start_poll to return ENOENT means device CQ is empty */
	will_return(efa_mock_ibv_start_poll_return_mock, ENOENT);

	ret = fi_cq_read(resource->cq, &cq_entry, 1);

	assert_int_equal(ret, -FI_EAGAIN);
}

/**
 * @brief verify DGRAM CQ's fi_cq_read() works with empty CQ
 *
 * When CQ is empty, fi_cq_read() should return -FI_EAGAIN.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_dgram_cq_read_empty_cq(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_impl_cq_read_empty_cq(resource, FI_EP_DGRAM);
}

/**
 * @brief verify RDM CQ's fi_cq_read() works with empty CQ
 *
 * When CQ is empty, fi_cq_read() should return -FI_EAGAIN.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_ibv_cq_ex_read_empty_cq(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_impl_cq_read_empty_cq(resource, FI_EP_RDM);
}

/**
 * @brief test RDM CQ's fi_cq_read()/fi_cq_readerr() works properly when rdma-core return bad status for send.
 *
 * When the send operation failed, fi_cq_read() should return -FI_EAVAIL, which means error available.
 * then user should call fi_cq_readerr() to get an error CQ entry that contain error code.
 *
 * @param[in]  state            struct efa_resource that is managed by the framework
 * @param[in]  local_host_id    Local(sender) host id
 * @param[in]  peer_host_id     Peer(receiver) host id
 * @param[in]  vendor_error     Vendor error returned by ibv_read_vendor_err
 */
static void test_rdm_cq_read_bad_send_status(struct efa_resource *resource,
                                             uint64_t local_host_id, uint64_t peer_host_id,
                                             int vendor_error)
{
	const char *strerror;
	fi_addr_t addr;
	int ret, err;
	char host_id_str[] = "xxxxx host id: i-01234567812345678";
	size_t raw_addr_len = sizeof(struct efa_ep_addr);
	struct efa_ep_addr raw_addr;
	struct efa_unit_test_buff send_buff;
	struct fi_cq_data_entry cq_entry;
	struct fi_cq_err_entry cq_err_entry = {0};
	struct ibv_cq_ex *ibv_cqx;
	struct ibv_qp_ex *ibv_qpx;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_peer *peer;
	struct efa_rdm_cq *efa_rdm_cq;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_unit_test_buff_construct(&send_buff, resource, 4096 /* buff_size */);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->host_id = local_host_id;
	ibv_qpx = efa_rdm_ep->base_ep.qp->ibv_qp_ex;

	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);
	ibv_cqx = efa_rdm_cq->ibv_cq.ibv_cq_ex;
	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		err = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(err, 0);
		efa_rdm_ep->shm_ep = NULL;
	}

	ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	ret = fi_av_insert(resource->av, &raw_addr, 1, &addr, 0 /* flags */, NULL /* context */);
	assert_int_equal(ret, 1);

	peer = efa_rdm_ep_get_peer(efa_rdm_ep, addr);
	assert_non_null(peer);
	peer->host_id = peer_host_id;

	ibv_qpx->wr_start = &efa_mock_ibv_wr_start_no_op;
	/* this mock will save the send work request (wr) in a global list */
	ibv_qpx->wr_send = &efa_mock_ibv_wr_send_save_wr;
	ibv_qpx->wr_set_sge_list = &efa_mock_ibv_wr_set_sge_list_no_op;
	ibv_qpx->wr_set_ud_addr = &efa_mock_ibv_wr_set_ud_addr_no_op;
	ibv_qpx->wr_complete = &efa_mock_ibv_wr_complete_no_op;
	assert_int_equal(g_ibv_submitted_wr_id_cnt, 0);

	err = fi_send(resource->ep, send_buff.buff, send_buff.size, fi_mr_desc(send_buff.mr), addr, NULL /* context */);
	assert_int_equal(err, 0);
	/* fi_send() called efa_mock_ibv_wr_send_save_wr(), which saved one send_wr in g_ibv_submitted_wr_id_vec */
	assert_int_equal(g_ibv_submitted_wr_id_cnt, 1);

	/* this mock will set ibv_cq_ex->wr_id to the wr_id f the head of global send_wr,
	 * and set ibv_cq_ex->status to mock value */
	ibv_cqx->start_poll = &efa_mock_ibv_start_poll_use_saved_send_wr_with_mock_status;
	ibv_cqx->end_poll = &efa_mock_ibv_end_poll_check_mock;
	ibv_cqx->read_opcode = &efa_mock_ibv_read_opcode_return_mock;
	ibv_cqx->read_vendor_err = &efa_mock_ibv_read_vendor_err_return_mock;
	ibv_cqx->read_qp_num = &efa_mock_ibv_read_qp_num_return_mock;
	will_return(efa_mock_ibv_start_poll_use_saved_send_wr_with_mock_status, IBV_WC_GENERAL_ERR);
	will_return(efa_mock_ibv_end_poll_check_mock, NULL);
	will_return(efa_mock_ibv_read_opcode_return_mock, IBV_WC_SEND);
	will_return(efa_mock_ibv_read_vendor_err_return_mock, vendor_error);
	will_return(efa_mock_ibv_read_qp_num_return_mock, 0);
	ret = fi_cq_read(resource->cq, &cq_entry, 1);
	/* fi_cq_read() called efa_mock_ibv_start_poll_use_saved_send_wr(), which pulled one send_wr from g_ibv_submitted_wr_idv=_vec */
	assert_int_equal(g_ibv_submitted_wr_id_cnt, 0);
	assert_int_equal(ret, -FI_EAVAIL);

	/* Allocate memory to read CQ error */
	cq_err_entry.err_data_size = EFA_RDM_ERROR_MSG_BUFFER_LENGTH;
	cq_err_entry.err_data = malloc(cq_err_entry.err_data_size);
	assert_non_null(cq_err_entry.err_data);

	ret = fi_cq_readerr(resource->cq, &cq_err_entry, 0);
	assert_true(cq_err_entry.err_data_size > 0);
	strerror = fi_cq_strerror(resource->cq, cq_err_entry.prov_errno, cq_err_entry.err_data, NULL, 0);

	assert_int_equal(ret, 1);
	assert_int_not_equal(cq_err_entry.err, FI_SUCCESS);
	assert_int_equal(cq_err_entry.prov_errno, vendor_error);

	/* Reset value */
	memset(host_id_str, 0, sizeof(host_id_str));

	/* Set expected host id */
	if (local_host_id) {
		snprintf(host_id_str, sizeof(host_id_str), "My host id: i-%017lx", local_host_id);
	} else {
		strcpy(host_id_str, "My host id: N/A");
	}
	/* Look for My host id */
	assert_non_null(strstr(strerror, host_id_str));

	/* Reset value */
	memset(host_id_str, 0, sizeof(host_id_str));

	/* Set expected host id */
	if (peer_host_id) {
		snprintf(host_id_str, sizeof(host_id_str), "Peer host id: i-%017lx", peer_host_id);
	} else {
		strcpy(host_id_str, "Peer host id: N/A");
	}
	/* Look for peer host id */
	assert_non_null(strstr(strerror, host_id_str));
	efa_unit_test_buff_destruct(&send_buff);
}

/**
 * @brief test that RDM CQ's fi_cq_read()/fi_cq_readerr() works properly when rdma-core returns
 * unresponsive receiver error for send.
 *
 * When send operation failed, fi_cq_read() should return -FI_EAVAIL, which means error available.
 * then user should call fi_cq_readerr() to get an error CQ entry that contain error code.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_cq_read_bad_send_status_unresponsive_receiver(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_rdm_cq_read_bad_send_status(resource,
					 0x1234567812345678, 0x8765432187654321,
					 EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);
}

/**
 * @brief test that RDM CQ's fi_cq_read()/fi_cq_readerr() works properly when rdma-core returns
 * unresponsive receiver error for send. This test verifies peer host id is printed correctly if it is unknown.
 *
 * When send operation failed, fi_cq_read() should return -FI_EAVAIL, which means error available.
 * then user should call fi_cq_readerr() to get an error CQ entry that contain error code.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_cq_read_bad_send_status_unresponsive_receiver_missing_peer_host_id(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_rdm_cq_read_bad_send_status(resource,
					 0x1234567812345678, 0,
					 EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);
}

/**
 * @brief test that RDM CQ's fi_cq_read()/fi_cq_readerr() works properly when rdma-core returns
 * invalid qpn error for send.
 *
 * When send operation failed, fi_cq_read() should return -FI_EAVAIL, which means error available.
 * then user should call fi_cq_readerr() to get an error CQ entry that contain error code.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_cq_read_bad_send_status_invalid_qpn(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	test_rdm_cq_read_bad_send_status(resource,
					 0x1234567812345678, 0x8765432187654321,
					 EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_DEST_QPN);
}

/**
 * @brief test that RDM CQ's fi_cq_read()/fi_cq_readerr() works properly when rdma-core returns
 * message too long error for send.
 *
 * When send operation failed, fi_cq_read() should return -FI_EAVAIL, which means error available.
 * then user should call fi_cq_readerr() to get an error CQ entry that contain error code.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_cq_read_bad_send_status_message_too_long(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_rdm_cq_read_bad_send_status(resource,
					 0x1234567812345678, 0x8765432187654321,
					 EFA_IO_COMP_STATUS_LOCAL_ERROR_BAD_LENGTH);
}

/**
 * @brief verify that fi_cq_read/fi_cq_readerr works properly when rdma-core return bad status for recv.
 *
 * When an ibv_post_recv() operation failed, no data was received. Therefore libfabric cannot
 * find the corresponding RX operation to write a CQ error. It will write an EQ error instead.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_ibv_cq_ex_read_bad_recv_status(struct efa_resource **state)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_resource *resource = *state;
	struct efa_rdm_pke *pkt_entry;
	struct fi_cq_data_entry cq_entry;
	struct fi_eq_err_entry eq_err_entry;
	int ret;
	struct efa_rdm_cq *efa_rdm_cq;


	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

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

	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);

	efa_rdm_cq->ibv_cq.ibv_cq_ex->start_poll = &efa_mock_ibv_start_poll_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->end_poll = &efa_mock_ibv_end_poll_check_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_opcode = &efa_mock_ibv_read_opcode_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_vendor_err = &efa_mock_ibv_read_vendor_err_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_qp_num = &efa_mock_ibv_read_qp_num_return_mock;

	will_return(efa_mock_ibv_start_poll_return_mock, 0);
	will_return(efa_mock_ibv_end_poll_check_mock, NULL);
	/* efa_mock_ibv_read_opcode_return_mock() will be called once in release mode,
	 * but will be called twice in debug mode. because there is an assertion that called ibv_read_opcode(),
	 * therefore use will_return_always()
	 */
	will_return_always(efa_mock_ibv_read_opcode_return_mock, IBV_WC_RECV);
	will_return_always(efa_mock_ibv_read_qp_num_return_mock, 0);
	will_return(efa_mock_ibv_read_vendor_err_return_mock, EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);
	/* the recv error will not populate to application cq because it's an EFA internal error and
	 * and not related to any application recv. Currently we can only read the error from eq.
	 */
	efa_rdm_cq->ibv_cq.ibv_cq_ex->wr_id = (uintptr_t)pkt_entry;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->status = IBV_WC_GENERAL_ERR;
	ret = fi_cq_read(resource->cq, &cq_entry, 1);
	assert_int_equal(ret, -FI_EAGAIN);

	ret = fi_eq_readerr(resource->eq, &eq_err_entry, 0);
	assert_int_equal(ret, sizeof(eq_err_entry));
	assert_int_not_equal(eq_err_entry.err, FI_SUCCESS);
	assert_int_equal(eq_err_entry.prov_errno, EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);
}

/**
 * @brief verify that fi_cq_read/fi_cq_readerr works properly when ibv_start_poll failed.
 *
 * When an ibv_start_poll() failed. Libfabric should write an EQ error.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_ibv_cq_ex_read_failed_poll(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct fi_cq_data_entry cq_entry;
	struct fi_cq_err_entry cq_err_entry;
	int ret;
	struct efa_rdm_cq *efa_rdm_cq;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);
	efa_rdm_cq->ibv_cq.ibv_cq_ex->start_poll = &efa_mock_ibv_start_poll_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->end_poll = &efa_mock_ibv_end_poll_check_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_vendor_err = &efa_mock_ibv_read_vendor_err_return_mock;

	will_return(efa_mock_ibv_start_poll_return_mock, EFAULT);
	will_return(efa_mock_ibv_read_vendor_err_return_mock, EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);

	ret = fi_cq_read(resource->cq, &cq_entry, 1);
	assert_int_equal(ret, -FI_EAVAIL);

	ret = fi_cq_readerr(resource->cq, &cq_err_entry, 0);
	assert_int_equal(ret, 1);
	assert_int_not_equal(cq_err_entry.err, FI_ENOENT);
	assert_int_equal(cq_err_entry.prov_errno, EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE);
}

/**
 * @brief Test efa_rdm_cq_open() handles rdma-core CQ creation failure gracefully
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_cq_create_error_handling(struct efa_resource **state)
{

	struct efa_resource *resource = *state;
	struct ibv_device **ibv_device_list;
	struct efa_device efa_device = {0};
	struct efa_domain *efa_domain = NULL;
	struct verbs_context *vctx = NULL;
	struct fi_cq_attr cq_attr = {0};

	ibv_device_list = ibv_get_device_list(&g_device_cnt);
	if (ibv_device_list == NULL) {
		skip();
		return;
	}
	efa_device_construct(&efa_device, 0, ibv_device_list[0]);

	resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
	assert_non_null(resource->hints);
	assert_int_equal(fi_getinfo(FI_VERSION(1, 14), NULL, NULL, 0ULL, resource->hints, &resource->info), 0);
	assert_int_equal(fi_fabric(resource->info->fabric_attr, &resource->fabric, NULL), 0);
	assert_int_equal(fi_domain(resource->fabric, resource->info, &resource->domain, NULL), 0);

	vctx = verbs_get_ctx_op(efa_device.ibv_ctx, create_cq_ex);
#if HAVE_EFADV_CQ_EX
	g_efa_unit_test_mocks.efadv_create_cq = &efa_mock_efadv_create_cq_set_eopnotsupp_and_return_null;
	expect_function_call(efa_mock_efadv_create_cq_set_eopnotsupp_and_return_null);
#endif
	/* Mock out the create_cq_ex function pointer which is called by ibv_create_cq_ex */
	vctx->create_cq_ex = &efa_mock_create_cq_ex_return_null;
	expect_function_call(efa_mock_create_cq_ex_return_null);

	efa_domain = container_of(resource->domain, struct efa_domain, util_domain.domain_fid);
	efa_domain->device = &efa_device;

	assert_int_not_equal(fi_cq_open(resource->domain, &cq_attr, &resource->cq, NULL), 0);
	/* set cq as NULL to avoid double free by fi_close in cleanup stage */
	resource->cq = NULL;
}

/**
 * @brief get the length of the ibv_cq_poll_list for a given efa_rdm_cq
 *
 * @param cq_fid cq fid
 * @return int the length of the ibv_cq_poll_list
 */
static
int test_efa_rdm_cq_get_ibv_cq_poll_list_length(struct fid_cq *cq_fid)
{
	struct efa_rdm_cq *cq;

	cq = container_of(cq_fid, struct efa_rdm_cq, util_cq.cq_fid.fid);
	return efa_unit_test_get_dlist_length(&cq->ibv_cq_poll_list);
}

/**
 * @brief Check the length of ibv_cq_poll_list when 1 cq is bind to 1 ep
 * as both tx/rx cq.
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_cq_ibv_cq_poll_list_same_tx_rx_cq_single_ep(struct efa_resource **state)
{
	struct efa_resource *resource = *state;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	/* efa_unit_test_resource_construct binds single OFI CQ as both tx/rx cq of ep */
	assert_int_equal(test_efa_rdm_cq_get_ibv_cq_poll_list_length(resource->cq), 1);
}

/**
 * @brief Check the length of ibv_cq_poll_list when separate tx/rx cq is bind to 1 ep.
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_cq_ibv_cq_poll_list_separate_tx_rx_cq_single_ep(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct fid_cq *txcq, *rxcq;
	struct fi_cq_attr cq_attr = {0};

	efa_unit_test_resource_construct_no_cq_and_ep_not_enabled(resource, FI_EP_RDM);

	assert_int_equal(fi_cq_open(resource->domain, &cq_attr, &txcq, NULL), 0);

	assert_int_equal(fi_ep_bind(resource->ep, &txcq->fid, FI_SEND), 0);

	assert_int_equal(fi_cq_open(resource->domain, &cq_attr, &rxcq, NULL), 0);

	assert_int_equal(fi_ep_bind(resource->ep, &rxcq->fid, FI_RECV), 0);

	assert_int_equal(fi_enable(resource->ep), 0);

	assert_int_equal(test_efa_rdm_cq_get_ibv_cq_poll_list_length(txcq), 2);

	assert_int_equal(test_efa_rdm_cq_get_ibv_cq_poll_list_length(rxcq), 2);

	/* ep must be closed before cq/av/eq... */
	fi_close(&resource->ep->fid);
	resource->ep = NULL;
	fi_close(&txcq->fid);
	fi_close(&rxcq->fid);
}

void test_efa_rdm_cq_post_initial_rx_pkts(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_cq *efa_rdm_cq;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);

	/* At this time, rx pkts are not growed and posted */
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_to_post, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_posted, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_held, 0);

	assert_false(efa_rdm_cq->initial_rx_to_all_eps_posted);
	fi_cq_read(resource->cq, NULL, 0);

	/* At this time, rx pool size number of rx pkts are posted */
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_posted, efa_rdm_ep_get_rx_pool_size(efa_rdm_ep));
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_to_post, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_held, 0);

	assert_true(efa_rdm_cq->initial_rx_to_all_eps_posted);
}
#if HAVE_EFADV_CQ_EX
/**
 * @brief Construct an RDM endpoint and receive an eager MSG RTM packet.
 * Simulate EFA device by setting peer AH to unknown and make sure the
 * endpoint recovers the peer address iff(if and only if) the peer is
 * inserted to AV.
 *
 * @param resource		struct efa_resource that is managed by the framework
 * @param remove_peer	Boolean value that indicates if the peer was removed explicitly
 * @param support_efadv_cq	Boolean value that indicates if EFA device supports EFA DV CQ
 */
static void test_impl_ibv_cq_ex_read_unknow_peer_ah(struct efa_resource *resource, bool remove_peer, bool support_efadv_cq)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_pke *pkt_entry;
	struct efa_ep_addr raw_addr = {0};
	size_t raw_addr_len = sizeof(raw_addr);
	fi_addr_t peer_addr = 0;
	struct fi_cq_data_entry cq_entry;
	struct efa_unit_test_eager_rtm_pkt_attr pkt_attr = {0};
	struct efadv_cq *efadv_cq;
	struct efa_unit_test_buff recv_buff;
	int ret;
	struct efa_rdm_cq *efa_rdm_cq;

	/*
	 * Always use mocked efadv_create_cq instead of the real one.
	 * Otherwise the test is undeterministic depending on the host kernel:
	 * - If the kernel supports EFA DV CQ and we set support_efadv_cq = true, then the test will pass
	 * - If the kernel does NOT support EFA DV CQ and we set support_efadv_cq = true, then the test will fail
	 */
	if (support_efadv_cq) {
		g_efa_unit_test_mocks.efadv_create_cq = &efa_mock_efadv_create_cq_with_ibv_create_cq_ex;
		expect_function_call(efa_mock_efadv_create_cq_with_ibv_create_cq_ex);
	} else {
		g_efa_unit_test_mocks.efadv_create_cq = &efa_mock_efadv_create_cq_set_eopnotsupp_and_return_null;
		expect_function_call(efa_mock_efadv_create_cq_set_eopnotsupp_and_return_null);
	}

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_cq = container_of(resource->cq, struct efa_rdm_cq, util_cq.cq_fid.fid);

	/* Construct a minimal recv buffer */
	efa_unit_test_buff_construct(&recv_buff, resource, efa_rdm_ep->min_multi_recv_size);

	/* Create and register a fake peer */
	ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);
	raw_addr.qpn = 0;
	raw_addr.qkey = 0x1234;

	struct efa_rdm_peer *peer;
	ret = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(ret, 1);

	/* Skip handshake */
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr);
	assert_non_null(peer);
	peer->flags |= EFA_RDM_PEER_HANDSHAKE_SENT;

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

	pkt_attr.msg_id = 0;
	pkt_attr.connid = raw_addr.qkey;
	/* Packet type must be in [EFA_RDM_REQ_PKT_BEGIN, EFA_RDM_EXTRA_REQ_PKT_END) */
	efa_unit_test_eager_msgrtm_pkt_construct(pkt_entry, &pkt_attr);

	/* Setup CQ */
	efa_rdm_cq->ibv_cq.ibv_cq_ex->wr_id = (uintptr_t)pkt_entry;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->start_poll = &efa_mock_ibv_start_poll_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->next_poll = &efa_mock_ibv_next_poll_check_function_called_and_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->end_poll = &efa_mock_ibv_end_poll_check_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_slid = &efa_mock_ibv_read_slid_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_byte_len = &efa_mock_ibv_read_byte_len_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_opcode = &efa_mock_ibv_read_opcode_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_qp_num = &efa_mock_ibv_read_qp_num_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_wc_flags = &efa_mock_ibv_read_wc_flags_return_mock;
	efa_rdm_cq->ibv_cq.ibv_cq_ex->read_src_qp = &efa_mock_ibv_read_src_qp_return_mock;

	if (support_efadv_cq) {
		efadv_cq = efadv_cq_from_ibv_cq_ex(efa_rdm_cq->ibv_cq.ibv_cq_ex);
		assert_non_null(efadv_cq);
		efadv_cq->wc_read_sgid = &efa_mock_efadv_wc_read_sgid_return_zero_code_and_expect_next_poll_and_set_gid;

		/* Return unknown AH from efadv */
		will_return(efa_mock_efadv_wc_read_sgid_return_zero_code_and_expect_next_poll_and_set_gid, raw_addr.raw);
	} else {
		expect_function_call(efa_mock_ibv_next_poll_check_function_called_and_return_mock);
	}

	/* Read 1 entry with unknown AH */
	will_return(efa_mock_ibv_start_poll_return_mock, 0);
	will_return(efa_mock_ibv_next_poll_check_function_called_and_return_mock, ENOENT);
	will_return(efa_mock_ibv_end_poll_check_mock, NULL);
	will_return(efa_mock_ibv_read_slid_return_mock, 0xffff); // slid=0xffff(-1) indicates an unknown AH
	will_return(efa_mock_ibv_read_byte_len_return_mock, pkt_entry->pkt_size);
	will_return_maybe(efa_mock_ibv_read_opcode_return_mock, IBV_WC_RECV);
	will_return_maybe(efa_mock_ibv_read_qp_num_return_mock, 0);
	will_return_maybe(efa_mock_ibv_read_wc_flags_return_mock, 0);
	will_return_maybe(efa_mock_ibv_read_src_qp_return_mock, raw_addr.qpn);

	/* Post receive buffer */
	ret = fi_recv(resource->ep, recv_buff.buff, recv_buff.size, fi_mr_desc(recv_buff.mr), peer_addr, NULL /* context */);
	assert_int_equal(ret, 0);

	if (remove_peer) {
		ret = fi_av_remove(resource->av, &peer_addr, 1, 0);
		assert_int_equal(ret, 0);
	}

	ret = fi_cq_read(resource->cq, &cq_entry, 1);

	if (remove_peer || !support_efadv_cq) {
		/* Ignored WC because the peer is removed, or EFA device does not support extended CQ */
		assert_int_equal(ret, -FI_EAGAIN);
	}
	else {
		/* Found 1 matching rxe */
		assert_int_equal(ret, 1);
	}

	efa_unit_test_buff_destruct(&recv_buff);
}

/**
 * @brief Verify that RDM endpoint fi_cq_read recovers unknown peer AH
 * by querying efadv to get raw address.
 * A fake peer is registered in AV. The endpoint receives a packet from it,
 * for which the EFA device returns an unknown AH. The endpoint will retrieve
 * the peer's raw address using efadv verbs, and recover it's AH using
 * Raw:QPN:QKey.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_ibv_cq_ex_read_recover_forgotten_peer_ah(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_impl_ibv_cq_ex_read_unknow_peer_ah(resource, false, true);
}

/**
 * @brief Verify that RDM endpoint falls back to ibv_create_cq_ex if rdma-core
 * provides efadv_create_cq verb but EFA device does not support EFA DV CQ.
 * In this case the endpoint will not attempt to recover a forgotten peer's address.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_rdm_fallback_to_ibv_create_cq_ex_cq_read_ignore_forgotton_peer(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_impl_ibv_cq_ex_read_unknow_peer_ah(resource, false, false);
}

/**
 * @brief Verify that RDM endpoint progress engine ignores unknown peer AH
 * if the peer is not registered in AV, e.g. removed.
 * The endpoint receives a packet from an alien peer, which corresponds to
 * an unknown AH. The endpoint attempts to look up the AH for the peer but
 * was rightly unable to, thus ignoring the packet.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_ibv_cq_ex_read_ignore_removed_peer(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	test_impl_ibv_cq_ex_read_unknow_peer_ah(resource, true, true);
}
#else
void test_ibv_cq_ex_read_recover_forgotten_peer_ah()
{
	skip();
}
void test_rdm_fallback_to_ibv_create_cq_ex_cq_read_ignore_forgotton_peer()
{
	skip();
}
void test_ibv_cq_ex_read_ignore_removed_peer()
{
	skip();
}
#endif
