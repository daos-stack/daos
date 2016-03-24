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

/* The array remember all of registered module keys on one node. */
#define DAOS_MODULE_KEYS_NR 10
static struct dss_module_key *dss_module_keys[DAOS_MODULE_KEYS_NR] = { NULL };

pthread_mutex_t dss_module_keys_lock = PTHREAD_MUTEX_INITIALIZER;

void
dss_register_key(struct dss_module_key *key)
{
	int i;

	pthread_mutex_lock(&dss_module_keys_lock);
	for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
		if (dss_module_keys[i] == NULL) {
			dss_module_keys[i] = key;
			key->dmk_index = i;
			break;
		}
	}
	pthread_mutex_unlock(&dss_module_keys_lock);
	D_ASSERT(i < DAOS_MODULE_KEYS_NR);
}

void
dss_unregister_key(struct dss_module_key *key)
{
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	pthread_mutex_lock(&dss_module_keys_lock);
	dss_module_keys[key->dmk_index] = NULL;
	pthread_mutex_unlock(&dss_module_keys_lock);
}

/**
 * Init thread context
 *
 * \param[in]dtls	Init the thread context to allocate the
 *                      local thread variable for each module.
 *
 * \retval		0 if initialization succeeds
 * \retval		negative errno if initialization fails
 */
static int
dss_thread_local_storage_init(struct dss_thread_local_storage *dtls)
{
	int rc = 0;
	int i;

	if (dtls->dtls_values == NULL) {
		D_ALLOC(dtls->dtls_values, ARRAY_SIZE(dss_module_keys) *
					 sizeof(dtls->dtls_values[0]));
		if (dtls->dtls_values == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
		struct dss_module_key *dmk = dss_module_keys[i];

		if (dmk != NULL && dtls->dtls_tag & dmk->dmk_tags) {
			D_ASSERT(dmk->dmk_init != NULL);
			dtls->dtls_values[i] = dmk->dmk_init(dtls, dmk);
			if (dtls->dtls_values[i] == NULL) {
				rc = -DER_NOMEM;
				break;
			}
		}
	}
	return rc;
}

/**
 * Finish module context
 *
 * \param[in]dtls	Finish the thread context to free the
 *                      local thread variable for each module.
 */
static void
dss_thread_local_storage_fini(struct dss_thread_local_storage *dtls)
{
	int i;

	if (dtls->dtls_values != NULL) {
		for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
			struct dss_module_key *dmk = dss_module_keys[i];

			if (dtls->dtls_values[i] != NULL) {
				D_ASSERT(dmk != NULL);
				D_ASSERT(dmk->dmk_fini != NULL);
				dmk->dmk_fini(dtls, dmk, dtls->dtls_values[i]);
			}
		}
	}

	D_FREE(dtls->dtls_values,
	       ARRAY_SIZE(dss_module_keys) * sizeof(dtls->dtls_values[0]));
}

/**
 * Get value from context by the key
 *
 * Get value inside dtls by key. So each module will use this API to
 * retrieve their own value in the thread context.
 *
 * \param[in] dtls	the thread context.
 * \param[in] key	key used to retrieve the dtls_value.
 *
 * \retval		the dtls_value retrieved by key.
 */
void *
dss_module_key_get(struct dss_thread_local_storage *dtls,
		   struct dss_module_key *key)
{
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	D_ASSERT(dss_module_keys[key->dmk_index] == key);

	return dtls->dtls_values[key->dmk_index];
}

pthread_key_t dss_tls_key;

/*
 * Allocate dss_thread_local_storage for a particular thread and
 * store the pointer in a thread-specific value which can be
 * fetched at any time with dss_tls_get().
 */
struct dss_thread_local_storage *
dss_tls_init(int tag)
{
	struct dss_thread_local_storage *dtls;
	int		 rc;

	D_ALLOC_PTR(dtls);
	if (dtls == NULL)
		return NULL;

	dtls->dtls_tag = tag;
	rc = dss_thread_local_storage_init(dtls);
	if (rc != 0) {
		D_FREE_PTR(dtls);
		return NULL;
	}

	rc = pthread_setspecific(dss_tls_key, dtls);
	if (rc) {
		D_ERROR("failed to initialize tls: %d\n", rc);
		dss_thread_local_storage_fini(dtls);
		D_FREE_PTR(dtls);
		return NULL;
	}

	return dtls;
}

/*
 * Free DTC for a particular thread. Called upon thread termination via the
 * pthread key destructor.
 */
void
dss_tls_fini(void *arg)
{
	struct dss_thread_local_storage  *dtls;

	dtls = (struct dss_thread_local_storage  *)arg;
	dss_thread_local_storage_fini(dtls);
}
