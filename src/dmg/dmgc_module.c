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
 * dmgc: the DMG client module/library. It exports the DMG API defined
 *       in daos_mgmt.h
 */

#include "dmgc_internal.h"

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static int		module_initialized;

int
dmg_init()
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = daos_eq_lib_init();
	if (rc != 0)
		D_GOTO(unlock, rc);

	rc = daos_rpc_register(dmg_rpcs, NULL, DAOS_DMG_MODULE);
	if (rc != 0) {
		daos_eq_lib_fini();
		D_GOTO(unlock, rc);
	}

	module_initialized = 1;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}

bool
dmg_initialized()
{
	return (module_initialized != 0);
}

int
dmg_fini()
{
	int	rc;

	pthread_mutex_lock(&module_lock);
	if (!dmg_initialized())
		D_GOTO(unlock, rc = -DER_UNINIT);

	daos_rpc_unregister(dmg_rpcs);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	module_initialized = 0;

unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}
