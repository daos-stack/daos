/**
 * (C) Copyright 2015 Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/**
 * This file is part of daos
 *
 * common/debug.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <daos_common.h>

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
