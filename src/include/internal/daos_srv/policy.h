/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_POLICY_H__
#define __DAOS_POLICY_H__
#include <daos/common.h>

/** tier placement policy */
enum tier_policy_t {
	DAOS_MEDIA_POLICY_IO_SIZE,
	DAOS_MEDIA_POLICY_WRITE_INTENSIVITY,
	DAOS_MEDIA_POLICY_MAX
};

/** max amount of policy parameters */
#define DAOS_MEDIA_POLICY_PARAMS_MAX    (4)

/** policy descriptor - holds policy index and optional parameters */
struct policy_desc_t {
	enum tier_policy_t	policy;
	uint32_t		params[DAOS_MEDIA_POLICY_PARAMS_MAX];
};

bool daos_policy_try_parse(const char *policy_str,
			   struct policy_desc_t *out_pd);

#endif
