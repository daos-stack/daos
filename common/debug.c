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
