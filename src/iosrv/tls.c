/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements thread-local storage
 * (TLS) for DAOS service threads.
 */
#define D_LOGFAC       DD_FAC(server)

#include <pthread.h>
#include "srv_internal.h"

/* The array remember all of registered module keys on one node. */
struct dss_module_key *dss_module_keys[DAOS_MODULE_KEYS_NR] = { NULL };

pthread_mutex_t dss_module_keys_lock = PTHREAD_MUTEX_INITIALIZER;

void
dss_register_key(struct dss_module_key *key)
{
	int i;

	D_MUTEX_LOCK(&dss_module_keys_lock);
	for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
		if (dss_module_keys[i] == NULL) {
			dss_module_keys[i] = key;
			key->dmk_index = i;
			break;
		}
	}
	D_MUTEX_UNLOCK(&dss_module_keys_lock);
	D_ASSERT(i < DAOS_MODULE_KEYS_NR);
}

void
dss_unregister_key(struct dss_module_key *key)
{
	if (key == NULL)
		return;
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	D_MUTEX_LOCK(&dss_module_keys_lock);
	dss_module_keys[key->dmk_index] = NULL;
	D_MUTEX_UNLOCK(&dss_module_keys_lock);
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
		D_ALLOC_ARRAY(dtls->dtls_values,
			      (int)ARRAY_SIZE(dss_module_keys));
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
		for (i = DAOS_MODULE_KEYS_NR - 1; i >= 0; i--) {
			struct dss_module_key *dmk = dss_module_keys[i];

			if (dmk != NULL && dtls->dtls_tag & dmk->dmk_tags) {
				D_ASSERT(dtls->dtls_values[i] != NULL);
				D_ASSERT(dmk->dmk_fini != NULL);
				dmk->dmk_fini(dtls, dmk, dtls->dtls_values[i]);
			}
		}
	}

	D_FREE(dtls->dtls_values);
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
		D_FREE(dtls);
		return NULL;
	}

	rc = pthread_setspecific(dss_tls_key, dtls);
	if (rc) {
		D_ERROR("failed to initialize tls: "DF_RC"\n", DP_RC(rc));
		dss_thread_local_storage_fini(dtls);
		D_FREE(dtls);
		return NULL;
	}

	return dtls;
}

/* Free DTC for a particular thread. */
void
dss_tls_fini(struct dss_thread_local_storage *dtls)
{
	dss_thread_local_storage_fini(dtls);
	D_FREE(dtls);
	pthread_setspecific(dss_tls_key, NULL);
}
