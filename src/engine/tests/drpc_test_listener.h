/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * This header defines utilities for working with a simplified test version of the
 * dRPC listener in cmocka tests.
 */

#ifndef __DAOS_TESTS_DRPC_TEST_LISTENER_H___
#define __DAOS_TESTS_DRPC_TEST_LISTENER_H___

#include "../drpc_internal.h"

struct drpc_test_state {
	struct drpc_progress_context *progress_ctx;
	char                         *test_dir;
	char                         *sock_path;
};

char *
get_greeting(const char *name);

int
drpc_listener_setup(void **state);
int
drpc_listener_teardown(void **state);

#endif /* __DAOS_TESTS_DRPC_TEST_LISTENER_H___ */
