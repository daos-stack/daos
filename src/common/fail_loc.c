/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * common/fail_loc.c to inject failure scenario
 */
#include <daos/common.h>

uint32_t daos_fail_loc;

#define DAOS_FAIL_MASK_MOD	0x0000ff00
#define DAOS_FAIL_MASK_LOC	(DAOS_FAIL_MASK_MOD | 0x000000ff)

void
daos_reset_fail_loc()
{
	daos_fail_loc = 0;
}

int
daos_fail_check(uint32_t id, uint32_t ret_value)
{
	if ((daos_fail_loc & DAOS_FAIL_MASK_LOC) ==
	   (id & DAOS_FAIL_MASK_LOC)) {
		D_ERROR("**** daos_fail_loc=0x%x, id =0x%x rc = %d***\n",
			daos_fail_loc, id, ret_value);
		daos_reset_fail_loc();
		return ret_value;
	}

	return 0;
}

void
daos_fail_loc_set(uint32_t id)
{
	daos_fail_loc = id;
}

int
daos_fail_loc_init()
{
	char *fail_loc_str;

	fail_loc_str = getenv("DAOS_FAIL_LOC_MASK");
	if (fail_loc_str == NULL) {
		daos_fail_loc = 0;
		return 0;
	}

	daos_fail_loc = (uint32_t)strtoul(fail_loc_str, NULL, 0);
	if (daos_fail_loc == 0)
		D_ERROR("DAOS_FAIL_LOC_MASK %s might be invalid\n",
			fail_loc_str);
	else
		D_DEBUG(DF_MISC, "daos_fail_loc = 0x%x\n", daos_fail_loc);

	return 0;
}
