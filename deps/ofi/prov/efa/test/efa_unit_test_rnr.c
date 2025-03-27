/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"
#include "efa_rdm_pke_cmd.h"

/**
 * @brief this test validate that during RNR queuing and resending,
 * the "rnr_queued_pkt_cnt" in endpoint and peer were properly updated,
 * so is the EFA_RDM_PKE_RNR_RETRANSMIT flag.
 */
void test_efa_rnr_queue_and_resend(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_unit_test_buff send_buff;
	struct efa_ep_addr raw_addr;
	struct efa_rdm_ep *efa_rdm_ep;
	struct efa_rdm_ope *txe;
	struct efa_rdm_pke *pkt_entry;
	size_t raw_addr_len = sizeof(raw_addr);
	fi_addr_t peer_addr;
	int ret;

	efa_unit_test_resource_construct(resource, FI_EP_RDM);
	efa_unit_test_buff_construct(&send_buff, resource, 4096 /* buff_size */);
	/* Create and register a fake peer */
	ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);
	raw_addr.qpn = 0;
	raw_addr.qkey = 0x1234;

	ret = fi_av_insert(resource->av, &raw_addr, 1, &peer_addr, 0, NULL);
	assert_int_equal(ret, 1);

	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);
	efa_rdm_ep->base_ep.qp->ibv_qp_ex->wr_start = &efa_mock_ibv_wr_start_no_op;
	efa_rdm_ep->base_ep.qp->ibv_qp_ex->wr_send = &efa_mock_ibv_wr_send_save_wr;
	efa_rdm_ep->base_ep.qp->ibv_qp_ex->wr_set_ud_addr = &efa_mock_ibv_wr_set_ud_addr_no_op;
	efa_rdm_ep->base_ep.qp->ibv_qp_ex->wr_set_sge_list = &efa_mock_ibv_wr_set_sge_list_no_op;
	efa_rdm_ep->base_ep.qp->ibv_qp_ex->wr_complete = &efa_mock_ibv_wr_complete_no_op;
	assert_true(dlist_empty(&efa_rdm_ep->txe_list));

	/* close shm_ep to force efa_rdm_ep to use efa device to send */
	if (efa_rdm_ep->shm_ep) {
		ret = fi_close(&efa_rdm_ep->shm_ep->fid);
		assert_int_equal(ret, 0);
		efa_rdm_ep->shm_ep = NULL;
	}

	ret = fi_send(resource->ep, send_buff.buff, send_buff.size, fi_mr_desc(send_buff.mr), peer_addr, NULL /* context */);
	assert_int_equal(ret, 0);
	assert_false(dlist_empty(&efa_rdm_ep->txe_list));
	assert_int_equal(g_ibv_submitted_wr_id_cnt, 1);

	txe = container_of(efa_rdm_ep->txe_list.next, struct efa_rdm_ope, ep_entry);
	pkt_entry = (struct efa_rdm_pke *)g_ibv_submitted_wr_id_vec[0];

	efa_rdm_ep_record_tx_op_completed(efa_rdm_ep, pkt_entry);
	efa_rdm_ep_queue_rnr_pkt(efa_rdm_ep, &txe->queued_pkts, pkt_entry);
	assert_int_equal(pkt_entry->flags & EFA_RDM_PKE_RNR_RETRANSMIT, EFA_RDM_PKE_RNR_RETRANSMIT);
	assert_int_equal(efa_rdm_ep->efa_rnr_queued_pkt_cnt, 1);
	assert_int_equal(efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr)->rnr_queued_pkt_cnt, 1);

	ret = efa_rdm_ep_post_queued_pkts(efa_rdm_ep, &txe->queued_pkts);
	assert_int_equal(ret, 0);
	assert_int_equal(pkt_entry->flags & EFA_RDM_PKE_RNR_RETRANSMIT, 0);
	assert_int_equal(efa_rdm_ep->efa_rnr_queued_pkt_cnt, 0);
	assert_int_equal(efa_rdm_ep_get_peer(efa_rdm_ep, peer_addr)->rnr_queued_pkt_cnt, 0);

	efa_rdm_pke_handle_send_completion(pkt_entry);

	efa_unit_test_buff_destruct(&send_buff);
}
