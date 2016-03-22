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
 * Server-side API of the DAOS-M layer.
 */

#ifndef __DSM_SRV_H__
#define __DSM_SRV_H__

#include <daos/daos_transport.h>

int
dsms_pool_create(const uuid_t uuid, unsigned int uid, unsigned int gid,
		 unsigned int mode, int ntargets, const dtp_phy_addr_t *targets,
		 int ndomains, const int *domains, const char *path);

/* TODO(liwei): Can dmg simply remove the file without calling into dsms? */
void
dsms_pool_destroy(const uuid_t uuid, const char *path);

#endif /* __DSM_SRV_H__ */
