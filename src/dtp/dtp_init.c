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
/**
 * This file is part of daos_transport. It implements dtp init and finalize
 * related APIs/handling.
 */

#include <dtp_internal.h>

struct dtp_gdata dtp_gdata;
static pthread_once_t gdata_init_once = PTHREAD_ONCE_INIT;
static volatile int   gdata_init_flag;

/* first step init - for initializing dtp_gdata */
static void data_init()
{
	int rc = 0;

	D_DEBUG(DF_TP, "initializing dtp_gdata...\n");

	DAOS_INIT_LIST_HEAD(&dtp_gdata.dg_ctx_list);

	rc = pthread_rwlock_init(&dtp_gdata.dg_rwlock, NULL);
	D_ASSERT(rc == 0);

	dtp_gdata.dg_ctx_num = 0;
	dtp_gdata.dg_refcount = 0;
	dtp_gdata.dg_inited = 0;

	gdata_init_flag = 1;
}

int
dtp_init(const dtp_phy_addr_t addr, bool server)
{
	int    rc = 0;

	D_DEBUG(DF_TP, "Enter dtp_init.\n");

	if (addr == NULL || strlen(addr) == 0) {
		D_ERROR("invalid parameter of info_string.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (gdata_init_flag == 0) {
		rc = pthread_once(&gdata_init_once, data_init);
		if (rc != 0) {
			D_ERROR("dtp_init failed, rc(%d) - %s.\n",
				rc, strerror(rc));
			D_GOTO(out, rc = -rc);
		}
	}
	D_ASSERT(gdata_init_flag == 1);

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
	if (dtp_gdata.dg_inited == 0) {
		dtp_gdata.dg_self_addr = strdup(addr);
		dtp_gdata.dg_server = server;

		rc = dtp_hg_init(addr, server);
		if (rc != 0) {
			D_ERROR("dtp_hg_init failed rc: %d.\n", rc);
			D_GOTO(unlock, rc);
		}

		rc = dtp_opc_map_create(DTP_OPC_MAP_BITS,
					&dtp_gdata.dg_opc_map);
		if (rc != 0) {
			dtp_hg_fini();
			D_ERROR("dtp_opc_map_create failed rc: %d.\n", rc);
			D_GOTO(unlock, rc);
		}

		dtp_gdata.dg_inited = 1;
	}

	dtp_gdata.dg_refcount++;

unlock:
	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
out:
	D_DEBUG(DF_TP, "Exit dtp_init, rc: %d.\n", rc);
	return rc;
}

bool
dtp_initialized()
{
	return (gdata_init_flag == 1) && (dtp_gdata.dg_inited == 1);
}

int
dtp_finalize(void)
{
	int rc = 0, len;

	D_DEBUG(DF_TP, "Enter dtp_finalize.\n");

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);

	if (!dtp_initialized()) {
		D_ERROR("cannot finalize before initializing.\n");
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (dtp_gdata.dg_ctx_num > 0) {
		D_ASSERT(!dtp_context_empty(DTP_LOCKED));
		D_ERROR("cannot finalize before destroying all dtp contexts, "
			"current ctx_num(%d.).\n", dtp_gdata.dg_ctx_num);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc = -DER_NO_PERM);
	} else {
		D_ASSERT(dtp_context_empty(DTP_LOCKED));
	}

	dtp_gdata.dg_refcount--;
	if (dtp_gdata.dg_refcount == 0) {
		rc = dtp_hg_fini();
		if (rc != 0) {
			D_ERROR("dtp_hg_fini failed rc: %d.\n", rc);
			dtp_gdata.dg_refcount++;
			pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
			D_GOTO(out, rc);
		}

		D_ASSERT(dtp_gdata.dg_self_addr != NULL);
		len = strlen(dtp_gdata.dg_self_addr);
		D_FREE(dtp_gdata.dg_self_addr, len);
		dtp_gdata.dg_server = 0;

		dtp_opc_map_destroy(dtp_gdata.dg_opc_map);

		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

		rc = pthread_rwlock_destroy(&dtp_gdata.dg_rwlock);
		if (rc != 0) {
			D_ERROR("failed to destroy dtp_gdata.dg_rwlock, "
				"rc: %d.\n", rc);
			D_GOTO(out, rc = -rc);
		}

		/* allow the same program to re-initialize */
		dtp_gdata.dg_refcount = 0;
		dtp_gdata.dg_inited = 0;
		gdata_init_flag = 0;
	} else {
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
	}

out:
	D_DEBUG(DF_TP, "Exit dtp_finalize, rc: %d.\n", rc);
	return rc;
}
