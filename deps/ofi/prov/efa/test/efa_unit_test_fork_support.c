/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include "efa_unit_tests.h"

/**
 * @brief verify efa_fork_support_request_initialize() set value of g_efa_fork_status and efa_env.use_huge_page correctly
 */
void test_efa_fork_support_request_initialize_when_ibv_fork_support_is_needed(void **state)
{
	setenv("FI_EFA_FORK_SAFE", "1", true);
	will_return(efa_mock_ibv_is_fork_initialized_return_mock, IBV_FORK_DISABLED);
	g_efa_unit_test_mocks.ibv_is_fork_initialized = &efa_mock_ibv_is_fork_initialized_return_mock;

	efa_fork_support_request_initialize();
	assert_int_equal(g_efa_fork_status, EFA_FORK_SUPPORT_ON);
	/* when user space fork support is on, EFA provider should not use huge page*/
	assert_int_equal(efa_env.huge_page_setting, EFA_ENV_HUGE_PAGE_DISABLED);

	g_efa_unit_test_mocks.ibv_is_fork_initialized = __real_ibv_is_fork_initialized;
	unsetenv("FI_EFA_FORK_SAFE");
}

/**
 * @brief verify efa_fork_support_request_initialize() set value of g_efa_fork_status and efa_env.use_huge_page correctly
 */
void test_efa_fork_support_request_initialize_when_ibv_fork_support_is_unneeded(void **state)
{
	setenv("FI_EFA_FORK_SAFE", "1", true);
	will_return(efa_mock_ibv_is_fork_initialized_return_mock, IBV_FORK_UNNEEDED);
	g_efa_unit_test_mocks.ibv_is_fork_initialized = &efa_mock_ibv_is_fork_initialized_return_mock;

	efa_fork_support_request_initialize();
	assert_int_equal(g_efa_fork_status, EFA_FORK_SUPPORT_UNNEEDED);

	g_efa_unit_test_mocks.ibv_is_fork_initialized = __real_ibv_is_fork_initialized;
	unsetenv("FI_EFA_FORK_SAFE");
}
