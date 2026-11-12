/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <abt.h>
#include <cmocka.h>

#include <daos_srv/vos.h>
#include <engine/srv_internal.h>

#include "dlck_ut_daos_mock.h"

/** sentinels */
struct bio_xs_context *const     Mock_xs_ctxt = (struct bio_xs_context *)0xdef0;

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
