/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_RPC_CB_H
#define MERCURY_RPC_CB_H

/**
 * test_rpc
 */
hg_return_t
hg_test_rpc_null_cb(hg_handle_t handle);
hg_return_t
hg_test_rpc_open_cb(hg_handle_t handle);
hg_return_t
hg_test_rpc_open_no_resp_cb(hg_handle_t handle);
hg_return_t
hg_test_overflow_cb(hg_handle_t handle);
hg_return_t
hg_test_cancel_rpc_cb(hg_handle_t handle);

/**
 * test_bulk
 */
hg_return_t
hg_test_bulk_write_cb(hg_handle_t handle);
hg_return_t
hg_test_bulk_bind_write_cb(hg_handle_t handle);
hg_return_t
hg_test_bulk_bind_forward_cb(hg_handle_t handle);

/**
 * test_kill
 */
hg_return_t
hg_test_killed_rpc_cb(hg_handle_t handle);

/**
 * test_nested
 */
hg_return_t
hg_test_nested1_cb(hg_handle_t handle);
hg_return_t
hg_test_nested2_cb(hg_handle_t handle);

#endif /* MERCURY_RPC_CB_H */
