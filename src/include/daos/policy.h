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
enum tier_policy_t {
        DAOS_MEDIA_POLICY_NOT_EXIST = -1,
        DAOS_MEDIA_POLICY_DEFAULT,
        DAOS_MEDIA_POLICY_IO_SIZE,
        DAOS_MEDIA_POLICY_WRITE_INTENSIVITY,
        DAOS_MEDIA_POLICY_MAX
};

int get_policy_index(const char *name);

#endif