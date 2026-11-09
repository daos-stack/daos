/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <abt.h>
#include <cmocka.h>

#include <daos_srv/vos.h>
#include <engine/srv_internal.h>

#include "dlck_engine_ut_mock.h"

/** sentinels */
const ABT_pool                   Mock_pool     = (ABT_pool)0x1234;
const ABT_thread                 Mock_thread   = (ABT_thread)0x5678;
const ABT_eventual               Mock_eventual = (ABT_eventual)0x9abc;
struct bio_xs_context *const     Mock_xs_ctxt  = (struct bio_xs_context *)0xdef0;

/** thread-local storage shared by all mocked execution streams */
struct dss_module_info           Mock_dmi;
static void                     *Mock_dtls_values[DAOS_MODULE_KEYS_NR] = {&Mock_dmi};
struct daos_thread_local_storage Mock_dtls = {.dtls_values = Mock_dtls_values};

int
mock_sm_init(void)
{
	return mock_type(int);
}

int
mock_sm_fini(void)
{
	return mock_type(int);
}

/** module data normally provided by dlck_tls.c, the engine, and the VOS server module */
struct dss_module_key daos_srv_modkey = {.dmk_tags = DAOS_SERVER_TAG, .dmk_index = 0};
struct dss_module_key vos_module_key  = {.dmk_tags = DAOS_SERVER_TAG, .dmk_index = 1};
struct dss_module     vos_srv_module  = {.sm_init = mock_sm_init, .sm_fini = mock_sm_fini};

/** allocation mocks */

void *
__wrap_d_calloc(size_t nmemb, size_t size)
{
	check_expected(nmemb);
	check_expected(size);

	void *ptr = mock_ptr_type(void *);

	if (ptr != NULL) {
		memset(ptr, 0, nmemb * size);
	}

	return ptr;
}

void
__wrap_d_free(void *ptr)
{
	check_expected_ptr(ptr);
}

/** TLS mocks */

struct daos_thread_local_storage *
__wrap_dss_tls_init(int tag, int xs_id, int tgt_id)
{
	check_expected(tag);
	check_expected(xs_id);
	check_expected(tgt_id);

	return mock_ptr_type(struct daos_thread_local_storage *);
}

void
__wrap_dss_tls_fini(struct daos_thread_local_storage *dtls)
{
	check_expected_ptr(dtls);
}

struct daos_thread_local_storage *
__wrap_dss_tls_get(void)
{
	return mock_ptr_type(struct daos_thread_local_storage *);
}

int
__wrap_ds_tls_key_create(void)
{
	return mock_type(int);
}

void
__wrap_ds_tls_key_delete(void)
{
	function_called();
}

void
__wrap_daos_register_key(struct daos_module_key *key)
{
	check_expected_ptr(key);
}

void
__wrap_daos_unregister_key(struct daos_module_key *key)
{
	check_expected_ptr(key);
}

struct daos_module_key *
__wrap_daos_get_module_key(int index)
{
	check_expected(index);

	return mock_ptr_type(struct daos_module_key *);
}

/** ABT mocks */

int
__wrap_ABT_thread_join(ABT_thread thread)
{
	check_expected_ptr(thread);

	return mock_type(int);
}

int
__wrap_ABT_thread_free(ABT_thread *thread)
{
	assert_non_null(thread);

	ABT_thread handle = *thread;

	check_expected_ptr(handle);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*thread = ABT_THREAD_NULL;
	}

	return rc;
}

int
__wrap_ABT_thread_yield(void)
{
	return mock_type(int);
}

/**
 * The production code asserts on the return code so only ABT_SUCCESS is sensible here.
 * The emptiness result is always queue-controlled.
 */
int
__wrap_ABT_pool_is_empty(ABT_pool pool, ABT_bool *is_empty)
{
	check_expected_ptr(pool);
	assert_non_null(is_empty);
	*is_empty = mock_type(ABT_bool);

	return ABT_SUCCESS;
}

int
__wrap_ABT_eventual_create(int nbytes, ABT_eventual *neweventual)
{
	assert_non_null(neweventual);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*neweventual = mock_ptr_type(ABT_eventual);
	}

	return rc;
}

int
__wrap_ABT_eventual_free(ABT_eventual *eventual)
{
	assert_non_null(eventual);

	ABT_eventual handle = *eventual;

	check_expected_ptr(handle);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*eventual = ABT_EVENTUAL_NULL;
	}

	return rc;
}

int
__wrap_ABT_eventual_set(ABT_eventual eventual, void *value, int nbytes)
{
	check_expected_ptr(eventual);

	return mock_type(int);
}

int
__wrap_ABT_eventual_test(ABT_eventual eventual, void **value, ABT_bool *is_ready)
{
	check_expected_ptr(eventual);
	assert_non_null(is_ready);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*is_ready = mock_type(ABT_bool);
	}

	return rc;
}

/** engine mocks */

int
dss_register_dbtree_classes(void)
{
	return mock_type(int);
}

/** BIO mocks */

int
bio_nvme_init(const char *nvme_conf, int numa_node, unsigned int mem_size,
	      unsigned int hugepage_size, unsigned int tgt_nr, bool bypass)
{
	check_expected(nvme_conf);
	check_expected(numa_node);
	check_expected(mem_size);
	check_expected(hugepage_size);
	check_expected(tgt_nr);
	assert_false(bypass);

	return mock_type(int);
}

void
bio_nvme_fini(void)
{
	function_called();
}

bool
bio_nvme_configured(enum smd_dev_type type)
{
	check_expected(type);

	return mock_type(bool);
}

int
bio_nvme_poll(struct bio_xs_context *ctxt)
{
	check_expected_ptr(ctxt);

	return mock_type(int);
}

int
bio_xsctxt_alloc(struct bio_xs_context **pctxt, int tgt_id, bool self_polling)
{
	assert_non_null(pctxt);
	check_expected(tgt_id);
	assert_false(self_polling);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		*pctxt = mock_ptr_type(struct bio_xs_context *);
	}

	return rc;
}

void
bio_xsctxt_free(struct bio_xs_context *ctxt)
{
	check_expected_ptr(ctxt);
}

/** VOS mocks */

int
vos_standalone_tls_init(int tags)
{
	check_expected(tags);

	return mock_type(int);
}

void
vos_standalone_tls_fini(void)
{
	function_called();
}

int
vos_sys_db_init(const char *nvme_conf, const char *storage_path)
{
	check_expected(nvme_conf);
	check_expected(storage_path);

	return mock_type(int);
}

void
vos_db_fini(void)
{
	function_called();
}

/** dlck_abt.c replacements */

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		xs->pool = mock_ptr_type(ABT_pool);
	}

	return rc;
}

int
dlck_xstream_free(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	return mock_type(int);
}

/**
 * On success the created ULT is executed synchronously so that all its effects are observable
 * right after the call. The tests have to queue all the values consumed by the ULT body.
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult)
{
	check_expected_ptr(pool);
	assert_non_null(func);
	assert_non_null(ult);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		ult->thread = Mock_thread;
		func(arg);
	}

	return rc;
}
