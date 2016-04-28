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
 * dmgc: the DMG client module/library. It exports the DSM API defined
 *       in daos_mgmt.h
 */

#include <pthread.h>
#include <daos/rpc.h>
#include <daos/transport.h>

#include "dmg_rpc.h"

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static int		module_initialized;

/**
 *   Initialize dmgc client library.
 *
 *   This function will register the RPCs.
 */
int
dmg_init()
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = 0);

	rc = daos_rpc_register(dmg_rpcs, NULL, DAOS_DMG_MODULE);
	if (rc != 0) {
		D_ERROR("rpc register failure: rc = %d\n", rc);
		D_GOTO(unlock, rc);
	}

	module_initialized = 1;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}

/**
 * Finalize dmgc client.
 */
int
dmg_fini()
{
	pthread_mutex_lock(&module_lock);
	if (!module_initialized) {
		pthread_mutex_unlock(&module_lock);
		return 0;
	}

	daos_rpc_unregister(dmg_rpcs);

	module_initialized = 0;
	pthread_mutex_unlock(&module_lock);

	return 0;
}
