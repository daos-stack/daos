/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */
#include "efa_unit_tests.h"
#include "efa_cntr.h"

/**
 * @brief get the length of the ibv_cq_poll_list for a given efa_rdm_cq
 *
 * @param cq_fid cq fid
 * @return int the length of the ibv_cq_poll_list
 */
static
int test_efa_rdm_cntr_get_ibv_cq_poll_list_length(struct fid_cntr *cntr_fid)
{
	int i = 0;
	struct dlist_entry *item;
	struct efa_cntr *cntr;

	cntr = container_of(cntr_fid, struct efa_cntr, util_cntr.cntr_fid.fid);
	dlist_foreach(&cntr->ibv_cq_poll_list, item) {
		i++;
	}

	return i;
}

/**
 * @brief Check the length of ibv_cq_poll_list in cntr when 1 cq is bind to 1 ep
 * as both tx/rx cq.
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_cntr_ibv_cq_poll_list_same_tx_rx_cq_single_ep(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
    struct fid_cntr *cntr;
    struct fi_cntr_attr cntr_attr = {0};

	efa_unit_test_resource_construct_ep_not_enabled(resource, FI_EP_RDM);

    assert_int_equal(fi_cntr_open(resource->domain, &cntr_attr, &cntr, NULL), 0);

    /* TODO: expand this test to all flags */
	assert_int_equal(fi_ep_bind(resource->ep, &cntr->fid, FI_TRANSMIT), 0);

    assert_int_equal(fi_enable(resource->ep), 0);

	/* efa_unit_test_resource_construct binds single OFI CQ as both tx/rx cq of ep */
	assert_int_equal(test_efa_rdm_cntr_get_ibv_cq_poll_list_length(cntr), 1);

    /* ep must be closed before cq/av/eq... */
	fi_close(&resource->ep->fid);
	resource->ep = NULL;

    fi_close(&cntr->fid);
}

/**
 * @brief Check the length of ibv_cq_poll_list in cntr when separate tx/rx cq is bind to 1 ep.
 *
 * @param state struct efa_resource that is managed by the framework
 */
void test_efa_rdm_cntr_ibv_cq_poll_list_separate_tx_rx_cq_single_ep(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct fid_cq *txcq, *rxcq;
	struct fi_cq_attr cq_attr = {0};
    struct fid_cntr *cntr;
    struct fi_cntr_attr cntr_attr = {0};

	efa_unit_test_resource_construct_no_cq_and_ep_not_enabled(resource, FI_EP_RDM);

	assert_int_equal(fi_cq_open(resource->domain, &cq_attr, &txcq, NULL), 0);

	assert_int_equal(fi_ep_bind(resource->ep, &txcq->fid, FI_SEND), 0);

	assert_int_equal(fi_cq_open(resource->domain, &cq_attr, &rxcq, NULL), 0);

	assert_int_equal(fi_ep_bind(resource->ep, &rxcq->fid, FI_RECV), 0);

    assert_int_equal(fi_cntr_open(resource->domain, &cntr_attr, &cntr, NULL), 0);

    /* TODO: expand this test to all flags */
	assert_int_equal(fi_ep_bind(resource->ep, &cntr->fid, FI_TRANSMIT), 0);

	assert_int_equal(fi_enable(resource->ep), 0);

	assert_int_equal(test_efa_rdm_cntr_get_ibv_cq_poll_list_length(cntr), 2);

	/* ep must be closed before cq/av/eq... */
	fi_close(&resource->ep->fid);
	resource->ep = NULL;
	fi_close(&txcq->fid);
	fi_close(&rxcq->fid);
    fi_close(&cntr->fid);
}

void test_efa_cntr_post_initial_rx_pkts(struct efa_resource **state)
{
	struct efa_resource *resource = *state;
	struct efa_rdm_ep *efa_rdm_ep;
	struct fid_cntr *cntr;
	struct fi_cntr_attr cntr_attr = {0};
	struct efa_cntr *efa_cntr;
	uint64_t cnt;

	efa_unit_test_resource_construct_ep_not_enabled(resource, FI_EP_RDM);
	efa_rdm_ep = container_of(resource->ep, struct efa_rdm_ep, base_ep.util_ep.ep_fid);

	/* At this time, rx pkts are not growed and posted */
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_to_post, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_posted, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_held, 0);

	assert_int_equal(fi_cntr_open(resource->domain, &cntr_attr, &cntr, NULL), 0);

	/* TODO: expand this test to all flags */
	assert_int_equal(fi_ep_bind(resource->ep, &cntr->fid, FI_TRANSMIT), 0);

	assert_int_equal(fi_enable(resource->ep), 0);

	efa_cntr = container_of(cntr, struct efa_cntr, util_cntr.cntr_fid);

	assert_false(efa_cntr->initial_rx_to_all_eps_posted);

	cnt = fi_cntr_read(cntr);
	/* No completion should be read */
	assert_int_equal(cnt, 0);

	/* At this time, rx pool size number of rx pkts are posted */
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_posted, efa_rdm_ep_get_rx_pool_size(efa_rdm_ep));
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_to_post, 0);
	assert_int_equal(efa_rdm_ep->efa_rx_pkts_held, 0);

	assert_true(efa_cntr->initial_rx_to_all_eps_posted);
	/* ep must be closed before cq/av/eq... */
	fi_close(&resource->ep->fid);
	resource->ep = NULL;

	fi_close(&cntr->fid);
}
