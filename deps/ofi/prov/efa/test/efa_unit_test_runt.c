/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

/**
 * @brief Test the runt size returned by efa_rdm_peer_get_runt_size
 *
 * @param resource efa_resource
 * @param iface hmem iface
 * @param peer_num_runt_bytes_in_flight how many runt bytes are in flight on the peer side
 * @param total_runt_size the total runt size
 * @param total_len the total length of the message to be sent
 * @param expected_runt_size the expected runt size
 */
static
void test_efa_rdm_peer_get_runt_size_impl(
		struct efa_resource *resource,
		enum fi_hmem_iface iface, size_t peer_num_runt_bytes_in_flight,
		size_t total_runt_size, size_t total_len, size_t expected_runt_size)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_ep_addr raw_addr;
	size_t raw_addr_len = sizeof(raw_addr);
	struct efa_rdm_peer *peer;
	fi_addr_t addr;
	struct efa_mr mock_mr;
	struct efa_rdm_ope mock_txe;
	size_t runt_size;
	struct efa_domain *efa_domain;
	int ret;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_domain = efa_rdm_ep_domain(efa_rdm_ep);
	efa_domain->hmem_info[iface].runt_size = total_runt_size;

	/* insert a fake peer */
	ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	ret = fi_av_insert(resource->av, &raw_addr, 1, &addr, 0 /* flags */, NULL /* context */);
	assert_int_equal(ret, 1);
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, addr);
	peer->num_runt_bytes_in_flight = peer_num_runt_bytes_in_flight;

	mock_mr.peer.iface = iface;

	memset(&mock_txe, 0, sizeof(mock_txe));
	mock_txe.total_len = total_len;
	mock_txe.addr = addr;
	mock_txe.desc[0] = &mock_mr;

	runt_size = efa_rdm_peer_get_runt_size(peer, efa_rdm_ep, &mock_txe);
	assert_true(runt_size == expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_no_enough_runt(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1001;
	total_runt_size = 1000;
	/* 1001 is exceeding 1000, cannot runt */
	expected_runt_size = 0;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_SYSTEM, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_smaller_than_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1000;
	total_runt_size = 1048;
	/* 1048 - 1000 is smaller than cuda memory alignment (64), runt size must be 0 */
	expected_runt_size = 0;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_exceeding_total_len(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 0;
	total_runt_size = 16384;
	/* 16384 - 0 is exceeding 12000 (total_len), runt size must be 12000 // 64 * 64 = 11968 */
	expected_runt_size = 11968;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_normal(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 10000;
	total_runt_size = 16384;
	/* 16384 - 10000 is smaller than 12000 (total_len), runt size must be (16384 - 10000) // 64 * 64 = 6336 */
	expected_runt_size = 6336;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

/* When using LL128 protocol, the segmented size of runting read must be 128 multiple. */
void test_efa_rdm_peer_get_runt_size_cuda_memory_128_multiple_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 10240;
	total_runt_size = 16384;
	/* 16384 - 10240 is smaller than 12000 (total_len), 
	 * runt size must be (16384 - 10240) // 128 * 128 = 6144 
	 */
	expected_runt_size = 6144;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_non_128_multiple_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 512;
	total_runt_size = 1004;
	/* 1004 - 512 is smaller than 12000 (total_len), 
	 * runt size must be (1004 - 512) // 128 * 128 = 384 
	 */
	expected_runt_size = 384;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_smaller_than_128_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1000;
	total_runt_size = 1048;
	/* 1048 - 1000 is smaller than 128 memory alignment, runt size must be 0 */
	expected_runt_size = 0;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_cuda_memory_exceeding_total_len_128_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->sendrecv_in_order_aligned_128_bytes = 1;

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 0;
	total_runt_size = 16384;
	/* 16384 - 0 is exceeding 12000 (total_len), runt size must be 12000 // 128 * 128 = 11904 */
	expected_runt_size = 11904;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_host_memory_smaller_than_alignment(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1000;
	total_runt_size = 1004;
	/* 1004 - 1000 is smaller than host memory alignment (8), runt size must be 0 */
	expected_runt_size = 0;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_SYSTEM, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_host_memory_exceeding_total_len(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 1111;
	peer_num_runt_bytes_in_flight = 0;
	total_runt_size = 16384;
	/* 16384 - 0 is exceeding 1111 (total_len), runt size must be 1111 // 8 * 8 = 1104 */
	expected_runt_size = 1104;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_SYSTEM, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

void test_efa_rdm_peer_get_runt_size_host_memory_normal(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t expected_runt_size;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 10000;
	total_runt_size = 11111;
	/* 11111 - 10000 is smaller than 12000 (total_len), runt size must be (11111 - 10000) // 8 * 8 = 1104 */
	expected_runt_size = 1104;
	test_efa_rdm_peer_get_runt_size_impl(resource, FI_HMEM_SYSTEM, peer_num_runt_bytes_in_flight, total_runt_size, msg_length, expected_runt_size);
}

/**
 * @brief Test the protocol returned by efa_rdm_peer_select_readbase_rtm()
 *
 * @param resource efa test resources
 * @param iface hmem iface
 * @param peer_num_runt_bytes_in_flight how many runt bytes are in flight on the peer side
 * @param total_runt_size the total runt size
 * @param total_len the total length of the message to be sent
 * @param op the operational code (ofi_op_msg or ofi_op_tag)
 * @param fi_flags the op flags of the send operations
 * @param expected_protocol the expected protocol to be selected
 */
static
void test_efa_rdm_peer_select_readbase_rtm_impl(
		struct efa_resource *resource,
		enum fi_hmem_iface iface, size_t peer_num_runt_bytes_in_flight,
		size_t total_runt_size, size_t total_len,
		int op, uint64_t fi_flags, int expected_protocol)
{
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_ep_addr raw_addr;
	size_t raw_addr_len = sizeof(raw_addr);
	struct efa_rdm_peer *peer;
	fi_addr_t addr;
	struct efa_mr mock_mr;
	struct efa_rdm_ope mock_txe;
	struct efa_domain *efa_domain;
	int readbase_rtm;
	int ret;

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_domain = efa_rdm_ep_domain(efa_rdm_ep);
	efa_domain->hmem_info[iface].runt_size = total_runt_size;

	/* insert a fake peer */
	ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);
	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	ret = fi_av_insert(resource->av, &raw_addr, 1, &addr, 0 /* flags */, NULL /* context */);
	assert_int_equal(ret, 1);
	peer = efa_rdm_ep_get_peer(efa_rdm_ep, addr);
	peer->num_runt_bytes_in_flight = peer_num_runt_bytes_in_flight;

	mock_mr.peer.iface = iface;

	memset(&mock_txe, 0, sizeof(mock_txe));
	mock_txe.total_len = total_len;
	mock_txe.addr = addr;
	mock_txe.desc[0] = &mock_mr;
	mock_txe.op = op;
	mock_txe.fi_flags = fi_flags;

	readbase_rtm = efa_rdm_peer_select_readbase_rtm(peer, efa_rdm_ep, &mock_txe);
	assert_true(readbase_rtm == expected_protocol);
}

void test_efa_rdm_peer_select_readbase_rtm_no_runt(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1000;
	total_runt_size = 1048;
	/* 1048 - 1000 is smaller than cuda memory alignment, runt size must be 0, use long read protocol */
	test_efa_rdm_peer_select_readbase_rtm_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight,
	total_runt_size, msg_length, ofi_op_msg, 0, EFA_RDM_LONGREAD_MSGRTM_PKT);
}

void test_efa_rdm_peer_select_readbase_rtm_do_runt(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	size_t msg_length;
	size_t peer_num_runt_bytes_in_flight;
	size_t total_runt_size;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);

	msg_length = 12000;
	peer_num_runt_bytes_in_flight = 1000;
	total_runt_size = 2000;
	/* 2000 - 1000 is larger than cuda memory alignment, should use runt read protocol */
	test_efa_rdm_peer_select_readbase_rtm_impl(resource, FI_HMEM_CUDA, peer_num_runt_bytes_in_flight,
	total_runt_size, msg_length, ofi_op_msg, 0, EFA_RDM_RUNTREAD_MSGRTM_PKT);
}