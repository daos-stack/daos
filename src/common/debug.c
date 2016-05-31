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
 * common/debug.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */

#include <daos/common.h>

static unsigned int	debug_mask	= DF_UNKNOWN;

unsigned int
daos_debug_mask(void)
{
	char	*feats;

	if (debug_mask != DF_UNKNOWN)
		return debug_mask;

	feats = getenv(DAOS_ENV_DEBUG);
	if (feats != NULL) {
		debug_mask = atoi(feats);
		if (debug_mask > 0) {
			D_PRINT("set debug to %d/%x\n", debug_mask, debug_mask);
			return debug_mask;
		}
	}

	debug_mask = 0;
	return debug_mask;
}
