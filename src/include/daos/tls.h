/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/tls.h
 */

#ifndef __DAOS_TLS_H__
#define __DAOS_TLS_H__

#include <daos/common.h>
#include <daos_types.h>

/**
 * Stackable Module API
 * Provides a modular interface to load and register server-side code on
 * demand. A module is composed of:
 * - a set of request handlers which are registered when the module is loaded.
 * - a server-side API (see header files suffixed by "_srv") used for
 *   inter-module direct calls.
 *
 * For now, all loaded modules are assumed to be trustful, but sandboxes can be
 * implemented in the future.
 */
/*
 * Thead-local storage
 */
struct daos_thread_local_storage {
	uint32_t dtls_tag;
	void   **dtls_values;
};

enum daos_module_tag {
	DAOS_SYS_TAG    = 1 << 0, /** only run on system xstream */
	DAOS_TGT_TAG    = 1 << 1, /** only run on target xstream */
	DAOS_RDB_TAG    = 1 << 2, /** only run on rdb xstream */
	DAOS_OFF_TAG    = 1 << 3, /** only run on offload/helper xstream */
	DAOS_CLI_TAG    = 1 << 4, /** only run on client stack */
	DAOS_SERVER_TAG = 0xff,   /** run on all xstream */
};

/* The module key descriptor for each xstream */
struct daos_module_key {
	/* Indicate where the keys should be instantiated */
	enum daos_module_tag dmk_tags;

	/* The position inside the daos_module_keys */
	int                  dmk_index;
	/* init keys for context */
	void *(*dmk_init)(int tags, int xs_id, int tgt_id);

	/* fini keys for context */
	void (*dmk_fini)(int tags, void *data);
};

#define DAOS_MODULE_KEYS_NR 10
struct daos_thread_local_storage *
dss_tls_get(void);
struct daos_thread_local_storage *
dc_tls_get(unsigned int tag);

int
ds_tls_key_create(void);
int
dc_tls_key_create(void);
void
ds_tls_key_delete(void);
void
dc_tls_key_delete(void);

struct daos_module_key *
daos_get_module_key(int index);

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
static inline void *
daos_module_key_get(struct daos_thread_local_storage *dtls, struct daos_module_key *key)
{
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	D_ASSERT(daos_get_module_key(key->dmk_index) == key);
	D_ASSERT(dtls != NULL);

	return dtls->dtls_values[key->dmk_index];
}

#define dss_module_key_get       daos_module_key_get
#define dss_register_key         daos_register_key
#define dss_unregister_key       daos_unregister_key
#define dss_module_info          daos_module_info
#define dss_module_tag           daos_module_tag
#define dss_module_key           daos_module_key
#define dss_thread_local_storage daos_thread_local_storage

void
daos_register_key(struct daos_module_key *key);
void
daos_unregister_key(struct daos_module_key *key);
struct daos_thread_local_storage *
dc_tls_init(int tag, uint32_t pid);
void
dc_tls_fini(void);
struct daos_thread_local_storage *
dss_tls_init(int tag, int xs_id, int tgt_id);
void
dss_tls_fini(struct daos_thread_local_storage *dtls);

#endif /*__DAOS_TLS_H__*/
