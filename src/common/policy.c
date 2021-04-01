/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/common/policy.c
 */
#include <daos/common.h>
#include <daos_prop.h>
#include <daos/policy.h>


/* policy name to index mapping table */
static const char* policy_names[TIER_POLICY_MAX] = {
        [TIER_POLICY_DEFAULT] = "default",
        [TIER_POLICY_IO_SIZE] = "io_size",
        [TIER_POLICY_WRITE_INTENSIVITY] = "write_intensivity",
};

bool is_policy_name_valid(const char *name)
{
        for (int i = 0; i < TIER_POLICY_MAX; i++) {
                if (strncmp(name, policy_names[i], DAOS_PROP_POLICY_MAX_LEN) == 0)
                        return true;
        }

        return false;
}