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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsmc: Internal Declarations
 */

#ifndef __DSMC_INTERNAL_H__
#define __DSMC_INTERNAL_H__

#include <daos/transport.h>

/*
 * Client pool handle
 *
 * This is called a connection to avoid potential confusion with the server
 * pool_hdl.
 */
struct pool_conn {
	uuid_t		pc_pool;
	uuid_t		pc_pool_hdl;
	uint64_t	pc_capas;
};

#endif /* __DSMC_INTERNAL_H__ */
