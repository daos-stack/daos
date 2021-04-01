/**
 * (C) Copyright 2021-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * include/daos/policy.h
 */

#ifndef __DAOS_POLICY_H__
#define __DAOS_POLICY_H__
#include <daos/common.h>

/**
 * tier placement policy
 */
typedef enum tier_policy {
        TIER_POLICY_DEFAULT,
        TIER_POLICY_IO_SIZE,
        TIER_POLICY_WRITE_INTENSIVITY,
        TIER_POLICY_MAX
} tier_policy_t;

bool is_policy_name_valid(const char *name);

#endif