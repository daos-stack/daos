/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

void test_efa_mr_reg_counters(struct efa_resource **state)
{
    struct efa_resource *resource = *state;
    struct efa_domain *efa_domain;
    size_t mr_size = 64;
    char *buf;
    struct fid_mr *mr;

    efa_unit_test_resource_construct(resource, FI_EP_RDM);

    efa_domain = container_of(resource->domain, struct efa_domain, util_domain.domain_fid);
    assert_true(efa_domain->ibv_mr_reg_ct == 0);
    assert_true(efa_domain->ibv_mr_reg_sz == 0);


    buf = malloc(mr_size);
    assert_non_null(buf);

    assert_int_equal(fi_mr_reg(resource->domain, buf, mr_size,
            FI_SEND | FI_RECV, 0, 0, 0, &mr, NULL), 0);

    assert_true(efa_domain->ibv_mr_reg_ct == 1);
    assert_true(efa_domain->ibv_mr_reg_sz == mr_size);

    assert_int_equal(fi_close(&mr->fid), 0);
    assert_true(efa_domain->ibv_mr_reg_ct == 0);
    assert_true(efa_domain->ibv_mr_reg_sz == 0);

    free(buf);
}
