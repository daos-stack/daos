/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"


#if HAVE_NEURON
/**
 * @brief Verify when neuron_alloc failed (return null),
 * efa_domain_open, which call efa_hmem_info_update_neuron
 * when HAVE_NEURON=1, will still return 0 but leave
 * efa_hmem_info[FI_HMEM_NEURON].initialized and
 * efa_hmem_info[FI_HMEM_NEURON].p2p_supported_by_device as false.
 *
 * @param[in]	state		struct efa_resource that is managed by the framework
 */
void test_efa_hmem_info_update_neuron(struct efa_resource **state)
{
        int ret;
        struct efa_resource *resource = *state;
        struct efa_domain *efa_domain;
        uint32_t efa_device_caps_orig;
        bool neuron_initialized_orig;

        resource->hints = efa_unit_test_alloc_hints(FI_EP_RDM);
        assert_non_null(resource->hints);

        ret = fi_getinfo(FI_VERSION(1, 14), NULL, NULL, 0ULL, resource->hints, &resource->info);
        assert_int_equal(ret, 0);

        ret = fi_fabric(resource->info->fabric_attr, &resource->fabric, NULL);
        assert_int_equal(ret, 0);

        neuron_initialized_orig = hmem_ops[FI_HMEM_NEURON].initialized;
        hmem_ops[FI_HMEM_NEURON].initialized = true;
        efa_device_caps_orig = g_device_list[0].device_caps;
        g_device_list[0].device_caps |= EFADV_DEVICE_ATTR_CAPS_RDMA_READ;
        g_efa_unit_test_mocks.neuron_alloc = &efa_mock_neuron_alloc_return_null;

        ret = fi_domain(resource->fabric, resource->info, &resource->domain, NULL);

        /* recover the modified global variables before doing check */
        hmem_ops[FI_HMEM_NEURON].initialized = neuron_initialized_orig;
        g_device_list[0].device_caps = efa_device_caps_orig;

        assert_int_equal(ret, 0);
        efa_domain = container_of(resource->domain, struct efa_domain,
				  util_domain.domain_fid.fid);
        assert_false(efa_domain->hmem_info[FI_HMEM_NEURON].initialized);
        assert_false(efa_domain->hmem_info[FI_HMEM_NEURON].p2p_supported_by_device);
}
#else
void test_efa_hmem_info_update_neuron()
{
        skip();
}
#endif /* HAVE_NEURON */
