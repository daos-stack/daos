/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdint.h>
#include <daos_security.h>

/**
 * Common utilities for security unit tests.
 */

void
free_ace_list(struct daos_ace **aces, size_t len);

struct daos_acl *
get_acl_with_perms(uint64_t owner_perms, uint64_t group_perms);

struct daos_acl *
get_user_acl_with_perms(const char *user, uint64_t perms);
