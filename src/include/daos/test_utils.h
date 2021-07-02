/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Convenient unit test utility methods
 */

#ifndef __DAOS_TEST_UTILS_H__
#define __DAOS_TEST_UTILS_H__

#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>

/*
 * drpc unit test utilities
 */

/**
 * Creates a new drpc context with a specified socket fd. Not tied to anything
 * in the real file system.
 *
 * \param	fd	desired socket fd
 *
 * \return	Newly allocated struct drpc
 */
struct drpc *new_drpc_with_fd(int fd);

/**
 * Frees a drpc context and cleans up. Not tied to anything in the real file
 * system.
 *
 * \param	ctx	drpc ctx to free
 */
void free_drpc(struct drpc *ctx);

/**
 * Generates a valid Drpc__Call structure.
 *
 * \return	Newly allocated Drpc__Call
 */
Drpc__Call *new_drpc_call(void);

/**
 * Generates a valid Drpc__Call structure with a specific module ID.
 *
 * \return	Newly allocated Drpc__Call
 */
Drpc__Call *new_drpc_call_with_module(int module_id);

/**
 * Using mocks in test_mocks.h, sets up recvmsg mock to populate a valid
 * serialized Drpc__Call as the message received.
 */
void mock_valid_drpc_call_in_recvmsg(void);

/**
 * Using mocks in test_mocks.h, sets up recvmsg mock to populate a valid
 * serialized Drpc__Response as the message received.
 */
void mock_valid_drpc_resp_in_recvmsg(Drpc__Status status);

/**
 * Generates a valid Drpc__Response structure.
 *
 * \return	Newly allocated Drpc__Response
 */
Drpc__Response *new_drpc_response(void);

/*
 * ACL unit test utilities
 */

/**
 * Fills up ACE array with unique named users.
 *
 * \param[in][out]	ace		Array of pointers to DAOS ACEs
 * \param[in]		num_aces	Length of ace array
 */
void
fill_ace_list_with_users(struct daos_ace *ace[], size_t num_aces);

/**
 * Frees all items in ACE array.
 *
 * \param	ace		ACEs to be freed
 * \param	num_aces	Length of ace array
 */
void
free_all_aces(struct daos_ace *ace[], size_t num_aces);

#endif /* __DAOS_TEST_UTILS_H__ */
