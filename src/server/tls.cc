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
 * This file is part of the DAOS server. It implements thread-local storage
 * (TLS) for DAOS service threads.
 */

#include <pthread.h>

#include "dss_internal.h"

pthread_key_t dss_tls_key;

/*
 * Allocate TLS for a particular thread and store the pointer in a
 * thread-specific value which can be fetched at any time with dss_tls_get().
 */
struct dss_tls *
dss_tls_init()
{
	struct dss_tls	*tls;
	int		 rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	rc = pthread_setspecific(dss_tls_key, tls);
	if (rc) {
		D_ERROR("failed to initialize tls: %d\n", rc);
		D_FREE_PTR(tls);
		return NULL;
	}

	return tls;
}

/*
 * Free TLS for a particular thread. Called upon thread termination via the
 * pthread key destructor.
 */
void
dss_tls_fini(void *arg)
{
	struct dss_tls  *tls = (struct dss_tls  *)arg;

	D_FREE_PTR(tls);
}
