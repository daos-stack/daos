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
 * Layout definition for VOS root object
 * vos/include/vos_internal.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#ifndef _VOS_INTERNAL_H
#define _VOS_INTERNAL_H

#include <daos/daos_list.h>
#include <daos/daos_hash.h>

#include "vos_layout.h"

/**
 * VOS pool handle
 */
struct vos_pool {
	struct daos_hlink	vpool_hlink;
	PMEMobjpool		*ph;
	char			*path;
};

struct vos_co_hdl {
	struct daos_hlink        co_hdl_hlink;
	PMEMobjpool		*ph;
	uuid_t			 container_id;
	struct vos_object_table	*obj_table;
	struct vos_epoch_table	*epoch_table;
};

/*
 * Handle hash globals
 **/
struct daos_hhash	*daos_vos_hhash;

int
vos_create_hhash(void);

struct vos_pool*
vos_pool_lookup_handle(daos_handle_t poh);

/* Not yet implemented */
struct vos_pool*
vos_co_lookup_handle(daos_handle_t poh);


#endif
