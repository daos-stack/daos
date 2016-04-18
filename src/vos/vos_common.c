/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * Common internal functions for VOS
 * vos/vos_common.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <daos/daos_common.h>
#include "vos_internal.h"

int
vos_create_hhash(void)
{
	static pthread_mutex_t	create_mutex = PTHREAD_MUTEX_INITIALIZER;
	static int		hhash_is_create = 0;
	int			ret = 0;

	if (!hhash_is_create) {
		pthread_mutex_lock(&create_mutex);
		if (!hhash_is_create) {
			ret = daos_hhash_create(DAOS_HHASH_BITS,
						&daos_vos_hhash);
			if (ret != 0) {
				D_ERROR("VOS hhash creation error\n");
				pthread_mutex_unlock(&create_mutex);
				return ret;
			}
			hhash_is_create = 1;
		}
		pthread_mutex_unlock(&create_mutex);
	}
	return ret;
}

struct vp_hdl*
vos_pool_lookup_handle(daos_handle_t poh)
{
	struct vp_hdl		*vpool = NULL;
	struct daos_hlink	*hlink = NULL;

	hlink = daos_hhash_link_lookup(daos_vos_hhash, poh.cookie);
	if (!hlink)
		D_ERROR("VOS pool handle lookup error\n");
	else
		vpool = container_of(hlink, struct vp_hdl,
				     vp_hlink);
	return vpool;
}

inline void
vos_pool_putref_handle(struct vp_hdl *vpool)
{
	if (!vpool) {
		D_ERROR("Empty handle error\n");
		return;
	}
	daos_hhash_link_putref(daos_vos_hhash,
			       &vpool->vp_hlink);
}
