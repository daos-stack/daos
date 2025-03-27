/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"
#include "ofi_util.h"
#include "efa_rdm_ep.h"

#define MSG_SIZE 10

void test_efa_rdm_msg_send_to_local_peer_with_null_desc(struct efa_resource **state)
{
        struct efa_resource *resource = *state;
        char buf[MSG_SIZE];
        int i;
        struct iovec iov;
        struct efa_ep_addr raw_addr;
	size_t raw_addr_len = sizeof(raw_addr);
        fi_addr_t addr;
        int ret;
        struct fi_msg msg = {0};
        struct fi_msg_tagged tmsg = {0};

        efa_unit_test_resource_construct(resource, FI_EP_RDM);

        ret = fi_getname(&resource->ep->fid, &raw_addr, &raw_addr_len);
	assert_int_equal(ret, 0);

	raw_addr.qpn = 1;
	raw_addr.qkey = 0x1234;
	ret = fi_av_insert(resource->av, &raw_addr, 1, &addr, 0 /* flags */, NULL /* context */);
	assert_int_equal(ret, 1);

        for (i = 0; i < MSG_SIZE; i++)
                buf[i] = 'a' + i;

        iov.iov_base = buf;
        iov.iov_len = MSG_SIZE;

        efa_unit_test_construct_msg(&msg, &iov, 1, addr, NULL, 0, NULL);

        efa_unit_test_construct_tmsg(&tmsg, &iov, 1, addr, NULL, 0, NULL, 0, 0);

        /* The peer won't be verified by shm so it is expected that EAGAIN will be returned */
        ret = fi_send(resource->ep, buf, MSG_SIZE, NULL, addr, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_sendv(resource->ep, &iov, NULL, 1, addr, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_senddata(resource->ep, buf, MSG_SIZE, NULL, 0, addr, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_sendmsg(resource->ep, &msg, 0);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_tsend(resource->ep, buf, MSG_SIZE, NULL, addr, 0, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_tsendv(resource->ep, &iov, NULL, 1, addr, 0, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_tsenddata(resource->ep, buf, MSG_SIZE, NULL, 0, addr, 0, NULL);
        assert_int_equal(ret, -FI_EAGAIN);

        ret = fi_tsendmsg(resource->ep, &tmsg, 0);
        assert_int_equal(ret, -FI_EAGAIN);
}
