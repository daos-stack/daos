/**
 * (C) Copyright 2019 Intel Corporation.
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
 * common/errno.c
 *
 */
#include <daos/common.h>

/** Define the string constants for errno */
D_DEFINE_RANGE_ERRSTR(DAOS)

int
daos_errno_init(void)
{
	int	rc;

	rc = D_REGISTER_RANGE(DAOS);
	if (rc != 0) {
		D_ERROR("Unable to register error range for DAOS: rc = "
			DF_RC"\n", DP_RC(rc));
	}

	return rc;
}

void
daos_errno_fini(void)
{
	D_DEREGISTER_RANGE(DAOS);
}


