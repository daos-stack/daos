/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

/*
 * test the error handling path of efa_device_construct()
 */
void test_efa_device_construct_error_handling()
{
	int ibv_err = 4242;
	struct ibv_device **ibv_device_list;
	struct efa_device efa_device = {0};

	ibv_device_list = ibv_get_device_list(&g_device_cnt);
	if (ibv_device_list == NULL) {
		skip();
		return;
	}

	g_efa_unit_test_mocks.efadv_query_device = &efa_mock_efadv_query_device_return_mock;
	will_return(efa_mock_efadv_query_device_return_mock, ibv_err);

	efa_device_construct(&efa_device, 0, ibv_device_list[0]);

	/* when error happend, resources in efa_device should be NULL */
	assert_null(efa_device.ibv_ctx);
	assert_null(efa_device.rdm_info);
	assert_null(efa_device.dgram_info);
}

