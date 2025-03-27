/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

/* test fi_open_ops with a wrong name */
void test_efa_domain_open_ops_wrong_name(struct efa_resource **state)
{
    struct efa_resource *resource = *state;
    int ret;
    struct fi_efa_ops_domain *efa_domain_ops;

    efa_unit_test_resource_construct(resource, FI_EP_RDM);

    ret = fi_open_ops(&resource->domain->fid, "arbitrary name", 0, (void **)&efa_domain_ops, NULL);
    assert_int_equal(ret, -FI_EINVAL);
}

static
void test_efa_domain_open_ops_mr_query_common(
                            struct efa_resource *resource,
                            int expected_ret,
                            uint16_t expected_ic_id_validity,
                            uint16_t expected_recv_ic_id,
                            uint16_t expected_rdma_read_ic_id,
                            uint16_t expected_rdma_recv_ic_id)
{
    int ret;
    struct fi_efa_ops_domain *efa_domain_ops;
    struct fi_efa_mr_attr efa_mr_attr = {0};
    struct efa_mr mr = {0};
    struct fid_mr mr_fid = {0};

    mr.mr_fid = mr_fid;
    mr.ibv_mr = NULL;

    ret = fi_open_ops(&resource->domain->fid, FI_EFA_DOMAIN_OPS, 0, (void **)&efa_domain_ops, NULL);
    assert_int_equal(ret, 0);

    ret = efa_domain_ops->query_mr(&mr.mr_fid, &efa_mr_attr);
    assert_int_equal(ret, expected_ret);

    if (expected_ret == -FI_ENOSYS)
        return;

    assert_true(efa_mr_attr.ic_id_validity == expected_ic_id_validity);

    if (efa_mr_attr.ic_id_validity & FI_EFA_MR_ATTR_RECV_IC_ID)
        assert_true(efa_mr_attr.recv_ic_id == expected_recv_ic_id);

    if (efa_mr_attr.ic_id_validity & FI_EFA_MR_ATTR_RDMA_READ_IC_ID)
        assert_true(efa_mr_attr.rdma_read_ic_id == expected_rdma_read_ic_id);

    if (efa_mr_attr.ic_id_validity & FI_EFA_MR_ATTR_RDMA_RECV_IC_ID)
        assert_true(efa_mr_attr.rdma_recv_ic_id == expected_rdma_recv_ic_id);
}

#if HAVE_EFADV_QUERY_MR

void test_efa_domain_open_ops_mr_query(struct efa_resource **state)
{
    struct efa_resource *resource = *state;

    efa_unit_test_resource_construct(resource, FI_EP_RDM);

    /* set recv_ic_id as 0 */
    g_efa_unit_test_mocks.efadv_query_mr = &efa_mock_efadv_query_mr_recv_ic_id_0;

    test_efa_domain_open_ops_mr_query_common(
                                resource,
                                0,
                                FI_EFA_MR_ATTR_RECV_IC_ID,
                                0,
                                0 /* ignored */,
                                0 /* ignored */);

    /* set rdma_read_ic_id as 1 */
    g_efa_unit_test_mocks.efadv_query_mr = &efa_mock_efadv_query_mr_rdma_read_ic_id_1;

    test_efa_domain_open_ops_mr_query_common(
                                resource,
                                0,
                                FI_EFA_MR_ATTR_RDMA_READ_IC_ID,
                                0 /* ignored */,
                                1,
                                0 /* ignored */);

    /* set rdma_recv_ic_id as 2 */
    g_efa_unit_test_mocks.efadv_query_mr = &efa_mock_efadv_query_mr_rdma_recv_ic_id_2;

    test_efa_domain_open_ops_mr_query_common(
                                resource,
                                0,
                                FI_EFA_MR_ATTR_RDMA_RECV_IC_ID,
                                0 /* ignored */,
                                0 /* ignored */,
                                2);

    /* set recv_ic_id as 0, rdma_read_ic_id as 1 */
    g_efa_unit_test_mocks.efadv_query_mr = &efa_mock_efadv_query_mr_recv_and_rdma_read_ic_id_0_1;

    test_efa_domain_open_ops_mr_query_common(
                                resource,
                                0,
                                FI_EFA_MR_ATTR_RECV_IC_ID | FI_EFA_MR_ATTR_RDMA_READ_IC_ID,
                                0,
                                1,
                                0 /* ignored */);
}

#else

void test_efa_domain_open_ops_mr_query(struct efa_resource **state)
{
    struct efa_resource *resource = *state;

    efa_unit_test_resource_construct(resource, FI_EP_RDM);

    test_efa_domain_open_ops_mr_query_common(
                                resource,
                                -FI_ENOSYS,
                                0, /* ignored */
                                0, /* ignored */
                                1, /* ignored */
                                0  /* ignored */);
}

#endif /* HAVE_EFADV_QUERY_MR */